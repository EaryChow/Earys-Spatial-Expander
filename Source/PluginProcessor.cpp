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
              juce::StringArray { "512", "1024", "2048" }, 1),
          std::make_unique<juce::AudioParameterFloat> ("leakCenter", "Leak Center",
              juce::NormalisableRange<float> (0.0f, 1.5f, 0.01f), 0.0f),
          std::make_unique<juce::AudioParameterFloat> ("stretch", "Stretch",
              juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f),
          std::make_unique<juce::AudioParameterBool> ("rearIsolation", "5.1 Rear Channel Isolation", true)
      })
{
    stft.setFrameListener (this);
    apvts.addParameterListener ("latency", this);

    gainTable.resize (ildTableSize, 1.0f);
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

int SpatialExpanderAudioProcessor::getNumSpectralOutputs() const noexcept
{
    int fmt = static_cast<int> (apvts.getRawParameterValue ("outputFormat")->load());

    auto output = getBus (false, 0);
    int busCh = (output != nullptr) ? output->getCurrentLayout().size() : 0;

    int effective = fmt;
    if (fmt == Auto)
    {
        if (busCh <= 2)       effective = Fmt30;
        else if (busCh <= 4)  effective = Fmt30;
        else if (busCh <= 6)  effective = Fmt51;
        else if (busCh <= 8)  effective = Fmt71;
        else                  effective = Fmt916;
    }

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

    int firstOutput = currentBlockSize * ((fftSize + hopSize - 1) / currentBlockSize);
    int actualDelay = firstOutput - hopSize;
    delaySize       = fftSize - actualDelay;
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

    // Pre-allocate cascade scratch buffers for audio-thread safety
    cascadeFftSize = stft.fftSize;
    int cs = cascadeFftSize;
    cascadeCenter.assign (cs, 0.0f);
    cascadeFrontL.assign (cs, 0.0f);
    cascadeFrontR.assign (cs, 0.0f);
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

    // Determine surround channel indices from bus layout
    auto outputBus = getBus (false, 0);
    auto outputLayout = (outputBus != nullptr) ? outputBus->getCurrentLayout() : juce::AudioChannelSet();
    int surroundLIdx = -1, surroundRIdx = -1;

    if (numSpecOut > 3)
    {
        // Prefer Rear Surround (Lrs/Rrs) when available; fall back to Side Surround (Ls/Rs)
        int idx;
        idx = outputLayout.getChannelIndexForType (juce::AudioChannelSet::leftSurroundRear);
        if (idx < 0) idx = outputLayout.getChannelIndexForType (juce::AudioChannelSet::leftSurround);
        surroundLIdx = idx;

        idx = outputLayout.getChannelIndexForType (juce::AudioChannelSet::rightSurroundRear);
        if (idx < 0) idx = outputLayout.getChannelIndexForType (juce::AudioChannelSet::rightSurround);
        surroundRIdx = idx;

        // Fallback for discrete/unknown layouts: assume ch4/5 are surround
        if (surroundLIdx < 0 && numOutChannels > 4) surroundLIdx = 4;
        if (surroundRIdx < 0 && numOutChannels > 5) surroundRIdx = 5;
    }

    // Route spectral outputs to audio buffer channels
    // 3.0 mode: chOutputs = [Center, Lres, Rres] -> L, R, C
    // 5.1 mode: chOutputs = [Center, FrontL, FrontR, RearL, RearR] -> L, R, C, LFE, surroundL, surroundR
    int maxUsedCh = 3;

    if (numOutChannels > 0)
        juce::FloatVectorOperations::copy (buffer.getWritePointer (0), chOutputs[1].data(), numSamples);
    if (numOutChannels > 1)
        juce::FloatVectorOperations::copy (buffer.getWritePointer (1), chOutputs[2].data(), numSamples);
    if (numOutChannels > 2)
    {
        juce::FloatVectorOperations::copy (buffer.getWritePointer (2), chOutputs[0].data(), numSamples);
        maxUsedCh = 3;
    }

    // LFE channel (always ch3 for standard layouts)
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
        maxUsedCh = juce::jmax (maxUsedCh, 4);
    }

    // 5.1 RearL/RearR -> correct surround channels based on bus layout
    if (numSpecOut > 3)
    {
        if (surroundLIdx >= 0 && surroundLIdx < numOutChannels)
        {
            juce::FloatVectorOperations::copy (buffer.getWritePointer (surroundLIdx), chOutputs[3].data(), numSamples);
            maxUsedCh = juce::jmax (maxUsedCh, surroundLIdx + 1);
        }
        if (surroundRIdx >= 0 && surroundRIdx < numOutChannels)
        {
            juce::FloatVectorOperations::copy (buffer.getWritePointer (surroundRIdx), chOutputs[4].data(), numSamples);
            maxUsedCh = juce::jmax (maxUsedCh, surroundRIdx + 1);
        }
    }

    // Zero remaining channels
    for (int c = maxUsedCh; c < numOutChannels; ++c)
    {
        if (c == 3) continue;
        buffer.clear (c, 0, numSamples);
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

void SpatialExpanderAudioProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    if (parameterID == "latency")
    {
        if (calState.load() != CalState::Normal)
            return;

        if (fadeSamplesTotal <= 0)
        {
            pendingWindowOrder.store (9 + static_cast<int> (newValue));
            triggerAsyncUpdate();
            return;
        }

        pendingWindowOrder.store (9 + static_cast<int> (newValue));
        fadeSamplesLeft.store (fadeSamplesTotal);
        calState.store (CalState::FadingOut);
    }
}

void SpatialExpanderAudioProcessor::handleAsyncUpdate()
{
    if (calState.load() == CalState::Reconfiguring)
    {
        int newOrder = pendingWindowOrder.exchange (0);
        if (newOrder > 0)
        {
            stft.setWindowSize (newOrder);
            analyser.prepare (stft.fftSize);

            int numSpecOut = getNumSpectralOutputs();
            stft.setNumOutputs (numSpecOut);

            int currentBlockSize = lastBlockSize;
            if (currentBlockSize < 1) currentBlockSize = 512;
            int fftSize = stft.fftSize;
            int hopSize = stft.hopSize;

            int firstOutput = currentBlockSize * ((fftSize + hopSize - 1) / currentBlockSize);
            int actualDelay = firstOutput - hopSize;
            delaySize       = fftSize - actualDelay;
            if (delaySize < 0) delaySize = 0;
            delayCapacity   = std::max (delaySize, currentBlockSize) * 4;
            delayBufs.resize (numSpecOut);
            for (auto& buf : delayBufs)
                buf.assign (delayCapacity, 0.0f);
            delayWritePos   = 0;

            float cutoff = apvts.getRawParameterValue ("lfeCutoff")->load();
            lfe.prepare (getSampleRate(), fftSize, cutoff);
            prevLfeCutoff = cutoff;

            chOutputs.resize (numSpecOut);
            for (auto& buf : chOutputs)
                buf.resize (currentBlockSize, 0.0f);
            chOutputPtrs.resize (numSpecOut);
            for (int ch = 0; ch < numSpecOut; ++ch)
                chOutputPtrs[ch] = chOutputs[ch].data();

            // Resize cascade buffers
            cascadeFftSize = fftSize;
            cascadeCenter.assign (fftSize, 0.0f);
            cascadeFrontL.assign (fftSize, 0.0f);
            cascadeFrontR.assign (fftSize, 0.0f);
            cascadeRearL.assign (fftSize, 0.0f);
            cascadeRearR.assign (fftSize, 0.0f);
            cascadeTemp.assign (fftSize, 0.0f);
            cascadeLresSave.assign (fftSize, 0.0f);
            cascadeRresSave.assign (fftSize, 0.0f);
        }

        runCalibration();

        setLatencySamples (stft.fftSize);
        updateHostDisplay();
        fadeSamplesLeft.store (fadeSamplesTotal);
        calState.store (CalState::FadingIn);
        return;
    }

    int newOrder = pendingWindowOrder.exchange (0);
    if (newOrder == 0)
        return;

    stft.setWindowSize (newOrder);
    analyser.prepare (stft.fftSize);

    int numSpecOut = getNumSpectralOutputs();
    stft.setNumOutputs (numSpecOut);

    int currentBlockSize = lastBlockSize;
    if (currentBlockSize < 1) currentBlockSize = 512;
    int fftSize = stft.fftSize;
    int hopSize = stft.hopSize;

    int firstOutput = currentBlockSize * ((fftSize + hopSize - 1) / currentBlockSize);
    int actualDelay = firstOutput - hopSize;
    delaySize       = fftSize - actualDelay;
    if (delaySize < 0) delaySize = 0;
    delayCapacity   = std::max (delaySize, currentBlockSize) * 4;
    delayBufs.resize (numSpecOut);
    for (auto& buf : delayBufs)
        buf.assign (delayCapacity, 0.0f);
    delayWritePos   = 0;

    int reportedLatency = fftSize;
    float cutoff = apvts.getRawParameterValue ("lfeCutoff")->load();
    lfe.prepare (getSampleRate(), reportedLatency, cutoff);
    setLatencySamples (reportedLatency);
    updateHostDisplay();

    // Resize cascade buffers
    cascadeFftSize = fftSize;
    cascadeCenter.assign (fftSize, 0.0f);
    cascadeFrontL.assign (fftSize, 0.0f);
    cascadeFrontR.assign (fftSize, 0.0f);
    cascadeRearL.assign (fftSize, 0.0f);
    cascadeRearR.assign (fftSize, 0.0f);
    cascadeTemp.assign (fftSize, 0.0f);
    cascadeLresSave.assign (fftSize, 0.0f);
    cascadeRresSave.assign (fftSize, 0.0f);
}

void SpatialExpanderAudioProcessor::doCascade (const float* fftL, const float* fftR,
                                                float* fftCenter, float* fftFrontL,
                                                float* fftFrontR, float* fftRearL,
                                                float* fftRearR, float* fftTemp,
                                                int fftSize)
{
    int numSpecOut = getNumSpectralOutputs();

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
}

void SpatialExpanderAudioProcessor::applyStretch (float* /*fftCenter*/, float* fftFrontL,
                                                   float* fftFrontR, float* fftRearL,
                                                   float* fftRearR, int fftSize,
                                                   float stretch)
{
    int numSpecOut = getNumSpectralOutputs();
    if (numSpecOut <= 3 || stretch >= 1.0f)
        return;

    float s = stretch;
    float invS = 1.0f - s;

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

void SpatialExpanderAudioProcessor::onFrame (const float* fftL, const float* fftR,
                                              float** fftOutputs, int /*numOutputs*/,
                                              int fftSize)
{
    int numSpecOut = getNumSpectralOutputs();

    // Ensure cascade buffers are sized
    if (cascadeFftSize != fftSize)
    {
        cascadeCenter.resize (fftSize);
        cascadeFrontL.resize (fftSize);
        cascadeFrontR.resize (fftSize);
        cascadeRearL.resize (fftSize);
        cascadeRearR.resize (fftSize);
        cascadeTemp.resize (fftSize);
        cascadeFftSize = fftSize;
    }

    // Full cascade using pre-allocated buffers
    doCascade (fftL, fftR, cascadeCenter.data(), cascadeFrontL.data(),
               cascadeFrontR.data(), cascadeRearL.data(), cascadeRearR.data(),
               cascadeTemp.data(), fftSize);

    // Stretch
    float stretch = apvts.getRawParameterValue ("stretch")->load();
    applyStretch (cascadeCenter.data(), cascadeFrontL.data(), cascadeFrontR.data(),
                  cascadeRearL.data(), cascadeRearR.data(), fftSize, stretch);

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
        applyGain (cascadeRearL.data(), fftSize);
        applyGain (cascadeRearR.data(), fftSize);
    }

    // Write to output buffers
    // 3.0: outputs[0]=Center, [1]=Lres, [2]=Rres
    // 5.1: outputs[0]=Center, [1]=FrontL, [2]=FrontR, [3]=RearL, [4]=RearR
    if (numSpecOut <= 3)
    {
        std::copy (cascadeCenter.begin(), cascadeCenter.end(), fftOutputs[0]);
        std::copy (cascadeFrontL.begin(), cascadeFrontL.end(), fftOutputs[1]);
        std::copy (cascadeFrontR.begin(), cascadeFrontR.end(), fftOutputs[2]);
    }
    else
    {
        std::copy (cascadeCenter.begin(), cascadeCenter.end(), fftOutputs[0]);
        std::copy (cascadeFrontL.begin(), cascadeFrontL.end(), fftOutputs[1]);
        std::copy (cascadeFrontR.begin(), cascadeFrontR.end(), fftOutputs[2]);
        std::copy (cascadeRearL.begin(), cascadeRearL.end(), fftOutputs[3]);
        std::copy (cascadeRearR.begin(), cascadeRearR.end(), fftOutputs[4]);
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
                   fftFrontR.data(), fftRearL.data(),
                   fftRearR.data(), fftTemp.data(), fftSize);

        // Apply Stretch
        applyStretch (fftCenter.data(), fftFrontL.data(), fftFrontR.data(),
                      fftRearL.data(), fftRearR.data(), fftSize, stretch);

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

        if (numSpecOut > 3)
        {
            totalPower += measurePower (fftRearL.data())
                        + measurePower (fftRearR.data());
        }

        if (iIdx == 0)
            refPower = totalPower;

        newTable[static_cast<size_t> (iIdx)] = (totalPower > 1e-18)
            ? static_cast<float> (std::sqrt (refPower / totalPower)) : 1.0f;
    }

    gainTable = std::move (newTable);
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
