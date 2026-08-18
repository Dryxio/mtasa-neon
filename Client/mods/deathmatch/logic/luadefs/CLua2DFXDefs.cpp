/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.x / Neon
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/mods/deathmatch/logic/luadefs/CLua2DFXDefs.cpp
 *  PURPOSE:     Resource-owned model 2DFX Lua API
 *
 *****************************************************************************/
#include "StdInc.h"
#include "CLua2DFXDefs.h"
#include "../CClient2DFXManager.h"

#include <cmath>
#include <limits>

namespace
{
    int AbsIndex(lua_State* luaVM, int index)
    {
        if (index > 0 || index <= LUA_REGISTRYINDEX)
            return index;
        return lua_gettop(luaVM) + index + 1;
    }

    CClient2DFXManager& Manager()
    {
        CClient2DFXManager& manager = CClient2DFXManager::GetSingleton();
        manager.Initialize(CLuaDefs::m_pManager);
        return manager;
    }

    CResource* Owner(lua_State* luaVM)
    {
        CLuaMain* main = CLuaDefs::m_pLuaManager ? CLuaDefs::m_pLuaManager->GetVirtualMachine(luaVM) : nullptr;
        return main ? main->GetResource() : nullptr;
    }

    void LogError(lua_State* luaVM, const char* function, const char* message)
    {
        if (CLuaDefs::m_pScriptDebugging)
            CLuaDefs::m_pScriptDebugging->LogCustom(luaVM, SString("Bad argument @ '%s' [%s]", function, message));
    }

    bool IsFinite(float value)
    {
        return std::isfinite(value);
    }

    bool IsFinite(const CVector& value)
    {
        return IsFinite(value.fX) && IsFinite(value.fY) && IsFinite(value.fZ);
    }

    bool ReadNumberField(lua_State* luaVM, int tableIndex, const char* key, float& output)
    {
        tableIndex = AbsIndex(luaVM, tableIndex);
        lua_getfield(luaVM, tableIndex, key);
        const bool valid = lua_isnumber(luaVM, -1) != 0;
        if (valid)
            output = static_cast<float>(lua_tonumber(luaVM, -1));
        lua_pop(luaVM, 1);
        return valid && IsFinite(output);
    }

    bool ReadIntegerField(lua_State* luaVM, int tableIndex, const char* key, int& output)
    {
        float value{};
        if (!ReadNumberField(luaVM, tableIndex, key, value) || std::floor(value) != value || value < static_cast<float>(std::numeric_limits<int>::min()) ||
            value > static_cast<float>(std::numeric_limits<int>::max()))
            return false;
        output = static_cast<int>(value);
        return true;
    }

    bool ReadBoolField(lua_State* luaVM, int tableIndex, const char* key, bool& output)
    {
        tableIndex = AbsIndex(luaVM, tableIndex);
        lua_getfield(luaVM, tableIndex, key);
        const bool valid = lua_isboolean(luaVM, -1) != 0;
        if (valid)
            output = lua_toboolean(luaVM, -1) != 0;
        lua_pop(luaVM, 1);
        return valid;
    }

    bool ReadStringField(lua_State* luaVM, int tableIndex, const char* key, std::string& output)
    {
        tableIndex = AbsIndex(luaVM, tableIndex);
        lua_getfield(luaVM, tableIndex, key);
        const bool valid = lua_type(luaVM, -1) == LUA_TSTRING;
        if (valid)
            output = lua_tostring(luaVM, -1);
        lua_pop(luaVM, 1);
        return valid;
    }

    bool ReadVector(lua_State* luaVM, int index, CVector& output, std::size_t components = 3)
    {
        index = AbsIndex(luaVM, index);
        if (!lua_istable(luaVM, index))
            return false;

        float values[3]{};
        for (std::size_t component = 0; component < components; ++component)
        {
            lua_rawgeti(luaVM, index, static_cast<int>(component + 1));
            const bool valid = lua_isnumber(luaVM, -1) != 0;
            if (valid)
                values[component] = static_cast<float>(lua_tonumber(luaVM, -1));
            lua_pop(luaVM, 1);
            if (!valid || !IsFinite(values[component]))
                return false;
        }
        output = CVector(values[0], values[1], components > 2 ? values[2] : 0.0f);
        return true;
    }

    bool ReadVectorField(lua_State* luaVM, int tableIndex, const char* key, CVector& output, std::size_t components = 3)
    {
        tableIndex = AbsIndex(luaVM, tableIndex);
        lua_getfield(luaVM, tableIndex, key);
        const bool result = ReadVector(luaVM, -1, output, components);
        lua_pop(luaVM, 1);
        return result;
    }

    bool ReadPackedColor(lua_State* luaVM, int index, std::uint32_t& color)
    {
        if (!lua_isnumber(luaVM, index))
            return false;
        const double value = lua_tonumber(luaVM, index);
        if (!std::isfinite(value) || value < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
            value > static_cast<double>(std::numeric_limits<std::uint32_t>::max()))
            return false;
        color = static_cast<std::uint32_t>(static_cast<std::int64_t>(value));
        return true;
    }

    bool ReadColorField(lua_State* luaVM, int tableIndex, const char* key, std::uint32_t& color)
    {
        tableIndex = AbsIndex(luaVM, tableIndex);
        lua_getfield(luaVM, tableIndex, key);
        const bool result = ReadPackedColor(luaVM, -1, color);
        lua_pop(luaVM, 1);
        return result;
    }

    bool StringToEffectType(const char* value, e2dEffectType& output)
    {
        if (!value)
            return false;
        if (strcmp(value, "light") == 0) output = e2dEffectType::LIGHT;
        else if (strcmp(value, "particle") == 0) output = e2dEffectType::PARTICLE;
        else if (strcmp(value, "sun_glare") == 0) output = e2dEffectType::SUN_GLARE;
        else if (strcmp(value, "roadsign") == 0) output = e2dEffectType::ROADSIGN;
        else if (strcmp(value, "escalator") == 0) output = e2dEffectType::ESCALATOR;
        else return false;
        return true;
    }

    const char* EffectTypeToString(e2dEffectType type)
    {
        switch (type)
        {
            case e2dEffectType::LIGHT: return "light";
            case e2dEffectType::PARTICLE: return "particle";
            case e2dEffectType::SUN_GLARE: return "sun_glare";
            case e2dEffectType::ROADSIGN: return "roadsign";
            case e2dEffectType::ESCALATOR: return "escalator";
            default: return "unknown";
        }
    }

    bool StringToFlashType(const char* value, e2dCoronaFlashType& output)
    {
        if (!value) return false;
        struct Pair { const char* name; e2dCoronaFlashType value; };
        static const Pair values[]{
            {"default", e2dCoronaFlashType::DEFAULT}, {"random_flashing", e2dCoronaFlashType::RANDOM},
            {"random_at_wet_weather", e2dCoronaFlashType::RANDOM_WHEN_WET}, {"anim_speed_4x", e2dCoronaFlashType::ANIM_SPEED_4X},
            {"anim_speed_2x", e2dCoronaFlashType::ANIM_SPEED_2X}, {"anim_speed_1x", e2dCoronaFlashType::ANIM_SPEED_1X},
            {"warnlight", e2dCoronaFlashType::WARNLIGHT}, {"trafficlight", e2dCoronaFlashType::TRAFFICLIGHT},
            {"traincrosslight", e2dCoronaFlashType::TRAINCROSSING}, {"at_rain_only", e2dCoronaFlashType::ONLY_RAIN},
            {"on_off_at_5", e2dCoronaFlashType::ON5_OFF5}, {"on_at_6_off_at_4", e2dCoronaFlashType::ON6_OFF4},
            {"on_at_4_off_at_6", e2dCoronaFlashType::ON4_OFF6},
        };
        for (const auto& item : values)
        {
            if (strcmp(item.name, value) == 0)
            {
                output = item.value;
                return true;
            }
        }
        return false;
    }

    const char* FlashTypeToString(e2dCoronaFlashType type)
    {
        switch (type)
        {
            case e2dCoronaFlashType::DEFAULT: return "default";
            case e2dCoronaFlashType::RANDOM: return "random_flashing";
            case e2dCoronaFlashType::RANDOM_WHEN_WET: return "random_at_wet_weather";
            case e2dCoronaFlashType::ANIM_SPEED_4X: return "anim_speed_4x";
            case e2dCoronaFlashType::ANIM_SPEED_2X: return "anim_speed_2x";
            case e2dCoronaFlashType::ANIM_SPEED_1X: return "anim_speed_1x";
            case e2dCoronaFlashType::WARNLIGHT: return "warnlight";
            case e2dCoronaFlashType::TRAFFICLIGHT: return "trafficlight";
            case e2dCoronaFlashType::TRAINCROSSING: return "traincrosslight";
            case e2dCoronaFlashType::ONLY_RAIN: return "at_rain_only";
            case e2dCoronaFlashType::ON5_OFF5: return "on_off_at_5";
            case e2dCoronaFlashType::ON6_OFF4: return "on_at_6_off_at_4";
            case e2dCoronaFlashType::ON4_OFF6: return "on_at_4_off_at_6";
            default: return "default";
        }
    }

    bool StringToProperty(const char* value, e2dEffectProperty& output)
    {
        if (!value) return false;
        struct Pair { const char* name; e2dEffectProperty property; };
        static const Pair properties[]{
            {"drawDistance", e2dEffectProperty::FAR_CLIP_DISTANCE}, {"lightRange", e2dEffectProperty::LIGHT_RANGE},
            {"coronaSize", e2dEffectProperty::CORONA_SIZE}, {"shadowSize", e2dEffectProperty::SHADOW_SIZE},
            {"shadowMultiplier", e2dEffectProperty::SHADOW_MULT}, {"showMode", e2dEffectProperty::FLASH_TYPE},
            {"coronaReflection", e2dEffectProperty::CORONA_REFLECTION}, {"flareType", e2dEffectProperty::FLARE_TYPE},
            {"shadowDistance", e2dEffectProperty::SHADOW_DISTANCE}, {"offset", e2dEffectProperty::OFFSET},
            {"color", e2dEffectProperty::COLOR}, {"coronaName", e2dEffectProperty::CORONA_NAME},
            {"shadowName", e2dEffectProperty::SHADOW_NAME}, {"flags", e2dEffectProperty::FLAGS},
            {"name", e2dEffectProperty::PARTICLE_NAME}, {"size", e2dEffectProperty::SIZE}, {"rotation", e2dEffectProperty::ROTATION},
            {"text1", e2dEffectProperty::TEXT_1}, {"text2", e2dEffectProperty::TEXT_2}, {"text3", e2dEffectProperty::TEXT_3},
            {"text4", e2dEffectProperty::TEXT_4}, {"bottom", e2dEffectProperty::BOTTOM}, {"top", e2dEffectProperty::TOP},
            {"end", e2dEffectProperty::END}, {"direction", e2dEffectProperty::DIRECTION},
        };
        for (const auto& item : properties)
        {
            if (strcmp(item.name, value) == 0)
            {
                output = item.property;
                return true;
            }
        }
        return false;
    }

    bool ReadFlags(lua_State* luaVM, int index, e2dEffectType type, std::uint16_t& flags)
    {
        index = AbsIndex(luaVM, index);
        if (lua_isnumber(luaVM, index))
        {
            const double value = lua_tonumber(luaVM, index);
            if (!std::isfinite(value) || value < 0.0 || value > 65535.0 || std::floor(value) != value)
                return false;
            flags = static_cast<std::uint16_t>(value);
            return true;
        }
        if (!lua_istable(luaVM, index))
            return false;

        flags = 0;
        if (type == e2dEffectType::LIGHT)
        {
            struct Flag { const char* name; unsigned bit; };
            static const Flag values[]{
                {"checkObstacles", 0}, {"fogType", 1}, {"fogType2", 2}, {"withoutCorona", 3}, {"onlyLongDistance", 4}, {"atDay", 5},
                {"atNight", 6}, {"blinking1", 7}, {"onlyFromBelow", 8}, {"blinking2", 9}, {"updateHeightAboveGround", 10},
                {"checkDirection", 11}, {"blinking3", 12},
            };
            for (const auto& item : values)
            {
                lua_getfield(luaVM, index, item.name);
                if (lua_isboolean(luaVM, -1) && lua_toboolean(luaVM, -1))
                    flags |= static_cast<std::uint16_t>(1u << item.bit);
                else if (!lua_isnil(luaVM, -1) && !lua_isboolean(luaVM, -1))
                {
                    lua_pop(luaVM, 1);
                    return false;
                }
                lua_pop(luaVM, 1);
            }
            return true;
        }
        if (type == e2dEffectType::ROADSIGN)
        {
            int lines = 4;
            int characters = 16;
            lua_getfield(luaVM, index, "lines");
            if (!lua_isnil(luaVM, -1))
            {
                if (!lua_isnumber(luaVM, -1)) { lua_pop(luaVM, 1); return false; }
                lines = static_cast<int>(lua_tonumber(luaVM, -1));
            }
            lua_pop(luaVM, 1);
            lua_getfield(luaVM, index, "charactersPerLine");
            if (!lua_isnil(luaVM, -1))
            {
                if (!lua_isnumber(luaVM, -1)) { lua_pop(luaVM, 1); return false; }
                characters = static_cast<int>(lua_tonumber(luaVM, -1));
            }
            lua_pop(luaVM, 1);
            if (lines < 1 || lines > 4 || (characters != 2 && characters != 4 && characters != 8 && characters != 16))
                return false;
            const int lineBits = lines == 4 ? 0 : lines;
            int characterBits = 0;
            if (characters == 2) characterBits = 1;
            else if (characters == 4) characterBits = 2;
            else if (characters == 8) characterBits = 3;
            flags = static_cast<std::uint16_t>((lineBits & 3) | ((characterBits & 3) << 2));
            return true;
        }
        return false;
    }

    bool ReadFlagsField(lua_State* luaVM, int tableIndex, const char* key, e2dEffectType type, std::uint16_t& flags)
    {
        tableIndex = AbsIndex(luaVM, tableIndex);
        lua_getfield(luaVM, tableIndex, key);
        const bool result = ReadFlags(luaVM, -1, type, flags);
        lua_pop(luaVM, 1);
        return result;
    }

    void PushVector(lua_State* luaVM, const CVector& value, std::size_t components = 3)
    {
        lua_createtable(luaVM, static_cast<int>(components), 0);
        lua_pushnumber(luaVM, value.fX); lua_rawseti(luaVM, -2, 1);
        lua_pushnumber(luaVM, value.fY); lua_rawseti(luaVM, -2, 2);
        if (components > 2) { lua_pushnumber(luaVM, value.fZ); lua_rawseti(luaVM, -2, 3); }
    }

    void PushColor(lua_State* luaVM, std::uint32_t color)
    {
        lua_createtable(luaVM, 4, 0);
        lua_pushnumber(luaVM, (color >> 16) & 0xFF); lua_rawseti(luaVM, -2, 1);
        lua_pushnumber(luaVM, (color >> 8) & 0xFF); lua_rawseti(luaVM, -2, 2);
        lua_pushnumber(luaVM, color & 0xFF); lua_rawseti(luaVM, -2, 3);
        lua_pushnumber(luaVM, (color >> 24) & 0xFF); lua_rawseti(luaVM, -2, 4);
    }

    void PushFlags(lua_State* luaVM, e2dEffectType type, std::uint16_t flags)
    {
        lua_newtable(luaVM);
        if (type == e2dEffectType::LIGHT)
        {
            struct Flag { const char* name; unsigned bit; };
            static const Flag values[]{
                {"checkObstacles", 0}, {"fogType", 1}, {"fogType2", 2}, {"withoutCorona", 3}, {"onlyLongDistance", 4}, {"atDay", 5},
                {"atNight", 6}, {"blinking1", 7}, {"onlyFromBelow", 8}, {"blinking2", 9}, {"updateHeightAboveGround", 10},
                {"checkDirection", 11}, {"blinking3", 12},
            };
            for (const auto& item : values)
            {
                lua_pushboolean(luaVM, (flags & (1u << item.bit)) != 0);
                lua_setfield(luaVM, -2, item.name);
            }
        }
        else if (type == e2dEffectType::ROADSIGN)
        {
            const int lineBits = flags & 3;
            const int charBits = (flags >> 2) & 3;
            lua_pushnumber(luaVM, lineBits == 0 ? 4 : lineBits); lua_setfield(luaVM, -2, "lines");
            const int chars = charBits == 0 ? 16 : (charBits == 1 ? 2 : (charBits == 2 ? 4 : 8));
            lua_pushnumber(luaVM, chars); lua_setfield(luaVM, -2, "charactersPerLine");
        }
    }

    bool ReadEffectData(lua_State* luaVM, int tableIndex, e2dEffectType type, S2DFXData& data, const char*& error)
    {
        if (type == e2dEffectType::SUN_GLARE)
            return lua_isnil(luaVM, tableIndex) || lua_istable(luaVM, tableIndex);
        if (!lua_istable(luaVM, tableIndex)) { error = "properties must be a table"; return false; }
        tableIndex = AbsIndex(luaVM, tableIndex);

        if (type == e2dEffectType::LIGHT)
        {
            float shadowMultiplier{}, shadowDistance{}, flareType{};
            std::string showMode;
            if (!ReadNumberField(luaVM, tableIndex, "drawDistance", data.drawDistance) || data.drawDistance < 0.0f) { error = "invalid drawDistance"; return false; }
            if (!ReadNumberField(luaVM, tableIndex, "lightRange", data.lightRange) || data.lightRange < 0.0f) { error = "invalid lightRange"; return false; }
            if (!ReadNumberField(luaVM, tableIndex, "coronaSize", data.coronaSize) || data.coronaSize < 0.0f) { error = "invalid coronaSize"; return false; }
            if (!ReadNumberField(luaVM, tableIndex, "shadowSize", data.shadowSize) || data.shadowSize < 0.0f) { error = "invalid shadowSize"; return false; }
            if (!ReadNumberField(luaVM, tableIndex, "shadowMultiplier", shadowMultiplier) || shadowMultiplier < 0.0f || shadowMultiplier > 255.0f || std::floor(shadowMultiplier) != shadowMultiplier) { error = "invalid shadowMultiplier"; return false; }
            if (!ReadStringField(luaVM, tableIndex, "showMode", showMode) || !StringToFlashType(showMode.c_str(), data.flashType)) { error = "invalid showMode"; return false; }
            if (!ReadBoolField(luaVM, tableIndex, "coronaReflection", data.coronaReflection)) { error = "invalid coronaReflection"; return false; }
            if (!ReadNumberField(luaVM, tableIndex, "flareType", flareType) || flareType < 0.0f || flareType > 1.0f || std::floor(flareType) != flareType) { error = "invalid flareType"; return false; }
            if (!ReadFlagsField(luaVM, tableIndex, "flags", type, data.flags)) { error = "invalid flags"; return false; }
            if (!ReadNumberField(luaVM, tableIndex, "shadowDistance", shadowDistance) || shadowDistance < -128.0f || shadowDistance > 127.0f || std::floor(shadowDistance) != shadowDistance) { error = "invalid shadowDistance"; return false; }
            if (!ReadVectorField(luaVM, tableIndex, "offset", data.offset) || data.offset.fX < -128.0f || data.offset.fX > 127.0f || data.offset.fY < -128.0f || data.offset.fY > 127.0f || data.offset.fZ < -128.0f || data.offset.fZ > 127.0f) { error = "invalid offset"; return false; }
            if (!ReadColorField(luaVM, tableIndex, "color", data.color)) { error = "invalid color"; return false; }
            if (!ReadStringField(luaVM, tableIndex, "coronaName", data.coronaName) || data.coronaName.empty()) { error = "invalid coronaName"; return false; }
            if (!ReadStringField(luaVM, tableIndex, "shadowName", data.shadowName) || data.shadowName.empty()) { error = "invalid shadowName"; return false; }
            data.shadowMultiplier = static_cast<std::uint8_t>(shadowMultiplier);
            data.shadowDistance = static_cast<std::int8_t>(shadowDistance);
            data.flareType = static_cast<std::uint8_t>(flareType);
            return true;
        }
        if (type == e2dEffectType::PARTICLE)
        {
            if (!ReadStringField(luaVM, tableIndex, "name", data.particleName) || data.particleName.empty() || data.particleName.size() >= 24) { error = "particle name must contain 1-23 characters"; return false; }
            return true;
        }
        if (type == e2dEffectType::ROADSIGN)
        {
            if (!ReadVectorField(luaVM, tableIndex, "size", data.size, 2) || data.size.fX <= 0.0f || data.size.fY <= 0.0f) { error = "invalid size"; return false; }
            if (!ReadVectorField(luaVM, tableIndex, "rotation", data.rotation)) { error = "invalid rotation"; return false; }
            if (!ReadFlagsField(luaVM, tableIndex, "flags", type, data.flags)) { error = "invalid flags"; return false; }
            if (!ReadColorField(luaVM, tableIndex, "color", data.color)) { error = "invalid color"; return false; }
            for (std::size_t line = 0; line < 4; ++line)
            {
                const char* keys[]{"text1", "text2", "text3", "text4"};
                if (!ReadStringField(luaVM, tableIndex, keys[line], data.text[line]) || data.text[line].size() > 16) { error = "roadsign lines may contain at most 16 characters"; return false; }
            }
            return true;
        }
        if (type == e2dEffectType::ESCALATOR)
        {
            float direction{};
            if (!ReadVectorField(luaVM, tableIndex, "bottom", data.bottom)) { error = "invalid bottom"; return false; }
            if (!ReadVectorField(luaVM, tableIndex, "top", data.top)) { error = "invalid top"; return false; }
            if (!ReadVectorField(luaVM, tableIndex, "end", data.end)) { error = "invalid end"; return false; }
            if (!ReadNumberField(luaVM, tableIndex, "direction", direction) || (direction != 0.0f && direction != 1.0f)) { error = "direction must be 0 or 1"; return false; }
            data.direction = static_cast<std::uint8_t>(direction);
            return true;
        }
        error = "unsupported effect type";
        return false;
    }

    bool ReadPropertyValue(lua_State* luaVM, int index, e2dEffectType type, e2dEffectProperty property, S2DFXData& data)
    {
        float number{};
        switch (property)
        {
            case e2dEffectProperty::FAR_CLIP_DISTANCE:
            case e2dEffectProperty::LIGHT_RANGE:
            case e2dEffectProperty::CORONA_SIZE:
            case e2dEffectProperty::SHADOW_SIZE:
                if (!lua_isnumber(luaVM, index)) return false;
                number = static_cast<float>(lua_tonumber(luaVM, index));
                if (!IsFinite(number) || number < 0.0f) return false;
                if (property == e2dEffectProperty::FAR_CLIP_DISTANCE) data.drawDistance = number;
                else if (property == e2dEffectProperty::LIGHT_RANGE) data.lightRange = number;
                else if (property == e2dEffectProperty::CORONA_SIZE) data.coronaSize = number;
                else data.shadowSize = number;
                return true;
            case e2dEffectProperty::SHADOW_MULT:
                if (!lua_isnumber(luaVM, index)) return false;
                number = static_cast<float>(lua_tonumber(luaVM, index));
                if (!IsFinite(number) || number < 0.0f || number > 255.0f || std::floor(number) != number) return false;
                data.shadowMultiplier = static_cast<std::uint8_t>(number); return true;
            case e2dEffectProperty::FLASH_TYPE:
                return lua_type(luaVM, index) == LUA_TSTRING && StringToFlashType(lua_tostring(luaVM, index), data.flashType);
            case e2dEffectProperty::CORONA_REFLECTION:
                if (!lua_isboolean(luaVM, index)) return false;
                data.coronaReflection = lua_toboolean(luaVM, index) != 0; return true;
            case e2dEffectProperty::FLARE_TYPE:
                if (!lua_isnumber(luaVM, index)) return false;
                number = static_cast<float>(lua_tonumber(luaVM, index));
                if (number != 0.0f && number != 1.0f) return false;
                data.flareType = static_cast<std::uint8_t>(number); return true;
            case e2dEffectProperty::SHADOW_DISTANCE:
                if (!lua_isnumber(luaVM, index)) return false;
                number = static_cast<float>(lua_tonumber(luaVM, index));
                if (!IsFinite(number) || number < -128.0f || number > 127.0f || std::floor(number) != number) return false;
                data.shadowDistance = static_cast<std::int8_t>(number); return true;
            case e2dEffectProperty::OFFSET:
                if (!ReadVector(luaVM, index, data.offset) || data.offset.fX < -128.0f || data.offset.fX > 127.0f || data.offset.fY < -128.0f || data.offset.fY > 127.0f || data.offset.fZ < -128.0f || data.offset.fZ > 127.0f) return false;
                return true;
            case e2dEffectProperty::COLOR:
                return ReadPackedColor(luaVM, index, data.color);
            case e2dEffectProperty::CORONA_NAME:
                if (lua_type(luaVM, index) != LUA_TSTRING) return false;
                data.coronaName = lua_tostring(luaVM, index); return !data.coronaName.empty();
            case e2dEffectProperty::SHADOW_NAME:
                if (lua_type(luaVM, index) != LUA_TSTRING) return false;
                data.shadowName = lua_tostring(luaVM, index); return !data.shadowName.empty();
            case e2dEffectProperty::FLAGS:
                return ReadFlags(luaVM, index, type, data.flags);
            case e2dEffectProperty::PARTICLE_NAME:
                if (lua_type(luaVM, index) != LUA_TSTRING) return false;
                data.particleName = lua_tostring(luaVM, index); return !data.particleName.empty() && data.particleName.size() < 24;
            case e2dEffectProperty::SIZE:
                return ReadVector(luaVM, index, data.size, 2) && data.size.fX > 0.0f && data.size.fY > 0.0f;
            case e2dEffectProperty::ROTATION:
                return ReadVector(luaVM, index, data.rotation);
            case e2dEffectProperty::TEXT_1:
            case e2dEffectProperty::TEXT_2:
            case e2dEffectProperty::TEXT_3:
            case e2dEffectProperty::TEXT_4:
            {
                if (lua_type(luaVM, index) != LUA_TSTRING) return false;
                const std::size_t line = static_cast<std::size_t>(property) - static_cast<std::size_t>(e2dEffectProperty::TEXT_1);
                data.text[line] = lua_tostring(luaVM, index); return data.text[line].size() <= 16;
            }
            case e2dEffectProperty::BOTTOM: return ReadVector(luaVM, index, data.bottom);
            case e2dEffectProperty::TOP: return ReadVector(luaVM, index, data.top);
            case e2dEffectProperty::END: return ReadVector(luaVM, index, data.end);
            case e2dEffectProperty::DIRECTION:
                if (!lua_isnumber(luaVM, index)) return false;
                number = static_cast<float>(lua_tonumber(luaVM, index));
                if (number != 0.0f && number != 1.0f) return false;
                data.direction = static_cast<std::uint8_t>(number); return true;
            default: return false;
        }
    }

    void PushProperties(lua_State* luaVM, e2dEffectType type, const S2DFXData& data)
    {
        lua_newtable(luaVM);
        if (type == e2dEffectType::LIGHT)
        {
            lua_pushnumber(luaVM, data.drawDistance); lua_setfield(luaVM, -2, "drawDistance");
            lua_pushnumber(luaVM, data.lightRange); lua_setfield(luaVM, -2, "lightRange");
            lua_pushnumber(luaVM, data.coronaSize); lua_setfield(luaVM, -2, "coronaSize");
            lua_pushnumber(luaVM, data.shadowSize); lua_setfield(luaVM, -2, "shadowSize");
            lua_pushnumber(luaVM, data.shadowMultiplier); lua_setfield(luaVM, -2, "shadowMultiplier");
            lua_pushstring(luaVM, FlashTypeToString(data.flashType)); lua_setfield(luaVM, -2, "showMode");
            lua_pushboolean(luaVM, data.coronaReflection); lua_setfield(luaVM, -2, "coronaReflection");
            lua_pushnumber(luaVM, data.flareType); lua_setfield(luaVM, -2, "flareType");
            lua_pushnumber(luaVM, data.flags); lua_setfield(luaVM, -2, "flags");
            lua_pushnumber(luaVM, data.shadowDistance); lua_setfield(luaVM, -2, "shadowDistance");
            PushVector(luaVM, data.offset); lua_setfield(luaVM, -2, "offset");
            PushColor(luaVM, data.color); lua_setfield(luaVM, -2, "color");
            lua_pushstring(luaVM, data.coronaName.c_str()); lua_setfield(luaVM, -2, "coronaName");
            lua_pushstring(luaVM, data.shadowName.c_str()); lua_setfield(luaVM, -2, "shadowName");
        }
        else if (type == e2dEffectType::PARTICLE)
        {
            lua_pushstring(luaVM, data.particleName.c_str()); lua_setfield(luaVM, -2, "name");
        }
        else if (type == e2dEffectType::ROADSIGN)
        {
            PushVector(luaVM, data.size, 2); lua_setfield(luaVM, -2, "size");
            PushVector(luaVM, data.rotation); lua_setfield(luaVM, -2, "rotation");
            lua_pushnumber(luaVM, data.flags); lua_setfield(luaVM, -2, "flags");
            PushColor(luaVM, data.color); lua_setfield(luaVM, -2, "color");
            const char* keys[]{"text1", "text2", "text3", "text4"};
            for (std::size_t line = 0; line < 4; ++line) { lua_pushstring(luaVM, data.text[line].c_str()); lua_setfield(luaVM, -2, keys[line]); }
        }
        else if (type == e2dEffectType::ESCALATOR)
        {
            PushVector(luaVM, data.bottom); lua_setfield(luaVM, -2, "bottom");
            PushVector(luaVM, data.top); lua_setfield(luaVM, -2, "top");
            PushVector(luaVM, data.end); lua_setfield(luaVM, -2, "end");
            lua_pushnumber(luaVM, data.direction); lua_setfield(luaVM, -2, "direction");
        }
    }

    bool ReadModelIndex(lua_State* luaVM, int modelArg, int indexArg, std::uint32_t& model, std::uint32_t& index)
    {
        if (!lua_isnumber(luaVM, modelArg) || !lua_isnumber(luaVM, indexArg))
            return false;
        const double modelValue = lua_tonumber(luaVM, modelArg);
        const double indexValue = lua_tonumber(luaVM, indexArg);
        if (!std::isfinite(modelValue) || !std::isfinite(indexValue) || modelValue < 0.0 || modelValue > 65535.0 || indexValue < 0.0 ||
            indexValue > static_cast<double>(std::numeric_limits<std::uint32_t>::max()) || std::floor(modelValue) != modelValue || std::floor(indexValue) != indexValue)
            return false;
        model = static_cast<std::uint32_t>(modelValue);
        index = static_cast<std::uint32_t>(indexValue);
        return true;
    }
}

void CLua2DFXDefs::LoadFunctions()
{
    constexpr static const std::pair<const char*, lua_CFunction> functions[]{
        {"addModel2DFX", AddModel2DFX}, {"removeModel2DFX", RemoveModel2DFX}, {"restoreModel2DFX", RestoreModel2DFX},
        {"resetModel2DFXEffects", ResetModel2DFXEffects}, {"setModel2DFXPosition", SetModel2DFXPosition},
        {"setModel2DFXProperty", SetModel2DFXProperty}, {"resetModel2DFXProperty", ResetModel2DFXProperty},
        {"resetModel2DFXPosition", ResetModel2DFXPosition}, {"getModel2DFXPosition", GetModel2DFXPosition},
        {"getModel2DFXProperty", GetModel2DFXProperty}, {"getModel2DFXEffects", GetModel2DFXEffects},
        {"getModel2DFXCount", GetModel2DFXCount}, {"getModel2DFXType", GetModel2DFXType},
    };
    for (const auto& [name, function] : functions)
        CLuaCFunctions::AddFunction(name, function);
}

int CLua2DFXDefs::AddModel2DFX(lua_State* luaVM)
{
    const char* function = "addModel2DFX";
    if (lua_gettop(luaVM) < 6 || !lua_isnumber(luaVM, 1) || !lua_isnumber(luaVM, 2) || !lua_isnumber(luaVM, 3) || !lua_isnumber(luaVM, 4) ||
        lua_type(luaVM, 5) != LUA_TSTRING)
    {
        LogError(luaVM, function, "expected model, x, y, z, type, properties"); lua_pushboolean(luaVM, false); return 1;
    }
    const double modelValue = lua_tonumber(luaVM, 1);
    CVector position(static_cast<float>(lua_tonumber(luaVM, 2)), static_cast<float>(lua_tonumber(luaVM, 3)), static_cast<float>(lua_tonumber(luaVM, 4)));
    e2dEffectType type{};
    if (!std::isfinite(modelValue) || modelValue < 0.0 || modelValue > 65535.0 || std::floor(modelValue) != modelValue || !IsFinite(position) ||
        !StringToEffectType(lua_tostring(luaVM, 5), type))
    {
        LogError(luaVM, function, "invalid model, position or effect type"); lua_pushboolean(luaVM, false); return 1;
    }
    S2DFXData data{};
    data.position = position;
    const char* error = nullptr;
    if (!ReadEffectData(luaVM, 6, type, data, error))
    {
        LogError(luaVM, function, error ? error : "invalid properties"); lua_pushboolean(luaVM, false); return 1;
    }
    CResource* owner = Owner(luaVM);
    const bool result = owner && Manager().Add(owner, static_cast<std::uint32_t>(modelValue), type, data);
    lua_pushboolean(luaVM, result);
    return 1;
}

int CLua2DFXDefs::RemoveModel2DFX(lua_State* luaVM)
{
    std::uint32_t model{}, index{};
    CResource* owner = Owner(luaVM);
    const bool result = owner && ReadModelIndex(luaVM, 1, 2, model, index) && Manager().Remove(owner, model, index);
    lua_pushboolean(luaVM, result);
    return 1;
}

int CLua2DFXDefs::RestoreModel2DFX(lua_State* luaVM)
{
    std::uint32_t model{}, index{};
    CResource* owner = Owner(luaVM);
    if (owner && ReadModelIndex(luaVM, 1, 2, model, index))
        Manager().Restore(owner, model, index);
    return 0;
}

int CLua2DFXDefs::ResetModel2DFXEffects(lua_State* luaVM)
{
    if (!lua_isnumber(luaVM, 1)) return 0;
    const double value = lua_tonumber(luaVM, 1);
    CResource* owner = Owner(luaVM);
    if (owner && std::isfinite(value) && value >= 0.0 && value <= 65535.0 && std::floor(value) == value)
        Manager().ResetModel(owner, static_cast<std::uint32_t>(value));
    return 0;
}

int CLua2DFXDefs::SetModel2DFXPosition(lua_State* luaVM)
{
    std::uint32_t model{}, index{};
    if (!ReadModelIndex(luaVM, 1, 2, model, index) || !lua_isnumber(luaVM, 3) || !lua_isnumber(luaVM, 4) || !lua_isnumber(luaVM, 5))
        return 0;
    S2DFXData value{};
    value.position = CVector(static_cast<float>(lua_tonumber(luaVM, 3)), static_cast<float>(lua_tonumber(luaVM, 4)), static_cast<float>(lua_tonumber(luaVM, 5)));
    CResource* owner = Owner(luaVM);
    if (owner && IsFinite(value.position))
        Manager().SetProperty(owner, model, index, e2dEffectProperty::POSITION, value);
    return 0;
}

int CLua2DFXDefs::SetModel2DFXProperty(lua_State* luaVM)
{
    std::uint32_t model{}, index{};
    e2dEffectProperty property{};
    if (!ReadModelIndex(luaVM, 1, 2, model, index) || lua_type(luaVM, 3) != LUA_TSTRING || !StringToProperty(lua_tostring(luaVM, 3), property))
    {
        lua_pushboolean(luaVM, false); return 1;
    }
    e2dEffectType type = Manager().GetType(model, index);
    S2DFXData value{};
    if (!CClient2DFXManager::IsPropertyValid(type, property) || !ReadPropertyValue(luaVM, 4, type, property, value))
    {
        lua_pushboolean(luaVM, false); return 1;
    }
    CResource* owner = Owner(luaVM);
    lua_pushboolean(luaVM, owner && Manager().SetProperty(owner, model, index, property, value));
    return 1;
}

int CLua2DFXDefs::ResetModel2DFXProperty(lua_State* luaVM)
{
    std::uint32_t model{}, index{};
    e2dEffectProperty property{};
    CResource* owner = Owner(luaVM);
    if (owner && ReadModelIndex(luaVM, 1, 2, model, index) && lua_type(luaVM, 3) == LUA_TSTRING && StringToProperty(lua_tostring(luaVM, 3), property))
        Manager().ResetProperty(owner, model, index, property);
    return 0;
}

int CLua2DFXDefs::ResetModel2DFXPosition(lua_State* luaVM)
{
    std::uint32_t model{}, index{};
    CResource* owner = Owner(luaVM);
    if (owner && ReadModelIndex(luaVM, 1, 2, model, index))
        Manager().ResetProperty(owner, model, index, e2dEffectProperty::POSITION);
    return 0;
}

int CLua2DFXDefs::GetModel2DFXPosition(lua_State* luaVM)
{
    std::uint32_t model{}, index{};
    S2DFXData data{};
    if (!ReadModelIndex(luaVM, 1, 2, model, index) || !Manager().GetData(model, index, data))
    {
        lua_pushboolean(luaVM, false); return 1;
    }
    lua_pushnumber(luaVM, data.position.fX); lua_pushnumber(luaVM, data.position.fY); lua_pushnumber(luaVM, data.position.fZ);
    return 3;
}

int CLua2DFXDefs::GetModel2DFXProperty(lua_State* luaVM)
{
    std::uint32_t model{}, index{};
    e2dEffectProperty property{};
    S2DFXData data{};
    if (!ReadModelIndex(luaVM, 1, 2, model, index) || lua_type(luaVM, 3) != LUA_TSTRING || !StringToProperty(lua_tostring(luaVM, 3), property) ||
        !Manager().GetData(model, index, data) || !CClient2DFXManager::IsPropertyValid(Manager().GetType(model, index), property))
    {
        lua_pushboolean(luaVM, false); return 1;
    }
    switch (property)
    {
        case e2dEffectProperty::FAR_CLIP_DISTANCE: lua_pushnumber(luaVM, data.drawDistance); return 1;
        case e2dEffectProperty::LIGHT_RANGE: lua_pushnumber(luaVM, data.lightRange); return 1;
        case e2dEffectProperty::CORONA_SIZE: lua_pushnumber(luaVM, data.coronaSize); return 1;
        case e2dEffectProperty::SHADOW_SIZE: lua_pushnumber(luaVM, data.shadowSize); return 1;
        case e2dEffectProperty::SHADOW_MULT: lua_pushnumber(luaVM, data.shadowMultiplier); return 1;
        case e2dEffectProperty::FLASH_TYPE: lua_pushstring(luaVM, FlashTypeToString(data.flashType)); return 1;
        case e2dEffectProperty::CORONA_REFLECTION: lua_pushboolean(luaVM, data.coronaReflection); return 1;
        case e2dEffectProperty::FLARE_TYPE: lua_pushnumber(luaVM, data.flareType); return 1;
        case e2dEffectProperty::SHADOW_DISTANCE: lua_pushnumber(luaVM, data.shadowDistance); return 1;
        case e2dEffectProperty::OFFSET: lua_pushnumber(luaVM, data.offset.fX); lua_pushnumber(luaVM, data.offset.fY); lua_pushnumber(luaVM, data.offset.fZ); return 3;
        case e2dEffectProperty::COLOR:
            lua_pushnumber(luaVM, (data.color >> 16) & 0xFF); lua_pushnumber(luaVM, (data.color >> 8) & 0xFF); lua_pushnumber(luaVM, data.color & 0xFF); lua_pushnumber(luaVM, (data.color >> 24) & 0xFF); return 4;
        case e2dEffectProperty::CORONA_NAME: lua_pushstring(luaVM, data.coronaName.c_str()); return 1;
        case e2dEffectProperty::SHADOW_NAME: lua_pushstring(luaVM, data.shadowName.c_str()); return 1;
        case e2dEffectProperty::FLAGS:
            if (lua_toboolean(luaVM, 4)) PushFlags(luaVM, Manager().GetType(model, index), data.flags); else lua_pushnumber(luaVM, data.flags);
            return 1;
        case e2dEffectProperty::PARTICLE_NAME: lua_pushstring(luaVM, data.particleName.c_str()); return 1;
        case e2dEffectProperty::SIZE: lua_pushnumber(luaVM, data.size.fX); lua_pushnumber(luaVM, data.size.fY); return 2;
        case e2dEffectProperty::ROTATION: lua_pushnumber(luaVM, data.rotation.fX); lua_pushnumber(luaVM, data.rotation.fY); lua_pushnumber(luaVM, data.rotation.fZ); return 3;
        case e2dEffectProperty::TEXT_1: lua_pushstring(luaVM, data.text[0].c_str()); return 1;
        case e2dEffectProperty::TEXT_2: lua_pushstring(luaVM, data.text[1].c_str()); return 1;
        case e2dEffectProperty::TEXT_3: lua_pushstring(luaVM, data.text[2].c_str()); return 1;
        case e2dEffectProperty::TEXT_4: lua_pushstring(luaVM, data.text[3].c_str()); return 1;
        case e2dEffectProperty::BOTTOM: lua_pushnumber(luaVM, data.bottom.fX); lua_pushnumber(luaVM, data.bottom.fY); lua_pushnumber(luaVM, data.bottom.fZ); return 3;
        case e2dEffectProperty::TOP: lua_pushnumber(luaVM, data.top.fX); lua_pushnumber(luaVM, data.top.fY); lua_pushnumber(luaVM, data.top.fZ); return 3;
        case e2dEffectProperty::END: lua_pushnumber(luaVM, data.end.fX); lua_pushnumber(luaVM, data.end.fY); lua_pushnumber(luaVM, data.end.fZ); return 3;
        case e2dEffectProperty::DIRECTION: lua_pushnumber(luaVM, data.direction); return 1;
        default: lua_pushboolean(luaVM, false); return 1;
    }
}

int CLua2DFXDefs::GetModel2DFXEffects(lua_State* luaVM)
{
    if (!lua_isnumber(luaVM, 1)) { lua_pushboolean(luaVM, false); return 1; }
    const double modelValue = lua_tonumber(luaVM, 1);
    if (!std::isfinite(modelValue) || modelValue < 0.0 || modelValue > 65535.0 || std::floor(modelValue) != modelValue) { lua_pushboolean(luaVM, false); return 1; }
    const std::uint32_t model = static_cast<std::uint32_t>(modelValue);
    const bool includeCustom = lua_gettop(luaVM) < 2 || lua_toboolean(luaVM, 2) != 0;
    if (!Manager().IsValidModel(model)) { lua_pushboolean(luaVM, false); return 1; }

    lua_newtable(luaVM);
    const std::uint32_t count = Manager().GetCount(model, includeCustom);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        lua_pushnumber(luaVM, index);
        lua_newtable(luaVM);
        const e2dEffectType type = Manager().GetType(model, index);
        S2DFXData data{};
        const bool available = Manager().GetData(model, index, data);
        if (available) PushVector(luaVM, data.position); else PushVector(luaVM, CVector());
        lua_setfield(luaVM, -2, "position");
        lua_pushstring(luaVM, EffectTypeToString(type)); lua_setfield(luaVM, -2, "type");
        if (available) PushProperties(luaVM, type, data); else lua_newtable(luaVM);
        lua_setfield(luaVM, -2, "properties");
        lua_settable(luaVM, -3);
    }
    return 1;
}

int CLua2DFXDefs::GetModel2DFXCount(lua_State* luaVM)
{
    if (!lua_isnumber(luaVM, 1)) { lua_pushnumber(luaVM, 0); return 1; }
    const double modelValue = lua_tonumber(luaVM, 1);
    if (!std::isfinite(modelValue) || modelValue < 0.0 || modelValue > 65535.0 || std::floor(modelValue) != modelValue) { lua_pushnumber(luaVM, 0); return 1; }
    const bool includeCustom = lua_gettop(luaVM) < 2 || lua_toboolean(luaVM, 2) != 0;
    lua_pushnumber(luaVM, Manager().GetCount(static_cast<std::uint32_t>(modelValue), includeCustom));
    return 1;
}

int CLua2DFXDefs::GetModel2DFXType(lua_State* luaVM)
{
    std::uint32_t model{}, index{};
    if (!ReadModelIndex(luaVM, 1, 2, model, index)) { lua_pushboolean(luaVM, false); return 1; }
    const e2dEffectType type = Manager().GetType(model, index);
    if (type == e2dEffectType::NONE) { lua_pushboolean(luaVM, false); return 1; }
    lua_pushstring(luaVM, EffectTypeToString(type));
    return 1;
}
