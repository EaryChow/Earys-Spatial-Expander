#include "StereoSTFT.h"

StereoSTFT::StereoSTFT() : fft (std::make_unique<juce::dsp::FFT> (fftOrder)) {}

void StereoSTFT::allocateBuffers()
{
    window.resize (fftSize);
    for (int i = 0; i < fftSize; ++i)
    {
        float w = 0.5f * (1.0f - std::cos (2.0f * juce::MathConstants<float>::pi * i / fftSize));
        window[i] = w;
    }

    synthWindow.resize (fftSize);
    for (int i = 0; i < hopSize; ++i)
    {
        float w0 = window[i];
        float w1 = window[i + hopSize];
        float denom = w0 * w0 + w1 * w1;
        synthWindow[i] = (denom > 1e-12f) ? w0 / denom : w0;
        synthWindow[i + hopSize] = (denom > 1e-12f) ? w1 / denom : w1;
    }

    auto outBufSize = static_cast<size_t> (fftSize) * 4;
    inBufL.assign (fftSize, 0.0f);
    inBufR.assign (fftSize, 0.0f);
    outBufC.assign (outBufSize, 0.0f);
    outBufLres.assign (outBufSize, 0.0f);
    outBufRres.assign (outBufSize, 0.0f);

    auto fftBufSize = static_cast<size_t> (fftSize) * 2;
    fftBufL.assign (fftBufSize, 0.0f);
    fftBufR.assign (fftBufSize, 0.0f);
    fftBufC.assign (fftBufSize, 0.0f);
    fftBufLres.assign (fftBufSize, 0.0f);
    fftBufRres.assign (fftBufSize, 0.0f);
}

void StereoSTFT::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;
    allocateBuffers();
    reset();
}

void StereoSTFT::setWindowSize (int newFftOrder)
{
    if (newFftOrder == fftOrder)
        return;

    fftOrder = newFftOrder;
    fftSize  = 1 << fftOrder;
    hopSize  = fftSize / 2;

    fft = std::make_unique<juce::dsp::FFT> (fftOrder);
    allocateBuffers();
    reset();
}

void StereoSTFT::reset()
{
    if (! inBufL.empty())
    {
        std::fill (inBufL.begin(), inBufL.end(), 0.0f);
        std::fill (inBufR.begin(), inBufR.end(), 0.0f);
    }
    if (! outBufC.empty())
    {
        std::fill (outBufC.begin(), outBufC.end(), 0.0f);
        std::fill (outBufLres.begin(), outBufLres.end(), 0.0f);
        std::fill (outBufRres.begin(), outBufRres.end(), 0.0f);
    }
    if (! fftBufL.empty())
    {
        std::fill (fftBufL.begin(), fftBufL.end(), 0.0f);
        std::fill (fftBufR.begin(), fftBufR.end(), 0.0f);
        std::fill (fftBufC.begin(), fftBufC.end(), 0.0f);
        std::fill (fftBufLres.begin(), fftBufLres.end(), 0.0f);
        std::fill (fftBufRres.begin(), fftBufRres.end(), 0.0f);
    }
    inWp = 0;
    totalIn = 0;
    outWp = 0;
    outRp = hopSize;
    outReady = 0;
    framesCompleted = 0;
}

double StereoSTFT::getLatencyMs() const noexcept
{
    return static_cast<double> (hopSize) / sampleRate_ * 1000.0;
}

void StereoSTFT::process (const float* inL, const float* inR,
                           float* outC, float* outLres, float* outRres, int numSamples)
{
    auto outBufSize = static_cast<int> (outBufC.size());

    for (int i = 0; i < numSamples; ++i)
    {
        inBufL[inWp] = inL[i];
        inBufR[inWp] = inR[i];
        inWp = (inWp + 1) % fftSize;
        ++totalIn;

        if (totalIn >= fftSize && ((totalIn - fftSize) % hopSize == 0))
            processFrame();
    }

    int available = std::min (numSamples, outReady);
    for (int i = 0; i < available; ++i)
    {
        outC[i]    = outBufC[outRp];
        outLres[i] = outBufLres[outRp];
        outRres[i] = outBufRres[outRp];

        outBufC[outRp]    = 0.0f;
        outBufLres[outRp] = 0.0f;
        outBufRres[outRp] = 0.0f;

        outRp = (outRp + 1) % outBufSize;
    }
    outReady -= available;

    for (int i = available; i < numSamples; ++i)
    {
        outC[i]    = 0.0f;
        outLres[i] = 0.0f;
        outRres[i] = 0.0f;
    }
}

void StereoSTFT::processFrame()
{
    auto outBufSize = static_cast<int> (outBufC.size());

    for (int i = 0; i < fftSize; ++i)
    {
        int idx = (inWp + i) % fftSize;
        fftBufL[i] = inBufL[idx] * window[i];
        fftBufR[i] = inBufR[idx] * window[i];
    }
    std::fill (fftBufL.begin() + fftSize, fftBufL.end(), 0.0f);
    std::fill (fftBufR.begin() + fftSize, fftBufR.end(), 0.0f);

    fft->performRealOnlyForwardTransform (fftBufL.data());
    fft->performRealOnlyForwardTransform (fftBufR.data());

    if (frameListener != nullptr)
    {
        frameListener->onFrame (fftBufL.data(), fftBufR.data(),
                                fftBufC.data(), fftBufLres.data(), fftBufRres.data(),
                                fftSize);
    }
    else
    {
        std::copy (fftBufL.begin(), fftBufL.begin() + fftSize, fftBufC.begin());
        std::copy (fftBufR.begin(), fftBufR.begin() + fftSize, fftBufLres.begin());
        std::fill (fftBufRres.begin(), fftBufRres.begin() + fftSize, 0.0f);
    }

    fft->performRealOnlyInverseTransform (fftBufC.data());
    fft->performRealOnlyInverseTransform (fftBufLres.data());
    fft->performRealOnlyInverseTransform (fftBufRres.data());

    for (int i = 0; i < fftSize; ++i)
    {
        int pos = (outWp + i) % outBufSize;
        outBufC[pos] += fftBufC[i] * synthWindow[i];
        outBufLres[pos] += fftBufLres[i] * synthWindow[i];
        outBufRres[pos] += fftBufRres[i] * synthWindow[i];
    }
    outWp = (outWp + hopSize) % outBufSize;

    ++framesCompleted;
    if (framesCompleted >= 2)
        outReady += hopSize;
}
