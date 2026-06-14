#include "SpatialRenderer.h"

// ═══════════════════════════════════════════════════════════════════════════
// Cubic spline helpers  (natural spline, C2 continuous, monotonic anchors)
// ═══════════════════════════════════════════════════════════════════════════

static void solveCubicSpline (const float* x, const float* y, int n,
                              float* a, float* b, float* c, float* d)
{
    if (n < 2) return;

    std::vector<float> h (n - 1);
    for (int i = 0; i < n - 1; ++i)
        h[i] = x[i + 1] - x[i];

    std::vector<float> alpha (n - 1);
    for (int i = 1; i < n - 1; ++i)
        alpha[i] = (3.0f / h[i]) * (y[i + 1] - y[i])
                 - (3.0f / h[i - 1]) * (y[i] - y[i - 1]);

    std::vector<float> l (n, 0.0f);
    std::vector<float> mu (n, 0.0f);
    std::vector<float> z (n, 0.0f);

    l[0] = 1.0f;
    for (int i = 1; i < n - 1; ++i)
    {
        l[i] = 2.0f * (x[i + 1] - x[i - 1]) - h[i - 1] * mu[i - 1];
        mu[i] = h[i] / l[i];
        z[i] = (alpha[i] - h[i - 1] * z[i - 1]) / l[i];
    }
    l[n - 1] = 1.0f;

    for (int i = 0; i < n; ++i)
        c[i] = z[i];

    for (int j = n - 2; j >= 0; --j)
    {
        c[j] = z[j] - mu[j] * c[j + 1];
        b[j] = (y[j + 1] - y[j]) / h[j] - h[j] * (c[j + 1] + 2.0f * c[j]) / 3.0f;
        d[j] = (c[j + 1] - c[j]) / (3.0f * h[j]);
        a[j] = y[j];
    }
}

static float evalSpline (float t, float x0, float a, float b, float c, float d)
{
    float dx = t - x0;
    return a + b * dx + c * dx * dx + d * dx * dx * dx;
}

// ═══════════════════════════════════════════════════════════════════════════
// Construction
// ═══════════════════════════════════════════════════════════════════════════

SpatialRenderer::SpatialRenderer()
    : fft (fftOrder)
{
    buildAzimuthLut();
}

void SpatialRenderer::buildAzimuthLut()
{
    constexpr int numAnchors = 5;
    const float anchorIld[numAnchors]  = { 0.0f, 3.0f, 8.0f, 15.0f, 30.0f };
    const float anchorAng[numAnchors]  = { 0.0f, 30.0f, 90.0f, 150.0f, 180.0f };

    float a[5], b[5], c[5], d[5];
    solveCubicSpline (anchorIld, anchorAng, numAnchors, a, b, c, d);

    azimuthLut_.resize (lutSize_);
    float ildStep = 30.0f / static_cast<float> (lutSize_ - 1);

    for (int i = 0; i < lutSize_; ++i)
    {
        float ild = static_cast<float> (i) * ildStep;

        int seg = numAnchors - 2;
        for (int j = 0; j < numAnchors - 1; ++j)
        {
            if (ild >= anchorIld[j] && ild <= anchorIld[j + 1])
            {
                seg = j;
                break;
            }
        }

        float angle = evalSpline (ild, anchorIld[seg], a[seg], b[seg], c[seg], d[seg]);
        if (angle > 180.0f) angle = 180.0f;

        azimuthLut_[i] = angle;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Prepare / Reset
// ═══════════════════════════════════════════════════════════════════════════

void SpatialRenderer::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;

    window_.resize (fftSize);
    for (int i = 0; i < fftSize; ++i)
        window_[i] = 0.5f * (1.0f - std::cos (
            2.0f * juce::MathConstants<float>::pi * static_cast<float> (i) / fftSize));

    auto outBufSize = static_cast<size_t> (fftSize) * 4;

    inBufL_.assign (fftSize, 0.0f);
    inBufR_.assign (fftSize, 0.0f);
    stereoFftBufL_.assign (fftSize * 2, 0.0f);
    stereoFftBufR_.assign (fftSize * 2, 0.0f);

    olaSize_ = static_cast<int> (outBufSize);
    for (int ch = 0; ch < numGroundChannels; ++ch)
    {
        channelFftBuf_[ch].assign (fftSize * 2, 0.0f);
        olaBuf_[ch].assign (outBufSize, 0.0f);
        olaWp_[ch] = 0;
    }

    inWp_ = 0;
    totalIn_ = 0;
    framesCompleted_ = 0;
    outReady_ = 0;

    for (int i = 0; i < cacheSize_; ++i)
        objectCache_[i].id = -1;

    analyser_.prepare (fftSize, sampleRate);
    updateSmoothingAlpha();
    updateHeightBin();
}

void SpatialRenderer::reset()
{
    if (! inBufL_.empty())
    {
        std::fill (inBufL_.begin(), inBufL_.end(), 0.0f);
        std::fill (inBufR_.begin(), inBufR_.end(), 0.0f);
        std::fill (stereoFftBufL_.begin(), stereoFftBufL_.end(), 0.0f);
        std::fill (stereoFftBufR_.begin(), stereoFftBufR_.end(), 0.0f);
    }

    for (int ch = 0; ch < numGroundChannels; ++ch)
    {
        if (! channelFftBuf_[ch].empty())
            std::fill (channelFftBuf_[ch].begin(), channelFftBuf_[ch].end(), 0.0f);
        if (! olaBuf_[ch].empty())
        {
            std::fill (olaBuf_[ch].begin(), olaBuf_[ch].end(), 0.0f);
            olaWp_[ch] = 0;
        }
    }

    inWp_ = 0;
    totalIn_ = 0;
    framesCompleted_ = 0;
    outReady_ = 0;
    outRp_ = 0;

    for (int i = 0; i < cacheSize_; ++i)
        objectCache_[i].id = -1;

    analyser_.reset();
}

double SpatialRenderer::getLatencyMs() const noexcept
{
    return static_cast<double> (fftSize) / sampleRate_ * 1000.0;
}

// ═══════════════════════════════════════════════════════════════════════════
// Parameter helpers
// ═══════════════════════════════════════════════════════════════════════════

void SpatialRenderer::setSmoothingMs (float ms)
{
    smoothingMs_ = ms;
    updateSmoothingAlpha();
}

void SpatialRenderer::updateSmoothingAlpha()
{
    float tauSamples = (smoothingMs_ / 1000.0f) * static_cast<float> (sampleRate_);
    if (tauSamples > 1.0f)
        smoothingAlpha_ = 1.0f - std::exp (-static_cast<float> (hopSize) / tauSamples);
    else
        smoothingAlpha_ = 1.0f;
}

void SpatialRenderer::setHeightThresholdHz (float hz)
{
    heightThresholdHz_ = hz;
    updateHeightBin();
}

void SpatialRenderer::updateHeightBin()
{
    heightThresholdBin_ = static_cast<int> (
        heightThresholdHz_ * static_cast<float> (fftSize) / static_cast<float> (sampleRate_) + 0.5f);
    if (heightThresholdBin_ < 0) heightThresholdBin_ = 0;
    if (heightThresholdBin_ > fftSize / 2) heightThresholdBin_ = fftSize / 2;
}

// ═══════════════════════════════════════════════════════════════════════════
// Tent-map panning law (7.1 ground)
// ═══════════════════════════════════════════════════════════════════════════

void SpatialRenderer::computePanGains (float angleDeg, float* gains) const
{
    static constexpr float spkAngles[numGroundChannels] =
        { 210.0f, 270.0f, 330.0f, 0.0f, 30.0f, 90.0f, 150.0f };
    // Lb=210  Ls=270  L=330  C=0  R=30  Rs=90  Rb=150

    std::fill (gains, gains + numGroundChannels, 0.0f);

    float theta = angleDeg;
    while (theta < 0.0f)   theta += 360.0f;
    while (theta >= 360.0f) theta -= 360.0f;

    int idxA = 0, idxB = 0;
    float frac = 0.0f;

    for (int i = 0; i < numGroundChannels; ++i)
    {
        int next = (i + 1) % numGroundChannels;
        float a = spkAngles[i];
        float b = spkAngles[next];
        if (b < a) b += 360.0f;

        float t = theta;
        if (t < a) t += 360.0f;

        if (t >= a && t <= b)
        {
            idxA = i;
            idxB = next;
            frac = (t - a) / (b - a);
            break;
        }
    }

    frac = juce::jlimit (0.0f, 1.0f, frac);

    if (frac < soloZone_)
    {
        float rolloff = 0.5f + 0.5f * std::cos (
            juce::MathConstants<float>::pi * frac / soloZone_);
        gains[idxA] = rolloff;
        gains[idxB] = 1.0f - rolloff;
    }
    else if (frac > 1.0f - soloZone_)
    {
        float rolloff = 0.5f + 0.5f * std::cos (
            juce::MathConstants<float>::pi * (1.0f - frac) / soloZone_);
        gains[idxB] = rolloff;
        gains[idxA] = 1.0f - rolloff;
    }
    else
    {
        gains[idxA] = 1.0f - frac;
        gains[idxB] = frac;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Main processing
// ═══════════════════════════════════════════════════════════════════════════

void SpatialRenderer::process (const float* inL, const float* inR,
                                float* const* outGround, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        inBufL_[inWp_] = inL[i];
        inBufR_[inWp_] = inR[i];
        inWp_ = (inWp_ + 1) % fftSize;
        ++totalIn_;

        if (totalIn_ >= fftSize && ((totalIn_ - fftSize) % hopSize == 0))
            processFrame();

        if (outReady_ > 0)
        {
            for (int ch = 0; ch < numGroundChannels; ++ch)
            {
                outGround[ch][i] = olaBuf_[ch][outRp_];
                olaBuf_[ch][outRp_] = 0.0f;
            }
            outRp_ = (outRp_ + 1) % olaSize_;
            --outReady_;
        }
        else
        {
            for (int ch = 0; ch < numGroundChannels; ++ch)
                outGround[ch][i] = 0.0f;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Per-frame processing
// ═══════════════════════════════════════════════════════════════════════════

void SpatialRenderer::processFrame()
{
    applyWindow();

    fft.performRealOnlyForwardTransform (stereoFftBufL_.data());
    fft.performRealOnlyForwardTransform (stereoFftBufR_.data());

    analyser_.onFrame (stereoFftBufL_.data(), stereoFftBufR_.data(), fftSize);

    for (int ch = 0; ch < numGroundChannels; ++ch)
        std::fill (channelFftBuf_[ch].begin(), channelFftBuf_[ch].end(), 0.0f);

    auto* objects = analyser_.getObjects();
    int numObjects = analyser_.getNumObjects();
    auto& metrics = analyser_.getBinMetrics();

    for (int o = 0; o < numObjects; ++o)
    {
        const auto& obj = objects[o];
        float objGain = obj.smoothedGain;
        if (objGain < 1e-6f) continue;

        int startB = obj.startBin;
        int endB   = obj.endBin;
        if (startB < 0 || endB >= fftSize / 2 + 1) continue;

        // ── Stretched azimuth ──────────────────────────────────────────
        float ildAbs = std::abs (obj.meanILD);
        int lutIdx = static_cast<int> (ildAbs * static_cast<float> (lutSize_ - 1) / 30.0f);
        lutIdx = juce::jlimit (0, lutSize_ - 1, lutIdx);
        float stretchedAngle = azimuthLut_[lutIdx];

        float sign = (obj.meanILD >= 0.0f) ? -1.0f : 1.0f;
        float originalAngle = sign * std::min (30.0f, ildAbs * 10.0f);
        float finalAngle = (1.0f - stretchParam_) * originalAngle
                          + stretchParam_ * sign * stretchedAngle;

        // ── Rear bias (§8) ──────────────────────────────────────────────
        // Condition: |PhaseDiff - 180°| < 30° AND |ILD| < 6 dB AND Corr < 0.5
        float meanPhaseDeg = obj.meanPhaseDiff * 180.0f / juce::MathConstants<float>::pi;
        float phaseDiff180 = std::abs (meanPhaseDeg - 180.0f);
        if (phaseDiff180 > 180.0f) phaseDiff180 = 360.0f - phaseDiff180;

        if (phaseDiff180 < 30.0f && ildAbs < 6.0f && obj.meanCorr < 0.5f)
            finalAngle += sign * rearBias_ * 60.0f;

        // ── Height decision (§7) ────────────────────────────────────────
        bool routeToHeight = false;
        if (heightThresholdBin_ > 0 && heightThresholdBin_ < fftSize / 2)
        {
            float energyAbove = 0.0f;
            float energyTotal = 0.0f;
            float transientEnergy = 0.0f;

            for (int b = startB; b <= endB; ++b)
            {
                float magL = metrics[b].magnitudeL;
                float magR = metrics[b].magnitudeR;
                float mag   = magL + magR;
                float sq    = mag * mag;
                energyTotal += sq;
                if (b >= heightThresholdBin_)
                    energyAbove += sq;
                transientEnergy += metrics[b].spectralFlux;
            }

            if (energyTotal > 1e-15f)
            {
                float effectiveAbove = energyAbove + (transientEnergy * transientBoost_ * 0.5f);
                routeToHeight = (effectiveAbove / energyTotal) > 0.5f;
            }
        }

        // ── Pan gains ─────────────────────────────────────────────────
        float targetGains[numGroundChannels] = {};
        if (! routeToHeight)
            computePanGains (finalAngle, targetGains);
        else
        {
            // Route to height: silence ground channels for this object
            // (Phase 4 will implement actual height routing)
        }

        // ── Dominant channel extraction ────────────────────────────────
        // Use L if ILD >= 0 (left-dominant), R if ILD < 0 (right-dominant).
        // Avoids comb filtering from complex L+R summation.
        bool useLeft = (obj.meanILD >= 0.0f);
        auto* stereoBufL = stereoFftBufL_.data();
        auto* stereoBufR = stereoFftBufR_.data();

        // ── Per-bin scaling with gain smoothing ────────────────────────
        int cacheIdx = obj.id & (cacheSize_ - 1);
        auto& cache = objectCache_[cacheIdx];
        if (cache.id != obj.id)
        {
            cache.id = obj.id;
            std::fill (cache.gains, cache.gains + numGroundChannels, 0.0f);
        }

        for (int ch = 0; ch < numGroundChannels; ++ch)
        {
            float target = targetGains[ch] * objGain;
            float& smoothed = cache.gains[ch];
            smoothed += smoothingAlpha_ * (target - smoothed);

            float gain = smoothed;
            if (std::abs (gain) < 1e-8f) continue;

            for (int b = startB; b <= endB; ++b)
            {
                int idx = 2 * b;
                if (useLeft)
                {
                    channelFftBuf_[ch][idx]     += stereoBufL[idx]     * gain;
                    channelFftBuf_[ch][idx + 1] += stereoBufL[idx + 1] * gain;
                }
                else
                {
                    channelFftBuf_[ch][idx]     += stereoBufR[idx]     * gain;
                    channelFftBuf_[ch][idx + 1] += stereoBufR[idx + 1] * gain;
                }
            }
        }
    }

    // ── Per-channel iFFT + OLA ──────────────────────────────────────────
    for (int ch = 0; ch < numGroundChannels; ++ch)
    {
        fft.performRealOnlyInverseTransform (channelFftBuf_[ch].data());
        for (int i = 0; i < fftSize; ++i)
        {
            int pos = (olaWp_[ch] + i) % olaSize_;
            olaBuf_[ch][pos] += channelFftBuf_[ch][i];
        }
        olaWp_[ch] = (olaWp_[ch] + hopSize) % olaSize_;
    }

    ++framesCompleted_;
    if (framesCompleted_ >= 2)
        outReady_ += hopSize;
}

void SpatialRenderer::applyWindow()
{
    for (int i = 0; i < fftSize; ++i)
    {
        int idx = (inWp_ + i) % fftSize;
        stereoFftBufL_[i] = inBufL_[idx] * window_[i];
        stereoFftBufR_[i] = inBufR_[idx] * window_[i];
    }
    std::fill (stereoFftBufL_.begin() + fftSize, stereoFftBufL_.end(), 0.0f);
    std::fill (stereoFftBufR_.begin() + fftSize, stereoFftBufR_.end(), 0.0f);
}
