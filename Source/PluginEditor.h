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

    juce::ComboBox windowSizeComboBox;
    juce::Label windowSizeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> windowSizeAttach;

    juce::Label warningLabel;

    int lastDetectedFormat = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpatialExpanderAudioProcessorEditor)
};
