/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.x
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/mods/deathmatch/logic/luadefs/CLuaBreakEffectDefs.cpp
 *  PURPOSE:     Lua definitions for generic managed object fracture effects
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CLuaBreakEffectDefs.h"
#include "../CClientBreakEffect.h"
#include "../CClientBreakEffectManager.h"
#include "../CClientObject.h"

#include <cmath>

namespace
{
    CClientBreakEffect* ReadBreakEffect(CScriptArgReader& args)
    {
        CClientEntity* entity = nullptr;
        args.ReadUserData(entity);
        if (args.HasErrors() || !entity || entity->GetTypeHash() != CClientEntity::GetTypeHashFromString("break-effect"))
            return nullptr;
        return static_cast<CClientBreakEffect*>(entity);
    }

    bool ReadNumberField(lua_State* luaVM, int tableIndex, const char* name, float& value)
    {
        tableIndex = lua_absindex(luaVM, tableIndex);
        lua_getfield(luaVM, tableIndex, name);
        if (!lua_isnumber(luaVM, -1))
        {
            lua_pop(luaVM, 1);
            return false;
        }
        value = static_cast<float>(lua_tonumber(luaVM, -1));
        lua_pop(luaVM, 1);
        return std::isfinite(value);
    }

    bool ReadIntegerField(lua_State* luaVM, int tableIndex, const char* name, std::uint32_t& value)
    {
        tableIndex = lua_absindex(luaVM, tableIndex);
        lua_getfield(luaVM, tableIndex, name);
        if (!lua_isnumber(luaVM, -1))
        {
            lua_pop(luaVM, 1);
            return false;
        }
        const lua_Number number = lua_tonumber(luaVM, -1);
        lua_pop(luaVM, 1);
        if (!std::isfinite(static_cast<double>(number)) || number < 0.0 || number > 4294967295.0)
            return false;
        value = static_cast<std::uint32_t>(number);
        return true;
    }

    bool ReadBoolField(lua_State* luaVM, int tableIndex, const char* name, bool& value)
    {
        tableIndex = lua_absindex(luaVM, tableIndex);
        lua_getfield(luaVM, tableIndex, name);
        if (!lua_isboolean(luaVM, -1))
        {
            lua_pop(luaVM, 1);
            return false;
        }
        value = lua_toboolean(luaVM, -1) != 0;
        lua_pop(luaVM, 1);
        return true;
    }

    bool ReadVectorField(lua_State* luaVM, int tableIndex, const char* name, CVector& value)
    {
        tableIndex = lua_absindex(luaVM, tableIndex);
        lua_getfield(luaVM, tableIndex, name);
        if (!lua_istable(luaVM, -1))
        {
            lua_pop(luaVM, 1);
            return false;
        }

        auto readComponent = [luaVM](const char* component, int arrayIndex, float& output)
        {
            lua_getfield(luaVM, -1, component);
            if (!lua_isnumber(luaVM, -1))
            {
                lua_pop(luaVM, 1);
                lua_rawgeti(luaVM, -1, arrayIndex);
            }
            if (!lua_isnumber(luaVM, -1))
            {
                lua_pop(luaVM, 1);
                return false;
            }
            output = static_cast<float>(lua_tonumber(luaVM, -1));
            lua_pop(luaVM, 1);
            return std::isfinite(output);
        };

        const bool ok = readComponent("x", 1, value.fX) && readComponent("y", 2, value.fY) && readComponent("z", 3, value.fZ);
        lua_pop(luaVM, 1);
        return ok;
    }

    bool ParseOptions(lua_State* luaVM, int tableIndex, SManagedBreakOptions& options, SString& error)
    {
        if (!lua_istable(luaVM, tableIndex))
            return true;

        std::uint32_t integerValue = 0;
        if (ReadIntegerField(luaVM, tableIndex, "fragments", integerValue))
        {
            if (integerValue < 1 || integerValue > CClientBreakEffectManager::MAX_FRAGMENTS_PER_EFFECT)
            {
                error = "fragments must be between 1 and 64";
                return false;
            }
            options.fragments = integerValue;
        }
        if (ReadIntegerField(luaVM, tableIndex, "lifetime", integerValue))
        {
            if (integerValue < 100 || integerValue > 600000)
            {
                error = "lifetime must be between 100 and 600000 milliseconds";
                return false;
            }
            options.lifetimeMs = integerValue;
        }
        if (ReadIntegerField(luaVM, tableIndex, "seed", integerValue))
            options.seed = integerValue;

        ReadNumberField(luaVM, tableIndex, "force", options.force);
        ReadNumberField(luaVM, tableIndex, "randomness", options.randomness);
        ReadNumberField(luaVM, tableIndex, "gravity", options.gravity);
        ReadNumberField(luaVM, tableIndex, "bounce", options.bounce);
        ReadNumberField(luaVM, tableIndex, "drag", options.drag);
        ReadNumberField(luaVM, tableIndex, "renderDistance", options.renderDistance);
        ReadVectorField(luaVM, tableIndex, "velocity", options.velocity);
        if (ReadVectorField(luaVM, tableIndex, "impactPosition", options.impactPosition))
            options.hasImpactPosition = true;
        ReadBoolField(luaVM, tableIndex, "hideOriginal", options.hideOriginal);
        ReadBoolField(luaVM, tableIndex, "disableOriginalCollision", options.disableOriginalCollision);

        if (options.force < 0.0f || options.randomness < 0.0f || options.gravity < 0.0f || options.bounce < 0.0f || options.bounce > 1.5f ||
            options.drag < 0.0f || options.renderDistance <= 0.0f)
        {
            error = "invalid managed break effect numeric option";
            return false;
        }
        return true;
    }
}

void CLuaBreakEffectDefs::LoadFunctions()
{
    constexpr static const std::pair<const char*, lua_CFunction> functions[]{
        {"createObjectBreakEffect", CreateObjectBreakEffect},
        {"getBreakEffectFragmentCount", GetBreakEffectFragmentCount},
        {"getBreakEffectSourceTriangleCount", GetBreakEffectSourceTriangleCount},
        {"getBreakEffectSleepingFragmentCount", GetBreakEffectSleepingFragmentCount},
        {"getBreakEffectCacheHit", GetBreakEffectCacheHit},
        {"isBreakEffectPaused", IsBreakEffectPaused},
        {"setBreakEffectPaused", SetBreakEffectPaused},
        {"getBreakEffectCacheSize", GetBreakEffectCacheSize},
        {"clearBreakEffectCache", ClearBreakEffectCache},
    };

    for (const auto& [name, function] : functions)
        CLuaCFunctions::AddFunction(name, function);
}

void CLuaBreakEffectDefs::AddClass(lua_State* luaVM)
{
    lua_newclass(luaVM);
    lua_classfunction(luaVM, "create", "createObjectBreakEffect");
    lua_classfunction(luaVM, "getFragmentCount", "getBreakEffectFragmentCount");
    lua_classfunction(luaVM, "getSourceTriangleCount", "getBreakEffectSourceTriangleCount");
    lua_classfunction(luaVM, "getSleepingFragmentCount", "getBreakEffectSleepingFragmentCount");
    lua_classfunction(luaVM, "getCacheHit", "getBreakEffectCacheHit");
    lua_classfunction(luaVM, "isPaused", "isBreakEffectPaused");
    lua_classfunction(luaVM, "setPaused", "setBreakEffectPaused");
    lua_classvariable(luaVM, "paused", "setBreakEffectPaused", "isBreakEffectPaused");
    lua_registerclass(luaVM, "BreakEffect", "Element");
}

int CLuaBreakEffectDefs::CreateObjectBreakEffect(lua_State* luaVM)
{
    CClientObject* object = nullptr;
    CScriptArgReader args(luaVM);
    args.ReadUserData(object);
    if (args.HasErrors() || !object)
    {
        m_pScriptDebugging->LogBadType(luaVM);
        lua_pushboolean(luaVM, false);
        return 1;
    }

    SManagedBreakOptions options;
    SString error;
    if (lua_gettop(luaVM) >= 2 && !lua_isnil(luaVM, 2) && !lua_istable(luaVM, 2))
    {
        m_pScriptDebugging->LogCustom(luaVM, "Expected options table at argument 2");
        lua_pushboolean(luaVM, false);
        return 1;
    }
    if (!ParseOptions(luaVM, 2, options, error))
    {
        m_pScriptDebugging->LogCustom(luaVM, error);
        lua_pushboolean(luaVM, false);
        return 1;
    }

    CLuaMain* luaMain = m_pLuaManager->GetVirtualMachine(luaVM);
    CResource* resource = luaMain ? luaMain->GetResource() : nullptr;
    CClientBreakEffect* effect = resource ? CClientBreakEffectManager::GetSingleton().CreateFromObject(m_pManager, object, INVALID_ELEMENT_ID, options) : nullptr;
    if (!effect)
    {
        m_pScriptDebugging->LogWarning(luaVM, "Unable to fracture object: object must be streamed and contain valid static RenderWare geometry");
        lua_pushboolean(luaVM, false);
        return 1;
    }

    effect->SetParent(resource->GetResourceDynamicEntity());
    if (CElementGroup* group = resource->GetElementGroup())
        group->Add(effect);
    lua_pushelement(luaVM, effect);
    return 1;
}

int CLuaBreakEffectDefs::GetBreakEffectFragmentCount(lua_State* luaVM)
{
    CScriptArgReader args(luaVM);
    CClientBreakEffect* effect = ReadBreakEffect(args);
    if (effect)
        lua_pushnumber(luaVM, static_cast<lua_Number>(effect->GetFragmentCount()));
    else
        lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaBreakEffectDefs::GetBreakEffectSourceTriangleCount(lua_State* luaVM)
{
    CScriptArgReader args(luaVM);
    CClientBreakEffect* effect = ReadBreakEffect(args);
    if (effect)
        lua_pushnumber(luaVM, static_cast<lua_Number>(effect->GetSourceTriangleCount()));
    else
        lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaBreakEffectDefs::GetBreakEffectSleepingFragmentCount(lua_State* luaVM)
{
    CScriptArgReader args(luaVM);
    CClientBreakEffect* effect = ReadBreakEffect(args);
    if (effect)
        lua_pushnumber(luaVM, static_cast<lua_Number>(effect->GetSleepingFragmentCount()));
    else
        lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaBreakEffectDefs::GetBreakEffectCacheHit(lua_State* luaVM)
{
    CScriptArgReader args(luaVM);
    CClientBreakEffect* effect = ReadBreakEffect(args);
    lua_pushboolean(luaVM, effect && effect->WasCacheHit());
    return 1;
}

int CLuaBreakEffectDefs::IsBreakEffectPaused(lua_State* luaVM)
{
    CScriptArgReader args(luaVM);
    CClientBreakEffect* effect = ReadBreakEffect(args);
    lua_pushboolean(luaVM, effect && effect->IsPaused());
    return 1;
}

int CLuaBreakEffectDefs::SetBreakEffectPaused(lua_State* luaVM)
{
    CScriptArgReader args(luaVM);
    CClientBreakEffect* effect = ReadBreakEffect(args);
    bool paused = false;
    args.ReadBool(paused);
    if (!effect || args.HasErrors())
    {
        lua_pushboolean(luaVM, false);
        return 1;
    }
    effect->SetPaused(paused);
    lua_pushboolean(luaVM, true);
    return 1;
}

int CLuaBreakEffectDefs::GetBreakEffectCacheSize(lua_State* luaVM)
{
    (void)luaVM;
    lua_pushnumber(luaVM, static_cast<lua_Number>(CClientBreakEffectManager::GetSingleton().GetCacheEntryCount()));
    return 1;
}

int CLuaBreakEffectDefs::ClearBreakEffectCache(lua_State* luaVM)
{
    CClientBreakEffectManager::GetSingleton().ClearCache();
    lua_pushboolean(luaVM, true);
    return 1;
}
