#pragma once
#include <JuceHeader.h>

class SurroundSTFT
{
public:
    SurroundSTFT();
    ~SurroundSTFT() = default;

    void prepare (double sampleRate);
    void reset();
    void setWindowSize (int newFftOrder);
    void setNumOutputs (int n);
    void setNumInputs (int n);

    void process (const float* inL, const float* inR,
                  float** outputs, int numSamples);

    void process (const float** inputs, int numInputs_,
                  float** outputs, int numOutputs_, int numSamples);

    int fftOrder = 9;
    int fftSize  = 512;
    int hopSize  = 16;
    int numOutputs = 3;
    int numInputs  = 2;

    class FrameListener
    {
    public:
        virtual ~FrameListener() = default;
        virtual void onFrame (const float** fftInputs, int numInputs_,
                              float** fftOutputs, int numOutputs_,
                              int fftSize) = 0;
    };

    void setFrameListener (FrameListener* listener) { frameListener = listener; }

private:
    void processFrame();
    void allocateBuffers();

    std::unique_ptr<juce::dsp::FFT> fft;

    std::vector<float> window;
    std::vector<float> synthWindow;
    std::vector<std::vector<float>> inBufs;
    std::vector<std::vector<float>> outBufs;
    std::vector<std::vector<float>> fftBufsInput;
    std::vector<std::vector<float>> fftBufs;

    int activeInputs = 2;
    int inWp = 0;
    int64_t totalIn = 0;
    int outWp = 0;
    int outRp = 0;
    int outReady = 0;
    int framesCompleted = 0;

    FrameListener* frameListener = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SurroundSTFT)
};
