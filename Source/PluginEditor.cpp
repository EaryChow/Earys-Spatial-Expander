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

    preampLabel.setText ("Preamp", juce::dontSendNotification);
    preampLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (preampLabel);

    preampSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    preampSlider.setTextBoxStyle (juce::Slider::TextBoxRight, true, 60, 20);
    preampSlider.setRange (-6.0, 6.0, 0.1);
    addAndMakeVisible (preampSlider);
    preampAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.getAPVTS(), "preamp", preampSlider);
    preampSlider.textFromValueFunction = [] (double value) -> juce::String
    {
        return juce::String (value, 1) + " dB";
    };
    preampSlider.valueFromTextFunction = [] (const juce::String& text) -> double
    {
        return text.getDoubleValue();
    };

    rearIsolationButton.setButtonText ("Rear Isolation");
    rearIsolationButton.setToggleState (false, juce::dontSendNotification);
    addAndMakeVisible (rearIsolationButton);
    rearIsolationAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getAPVTS(), "rearIsolation", rearIsolationButton);

    latencyLabel.setText ("Latency", juce::dontSendNotification);
    latencyLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (latencyLabel);

    updateLatencyComboBox();
    latencyComboBox.setSelectedId (4);
    addAndMakeVisible (latencyComboBox);
    latencyAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.getAPVTS(), "latency", latencyComboBox);

    measureButton.onClick = [this]
    {
        processor.pendingLatencyMeasurement.store (true);
        latencyResultLabel.setText ("Measuring\u2026", juce::dontSendNotification);
    };

    measureButton.setButtonText ("Measure Latency");
    addAndMakeVisible (measureButton);

    latencyResultLabel.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    latencyResultLabel.setColour (juce::Label::textColourId, juce::Colours::yellow);
    latencyResultLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (latencyResultLabel);

    warningLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    warningLabel.setColour (juce::Label::textColourId, juce::Colours::orange);
    warningLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (warningLabel);

    // Advanced panel
    advancedToggle.setButtonText ("Advanced");
    advancedToggle.setToggleState (false, juce::dontSendNotification);
    addAndMakeVisible (advancedToggle);
    advancedToggle.onClick = [this] { updateAdvancedPanel(); };

    advancedInfoLabel.setText ("For content-level balancing only. Room and speaker calibration should be done in your receiver or audio interface.", juce::dontSendNotification);
    advancedInfoLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    advancedInfoLabel.setJustificationType (juce::Justification::centred);
    advancedInfoLabel.setColour (juce::Label::textColourId, juce::Colours::grey);

    const char* chNames[] = { "Center", "Front L", "Front R", "Wide L", "Wide R", "Side L", "Side R", "Rear L", "Rear R" };
    const char* chParamIds[] = { "chOffC", "chOffFL", "chOffFR", "chOffWL", "chOffWR", "chOffSL", "chOffSR", "chOffRL", "chOffRR" };

    for (int i = 0; i < numChOff; ++i)
    {
        auto& sld = chOffSliders[i];
        sld.setSliderStyle (juce::Slider::LinearHorizontal);
        sld.setTextBoxStyle (juce::Slider::TextBoxRight, true, 50, 20);
        sld.setRange (-12.0, 12.0, 0.1);
        sld.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " dB"; };
        sld.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue(); };

        auto& lbl = chOffLabels[i];
        lbl.setText (chNames[i], juce::dontSendNotification);
        lbl.setJustificationType (juce::Justification::centred);

        chOffAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.getAPVTS(), chParamIds[i], sld);

        addAndMakeVisible (lbl);
        addAndMakeVisible (sld);
    }

    updateFormatComboBox();
    updateAdvancedPanel();

    setSize (550, 570);
    startTimerHz (4);
}

SpatialExpanderAudioProcessorEditor::~SpatialExpanderAudioProcessorEditor()
{
    stopTimer();
}

void SpatialExpanderAudioProcessorEditor::updateAdvancedPanel()
{
    bool show = advancedToggle.getToggleState();

    int numSpecOut = processor.getNumSpectralOutputs();
    bool hasRear  = numSpecOut > 3;
    bool hasSide  = numSpecOut > 5;
    bool hasWide  = numSpecOut > 7;

    bool visible[9];
    for (int i = 0; i < 3; ++i) visible[i] = show;           // Center, Front L/R
    visible[3] = show && hasWide;
    visible[4] = show && hasWide;
    visible[5] = show && hasSide;
    visible[6] = show && hasSide;
    visible[7] = show && hasRear;
    visible[8] = show && hasRear;

    advancedInfoLabel.setVisible (show);

    for (int i = 0; i < numChOff; ++i)
    {
        chOffLabels[i].setVisible (visible[i]);
        chOffSliders[i].setVisible (visible[i]);
    }

    // Resize window to fit
    if (show)
    {
        int extraRows = 0;
        for (int i = 0; i < numChOff; ++i)
            if (visible[i]) ++extraRows;
        int newH = 570 + 24 + static_cast<int> (advancedInfoLabel.getFont().getHeight()) + 6 + extraRows * 34 + 10;
        setSize (550, newH);
    }
    else
    {
        setSize (550, 570);
    }
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

    sl = area.removeFromTop (50);
    preampLabel.setBounds (sl.removeFromLeft (100));
    preampSlider.setBounds (sl.reduced (5));

    sl = area.removeFromTop (30);
    rearIsolationButton.setBounds (sl.reduced (10, 0));

    auto fmtArea = area.removeFromTop (45);
    formatLabel.setBounds (fmtArea.removeFromTop (20));
    formatComboBox.setBounds (fmtArea.reduced (60, 0));

    auto latArea = area.removeFromTop (45);
    latencyLabel.setBounds (latArea.removeFromTop (20));
    latencyComboBox.setBounds (latArea.reduced (60, 0));

    auto measArea = area.removeFromTop (24);
    measureButton.setBounds (measArea.removeFromLeft (120).reduced (2));
    latencyResultLabel.setBounds (measArea.reduced (2));

    // Advanced panel
    auto advArea = area.removeFromTop (24);
    advancedToggle.setBounds (advArea.reduced (60, 0));

    if (advancedToggle.getToggleState())
    {
        auto infoArea = area.removeFromTop (static_cast<int> (advancedInfoLabel.getFont().getHeight()) + 6);
        advancedInfoLabel.setBounds (infoArea.reduced (10, 0));

        for (int i = 0; i < numChOff; ++i)
        {
            if (!chOffSliders[i].isVisible())
                continue;
            auto row = area.removeFromTop (34);
            chOffLabels[i].setBounds (row.removeFromLeft (100));
            chOffSliders[i].setBounds (row.reduced (5));
        }
    }

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
    int numSpecOut = processor.getNumSpectralOutputs();
    bool enableStretch = numSpecOut > 3;
    stretchSlider.setEnabled (enableStretch);
    stretchLabel.setEnabled (enableStretch);

    // Latency measurement result
    if (processor.pendingLatencyMeasurement.load())
    {
        latencyResultLabel.setText ("Measuring\u2026", juce::dontSendNotification);
    }
    else
    {
        int measured = processor.measuredLatencySamples.load();
        if (measured != lastDisplayedLatency)
        {
            lastDisplayedLatency = measured;
            if (measured >= 0)
            {
                int expected = static_cast<int> (processor.getLatencySamples());
                if (measured == expected)
                    latencyResultLabel.setText ("Latency: " + juce::String (measured) + " samples (OK)",
                                                juce::dontSendNotification);
                else
                    latencyResultLabel.setText ("Latency: " + juce::String (measured)
                                                + " samples (expected " + juce::String (expected) + ")",
                                                juce::dontSendNotification);
            }
        }
    }

    // Refresh advanced panel if format changed while open
    if (advancedToggle.getToggleState() && numSpecOut != lastDetectedFormat)
    {
        lastDetectedFormat = numSpecOut;
        updateAdvancedPanel();
    }
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
    int values[] = { 496, 992, 1984, 3968 };

    latencyComboBox.clear (juce::dontSendNotification);
    for (int i = 0; i < 4; ++i)
    {
        double ms = static_cast<double> (values[i]) / sr * 1000.0;
        juce::String label = juce::String (values[i]) + " (" + juce::String (ms, 1) + " ms)";
        latencyComboBox.addItem (label, i + 1);
    }
    latencyComboBox.setSelectedId (currentId, juce::dontSendNotification);
}
