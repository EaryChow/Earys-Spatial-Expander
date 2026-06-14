#include "PluginProcessor.h"
#include "PluginEditor.h"

SpatialExpanderAudioProcessor::SpatialExpanderAudioProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::create7point1(), true))
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
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::create7point1())
        return false;
    return true;
}

void SpatialExpanderAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    renderer.prepare (sampleRate);
    lfe.prepare (sampleRate, renderer.getLatencySamples());
    setLatencySamples (renderer.getLatencySamples());

    auto maxBlock = static_cast<size_t> (samplesPerBlock);
    lfeOutBuf.assign (maxBlock, 0.0f);
}

void SpatialExpanderAudioProcessor::releaseResources()
{
    renderer.reset();
    lfe.reset();
}

void SpatialExpanderAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto numSamples = buffer.getNumSamples();
    auto numOut = getTotalNumOutputChannels();

    auto* inL = buffer.getReadPointer (0);
    auto* inR = buffer.getReadPointer (1);
    auto* const* channelData = buffer.getArrayOfWritePointers();

    // Zero non-stereo channels (renderer writes L/R, LFE writes ch3)
    for (int ch = 2; ch < numOut; ++ch)
        juce::FloatVectorOperations::fill (channelData[ch], 0.0f, numSamples);

    // Route renderer ground channels to JUCE 7.1 bus positions:
    //   Lb(6), Ls(4), L(0), C(2), R(1), Rs(5), Rb(7)
    float* groundPtrs[SpatialRenderer::numGroundChannels];
    groundPtrs[SpatialRenderer::Lb] = (numOut > 6) ? channelData[6] : channelData[0];
    groundPtrs[SpatialRenderer::Ls] = (numOut > 4) ? channelData[4] : channelData[0];
    groundPtrs[SpatialRenderer::L]  = channelData[0];
    groundPtrs[SpatialRenderer::C]  = (numOut > 2) ? channelData[2] : channelData[0];
    groundPtrs[SpatialRenderer::R]  = channelData[1];
    groundPtrs[SpatialRenderer::Rs] = (numOut > 5) ? channelData[5] : channelData[1];
    groundPtrs[SpatialRenderer::Rb] = (numOut > 7) ? channelData[7] : channelData[1];

    renderer.process (inL, inR, groundPtrs, numSamples);
    lfe.process (inL, inR, lfeOutBuf.data(), numSamples);

    if (numOut >= 4)
        juce::FloatVectorOperations::copy (channelData[3], lfeOutBuf.data(), numSamples);
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
