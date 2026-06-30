#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <vector>

class LoudnessMeter
{
public:
    LoudnessMeter() = default;
    ~LoudnessMeter() = default;

    void prepare (double sampleRate, int maxChannels);
    void reset();
    void process (const juce::AudioBuffer<float>& buffer,
                  const juce::AudioChannelSet& layout,
                  int outputFormat);
    double getMomentaryLoudness() const noexcept;

private:
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;

        void reset() noexcept { z1 = z2 = 0.0; }
        double process (double x) noexcept
        {
            double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    void designKFilter (double sampleRate);
    static double getChannelWeight (juce::AudioChannelSet::ChannelType type,
                                   int channelIndex, int outputFormat);

    double sampleRate = 48000.0;
    int maxChannels = 0;

    std::vector<Biquad> stage1;
    std::vector<Biquad> stage2;
    std::vector<double> channelWeights;

    std::vector<double> powerWindow;
    int windowSize = 0;
    int writePos = 0;
    int samplesFilled = 0;
    double runningSum = 0.0;

    std::atomic<double> momentaryLoudness { -70.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoudnessMeter)
};
