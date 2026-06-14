#pragma once
#include <atomic>
#include <JuceHeader.h>
#include "DSP/LFEExtractor.h"
#include "DSP/StereoSTFT.h"
#include "DSP/SpatialAnalyser.h"

class SpatialExpanderAudioProcessor : public juce::AudioProcessor,
                                       private StereoSTFT::FrameListener,
                                       private juce::AudioProcessorValueTreeState::Listener
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

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    juce::String getInputWarningText() const;
    juce::String getFormatWarningText (int selectedFormat) const;
    juce::String getCurrentBusFormatName() const;

    enum OutputFormat { Auto = 0, Fmt51, Fmt71, Fmt916 };

private:
    void onFrame (const float* fftL, const float* fftR,
                  float* fftC, float* fftLres, float* fftRres,
                  int fftSize) override;

    void parameterChanged (const juce::String& parameterID, float newValue) override;

    std::atomic<int> pendingWindowOrder { 0 };

    int blockSize = 512;

    StereoSTFT stft;
    SpatialAnalyser analyser;
    LFEExtractor lfe;

    std::vector<float> chC, chLres, chRres;
    std::vector<float> chLFE;

    float prevLfeCutoff = 80.0f;

    juce::AudioProcessorValueTreeState apvts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpatialExpanderAudioProcessor)
};
