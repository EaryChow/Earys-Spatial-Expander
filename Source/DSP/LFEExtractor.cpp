#include "LFEExtractor.h"
#include <cmath>

void LFEExtractor::designLowPass (std::vector<float>& coeffs, double sampleRate, double cutoff)
{
    int N = static_cast<int> (coeffs.size());
    int M = (N - 1) / 2;
    double fc = cutoff / sampleRate;

    double sum = 0.0;
    for (int i = 0; i < N; ++i)
    {
        int n = i - M;
        double val;
        if (std::abs (n) < 1e-10)
            val = 2.0 * fc;
        else
            val = std::sin (2.0 * juce::MathConstants<double>::pi * fc * n) / (juce::MathConstants<double>::pi * n);

        double hamming = 0.54 - 0.46 * std::cos (2.0 * juce::MathConstants<double>::pi * i / (N - 1));
        val *= hamming;
        coeffs[i] = static_cast<float> (val);
        sum += val;
    }

    float invSum = static_cast<float> (1.0 / sum);
    for (auto& c : coeffs)
        c *= invSum;
}

void LFEExtractor::prepare (double sampleRate, int stftLatencySamples, double cutoff)
{
    sampleRate_ = sampleRate;

    coeffs.assign (firNumTaps, 0.0f);
    firBuf.assign (firNumTaps, 0.0f);
    designLowPass (coeffs, sampleRate, cutoff);

    firPos = 0;

    int extraSamples = stftLatencySamples - firDelay;
    extraDelay.assign (static_cast<size_t> (extraSamples), 0.0f);
    extraPos = 0;
    totalLatency = stftLatencySamples;
}

void LFEExtractor::setCutoff (double cutoff)
{
    designLowPass (coeffs, sampleRate_, cutoff);
}

void LFEExtractor::reset()
{
    std::fill (firBuf.begin(), firBuf.end(), 0.0f);
    std::fill (extraDelay.begin(), extraDelay.end(), 0.0f);
    firPos = 0;
    extraPos = 0;
}

void LFEExtractor::process (const float* inL, const float* inR,
                            float* outLFE, int numSamples)
{
    int extraLen = static_cast<int> (extraDelay.size());

    for (int i = 0; i < numSamples; ++i)
    {
        float sum = (inL[i] + inR[i]) * 0.5f;

        firBuf[firPos] = sum;
        int idx = firPos;
        float acc = 0.0f;
        for (int j = 0; j < firNumTaps; ++j)
        {
            acc += firBuf[idx] * coeffs[j];
            idx = (idx - 1 + firNumTaps) % firNumTaps;
        }
        firPos = (firPos + 1) % firNumTaps;

        outLFE[i] = extraDelay[extraPos];
        extraDelay[extraPos] = acc;
        extraPos = (extraPos + 1) % extraLen;
    }
}
