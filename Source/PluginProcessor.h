#pragma once
#include <atomic>
#include <JuceHeader.h>
#include "DSP/LFEExtractor.h"
#include "DSP/StereoSTFT.h"
#include "DSP/SpatialAnalyser.h"

class SpatialExpanderAudioProcessor : public juce::AudioProcessor,
                                       private StereoSTFT::FrameListener,
                                       private juce::AudioProcessorValueTreeState::Listener,
                                       private juce::AsyncUpdater
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

    double getLatencyMs() const noexcept;

    juce::String getInputWarningText() const;
    juce::String getFormatWarningText (int selectedFormat) const;
    juce::String getCurrentBusFormatName() const;

    enum OutputFormat { Auto = 0, Fmt30, Fmt51, Fmt71, Fmt916 };

private:
    void onFrame (const float* fftL, const float* fftR,
                  float* fftC, float* fftLres, float* fftRres,
                  int fftSize) override;

    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    void triggerCalibration();

    enum class CalState : int { Normal, FadingOut, Reconfiguring, FadingIn };

    std::atomic<int> pendingWindowOrder { 0 };
    std::atomic<CalState> calState { CalState::Normal };
    std::atomic<int> fadeSamplesLeft { 0 };
    int fadeSamplesTotal = 0;
    std::atomic<bool> autoCalibrate { false };
    std::atomic<bool> isTransportPlaying { false };

    StereoSTFT stft;
    SpatialAnalyser analyser;
    LFEExtractor lfe;

    std::vector<float> chC, chLres, chRres;
    std::vector<float> chLFE;

    std::vector<float> delayC, delayL, delayR;
    int delayWritePos = 0;
    int delaySize = 0;
    int delayCapacity = 0;

    float prevLfeCutoff = 80.0f;

    int lastBlockSize = 512;

    juce::AudioProcessorValueTreeState apvts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpatialExpanderAudioProcessor)
};
