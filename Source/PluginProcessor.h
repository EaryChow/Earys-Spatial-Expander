#pragma once
#include <JuceHeader.h>
#include "DSP/StereoSTFT.h"
#include "DSP/LFEExtractor.h"

class SpatialExpanderAudioProcessor : public juce::AudioProcessor
{
public:
    SpatialExpanderAudioProcessor();
    ~SpatialExpanderAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;
    const juce::String getName() const override;

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;


private:
    StereoSTFT stft;
    LFEExtractor lfe;

    std::vector<float> stftOutBufL;
    std::vector<float> stftOutBufR;
    std::vector<float> lfeOutBuf;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpatialExpanderAudioProcessor)
};
