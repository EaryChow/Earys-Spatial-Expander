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

    inFormatLabel.setText ("Input Format", juce::dontSendNotification);
    inFormatLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (inFormatLabel);

    inFormatComboBox.addItemList ({ "Stereo", "5.1", "7.1" }, 1);
    inFormatComboBox.setSelectedId (1);
    addAndMakeVisible (inFormatComboBox);
    inFormatAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.getAPVTS(), "inputFormat", inFormatComboBox);

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

    crosstalkLabel.setText ("Crosstalk", juce::dontSendNotification);
    crosstalkLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (crosstalkLabel);

    crosstalkSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    crosstalkSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 20);
    crosstalkSlider.setRange (0.0, 0.5, 0.01);
    addAndMakeVisible (crosstalkSlider);
    crosstalkAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.getAPVTS(), "crosstalk", crosstalkSlider);
    crosstalkSlider.onDragStart = [this] { processor.beginRecalibrationGesture(); };
    crosstalkSlider.onDragEnd   = [this] { processor.endRecalibrationGesture(); };

    stretchLabel.setText ("Stretch", juce::dontSendNotification);
    stretchLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (stretchLabel);

    stretchSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    stretchSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 20);
    stretchSlider.setRange (0.0, 1.0, 0.01);
    addAndMakeVisible (stretchSlider);
    stretchAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.getAPVTS(), "stretch", stretchSlider);
    stretchSlider.onDragStart = [this] { processor.beginRecalibrationGesture(); };
    stretchSlider.onDragEnd   = [this] { processor.endRecalibrationGesture(); };

    preampLabel.setText ("Preamp", juce::dontSendNotification);
    preampLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (preampLabel);

    preampSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    preampSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
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

    rearBiasLabel.setText ("Rear Bias", juce::dontSendNotification);
    rearBiasLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (rearBiasLabel);

    rearBiasSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    rearBiasSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 20);
    rearBiasSlider.setRange (0.0, 1.0, 0.01);
    addAndMakeVisible (rearBiasSlider);
    rearBiasAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.getAPVTS(), "rearBias", rearBiasSlider);
    rearBiasSlider.onDragStart = [this] { processor.beginRecalibrationGesture(); };
    rearBiasSlider.onDragEnd   = [this] { processor.endRecalibrationGesture(); };
    latencyLabel.setText ("Quality/Latency", juce::dontSendNotification);
    latencyLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (latencyLabel);

    updateLatencyComboBox();
    latencyComboBox.setSelectedId (2);
    addAndMakeVisible (latencyComboBox);
    latencyAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.getAPVTS(), "latency", latencyComboBox);

    // measurement UI hidden, logic kept for future use
    // measureButton.onClick = [this]
    // {
    //     processor.pendingLatencyMeasurement.store (true);
    //     latencyResultLabel.setText ("Measuring\u2026", juce::dontSendNotification);
    // };
    // 
    // measureButton.setButtonText ("Measure Latency");
    // addAndMakeVisible (measureButton);
    // 
    // latencyResultLabel.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    // latencyResultLabel.setColour (juce::Label::textColourId, juce::Colours::yellow);
    // latencyResultLabel.setJustificationType (juce::Justification::centred);
    // addAndMakeVisible (latencyResultLabel);

    loudnessSectionLabel.setText ("Loudness Meter", juce::dontSendNotification);
    loudnessSectionLabel.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    loudnessSectionLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (loudnessSectionLabel);

    loudnessReadoutLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    loudnessReadoutLabel.setJustificationType (juce::Justification::centred);
    loudnessReadoutLabel.setColour (juce::Label::textColourId, juce::Colours::lime);
    addAndMakeVisible (loudnessReadoutLabel);

    loudnessPauseButton.setButtonText (processor.isLoudnessPaused() ? "Resume" : "Pause");
    loudnessPauseButton.onClick = [this]
    {
        bool currentlyPaused = processor.isLoudnessPaused();
        processor.setLoudnessPaused (!currentlyPaused);
        loudnessPauseButton.setButtonText (currentlyPaused ? "Pause" : "Resume");
    };
    addAndMakeVisible (loudnessPauseButton);

    loudnessRefreshButton.setButtonText ("Refresh");
    loudnessRefreshButton.onClick = [this]
    {
        processor.refreshLoudness();
    };
    addAndMakeVisible (loudnessRefreshButton);

    debugText.setMultiLine (true, true);
    debugText.setReadOnly (true);
    debugText.setScrollbarsShown (false);
    debugText.setCaretVisible (false);
    debugText.setJustification (juce::Justification::centredLeft);
    debugText.setFont (juce::Font (juce::FontOptions (10.0f)));
    debugText.setColour (juce::TextEditor::textColourId, juce::Colours::yellow);
    debugText.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    debugText.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (debugText);

    warningText.setMultiLine (true, true);
    warningText.setReadOnly (true);
    warningText.setScrollbarsShown (false);
    warningText.setCaretVisible (false);
    warningText.setJustification (juce::Justification::centred);
    warningText.setFont (juce::Font (juce::FontOptions (12.0f)));
    warningText.setColour (juce::TextEditor::textColourId, juce::Colours::orange);
    warningText.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    warningText.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (warningText);

    // Per-Channel Gain panel
    chGainToggle.setButtonText ("Per-Channel Gain");
    chGainToggle.setToggleState (false, juce::dontSendNotification);
    addAndMakeVisible (chGainToggle);
    chGainToggle.onClick = [this] { updateChGainPanel(); };

    chGainInfoLabel.setText ("For content-level balancing only. Room and speaker calibration should be done in your receiver or audio interface.", juce::dontSendNotification);
    chGainInfoLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    chGainInfoLabel.setJustificationType (juce::Justification::centred);
    chGainInfoLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (chGainInfoLabel);

    const char* chNames[] = { "Center", "Front L", "Front R", "Wide L", "Wide R", "Side L", "Side R", "Rear L", "Rear R" };
    const char* chParamIds[] = { "chOffC", "chOffFL", "chOffFR", "chOffWL", "chOffWR", "chOffSL", "chOffSR", "chOffRL", "chOffRR" };

    for (int i = 0; i < numChOff; ++i)
    {
        auto& sld = chOffSliders[i];
        sld.setSliderStyle (juce::Slider::LinearHorizontal);
        sld.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 20);
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

    debugToggle.setButtonText ("Show Debug Channel Info");
    debugToggle.setToggleState (false, juce::dontSendNotification);
    addAndMakeVisible (debugToggle);
    debugToggle.onClick = [this] { updateChGainPanel(); };

    updateFormatComboBox();
    updateChGainPanel();

    setSize (550, 700);
    startTimerHz (4);
}

SpatialExpanderAudioProcessorEditor::~SpatialExpanderAudioProcessorEditor()
{
    stopTimer();
}

void SpatialExpanderAudioProcessorEditor::updateChGainPanel()
{
    bool show = chGainToggle.getToggleState();

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

    chGainInfoLabel.setVisible (show);

    for (int i = 0; i < numChOff; ++i)
    {
        chOffLabels[i].setVisible (visible[i]);
        chOffSliders[i].setVisible (visible[i]);
    }

    // Resize window to fit
    int newH = 700;
    if (show)
    {
        int extraRows = 0;
        for (int i = 0; i < numChOff; ++i)
            if (visible[i]) ++extraRows;
        newH = 700 + 24 + static_cast<int> (chGainInfoLabel.getFont().getHeight()) + 6 + extraRows * 34 + 10;
    }
    if (debugToggle.getToggleState())
        newH += 24 + 80 + 6;
    setSize (550, newH);
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
    crosstalkLabel.setBounds (sl.removeFromLeft (100));
    crosstalkSlider.setBounds (sl.reduced (5));

    sl = area.removeFromTop (50);
    stretchLabel.setBounds (sl.removeFromLeft (100));
    stretchSlider.setBounds (sl.reduced (5));

    sl = area.removeFromTop (50);
    rearBiasLabel.setBounds (sl.removeFromLeft (100));
    rearBiasSlider.setBounds (sl.reduced (5));

    sl = area.removeFromTop (50);
    preampLabel.setBounds (sl.removeFromLeft (100));
    preampSlider.setBounds (sl.reduced (5));

    // Loudness Meter Section
    area.removeFromTop (8);
    auto loudSectionArea = area.removeFromTop (70);
    loudnessSectionLabel.setBounds (loudSectionArea.removeFromTop (16).reduced (10, 0));
    loudnessReadoutLabel.setBounds (loudSectionArea.removeFromTop (20).reduced (10, 0));
    auto btnArea = loudSectionArea.removeFromTop (24);
    loudnessPauseButton.setBounds (btnArea.removeFromLeft (80).reduced (4, 2));
    loudnessRefreshButton.setBounds (btnArea.removeFromLeft (80).reduced (4, 2));

    auto inFmtArea = area.removeFromTop (45);
    inFormatLabel.setBounds (inFmtArea.removeFromTop (20));
    inFormatComboBox.setBounds (inFmtArea.reduced (60, 0));

    auto fmtArea = area.removeFromTop (45);
    formatLabel.setBounds (fmtArea.removeFromTop (20));
    formatComboBox.setBounds (fmtArea.reduced (60, 0));

    auto latArea = area.removeFromTop (45);
    latencyLabel.setBounds (latArea.removeFromTop (20));
    latencyComboBox.setBounds (latArea.reduced (60, 0));

    // measurement UI hidden
    // auto measArea = area.removeFromTop (24);
    // measureButton.setBounds (measArea.removeFromLeft (120).reduced (2));
    // latencyResultLabel.setBounds (measArea.reduced (2));

    // Per-Channel Gain panel
    area.removeFromTop (16);
    auto advArea = area.removeFromTop (24);
    chGainToggle.setBounds (advArea.reduced (10, 0));

    if (chGainToggle.getToggleState())
    {
        auto infoArea = area.removeFromTop (static_cast<int> (chGainInfoLabel.getFont().getHeight()) + 6);
        chGainInfoLabel.setBounds (infoArea.reduced (10, 0));

        for (int i = 0; i < numChOff; ++i)
        {
            if (!chOffSliders[i].isVisible())
                continue;
            auto row = area.removeFromTop (34);
            chOffLabels[i].setBounds (row.removeFromLeft (100));
            chOffSliders[i].setBounds (row.reduced (5));
        }
    }

    {
        auto debugToggleArea = area.removeFromTop (24);
        debugToggle.setBounds (debugToggleArea.reduced (10, 0));
    }

    debugText.setVisible (debugToggle.getToggleState());
    if (debugToggle.getToggleState())
    {
        auto debugArea = area.removeFromTop (80);
        debugText.setBounds (debugArea.reduced (10, 2));
    }

    auto warnArea = area.removeFromBottom (60);
    warningText.setBounds (warnArea.reduced (10, 2));
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
            warningText.setText (inputWarn + "\n" + formatWarn, juce::dontSendNotification);
        else if (inputWarn.isNotEmpty())
            warningText.setText (inputWarn, juce::dontSendNotification);
        else if (formatWarn.isNotEmpty())
            warningText.setText (formatWarn, juce::dontSendNotification);
        else
            warningText.setText ({}, juce::dontSendNotification);
    }

    {
        double mom = processor.getMomentaryLoudness();
        double sh = processor.getShortTermLoudness();
        double shMax = processor.getShortTermMaxLoudness();
        double integ = processor.getIntegratedLoudness();

        auto fmt = [] (double v) -> juce::String
        {
            if (v <= -69.0) return "--";
            return juce::String (v, 1);
        };

        juce::String txt = "Momentary: " + fmt (mom)
                         + "    Short: " + fmt (sh)
                         + "    S-Max: " + fmt (shMax)
                         + "    Int: " + fmt (integ);
        loudnessReadoutLabel.setText (txt, juce::dontSendNotification);
    }

    debugText.setText (processor.getLoudnessDebugText(), juce::dontSendNotification);

    // Disable Stretch control in 3.1 mode
    int numSpecOut = processor.getNumSpectralOutputs();
    bool enableStretch = numSpecOut > 3;
    stretchSlider.setEnabled (enableStretch);
    stretchLabel.setEnabled (enableStretch);

    // measurement UI hidden, logic kept for future use
    // if (processor.pendingLatencyMeasurement.load())
    // {
    //     latencyResultLabel.setText ("Measuring\u2026", juce::dontSendNotification);
    // }
    // else
    // {
    //     int measured = processor.measuredLatencySamples.load();
    //     if (measured != lastDisplayedLatency)
    //     {
    //         lastDisplayedLatency = measured;
    //         if (measured >= 0)
    //         {
    //             int expected = static_cast<int> (processor.getLatencySamples());
    //             if (measured == expected)
    //                 latencyResultLabel.setText ("Latency: " + juce::String (measured) + " samples (OK)",
    //                                             juce::dontSendNotification);
    //             else
    //                 latencyResultLabel.setText ("Latency: " + juce::String (measured)
    //                                             + " samples (expected " + juce::String (expected) + ")",
    //                                             juce::dontSendNotification);
    //         }
    //     }
    // }

    // Refresh gain panel if format changed while open
    if (chGainToggle.getToggleState() && numSpecOut != lastDetectedFormat)
    {
        lastDetectedFormat = numSpecOut;
        updateChGainPanel();
    }

    // Override: disable all upmix controls when bypassed
    {
        bool bypass = processor.isBypassed();

        lfeCutoffSlider.setEnabled (!bypass);
        lfeCutoffLabel.setEnabled  (!bypass);
        lfeLevelSlider.setEnabled  (!bypass);
        lfeLevelLabel.setEnabled   (!bypass);

        inFormatComboBox.setEnabled (!bypass);
        inFormatLabel.setEnabled    (!bypass);

        formatComboBox.setEnabled (!bypass);
        formatLabel.setEnabled    (!bypass);

        leakCenterSlider.setEnabled (!bypass);
        leakCenterLabel.setEnabled  (!bypass);

        crosstalkSlider.setEnabled  (!bypass);
        crosstalkLabel.setEnabled   (!bypass);
        rearBiasSlider.setEnabled   (!bypass);
        rearBiasLabel.setEnabled    (!bypass);

        stretchSlider.setEnabled    (!bypass);
        stretchLabel.setEnabled     (!bypass);

        chGainToggle.setEnabled (!bypass);
        for (int i = 0; i < numChOff; ++i)
        {
            chOffSliders[i].setEnabled (!bypass);
            chOffLabels[i].setEnabled  (!bypass);
        }
    }
}

void SpatialExpanderAudioProcessorEditor::updateFormatComboBox()
{
    auto busName = processor.getCurrentBusFormatName();
    int minOut = processor.getMinOutputFormat();
    static constexpr const char* fmtNames[] = { "Auto", "3.0", "5.1", "7.1", "9.1" };
    static constexpr int numFmts = 5;

    if (formatComboBox.getNumItems() == 0 ||
        ! formatComboBox.getItemText (0).contains (busName) ||
        lastMinOut != minOut)
    {
        auto currentId = formatComboBox.getSelectedId();

        // If the current parameter selects a format below minimum, reset to minOut
        auto& apvts = processor.getAPVTS();
        int currentParam = static_cast<int> (apvts.getRawParameterValue ("outputFormat")->load());
        if (currentParam != 0 && currentParam < minOut)
        {
            apvts.getParameter ("outputFormat")->setValueNotifyingHost (
                static_cast<float> (minOut) / static_cast<float> (numFmts - 1));
            currentId = minOut + 1;
        }

        formatComboBox.clear (juce::dontSendNotification);
        for (int i = 0; i < numFmts; ++i)
        {
            if (i > 0 && i < minOut) continue;  // skip formats below minimum
            juce::String label = (i == 0) ? "Auto (" + busName + ")" : fmtNames[i];
            formatComboBox.addItem (label, i + 1);
        }
        formatComboBox.setSelectedId (currentId, juce::dontSendNotification);
        lastMinOut = minOut;
    }
}

void SpatialExpanderAudioProcessorEditor::updateLatencyComboBox()
{
    auto sr = processor.getSampleRate();
    if (sr < 1000.0) sr = 48000.0;

    auto currentId = latencyComboBox.getSelectedId();

    const char* qualityLabels[] = { "Low/", "Low-Mid/", "Mid/", "High/" };
    latencyComboBox.clear (juce::dontSendNotification);
    for (int i = 0; i < 4; ++i)
    {
        int latency = SpatialExpanderAudioProcessor::getLatencySamplesForMode (i);
        double ms = static_cast<double> (latency) / sr * 1000.0;
        juce::String label = juce::String (qualityLabels[i]) + juce::String (latency) + " (" + juce::String (ms, 1) + " ms)";
        latencyComboBox.addItem (label, i + 1);
    }
    latencyComboBox.addItem ("Bypass/Realtime", 5);
    latencyComboBox.setSelectedId (currentId, juce::dontSendNotification);
}
