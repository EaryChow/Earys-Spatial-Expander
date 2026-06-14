#pragma once
#include <JuceHeader.h>

class LFEExtractor
{
public:
    LFEExtractor() = default;
    ~LFEExtractor() = default;

    void prepare (double sampleRate, int stftLatencySamples, double cutoff = 80.0);
    void reset();
    void setCutoff (double cutoff);
    void process (const float* inL, const float* inR,
                  float* outLFE, int numSamples);

    int getLatencySamples() const noexcept { return totalLatency; }

private:
    static constexpr int firOrder = 510;
    static constexpr int firNumTaps = firOrder + 1;
    static constexpr int firDelay = firOrder / 2;

    std::vector<float> coeffs;
    std::vector<float> firBuf;
    int firPos = 0;

    std::vector<float> extraDelay;
    int extraPos = 0;
    int totalLatency = 0;

    double sampleRate_ = 48000.0;

    static void designLowPass (std::vector<float>& coeffs, double sampleRate, double cutoff);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LFEExtractor)
};
