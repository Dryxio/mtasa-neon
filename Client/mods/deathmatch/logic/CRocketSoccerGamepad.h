/*****************************************************************************
 * PROJECT: Multi Theft Auto: Neon
 * LICENSE: See LICENSE in the top level directory
 *****************************************************************************/
#pragma once
#include <array>
#include <cstdint>

namespace RocketSoccer
{
    struct GamepadSnapshot
    {
        std::array<float, 4>  axes{};
        std::array<float, 17> buttons{};
    };

    // Raw normalization only: applying XInput's suggested deadzone here would
    // apply it twice, before the reference's configurable Gamepad deadzone.
    inline GamepadSnapshot NormalizeGamepad(std::uint16_t mask, std::uint8_t lt, std::uint8_t rt, std::int16_t lx, std::int16_t ly, std::int16_t rx,
                                            std::int16_t ry)
    {
        auto            axis = [](std::int16_t v) { return v / (v < 0 ? 32768.0f : 32767.0f); };
        GamepadSnapshot out;
        out.axes = {axis(lx), -axis(ly), axis(rx), -axis(ry)};
        constexpr std::array<std::uint16_t, 17> bits = {0x1000, 0x2000, 0x4000, 0x8000, 0x100, 0x200, 0, 0, 0x20, 0x10, 0x40, 0x80, 1, 2, 4, 8, 0};
        for (std::size_t i = 0; i < bits.size(); ++i)
            out.buttons[i] = (mask & bits[i]) ? 1.0f : 0.0f;
        out.buttons[6] = lt / 255.0f;
        out.buttons[7] = rt / 255.0f;
        return out;
    }
}
