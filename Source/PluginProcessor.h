#pragma once
#include <atomic>
#include <random>
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

    static int getLatencySamplesForMode (int modeIndex) noexcept;

    juce::String getInputWarningText() const;
    juce::String getFormatWarningText (int selectedFormat) const;
    juce::String getCurrentBusFormatName() const;

    int getNumSpectralOutputs() const noexcept;

    std::atomic<bool> pendingLatencyMeasurement { false };
    std::atomic<int> measuredLatencySamples { -1 };

    enum OutputFormat { Auto = 0, Fmt30, Fmt51, Fmt71, Fmt916 };
    enum AuxBusIdx { AuxFront = 1, AuxCenter, AuxLFE, AuxWide, AuxSide, AuxRear };

private:
    int detectFormatFromBus() const noexcept;

    void onFrame (const float* fftL, const float* fftR,
                  float** fftOutputs, int numOutputs,
                  int fftSize) override;

    void doCascade (const float* fftL, const float* fftR,
                    float* fftCenter, float* fftFrontL, float* fftFrontR,
                    float* fftWideL, float* fftWideR,
                    float* fftSideL, float* fftSideR,
                    float* fftRearL, float* fftRearR,
                    float* fftTemp, int fftSize, int numSpecOut);

    void applyStretch (float* fftCenter, float* fftFrontL, float* fftFrontR,
                       float* fftWideL, float* fftWideR,
                       float* fftSideL, float* fftSideR,
                       float* fftRearL, float* fftRearR,
                       int fftSize, float stretch, int numSpecOut);

    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    void runCalibration();

    enum class CalState : int { Normal, FadingOut, Reconfiguring, FadingIn };

    std::atomic<int> pendingWindowOrder { 0 };
    std::atomic<bool> pendingFormatChange { false };
    std::atomic<CalState> calState { CalState::Normal };
    std::atomic<int> fadeSamplesLeft { 0 };
    int fadeSamplesTotal = 0;

    static constexpr int ildTableSize = 121;
    std::vector<float> gainTable;

    StereoSTFT stft;
    SpatialAnalyser analyser;
    LFEExtractor lfe;

    std::vector<std::vector<float>> chOutputs;
    std::vector<float*> chOutputPtrs;
    std::vector<float> chLFE;

    float prevLfeCutoff = 80.0f;

    int lastBlockSize = 512;

    // Pre-allocated cascade scratch buffers (audio thread safe)
    std::vector<float> cascadeCenter;
    std::vector<float> cascadeFrontL;
    std::vector<float> cascadeFrontR;
    std::vector<float> cascadeWideL;
    std::vector<float> cascadeWideR;
    std::vector<float> cascadeSideL;
    std::vector<float> cascadeSideR;
    std::vector<float> cascadeRearL;
    std::vector<float> cascadeRearR;
    std::vector<float> cascadeTemp;
    std::vector<float> cascadeLresSave;
    std::vector<float> cascadeRresSave;
    int cascadeFftSize = 0;

    // Per-bin static calibration gains (no temporal smoothing)
    std::vector<float> binCalGains;
    std::vector<float> binCalGainsSmoothed;

    std::atomic<float>* chOffParams[9] = {};

    // Latency measurement state (audio-thread only after trigger)
    int measPhase = 0;
    int measTotalPos = 0;
    int measTotalLen = 0;
    int measImpulsePos = 0;
    int measNumSpecOut = 0;

    std::vector<float> measInput;
    std::vector<std::vector<float>> measOutput;

    juce::AudioProcessorValueTreeState apvts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpatialExpanderAudioProcessor)
};
