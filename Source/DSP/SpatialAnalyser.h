#pragma once
#include <JuceHeader.h>
#include "StereoSTFT.h"

class SpatialAnalyser : public StereoSTFT::FrameListener
{
public:
    SpatialAnalyser();
    ~SpatialAnalyser() = default;

    void prepare (int fftSize);
    void reset();

    void onFrame (const float* fftL, const float* fftR,
                  float* fftC, float* fftLres, float* fftRres,
                  int fftSize) override;

private:

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpatialAnalyser)
};
