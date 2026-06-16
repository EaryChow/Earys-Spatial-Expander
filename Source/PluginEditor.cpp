#include "PluginProcessor.h"
#include "PluginEditor.h"

SpatialExpanderAudioProcessorEditor::SpatialExpanderAudioProcessorEditor (
    SpatialExpanderAudioProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    titleLabel.setText ("Spatial Expander", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
    titleLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (titleLabel);

    lfeCutoffLabel.setText ("LFE Cutoff", juce::dontSendNotification);
    lfeCutoffLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (lfeCutoffLabel);

    lfeCutoffSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    lfeCutoffSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    lfeCutoffSlider.setRange (40.0, 200.0, 1.0);
    addAndMakeVisible (lfeCutoffSlider);
    lfeCutoffAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.getAPVTS(), "lfeCutoff", lfeCutoffSlider);

    lfeLevelLabel.setText ("LFE Level", juce::dontSendNotification);
    lfeLevelLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (lfeLevelLabel);

    lfeLevelSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    lfeLevelSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    lfeLevelSlider.setRange (-60.0, 12.0, 0.5);
    addAndMakeVisible (lfeLevelSlider);
    lfeLevelAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.getAPVTS(), "lfeLevel", lfeLevelSlider);

    formatLabel.setText ("Output Format", juce::dontSendNotification);
    formatLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (formatLabel);

    formatComboBox.addItemList ({ "Auto", "3.0", "5.1", "7.1", "9.1.6" }, 1);
    formatComboBox.setSelectedId (1);
    addAndMakeVisible (formatComboBox);
    formatAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.getAPVTS(), "outputFormat", formatComboBox);

    latencyLabel.setText ("Latency", juce::dontSendNotification);
    latencyLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (latencyLabel);

    updateLatencyComboBox();
    latencyComboBox.setSelectedId (2);
    addAndMakeVisible (latencyComboBox);
    latencyAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.getAPVTS(), "latency", latencyComboBox);

    warningLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    warningLabel.setColour (juce::Label::textColourId, juce::Colours::orange);
    warningLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (warningLabel);

    updateFormatComboBox();

    setSize (550, 350);
    startTimerHz (4);
}

SpatialExpanderAudioProcessorEditor::~SpatialExpanderAudioProcessorEditor()
{
    stopTimer();
}

void SpatialExpanderAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void SpatialExpanderAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (10);
    titleLabel.setBounds (area.removeFromTop (30));

    auto sl = area.removeFromTop (50);
    lfeCutoffLabel.setBounds (sl.removeFromLeft (100));
    lfeCutoffSlider.setBounds (sl.reduced (5));

    sl = area.removeFromTop (50);
    lfeLevelLabel.setBounds (sl.removeFromLeft (100));
    lfeLevelSlider.setBounds (sl.reduced (5));

    auto fmtArea = area.removeFromTop (45);
    formatLabel.setBounds (fmtArea.removeFromTop (20));
    formatComboBox.setBounds (fmtArea.reduced (60, 0));

    auto latArea = area.removeFromTop (45);
    latencyLabel.setBounds (latArea.removeFromTop (20));
    latencyComboBox.setBounds (latArea.reduced (60, 0));

    warningLabel.setBounds (area.reduced (10, 0));
}

void SpatialExpanderAudioProcessorEditor::timerCallback()
{
    updateFormatComboBox();
    updateLatencyComboBox();
    warningLabel.setText (processor.getMeasuredLatencyDebugString(), juce::dontSendNotification);
}

void SpatialExpanderAudioProcessorEditor::updateFormatComboBox()
{
    auto busName = processor.getCurrentBusFormatName();
    auto currentId = formatComboBox.getSelectedId();

    formatComboBox.clear (juce::dontSendNotification);
    formatComboBox.addItem ("Auto (" + busName + ")", 1);
    formatComboBox.addItem ("3.0", 2);
    formatComboBox.addItem ("5.1", 3);
    formatComboBox.addItem ("7.1", 4);
    formatComboBox.addItem ("9.1.6", 5);
    formatComboBox.setSelectedId (currentId, juce::dontSendNotification);
}

void SpatialExpanderAudioProcessorEditor::updateLatencyComboBox()
{
    auto sr = processor.getSampleRate();
    if (sr < 1000.0) sr = 48000.0;

    auto currentId = latencyComboBox.getSelectedId();
    int values[] = { 512, 1024, 2048 };

    latencyComboBox.clear (juce::dontSendNotification);
    for (int i = 0; i < 3; ++i)
    {
        double ms = static_cast<double> (values[i]) / sr * 1000.0;
        juce::String label = juce::String (values[i]) + " (" + juce::String (ms, 1) + " ms)";
        latencyComboBox.addItem (label, i + 1);
    }
    latencyComboBox.setSelectedId (currentId, juce::dontSendNotification);
}
