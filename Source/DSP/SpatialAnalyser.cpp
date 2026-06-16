#include "SpatialAnalyser.h"
#include <cmath>

SpatialAnalyser::SpatialAnalyser() {}

void SpatialAnalyser::prepare (int /*fftSize*/)
{
}

void SpatialAnalyser::reset()
{
}

void SpatialAnalyser::onFrame (const float* fftL, const float* fftR,
                                float* fftC, float* fftLres, float* fftRres,
                                int fftSize)
{
    auto numBins = fftSize / 2 + 1;

    for (int k = 0; k < numBins; ++k)
    {
        std::complex<float> a, b;

        if (k == 0)
        {
            a = { fftL[0], 0.0f };
            b = { fftR[0], 0.0f };
        }
        else if (k == fftSize / 2)
        {
            a = { fftL[1], 0.0f };
            b = { fftR[1], 0.0f };
        }
        else
        {
            a = { fftL[2 * k], fftL[2 * k + 1] };
            b = { fftR[2 * k], fftR[2 * k + 1] };
        }

        float aMag = std::abs(a);
        float bMag = std::abs(b);
        float cMag = std::min(aMag, bMag);

        std::complex<float> c = 0.0f;
        if (cMag > 1e-18f)
        {
            std::complex<float> sum = a + b;
            float sumMag = std::abs(sum);
            if (sumMag > 1e-18f)
                c = cMag * (sum / sumMag);
        }

        std::complex<float> resA = a - c;
        std::complex<float> resB = b - c;

        if (k == 0)
        {
            fftC[0] = c.real();
            fftLres[0] = resA.real();
            fftRres[0] = resB.real();
        }
        else if (k == fftSize / 2)
        {
            fftC[1] = c.real();
            fftLres[1] = resA.real();
            fftRres[1] = resB.real();
        }
        else
        {
            fftC[2 * k] = c.real();
            fftC[2 * k + 1] = c.imag();
            fftLres[2 * k] = resA.real();
            fftLres[2 * k + 1] = resA.imag();
            fftRres[2 * k] = resB.real();
            fftRres[2 * k + 1] = resB.imag();
        }
    }
}
