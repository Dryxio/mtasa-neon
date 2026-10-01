#pragma once

#include <algorithm>
#include <cmath>

namespace CameraAimMath
{
    constexpr float Pi = 3.14159265358979323846f;
    constexpr float DegreesPerRadian = 180.0f / Pi;
    // Reject corrupt script input before narrowing or reducing it modulo a turn.
    constexpr float MaxInputDegrees = 360000.0f;

    inline float WrapDegrees(float degrees)
    {
        const float wrapped = std::fmod(degrees, 360.0f);
        const float positive = wrapped < 0.0f ? wrapped + 360.0f : wrapped;
        return positive >= 360.0f ? 0.0f : positive;
    }

    inline bool Encode(float horizontal, float vertical, float minimum, float maximum, bool runabout, float& beta, float& alpha)
    {
        if (!std::isfinite(horizontal) || !std::isfinite(vertical) || std::abs(horizontal) > MaxInputDegrees || std::abs(vertical) > MaxInputDegrees ||
            !std::isfinite(minimum) || !std::isfinite(maximum) || minimum > maximum)
            return false;

        // The runabout processor builds Front with the opposite XY sign to
        // AimWeapon/M16_1stPerson. Expose one heading convention in every mode.
        beta = WrapDegrees(horizontal) / DegreesPerRadian + (runabout ? Pi / 2.0f : -Pi / 2.0f);
        beta = std::remainder(beta, 2.0f * Pi);
        alpha = std::clamp(vertical / DegreesPerRadian, minimum, maximum);
        return true;
    }

    inline bool Decode(float beta, float alpha, bool runabout, float& horizontal, float& vertical)
    {
        if (!std::isfinite(beta) || !std::isfinite(alpha))
            return false;
        horizontal = WrapDegrees((beta + (runabout ? -Pi / 2.0f : Pi / 2.0f)) * DegreesPerRadian);
        vertical = alpha * DegreesPerRadian;
        return std::isfinite(horizontal) && std::isfinite(vertical);
    }
}
