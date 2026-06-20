#include "StereoSTFT.h"

StereoSTFT::StereoSTFT() : fft (std::make_unique<juce::dsp::FFT> (fftOrder)) {}

void StereoSTFT::allocateBuffers()
{
    window.resize (fftSize);
    double beta = 0.5;
    for (int i = 0; i < fftSize; ++i)
        window[i] = static_cast<float> (juce::dsp::SpecialFunctions::besselI0 (beta * std::sqrt (1.0 - std::pow ((2.0 * i / fftSize - 1.0), 2.0))) /
                                          juce::dsp::SpecialFunctions::besselI0 (beta));

    synthWindow.resize (fftSize);
    for (int i = 0; i < fftSize; ++i)
    {
        float sumSq = 0.0f;
        for (int offset = 0; offset < fftSize; offset += hopSize)
        {
            int idx = (i + offset) % fftSize;
            sumSq += window[idx] * window[idx];
        }
        synthWindow[i] = (sumSq > 1e-12f) ? window[i] / sumSq : window[i];
    }

    inBufL.assign (fftSize, 0.0f);
    inBufR.assign (fftSize, 0.0f);

    auto outBufSize = static_cast<size_t> (fftSize) * 4;
    outBufs.resize (numOutputs);
    for (int ch = 0; ch < numOutputs; ++ch)
        outBufs[ch].assign (outBufSize, 0.0f);

    auto fftBufSize = static_cast<size_t> (fftSize) * 2;
    fftBufL.assign (fftBufSize, 0.0f);
    fftBufR.assign (fftBufSize, 0.0f);

    fftBufs.resize (numOutputs);
    for (int ch = 0; ch < numOutputs; ++ch)
        fftBufs[ch].assign (fftBufSize, 0.0f);
}

void StereoSTFT::setNumOutputs (int n)
{
    if (n == numOutputs)
        return;
    numOutputs = n;
    if (fftSize > 0)
    {
        allocateBuffers();
        reset();
    }
}

void StereoSTFT::prepare (double)
{
    allocateBuffers();
    reset();
}

void StereoSTFT::setWindowSize (int newFftOrder)
{
    if (newFftOrder == fftOrder)
        return;

    fftOrder = newFftOrder;
    fftSize  = 1 << fftOrder;
    hopSize  = fftSize / 32;

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
    for (auto& buf : outBufs)
        std::fill (buf.begin(), buf.end(), 0.0f);
    for (auto& buf : fftBufs)
        std::fill (buf.begin(), buf.end(), 0.0f);
    inWp = 0;
    totalIn = 0;
    outWp = 0;
    outRp = fftSize - hopSize;
    outReady = 0;
    framesCompleted = 0;
}

void StereoSTFT::process (const float* inL, const float* inR,
                           float** outputs, int numSamples)
{
    auto outBufSize = static_cast<int> (outBufs[0].size());

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
        for (int ch = 0; ch < numOutputs; ++ch)
        {
            outputs[ch][i] = outBufs[ch][outRp];
            outBufs[ch][outRp] = 0.0f;
        }
        outRp = (outRp + 1) % outBufSize;
    }
    outReady -= available;

    for (int i = available; i < numSamples; ++i)
    {
        for (int ch = 0; ch < numOutputs; ++ch)
            outputs[ch][i] = 0.0f;
    }
}

void StereoSTFT::processFrame()
{
    auto outBufSize = static_cast<int> (outBufs[0].size());
    int outCh = numOutputs;

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
        std::vector<float*> fftPtrs (outCh);
        for (int ch = 0; ch < outCh; ++ch)
            fftPtrs[ch] = fftBufs[ch].data();

        frameListener->onFrame (fftBufL.data(), fftBufR.data(),
                                fftPtrs.data(), outCh, fftSize);
    }
    else
    {
        for (int ch = 0; ch < outCh; ++ch)
        {
            if (ch == 0)
                std::copy (fftBufL.begin(), fftBufL.begin() + fftSize, fftBufs[ch].begin());
            else if (ch == 1)
                std::copy (fftBufR.begin(), fftBufR.begin() + fftSize, fftBufs[ch].begin());
            else
                std::fill (fftBufs[ch].begin(), fftBufs[ch].begin() + fftSize, 0.0f);
        }
    }

    for (int ch = 0; ch < outCh; ++ch)
        fft->performRealOnlyInverseTransform (fftBufs[ch].data());

    for (int ch = 0; ch < outCh; ++ch)
    {
        for (int i = 0; i < fftSize; ++i)
        {
            int pos = (outWp + i) % outBufSize;
            outBufs[ch][pos] += fftBufs[ch][i] * synthWindow[i];
        }
    }
    outWp = (outWp + hopSize) % outBufSize;

    ++framesCompleted;
    int minFramesForCola = fftSize / hopSize;
    if (framesCompleted >= minFramesForCola)
        outReady += hopSize;
}
