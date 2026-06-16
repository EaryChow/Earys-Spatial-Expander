#include "LFEExtractor.h"
#include <cmath>

void LFEExtractor::Biquad::reset() noexcept
{
    x1 = x2 = y1 = y2 = 0.0f;
}

float LFEExtractor::Biquad::process (float x) noexcept
{
    float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1; x1 = x;
    y2 = y1; y1 = y;
    return y;
}

void LFEExtractor::designBesselLowPass (double cutoff)
{
    // 4th-order Bessel-Thomson lowpass via two cascaded biquads.
    // Bessel polynomial θ₄(s) = s⁴ + 10s³ + 45s² + 105s + 105
    // Poles (roots of θ₄):  -2.896±j1.336 and -2.104±j2.374
    // -3dB frequency of θ₄ is ω₋₃dB = 2.115 rad/s (analog normalized)

    // Section 1: ω₁ = 3.189, Q₁ = 0.551  → f₁ = fc·1.508
    // Section 2: ω₂ = 3.172, Q₂ = 0.754  → f₂ = fc·1.500
    double fc = cutoff;
    double f1 = fc * 1.508;
    double f2 = fc * 1.500;
    double q1 = 0.551;
    double q2 = 0.754;

    // RBJ cookbook bilinear-transform lowpass
    auto design = [&](Biquad& bq, double freq, double Q)
    {
        double w0 = 2.0 * juce::MathConstants<double>::pi * freq / sampleRate_;
        double cos_w0 = std::cos (w0);
        double alpha = std::sin (w0) / (2.0 * Q);
        double norm = 1.0 / (1.0 + alpha);

        bq.b0 = static_cast<float> ((1.0 - cos_w0) * 0.5 * norm);
        bq.b1 = static_cast<float> ((1.0 - cos_w0) * norm);
        bq.b2 = bq.b0;
        bq.a1 = static_cast<float> (-2.0 * cos_w0 * norm);
        bq.a2 = static_cast<float> ((1.0 - alpha) * norm);
    };

    design (biquad1, f1, q1);
    design (biquad2, f2, q2);

    computeGroupDelay();
}

void LFEExtractor::computeGroupDelay()
{
    // DC group delay of 4th-order Bessel-Thomson scaled to cutoff fc:
    // τ(0) = ω₋₃dB / (2π·fc) seconds, where ω₋₃dB = 2.115
    double wc = 2.0 * juce::MathConstants<double>::pi * cutoff_;
    iirGroupDelay = static_cast<int> (std::round (sampleRate_ * 2.115 / wc));
}

void LFEExtractor::prepare (double sampleRate, int stftLatencySamples, double cutoff)
{
    sampleRate_ = sampleRate;
    stftLatencySamples_ = stftLatencySamples;
    cutoff_ = cutoff;

    extraDelay.assign (static_cast<size_t> (stftLatencySamples), 0.0f);
    designBesselLowPass (cutoff);
    reset();

    int extra = stftLatencySamples - iirGroupDelay;
    effectiveExtraDelay = std::max (0, extra);
    totalLatency = stftLatencySamples;
    jassert (extra >= 0);
}

void LFEExtractor::setCutoff (double cutoff)
{
    if (cutoff == cutoff_)
        return;
    cutoff_ = cutoff;
    designBesselLowPass (cutoff);

    int extra = stftLatencySamples_ - iirGroupDelay;
    effectiveExtraDelay = std::max (0, extra);
    // extraDelay buffer stays at existing size; effective length is adjusted at runtime
}

void LFEExtractor::reset()
{
    biquad1.reset();
    biquad2.reset();
    std::fill (extraDelay.begin(), extraDelay.end(), 0.0f);
    writePos = 0;
}

void LFEExtractor::process (const float* inL, const float* inR,
                            float* outLFE, int numSamples)
{
    // When IIR group delay ≥ STFT latency, no extra delay needed
    if (effectiveExtraDelay <= 0)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float sum = (inL[i] + inR[i]) * 0.5f;
            outLFE[i] = biquad1.process (biquad2.process (sum));
        }
        return;
    }

    int size = static_cast<int> (extraDelay.size());
    for (int i = 0; i < numSamples; ++i)
    {
        float sum = (inL[i] + inR[i]) * 0.5f;
        float filtered = biquad1.process (biquad2.process (sum));

        int readPos = (writePos - effectiveExtraDelay + size) % size;
        outLFE[i] = extraDelay[readPos];
        extraDelay[writePos] = filtered;
        writePos = (writePos + 1) % size;
    }
}
