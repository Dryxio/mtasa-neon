// Project2DFX data and submission rules shared by the game and standalone tests.
// Keep native calls outside this file so tests execute the production decisions.
#pragma once
#include "../sdk/game/SDistantLightSettings.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace DistantLights
{
    template <class Vector>
    struct Definition
    {
        Vector       localPosition{};
        float        coronaSize = 0, drawDistance = 0;
        std::uint8_t red = 0, green = 0, blue = 0, alpha = 0, flashType = 0;
        // Values >= 2 encode a native point-light type, not just a distance flag.
        std::uint8_t noDistance = 0;
        bool         trafficLight = false, drawSearchlight = false;
    };

    template <class T>
    bool ReadNumber(const std::string& token, T& value)
    {
        std::istringstream input(token);
        input.imbue(std::locale::classic());
        return bool(input >> value) && input.peek() == std::char_traits<char>::eof();
    }

    template <class Vector>
    bool Parse(const char* line, Definition<Vector>& result)
    {
        std::istringstream       input(line);
        std::vector<std::string> fields;
        std::string              token;
        while (input >> token)
        {
            if (token[0] == '#')
                break;
            fields.push_back(token);
        }
        if (fields.size() != 11 && fields.size() != 12)
            return false;
        Definition<Vector> next{};
        int                rgba[4]{}, flash = 0, type = 0, searchlight = 0;
        for (int i = 0; i < 4; ++i)
            if (!ReadNumber(fields[i], rgba[i]) || rgba[i] < 0 || rgba[i] > 255)
                return false;
        float*    values[] = {&next.localPosition.fX, &next.localPosition.fY, &next.localPosition.fZ, &next.coronaSize, &next.drawDistance};
        const int count = fields.size() == 12 ? 5 : 4;
        for (int i = 0; i < count; ++i)
            if (!ReadNumber(fields[4 + i], *values[i]) || !std::isfinite(*values[i]))
                return false;
        if (!ReadNumber(fields[4 + count], flash) || flash < 0 || flash > 7 || !ReadNumber(fields[5 + count], type) || type < 0 || type > 7 ||
            !ReadNumber(fields[6 + count], searchlight) || searchlight < 0 || searchlight > 1 || next.coronaSize <= 0 || next.drawDistance < 0)
            return false;
        next.red = static_cast<std::uint8_t>(rgba[0]);
        next.green = static_cast<std::uint8_t>(rgba[1]);
        next.blue = static_cast<std::uint8_t>(rgba[2]);
        next.alpha = static_cast<std::uint8_t>(rgba[3]);
        next.flashType = static_cast<std::uint8_t>(flash ? flash + 18 : 0);
        next.noDistance = static_cast<std::uint8_t>(type);
        next.drawSearchlight = searchlight != 0;
        next.trafficLight = std::abs(next.coronaSize - 0.45f) < 0.001f;
        result = next;
        return true;
    }

    // Retain unique immutable IPL placements: streaming can unload their entities
    // before an explicit rebuild. No GTA entity pointer is kept across streaming.
    template <class Source>
    class Sources
    {
        std::map<std::array<float, 8>, Source> records;

    public:
        void Remember(const Source& source)
        {
            const std::array<float, 8> key{float(source.modelId), source.position.fX, source.position.fY, source.position.fZ,
                                           source.rotation.fX,    source.rotation.fY, source.rotation.fZ, source.rotation.fW};
            for (float value : key)
                if (!std::isfinite(value))
                    return;
            records.emplace(key, source);
        }
        template <class Visitor>
        void Replay(Visitor visitor) const
        {
            for (const auto& item : records)
                visitor(item.second);
        }
        void        clear() { records.clear(); }
        bool        empty() const { return records.empty(); }
        std::size_t size() const { return records.size(); }
    };

    inline float SolveLinear(float a, float b, float c, float d, float value)
    {
        const float determinant = a - c;
        if (std::abs(determinant) < 0.001f)
            return d;

        const float x = (b - d) / determinant;
        const float y = (a * d - b * c) / determinant;
        return std::min(x * value + y, d);
    }

    inline std::uint8_t GetNightAlpha(std::uint8_t hour, std::uint8_t minute)
    {
        const unsigned int time = hour * 60 + minute;
        if (time >= 20 * 60)
            return static_cast<std::uint8_t>(std::clamp((15.0f / 16.0f) * time - 1095.0f, 0.0f, 255.0f));
        if (time < 3 * 60)
            return 255;
        return static_cast<std::uint8_t>(std::clamp((-15.0f / 16.0f) * time + 424.0f, 0.0f, 255.0f));
    }

    inline bool IsDistantLightOn(std::uint8_t flashType, std::size_t index, std::uint32_t timeMs)
    {
        const std::uint32_t seed = static_cast<std::uint32_t>(index * 2654435761u);
        switch (flashType)
        {
            case 0:  // FLASH_DEFAULT
                return true;
            case 1:  // FLASH_RANDOM
            case 2:  // FLASH_RANDOM_WHEN_WET; weather dependency is omitted in phase 1
                return ((timeMs ^ seed) & 0x60) != 0 || ((seed ^ (timeMs / 4096)) & 0x3) != 0;
            case 3:  // FLASH_ANIM_SPEED_4X
                return ((timeMs + seed * 128) & 0x200) != 0;
            case 4:  // FLASH_ANIM_SPEED_2X
                return ((timeMs + seed * 256) & 0x400) != 0;
            case 5:  // FLASH_ANIM_SPEED_1X
                return ((timeMs + seed * 512) & 0x800) != 0;
            case 11:  // FLASH_5ON_5OFF
                return (timeMs + seed) % 10000 < 5000;
            case 12:  // FLASH_6ON_4OFF
                return (timeMs + seed) % 10000 < 6000;
            case 13:  // FLASH_4ON_6OFF
                return (timeMs + seed) % 10000 < 4000;
            case 19:  // Project2DFX DAT: random flashing (500 ms on/off)
                return (timeMs + seed) % 1000 < 500;
            case 20:  // Project2DFX DAT: 1 second on/off
                return (timeMs + seed) % 2000 < 1000;
            case 21:  // Project2DFX DAT: 2 seconds on/off
                return (timeMs + seed) % 4000 < 2000;
            case 22:  // Project2DFX DAT: 3 seconds on/off
                return (timeMs + seed) % 6000 < 3000;
            case 23:  // Project2DFX DAT: 4 seconds on/off
                return (timeMs + seed) % 8000 < 4000;
            case 24:  // Project2DFX DAT: 5 seconds on/off
                return (timeMs + seed) % 10000 < 5000;
            case 25:  // Project2DFX DAT: 6 seconds on, 4 seconds off
                return (timeMs + seed) % 10000 < 6000;
            default:
                return false;
        }
    }

    template <class Source, class Vector>
    Vector Transform(const Source& instance, const Vector& localPosition)
    {
        float       x = instance.rotation.fX;
        float       y = instance.rotation.fY;
        float       z = instance.rotation.fZ;
        float       w = instance.rotation.fW;
        const float lengthSquared = x * x + y * y + z * z + w * w;
        if (lengthSquared <= std::numeric_limits<float>::epsilon())
        {
            x = 0.0f;
            y = 0.0f;
            z = 0.0f;
            w = 1.0f;
        }
        else if (std::abs(lengthSquared - 1.0f) > std::numeric_limits<float>::epsilon())
        {
            const float inverseLength = 1.0f / std::sqrt(lengthSquared);
            x *= inverseLength;
            y *= inverseLength;
            z *= inverseLength;
            w *= inverseLength;
        }

        const Vector right(1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y + w * z), 2.0f * (x * z - w * y));
        const Vector front(2.0f * (x * y - w * z), 1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z + w * x));
        const Vector up(2.0f * (x * z + w * y), 2.0f * (y * z - w * x), 1.0f - 2.0f * (x * x + y * y));
        return instance.position + right * localPosition.fX + front * localPosition.fY + up * localPosition.fZ;
    }

    inline float ResolveRange(bool automatic, float gameFarClip, float manualRange)
    {
        // Read the game's resolved far clip; never override its weather/server
        // policy. A transient invalid value falls back to the manual preference.
        return automatic && std::isfinite(gameFarClip) && gameFarClip > 1.0f ? gameFarClip : manualRange;
    }

    struct Corona
    {
        float        radius = 0, intensity = 0;
        std::uint8_t alpha = 0;
    };
    inline Corona Evaluate(float distance, float drawDistance, float farDistance, float size, float multiplier, unsigned type, std::uint8_t alpha,
                           std::uint8_t nightAlpha, bool on, const SDistantLightSettings& settings = {})
    {
        const float nearDistance = std::max(0.0f, drawDistance - 30.0f);
        if (!on || distance >= farDistance || (!type && distance <= nearDistance))
            return {};
        float radius = type ? 1.75f : SolveLinear(nearDistance, 0, std::max(drawDistance, nearDistance + 1), 1.75f, distance);
        if (settings.growWithDistance)
            radius *= std::min(SolveLinear(nearDistance, 1, 1000, 4, distance), 4.0f);
        radius *= size * multiplier;
        float fade = type ? 1.0f : std::clamp((distance - nearDistance) / 30, 0.0f, 1.0f);
        if (!type && distance >= drawDistance && distance > farDistance - 100)
            fade *= std::clamp((farDistance - distance) / 100, 0.0f, 1.0f);
        const float offset = distance - nearDistance;
        float       boost = settings.nearAlpha + std::clamp(offset / settings.reachFullAlpha, 0.0f, 1.0f) * (1 - settings.nearAlpha);
        if (offset > settings.boostStart)
            boost = 1 + std::clamp((offset - settings.boostStart) / 900, 0.0f, 1.0f) * (settings.farAlphaBoost - 1);
        const float scale = radius > 1 ? std::clamp(1 / (0.75f * radius + 0.25f), 0.3f, 1.0f) : 1;
        const float intensity = (nightAlpha / 255.0f) * (alpha / 255.0f) * fade * boost * scale;
        return {radius, intensity, static_cast<std::uint8_t>(std::clamp(intensity * 255, 0.0f, 255.0f))};
    }
    struct PointLight
    {
        int   type = 0;
        float radius = 0, red = 0, green = 0, blue = 0;
    };
    inline bool MakePointLight(unsigned type, float distance, float size, float drawDistance, float intensity, unsigned red, unsigned green, unsigned blue,
                               PointLight& output)
    {
        if (type < 2 || type > 7 || distance > 22 || intensity <= 0)
            return false;
        const float radius = drawDistance < 20 ? drawDistance : size * 10;
        if (!std::isfinite(radius) || radius <= 0 || !std::isfinite(intensity))
            return false;
        output = {static_cast<int>(type - 2), radius, red / 255.0f * intensity, green / 255.0f * intensity, blue / 255.0f * intensity};
        return true;
    }
    // GTA's Win32 cdecl ABI passes all fourteen arguments on the stack.
#ifdef _MSC_VER
    using AddPointLight = void(__cdecl*)(int, float, float, float, float, float, float, float, float, float, float, int, int, void*);
#else
    using AddPointLight = void (*)(int, float, float, float, float, float, float, float, float, float, float, int, int, void*);
#endif
    template <class Vector>
    void SubmitPointLight(AddPointLight submit, const Vector& position, const PointLight& light)
    {
        submit(light.type, position.fX, position.fY, position.fZ, 0.0f, 0.0f, -1.0f, light.radius, light.red, light.green, light.blue, 0, 0, nullptr);
    }

    // Only render when all original states can be recovered. A failed capture
    // must not leave a half-configured renderer for subsequent MTA passes.
    template <class Get, class Set, class Render>
    bool WithConeStates(Get get, Set set, Render render)
    {
        constexpr std::uint32_t states[] = {1, 6, 7, 8, 10, 11, 12, 14, 20, 29, 30};
        void*                   saved[11]{};
        for (std::size_t i = 0; i < 11; ++i)
            if (!get(states[i], &saved[i]))
                return false;
        render();
        for (std::size_t i = 0; i < 11; ++i)
            set(states[i], saved[i]);
        return true;
    }

    inline float SmoothStep(float value)
    {
        value = std::clamp(value, 0.0f, 1.0f);
        return value * value * (3 - 2 * value);
    }
    struct Cone
    {
        float length = 0, startRadius = 0, endRadius = 0, intensity = 0;
    };
    inline bool MakeCone(float distance, float height, float size, bool on, Cone& output)
    {
        if (!on || !std::isfinite(height) || height <= 0 || distance <= 45 || distance >= 300)
            return false;
        // Project2DFX projects a radius at 100 metres onto the end plane,
        // extends it by 3 metres, then caps each cone edge at 100 metres.
        // GTA expects the actual end radius rather than that reference radius.
        const float startRadius = size / 6;
        const float slope = (std::min(height * 8, 90.0f) - startRadius) / 100;
        const float length = std::min(height + 3, 100 / std::sqrt(1 + slope * slope));
        output = {length, startRadius, startRadius + slope * length, 0.4f * SmoothStep((distance - 45) / 20) * SmoothStep((300 - distance) / 20)};
        return true;
    }
    template <class Candidate>
    void KeepNearest(std::vector<Candidate>& candidates, std::size_t capacity)
    {
        if (candidates.size() <= capacity)
            return;
        std::nth_element(candidates.begin(), candidates.begin() + capacity, candidates.end(),
                         [](const Candidate& a, const Candidate& b) { return a.distanceSquared < b.distanceSquared; });
        candidates.resize(capacity);
    }
}
