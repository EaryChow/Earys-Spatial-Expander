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
    void designBesselLowPass (double cutoff);
    void computeGroupDelay();

    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;

        void reset() noexcept;
        float process (float x) noexcept;
    };

    Biquad biquad1, biquad2;

    std::vector<float> extraDelay;
    int writePos = 0;
    int effectiveExtraDelay = 0;
    int iirGroupDelay = 0;
    int totalLatency = 0;

    double sampleRate_ = 48000.0;
    double cutoff_ = 80.0;
    int stftLatencySamples_ = 256;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LFEExtractor)
};
