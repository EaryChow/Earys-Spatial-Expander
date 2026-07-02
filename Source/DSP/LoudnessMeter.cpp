#include "LoudnessMeter.h"
#include <cmath>

void LoudnessMeter::prepare (double sr, int maxCh)
{
    sampleRate = sr;
    maxChannels = juce::jmax (maxCh, 16);

    stage1.assign (maxChannels, Biquad{});
    stage2.assign (maxChannels, Biquad{});
    channelWeights.assign (maxChannels, 0.0);
    chSqSum.resize (maxChannels);

    momentaryWindowSize = static_cast<int> (std::ceil (sr * 0.4));
    momentaryWindow.assign (momentaryWindowSize, 0.0);

    shortWindowSize = static_cast<int> (std::ceil (sr * 3.0));
    shortWindow.assign (shortWindowSize, 0.0);

    blockSizeSamples = static_cast<int> (std::ceil (sr * 0.4));

    reset();
    designKFilter (sr);
}

void LoudnessMeter::reset()
{
    for (auto& b : stage1) b.reset();
    for (auto& b : stage2) b.reset();

    std::fill (momentaryWindow.begin(), momentaryWindow.end(), 0.0);
    momentaryWritePos = 0;
    momentarySamplesFilled = 0;
    momentaryRunningSum = 0.0;
    momentaryLoudness.store (-70.0);

    std::fill (shortWindow.begin(), shortWindow.end(), 0.0);
    shortWritePos = 0;
    shortSamplesFilled = 0;
    shortRunningSum = 0.0;
    shortTermLoudness.store (-70.0);
    shortTermMax.store (-70.0);

    integratedHistory.clear();
    currentBlockPowerSum = 0.0;
    currentBlockSamples = 0;
    integratedLoudness.store (-70.0);

    channelDebugInfo.assign (maxChannels, ChannelDebugInfo{});
}

void LoudnessMeter::setPaused (bool p) { paused.store (p); }
bool LoudnessMeter::isPaused() const noexcept { return paused.load(); }

void LoudnessMeter::clearHistory()
{
    shortTermMax.store (-70.0);
    integratedHistory.clear();
    integratedLoudness.store (-70.0);
}

void LoudnessMeter::designKFilter (double sr)
{
    auto bilinearBiquad = [] (double b2_a, double b1_a, double b0_a,
                              double a2_a, double a1_a, double a0_a,
                              double sr,
                              double& b0, double& b1, double& b2,
                              double& a1, double& a2)
    {
        double c  = 2.0 * sr;
        double c2 = c * c;

        double num0 = b2_a * c2 + b1_a * c + b0_a;
        double num1 = -2.0 * b2_a * c2 + 2.0 * b0_a;
        double num2 = b2_a * c2 - b1_a * c + b0_a;

        double den0 = a2_a * c2 + a1_a * c + a0_a;
        double den1 = -2.0 * a2_a * c2 + 2.0 * a0_a;
        double den2 = a2_a * c2 - a1_a * c + a0_a;

        b0 = num0 / den0;
        b1 = num1 / den0;
        b2 = num2 / den0;
        a1 = den1 / den0;
        a2 = den2 / den0;
    };

    double b0_1, b1_1, b2_1, a1_1, a2_1;
    double b0_2, b1_2, b2_2, a1_2, a2_2;

    if (sr == 48000.0)
    {
        b0_1 =  1.53512485958697; b1_1 = -2.69169618940638; b2_1 =  1.19839281085285;
        a1_1 = -1.69065929318241; a2_1 =  0.73248077421585;

        b0_2 =  1.0;              b1_2 = -2.0;              b2_2 =  1.0;
        a1_2 = -1.99004745483398; a2_2 =  0.99007225036621;
    }
    else if (sr == 44100.0)
    {
        b0_1 =  1.531434499312139; b1_1 = -2.652311075972248; b2_1 =  1.169767326192515;
        a1_1 = -1.663901800496320; a2_1 =  0.712774858093634;

        b0_2 =  0.999564193543748; b1_2 = -1.999128387087495; b2_2 =  0.999564193543748;
        a1_2 = -1.989169677772349; a2_2 =  0.989199039907131;
    }
    else if (sr == 96000.0)
    {
        b0_1 =  1.560203182014715; b1_1 = -2.927074199178258; b2_1 =  1.378174806459022;
        a1_1 = -1.844138864893892; a2_1 =  0.855438563724233;

        b0_2 =  1.002496924130377; b1_2 = -2.004993848260754; b2_2 =  1.002496924130377;
        a1_2 = -1.995017536974399; a2_2 =  0.995023751309940;
    }
    else
    {
        double z1r = -5958.525786, z1i = 5961.50810662;
        double p1r = -7502.42326333, p1i = 7503.87582837;
        double g1  =  1.5854384185284838;

        bilinearBiquad (g1, -g1 * (2.0 * z1r), g1 * (z1r * z1r + z1i * z1i),
                        1.0, -(2.0 * p1r), (p1r * p1r + p1i * p1i),
                        sr, b0_1, b1_1, b2_1, a1_1, a2_1);

        double p2r = -239.4561057, p2i = 8.66214753;
        double g2  =  1.0049990480789601;

        bilinearBiquad (g2, 0.0, 0.0,
                        1.0, -(2.0 * p2r), (p2r * p2r + p2i * p2i),
                        sr, b0_2, b1_2, b2_2, a1_2, a2_2);
    }

    for (auto& bq : stage1)
    {
        bq.b0 = b0_1; bq.b1 = b1_1; bq.b2 = b2_1; bq.a1 = a1_1; bq.a2 = a2_1;
    }
    for (auto& bq : stage2)
    {
        bq.b0 = b0_2; bq.b1 = b1_2; bq.b2 = b2_2; bq.a1 = a1_2; bq.a2 = a2_2;
    }
}

double LoudnessMeter::getChannelWeight (juce::AudioChannelSet::ChannelType type,
                                        int chIndex, int outputFormat,
                                        const juce::AudioChannelSet& layout)
{
    using CT = juce::AudioChannelSet::ChannelType;
    (void) outputFormat;

    if (type == CT::LFE) return 0.0;

    if (type == CT::left || type == CT::right || type == CT::centre)
        return 1.0;

    if (type == CT::wideLeft || type == CT::wideRight)
        return 1.41;

    if (type == CT::leftSurroundSide || type == CT::rightSurroundSide)
        return 1.41;

    if (type == CT::leftSurroundRear || type == CT::rightSurroundRear)
        return 1.0;

    if (type == CT::leftCentre || type == CT::rightCentre)
    {
        bool hasExplicitWide = false;
        for (int ch = 0; ch < layout.size(); ++ch)
        {
            auto t = layout.getTypeOfChannel (ch);
            if (t == CT::wideLeft || t == CT::wideRight)
            {
                hasExplicitWide = true;
                break;
            }
        }
        if (hasExplicitWide)
            return 1.0;

        int lcIndex = -1, lsIndex = -1;
        for (int ch = 0; ch < layout.size(); ++ch)
        {
            auto t = layout.getTypeOfChannel (ch);
            if (lcIndex < 0 && (t == CT::leftCentre || t == CT::rightCentre))
                lcIndex = ch;
            if (lsIndex < 0 && (t == CT::leftSurround || t == CT::leftSurroundSide))
                lsIndex = ch;
        }
        if (lcIndex > lsIndex && lsIndex >= 0 && layout.size() >= 10)
            return 1.41;

        return 1.0;
    }

    if (type == CT::leftSurround || type == CT::rightSurround)
    {
        bool hasSide = false;
        bool hasRear = false;
        for (int ch = 0; ch < layout.size(); ++ch)
        {
            auto t = layout.getTypeOfChannel (ch);
            if (t == CT::leftSurroundSide || t == CT::rightSurroundSide) hasSide = true;
            if (t == CT::leftSurroundRear || t == CT::rightSurroundRear) hasRear = true;
        }

        if (hasSide && !hasRear)      return 1.0;
        if (!hasSide && hasRear)      return 1.41;
        return 1.41;
    }

    if (type == CT::topFrontLeft  || type == CT::topFrontRight ||
        type == CT::topFrontCentre|| type == CT::topRearLeft    ||
        type == CT::topRearRight  || type == CT::topSideLeft    ||
        type == CT::topSideRight)
        return 1.0;

    if (type == CT::unknown)
    {
        int n = layout.size();
        int bedChannels = n;
        for (int ch = n - 1; ch >= 0; --ch)
        {
            auto t = layout.getTypeOfChannel (ch);
            if (t == CT::topFrontLeft || t == CT::topFrontRight || t == CT::topFrontCentre ||
                t == CT::topRearLeft  || t == CT::topRearRight  || t == CT::topSideLeft ||
                t == CT::topSideRight)
                --bedChannels;
            else
                break;
        }

        if (bedChannels <= 3)
        {
            if (chIndex <= 2) return 1.0;
        }
        else if (bedChannels <= 6)
        {
            if (chIndex <= 2) return 1.0;
            if (chIndex == 3) return 0.0;
            if (chIndex >= 4) return 1.41;
        }
        else if (bedChannels <= 8)
        {
            if (chIndex <= 2) return 1.0;
            if (chIndex == 3) return 0.0;
            if (chIndex == 4 || chIndex == 5) return 1.41;
            if (chIndex == 6 || chIndex == 7) return 1.0;
        }
        else
        {
            if (chIndex <= 2) return 1.0;
            if (chIndex == 3) return 0.0;
            if (chIndex == 4 || chIndex == 5) return 1.41;
            if (chIndex == 6 || chIndex == 7) return 1.0;
            if (chIndex == 8 || chIndex == 9) return 1.41;
        }
    }

    return 1.0;
}

void LoudnessMeter::process (const juce::AudioBuffer<float>& buffer,
                             const juce::AudioChannelSet& layout,
                             int outputFormat)
{
    int numChannels = buffer.getNumChannels();
    int numSamples  = buffer.getNumSamples();

    for (int ch = 0; ch < numChannels; ++ch)
        channelWeights[ch] = getChannelWeight (layout.getTypeOfChannel (ch), ch, outputFormat, layout);
    for (int ch = numChannels; ch < maxChannels; ++ch)
        channelWeights[ch] = 0.0;

    std::fill (chSqSum.begin(), chSqSum.begin() + numChannels, 0.0);

    for (int i = 0; i < numSamples; ++i)
    {
        double weightedPower = 0.0;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            double w = channelWeights[ch];
            double s = static_cast<double> (buffer.getReadPointer (ch)[i]);
            double y1 = stage1[ch].process (s);
            double y2 = stage2[ch].process (y1);

            chSqSum[static_cast<size_t> (ch)] += y2 * y2;

            if (w <= 0.0) continue;
            weightedPower += w * y2 * y2;
        }

        // Momentary (400 ms)
        if (momentarySamplesFilled < momentaryWindowSize)
        {
            momentaryWindow[momentaryWritePos] = weightedPower;
            momentaryRunningSum += weightedPower;
            ++momentarySamplesFilled;
            momentaryWritePos = (momentaryWritePos + 1) % momentaryWindowSize;
        }
        else
        {
            double oldest = momentaryWindow[momentaryWritePos];
            momentaryRunningSum += weightedPower - oldest;
            momentaryWindow[momentaryWritePos] = weightedPower;
            momentaryWritePos = (momentaryWritePos + 1) % momentaryWindowSize;
        }

        double momentaryMean = (momentarySamplesFilled > 0)
            ? (momentaryRunningSum / momentarySamplesFilled) : 0.0;
        double momentaryVal = (momentaryMean > 1e-18)
            ? (-0.691 + 10.0 * std::log10 (momentaryMean)) : -100.0;
        momentaryLoudness.store (momentaryVal);

        // Short-term (3 s)
        if (shortSamplesFilled < shortWindowSize)
        {
            shortWindow[shortWritePos] = weightedPower;
            shortRunningSum += weightedPower;
            ++shortSamplesFilled;
            shortWritePos = (shortWritePos + 1) % shortWindowSize;
        }
        else
        {
            double oldest = shortWindow[shortWritePos];
            shortRunningSum += weightedPower - oldest;
            shortWindow[shortWritePos] = weightedPower;
            shortWritePos = (shortWritePos + 1) % shortWindowSize;
        }

        double shortMean = (shortSamplesFilled > 0)
            ? (shortRunningSum / shortSamplesFilled) : 0.0;
        double shortVal = (shortMean > 1e-18)
            ? (-0.691 + 10.0 * std::log10 (shortMean)) : -100.0;
        shortTermLoudness.store (shortVal);

        if (!paused.load())
        {
            if (shortVal > shortTermMax.load())
                shortTermMax.store (shortVal);
        }

        // Integrated (400 ms blocks)
        currentBlockPowerSum += weightedPower;
        ++currentBlockSamples;

        if (currentBlockSamples >= blockSizeSamples)
        {
            double blockPower = currentBlockPowerSum / currentBlockSamples;
            if (!paused.load())
            {
                integratedHistory.push_back ({blockPower});
                recomputeIntegrated();
            }
            currentBlockPowerSum = 0.0;
            currentBlockSamples = 0;
        }
    }

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& d = channelDebugInfo[static_cast<size_t> (ch)];
        d.type = layout.getTypeOfChannel (ch);
        d.weight = channelWeights[ch];
        double rms = std::sqrt (chSqSum[static_cast<size_t> (ch)] / numSamples);
        d.kWeightedDbfs = 20.0 * std::log10 (rms + 1e-18);
    }
    for (int ch = numChannels; ch < maxChannels; ++ch)
    {
        auto& d = channelDebugInfo[static_cast<size_t> (ch)];
        d.type = juce::AudioChannelSet::unknown;
        d.weight = 0.0;
        d.kWeightedDbfs = -100.0;
    }
}

void LoudnessMeter::recomputeIntegrated()
{
    if (integratedHistory.empty())
    {
        integratedLoudness.store (-70.0);
        return;
    }

    // Stage 1: absolute gate (-70 LKFS)
    std::vector<double> stage1Powers;
    stage1Powers.reserve (integratedHistory.size());
    for (const auto& b : integratedHistory)
    {
        double l = -0.691 + 10.0 * std::log10 (b.power);
        if (l > -70.0)
            stage1Powers.push_back (b.power);
    }

    if (stage1Powers.empty())
    {
        integratedLoudness.store (-70.0);
        return;
    }

    double sum1 = 0.0;
    for (auto p : stage1Powers) sum1 += p;
    double avgPower1 = sum1 / stage1Powers.size();
    double gatedLoudness1 = -0.691 + 10.0 * std::log10 (avgPower1);

    // Stage 2: relative threshold (-10 dB below stage 1)
    double relThreshold = gatedLoudness1 - 10.0;

    std::vector<double> stage2Powers;
    stage2Powers.reserve (integratedHistory.size());
    for (const auto& b : integratedHistory)
    {
        double l = -0.691 + 10.0 * std::log10 (b.power);
        if (l > -70.0 && l > relThreshold)
            stage2Powers.push_back (b.power);
    }

    if (stage2Powers.empty())
    {
        integratedLoudness.store (gatedLoudness1);
        return;
    }

    double sum2 = 0.0;
    for (auto p : stage2Powers) sum2 += p;
    double avgPower2 = sum2 / stage2Powers.size();
    integratedLoudness.store (-0.691 + 10.0 * std::log10 (avgPower2));
}

double LoudnessMeter::getMomentaryLoudness() const noexcept { return momentaryLoudness.load(); }
double LoudnessMeter::getShortTermLoudness() const noexcept { return shortTermLoudness.load(); }
double LoudnessMeter::getShortTermMaxLoudness() const noexcept { return shortTermMax.load(); }
double LoudnessMeter::getIntegratedLoudness() const noexcept { return integratedLoudness.load(); }
