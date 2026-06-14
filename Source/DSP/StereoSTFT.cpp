#include "StereoSTFT.h"

StereoSTFT::StereoSTFT() : fft (fftOrder) {}

void StereoSTFT::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;

    window.resize (fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[i] = 0.5f * (1.0f - std::cos (2.0f * juce::MathConstants<float>::pi * i / fftSize));

    auto outBufSize = static_cast<size_t> (fftSize) * 4;
    inBufL.assign (fftSize, 0.0f);
    inBufR.assign (fftSize, 0.0f);
    outBufL.assign (outBufSize, 0.0f);
    outBufR.assign (outBufSize, 0.0f);
    fftBufL.assign (fftSize * 2, 0.0f);
    fftBufR.assign (fftSize * 2, 0.0f);

    inWp = 0;
    totalIn = 0;
    outWp = 0;
    outRp = 0;
    outReady = 0;
    framesCompleted = 0;
}

void StereoSTFT::reset()
{
    if (! inBufL.empty())
    {
        std::fill (inBufL.begin(), inBufL.end(), 0.0f);
        std::fill (inBufR.begin(), inBufR.end(), 0.0f);
    }
    if (! outBufL.empty())
    {
        std::fill (outBufL.begin(), outBufL.end(), 0.0f);
        std::fill (outBufR.begin(), outBufR.end(), 0.0f);
    }
    if (! fftBufL.empty())
    {
        std::fill (fftBufL.begin(), fftBufL.end(), 0.0f);
        std::fill (fftBufR.begin(), fftBufR.end(), 0.0f);
    }
    inWp = 0;
    totalIn = 0;
    outWp = 0;
    outRp = 0;
    outReady = 0;
    framesCompleted = 0;
}

double StereoSTFT::getLatencyMs() const noexcept
{
    return static_cast<double> (fftSize) / sampleRate_ * 1000.0;
}

void StereoSTFT::process (const float* inL, const float* inR,
                          float* outL, float* outR, int numSamples)
{
    auto outBufSize = static_cast<int> (outBufL.size());

    for (int i = 0; i < numSamples; ++i)
    {
        inBufL[inWp] = inL[i];
        inBufR[inWp] = inR[i];
        inWp = (inWp + 1) % fftSize;
        ++totalIn;

        if (totalIn >= fftSize && ((totalIn - fftSize) % hopSize == 0))
            processFrame();

        if (outReady > 0)
        {
            outL[i] = outBufL[outRp];
            outR[i] = outBufR[outRp];

            outBufL[outRp] = 0.0f;
            outBufR[outRp] = 0.0f;

            outRp = (outRp + 1) % outBufSize;
            --outReady;
        }
        else
        {
            outL[i] = 0.0f;
            outR[i] = 0.0f;
        }
    }
}

void StereoSTFT::processFrame()
{
    auto outBufSize = static_cast<int> (outBufL.size());

    for (int i = 0; i < fftSize; ++i)
    {
        int idx = (inWp + i) % fftSize;
        fftBufL[i] = inBufL[idx] * window[i];
        fftBufR[i] = inBufR[idx] * window[i];
    }
    std::fill (fftBufL.begin() + fftSize, fftBufL.end(), 0.0f);
    std::fill (fftBufR.begin() + fftSize, fftBufR.end(), 0.0f);

    fft.performRealOnlyForwardTransform (fftBufL.data());
    fft.performRealOnlyForwardTransform (fftBufR.data());

    fft.performRealOnlyInverseTransform (fftBufL.data());
    fft.performRealOnlyInverseTransform (fftBufR.data());

    for (int i = 0; i < fftSize; ++i)
    {
        int pos = (outWp + i) % outBufSize;
        outBufL[pos] += fftBufL[i];
        outBufR[pos] += fftBufR[i];
    }
    outWp = (outWp + hopSize) % outBufSize;

    ++framesCompleted;
    if (framesCompleted >= 2)
        outReady += hopSize;
}
