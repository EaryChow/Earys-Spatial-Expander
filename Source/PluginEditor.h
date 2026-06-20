#pragma once
#include <JuceHeader.h>

class SpatialExpanderAudioProcessor;

class SpatialExpanderAudioProcessorEditor
    : public juce::AudioProcessorEditor,
      private juce::Timer
{
public:
    explicit SpatialExpanderAudioProcessorEditor (SpatialExpanderAudioProcessor&);
    ~SpatialExpanderAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateFormatComboBox();

    SpatialExpanderAudioProcessor& processor;

    juce::Label titleLabel;

    juce::Slider lfeCutoffSlider;
    juce::Label lfeCutoffLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lfeCutoffAttach;

    juce::Slider lfeLevelSlider;
    juce::Label lfeLevelLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lfeLevelAttach;

    juce::ComboBox formatComboBox;
    juce::Label formatLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> formatAttach;

    void updateLatencyComboBox();

    juce::ComboBox latencyComboBox;
    juce::Label latencyLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> latencyAttach;

    juce::Slider leakCenterSlider;
    juce::Label leakCenterLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> leakCenterAttach;

    juce::Slider stretchSlider;
    juce::Label stretchLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> stretchAttach;

    juce::Slider preampSlider;
    juce::Label preampLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> preampAttach;

    juce::ToggleButton rearIsolationButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> rearIsolationAttach;

    static constexpr int numChOff = 9;

    juce::ToggleButton chGainToggle;
    juce::Label chGainInfoLabel;

    juce::Slider chOffSliders[numChOff];
    juce::Label chOffLabels[numChOff];
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> chOffAttachments[numChOff];

    void updateChGainPanel();

    // measurement UI hidden, logic kept in processor for future use
    // juce::TextButton measureButton;
    // juce::Label latencyResultLabel;

    juce::Label warningLabel;

    int lastDetectedFormat = -1;
    // int lastDisplayedLatency = -999;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpatialExpanderAudioProcessorEditor)
};
