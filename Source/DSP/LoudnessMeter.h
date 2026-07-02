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
    double getShortTermLoudness() const noexcept;
    double getShortTermMaxLoudness() const noexcept;
    double getIntegratedLoudness() const noexcept;

    void setPaused (bool paused);
    bool isPaused() const noexcept;
    void clearHistory();

    struct ChannelDebugInfo
    {
        juce::AudioChannelSet::ChannelType type = juce::AudioChannelSet::unknown;
        double kWeightedDbfs = -100.0;
        double weight = 0.0;
    };
    const std::vector<ChannelDebugInfo>& getChannelDebugInfo() const noexcept
    {
        return channelDebugInfo;
    }

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
                                   int channelIndex, int outputFormat,
                                   const juce::AudioChannelSet& layout);
    void recomputeIntegrated();

    double sampleRate = 48000.0;
    int maxChannels = 0;

    std::vector<Biquad> stage1;
    std::vector<Biquad> stage2;
    std::vector<double> channelWeights;
    std::vector<double> chSqSum;

    // Momentary (400 ms)
    std::vector<double> momentaryWindow;
    int momentaryWindowSize = 0;
    int momentaryWritePos = 0;
    int momentarySamplesFilled = 0;
    double momentaryRunningSum = 0.0;
    std::atomic<double> momentaryLoudness { -70.0 };

    // Short-term (3 s)
    std::vector<double> shortWindow;
    int shortWindowSize = 0;
    int shortWritePos = 0;
    int shortSamplesFilled = 0;
    double shortRunningSum = 0.0;
    std::atomic<double> shortTermLoudness { -70.0 };
    std::atomic<double> shortTermMax { -70.0 };

    // Integrated (400 ms blocks, BS.1770-5 two-stage gating)
    struct IntegratedBlock { double power = 0.0; };
    std::vector<IntegratedBlock> integratedHistory;
    double currentBlockPowerSum = 0.0;
    int currentBlockSamples = 0;
    int blockSizeSamples = 0;
    std::atomic<double> integratedLoudness { -70.0 };

    std::atomic<bool> paused { true };

    std::vector<ChannelDebugInfo> channelDebugInfo;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoudnessMeter)
};
