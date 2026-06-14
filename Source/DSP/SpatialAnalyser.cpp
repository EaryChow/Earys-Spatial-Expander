#include "SpatialAnalyser.h"

void SpatialAnalyser::prepare (int fftSize, double sampleRate)
{
    fftSize_ = fftSize;
    sampleRate_ = static_cast<float> (sampleRate);
    numPositiveBins = fftSize / 2 + 1;

    binMetrics.clear();
    binMetrics.resize (numPositiveBins);
    prevMagnitudes.clear();
    prevMagnitudes.resize (numPositiveBins, 0.0f);

    hopMs_ = static_cast<float> (fftSize / 2) / static_cast<float> (sampleRate) * 1000.0f;
    deathFrames_ = static_cast<int> (deathMs / hopMs_ + 0.5f);

    numObjects = 0;
    numPrevObjects = 0;
}

void SpatialAnalyser::reset()
{
    if (! prevMagnitudes.empty())
        std::fill (prevMagnitudes.begin(), prevMagnitudes.end(), 0.0f);
    numObjects = 0;
    numPrevObjects = 0;
    nextId = 1;
}

void SpatialAnalyser::onFrame (float* fftBufL, float* fftBufR, int /*fftSize*/)
{
    computeBinMetrics (fftBufL, fftBufR);
    groupBins();
    harmonicMerge();
    trackObjects();
    updateLifecycle();
}

// ── Per-bin metrics ──────────────────────────────────────────────────────

void SpatialAnalyser::computeBinMetrics (const float* fftBufL, const float* fftBufR)
{
    auto numBins = static_cast<size_t> (numPositiveBins);

    for (size_t k = 0; k < numBins; ++k)
    {
        float realL = fftBufL[2 * k];
        float imagL = fftBufL[2 * k + 1];
        float realR = fftBufR[2 * k];
        float imagR = fftBufR[2 * k + 1];

        float magL = std::sqrt (realL * realL + imagL * imagL);
        float magR = std::sqrt (realR * realR + imagR * imagR);

        float ild = 20.0f * std::log10 ((magL + 1e-20f) / (magR + 1e-20f));

        float crossReal = realL * realR + imagL * imagR;
        float crossImag = imagL * realR - realL * imagR;
        float crossMag = std::sqrt (crossReal * crossReal + crossImag * crossImag);
        float correlation = crossMag / ((magL * magR) + 1e-20f);

        float phaseL = std::atan2 (imagL, realL);
        float phaseR = std::atan2 (imagR, realR);
        float phaseDiff = phaseL - phaseR;
        while (phaseDiff > juce::MathConstants<float>::pi)
            phaseDiff -= 2.0f * juce::MathConstants<float>::pi;
        while (phaseDiff < -juce::MathConstants<float>::pi)
            phaseDiff += 2.0f * juce::MathConstants<float>::pi;

        float magnitude = magL + magR;
        float flux = 0.0f;
        if (k < prevMagnitudes.size())
            flux = std::abs (magnitude - prevMagnitudes[k]);

        binMetrics[k] = { magL, magR, ild, correlation, phaseDiff, flux };
    }

    prevMagnitudes.resize (numBins);
    for (size_t k = 0; k < numBins; ++k)
        prevMagnitudes[k] = binMetrics[k].magnitudeL + binMetrics[k].magnitudeR;
}

// ── Helpers for filling object metrics ───────────────────────────────────

static float computeTotalEnergy (const std::vector<BinMetrics>& metrics, int start, int end)
{
    float energy = 0.0f;
    for (int b = start; b <= end; ++b)
    {
        float mag = metrics[b].magnitudeL + metrics[b].magnitudeR;
        energy += mag * mag;
    }
    return energy;
}

static void fillObjectMetrics (SpatialObject& obj, const std::vector<BinMetrics>& metrics,
                               int start, int end)
{
    obj.startBin = start;
    obj.endBin = end;
    obj.id = -1;
    obj.ageFrames = 0;
    obj.alive = true;
    obj.deathCountdown = -1;
    obj.smoothedGain = 0.0f;
    obj.claimed = false;

    float totalMag = 0.0f;
    float ildSum = 0.0f;
    float corrSum = 0.0f;
    float centroidSum = 0.0f;
    float energy = 0.0f;

    for (int b = start; b <= end; ++b)
    {
        float mag = metrics[b].magnitudeL + metrics[b].magnitudeR;
        totalMag += mag;
        ildSum += metrics[b].ild * mag;
        corrSum += metrics[b].correlation * mag;
        centroidSum += static_cast<float> (b) * mag;
        energy += mag * mag;
    }

    if (totalMag > 1e-10f)
    {
        ildSum /= totalMag;
        corrSum /= totalMag;
        centroidSum /= totalMag;
    }

    obj.meanILD = ildSum;
    obj.meanCorr = corrSum;
    obj.spectralCentroid = centroidSum;
    obj.totalEnergy = energy;
    obj.bandwidth = static_cast<float> (end - start);
}

// ── Grouping with overflow handling ──────────────────────────────────────

void SpatialAnalyser::groupBins()
{
    numStaging = 0;
    if (numPositiveBins < 2)
        return;

    int startIdx = 0;

    for (int k = 1; k < numPositiveBins; ++k)
    {
        float ildDiff = std::abs (binMetrics[k].ild - binMetrics[k - 1].ild);
        float corrDiff = std::abs (binMetrics[k].correlation - binMetrics[k - 1].correlation);

        float corr = binMetrics[k - 1].correlation;
        float ildThresh;
        if (corr > 0.9f)
            ildThresh = 4.0f;
        else if (corr < 0.5f)
            ildThresh = 1.0f;
        else
            ildThresh = 2.5f;

        bool shouldBreak = ! (ildDiff < ildThresh && corrDiff < 0.2f);

        if (shouldBreak)
        {
            finalizeObject (startIdx, k - 1);
            startIdx = k;
        }
    }

    if (startIdx < numPositiveBins)
        finalizeObject (startIdx, numPositiveBins - 1);
}

void SpatialAnalyser::finalizeObject (int start, int end)
{
    if (numStaging < maxObjects)
    {
        auto& obj = staging[numStaging];
        fillObjectMetrics (obj, binMetrics, start, end);
        ++numStaging;
    }
    else
    {
        int weakest = 0;
        float weakestEnergy = staging[0].totalEnergy;
        for (int i = 1; i < maxObjects; ++i)
        {
            if (staging[i].totalEnergy < weakestEnergy)
            {
                weakest = i;
                weakestEnergy = staging[i].totalEnergy;
            }
        }
        float newEnergy = computeTotalEnergy (binMetrics, start, end);
        if (newEnergy > weakestEnergy)
            fillObjectMetrics (staging[weakest], binMetrics, start, end);
    }
}

// ── Harmonic merge ───────────────────────────────────────────────────────

void SpatialAnalyser::harmonicMerge()
{
    if (numStaging < 2)
        return;

    int writeIdx = 0;

    for (int readIdx = 0; readIdx < numStaging; ++readIdx)
    {
        if (writeIdx > 0)
        {
            auto& prev = staging[writeIdx - 1];
            auto& curr = staging[readIdx];

            float ratio = curr.spectralCentroid / (prev.spectralCentroid + 1e-10f);
            float ratioRounded = std::round (ratio);

            if (std::abs (ratio - ratioRounded) < 0.15f
                && prev.meanCorr > 0.8f
                && curr.meanCorr > 0.8f)
            {
                float prevE = prev.totalEnergy;
                float currE = curr.totalEnergy;
                float totalE = prevE + currE;

                if (totalE > 1e-10f)
                {
                    prev.meanILD = (prev.meanILD * prevE + curr.meanILD * currE) / totalE;
                    prev.meanCorr = (prev.meanCorr * prevE + curr.meanCorr * currE) / totalE;
                    prev.spectralCentroid = (prev.spectralCentroid * prevE + curr.spectralCentroid * currE) / totalE;
                    prev.totalEnergy = totalE;
                }

                prev.endBin = curr.endBin;
                continue;
            }
        }

        if (writeIdx != readIdx)
            staging[writeIdx] = staging[readIdx];
        ++writeIdx;
    }

    numStaging = writeIdx;
}

// ── Frame-to-frame tracking ──────────────────────────────────────────────

static float matchScore (const SpatialObject& a, const SpatialObject& b, float maxBin)
{
    float ildDiff = std::abs (a.meanILD - b.meanILD);
    float centDiff = std::abs (a.spectralCentroid - b.spectralCentroid);
    float bwDiff = std::abs (a.bandwidth - b.bandwidth);

    float normIld = ildDiff / 30.0f;
    float normCent = centDiff / maxBin;
    float normBw = bwDiff / maxBin;

    return 0.40f * normIld + 0.35f * normCent + 0.25f * normBw;
}

void SpatialAnalyser::trackObjects()
{
    float maxBin = static_cast<float> (numPositiveBins);
    constexpr float matchThreshold = 0.30f;

    for (int i = 0; i < numPrevObjects; ++i)
        prevObjects[i].claimed = false;

    for (int s = 0; s < numStaging; ++s)
    {
        int bestIdx = -1;
        float bestScore = matchThreshold;

        for (int p = 0; p < numPrevObjects; ++p)
        {
            if (! prevObjects[p].alive || prevObjects[p].claimed)
                continue;

            float score = matchScore (staging[s], prevObjects[p], maxBin);
            if (score < bestScore)
            {
                bestScore = score;
                bestIdx = p;
            }
        }

        if (bestIdx >= 0)
        {
            staging[s].id = prevObjects[bestIdx].id;
            staging[s].ageFrames = prevObjects[bestIdx].ageFrames + 1;
            prevObjects[bestIdx].claimed = true;
        }
        else
        {
            staging[s].id = nextId++;
            staging[s].ageFrames = 0;
        }
    }
}

// ── Lifecycle update ─────────────────────────────────────────────────────

void SpatialAnalyser::updateLifecycle()
{
    SpatialObject newPool[maxObjects];
    int newCount = 0;

    // 1. Carried-over: previously alive objects that were claimed (matched)
    for (int p = 0; p < numPrevObjects; ++p)
    {
        if (! prevObjects[p].alive)
            continue;

        if (prevObjects[p].claimed)
        {
            int sidx = -1;
            for (int s = 0; s < numStaging; ++s)
            {
                if (staging[s].id == prevObjects[p].id)
                {
                    sidx = s;
                    break;
                }
            }
            if (sidx >= 0 && newCount < maxObjects)
            {
                newPool[newCount] = staging[sidx];
                float ageMs = newPool[newCount].ageFrames * hopMs_;
                float ramp = std::min (1.0f, ageMs / 20.0f);
                newPool[newCount].smoothedGain = ramp;
                ++newCount;
            }
        }
        else
        {
            SpatialObject dying = prevObjects[p];
            if (dying.deathCountdown < 0)
                dying.deathCountdown = deathFrames_;
            else
                --dying.deathCountdown;

            if (dying.deathCountdown >= 0 && newCount < maxObjects)
            {
                float deathAgeMs = static_cast<float> (deathFrames_ - dying.deathCountdown) * hopMs_;
                float ramp = std::max (0.0f, 1.0f - deathAgeMs / deathMs);
                dying.smoothedGain = prevObjects[p].smoothedGain * ramp;
                newPool[newCount] = dying;
                ++newCount;
            }
        }
    }

    // 2. New objects — unmatched staging entries
    for (int s = 0; s < numStaging; ++s)
    {
        bool isNew = true;
        for (int p = 0; p < numPrevObjects; ++p)
        {
            if (prevObjects[p].alive && prevObjects[p].claimed
                && prevObjects[p].id == staging[s].id)
            {
                isNew = false;
                break;
            }
        }

        if (isNew && newCount < maxObjects)
        {
            SpatialObject& obj = staging[s];
            obj.deathCountdown = -1;
            obj.smoothedGain = 0.0f;
            newPool[newCount] = obj;
            ++newCount;
        }
    }

    // 3. Handle overflow in newPool — overwrite weakest
    while (newCount > maxObjects)
    {
        int weakest = 0;
        for (int i = 1; i < newCount; ++i)
            if (newPool[i].totalEnergy < newPool[weakest].totalEnergy)
                weakest = i;
        for (int i = weakest; i < newCount - 1; ++i)
            newPool[i] = newPool[i + 1];
        --newCount;
    }

    for (int i = 0; i < newCount; ++i)
        objects[i] = newPool[i];
    numObjects = newCount;

    for (int i = 0; i < numObjects; ++i)
        prevObjects[i] = objects[i];
    numPrevObjects = numObjects;
}
