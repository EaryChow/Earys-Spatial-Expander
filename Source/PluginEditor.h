#pragma once
#include <JuceHeader.h>

class SpatialExpanderAudioProcessor;

class SpatialExpanderAudioProcessorEditor
    : public juce::AudioProcessorEditor
{
public:
    explicit SpatialExpanderAudioProcessorEditor (SpatialExpanderAudioProcessor&);
    ~SpatialExpanderAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SpatialExpanderAudioProcessor& processor;

    juce::Label titleLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpatialExpanderAudioProcessorEditor)
};
