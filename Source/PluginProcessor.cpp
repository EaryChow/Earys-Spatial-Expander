#include "PluginProcessor.h"
#include "PluginEditor.h"

SpatialExpanderAudioProcessor::SpatialExpanderAudioProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::surround71(), true))
{
}

SpatialExpanderAudioProcessor::~SpatialExpanderAudioProcessor() {}

const juce::String SpatialExpanderAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SpatialExpanderAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::surround71())
        return false;
    return true;
}

void SpatialExpanderAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    stft.prepare (sampleRate);
    lfe.prepare (sampleRate, stft.getLatencySamples());
    setLatencySamples (stft.getLatencySamples());

    auto maxBlock = static_cast<size_t> (samplesPerBlock);
    stftOutBufL.assign (maxBlock, 0.0f);
    stftOutBufR.assign (maxBlock, 0.0f);
    lfeOutBuf.assign (maxBlock, 0.0f);
}

void SpatialExpanderAudioProcessor::releaseResources()
{
    stft.reset();
    lfe.reset();
}

void SpatialExpanderAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto numSamples = buffer.getNumSamples();
    auto numOut = getTotalNumOutputChannels();

    auto* inL = buffer.getReadPointer (0);
    auto* inR = buffer.getReadPointer (1);

    auto** channelData = buffer.getArrayOfWritePointers();
    for (int ch = 0; ch < numOut; ++ch)
        juce::FloatVectorOperations::fill (channelData[ch], 0.0f, numSamples);

    stft.process (inL, inR, stftOutBufL.data(), stftOutBufR.data(), numSamples);
    lfe.process (inL, inR, lfeOutBuf.data(), numSamples);

    if (numOut >= 1) juce::FloatVectorOperations::copy (channelData[0], stftOutBufL.data(), numSamples);
    if (numOut >= 2) juce::FloatVectorOperations::copy (channelData[1], stftOutBufR.data(), numSamples);
    if (numOut >= 4) juce::FloatVectorOperations::copy (channelData[3], lfeOutBuf.data(), numSamples);
}

bool SpatialExpanderAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* SpatialExpanderAudioProcessor::createEditor()
{
    return new SpatialExpanderAudioProcessorEditor (*this);
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
