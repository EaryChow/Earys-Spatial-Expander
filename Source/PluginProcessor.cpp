#include "PluginProcessor.h"
#include "PluginEditor.h"

static constexpr int chL = 0, chR = 1, chC = 2, chLFE = 3;

SpatialExpanderAudioProcessor::SpatialExpanderAudioProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::create5point1(), true)),
      apvts (*this, nullptr, "Parameters", {
          std::make_unique<juce::AudioParameterFloat> ("lfeCutoff", "LFE Cutoff",
              juce::NormalisableRange<float> (40.0f, 200.0f, 1.0f), 80.0f),
          std::make_unique<juce::AudioParameterFloat> ("lfeLevel", "LFE Level",
              juce::NormalisableRange<float> (-12.1f, 12.0f, 0.1f), 0.0f),
          std::make_unique<juce::AudioParameterChoice> ("outputFormat", "Output Format",
              juce::StringArray { "Auto", "3.0", "5.1", "7.1", "9.1 (in 9.1.6)" }, 0),
          std::make_unique<juce::AudioParameterChoice> ("latency", "Latency",
              juce::StringArray { "512", "1024", "2048" }, 1)
      })
{
    stft.setFrameListener (this);
    apvts.addParameterListener ("latency", this);
}

SpatialExpanderAudioProcessor::~SpatialExpanderAudioProcessor() {}

const juce::String SpatialExpanderAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SpatialExpanderAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    auto inSize = layouts.getMainInputChannelSet().size();
    auto outSize = layouts.getMainOutputChannelSet().size();

    if (inSize < 2 || outSize < 2)
        return false;

    auto out = layouts.getMainOutputChannelSet();
    if (out == juce::AudioChannelSet::stereo() ||
        out == juce::AudioChannelSet::createLCR() ||
        out == juce::AudioChannelSet::create5point1() ||
        out == juce::AudioChannelSet::create7point1() ||
        out == juce::AudioChannelSet::create9point1point6())
        return true;

    if (out.size() >= 4 && out.isDiscreteLayout())
        return true;

    return false;
}

void SpatialExpanderAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fadeSamplesTotal = static_cast<int> (sampleRate * 0.01); // 10 ms

    int idx = static_cast<int> (apvts.getRawParameterValue ("latency")->load());
    int order = 9 + idx;
    stft.setWindowSize (order);
    stft.prepare (sampleRate);
    analyser.prepare (stft.fftSize);

    int currentBlockSize = samplesPerBlock;
    int fftSize   = stft.fftSize;
    int hopSize   = stft.hopSize;

    int firstOutput = currentBlockSize * ((fftSize + hopSize - 1) / currentBlockSize);
    int actualDelay = firstOutput - hopSize;
    delaySize       = fftSize - actualDelay;
    if (delaySize < 0) delaySize = 0;

    delayCapacity   = std::max (delaySize, currentBlockSize) * 4;
    delayC.assign (delayCapacity, 0.0f);
    delayL.assign (delayCapacity, 0.0f);
    delayR.assign (delayCapacity, 0.0f);
    delayWritePos   = 0;

    int reportedLatency = fftSize;
    float cutoff = apvts.getRawParameterValue ("lfeCutoff")->load();
    lfe.prepare (sampleRate, reportedLatency, cutoff);
    setLatencySamples (reportedLatency);

    chC.resize (samplesPerBlock, 0.0f);
    chLres.resize (samplesPerBlock, 0.0f);
    chRres.resize (samplesPerBlock, 0.0f);
    chLFE.resize (samplesPerBlock, 0.0f);

    prevLfeCutoff = cutoff;
}

void SpatialExpanderAudioProcessor::releaseResources()
{
}

void SpatialExpanderAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    lastBlockSize = buffer.getNumSamples();
    auto numSamples = buffer.getNumSamples();
    auto numInChannels  = buffer.getNumChannels();
    auto numOutChannels = buffer.getNumChannels();

    // ===== Calibration state machine =====
    auto state = calState.load();

    // Reconfiguring: output zero while message thread finishes
    if (state == CalState::Reconfiguring)
    {
        for (int c = 0; c < numOutChannels; ++c)
            buffer.clear (c, 0, numSamples);
        return;
    }

    // --- Normal processing chain ---
    auto* inL = buffer.getReadPointer (0);
    auto* inR = (numInChannels > 1) ? buffer.getReadPointer (1) : buffer.getReadPointer (0);

    float lfeCutoff = apvts.getRawParameterValue ("lfeCutoff")->load();
    float lfeLevelDb = apvts.getRawParameterValue ("lfeLevel")->load();

    if (lfeCutoff != prevLfeCutoff)
    {
        lfe.setCutoff (lfeCutoff);
        prevLfeCutoff = lfeCutoff;
    }

    if (autoCalibrate.exchange (false))
    {
        // TODO: Actual calibration logic once SpatialRenderer is implemented
        // This hook will trigger gain-table measurement each time latency changes
    }

    stft.process (inL, inR, chC.data(), chLres.data(), chRres.data(), numSamples);
    lfe.process (inL, inR, chLFE.data(), numSamples);

    // Delay line: makes total latency exactly fftSize for all modes
    for (int i = 0; i < numSamples; ++i)
    {
        int readPos = (delayWritePos - delaySize + delayCapacity) % delayCapacity;

        float c = chC[i];
        float l = chLres[i];
        float r = chRres[i];

        chC[i]    = delayC[readPos];
        chLres[i] = delayL[readPos];
        chRres[i] = delayR[readPos];

        delayC[delayWritePos] = c;
        delayL[delayWritePos] = l;
        delayR[delayWritePos] = r;

        delayWritePos = (delayWritePos + 1) % delayCapacity;
    }

    if (numOutChannels > 0)
        juce::FloatVectorOperations::copy (buffer.getWritePointer (0), chLres.data(), numSamples);
    if (numOutChannels > 1)
        juce::FloatVectorOperations::copy (buffer.getWritePointer (1), chRres.data(), numSamples);
    if (numOutChannels > 2)
        juce::FloatVectorOperations::copy (buffer.getWritePointer (2), chC.data(), numSamples);
    if (numOutChannels > 3)
    {
        if (lfeLevelDb >= -12.0f)
        {
            float gain = juce::Decibels::decibelsToGain (lfeLevelDb);
            for (int i = 0; i < numSamples; ++i)
                buffer.getWritePointer (3)[i] = chLFE[i] * gain;
        }
        else
        {
            buffer.clear (3, 0, numSamples);
        }
    }
    if (numOutChannels > 4)
    {
        for (int c = 4; c < numOutChannels; ++c)
            buffer.clear (c, 0, numSamples);
    }

    // --- Fade-out (ramp to zero, then hand off reconfiguration to message thread) ---
    if (state == CalState::FadingOut)
    {
        int left = fadeSamplesLeft.load();
        int n = std::min (left, numSamples);
        for (int i = 0; i < n; ++i)
        {
            float gain = static_cast<float> (left) / static_cast<float> (fadeSamplesTotal);
            for (int c = 0; c < numOutChannels; ++c)
                buffer.getWritePointer (c)[i] *= gain;
            --left;
        }
        for (int i = n; i < numSamples; ++i)
            for (int c = 0; c < numOutChannels; ++c)
                buffer.getWritePointer (c)[i] = 0.0f;
        fadeSamplesLeft.store (left);

        if (left <= 0)
        {
            calState.store (CalState::Reconfiguring);
            triggerAsyncUpdate();
        }

        return;
    }

    // --- Fade-in (ramp up to unity) ---
    if (state == CalState::FadingIn)
    {
        int left = fadeSamplesLeft.load();
        int n = std::min (left, numSamples);
        int total = fadeSamplesTotal;
        for (int i = 0; i < n; ++i)
        {
            float gain = 1.0f - static_cast<float> (left - 1) / static_cast<float> (total);
            for (int c = 0; c < numOutChannels; ++c)
                buffer.getWritePointer (c)[i] *= gain;
            --left;
        }
        fadeSamplesLeft.store (left);

        if (left <= 0)
            calState.store (CalState::Normal);
    }
}

void SpatialExpanderAudioProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    if (parameterID == "latency")
    {
        // Guard: don't interrupt an in-progress calibration
        if (calState.load() != CalState::Normal)
            return;

        // Fallback: if not yet prepared, use legacy immediate path
        if (fadeSamplesTotal <= 0)
        {
            pendingWindowOrder.store (9 + static_cast<int> (newValue));
            triggerAsyncUpdate();
            return;
        }

        // Initiate calibrated latency change (fade-out → reconfig → fade-in)
        pendingWindowOrder.store (9 + static_cast<int> (newValue));
        fadeSamplesLeft.store (fadeSamplesTotal);
        calState.store (CalState::FadingOut);
    }
}

void SpatialExpanderAudioProcessor::handleAsyncUpdate()
{
    // Path 1: Audio thread finished fade-out; do real-time-unsafe reconfiguration here
    if (calState.load() == CalState::Reconfiguring)
    {
        int newOrder = pendingWindowOrder.exchange (0);
        if (newOrder > 0)
        {
            stft.setWindowSize (newOrder);
            analyser.prepare (stft.fftSize);

            int currentBlockSize = lastBlockSize;
            if (currentBlockSize < 1) currentBlockSize = 512;
            int fftSize = stft.fftSize;
            int hopSize = stft.hopSize;

            int firstOutput = currentBlockSize * ((fftSize + hopSize - 1) / currentBlockSize);
            int actualDelay = firstOutput - hopSize;
            delaySize       = fftSize - actualDelay;
            if (delaySize < 0) delaySize = 0;
    delayCapacity   = std::max (delaySize, currentBlockSize) * 4;
            delayC.assign (delayCapacity, 0.0f);
            delayL.assign (delayCapacity, 0.0f);
            delayR.assign (delayCapacity, 0.0f);
            delayWritePos   = 0;

            float cutoff = apvts.getRawParameterValue ("lfeCutoff")->load();
            lfe.prepare (getSampleRate(), fftSize, cutoff);
            prevLfeCutoff = cutoff;
        }

        setLatencySamples (stft.fftSize);
        updateHostDisplay();
        triggerCalibration();
        fadeSamplesLeft.store (fadeSamplesTotal);
        calState.store (CalState::FadingIn);
        return;
    }

    // Path 2: Legacy immediate reconfig (initial prepareToPlay or non-fade fallback)
    int newOrder = pendingWindowOrder.exchange (0);
    if (newOrder == 0)
        return;

    stft.setWindowSize (newOrder);
    analyser.prepare (stft.fftSize);

    int currentBlockSize = lastBlockSize;
    if (currentBlockSize < 1) currentBlockSize = 512;
    int fftSize = stft.fftSize;
    int hopSize = stft.hopSize;

    int firstOutput = currentBlockSize * ((fftSize + hopSize - 1) / currentBlockSize);
    int actualDelay = firstOutput - hopSize;
    delaySize       = fftSize - actualDelay;
    if (delaySize < 0) delaySize = 0;
    delayCapacity   = std::max (delaySize, currentBlockSize) * 4;
    delayC.assign (delayCapacity, 0.0f);
    delayL.assign (delayCapacity, 0.0f);
    delayR.assign (delayCapacity, 0.0f);
    delayWritePos   = 0;

    int reportedLatency = fftSize;
    float cutoff = apvts.getRawParameterValue ("lfeCutoff")->load();
    lfe.prepare (getSampleRate(), reportedLatency, cutoff);
    setLatencySamples (reportedLatency);
    updateHostDisplay();
}

void SpatialExpanderAudioProcessor::triggerCalibration()
{
    autoCalibrate.store (true);
}

double SpatialExpanderAudioProcessor::getLatencyMs() const noexcept
{
    auto idx = static_cast<int> (apvts.getRawParameterValue ("latency")->load());
    int samples[] = { 512, 1024, 2048 };
    return static_cast<double> (samples[idx]) / getSampleRate() * 1000.0;
}



void SpatialExpanderAudioProcessor::onFrame (const float* fftL, const float* fftR,
                                              float* fftC, float* fftLres, float* fftRres,
                                              int fftSize)
{
    analyser.onFrame (fftL, fftR, fftC, fftLres, fftRres, fftSize);
}

bool SpatialExpanderAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* SpatialExpanderAudioProcessor::createEditor()
{
    return new SpatialExpanderAudioProcessorEditor (*this);
}

juce::String SpatialExpanderAudioProcessor::getInputWarningText() const
{
    auto input = getBus (true, 0);
    if (input == nullptr)
        return {};

    auto layout = input->getCurrentLayout();
    if (layout.size() > 2)
        return "Input has " + juce::String (layout.size()) + " channels; only L/R are used.";
    return {};
}

juce::String SpatialExpanderAudioProcessor::getFormatWarningText (int selectedFormat) const
{
    auto output = getBus (false, 0);
    if (output == nullptr)
        return {};

    int busChannels = output->getCurrentLayout().size();
    int neededChannels = 0;

    switch (selectedFormat)
    {
        case Fmt30:  neededChannels = 3;  break;
        case Fmt51:  neededChannels = 6;  break;
        case Fmt71:  neededChannels = 8;  break;
        case Fmt916: neededChannels = 16; break;
        default: return {};
    }

    if (busChannels < neededChannels)
        return "Bus has only " + juce::String (busChannels) + " ch; "
               + juce::String (neededChannels) + " ch needed for selected format. Extra channels discarded.";

    return {};
}

juce::String SpatialExpanderAudioProcessor::getCurrentBusFormatName() const
{
    auto output = getBus (false, 0);
    if (output == nullptr)
        return "?";

    auto layout = output->getCurrentLayout();
    if (layout == juce::AudioChannelSet::create5point1())       return "5.1";
    if (layout == juce::AudioChannelSet::create7point1())       return "7.1";
    if (layout == juce::AudioChannelSet::create9point1point6()) return "9.1 (in 9.1.6)";
    if (layout == juce::AudioChannelSet::createLCR())           return "3.0";
    if (layout == juce::AudioChannelSet::stereo())              return "Stereo";

    int n = layout.size();
    if (n == 3)  return "3.0 (LCR)";
    if (n == 4)  return "3.1 (4 ch)";
    if (n == 6)  return "5.1 (6 ch)";
    if (n == 8)  return "7.1 (8 ch)";
    if (n == 16) return "9.1 (in 9.1.6) (16 ch)";
    return juce::String (n) + " ch";
}

void SpatialExpanderAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ignoreUnused (destData);
}

void SpatialExpanderAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::ignoreUnused (data, sizeInBytes);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpatialExpanderAudioProcessor();
}
