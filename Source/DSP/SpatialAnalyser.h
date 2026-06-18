#pragma once
#include <JuceHeader.h>

class SpatialAnalyser
{
public:
    SpatialAnalyser();
    ~SpatialAnalyser() = default;

    void prepare (int fftSize);
    void reset();

    void onFrame (const float* fftL, const float* fftR,
                  float* fftC, float* fftLres, float* fftRres,
                  int fftSize);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpatialAnalyser)
};
