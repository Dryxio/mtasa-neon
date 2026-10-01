#include "CameraAimMath.h"
#include <cassert>
#include <iostream>
#include <limits>

namespace
{
    bool Near(float a, float b)
    {
        return std::abs(a - b) < 0.0002f;
    }
}

int main()
{
    using namespace CameraAimMath;
    // Check actual world-space directions, including the opposite native XY
    // convention in runabout cameras. Heading 0 is north, 90 is west.
    for (bool runabout : {false, true})
    {
        for (float heading : {0.0f, 45.0f, 90.0f, 180.0f, 270.0f, 359.0f})
        {
            float beta, alpha, outHeading, outPitch;
            assert(Encode(heading, 20, -1.2f, Pi / 3, runabout, beta, alpha));
            const float sign = runabout ? 1.0f : -1.0f;
            assert(Near(sign * std::cos(beta), -std::sin(heading / DegreesPerRadian)));
            assert(Near(sign * std::sin(beta), std::cos(heading / DegreesPerRadian)));
            assert(Near(std::sin(alpha), std::sin(20.0f / DegreesPerRadian)));
            assert(Decode(beta, alpha, runabout, outHeading, outPitch));
            assert(Near(std::remainder(outHeading - heading, 360.0f), 0));
            assert(Near(outPitch, 20));
        }
    }
    float beta = 123, alpha = 456;
    assert(Encode(361, 100, -1.2f, Pi / 3, false, beta, alpha));
    assert(Near(alpha, Pi / 3));
    float h, v;
    assert(Decode(beta, alpha, false, h, v) && Near(h, 1));
    assert(Encode(-1, -100, -1.2f, Pi / 3, false, beta, alpha));
    assert(Near(alpha, -1.2f));
    assert(Decode(beta, alpha, false, h, v) && Near(h, 359));

    // Invalid writes must leave both encoded outputs untouched.
    for (float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), 1e30f, -1e30f})
    {
        beta = 123;
        alpha = 456;
        assert(!Encode(bad, 1, -1, 1, false, beta, alpha));
        assert(beta == 123 && alpha == 456);
        assert(!Encode(1, bad, -1, 1, false, beta, alpha));
        assert(beta == 123 && alpha == 456);
    }
    assert(!Encode(0, 0, 1, -1, false, beta, alpha));

    // Repeated read/add/write must accumulate across the yaw seam and stop at
    // the native pitch limit instead of wrapping around the vertical axis.
    h = 359;
    v = 0;
    for (int shot = 0; shot < 100; ++shot)
    {
        assert(Encode(h + 2, v + 1, -89 / DegreesPerRadian, 45 / DegreesPerRadian, false, beta, alpha));
        assert(Decode(beta, alpha, false, h, v));
    }
    assert(Near(h, 199));
    assert(Near(v, 45));
    std::cout << "Camera aim math: world directions, round trips, limits, rejection and accumulation passed\n";
}
