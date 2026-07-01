#include "LoudnessMeter.h"
#include <cmath>

void LoudnessMeter::prepare (double sr, int maxCh)
{
    sampleRate = sr;
    maxChannels = juce::jmax (maxCh, 16);

    stage1.assign (maxChannels, Biquad{});
    stage2.assign (maxChannels, Biquad{});
    channelWeights.assign (maxChannels, 0.0);

    chSqSum.resize (maxChannels);
    channelDebugInfo.resize (maxChannels);

    windowSize = static_cast<int> (std::ceil (sr * 0.4));
    powerWindow.assign (windowSize, 0.0);

    writePos = 0;
    samplesFilled = 0;
    runningSum = 0.0;
    momentaryLoudness.store (-70.0);

    designKFilter (sr);
}

void LoudnessMeter::reset()
{
    for (auto& b : stage1) b.reset();
    for (auto& b : stage2) b.reset();
    std::fill (powerWindow.begin(), powerWindow.end(), 0.0);
    writePos = 0;
    samplesFilled = 0;
    runningSum = 0.0;
    momentaryLoudness.store (-70.0);
}

void LoudnessMeter::designKFilter (double sr)
{
    auto bilinearBiquad = [] (double b2_a, double b1_a, double b0_a,
                              double a2_a, double a1_a, double a0_a,
                              double sr,
                              double& b0, double& b1, double& b2,
                              double& a1, double& a2)
    {
        double c  = 2.0 * sr;
        double c2 = c * c;

        double num0 = b2_a * c2 + b1_a * c + b0_a;
        double num1 = -2.0 * b2_a * c2 + 2.0 * b0_a;
        double num2 = b2_a * c2 - b1_a * c + b0_a;

        double den0 = a2_a * c2 + a1_a * c + a0_a;
        double den1 = -2.0 * a2_a * c2 + 2.0 * a0_a;
        double den2 = a2_a * c2 - a1_a * c + a0_a;

        b0 = num0 / den0;
        b1 = num1 / den0;
        b2 = num2 / den0;
        a1 = den1 / den0;
        a2 = den2 / den0;
    };

    double b0_1, b1_1, b2_1, a1_1, a2_1;
    double b0_2, b1_2, b2_2, a1_2, a2_2;

    if (sr == 48000.0)
    {
        b0_1 =  1.53512485958697; b1_1 = -2.69169618940638; b2_1 =  1.19839281085285;
        a1_1 = -1.69065929318241; a2_1 =  0.73248077421585;

        b0_2 =  1.0;              b1_2 = -2.0;              b2_2 =  1.0;
        a1_2 = -1.99004745483398; a2_2 =  0.99007225036621;
    }
    else if (sr == 44100.0)
    {
        b0_1 =  1.531434499312139; b1_1 = -2.652311075972248; b2_1 =  1.169767326192515;
        a1_1 = -1.663901800496320; a2_1 =  0.712774858093634;

        b0_2 =  0.999564193543748; b1_2 = -1.999128387087495; b2_2 =  0.999564193543748;
        a1_2 = -1.989169677772349; a2_2 =  0.989199039907131;
    }
    else if (sr == 96000.0)
    {
        b0_1 =  1.560203182014715; b1_1 = -2.927074199178258; b2_1 =  1.378174806459022;
        a1_1 = -1.844138864893892; a2_1 =  0.855438563724233;

        b0_2 =  1.002496924130377; b1_2 = -2.004993848260754; b2_2 =  1.002496924130377;
        a1_2 = -1.995017536974399; a2_2 =  0.995023751309940;
    }
    else
    {
        double z1r = -5958.525786, z1i = 5961.50810662;
        double p1r = -7502.42326333, p1i = 7503.87582837;
        double g1  =  1.5854384185284838;

        bilinearBiquad (g1, -g1 * (2.0 * z1r), g1 * (z1r * z1r + z1i * z1i),
                        1.0, -(2.0 * p1r), (p1r * p1r + p1i * p1i),
                        sr, b0_1, b1_1, b2_1, a1_1, a2_1);

        double p2r = -239.4561057, p2i = 8.66214753;
        double g2  =  1.0049990480789601;

        bilinearBiquad (g2, 0.0, 0.0,
                        1.0, -(2.0 * p2r), (p2r * p2r + p2i * p2i),
                        sr, b0_2, b1_2, b2_2, a1_2, a2_2);
    }

    for (auto& bq : stage1)
    {
        bq.b0 = b0_1; bq.b1 = b1_1; bq.b2 = b2_1; bq.a1 = a1_1; bq.a2 = a2_1;
    }
    for (auto& bq : stage2)
    {
        bq.b0 = b0_2; bq.b1 = b1_2; bq.b2 = b2_2; bq.a1 = a1_2; bq.a2 = a2_2;
    }
}

double LoudnessMeter::getChannelWeight (juce::AudioChannelSet::ChannelType type,
                                        int chIndex, int outputFormat,
                                        const juce::AudioChannelSet& layout)
{
    using CT = juce::AudioChannelSet::ChannelType;
    (void) outputFormat;

    if (type == CT::LFE) return 0.0;

    // Unambiguous front
    if (type == CT::left || type == CT::right || type == CT::centre)
        return 1.0;

    // Unambiguous side (90°)
    if (type == CT::leftSurroundSide || type == CT::rightSurroundSide)
        return 1.41;

    // Unambiguous rear (>120°)
    if (type == CT::leftSurroundRear || type == CT::rightSurroundRear)
        return 1.0;

    // Unambiguous wide
    if (type == CT::wideLeft || type == CT::wideRight)
        return 1.41;

    // Ambiguous: leftCentre / rightCentre
    // Fairlight (and some other DAWs) use Lc/Rc for the ±60° wide pair in 9.1+ beds.
    // In small layouts (≤ 5.1) Lc/Rc might genuinely be front-half (~15°), so keep 1.0.
    if (type == CT::leftCentre || type == CT::rightCentre)
    {
        // If the DAW has explicit wide channels, Lc/Rc are genuinely front-half (e.g. ~15°)
        bool hasExplicitWide = false;
        for (int ch = 0; ch < layout.size(); ++ch)
        {
            auto t = layout.getTypeOfChannel (ch);
            if (t == CT::wideLeft || t == CT::wideRight)
            {
                hasExplicitWide = true;
                break;
            }
        }
        if (hasExplicitWide)
            return 1.0;

        // Fairlight fingerprint: Lc/Rc appear AFTER Ls/Rs in a large bed
        int lcIndex = -1, lsIndex = -1;
        for (int ch = 0; ch < layout.size(); ++ch)
        {
            auto t = layout.getTypeOfChannel (ch);
            if (lcIndex < 0 && (t == CT::leftCentre || t == CT::rightCentre))
                lcIndex = ch;
            if (lsIndex < 0 && (t == CT::leftSurround || t == CT::leftSurroundSide))
                lsIndex = ch;
        }
        if (lcIndex > lsIndex && lsIndex >= 0 && layout.size() >= 10)
            return 1.41;   // Fairlight convention: Lc/Rc are the ±60° wide pair

        return 1.0;        // Default: front-half speaker
    }

    // Ambiguous: leftSurround / rightSurround
    // This type is a catch-all. Disambiguate by looking at what OTHER specific types
    // the DAW provided in the same layout:
    //   - If Lss/Rss exists but Lsr/Rsr does NOT → Ls is rear   (Fairlight) → 1.00
    //   - If Lsr/Rsr exists but Lss/Rss does NOT → Ls is side   (Standard)  → 1.41
    //   - If both exist                          → Ls is 5.1 surround      → 1.41
    //   - If neither exists                      → pure 5.1 layout         → 1.41
    if (type == CT::leftSurround || type == CT::rightSurround)
    {
        bool hasSide = false;
        bool hasRear = false;
        for (int ch = 0; ch < layout.size(); ++ch)
        {
            auto t = layout.getTypeOfChannel (ch);
            if (t == CT::leftSurroundSide || t == CT::rightSurroundSide) hasSide = true;
            if (t == CT::leftSurroundRear || t == CT::rightSurroundRear) hasRear = true;
        }

        if (hasSide && !hasRear)      return 1.0;   // Fairlight convention
        if (!hasSide && hasRear)      return 1.41;  // Standard JUCE convention
        return 1.41;                                   // 5.1 surround or side
    }

    // Height / top channels (|φ| ≥ 30°) → 1.00
    if (type == CT::topFrontLeft  || type == CT::topFrontRight ||
        type == CT::topFrontCentre|| type == CT::topRearLeft    ||
        type == CT::topRearRight  || type == CT::topSideLeft    ||
        type == CT::topSideRight)
        return 1.0;

    // Unknown / discrete layouts: index fallback (best guess)
    if (type == CT::unknown)
    {
        int n = layout.size();
        // Count bed channels by stripping trailing top channels
        int bedChannels = n;
        for (int ch = n - 1; ch >= 0; --ch)
        {
            auto t = layout.getTypeOfChannel (ch);
            if (t == CT::topFrontLeft || t == CT::topFrontRight || t == CT::topFrontCentre ||
                t == CT::topRearLeft  || t == CT::topRearRight  || t == CT::topSideLeft ||
                t == CT::topSideRight)
                --bedChannels;
            else
                break;
        }

        if (bedChannels <= 3)       // 3.0
        {
            if (chIndex <= 2) return 1.0;
        }
        else if (bedChannels <= 6)  // 5.1
        {
            if (chIndex <= 2) return 1.0;
            if (chIndex == 3) return 0.0;      // LFE
            if (chIndex >= 4) return 1.41;
        }
        else if (bedChannels <= 8)  // 7.1
        {
            if (chIndex <= 2) return 1.0;
            if (chIndex == 3) return 0.0;      // LFE
            if (chIndex == 4 || chIndex == 5) return 1.41;
            if (chIndex == 6 || chIndex == 7) return 1.0;
        }
        else                        // 9.1+
        {
            if (chIndex <= 2) return 1.0;
            if (chIndex == 3) return 0.0;      // LFE
            // Standard bed: 4,5=side, 6,7=rear, 8,9=wide
            if (chIndex == 4 || chIndex == 5) return 1.41;
            if (chIndex == 6 || chIndex == 7) return 1.0;
            if (chIndex == 8 || chIndex == 9) return 1.41;
        }
    }

    return 1.0;
}

void LoudnessMeter::process (const juce::AudioBuffer<float>& buffer,
                             const juce::AudioChannelSet& layout,
                             int outputFormat)
{
    int numChannels = buffer.getNumChannels();
    int numSamples  = buffer.getNumSamples();

    for (int ch = 0; ch < numChannels; ++ch)
        channelWeights[ch] = getChannelWeight (layout.getTypeOfChannel (ch), ch, outputFormat, layout);
    for (int ch = numChannels; ch < maxChannels; ++ch)
        channelWeights[ch] = 0.0;

    std::fill (chSqSum.begin(), chSqSum.begin() + numChannels, 0.0);
    double currentLoudness = -100.0;

    for (int i = 0; i < numSamples; ++i)
    {
        double weightedPower = 0.0;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            double w = channelWeights[ch];
            double s = static_cast<double> (buffer.getReadPointer (ch)[i]);
            double y1 = stage1[ch].process (s);
            double y2 = stage2[ch].process (y1);

            chSqSum[static_cast<size_t> (ch)] += y2 * y2;

            if (w <= 0.0) continue;
            weightedPower += w * y2 * y2;
        }

        if (samplesFilled < windowSize)
        {
            powerWindow[writePos] = weightedPower;
            runningSum += weightedPower;
            ++samplesFilled;
            writePos = (writePos + 1) % windowSize;
        }
        else
        {
            double oldest = powerWindow[writePos];
            runningSum += weightedPower - oldest;
            powerWindow[writePos] = weightedPower;
            writePos = (writePos + 1) % windowSize;
        }

        double meanPower = (samplesFilled > 0) ? (runningSum / samplesFilled) : 0.0;
        if (meanPower > 1e-18)
            currentLoudness = -0.691 + 10.0 * std::log10 (meanPower);
        else
            currentLoudness = -100.0;
    }

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& d = channelDebugInfo[static_cast<size_t> (ch)];
        d.type = layout.getTypeOfChannel (ch);
        d.weight = channelWeights[ch];
        double rms = std::sqrt (chSqSum[static_cast<size_t> (ch)] / numSamples);
        d.kWeightedDbfs = 20.0 * std::log10 (rms + 1e-18);
    }
    for (int ch = numChannels; ch < maxChannels; ++ch)
    {
        auto& d = channelDebugInfo[static_cast<size_t> (ch)];
        d.type = juce::AudioChannelSet::unknown;
        d.weight = 0.0;
        d.kWeightedDbfs = -100.0;
    }

    momentaryLoudness.store (currentLoudness);
}

double LoudnessMeter::getMomentaryLoudness() const noexcept
{
    return momentaryLoudness.load();
}
