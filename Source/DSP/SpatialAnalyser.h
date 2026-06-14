#pragma once
#include <JuceHeader.h>

struct BinMetrics
{
    float magnitudeL;
    float magnitudeR;
    float ild;
    float correlation;
    float phaseDiff;
    float spectralFlux;
};

struct SpatialObject
{
    int startBin;
    int endBin;
    float meanILD;
    float meanCorr;
    float spectralCentroid;
    float totalEnergy;
    float meanPhaseDiff;
    float bandwidth;

    int id = -1;
    int ageFrames = 0;
    bool alive = true;
    int deathCountdown = -1;
    float smoothedGain = 0.0f;
    bool claimed = false;
};

class SpatialAnalyser
{
public:
    SpatialAnalyser() = default;

    void prepare (int fftSize, double sampleRate);
    void reset();
    void onFrame (float* fftBufL, float* fftBufR, int fftSize);

    const std::vector<BinMetrics>& getBinMetrics() const { return binMetrics; }
    const SpatialObject* getObjects() const { return objects; }
    int getNumObjects() const { return numObjects; }
    int getNextObjectId() const { return nextId; }

    static constexpr int maxObjects = 64;
    static constexpr float deathMs = 20.0f;

private:
    void computeBinMetrics (const float* fftBufL, const float* fftBufR);
    void finalizeObject (int start, int end);
    void groupBins();
    void harmonicMerge();
    void trackObjects();
    void updateLifecycle();

    int fftSize_ = 0;

    std::vector<BinMetrics> binMetrics;
    std::vector<float> prevMagnitudes;
    int numPositiveBins = 0;

    SpatialObject staging[maxObjects];
    int numStaging = 0;

    SpatialObject objects[maxObjects];
    int numObjects = 0;
    int nextId = 1;

    SpatialObject prevObjects[maxObjects];
    int numPrevObjects = 0;

    float sampleRate_ = 48000.0f;
    float hopMs_ = 5.333f;
    int deathFrames_ = 2;
};
