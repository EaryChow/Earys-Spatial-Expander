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
    lfeLevelSlider.setTextBoxStyle (juce::Slider::TextBoxRight, true, 60, 20);
    lfeLevelSlider.setRange (-12.1, 12.0, 0.1);
    addAndMakeVisible (lfeLevelSlider);
    lfeLevelAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.getAPVTS(), "lfeLevel", lfeLevelSlider);
    lfeLevelSlider.textFromValueFunction = [] (double value) -> juce::String
    {
        if (value < -12.0)
            return "-inf dB";
        return juce::String (value, 1) + " dB";
    };
    lfeLevelSlider.valueFromTextFunction = [] (const juce::String& text) -> double
    {
        if (text.containsIgnoreCase ("inf"))
            return -12.1;
        return text.getDoubleValue();
    };

    formatLabel.setText ("Output Format", juce::dontSendNotification);
    formatLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (formatLabel);

    formatComboBox.addItemList ({ "Auto", "3.0", "5.1", "7.1", "9.1" }, 1);
    formatComboBox.setSelectedId (1);
    addAndMakeVisible (formatComboBox);
    formatAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.getAPVTS(), "outputFormat", formatComboBox);

    leakCenterLabel.setText ("Leak Center", juce::dontSendNotification);
    leakCenterLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (leakCenterLabel);

    leakCenterSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    leakCenterSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 20);
    leakCenterSlider.setRange (0.0, 1.5, 0.01);
    addAndMakeVisible (leakCenterSlider);
    leakCenterAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.getAPVTS(), "leakCenter", leakCenterSlider);

    stretchLabel.setText ("Stretch", juce::dontSendNotification);
    stretchLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (stretchLabel);

    stretchSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    stretchSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 20);
    stretchSlider.setRange (0.0, 1.0, 0.01);
    addAndMakeVisible (stretchSlider);
    stretchAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.getAPVTS(), "stretch", stretchSlider);

    rearIsolationButton.setButtonText ("5.1 Rear Channel Isolation");
    rearIsolationButton.setToggleState (true, juce::dontSendNotification);
    addAndMakeVisible (rearIsolationButton);
    rearIsolationAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getAPVTS(), "rearIsolation", rearIsolationButton);

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

    setSize (550, 520);
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

    sl = area.removeFromTop (50);
    leakCenterLabel.setBounds (sl.removeFromLeft (100));
    leakCenterSlider.setBounds (sl.reduced (5));

    sl = area.removeFromTop (50);
    stretchLabel.setBounds (sl.removeFromLeft (100));
    stretchSlider.setBounds (sl.reduced (5));

    sl = area.removeFromTop (30);
    rearIsolationButton.setBounds (sl.reduced (10, 0));

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
    {
        auto selectedId = formatComboBox.getSelectedId();
        auto formatIdx = (selectedId > 0) ? selectedId - 1 : 0;
        auto inputWarn  = processor.getInputWarningText();
        auto formatWarn = processor.getFormatWarningText (formatIdx);
        if (inputWarn.isNotEmpty() && formatWarn.isNotEmpty())
            warningLabel.setText (inputWarn + "  " + formatWarn, juce::dontSendNotification);
        else if (inputWarn.isNotEmpty())
            warningLabel.setText (inputWarn, juce::dontSendNotification);
        else if (formatWarn.isNotEmpty())
            warningLabel.setText (formatWarn, juce::dontSendNotification);
        else
            warningLabel.setText ({}, juce::dontSendNotification);
    }

    // Disable Stretch control in 3.1 mode
    bool enableStretch = processor.getNumSpectralOutputs() > 3;
    stretchSlider.setEnabled (enableStretch);
    stretchLabel.setEnabled (enableStretch);
}

void SpatialExpanderAudioProcessorEditor::updateFormatComboBox()
{
    auto busName = processor.getCurrentBusFormatName();

    if (formatComboBox.getNumItems() == 0 ||
        ! formatComboBox.getItemText (0).contains (busName))
    {
        auto currentId = formatComboBox.getSelectedId();

        formatComboBox.clear (juce::dontSendNotification);
        formatComboBox.addItem ("Auto (" + busName + ")", 1);
        formatComboBox.addItem ("3.0", 2);
        formatComboBox.addItem ("5.1", 3);
        formatComboBox.addItem ("7.1", 4);
        formatComboBox.addItem ("9.1", 5);
        formatComboBox.setSelectedId (currentId, juce::dontSendNotification);
    }
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
