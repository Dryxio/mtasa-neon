#pragma once
#include "CRocketSoccerGamepad.h"
#include <cstddef>

namespace RocketSoccer
{
    inline bool IsSonyGamepad(unsigned vendor, unsigned product)
    {
        return vendor == 0x054c && (product == 0x05c4 || product == 0x09cc || product == 0x0ce6 || product == 0x0df2);
    }

    // Sony USB/full Bluetooth and Bluetooth compatibility reports use different
    // headers. Decode into the same browser-style layout as the XInput backend.
    // Layout references: Linux hid-playstation.c and SDL_hidapi_ps5.c.
    inline bool DecodeSonyGamepad(unsigned product, const std::uint8_t* data, std::size_t size, GamepadSnapshot& out)
    {
        if (!data || !size || !IsSonyGamepad(0x054c, product))
            return false;
        const bool  ps5 = product == 0x0ce6 || product == 0x0df2;
        std::size_t offset = 0, buttons = 4, triggers = 7;
        if (data[0] == 1 && (size == 10 || size == 64 || size == 78))
        {
            offset = 1;
            if (ps5 && size == 64)
            {
                buttons = 7;
                triggers = 4;
            }
        }
        else if (!ps5 && data[0] == 0x11 && size == 78)
            offset = 3;
        else if (ps5 && data[0] == 0x31 && size == 78)
        {
            offset = 2;
            buttons = 7;
            triggers = 4;
        }
        else
            return false;
        const auto*     p = data + offset;
        GamepadSnapshot result;
        for (unsigned i = 0; i < 4; ++i)
            result.axes[i] = (static_cast<int>(p[i]) - 128) / (p[i] < 128 ? 128.0f : 127.0f);
        const auto a = p[buttons], b = p[buttons + 1], c = p[buttons + 2];
        result.buttons[0] = (a & 0x20) != 0;
        result.buttons[1] = (a & 0x40) != 0;
        result.buttons[2] = (a & 0x10) != 0;
        result.buttons[3] = (a & 0x80) != 0;
        result.buttons[4] = (b & 1) != 0;
        result.buttons[5] = (b & 2) != 0;
        result.buttons[6] = p[triggers] / 255.0f;
        result.buttons[7] = p[triggers + 1] / 255.0f;
        for (unsigned i = 0; i < 4; ++i)
            result.buttons[8 + i] = (b & (0x10 << i)) != 0;
        const auto hat = a & 15;
        result.buttons[12] = hat == 0 || hat == 1 || hat == 7;
        result.buttons[13] = hat >= 3 && hat <= 5;
        result.buttons[14] = hat >= 5 && hat <= 7;
        result.buttons[15] = hat >= 1 && hat <= 3;
        result.buttons[16] = (c & 1) != 0;
        out = result;
        return true;
    }
}
