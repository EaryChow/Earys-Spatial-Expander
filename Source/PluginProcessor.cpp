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
              juce::StringArray { "Auto", "3.0", "5.1", "7.1", "9.1" }, 0),
          std::make_unique<juce::AudioParameterChoice> ("latency", "Latency",
              juce::StringArray { "512", "1024", "2048" }, 1),
          std::make_unique<juce::AudioParameterFloat> ("leakCenter", "Leak Center",
              juce::NormalisableRange<float> (0.0f, 1.5f, 0.01f), 0.0f),
          std::make_unique<juce::AudioParameterFloat> ("stretch", "Stretch",
              juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f),
          std::make_unique<juce::AudioParameterFloat> ("preamp", "Preamp",
              juce::NormalisableRange<float> (-6.0f, 6.0f, 0.1f), 0.0f),
          std::make_unique<juce::AudioParameterBool> ("rearIsolation", "5.1 Rear Channel Isolation", true),
          std::make_unique<juce::AudioParameterFloat> ("chOffC", "Center Offset",
              juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f),
          std::make_unique<juce::AudioParameterFloat> ("chOffFL", "Front L Offset",
              juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f),
          std::make_unique<juce::AudioParameterFloat> ("chOffFR", "Front R Offset",
              juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f),
          std::make_unique<juce::AudioParameterFloat> ("chOffWL", "Wide L Offset",
              juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f),
          std::make_unique<juce::AudioParameterFloat> ("chOffWR", "Wide R Offset",
              juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f),
          std::make_unique<juce::AudioParameterFloat> ("chOffSL", "Side L Offset",
              juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f),
          std::make_unique<juce::AudioParameterFloat> ("chOffSR", "Side R Offset",
              juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f),
          std::make_unique<juce::AudioParameterFloat> ("chOffRL", "Rear L Offset",
              juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f),
          std::make_unique<juce::AudioParameterFloat> ("chOffRR", "Rear R Offset",
              juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f)
      })
{
    stft.setFrameListener (this);
    apvts.addParameterListener ("latency", this);
    apvts.addParameterListener ("outputFormat", this);

    gainTable.resize (ildTableSize, 1.0f);
}

SpatialExpanderAudioProcessor::~SpatialExpanderAudioProcessor() {}

const juce::String SpatialExpanderAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SpatialExpanderAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    auto inLayout = layouts.getMainInputChannelSet();
    auto outLayout = layouts.getMainOutputChannelSet();
    auto inSize = inLayout.size();
    auto outSize = outLayout.size();

    if (inSize < 2 || outSize < 2)
        return false;

    // Standard DAWs force input == output on the same track/bus.
    if (inLayout == outLayout)
        return true;

    // Hosts often use discrete/custom channel sets for surround that don't
    // exactly match JUCE's named layouts. Accept same channel count.
    if (inSize == outSize)
        return true;

    // For node-based hosts that allow independent I/O (e.g. Cantabile).
    if (inLayout == juce::AudioChannelSet::stereo())
        return true;

    // Fallback: accept any layout with at least stereo input and output.
    // We only read input channels 0-1 (L/R) and explicitly map all outputs
    // via channelMap, so any channel count is safe.
    return true;
}

int SpatialExpanderAudioProcessor::detectFormatFromBus() const noexcept
{
    auto output = getBus (false, 0);
    if (output == nullptr) return Fmt30;

    auto layout = output->getCurrentLayout();
    int n = layout.size();

    if (n <= 2) return Fmt30;
    if (n <= 4) return Fmt30;

    bool hasWide = false;
    bool hasSide = false;
    bool hasRear = false;
    bool hasGenericSurround = false;

    for (int ch = 0; ch < n; ++ch)
    {
        auto type = layout.getTypeOfChannel (ch);
        if (type == juce::AudioChannelSet::wideLeft || type == juce::AudioChannelSet::wideRight ||
            type == juce::AudioChannelSet::leftCentre || type == juce::AudioChannelSet::rightCentre)
            hasWide = true;
        else if (type == juce::AudioChannelSet::leftSurroundSide || type == juce::AudioChannelSet::rightSurroundSide)
            hasSide = true;
        else if (type == juce::AudioChannelSet::leftSurroundRear || type == juce::AudioChannelSet::rightSurroundRear)
            hasRear = true;
        else if (type == juce::AudioChannelSet::leftSurround || type == juce::AudioChannelSet::rightSurround)
            hasGenericSurround = true;
    }

    // 9.1.x: wide channels present (aliases catch ITU leftCentre too)
    if (hasWide) return Fmt916;

    // 7.1.x: BOTH side and rear specific types must be present.
    // A single specific type (e.g. leftSurroundSide only) is treated as 5.1
    // to avoid misclassifying 5.1.2/5.1.4 where the DAW uses non-standard naming.
    if (hasSide && hasRear) return Fmt71;

    // 5.1.x: generic surround present with no side/rear distinction.
    // Extra channels are assumed to be height/top.
    if (hasGenericSurround && !hasSide && !hasRear) return Fmt51;

    // If only one specific surround type is present (and no generic),
    // default to 5.1 for safety — don't generate unused 7.1 spectral channels.
    if ((hasSide || hasRear) && !hasGenericSurround) return Fmt51;

    // Named layout fallbacks for hosts that use standard JUCE layouts
    if (layout == juce::AudioChannelSet::create9point1point6()) return Fmt916;
    if (layout == juce::AudioChannelSet::create7point1())       return Fmt71;
    if (layout == juce::AudioChannelSet::create5point1())       return Fmt51;
    if (layout == juce::AudioChannelSet::createLCR())           return Fmt30;

    // Discrete / unknown layout fallbacks by channel count.
    // Conservative: prefer smaller bed formats to avoid generating
    // spectral channels that have no physical destination.
    if (n >= 16) return Fmt916;  // 9.1.6
    if (n >= 14) return Fmt916;  // 9.1.4 (or 7.1.6 — 9.1 is the more common 14-ch bed)
    if (n >= 12) return Fmt71;   // 7.1.4 or 9.1.2
    if (n >= 10) return Fmt71;   // 7.1.2 or 9.1.0
    if (n >= 8)  return Fmt51;   // 5.1.2 or 7.1 — prefer 5.1 (safer default)
    if (n >= 6)  return Fmt51;
    return Fmt30;
}

int SpatialExpanderAudioProcessor::getNumSpectralOutputs() const noexcept
{
    int fmt = static_cast<int> (apvts.getRawParameterValue ("outputFormat")->load());
    int effective = (fmt == Auto) ? detectFormatFromBus() : fmt;

    switch (effective)
    {
        case Fmt30:  return 3;
        case Fmt51:  return 5;
        case Fmt71:  return 7;
        case Fmt916: return 9;
        default:     return 3;
    }
}

void SpatialExpanderAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fadeSamplesTotal = static_cast<int> (sampleRate * 0.01);

    int idx = static_cast<int> (apvts.getRawParameterValue ("latency")->load());
    int order = 9 + idx;

    int numSpecOut = getNumSpectralOutputs();
    stft.setNumOutputs (numSpecOut);
    stft.setWindowSize (order);
    stft.prepare (sampleRate);
    analyser.prepare (stft.fftSize);

    int currentBlockSize = samplesPerBlock;
    int fftSize   = stft.fftSize;
    int hopSize   = stft.hopSize;

    int overlapFactor = fftSize / hopSize;

    // First block boundary that contains the last frame needed for COLA
    int firstOutput = currentBlockSize * ((fftSize + (overlapFactor - 1) * hopSize - 1) / currentBlockSize);

    // Causal inherent delay: first sample with all overlapFactor frames contributing
    int inherentDelay = fftSize - hopSize;   // = (overlapFactor - 1) * hopSize

    int actualDelay   = firstOutput - inherentDelay;
    delaySize         = fftSize - actualDelay;
    if (delaySize < 0) delaySize = 0;

    delayCapacity   = std::max (delaySize, currentBlockSize) * 4;
    delayBufs.resize (numSpecOut);
    for (auto& buf : delayBufs)
        buf.assign (delayCapacity, 0.0f);
    delayWritePos   = 0;

    int reportedLatency = fftSize;
    float cutoff = apvts.getRawParameterValue ("lfeCutoff")->load();
    lfe.prepare (sampleRate, reportedLatency, cutoff);
    setLatencySamples (reportedLatency);

    chOutputs.resize (numSpecOut);
    for (auto& buf : chOutputs)
        buf.resize (samplesPerBlock, 0.0f);
    chOutputPtrs.resize (numSpecOut);
    for (int ch = 0; ch < numSpecOut; ++ch)
        chOutputPtrs[ch] = chOutputs[ch].data();
    chLFE.resize (samplesPerBlock, 0.0f);

    prevLfeCutoff = cutoff;

    // Cache channel offset parameter pointers
    static constexpr const char* chOffIds[] = { "chOffC", "chOffFL", "chOffFR", "chOffWL", "chOffWR", "chOffSL", "chOffSR", "chOffRL", "chOffRR" };
    for (int i = 0; i < 9; ++i)
        chOffParams[i] = apvts.getRawParameterValue (chOffIds[i]);

    // Pre-allocate cascade scratch buffers for audio-thread safety
    cascadeFftSize = stft.fftSize;
    int cs = cascadeFftSize;
    cascadeCenter.assign (cs, 0.0f);
    cascadeFrontL.assign (cs, 0.0f);
    cascadeFrontR.assign (cs, 0.0f);
    cascadeWideL.assign (cs, 0.0f);
    cascadeWideR.assign (cs, 0.0f);
    cascadeSideL.assign (cs, 0.0f);
    cascadeSideR.assign (cs, 0.0f);
    cascadeRearL.assign (cs, 0.0f);
    cascadeRearR.assign (cs, 0.0f);
    cascadeTemp.assign (cs, 0.0f);
    cascadeLresSave.assign (cs, 0.0f);
    cascadeRresSave.assign (cs, 0.0f);

    runCalibration();
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

    int expectedSpecOut = getNumSpectralOutputs();
    if (static_cast<int> (chOutputs.size()) != expectedSpecOut ||
        stft.numOutputs != expectedSpecOut)
    {
        if (calState.load() == CalState::Normal)
        {
            pendingFormatChange.store (true);
            triggerAsyncUpdate();
        }
    }

    auto state = calState.load();

    if (state == CalState::Reconfiguring)
    {
        for (int c = 0; c < numOutChannels; ++c)
            buffer.clear (c, 0, numSamples);
        return;
    }

    auto* inL = buffer.getReadPointer (0);
    auto* inR = (numInChannels > 1) ? buffer.getReadPointer (1) : buffer.getReadPointer (0);

    float lfeCutoff = apvts.getRawParameterValue ("lfeCutoff")->load();
    float lfeLevelDb = apvts.getRawParameterValue ("lfeLevel")->load();

    float preampDb = apvts.getRawParameterValue ("preamp")->load();
    float preampGain = juce::Decibels::decibelsToGain (preampDb);
    if (preampGain != 1.0f)
        buffer.applyGain (preampGain);

    if (lfeCutoff != prevLfeCutoff)
    {
        lfe.setCutoff (lfeCutoff);
        prevLfeCutoff = lfeCutoff;
    }

    int numSpecOut = static_cast<int> (chOutputs.size());

    stft.process (inL, inR, chOutputPtrs.data(), numSamples);
    lfe.process (inL, inR, chLFE.data(), numSamples);

    // Delay line: makes total latency exactly fftSize
    for (int i = 0; i < numSamples; ++i)
    {
        int readPos = (delayWritePos - delaySize + delayCapacity) % delayCapacity;

        for (int ch = 0; ch < numSpecOut; ++ch)
        {
            float input = chOutputs[ch][i];
            chOutputs[ch][i] = delayBufs[ch][readPos];
            delayBufs[ch][delayWritePos] = input;
        }

        delayWritePos = (delayWritePos + 1) % delayCapacity;
    }

    // Leak Center: redistribute calibrated Center to Front L/R
    {
        float leak = apvts.getRawParameterValue ("leakCenter")->load();
        if (leak > 0.0f && numSpecOut >= 3)
        {
            float denom = std::sqrt (1.0f + 2.0f * leak);
            float centerAmount = 1.0f / denom;
            float sideAmount   = std::sqrt (leak) / denom;
            for (int i = 0; i < numSamples; ++i)
            {
                float cOrig = chOutputs[0][i];
                chOutputs[0][i] = cOrig * centerAmount;
                chOutputs[1][i] += cOrig * sideAmount;
                chOutputs[2][i] += cOrig * sideAmount;
            }
        }
    }

    // -----------------------------------------------------------------
    // Hybrid routing: type-based primary with comprehensive aliases,
    // index-based fallback for unknown/discrete channels.
    //
    // Comprehensive alias system:
    //   WideL:  wideLeft (Atmos), leftCentre (ITU), leftSurround (last resort)
    //   WideR:  wideRight (Atmos), rightCentre (ITU), rightSurround (last resort)
    //   SideL:  leftSurroundSide (Atmos Lss), leftSurround (ITU Ls/generic),
    //           leftSurroundRear (DAWs that swap side/rear naming)
    //   SideR:  rightSurroundSide, rightSurround, rightSurroundRear
    //   RearL:  leftSurroundRear (Atmos Lsr), leftSurround (ITU/generic),
    //           leftSurroundSide (DAWs that swap side/rear naming)
    //   RearR:  rightSurroundRear, rightSurround, rightSurroundSide
    //
    // Index fallback for 16-ch (user's DAW buffer order):
    //   0-3:  L R C LFE
    //   4-5:  RearL/R  (swapped - confirmed by user testing)
    //   6-7:  SideL/R  (swapped - confirmed by user testing)
    //   8-9:  WideL/R
    //   10+:  top channels (unmapped)
    // -----------------------------------------------------------------
    auto outputBus = getBus (false, 0);
    auto outputLayout = (outputBus != nullptr) ? outputBus->getCurrentLayout() : juce::AudioChannelSet();
    numOutChannels = buffer.getNumChannels();
    numSpecOut = static_cast<int> (chOutputs.size());

    bool is3_0 = (numSpecOut <= 3);
    bool is5_1 = (numSpecOut > 3 && numSpecOut <= 5);
    bool is7_1 = (numSpecOut > 5 && numSpecOut <= 7);
    bool is9_1 = (numSpecOut > 7);

    float lfeGain = (lfeLevelDb >= -12.0f) ? juce::Decibels::decibelsToGain (lfeLevelDb) : 0.0f;

    std::vector<int> channelMap (numOutChannels, -1);
    std::vector<bool> channelIsLFE (numOutChannels, false);

    // Step 1: Front channels & LFE (type is reliable, index fallback for discrete)
    for (int ch = 0; ch < numOutChannels; ++ch)
    {
        auto type = outputLayout.getTypeOfChannel (ch);

        if (type == juce::AudioChannelSet::left || (type == juce::AudioChannelSet::unknown && ch == 0))
            channelMap[ch] = 1;
        else if (type == juce::AudioChannelSet::right || (type == juce::AudioChannelSet::unknown && ch == 1))
            channelMap[ch] = 2;
        else if (type == juce::AudioChannelSet::centre || (type == juce::AudioChannelSet::unknown && ch == 2))
            channelMap[ch] = 0;
        else if (type == juce::AudioChannelSet::LFE || (type == juce::AudioChannelSet::unknown && ch == 3))
            channelIsLFE[ch] = true;
    }

    // Step 2: Type-based assignment with comprehensive aliases
    auto isOutputTaken = [&](int output) -> bool
    {
        for (int c = 0; c < numOutChannels; ++c)
            if (channelMap[c] == output) return true;
        return false;
    };

    auto findChannelByType = [&](juce::AudioChannelSet::ChannelType type) -> int
    {
        for (int ch = 0; ch < numOutChannels; ++ch)
        {
            if (channelMap[ch] >= 0 || channelIsLFE[ch]) continue;
            if (outputLayout.getTypeOfChannel (ch) == type) return ch;
        }
        return -1;
    };

    struct RoleCandidate
    {
        int spectralOutput;
        std::vector<juce::AudioChannelSet::ChannelType> types;
    };

    std::vector<RoleCandidate> candidates;

    if (is9_1)
    {
        candidates.push_back ({3, {juce::AudioChannelSet::wideLeft, juce::AudioChannelSet::leftCentre}});
        candidates.push_back ({4, {juce::AudioChannelSet::wideRight, juce::AudioChannelSet::rightCentre}});
        candidates.push_back ({5, {juce::AudioChannelSet::leftSurroundSide, juce::AudioChannelSet::leftSurround}});
        candidates.push_back ({6, {juce::AudioChannelSet::rightSurroundSide, juce::AudioChannelSet::rightSurround}});
        candidates.push_back ({7, {juce::AudioChannelSet::leftSurroundRear, juce::AudioChannelSet::leftSurround}});
        candidates.push_back ({8, {juce::AudioChannelSet::rightSurroundRear, juce::AudioChannelSet::rightSurround}});
    }
    else if (is7_1)
    {
        candidates.push_back ({3, {juce::AudioChannelSet::leftSurroundSide, juce::AudioChannelSet::leftSurround}});
        candidates.push_back ({4, {juce::AudioChannelSet::rightSurroundSide, juce::AudioChannelSet::rightSurround}});
        candidates.push_back ({5, {juce::AudioChannelSet::leftSurroundRear, juce::AudioChannelSet::leftSurround}});
        candidates.push_back ({6, {juce::AudioChannelSet::rightSurroundRear, juce::AudioChannelSet::rightSurround}});
    }
    else if (is5_1)
    {
        candidates.push_back ({3, {juce::AudioChannelSet::leftSurroundRear, juce::AudioChannelSet::leftSurround, juce::AudioChannelSet::leftSurroundSide}});
        candidates.push_back ({4, {juce::AudioChannelSet::rightSurroundRear, juce::AudioChannelSet::rightSurround, juce::AudioChannelSet::rightSurroundSide}});
    }

    // Pass 1: specific types (first in each list)
    for (const auto& role : candidates)
    {
        if (role.spectralOutput >= numSpecOut) continue;
        if (isOutputTaken (role.spectralOutput)) continue;
        if (role.types.empty()) continue;

        int ch = findChannelByType (role.types[0]);
        if (ch >= 0) channelMap[ch] = role.spectralOutput;
    }

    // Pass 2: generic fallback types (second in each list)
    for (const auto& role : candidates)
    {
        if (role.spectralOutput >= numSpecOut) continue;
        if (isOutputTaken (role.spectralOutput)) continue;
        if (role.types.size() < 2) continue;

        int ch = findChannelByType (role.types[1]);
        if (ch >= 0) channelMap[ch] = role.spectralOutput;
    }

    // Pass 3: last-resort aliases (third in each list)
    for (const auto& role : candidates)
    {
        if (role.spectralOutput >= numSpecOut) continue;
        if (isOutputTaken (role.spectralOutput)) continue;
        if (role.types.size() < 3) continue;

        int ch = findChannelByType (role.types[2]);
        if (ch >= 0) channelMap[ch] = role.spectralOutput;
    }

    // Step 3: Index-based fallback for unknown/discrete channels ONLY
    auto isUnmappedDiscrete = [&](int ch) -> bool
    {
        return channelMap[ch] < 0 && !channelIsLFE[ch]
            && outputLayout.getTypeOfChannel(ch) == juce::AudioChannelSet::unknown;
    };

    if (numOutChannels == 6)
    {
        if (is5_1 && isUnmappedDiscrete(4)) channelMap[4] = 3;
        if (is5_1 && isUnmappedDiscrete(5)) channelMap[5] = 4;
    }
    else if (numOutChannels == 8)
    {
        if (is7_1)
        {
            if (isUnmappedDiscrete(4)) channelMap[4] = 3;
            if (isUnmappedDiscrete(5)) channelMap[5] = 4;
            if (isUnmappedDiscrete(6)) channelMap[6] = 5;
            if (isUnmappedDiscrete(7)) channelMap[7] = 6;
        }
        else if (is5_1)
        {
            if (isUnmappedDiscrete(6)) channelMap[6] = 3;
            if (isUnmappedDiscrete(7)) channelMap[7] = 4;
        }
    }
    else if (numOutChannels >= 10)   // Any 9.1.x bus: 9.1.0 (10ch), 9.1.2 (12ch), 9.1.4 (14ch), 9.1.6 (16ch)
    {
        // Standard JUCE/Atmos bed order: L R C LFE Lss Rss Lsr Rsr Lw Rw
        // Top/height channels appended at index 10+ remain silent.
        if (is9_1)
        {
            if (isUnmappedDiscrete(4)) channelMap[4] = 5;   // SideL  -> Lss
            if (isUnmappedDiscrete(5)) channelMap[5] = 6;   // SideR  -> Rss
            if (isUnmappedDiscrete(6)) channelMap[6] = 7;   // RearL  -> Lsr
            if (isUnmappedDiscrete(7)) channelMap[7] = 8;   // RearR  -> Rsr
            if (isUnmappedDiscrete(8)) channelMap[8] = 3;   // WideL  -> Lw
            if (isUnmappedDiscrete(9)) channelMap[9] = 4;   // WideR  -> Rw
        }
        else if (is7_1)
        {
            if (isUnmappedDiscrete(4)) channelMap[4] = 3;   // SideL  -> Lss
            if (isUnmappedDiscrete(5)) channelMap[5] = 4;   // SideR  -> Rss
            if (isUnmappedDiscrete(6)) channelMap[6] = 5;   // RearL  -> Lsr
            if (isUnmappedDiscrete(7)) channelMap[7] = 6;   // RearR  -> Rsr
        }
        else if (is5_1)
        {
            // 5.1 surrounds should be the rear channels
            if (isUnmappedDiscrete(6)) channelMap[6] = 3;   // RearL  -> Lsr
            if (isUnmappedDiscrete(7)) channelMap[7] = 4;   // RearR  -> Rsr
        }
    }
    else
    {
        // Unknown bus size: type-based fallback of last resort
        for (int ch = 0; ch < numOutChannels; ++ch)
        {
            if (channelMap[ch] >= 0 || channelIsLFE[ch]) continue;
            auto type = outputLayout.getTypeOfChannel (ch);

            if (type == juce::AudioChannelSet::wideLeft && is9_1)
                channelMap[ch] = 3;
            else if (type == juce::AudioChannelSet::wideRight && is9_1)
                channelMap[ch] = 4;
            else if (type == juce::AudioChannelSet::leftSurroundSide || type == juce::AudioChannelSet::leftSurround)
            {
                if (is9_1)      channelMap[ch] = 5;
                else if (is7_1) channelMap[ch] = 3;
                else if (is5_1) channelMap[ch] = 3;
            }
            else if (type == juce::AudioChannelSet::rightSurroundSide || type == juce::AudioChannelSet::rightSurround)
            {
                if (is9_1)      channelMap[ch] = 6;
                else if (is7_1) channelMap[ch] = 4;
                else if (is5_1) channelMap[ch] = 4;
            }
            else if (type == juce::AudioChannelSet::leftSurroundRear)
            {
                if (is9_1)      channelMap[ch] = 7;
                else if (is7_1) channelMap[ch] = 5;
                else if (is5_1) channelMap[ch] = 3;
            }
            else if (type == juce::AudioChannelSet::rightSurroundRear)
            {
                if (is9_1)      channelMap[ch] = 8;
                else if (is7_1) channelMap[ch] = 6;
                else if (is5_1) channelMap[ch] = 4;
            }
        }
    }

    // Apply the map
    for (int ch = 0; ch < numOutChannels; ++ch)
    {
        if (is3_0 && channelIsLFE[ch])
        {
            buffer.clear (ch, 0, numSamples);
            continue;
        }

        if (channelIsLFE[ch])
        {
            if (lfeGain > 0.0f)
                juce::FloatVectorOperations::multiply (buffer.getWritePointer (ch), chLFE.data(), lfeGain, numSamples);
            else
                buffer.clear (ch, 0, numSamples);
        }
        else if (channelMap[ch] >= 0 && channelMap[ch] < numSpecOut)
        {
            juce::FloatVectorOperations::copy (buffer.getWritePointer (ch), chOutputs[channelMap[ch]].data(), numSamples);
        }
        else
        {
            buffer.clear (ch, 0, numSamples);
        }
    }

    // Fade-out
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

    // Fade-in
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

void SpatialExpanderAudioProcessor::parameterChanged (const juce::String& parameterID, float /*newValue*/)
{
    if (parameterID == "latency")
    {
        if (calState.load() != CalState::Normal)
            return;

        pendingWindowOrder.store (9 + static_cast<int> (apvts.getRawParameterValue ("latency")->load()));
        fadeSamplesLeft.store (fadeSamplesTotal);
        calState.store (CalState::FadingOut);
    }
    else if (parameterID == "outputFormat")
    {
        if (calState.load() != CalState::Normal)
            return;

        pendingFormatChange.store (true);
        fadeSamplesLeft.store (fadeSamplesTotal);
        calState.store (CalState::FadingOut);
    }
}

void SpatialExpanderAudioProcessor::handleAsyncUpdate()
{
    if (calState.load() != CalState::Reconfiguring)
        return;

    int newOrder = pendingWindowOrder.exchange (0);
    bool fmtChanged = pendingFormatChange.exchange (false);

    if (newOrder > 0 || fmtChanged)
    {
        int numSpecOut = getNumSpectralOutputs();
        int currentBlockSize = lastBlockSize;
        if (currentBlockSize < 1) currentBlockSize = 512;

        if (newOrder > 0)
        {
            stft.setWindowSize (newOrder);
            analyser.prepare (stft.fftSize);
        }

        int fftSize = stft.fftSize;
        int hopSize = stft.hopSize;

        stft.setNumOutputs (numSpecOut);

        int overlapFactor = fftSize / hopSize;

        int firstOutput = currentBlockSize * ((fftSize + (overlapFactor - 1) * hopSize - 1) / currentBlockSize);
        int inherentDelay = fftSize - hopSize;

        int actualDelay   = firstOutput - inherentDelay;
        delaySize         = fftSize - actualDelay;
        if (delaySize < 0) delaySize = 0;
        delayCapacity   = std::max (delaySize, currentBlockSize) * 4;
        delayBufs.resize (numSpecOut);
        for (auto& buf : delayBufs)
            buf.assign (delayCapacity, 0.0f);
        delayWritePos   = 0;

        if (newOrder > 0)
        {
            float cutoff = apvts.getRawParameterValue ("lfeCutoff")->load();
            lfe.prepare (getSampleRate(), fftSize, cutoff);
            prevLfeCutoff = cutoff;
            setLatencySamples (fftSize);
        }

        chOutputs.resize (numSpecOut);
        for (auto& buf : chOutputs)
            buf.resize (currentBlockSize, 0.0f);
        chOutputPtrs.resize (numSpecOut);
        for (int ch = 0; ch < numSpecOut; ++ch)
            chOutputPtrs[ch] = chOutputs[ch].data();

        // Refresh channel offset param pointers on reconfiguration
        static constexpr const char* chOffIds[] = { "chOffC", "chOffFL", "chOffFR", "chOffWL", "chOffWR", "chOffSL", "chOffSR", "chOffRL", "chOffRR" };
        for (int i = 0; i < 9; ++i)
            chOffParams[i] = apvts.getRawParameterValue (chOffIds[i]);

        // Resize cascade buffers
        cascadeFftSize = fftSize;
        cascadeCenter.assign (fftSize, 0.0f);
        cascadeFrontL.assign (fftSize, 0.0f);
        cascadeFrontR.assign (fftSize, 0.0f);
        cascadeWideL.assign (fftSize, 0.0f);
        cascadeWideR.assign (fftSize, 0.0f);
        cascadeSideL.assign (fftSize, 0.0f);
        cascadeSideR.assign (fftSize, 0.0f);
        cascadeRearL.assign (fftSize, 0.0f);
        cascadeRearR.assign (fftSize, 0.0f);
        cascadeTemp.assign (fftSize, 0.0f);
        cascadeLresSave.assign (fftSize, 0.0f);
        cascadeRresSave.assign (fftSize, 0.0f);
    }

    runCalibration();
    updateHostDisplay();

    fadeSamplesLeft.store (fadeSamplesTotal);
    calState.store (CalState::FadingIn);
}

void SpatialExpanderAudioProcessor::doCascade (const float* fftL, const float* fftR,
                                                float* fftCenter, float* fftFrontL,
                                                float* fftFrontR, float* fftWideL,
                                                float* fftWideR, float* fftSideL,
                                                float* fftSideR, float* fftRearL,
                                                float* fftRearR, float* fftTemp,
                                                int fftSize, int numSpecOut)
{

    // Layer 1: Extract C, Lres, Rres from L, R
    analyser.onFrame (fftL, fftR, fftCenter, fftFrontL, fftFrontR, fftSize);

    if (numSpecOut <= 3)
        return;

    // Ensure save buffers are sized
    if (cascadeFftSize != fftSize)
    {
        cascadeLresSave.resize (fftSize);
        cascadeRresSave.resize (fftSize);
        cascadeFftSize = fftSize;
    }

    // Save original Lres/Rres before layer 2 overwrites them
    std::copy (fftFrontL, fftFrontL + fftSize, cascadeLresSave.data());
    std::copy (fftFrontR, fftFrontR + fftSize, cascadeRresSave.data());

    // Layer 2 left: Extract FrontL, RearL, tempC from Lres, C
    analyser.onFrame (cascadeLresSave.data(), fftCenter, fftFrontL, fftRearL, fftTemp, fftSize);

    // Layer 2 right: Extract FrontR, RearR, Center from Rres, tempC
    analyser.onFrame (cascadeRresSave.data(), fftTemp, fftFrontR, fftRearR, fftCenter, fftSize);

    bool rearIsolation = apvts.getRawParameterValue ("rearIsolation")->load() > 0.5f;
    if (!rearIsolation)
    {
        std::copy (cascadeLresSave.begin(), cascadeLresSave.end(), fftRearL);
        std::copy (cascadeRresSave.begin(), cascadeRresSave.end(), fftRearR);
    }

    if (numSpecOut <= 5)
        return;

    // Layer 3 left: Extract SideL, newFrontL, newRearL from (FrontL, RearL)
    std::copy (fftFrontL, fftFrontL + fftSize, cascadeLresSave.data());
    std::copy (fftRearL, fftRearL + fftSize, cascadeRresSave.data());
    analyser.onFrame (cascadeLresSave.data(), cascadeRresSave.data(),
                      fftSideL, fftFrontL, fftRearL, fftSize);

    // Layer 3 right: Extract SideR, newFrontR, newRearR from (FrontR, RearR)
    std::copy (fftFrontR, fftFrontR + fftSize, cascadeLresSave.data());
    std::copy (fftRearR, fftRearR + fftSize, cascadeRresSave.data());
    analyser.onFrame (cascadeLresSave.data(), cascadeRresSave.data(),
                      fftSideR, fftFrontR, fftRearR, fftSize);

    if (numSpecOut <= 7)
        return;

    // Layer 4 left: Extract WideL, newFrontL, newSideL from (FrontL, SideL)
    std::copy (fftFrontL, fftFrontL + fftSize, cascadeLresSave.data());
    std::copy (fftSideL, fftSideL + fftSize, cascadeRresSave.data());
    analyser.onFrame (cascadeLresSave.data(), cascadeRresSave.data(),
                      fftWideL, fftFrontL, fftSideL, fftSize);

    // Layer 4 right: Extract WideR, newFrontR, newSideR from (FrontR, SideR)
    std::copy (fftFrontR, fftFrontR + fftSize, cascadeLresSave.data());
    std::copy (fftSideR, fftSideR + fftSize, cascadeRresSave.data());
    analyser.onFrame (cascadeLresSave.data(), cascadeRresSave.data(),
                      fftWideR, fftFrontR, fftSideR, fftSize);
}

void SpatialExpanderAudioProcessor::applyStretch (float* fftCenter, float* fftFrontL,
                                                    float* fftFrontR, float* fftWideL,
                                                    float* fftWideR, float* fftSideL,
                                                    float* fftSideR, float* fftRearL,
                                                    float* fftRearR, int fftSize,
                                                    float stretch, int numSpecOut)
{
    if (numSpecOut <= 3 || stretch >= 1.0f)
        return;

    float s = stretch;
    float invS = 1.0f - s;

    if (numSpecOut <= 5)
    {
        // 5.1: RearL/RearR fold to FrontL/FrontR
        for (int i = 0; i < fftSize; ++i)
        {
            float rl = fftRearL[i];
            float rr = fftRearR[i];
            fftRearL[i] = rl * s;
            fftRearR[i] = rr * s;
            fftFrontL[i] += rl * invS;
            fftFrontR[i] += rr * invS;
        }
    }
    else if (numSpecOut <= 7)
    {
        // 7.1: SideL/SideR → Center/FrontL/FrontR, RearL/RearR → FrontL/FrontR
        for (int i = 0; i < fftSize; ++i)
        {
            float sl = fftSideL[i];
            float sr = fftSideR[i];
            float rl = fftRearL[i];
            float rr = fftRearR[i];

            fftSideL[i] = sl * s;
            fftSideR[i] = sr * s;
            fftRearL[i] = rl * s;
            fftRearR[i] = rr * s;

            fftCenter[i] += (sl + sr) * invS * 0.5f;
            fftFrontL[i] += sl * invS * 0.5f + rl * invS;
            fftFrontR[i] += sr * invS * 0.5f + rr * invS;
        }
    }
    else
    {
        // 9.1: WideL/WideR → Center/FrontL/FrontR, SideL/SideR → Center/FrontL/FrontR,
        //       RearL/RearR → FrontL/FrontR
        for (int i = 0; i < fftSize; ++i)
        {
            float wl = fftWideL[i];
            float wr = fftWideR[i];
            float sl = fftSideL[i];
            float sr = fftSideR[i];
            float rl = fftRearL[i];
            float rr = fftRearR[i];

            fftWideL[i] = wl * s;
            fftWideR[i] = wr * s;
            fftSideL[i] = sl * s;
            fftSideR[i] = sr * s;
            fftRearL[i] = rl * s;
            fftRearR[i] = rr * s;

            fftCenter[i] += (wl + wr + sl + sr) * invS * 0.5f;
            fftFrontL[i] += (wl + sl) * invS * 0.5f + rl * invS;
            fftFrontR[i] += (wr + sr) * invS * 0.5f + rr * invS;
        }
    }
}

void SpatialExpanderAudioProcessor::onFrame (const float* fftL, const float* fftR,
                                              float** fftOutputs, int numOutputs,
                                              int fftSize)
{
    int numSpecOut = numOutputs;

    // Ensure cascade buffers are sized
    if (cascadeFftSize != fftSize)
    {
        cascadeCenter.resize (fftSize);
        cascadeFrontL.resize (fftSize);
        cascadeFrontR.resize (fftSize);
        cascadeWideL.resize (fftSize);
        cascadeWideR.resize (fftSize);
        cascadeSideL.resize (fftSize);
        cascadeSideR.resize (fftSize);
        cascadeRearL.resize (fftSize);
        cascadeRearR.resize (fftSize);
        cascadeTemp.resize (fftSize);
        cascadeFftSize = fftSize;
    }

    // Full cascade using pre-allocated buffers
    doCascade (fftL, fftR, cascadeCenter.data(), cascadeFrontL.data(),
               cascadeFrontR.data(), cascadeWideL.data(), cascadeWideR.data(),
               cascadeSideL.data(), cascadeSideR.data(),
               cascadeRearL.data(), cascadeRearR.data(),
               cascadeTemp.data(), fftSize, numSpecOut);

    // Stretch
    float stretch = apvts.getRawParameterValue ("stretch")->load();
    applyStretch (cascadeCenter.data(), cascadeFrontL.data(), cascadeFrontR.data(),
                  cascadeWideL.data(), cascadeWideR.data(),
                  cascadeSideL.data(), cascadeSideR.data(),
                  cascadeRearL.data(), cascadeRearR.data(),
                  fftSize, stretch, numSpecOut);

    // ---- Phase coherence restoration (post-cascade, pre-gain) ----
    // L-derived channels inherit L's original phase; R-derived inherit R's.
    // Magnitudes are untouched, so spatial mapping stays exact.
    auto restorePhase = [&](float* outBuf, const float* phaseRef)
    {
        float magOut = std::abs (outBuf[0]);
        float magRef = std::abs (phaseRef[0]);
        if (magRef > 1e-18f)
        {
            float scale = magOut / magRef;
            outBuf[0] = phaseRef[0] * scale;
        }

        magOut = std::abs (outBuf[1]);
        magRef = std::abs (phaseRef[1]);
        if (magRef > 1e-18f)
        {
            float scale = magOut / magRef;
            outBuf[1] = phaseRef[1] * scale;
        }

        for (int k = 1; k < fftSize / 2; ++k)
        {
            size_t idx = static_cast<size_t> (k) * 2;
            float outRe = outBuf[idx], outIm = outBuf[idx + 1];
            float refRe = phaseRef[idx], refIm = phaseRef[idx + 1];

            float magOut = std::sqrt (outRe * outRe + outIm * outIm);
            float magRef = std::sqrt (refRe * refRe + refIm * refIm);

            if (magRef > 1e-18f)
            {
                float scale = magOut / magRef;
                outBuf[idx]     = refRe * scale;
                outBuf[idx + 1] = refIm * scale;
            }
        }
    };

    // L-derived channels
    if (numSpecOut > 3)
    {
        restorePhase (cascadeFrontL.data(), fftL);
        restorePhase (cascadeRearL.data(), fftL);
    }
    if (numSpecOut > 5)
        restorePhase (cascadeSideL.data(), fftL);
    if (numSpecOut > 7)
        restorePhase (cascadeWideL.data(), fftL);

    // R-derived channels
    if (numSpecOut > 3)
    {
        restorePhase (cascadeFrontR.data(), fftR);
        restorePhase (cascadeRearR.data(), fftR);
    }
    if (numSpecOut > 5)
        restorePhase (cascadeSideR.data(), fftR);
    if (numSpecOut > 7)
        restorePhase (cascadeWideR.data(), fftR);

    // Center is left as-is (already extracted with coherent L+R phase)

    // Calibration gain from ILD
    if (!gainTable.empty())
    {
        double sumL2 = 0.0, sumR2 = 0.0;

        sumL2 += static_cast<double> (fftL[0]) * static_cast<double> (fftL[0]);
        sumR2 += static_cast<double> (fftR[0]) * static_cast<double> (fftR[0]);

        sumL2 += static_cast<double> (fftL[1]) * static_cast<double> (fftL[1]);
        sumR2 += static_cast<double> (fftR[1]) * static_cast<double> (fftR[1]);

        for (int k = 1; k < fftSize / 2; ++k)
        {
            size_t idx = static_cast<size_t> (k) * 2;
            double lRe = fftL[idx], lIm = fftL[idx + 1];
            double rRe = fftR[idx], rIm = fftR[idx + 1];
            sumL2 += lRe * lRe + lIm * lIm;
            sumR2 += rRe * rRe + rIm * rIm;
        }

        float ildDb;
        if (sumL2 < 1e-18 && sumR2 < 1e-18)
            ildDb = 0.0f;
        else if (sumL2 < 1e-18)
            ildDb = -60.0f;
        else if (sumR2 < 1e-18)
            ildDb = 60.0f;
        else
            ildDb = 10.0f * std::log10 (static_cast<float> (sumL2 / sumR2));

        ildDb = std::max (-60.0f, std::min (60.0f, ildDb));
        int idx = static_cast<int> (std::round (ildDb)) + 60;
        idx = std::max (0, std::min (ildTableSize - 1, idx));

        float gain = gainTable[static_cast<size_t> (idx)];

        auto applyGain = [gain](float* buf, int size)
        {
            for (int i = 0; i < size; ++i)
                buf[i] *= gain;
        };

        applyGain (cascadeCenter.data(), fftSize);
        applyGain (cascadeFrontL.data(), fftSize);
        applyGain (cascadeFrontR.data(), fftSize);
        if (numSpecOut > 7)
        {
            applyGain (cascadeWideL.data(), fftSize);
            applyGain (cascadeWideR.data(), fftSize);
        }
        if (numSpecOut > 5)
        {
            applyGain (cascadeSideL.data(), fftSize);
            applyGain (cascadeSideR.data(), fftSize);
        }
        applyGain (cascadeRearL.data(), fftSize);
        applyGain (cascadeRearR.data(), fftSize);
    }

    // Channel Offsets (post-calibration per-channel gain)
    {
        float* bufMap[9] = {};
        if (numSpecOut <= 3)
        {
            bufMap[0] = cascadeCenter.data(); bufMap[1] = cascadeFrontL.data(); bufMap[2] = cascadeFrontR.data();
        }
        else if (numSpecOut <= 5)
        {
            bufMap[0] = cascadeCenter.data(); bufMap[1] = cascadeFrontL.data(); bufMap[2] = cascadeFrontR.data();
            bufMap[3] = cascadeRearL.data();  bufMap[4] = cascadeRearR.data();
        }
        else if (numSpecOut <= 7)
        {
            bufMap[0] = cascadeCenter.data(); bufMap[1] = cascadeFrontL.data(); bufMap[2] = cascadeFrontR.data();
            bufMap[3] = cascadeSideL.data();  bufMap[4] = cascadeSideR.data();
            bufMap[5] = cascadeRearL.data();  bufMap[6] = cascadeRearR.data();
        }
        else
        {
            bufMap[0] = cascadeCenter.data(); bufMap[1] = cascadeFrontL.data(); bufMap[2] = cascadeFrontR.data();
            bufMap[3] = cascadeWideL.data();  bufMap[4] = cascadeWideR.data();
            bufMap[5] = cascadeSideL.data();  bufMap[6] = cascadeSideR.data();
            bufMap[7] = cascadeRearL.data();  bufMap[8] = cascadeRearR.data();
        }
        for (int ch = 0; ch < numSpecOut; ++ch)
        {
            float offsetDb = chOffParams[ch]->load();
            if (std::abs (offsetDb) >= 0.01f)
            {
                float gain = juce::Decibels::decibelsToGain (offsetDb);
                juce::FloatVectorOperations::multiply (bufMap[ch], gain, fftSize);
            }
        }
    }

    // Write to output buffers
    // 3.0: outputs[0]=Center, [1]=Lres, [2]=Rres
    // 5.1: outputs[0]=Center, [1]=FrontL, [2]=FrontR, [3]=RearL, [4]=RearR
    // 7.1: outputs[0]=Center, [1]=FrontL, [2]=FrontR, [3]=SideL, [4]=SideR, [5]=RearL, [6]=RearR
    // 9.1: outputs[0]=Center, [1]=FrontL, [2]=FrontR, [3]=WideL, [4]=WideR,
    //       [5]=SideL, [6]=SideR, [7]=RearL, [8]=RearR
    if (numSpecOut <= 3)
    {
        std::copy (cascadeCenter.begin(), cascadeCenter.end(), fftOutputs[0]);
        std::copy (cascadeFrontL.begin(), cascadeFrontL.end(), fftOutputs[1]);
        std::copy (cascadeFrontR.begin(), cascadeFrontR.end(), fftOutputs[2]);
    }
    else if (numSpecOut <= 5)
    {
        std::copy (cascadeCenter.begin(), cascadeCenter.end(), fftOutputs[0]);
        std::copy (cascadeFrontL.begin(), cascadeFrontL.end(), fftOutputs[1]);
        std::copy (cascadeFrontR.begin(), cascadeFrontR.end(), fftOutputs[2]);
        std::copy (cascadeRearL.begin(), cascadeRearL.end(), fftOutputs[3]);
        std::copy (cascadeRearR.begin(), cascadeRearR.end(), fftOutputs[4]);
    }
    else if (numSpecOut <= 7)
    {
        std::copy (cascadeCenter.begin(), cascadeCenter.end(), fftOutputs[0]);
        std::copy (cascadeFrontL.begin(), cascadeFrontL.end(), fftOutputs[1]);
        std::copy (cascadeFrontR.begin(), cascadeFrontR.end(), fftOutputs[2]);
        std::copy (cascadeSideL.begin(), cascadeSideL.end(), fftOutputs[3]);
        std::copy (cascadeSideR.begin(), cascadeSideR.end(), fftOutputs[4]);
        std::copy (cascadeRearL.begin(), cascadeRearL.end(), fftOutputs[5]);
        std::copy (cascadeRearR.begin(), cascadeRearR.end(), fftOutputs[6]);
    }
    else
    {
        std::copy (cascadeCenter.begin(), cascadeCenter.end(), fftOutputs[0]);
        std::copy (cascadeFrontL.begin(), cascadeFrontL.end(), fftOutputs[1]);
        std::copy (cascadeFrontR.begin(), cascadeFrontR.end(), fftOutputs[2]);
        std::copy (cascadeWideL.begin(), cascadeWideL.end(), fftOutputs[3]);
        std::copy (cascadeWideR.begin(), cascadeWideR.end(), fftOutputs[4]);
        std::copy (cascadeSideL.begin(), cascadeSideL.end(), fftOutputs[5]);
        std::copy (cascadeSideR.begin(), cascadeSideR.end(), fftOutputs[6]);
        std::copy (cascadeRearL.begin(), cascadeRearL.end(), fftOutputs[7]);
        std::copy (cascadeRearR.begin(), cascadeRearR.end(), fftOutputs[8]);
    }
}

void SpatialExpanderAudioProcessor::runCalibration()
{
    int fftSize = stft.fftSize;
    if (fftSize < 2)
        return;

    int numBinsFull = fftSize;
    int numBinsHalf = fftSize / 2 + 1;
    int numSpecOut = getNumSpectralOutputs();

    // Build pink noise spectrum
    std::vector<float> pinkMag (static_cast<size_t> (numBinsHalf));
    double totalPinkPower = 0.0;
    for (int k = 0; k < numBinsHalf; ++k)
    {
        pinkMag[static_cast<size_t> (k)] = (k == 0) ? 1.0f
            : 1.0f / std::sqrt (static_cast<float> (k));
        totalPinkPower += static_cast<double> (pinkMag[static_cast<size_t> (k)])
                        * static_cast<double> (pinkMag[static_cast<size_t> (k)]);
    }
    double invNorm = 1.0 / std::sqrt (totalPinkPower);
    for (int k = 0; k < numBinsHalf; ++k)
        pinkMag[static_cast<size_t> (k)] *= static_cast<float> (invNorm);

    std::mt19937 rng (12345);
    std::uniform_real_distribution<float> phaseDist (0.0f,
        2.0f * juce::MathConstants<float>::pi);

    std::vector<float> noiseSpec (static_cast<size_t> (numBinsFull) * 2, 0.0f);
    for (int k = 0; k < numBinsHalf; ++k)
    {
        float phase = phaseDist (rng);
        float re = pinkMag[static_cast<size_t> (k)] * std::cos (phase);
        float im = pinkMag[static_cast<size_t> (k)] * std::sin (phase);
        if (k == 0)
            noiseSpec[0] = re;
        else if (k == fftSize / 2)
            noiseSpec[1] = re;
        else
        {
            size_t idx = static_cast<size_t> (k) * 2;
            noiseSpec[idx]     = re;
            noiseSpec[idx + 1] = im;
        }
    }

    // Buffers for panned input and cascade
    std::vector<float> fftL (static_cast<size_t> (numBinsFull) * 2, 0.0f);
    std::vector<float> fftR (static_cast<size_t> (numBinsFull) * 2, 0.0f);
    std::vector<float> fftCenter (static_cast<size_t> (numBinsFull) * 2, 0.0f);
    std::vector<float> fftFrontL (static_cast<size_t> (numBinsFull) * 2, 0.0f);
    std::vector<float> fftFrontR (static_cast<size_t> (numBinsFull) * 2, 0.0f);
    std::vector<float> fftWideL (static_cast<size_t> (numBinsFull) * 2, 0.0f);
    std::vector<float> fftWideR (static_cast<size_t> (numBinsFull) * 2, 0.0f);
    std::vector<float> fftSideL (static_cast<size_t> (numBinsFull) * 2, 0.0f);
    std::vector<float> fftSideR (static_cast<size_t> (numBinsFull) * 2, 0.0f);
    std::vector<float> fftRearL (static_cast<size_t> (numBinsFull) * 2, 0.0f);
    std::vector<float> fftRearR (static_cast<size_t> (numBinsFull) * 2, 0.0f);
    std::vector<float> fftTemp (static_cast<size_t> (numBinsFull) * 2, 0.0f);

    std::vector<float> newTable (static_cast<size_t> (ildTableSize));
    double refPower = 0.0;
    float stretch = apvts.getRawParameterValue ("stretch")->load();

    for (int iIdx = 0; iIdx < ildTableSize; ++iIdx)
    {
        float ildDb = static_cast<float> (iIdx - 60);

        float ratio = std::pow (10.0f, ildDb / 20.0f);
        float panL  = ratio / std::sqrt (1.0f + ratio * ratio);
        float panR  = 1.0f / std::sqrt (1.0f + ratio * ratio);

        for (int k = 0; k < numBinsFull; ++k)
        {
            fftL[static_cast<size_t> (k)] = noiseSpec[static_cast<size_t> (k)] * panL;
            fftR[static_cast<size_t> (k)] = noiseSpec[static_cast<size_t> (k)] * panR;
        }

        // Run cascade
        doCascade (fftL.data(), fftR.data(),
                   fftCenter.data(), fftFrontL.data(),
                   fftFrontR.data(), fftWideL.data(),
                   fftWideR.data(), fftSideL.data(),
                   fftSideR.data(), fftRearL.data(),
                   fftRearR.data(), fftTemp.data(), fftSize, numSpecOut);

        // Apply Stretch
        applyStretch (fftCenter.data(), fftFrontL.data(), fftFrontR.data(),
                      fftWideL.data(), fftWideR.data(),
                      fftSideL.data(), fftSideR.data(),
                      fftRearL.data(), fftRearR.data(),
                      fftSize, stretch, numSpecOut);

        // Measure total output power
        auto measurePower = [&](float* buf) -> double
        {
            double power = 0.0;
            power += static_cast<double> (buf[0]) * static_cast<double> (buf[0]);
            power += static_cast<double> (buf[1]) * static_cast<double> (buf[1]);
            for (int k = 1; k < fftSize / 2; ++k)
            {
                size_t idx = static_cast<size_t> (k) * 2;
                double re = buf[idx];
                double im = buf[idx + 1];
                power += re * re + im * im;
            }
            return power;
        };

        double totalPower = measurePower (fftCenter.data())
                          + measurePower (fftFrontL.data())
                          + measurePower (fftFrontR.data());

        if (numSpecOut > 7)
        {
            totalPower += measurePower (fftWideL.data())
                        + measurePower (fftWideR.data())
                        + measurePower (fftSideL.data())
                        + measurePower (fftSideR.data())
                        + measurePower (fftRearL.data())
                        + measurePower (fftRearR.data());
        }
        else if (numSpecOut > 5)
        {
            totalPower += measurePower (fftSideL.data())
                        + measurePower (fftSideR.data())
                        + measurePower (fftRearL.data())
                        + measurePower (fftRearR.data());
        }
        else if (numSpecOut > 3)
        {
            totalPower += measurePower (fftRearL.data())
                        + measurePower (fftRearR.data());
        }

        if (iIdx == 0)
            refPower = totalPower;

        newTable[static_cast<size_t> (iIdx)] = (totalPower > 1e-18)
            ? static_cast<float> (std::sqrt (refPower / totalPower)) : 1.0f;
    }

    // -----------------------------------------------------------------
    // Second pass: true peak measurement via full STFT pipeline.
    // Generate 0 dB true peak pink noise, run it through a temporary
    // STFT with the current cascade + gainTable, measure output peak.
    // -----------------------------------------------------------------

    // 1. Generate pink noise with 0 dB true peak
    const int calDuration = stft.fftSize * 8;
    std::vector<float> pinkL (calDuration), pinkR (calDuration);

    std::mt19937 rng2 (12345);
    std::uniform_real_distribution<float> white (-1.0f, 1.0f);
    float accL = 0.0f, accR = 0.0f;
    const float leak = 0.995f;
    for (int i = 0; i < calDuration; ++i)
    {
        accL = leak * accL + white (rng2);
        accR = leak * accR + white (rng2);
        pinkL[i] = accL;
        pinkR[i] = accR;
    }

    // Normalize to 0 dB true peak
    float inputPeak = 0.0f;
    for (int i = 0; i < calDuration; ++i)
    {
        inputPeak = std::max (inputPeak, std::abs (pinkL[i]));
        inputPeak = std::max (inputPeak, std::abs (pinkR[i]));
    }
    float norm = 1.0f / inputPeak;
    for (int i = 0; i < calDuration; ++i)
    {
        pinkL[i] *= norm;
        pinkR[i] *= norm;
    }

    // 2. Temporarily install the new table so onFrame uses it
    gainTable = newTable;

    // 3. Temporary STFT configured identically to the audio-thread one
    StereoSTFT calSTFT;
    calSTFT.setWindowSize (stft.fftOrder);
    calSTFT.setNumOutputs (numSpecOut);
    calSTFT.prepare (getSampleRate());
    calSTFT.setFrameListener (this);

    // 4. Process the noise through the temporary STFT
    std::vector<std::vector<float>> calOutputs (numSpecOut, std::vector<float> (calDuration, 0.0f));

    int calHop = stft.hopSize;
    for (int pos = 0; pos + calHop <= calDuration; pos += calHop)
    {
        std::vector<float*> outPtrs (numSpecOut);
        for (int ch = 0; ch < numSpecOut; ++ch)
            outPtrs[ch] = calOutputs[ch].data() + pos;

        calSTFT.process (pinkL.data() + pos, pinkR.data() + pos,
                         outPtrs.data(), calHop);
    }

    // 5. Measure output true peak (skip first fftSize samples = transient)
    float outputPeak = 0.0f;
    for (int ch = 0; ch < numSpecOut; ++ch)
    {
        for (int i = stft.fftSize; i < calDuration; ++i)
            outputPeak = std::max (outputPeak, std::abs (calOutputs[ch][i]));
    }

    // 6. Scale the entire table so the true peak sits at -0.1 dB
    const float targetPeak = juce::Decibels::decibelsToGain (-0.1f);
    float globalGain = (outputPeak > 1e-12f) ? (targetPeak / outputPeak) : targetPeak;
    for (auto& g : gainTable)
        g *= globalGain;
}

double SpatialExpanderAudioProcessor::getLatencyMs() const noexcept
{
    auto idx = static_cast<int> (apvts.getRawParameterValue ("latency")->load());
    int samples[] = { 512, 1024, 2048 };
    return static_cast<double> (samples[idx]) / getSampleRate() * 1000.0;
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

    auto inLayout = input->getCurrentLayout();
    if (inLayout.size() <= 2)
        return {};

    auto output = getBus (false, 0);
    if (output != nullptr && output->getCurrentLayout() == inLayout)
        return {};

    return "Input has " + juce::String (inLayout.size()) + " channels; only L/R are used.";
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
        case Fmt916: neededChannels = 10; break;  // 9.1 bed minimum (9.1.0 = 10 ch)
        default: return {};
    }

    if (busChannels < neededChannels)
        return "Bus has only " + juce::String (busChannels) + " ch; "
               + juce::String (neededChannels) + " ch needed for selected format. Some channels may be silent.";

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
    if (layout == juce::AudioChannelSet::create9point1point6()) return "9.1";
    if (layout == juce::AudioChannelSet::createLCR())           return "3.0";
    if (layout == juce::AudioChannelSet::stereo())              return "Stereo";

    int detected = detectFormatFromBus();
    switch (detected)
    {
        case Fmt30:  return "3.0";
        case Fmt51:  return "5.1";
        case Fmt71:  return "7.1";
        case Fmt916: return "9.1";
        default:     return juce::String (layout.size()) + " ch";
    }
}

void SpatialExpanderAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void SpatialExpanderAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpatialExpanderAudioProcessor();
}
