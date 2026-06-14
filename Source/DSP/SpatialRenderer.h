#pragma once
#include <JuceHeader.h>
#include "SpatialAnalyser.h"

class SpatialRenderer
{
public:
    SpatialRenderer();
    ~SpatialRenderer() = default;

    void prepare (double sampleRate);
    void reset();
    void process (const float* inL, const float* inR,
                  float* const* outGround, int numSamples);

    int getLatencySamples() const noexcept { return fftSize; }
    double getLatencyMs() const noexcept;

    SpatialAnalyser& getAnalyser() { return analyser_; }

    void setStretch (float s) { stretchParam_ = s; }
    void setSoloZone (float z) { soloZone_ = z; }
    void setRearBias (float b) { rearBias_ = b; }
    void setSmoothingMs (float ms);
    void setHeightThresholdHz (float hz);
    void setTransientBoost (float b) { transientBoost_ = b; }

    static constexpr int fftOrder = 9;
    static constexpr int fftSize  = 1 << fftOrder;
    static constexpr int hopSize  = fftSize / 2;
    static constexpr int numGroundChannels = 7;

    enum GroundChannel { Lb = 0, Ls = 1, L = 2, C = 3, R = 4, Rs = 5, Rb = 6 };

private:
    void processFrame();
    void applyWindow();
    void buildAzimuthLut();
    void updateSmoothingAlpha();
    void updateHeightBin();
    void computePanGains (float angleDeg, float* gains) const;

    juce::dsp::FFT fft;

    std::vector<float> window_;
    std::vector<float> inBufL_, inBufR_;
    std::vector<float> stereoFftBufL_, stereoFftBufR_;

    std::vector<float> channelFftBuf_[numGroundChannels];
    std::vector<float> olaBuf_[numGroundChannels];
    int olaWp_[numGroundChannels] = {};
    int olaSize_ = 0;

    int inWp_ = 0;
    int64_t totalIn_ = 0;
    int framesCompleted_ = 0;
    int outReady_ = 0;
    int outRp_ = 0;
    double sampleRate_ = 48000.0;

    SpatialAnalyser analyser_;

    static constexpr int lutSize_ = 512;
    std::vector<float> azimuthLut_;

    float stretchParam_ = 1.0f;
    float soloZone_ = 0.05f;
    float rearBias_ = 0.15f;
    float smoothingMs_ = 30.0f;
    float smoothingAlpha_ = 0.0f;
    float heightThresholdHz_ = 99999.0f;
    int heightThresholdBin_ = -1;
    float transientBoost_ = 0.4f;

    static constexpr int cacheSize_ = 1024;
    struct ObjectCache
    {
        int id = -1;
        float gains[numGroundChannels] = {};
    };
    ObjectCache objectCache_[cacheSize_];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpatialRenderer)
};
