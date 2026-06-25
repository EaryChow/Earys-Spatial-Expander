#include "PluginProcessor.h"
#include "PluginEditor.h"

static constexpr int chL = 0, chR = 1, chC = 2, chLFE = 3;

SpatialExpanderAudioProcessor::SpatialExpanderAudioProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::create5point1(), true)
                      .withOutput ("Front",  juce::AudioChannelSet::stereo(), false)
                      .withOutput ("Center", juce::AudioChannelSet::mono(), false)
                      .withOutput ("LFE",    juce::AudioChannelSet::mono(), false)
                      .withOutput ("Wide",   juce::AudioChannelSet::stereo(), false)
                      .withOutput ("Side",   juce::AudioChannelSet::stereo(), false)
                      .withOutput ("Rear",   juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "Parameters", {
          std::make_unique<juce::AudioParameterFloat> ("lfeCutoff", "LFE Cutoff",
              juce::NormalisableRange<float> (40.0f, 200.0f, 1.0f), 80.0f),
          std::make_unique<juce::AudioParameterFloat> ("lfeLevel", "LFE Level",
              juce::NormalisableRange<float> (-12.1f, 12.0f, 0.1f), 0.0f),
          std::make_unique<juce::AudioParameterChoice> ("outputFormat", "Output Format",
              juce::StringArray { "Auto", "3.0", "5.1", "7.1", "9.1" }, 0),
          std::make_unique<juce::AudioParameterChoice> ("latency", "Latency",
              juce::StringArray { "496", "992", "1984", "3968" }, 1),
          std::make_unique<juce::AudioParameterFloat> ("leakCenter", "Leak Center",
              juce::NormalisableRange<float> (0.0f, 1.5f, 0.01f), 0.0f),
          std::make_unique<juce::AudioParameterFloat> ("stretch", "Stretch",
              juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f),
          std::make_unique<juce::AudioParameterFloat> ("preamp", "Preamp",
              juce::NormalisableRange<float> (-6.0f, 6.0f, 0.1f), 0.0f),
          std::make_unique<juce::AudioParameterBool> ("rearIsolation", "Rear Isolation", false),
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

    if (n >= 16) return Fmt916;
    if (n >= 14) return Fmt916;
    if (n >= 12) return Fmt71;
    if (n >= 10) return Fmt71;
    if (n >= 8)  return Fmt51;
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

    int fftSize   = stft.fftSize;
    int hopSize   = stft.hopSize;

    int actualLatency = fftSize - hopSize;
    float cutoff = apvts.getRawParameterValue ("lfeCutoff")->load();
    lfe.prepare (sampleRate, actualLatency, cutoff);
    setLatencySamples (actualLatency);

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

    binCalGains.resize (cs / 2 + 1, 1.0f);
    binCalGainsSmoothed.resize (cs / 2 + 1, 1.0f);

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

    // --- Latency measurement: inject test signal into the live pipeline ---
    if (pendingLatencyMeasurement.load())
    {
        if (measPhase == 0)
        {
            measPhase = 1;
            int fftSize = stft.fftSize;
            measTotalLen = fftSize * 8;
            measImpulsePos = fftSize;
            measInput.assign (measTotalLen, 0.0f);
            measInput[measImpulsePos] = 1.0f;
            measNumSpecOut = static_cast<int> (chOutputs.size());
            measOutput.assign (measNumSpecOut, std::vector<float> (measTotalLen, 0.0f));

            measTotalPos = 0;
        }

        if (measPhase == 1)
        {
            int remain = measTotalLen - measTotalPos;
            int block = std::min (numSamples, remain);

            if (block > 0)
            {
                auto* bufL = buffer.getWritePointer (0);
                auto* bufR = (numInChannels > 1) ? buffer.getWritePointer (1) : buffer.getWritePointer (0);
                std::copy_n (measInput.data() + measTotalPos, block, bufL);
                for (int i = block; i < numSamples; ++i) bufL[i] = 0.0f;
                std::copy_n (measInput.data() + measTotalPos, block, bufR);
                if (numInChannels > 1)
                    for (int i = block; i < numSamples; ++i) bufR[i] = 0.0f;

                inL = buffer.getReadPointer (0);
                inR = (numInChannels > 1) ? buffer.getReadPointer (1) : buffer.getReadPointer (0);
            }
        }
    }

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

    // --- Latency measurement: capture spectral output + mute ---
    if (measPhase == 1)
    {
        int remain = measTotalLen - measTotalPos;
        int block = std::min (numSamples, remain);

        if (block > 0)
        {
            int safeOut = std::min (measNumSpecOut, static_cast<int> (chOutputs.size()));
            for (int ch = 0; ch < safeOut; ++ch)
                std::copy_n (chOutputs[ch].data(), block, measOutput[ch].data() + measTotalPos);

            measTotalPos += block;

            if (measTotalPos >= measTotalLen)
            {
                float peak = 0.0f;
                int peakPos = 0;
                for (int ch = 0; ch < measNumSpecOut; ++ch)
                    for (int i = 0; i < measTotalLen; ++i)
                    {
                        float absVal = std::abs (measOutput[ch][i]);
                        if (absVal > peak)
                        {
                            peak = absVal;
                            peakPos = i;
                        }
                    }

                measuredLatencySamples.store (peakPos - measImpulsePos);
                pendingLatencyMeasurement.store (false);
                measPhase = 0;
            }
        }

        for (int c = 0; c < numOutChannels; ++c)
            buffer.clear (c, 0, numSamples);
    }


    auto outputBus = getBus (false, 0);
    auto outputLayout = (outputBus != nullptr) ? outputBus->getCurrentLayout() : juce::AudioChannelSet();
    auto mainOutChannels = getMainBusNumOutputChannels();
    numOutChannels = buffer.getNumChannels();
    numSpecOut = static_cast<int> (chOutputs.size());

    bool is3_0 = (numSpecOut <= 3);
    bool is5_1 = (numSpecOut > 3 && numSpecOut <= 5);
    bool is7_1 = (numSpecOut > 5 && numSpecOut <= 7);
    bool is9_1 = (numSpecOut > 7);

    static constexpr float lfePrescale = 0.355f; // -9 dB default
    float lfeGain = (lfeLevelDb >= -12.0f) ? juce::Decibels::decibelsToGain (lfeLevelDb) * lfePrescale : 0.0f;

    std::vector<int> channelMap (mainOutChannels, -1);
    std::vector<bool> channelIsLFE (mainOutChannels, false);

    // Step 1: Front channels & LFE (type is reliable, index fallback for discrete)
    for (int ch = 0; ch < mainOutChannels; ++ch)
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
        for (int c = 0; c < mainOutChannels; ++c)
            if (channelMap[c] == output) return true;
        return false;
    };

    auto findChannelByType = [&](juce::AudioChannelSet::ChannelType type) -> int
    {
        for (int ch = 0; ch < mainOutChannels; ++ch)
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

    if (mainOutChannels == 6)
    {
        if (is5_1 && isUnmappedDiscrete(4)) channelMap[4] = 3;
        if (is5_1 && isUnmappedDiscrete(5)) channelMap[5] = 4;
    }
    else if (mainOutChannels == 8)
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
    else if (mainOutChannels >= 10)   // Any 9.1.x bus: 9.1.0 (10ch), 9.1.2 (12ch), 9.1.4 (14ch), 9.1.6 (16ch)
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
        for (int ch = 0; ch < mainOutChannels; ++ch)
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
    for (int ch = 0; ch < mainOutChannels; ++ch)
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

    // Auxiliary output buses (for node-based hosts like Cantabile)
    for (int busIdx = 1; busIdx < getBusCount(false); ++busIdx)
    {
        auto* bus = getBus(false, busIdx);
        if (bus == nullptr || !bus->isEnabled())
            continue;

        auto busBuffer = getBusBuffer(buffer, false, busIdx);
        int numBusCh = busBuffer.getNumChannels();
        if (numBusCh == 0)
            continue;

        switch (busIdx)
        {
            case AuxFront:
                if (numSpecOut >= 3)
                {
                    juce::FloatVectorOperations::copy(busBuffer.getWritePointer(0), chOutputs[1].data(), numSamples);
                    if (numBusCh > 1)
                        juce::FloatVectorOperations::copy(busBuffer.getWritePointer(1), chOutputs[2].data(), numSamples);
                }
                else
                    busBuffer.clear();
                break;

            case AuxCenter:
                if (numSpecOut >= 3)
                    juce::FloatVectorOperations::copy(busBuffer.getWritePointer(0), chOutputs[0].data(), numSamples);
                else
                    busBuffer.clear();
                break;

            case AuxLFE:
                if (numSpecOut > 3 && lfeGain > 0.0f)
                    juce::FloatVectorOperations::multiply(busBuffer.getWritePointer(0), chLFE.data(), lfeGain, numSamples);
                else
                    busBuffer.clear();
                break;

            case AuxWide:
                if (numSpecOut > 7)
                {
                    juce::FloatVectorOperations::copy(busBuffer.getWritePointer(0), chOutputs[3].data(), numSamples);
                    if (numBusCh > 1)
                        juce::FloatVectorOperations::copy(busBuffer.getWritePointer(1), chOutputs[4].data(), numSamples);
                }
                else
                    busBuffer.clear();
                break;

            case AuxSide:
                if (numSpecOut > 7)
                {
                    juce::FloatVectorOperations::copy(busBuffer.getWritePointer(0), chOutputs[5].data(), numSamples);
                    if (numBusCh > 1)
                        juce::FloatVectorOperations::copy(busBuffer.getWritePointer(1), chOutputs[6].data(), numSamples);
                }
                else if (numSpecOut > 5)
                {
                    juce::FloatVectorOperations::copy(busBuffer.getWritePointer(0), chOutputs[3].data(), numSamples);
                    if (numBusCh > 1)
                        juce::FloatVectorOperations::copy(busBuffer.getWritePointer(1), chOutputs[4].data(), numSamples);
                }
                else
                    busBuffer.clear();
                break;

            case AuxRear:
                if (numSpecOut > 7)
                {
                    juce::FloatVectorOperations::copy(busBuffer.getWritePointer(0), chOutputs[7].data(), numSamples);
                    if (numBusCh > 1)
                        juce::FloatVectorOperations::copy(busBuffer.getWritePointer(1), chOutputs[8].data(), numSamples);
                }
                else if (numSpecOut > 5)
                {
                    juce::FloatVectorOperations::copy(busBuffer.getWritePointer(0), chOutputs[5].data(), numSamples);
                    if (numBusCh > 1)
                        juce::FloatVectorOperations::copy(busBuffer.getWritePointer(1), chOutputs[6].data(), numSamples);
                }
                else if (numSpecOut > 3)
                {
                    juce::FloatVectorOperations::copy(busBuffer.getWritePointer(0), chOutputs[3].data(), numSamples);
                    if (numBusCh > 1)
                        juce::FloatVectorOperations::copy(busBuffer.getWritePointer(1), chOutputs[4].data(), numSamples);
                }
                else
                    busBuffer.clear();
                break;

            default:
                busBuffer.clear();
                break;
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

        if (newOrder > 0)
        {
            float cutoff = apvts.getRawParameterValue ("lfeCutoff")->load();
            lfe.prepare (getSampleRate(), fftSize - hopSize, cutoff);
            prevLfeCutoff = cutoff;
            setLatencySamples (fftSize - hopSize);
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

        binCalGains.resize (fftSize / 2 + 1, 1.0f);
        binCalGainsSmoothed.resize (fftSize / 2 + 1, 1.0f);
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
    {
        std::fill_n (fftRearL, fftSize, 0.0f);
        std::fill_n (fftRearR, fftSize, 0.0f);
        std::fill_n (fftSideL, fftSize, 0.0f);
        std::fill_n (fftSideR, fftSize, 0.0f);
        std::fill_n (fftWideL, fftSize, 0.0f);
        std::fill_n (fftWideR, fftSize, 0.0f);
        return;
    }

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
    {
        std::fill_n (fftSideL, fftSize, 0.0f);
        std::fill_n (fftSideR, fftSize, 0.0f);
        std::fill_n (fftWideL, fftSize, 0.0f);
        std::fill_n (fftWideR, fftSize, 0.0f);
        return;
    }

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
    {
        std::fill_n (fftWideL, fftSize, 0.0f);
        std::fill_n (fftWideR, fftSize, 0.0f);
        return;
    }

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

    // --- Per-bin static calibration gain ---
    // No temporal smoothing across frames; the STFT's 32x overlap provides ~10 ms
    // effective smoothing via the synthesis window.
    if (!gainTable.empty())
    {
        // Step 1: Compute frame peak energy for noise gate
        float framePeakEnergy = 0.0f;
        for (int k = 0; k <= fftSize / 2; ++k)
        {
            float l2, r2;
            if (k == 0)
            {
                l2 = static_cast<float> (fftL[0]) * static_cast<float> (fftL[0]);
                r2 = static_cast<float> (fftR[0]) * static_cast<float> (fftR[0]);
            }
            else if (k == fftSize / 2)
            {
                l2 = static_cast<float> (fftL[1]) * static_cast<float> (fftL[1]);
                r2 = static_cast<float> (fftR[1]) * static_cast<float> (fftR[1]);
            }
            else
            {
                size_t idx = static_cast<size_t> (k) * 2;
                float lRe = fftL[idx], lIm = fftL[idx + 1];
                float rRe = fftR[idx], rIm = fftR[idx + 1];
                l2 = lRe * lRe + lIm * lIm;
                r2 = rRe * rRe + rIm * rIm;
            }
            framePeakEnergy = std::max (framePeakEnergy, l2 + r2);
        }

        // Step 2: Compute per-bin gain
        for (int k = 0; k <= fftSize / 2; ++k)
        {
            float l2, r2;
            if (k == 0)
            {
                l2 = static_cast<float> (fftL[0]) * static_cast<float> (fftL[0]);
                r2 = static_cast<float> (fftR[0]) * static_cast<float> (fftR[0]);
            }
            else if (k == fftSize / 2)
            {
                l2 = static_cast<float> (fftL[1]) * static_cast<float> (fftL[1]);
                r2 = static_cast<float> (fftR[1]) * static_cast<float> (fftR[1]);
            }
            else
            {
                size_t idx = static_cast<size_t> (k) * 2;
                float lRe = fftL[idx], lIm = fftL[idx + 1];
                float rRe = fftR[idx], rIm = fftR[idx + 1];
                l2 = lRe * lRe + lIm * lIm;
                r2 = rRe * rRe + rIm * rIm;
            }

            float energy = l2 + r2;
            float snr = energy / (framePeakEnergy + 1e-18f);
            float confidence = std::min (1.0f, snr * 1e6f);

            float rawGain = 1.0f;
            if (energy > 1e-18f)
            {
                float ildDb;
                if (l2 < 1e-18f && r2 < 1e-18f)
                    ildDb = 0.0f;
                else if (l2 < 1e-18f)
                    ildDb = -60.0f;
                else if (r2 < 1e-18f)
                    ildDb = 60.0f;
                else
                    ildDb = 10.0f * std::log10 (l2 / r2);

                ildDb = std::max (-60.0f, std::min (60.0f, ildDb));
                int idx = static_cast<int> (std::round (ildDb)) + 60;
                idx = std::max (0, std::min (ildTableSize - 1, idx));
                rawGain = gainTable[static_cast<size_t> (idx)];
            }

            binCalGains[static_cast<size_t> (k)] = 1.0f + (rawGain - 1.0f) * confidence;
        }

        // Step 3: Optional 3-bin Gaussian spectral smoothing
        if (fftSize >= 4)
        {
            binCalGainsSmoothed[0] = binCalGains[0];
            binCalGainsSmoothed[static_cast<size_t> (fftSize / 2)] = binCalGains[static_cast<size_t> (fftSize / 2)];
            for (int k = 1; k < fftSize / 2; ++k)
            {
                size_t uk = static_cast<size_t> (k);
                binCalGainsSmoothed[uk] = 0.25f * binCalGains[uk - 1] + 0.5f * binCalGains[uk] + 0.25f * binCalGains[uk + 1];
            }
            binCalGains.swap (binCalGainsSmoothed);
        }

        // Step 4: Apply per-bin gain to all channels
        auto applyBinGain = [&](float* buf, int size)
        {
            buf[0] *= binCalGains[0];
            if (size > 2)
                buf[1] *= binCalGains[static_cast<size_t> (size / 2)];
            for (int k = 1; k < size / 2; ++k)
            {
                size_t idx = static_cast<size_t> (k) * 2;
                float g = binCalGains[static_cast<size_t> (k)];
                buf[idx] *= g;
                buf[idx + 1] *= g;
            }
        };

        applyBinGain (cascadeCenter.data(), fftSize);
        applyBinGain (cascadeFrontL.data(), fftSize);
        applyBinGain (cascadeFrontR.data(), fftSize);
        if (numSpecOut > 7)
        {
            applyBinGain (cascadeWideL.data(), fftSize);
            applyBinGain (cascadeWideR.data(), fftSize);
        }
        if (numSpecOut > 5)
        {
            applyBinGain (cascadeSideL.data(), fftSize);
            applyBinGain (cascadeSideR.data(), fftSize);
        }
        applyBinGain (cascadeRearL.data(), fftSize);
        applyBinGain (cascadeRearR.data(), fftSize);
    }

    // Channel Offsets (post-calibration per-channel gain)
    // chOffParams index order: C=0, FL=1, FR=2, WL=3, WR=4, SL=5, SR=6, RL=7, RR=8
    // Spectral output order differs per format, so we need a remapping.
    {
        float* bufMap[9] = {};
        int chOffIdx[9] = {};
        if (numSpecOut <= 3)
        {
            bufMap[0] = cascadeCenter.data(); bufMap[1] = cascadeFrontL.data(); bufMap[2] = cascadeFrontR.data();
            chOffIdx[0] = 0; chOffIdx[1] = 1; chOffIdx[2] = 2;
        }
        else if (numSpecOut <= 5)
        {
            bufMap[0] = cascadeCenter.data(); bufMap[1] = cascadeFrontL.data(); bufMap[2] = cascadeFrontR.data();
            bufMap[3] = cascadeRearL.data();  bufMap[4] = cascadeRearR.data();
            chOffIdx[0] = 0; chOffIdx[1] = 1; chOffIdx[2] = 2;
            chOffIdx[3] = 7; chOffIdx[4] = 8;
        }
        else if (numSpecOut <= 7)
        {
            bufMap[0] = cascadeCenter.data(); bufMap[1] = cascadeFrontL.data(); bufMap[2] = cascadeFrontR.data();
            bufMap[3] = cascadeSideL.data();  bufMap[4] = cascadeSideR.data();
            bufMap[5] = cascadeRearL.data();  bufMap[6] = cascadeRearR.data();
            chOffIdx[0] = 0; chOffIdx[1] = 1; chOffIdx[2] = 2;
            chOffIdx[3] = 5; chOffIdx[4] = 6;
            chOffIdx[5] = 7; chOffIdx[6] = 8;
        }
        else
        {
            bufMap[0] = cascadeCenter.data(); bufMap[1] = cascadeFrontL.data(); bufMap[2] = cascadeFrontR.data();
            bufMap[3] = cascadeWideL.data();  bufMap[4] = cascadeWideR.data();
            bufMap[5] = cascadeSideL.data();  bufMap[6] = cascadeSideR.data();
            bufMap[7] = cascadeRearL.data();  bufMap[8] = cascadeRearR.data();
            chOffIdx[0] = 0; chOffIdx[1] = 1; chOffIdx[2] = 2;
            chOffIdx[3] = 3; chOffIdx[4] = 4;
            chOffIdx[5] = 5; chOffIdx[6] = 6;
            chOffIdx[7] = 7; chOffIdx[8] = 8;
        }
        for (int ch = 0; ch < numSpecOut; ++ch)
        {
            float offsetDb = chOffParams[chOffIdx[ch]]->load();
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
    // Generate 0 dB true peak white noise, run it through a temporary
    // STFT with the current cascade + gainTable, measure output peak.
    //
    // White noise (flat spectrum) ensures all bins have roughly equal
    // energy and all receive full confidence in the per-bin noise gate.
    // Centered mono (noiseR = noiseL) means every bin has ILD = 0 dB,
    // so all bins look up the same gain (gainTable[60]).
    // -----------------------------------------------------------------

    // 1. Generate white noise with 0 dB true peak (centered mono)
    const int calDuration = 65536;
    std::vector<float> noiseL (calDuration), noiseR (calDuration);

    {
        auto genWhite = [&](std::vector<float>& buf, int seed)
        {
            juce::dsp::FFT whiteFFT (16);
            std::vector<float> spec (static_cast<size_t> (calDuration) * 2, 0.0f);
            std::mt19937 rng (seed);
            std::uniform_real_distribution<float> pd (0.0f, 2.0f * juce::MathConstants<float>::pi);

            for (int k = 0; k <= calDuration / 2; ++k)
            {
                float mag = 1.0f;
                float phase = pd (rng);
                float re = mag * std::cos (phase);
                float im = mag * std::sin (phase);

                if (k == 0)
                    spec[0] = re;
                else if (k == calDuration / 2)
                    spec[1] = re;
                else
                {
                    size_t idx = static_cast<size_t> (k) * 2;
                    spec[idx]     = re;
                    spec[idx + 1] = im;
                }
            }

            whiteFFT.performRealOnlyInverseTransform (spec.data());

            float peak = 0.0f;
            for (int i = 0; i < calDuration; ++i)
                peak = std::max (peak, std::abs (spec[i]));
            float norm = 1.0f / peak;
            for (int i = 0; i < calDuration; ++i)
                buf[i] = spec[i] * norm;
        };

        genWhite (noiseL, 42);
        noiseR = noiseL;  // Centered mono: ILD = 0 dB at every bin
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

        calSTFT.process (noiseL.data() + pos, noiseR.data() + pos,
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

int SpatialExpanderAudioProcessor::getLatencySamplesForMode (int modeIndex) noexcept
{
    int fftSize = 1 << (9 + modeIndex);
    return fftSize - fftSize / 32;
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
