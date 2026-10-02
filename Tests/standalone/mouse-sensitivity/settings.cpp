#include "../../../Client/sdk/game/MouseSensitivity.h"
#include <cassert>
#include <cstdio>
#include <limits>

int main()
{
    using namespace MouseSensitivity;
    assert(Multiplier(1.0f) * NativeAimCoefficient == NativeAimCoefficient);
    assert(Multiplier(0.25f) * NativeAimCoefficient == NativeAimCoefficient / 4.0f);
    assert(Multiplier(std::numeric_limits<float>::quiet_NaN()) == 1.0f);
    assert(Multiplier(std::numeric_limits<float>::infinity()) == 1.0f);
    assert(Multiplier(-1.0f) == 0.01f);
    assert(Multiplier(10.0f) == 2.0f);
    float value = 7;
    for (const char* invalid : {"", "nan", "inf", "1e999", "12x", "-1", "101"})
    {
        assert(!Parse(invalid, 0, 100, value));
        assert(value == 7);
    }
    assert(Parse("0.5125", 0, 100, value) && value == 0.5125f);
    for (float maximum : {HorizontalMaximum, VerticalMaximum})
    {
        // Saved raw values remain unchanged when opening the extended UI range.
        for (float raw : {0.000312f, 0.0015f, 0.0025f, maximum})
        {
            const float ui = (raw - Minimum) / (maximum - Minimum);
            const float restored = Minimum + ui * (maximum - Minimum);
            assert(std::abs(restored - raw) < 1e-9f);
        }
        float previous = -1;
        for (float percent : {0.0f, 0.5000f, 0.5001f, 0.5125f, 0.5250f})
        {
            const float raw = Minimum + (percent / 100.0f) * (maximum - Minimum);
            char        persisted[64];
            std::snprintf(persisted, sizeof(persisted), "%.9g", raw);
            float loaded = 0;
            assert(Parse(persisted, Minimum, maximum, loaded));
            assert(loaded == raw);
            assert(loaded > previous && loaded < 0.000312f);
            previous = loaded;
        }
    }
    assert(std::abs(Minimum + HorizontalDefault * (HorizontalMaximum - Minimum) - 0.0025f) < 1e-9f);
    assert(std::abs(Minimum + VerticalDefault * (VerticalMaximum - Minimum) - 0.0015f) < 1e-9f);
}
