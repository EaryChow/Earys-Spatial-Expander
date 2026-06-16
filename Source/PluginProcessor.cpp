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
              juce::NormalisableRange<float> (-60.0f, 12.0f, 0.5f), 0.0f),
          std::make_unique<juce::AudioParameterChoice> ("outputFormat", "Output Format",
              juce::StringArray { "Auto", "3.0", "5.1", "7.1", "9.1.6" }, 0),
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
    int idx = static_cast<int> (apvts.getRawParameterValue ("latency")->load());
    int order = 9 + idx;
    stft.setWindowSize (order);
    stft.prepare (sampleRate);
    analyser.prepare (stft.fftSize);

    int blockSize = samplesPerBlock;
    int fftSize   = stft.fftSize;
    int hopSize   = stft.hopSize;

    int firstOutput = blockSize * ((fftSize + hopSize - 1) / blockSize);
    int actualDelay = firstOutput - hopSize;
    delaySize       = fftSize - actualDelay;
    if (delaySize < 0) delaySize = 0;

    delayCapacity   = std::max (delaySize, blockSize) * 4;
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

    auto* inL = buffer.getReadPointer (0);
    auto* inR = (numInChannels > 1) ? buffer.getReadPointer (1) : buffer.getReadPointer (0);

    float lfeCutoff = apvts.getRawParameterValue ("lfeCutoff")->load();
    float lfeLevel = juce::Decibels::decibelsToGain (
        apvts.getRawParameterValue ("lfeLevel")->load());

    if (lfeCutoff != prevLfeCutoff)
    {
        lfe.setCutoff (lfeCutoff);
        prevLfeCutoff = lfeCutoff;
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
        for (int i = 0; i < numSamples; ++i)
            buffer.getWritePointer (3)[i] = chLFE[i] * lfeLevel;
    }
    if (numOutChannels > 4)
    {
        for (int c = 4; c < numOutChannels; ++c)
            buffer.clear (c, 0, numSamples);
    }
}

void SpatialExpanderAudioProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    if (parameterID == "latency")
    {
        pendingWindowOrder.store (9 + static_cast<int> (newValue));
        triggerAsyncUpdate();
    }
}

void SpatialExpanderAudioProcessor::handleAsyncUpdate()
{
    int newOrder = pendingWindowOrder.exchange (0);
    if (newOrder == 0)
        return;

    stft.setWindowSize (newOrder);
    analyser.prepare (stft.fftSize);

    int blockSize = lastBlockSize;
    if (blockSize < 1) blockSize = 512;
    int fftSize = stft.fftSize;
    int hopSize = stft.hopSize;

    int firstOutput = blockSize * ((fftSize + hopSize - 1) / blockSize);
    int actualDelay = firstOutput - hopSize;
    delaySize       = fftSize - actualDelay;
    if (delaySize < 0) delaySize = 0;
    delayCapacity   = std::max (delaySize, blockSize) * 4;
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

double SpatialExpanderAudioProcessor::getLatencyMs() const noexcept
{
    auto idx = static_cast<int> (apvts.getRawParameterValue ("latency")->load());
    int samples[] = { 512, 1024, 2048 };
    return static_cast<double> (samples[idx]) / getSampleRate() * 1000.0;
}

juce::String SpatialExpanderAudioProcessor::getMeasuredLatencyDebugString() const
{
    double sr = getSampleRate();
    if (sr < 1000.0) sr = 48000.0;

    int order = stft.fftOrder;
    if (order < 1) order = 9;
    int fftSize = 1 << order;
    int hopSize = fftSize / 2;
    int blockSize = lastBlockSize;
    if (blockSize < 1) blockSize = 512;

    int firstOutput = blockSize * ((fftSize + hopSize - 1) / blockSize);
    int actualDelay = firstOutput - hopSize;
    int extraDelay  = fftSize - actualDelay;
    if (extraDelay < 0) extraDelay = 0;
    int totalLatency = fftSize;

    int maxSamples = fftSize * 6;

    // Test A: Raw STFT (no listener) with DC input
    int rawSTFTLatency = -1;
    {
        StereoSTFT testSTFT;
        testSTFT.setWindowSize(order);
        testSTFT.prepare(sr);

        std::vector<float> inL(maxSamples, 1.0f), inR(maxSamples, 1.0f);
        std::vector<float> outC(maxSamples, 0.0f), outL(maxSamples, 0.0f), outR(maxSamples, 0.0f);

        for (int i = 0; i < maxSamples; i += blockSize)
        {
            int n = std::min(blockSize, maxSamples - i);
            testSTFT.process(inL.data() + i, inR.data() + i,
                             outC.data() + i, outL.data() + i, outR.data() + i, n);
        }

        for (int i = 0; i < maxSamples; ++i)
        {
            if (std::abs(outC[i]) > 0.5f || std::abs(outL[i]) > 0.5f)
            {
                rawSTFTLatency = i;
                break;
            }
        }
    }

    // Test B: STFT + SpatialAnalyser with DC input
    int spectralLatency = -1;
    {
        StereoSTFT testSTFT;
        testSTFT.setWindowSize(order);
        testSTFT.prepare(sr);

        SpatialAnalyser testAnalyser;
        testAnalyser.prepare(testSTFT.fftSize);
        testSTFT.setFrameListener(&testAnalyser);

        std::vector<float> inL(maxSamples, 1.0f), inR(maxSamples, 1.0f);
        std::vector<float> outC(maxSamples, 0.0f), outL(maxSamples, 0.0f), outR(maxSamples, 0.0f);

        for (int i = 0; i < maxSamples; i += blockSize)
        {
            int n = std::min(blockSize, maxSamples - i);
            testSTFT.process(inL.data() + i, inR.data() + i,
                             outC.data() + i, outL.data() + i, outR.data() + i, n);
        }

        for (int i = 0; i < maxSamples; ++i)
        {
            if (std::abs(outC[i]) > 0.5f || std::abs(outL[i]) > 0.5f || std::abs(outR[i]) > 0.5f)
            {
                spectralLatency = i;
                break;
            }
        }
    }

    // Test C: LFE with DC input
    int lfeLatency = -1;
    {
        float cutoff = 80.0f;
        if (auto* p = apvts.getRawParameterValue("lfeCutoff"))
            cutoff = p->load();

        LFEExtractor testLFE;
        testLFE.prepare(sr, totalLatency, cutoff);

        std::vector<float> inL(maxSamples, 1.0f), inR(maxSamples, 1.0f);
        std::vector<float> outLFE(maxSamples, 0.0f);

        for (int i = 0; i < maxSamples; i += blockSize)
        {
            int n = std::min(blockSize, maxSamples - i);
            testLFE.process(inL.data() + i, inR.data() + i, outLFE.data() + i, n);
        }

        for (int i = 0; i < maxSamples; ++i)
        {
            if (std::abs(outLFE[i]) > 0.5f)
            {
                lfeLatency = i;
                break;
            }
        }
    }

    return "RawSTFT=" + juce::String(rawSTFTLatency)
         + " Spectral=" + juce::String(spectralLatency)
         + " LFE=" + juce::String(lfeLatency)
         + " | STFT=" + juce::String(actualDelay)
         + " +Extra=" + juce::String(extraDelay)
         + " =Total=" + juce::String(totalLatency)
         + " BlockSize=" + juce::String(blockSize);
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
    if (layout == juce::AudioChannelSet::create9point1point6()) return "9.1.6";
    if (layout == juce::AudioChannelSet::createLCR())           return "3.0";
    if (layout == juce::AudioChannelSet::stereo())              return "Stereo";

    int n = layout.size();
    if (n == 3)  return "3.0 (LCR)";
    if (n == 4)  return "3.1 (4 ch)";
    if (n == 6)  return "5.1 (6 ch)";
    if (n == 8)  return "7.1 (8 ch)";
    if (n == 16) return "9.1.6 (16 ch)";
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
