#pragma once
#include <JuceHeader.h>

class StereoSTFT
{
public:
    StereoSTFT();
    ~StereoSTFT() = default;

    void prepare (double sampleRate);
    void reset();
    void process (const float* inL, const float* inR,
                  float* outL, float* outR, int numSamples);

    int getLatencySamples() const noexcept { return fftSize; }
    double getLatencyMs() const noexcept;

    static constexpr int fftOrder = 9;
    static constexpr int fftSize  = 1 << fftOrder;
    static constexpr int hopSize  = fftSize / 2;

private:
    void processFrame();

    juce::dsp::FFT fft;

    std::vector<float> window;
    std::vector<float> inBufL, inBufR;
    std::vector<float> outBufL, outBufR;
    std::vector<float> fftBufL, fftBufR;

    int inWp = 0;
    int64_t totalIn = 0;
    int outWp = 0;
    int outRp = 0;
    int outReady = 0;
    int framesCompleted = 0;
    double sampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StereoSTFT)
};
