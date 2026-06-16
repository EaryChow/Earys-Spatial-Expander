#pragma once
#include <JuceHeader.h>

class StereoSTFT
{
public:
    StereoSTFT();
    ~StereoSTFT() = default;

    void prepare (double sampleRate);
    void reset();
    void setWindowSize (int newFftOrder);
    void setNumOutputs (int n);

    void process (const float* inL, const float* inR,
                  float** outputs, int numSamples);

    int getLatencySamples() const noexcept { return hopSize; }
    double getLatencyMs() const noexcept;
    int getNumBins() const noexcept { return fftSize / 2 + 1; }

    int fftOrder = 9;
    int fftSize  = 512;
    int hopSize  = 256;
    int numOutputs = 3;

    class FrameListener
    {
    public:
        virtual ~FrameListener() = default;
        virtual void onFrame (const float* fftL, const float* fftR,
                              float** fftOutputs, int numOutputs,
                              int fftSize) = 0;
    };

    void setFrameListener (FrameListener* listener) { frameListener = listener; }

private:
    void processFrame();
    void allocateBuffers();

    std::unique_ptr<juce::dsp::FFT> fft;

    std::vector<float> window;
    std::vector<float> synthWindow;
    std::vector<float> inBufL, inBufR;
    std::vector<std::vector<float>> outBufs;
    std::vector<float> fftBufL, fftBufR;
    std::vector<std::vector<float>> fftBufs;

    int inWp = 0;
    int64_t totalIn = 0;
    int outWp = 0;
    int outRp = 0;
    int outReady = 0;
    int framesCompleted = 0;
    double sampleRate_ = 48000.0;

    FrameListener* frameListener = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StereoSTFT)
};
