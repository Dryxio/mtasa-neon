/*****************************************************************************
 * PROJECT: Multi Theft Auto - portable native UI ownership and validation
 * LICENSE: See LICENSE in the top level directory
 *****************************************************************************/
#pragma once
#include <game/CNativeUI.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <limits>
#include <utility>

namespace NativeUI
{
    constexpr std::size_t MaxObjects = 256;
    constexpr std::size_t MaxTextBytes = 160;

    // GTA's European font atlas is not Latin-1. Reject unmapped codepoints instead
    // of silently advertising Unicode or passing multibyte UTF-8 into CFont.
    inline bool Encode(const std::string& input, std::string& output)
    {
        static const std::uint16_t upper[] = {0xC0, 0xC1, 0xC2, 0xC4, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF, 0xD2, 0xD3,
                                              0xD4, 0xD6, 0xD9, 0xDA, 0xDB, 0xDC, 0xDF, 0xE0, 0xE1, 0xE2, 0xE4, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA,
                                              0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF6, 0xF9, 0xFA, 0xFB, 0xFC, 0xD1, 0xF1, 0xBF};
        output.clear();
        for (std::size_t i = 0; i < input.size(); ++i)
        {
            unsigned int cp = static_cast<unsigned char>(input[i]);
            if (cp >= 0xC2 && cp <= 0xDF)
            {
                if (++i >= input.size())
                    return false;
                const auto tail = static_cast<unsigned char>(input[i]);
                if ((tail & 0xC0) != 0x80)
                    return false;
                cp = ((cp & 31) << 6) | (tail & 63);
            }
            else if (cp >= 128)
                return false;
            if (cp == 10)
                output += "~n~";
            else if (cp >= 32 && cp < 127)
            {
                // These atlas positions contain inverted punctuation, not ASCII.
                if (cp == '[' || cp == ']' || cp == '^')
                    return false;
                output += static_cast<char>(cp == '<' ? '[' : cp == '>' ? ']' : cp);
            }
            else
            {
                const auto found = std::find(std::begin(upper), std::end(upper), cp);
                if (found == std::end(upper))
                    return false;
                output += static_cast<char>(0x80 + (found - std::begin(upper)));
            }
            if (output.size() > MaxTextBytes)
                return false;
        }
        return !output.empty();
    }

    inline bool Tokens(const std::string& text)
    {
        unsigned keys = 0, numbers = 0;
        for (std::size_t p = 0; (p = text.find('~', p)) != std::string::npos;)
        {
            const auto end = text.find('~', p + 1);
            if (end == std::string::npos)
                return false;
            const auto token = text.substr(p + 1, end - p - 1);
            p = end + 1;
            if ((token == "1" || token == "2") && ++numbers > 6)
                return false;
            if (token == "k")
            {
                if (++keys > 2 || p >= text.size() || text[p] != '~')
                    return false;
                const auto last = text.find('~', p + 1);
                if (last == std::string::npos)
                    return false;
                const auto  action = text.substr(p + 1, last - p - 1);
                const char* allowed[] = {"PED_FIREWEAPON",     "PED_SPRINT",    "PED_JUMPING", "VEHICLE_ENTER_EXIT",
                                         "VEHICLE_ACCELERATE", "VEHICLE_BRAKE", "PED_DUCK",    "PED_LOCK_TARGET"};
                if (std::none_of(std::begin(allowed), std::end(allowed), [&](const char* s) { return action == s; }))
                    return false;
                p = last + 1;
            }
            else if (token.size() != 1 || std::string("rgbwypnshz12").find(token[0]) == std::string::npos)
                return false;
        }
        return true;
    }

    inline std::string Numbers(std::string text, const std::array<int, 2>& numbers)
    {
        for (unsigned n = 0; n < 2; ++n)
        {
            const std::string token = n ? "~2~" : "~1~";
            const auto        value = std::to_string(numbers[n]);
            std::size_t       p = 0;
            while ((p = text.find(token, p)) != std::string::npos)
            {
                text.replace(p, 3, value);
                p += value.size();
            }
        }
        return text;
    }

    struct Entry
    {
        void*            owner{};
        ENativeUIKind    kind{};
        SNativeUIOptions options;
        std::string      encoded;
        std::uint64_t    tick{};
        int              slot{-1};
        int              accepted{-1};
        bool             cancelled{}, finished{}, completionReported{};
    };

    class Model
    {
    public:
        std::map<NativeUIHandle, Entry> entries;
        NativeUIHandle                  next{1};  // Never reused, including resource restart. Fail on exhaustion.
        Entry*                          Find(void* owner, NativeUIHandle handle)
        {
            auto i = entries.find(handle);
            return i != entries.end() && i->second.owner == owner ? &i->second : nullptr;
        }
        bool Text(void* owner, NativeUIHandle handle) const
        {
            auto i = entries.find(handle);
            return i != entries.end() && i->second.owner == owner && i->second.kind == ENativeUIKind::Text;
        }
        std::size_t Count(ENativeUIKind kind) const
        {
            return std::count_if(entries.begin(), entries.end(), [&](const auto& item) { return item.second.kind == kind; });
        }
        bool Validate(void* owner, ENativeUIKind kind, const SNativeUIOptions& o, std::string& error) const
        {
            auto fail = [&](const char* why)
            {
                error = why;
                return false;
            };
            if (!owner)
                return fail("invalid-owner");
            if (kind == ENativeUIKind::Text)
            {
                if (o.name.empty() || o.name.size() > 64 || o.content.empty() || o.content.size() > 640)
                    return fail("invalid-text");
                std::string encoded;
                if (!o.gxt && (!Encode(o.content, encoded) || !Tokens(encoded)))
                    return fail("unsupported-text-or-token");
                if (o.gxt && (o.content.size() > 7 ||
                              o.content.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos))
                    return fail("invalid-gxt-key");
                return true;
            }
            if (kind == ENativeUIKind::Card && (o.card < 1 || o.card > 53))
                return fail("invalid-card");
            if (o.text && !Text(owner, o.text))
                return fail("invalid-text-handle");
            const float finite[] = {o.x, o.y, o.width, o.height, o.scaleX, o.scaleY};
            for (float f : finite)
                if (!std::isfinite(f))
                    return fail("non-finite");
            if (o.x < 0 || o.x > 640 || o.y < 0 || o.y > 448 || o.width <= 0 || o.width > 640 || o.height <= 0 || o.height > 448 || o.scaleX <= 0 ||
                o.scaleX > 4 || o.scaleY <= 0 || o.scaleY > 4)
                return fail("invalid-layout");
            if (o.font < 0 || o.font > 2 || o.alignment < 0 || o.alignment > 2 || o.shadow < 0 || o.shadow > 4 || o.outline < 0 || o.outline > 4)
                return fail("invalid-font");
            if (!std::isfinite(o.value) || !std::isfinite(o.maximum) || o.value < 0 || o.maximum <= 0 || o.value > 2147483647 || o.maximum > 2147483647)
                return fail("invalid-value");
            if (kind == ENativeUIKind::Clock && (o.value > 5999999 || o.beepSeconds > 60))
                return fail("invalid-clock");
            if (kind == ENativeUIKind::Counter && (o.style < 0 || o.style > 2 || o.color > 14 || (o.style == 1 && o.value > o.maximum)))
                return fail("invalid-counter");
            if ((kind == ENativeUIKind::DrawText || kind == ENativeUIKind::Window) && !o.text)
                return fail("text-required");
            if (kind == ENativeUIKind::Menu || kind == ENativeUIKind::Grid)
            {
                if (o.columns < 1 || o.columns > (kind == ENativeUIKind::Grid ? 8u : 4u))
                    return fail("invalid-columns");
                if (kind == ENativeUIKind::Menu)
                {
                    if (o.cells.empty() || o.cells.size() % o.columns || o.cells.size() / o.columns > 12)
                        return fail("invalid-rows");
                    for (auto t : o.cells)
                        if (!Text(owner, t))
                            return fail("invalid-cell");
                    for (auto t : o.headers)
                        if (t && !Text(owner, t))
                            return fail("invalid-header");
                    if (o.headers.size() > o.columns || o.enabled.size() != o.cells.size() / o.columns ||
                        std::none_of(o.enabled.begin(), o.enabled.end(), [](bool b) { return b; }))
                        return fail("invalid-enabled-rows");
                    if (o.selected < 0 || static_cast<std::size_t>(o.selected) >= o.enabled.size() || !o.enabled[o.selected])
                        return fail("invalid-selection");
                    if (o.widths.size() > o.columns || o.alignments.size() > o.columns)
                        return fail("invalid-columns");
                    float total = 0;
                    for (unsigned i = 0; i < o.columns; ++i)
                    {
                        float w = i < o.widths.size() ? o.widths[i] : o.width;
                        if (!std::isfinite(w) || w <= 0 || w > 640)
                            return fail("invalid-column-width");
                        total += w;
                    }
                    if (total > 640)
                        return fail("menu-too-wide");
                    for (int a : o.alignments)
                        if (a < 0 || a > 2)
                            return fail("invalid-alignment");
                }
                else if (o.selected < 0 || o.selected >= static_cast<int>(o.columns * o.columns))
                    return fail("invalid-selection");
            }
            return true;
        }
        bool Referenced(void* owner, NativeUIHandle text) const
        {
            for (const auto& item : entries)
            {
                const auto& e = item.second;
                if (e.owner != owner || e.kind == ENativeUIKind::Text)
                    continue;
                if (e.options.text == text || std::find(e.options.cells.begin(), e.options.cells.end(), text) != e.options.cells.end() ||
                    std::find(e.options.headers.begin(), e.options.headers.end(), text) != e.options.headers.end())
                    return true;
            }
            return false;
        }
        NativeUIHandle Insert(void* owner, ENativeUIKind kind, const SNativeUIOptions& o, std::uint64_t now, std::string& error)
        {
            if (!Validate(owner, kind, o, error))
                return 0;
            if (entries.size() >= MaxObjects || next > 0xFFFFFF)
            {
                error = "capacity";
                return 0;
            }
            if (kind == ENativeUIKind::Text)
                for (const auto& item : entries)
                    if (item.second.owner == owner && item.second.kind == kind && item.second.options.name == o.name)
                    {
                        error = "duplicate-name";
                        return 0;
                    }
            if ((kind == ENativeUIKind::Clock && Count(kind) >= 1) || (kind == ENativeUIKind::Counter && Count(kind) >= 4) ||
                ((kind == ENativeUIKind::Menu || kind == ENativeUIKind::Grid) && Count(ENativeUIKind::Menu) + Count(ENativeUIKind::Grid) >= 2) ||
                ((kind == ENativeUIKind::DrawText || kind == ENativeUIKind::Window || kind == ENativeUIKind::Rectangle || kind == ENativeUIKind::Card) &&
                 Count(ENativeUIKind::DrawText) + Count(ENativeUIKind::Window) + Count(ENativeUIKind::Rectangle) + Count(ENativeUIKind::Card) >= 96))
            {
                error = "capacity";
                return 0;
            }
            auto  id = next++;
            Entry e;
            e.owner = owner;
            e.kind = kind;
            e.options = o;
            e.tick = now;
            if (kind == ENativeUIKind::Text && !o.gxt)
                Encode(o.content, e.encoded);
            entries.emplace(id, std::move(e));
            return id;
        }
        static bool TakeCompletion(Entry& e)
        {
            if (!e.finished || e.completionReported)
                return false;
            // A late authoritative correction may resume the displayed clock,
            // but the same generation must never notify mission completion twice.
            e.completionReported = true;
            return true;
        }
        static bool Tick(Entry& e, std::uint64_t now)
        {
            if (e.kind != ENativeUIKind::Clock)
                return false;
            const auto delta = now >= e.tick ? now - e.tick : 0;
            e.tick = now;
            if (e.options.paused || e.finished)
                return false;
            const auto before = e.options.value;
            e.options.value =
                e.options.countdown ? std::max(0.0, before - static_cast<double>(delta)) : std::min(5999999.0, before + static_cast<double>(delta));
            if (e.options.countdown && e.options.value == 0)
                e.finished = true;
            return e.options.countdown && std::floor(before / 1000) != std::floor(e.options.value / 1000) && e.options.value > 0 &&
                   e.options.value / 1000 < e.options.beepSeconds;
        }
    };
}
