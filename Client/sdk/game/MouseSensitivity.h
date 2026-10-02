#pragma once
#include <algorithm>
#include <cmath>
#include <cstdlib>

// UI percentages are a presentation of raw GTA coefficients. Keep the old
// maxima and raw saved values when extending the range below the old minimum.
namespace MouseSensitivity
{
    constexpr float Minimum = 0.000001f;
    constexpr float HorizontalMaximum = 0.004688f;
    constexpr float VerticalMaximum = 0.002688f;
    constexpr float HorizontalDefault = (0.0025f - Minimum) / (HorizontalMaximum - Minimum);
    constexpr float VerticalDefault = (0.0015f - Minimum) / (VerticalMaximum - Minimum);
    constexpr float NativeAimCoefficient = 0.0125f;

    inline float Multiplier(float value)
    {
        return std::isfinite(value) ? std::clamp(value, 0.01f, 2.0f) : 1.0f;
    }

    inline bool Parse(const char* text, float minimum, float maximum, float& result)
    {
        char*        end = nullptr;
        const double value = std::strtod(text, &end);
        const float  rounded = static_cast<float>(value);
        if (end == text || *end || !std::isfinite(rounded) || rounded < minimum || rounded > maximum)
            return false;
        result = rounded;
        return true;
    }
}
