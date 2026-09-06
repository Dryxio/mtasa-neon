/*****************************************************************************
 * PROJECT: Multi Theft Auto - native UI Lua boundary
 * LICENSE: See LICENSE in the top level directory
 *****************************************************************************/
#include "StdInc.h"
#include "CLuaNativeUIDefs.h"
#include "CLuaPlayerDefs.h"
#include <game/CNativeUI.h>
#include <cmath>
#include <limits>

namespace
{
    CResource* Owner(lua_State* L)
    {
        return g_pClientGame->GetResourceManager()->GetResourceFromLuaState(L);
    }
    CNativeUI* UI()
    {
        return g_pGame->GetNativeUI();
    }
    int Failure(lua_State* L, const std::string& error)
    {
        lua_pushboolean(L, false);
        lua_pushlstring(L, error.data(), error.size());
        return 2;
    }
    bool String(lua_State* L, int index, std::string& out)
    {
        if (lua_type(L, index) != LUA_TSTRING)
            return false;
        size_t      size{};
        const char* value = lua_tolstring(L, index, &size);
        if (size > 640 || std::memchr(value, 0, size))
            return false;
        out.assign(value, size);
        return true;
    }
    bool Number(lua_State* L, int index, double& out, bool integer = false)
    {
        if (lua_type(L, index) != LUA_TNUMBER)
            return false;
        out = lua_tonumber(L, index);
        return std::isfinite(out) && (!integer || std::floor(out) == out);
    }
    bool Handle(lua_State* L, int index, NativeUIHandle& id)
    {
        double n{};
        if (!Number(L, index, n, true) || n < 1 || n > 0xFFFFFF)
            return false;
        id = static_cast<NativeUIHandle>(n);
        return true;
    }
    const char* KindName(ENativeUIKind k)
    {
        const char* names[] = {"text", "clock", "counter", "menu", "grid", "drawText", "window", "rectangle", "card"};
        return names[static_cast<int>(k)];
    }
    bool Kind(const std::string& name, ENativeUIKind& kind)
    {
        for (int i = 0; i < 9; ++i)
            if (name == KindName(static_cast<ENativeUIKind>(i)))
            {
                kind = static_cast<ENativeUIKind>(i);
                return true;
            }
        return false;
    }
    bool Options(lua_State* L, int index, SNativeUIOptions& o, ENativeUIKind kind, std::string& error)
    {
        if (!lua_istable(L, index))
        {
            error = "options-table-required";
            return false;
        }
        // Reject unknown fields and type coercion: typos must not produce a valid
        // but surprising native command, especially for layout or ownership IDs.
        lua_pushnil(L);
        while (lua_next(L, index))
        {
            std::string key;
            bool        valid = String(L, -2, key);
            double      n{};
            auto        numeric = [&](auto& value, double low, double high, bool integer = false)
            {
                if (!Number(L, -1, n, integer) || n < low || n > high)
                    return false;
                value = static_cast<std::decay_t<decltype(value)>>(n);
                return true;
            };
            auto boolean = [&](bool& value)
            {
                if (lua_type(L, -1) != LUA_TBOOLEAN)
                    return false;
                value = lua_toboolean(L, -1) != 0;
                return true;
            };
            if (!valid)
            {
            }
            else if (key == "name")
                valid = String(L, -1, o.name);
            else if (key == "content")
                valid = String(L, -1, o.content);
            else if (key == "gxt")
                valid = boolean(o.gxt);
            else if (key == "text")
                valid = numeric(o.text, 0, 0xFFFFFF, true);
            else if (key == "x")
                valid = numeric(o.x, 0, 640);
            else if (key == "y")
                valid = numeric(o.y, 0, 448);
            else if (key == "width")
                valid = numeric(o.width, 0.01, 640);
            else if (key == "height")
                valid = numeric(o.height, 0.01, 448);
            else if (key == "scaleX")
                valid = numeric(o.scaleX, 0.01, 4);
            else if (key == "scaleY")
                valid = numeric(o.scaleY, 0.01, 4);
            else if (key == "color")
                valid = numeric(o.color, 0, 4294967295.0, true);
            else if (key == "background")
                valid = numeric(o.background, 0, 4294967295.0, true);
            else if (key == "dropColor")
                valid = numeric(o.dropColor, 0, 4294967295.0, true);
            else if (key == "font")
                valid = numeric(o.font, 0, 2, true);
            else if (key == "alignment")
                valid = numeric(o.alignment, 0, 2, true);
            else if (key == "shadow")
                valid = numeric(o.shadow, 0, 4, true);
            else if (key == "outline")
                valid = numeric(o.outline, 0, 4, true);
            else if (key == "style")
                valid = numeric(o.style, 0, 2, true);
            else if (key == "selected")
            {
                valid = numeric(o.selected, 1, 64, true);
                if (valid)
                    --o.selected;
            }
            else if (key == "value")
                valid = numeric(o.value, 0, 2147483647);
            else if (key == "maximum")
                valid = numeric(o.maximum, 0.01, 2147483647);
            else if (key == "beepSeconds")
                valid = numeric(o.beepSeconds, 0, 60, true);
            else if (key == "card")
                valid = numeric(o.card, 1, 53, true);
            else if (key == "columns")
                valid = numeric(o.columns, 1, 8, true);
            else if (key == "visible")
                valid = boolean(o.visible);
            else if (key == "beforeFade")
                valid = boolean(o.beforeFade);
            else if (key == "paused")
                valid = boolean(o.paused);
            else if (key == "countdown")
                valid = boolean(o.countdown);
            else if (key == "flash")
                valid = boolean(o.flash);
            else if (key == "proportional")
                valid = boolean(o.proportional);
            else if (key == "number1")
                valid = numeric(o.numbers[0], -2147483647, 2147483647, true);
            else if (key == "number2")
                valid = numeric(o.numbers[1], -2147483647, 2147483647, true);
            else if (key == "cells" || key == "headers" || key == "enabled" || key == "widths" || key == "alignments")
            {
                valid = lua_istable(L, -1);
                if (valid)
                {
                    const auto count = lua_objlen(L, -1);
                    valid = count <= 48;
                    if (key == "cells")
                        o.cells.clear();
                    if (key == "headers")
                        o.headers.clear();
                    if (key == "enabled")
                        o.enabled.clear();
                    if (key == "widths")
                        o.widths.clear();
                    if (key == "alignments")
                        o.alignments.clear();
                    // Dense arrays only. Sparse or keyed tables cannot silently
                    // discard entries at lua_objlen's undefined boundary.
                    std::size_t seen = 0;
                    lua_pushnil(L);
                    while (lua_next(L, -2))
                    {
                        double arrayIndex{};
                        if (!Number(L, -2, arrayIndex, true) || arrayIndex < 1 || arrayIndex > count)
                            valid = false;
                        ++seen;
                        lua_pop(L, 1);
                    }
                    if (seen != count)
                        valid = false;
                    for (std::size_t i = 0; valid && i < count; ++i)
                    {
                        lua_rawgeti(L, -1, static_cast<int>(i + 1));
                        if (key == "enabled")
                        {
                            valid = lua_type(L, -1) == LUA_TBOOLEAN;
                            if (valid)
                                o.enabled.push_back(lua_toboolean(L, -1) != 0);
                        }
                        else if (key == "cells" || key == "headers")
                        {
                            NativeUIHandle h{};
                            valid = Handle(L, -1, h);
                            if (valid)
                                (key == "cells" ? o.cells : o.headers).push_back(h);
                        }
                        else
                        {
                            valid = Number(L, -1, n, key == "alignments") && n >= 0 && n <= (key == "alignments" ? 2 : 640);
                            if (valid)
                            {
                                if (key == "widths")
                                    o.widths.push_back(static_cast<float>(n));
                                else
                                    o.alignments.push_back(static_cast<int>(n));
                            }
                        }
                        lua_pop(L, 1);
                    }
                }
            }
            else
                valid = false;
            lua_pop(L, 1);
            if (!valid)
            {
                lua_pop(L, 1);
                error = "invalid-option:" + key;
                return false;
            }
        }
        if (kind == ENativeUIKind::Menu && o.enabled.empty() && o.columns && o.cells.size() % o.columns == 0)
            o.enabled.assign(o.cells.size() / o.columns, true);
        return true;
    }
    void Field(lua_State* L, const char* name, double value)
    {
        lua_pushnumber(L, value);
        lua_setfield(L, -2, name);
    }
    void Field(lua_State* L, const char* name, bool value)
    {
        lua_pushboolean(L, value);
        lua_setfield(L, -2, name);
    }
}

void CLuaNativeUIDefs::LoadFunctions()
{
    constexpr static const std::pair<const char*, lua_CFunction> functions[]{
        {"createNativeUI", CreateNativeUI},   {"updateNativeUI", UpdateNativeUI}, {"destroyNativeUI", DestroyNativeUI}, {"getNativeUIState", GetNativeUIState},
        {"releaseNativeUI", ReleaseNativeUI}, {"showNativeText", ShowNativeText}, {"clearNativeText", ClearNativeText},
    };
    for (const auto& [name, function] : functions)
        CLuaCFunctions::AddFunction(name, function);
}
int CLuaNativeUIDefs::CreateNativeUI(lua_State* L)
{
    std::string   name, error;
    ENativeUIKind kind;
    if (lua_gettop(L) != 2 || !String(L, 1, name) || !Kind(name, kind))
        return Failure(L, "invalid-kind-or-arguments");
    SNativeUIOptions o;
    o.alignment = 1;
    if (kind == ENativeUIKind::Counter)
        o.color = 0;
    if (!Options(L, 2, o, kind, error))
        return Failure(L, error);
    const auto id = UI()->Create(Owner(L), kind, o, error);
    if (!id)
        return Failure(L, error);
    lua_pushnumber(L, id);
    return 1;
}
int CLuaNativeUIDefs::UpdateNativeUI(lua_State* L)
{
    NativeUIHandle   id{};
    SNativeUIOptions o;
    ENativeUIKind    kind;
    std::string      error;
    if (lua_gettop(L) != 2 || !Handle(L, 1, id) || !UI()->GetOptions(Owner(L), id, o, kind))
        return Failure(L, "invalid-handle-or-arguments");
    if (!Options(L, 2, o, kind, error) || !UI()->Update(Owner(L), id, o, error))
        return Failure(L, error);
    lua_pushboolean(L, true);
    return 1;
}
int CLuaNativeUIDefs::DestroyNativeUI(lua_State* L)
{
    NativeUIHandle id{};
    std::string    error;
    if (lua_gettop(L) != 1 || !Handle(L, 1, id))
        return Failure(L, "invalid-handle-or-arguments");
    if (!UI()->Destroy(Owner(L), id, error))
        return Failure(L, error);
    lua_pushboolean(L, true);
    return 1;
}
int CLuaNativeUIDefs::GetNativeUIState(lua_State* L)
{
    NativeUIHandle id{};
    SNativeUIState state;
    if (lua_gettop(L) != 1 || !Handle(L, 1, id) || !UI()->GetState(Owner(L), id, state))
        return Failure(L, "invalid-handle-or-arguments");
    lua_newtable(L);
    lua_pushstring(L, KindName(state.kind));
    lua_setfield(L, -2, "kind");
    Field(L, "value", state.value);
    Field(L, "visible", state.visible);
    Field(L, "paused", state.paused);
    Field(L, "finished", state.finished);
    Field(L, "available", state.available);
    Field(L, "selected", static_cast<double>(state.selected + 1));
    Field(L, "accepted", static_cast<double>(state.accepted + 1));
    Field(L, "cancelled", state.cancelled);
    Field(L, "color", static_cast<double>(state.color));
    return 1;
}
int CLuaNativeUIDefs::ReleaseNativeUI(lua_State* L)
{
    if (lua_gettop(L))
        return Failure(L, "unexpected-arguments");
    UI()->Release(Owner(L));
    lua_pushboolean(L, true);
    return 1;
}
int CLuaNativeUIDefs::ShowNativeText(lua_State* L)
{
    NativeUIHandle id{};
    std::string    channel, error;
    double         duration{};
    if (lua_gettop(L) < 3 || lua_gettop(L) > 6 || !Handle(L, 1, id) || !String(L, 2, channel) || !Number(L, 3, duration, true) || duration < 1 ||
        duration > 600000)
        return Failure(L, "invalid-message-arguments");
    int                style = 1;
    std::array<int, 2> numbers{{-1, -1}};
    for (int i = 4; i <= lua_gettop(L); ++i)
    {
        double n{};
        if (!Number(L, i, n, true) || n < -2147483647 || n > 2147483647)
            return Failure(L, "invalid-message-number");
        if (i == 4)
            style = static_cast<int>(n);
        else
            numbers[i - 5] = static_cast<int>(n);
    }
    if (CLuaPlayerDefs::HasMissionTextLease())
        return Failure(L, "legacy-gxt-channel-busy");
    if (!UI()->Show(Owner(L), id, channel, static_cast<unsigned int>(duration), style, numbers, error))
        return Failure(L, error);
    lua_pushboolean(L, true);
    return 1;
}
int CLuaNativeUIDefs::ClearNativeText(lua_State* L)
{
    std::string channel;
    if (lua_gettop(L) != 1 || !String(L, 1, channel))
        return Failure(L, "invalid-channel");
    if (!UI()->Clear(Owner(L), channel))
        return Failure(L, "invalid-channel");
    lua_pushboolean(L, true);
    return 1;
}
