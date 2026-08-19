/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.x
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/mods/deathmatch/logic/luadefs/CLuaBreakEffectDefs.h
 *  PURPOSE:     Lua definitions for generic managed object fracture effects
 *
 *****************************************************************************/

#pragma once

#include "CLuaDefs.h"

#ifndef lua_absindex
#define lua_absindex(L, i) ((i) > 0 || (i) <= LUA_REGISTRYINDEX ? (i) : lua_gettop(L) + (i) + 1)
#endif

class CEntitySAInterface;
class CVector;

class CLuaBreakEffectDefs : public CLuaDefs
{
public:
    static void LoadFunctions();
    static void AddClass(lua_State* luaVM);
    static void HandleExplosionDamage(const CVector& position, int explosionType, CEntitySAInterface* attackerInterface);

    LUA_DECLARE(CreateObjectBreakEffect);
    LUA_DECLARE(GetBreakEffectFragmentCount);
    LUA_DECLARE(GetBreakEffectSourceTriangleCount);
    LUA_DECLARE(GetBreakEffectSleepingFragmentCount);
    LUA_DECLARE(GetBreakEffectCacheHit);
    LUA_DECLARE(IsBreakEffectPaused);
    LUA_DECLARE(SetBreakEffectPaused);
    LUA_DECLARE(GetBreakEffectCacheSize);
    LUA_DECLARE(ClearBreakEffectCache);

    LUA_DECLARE(SetObjectBreakProfile);
    LUA_DECLARE(GetObjectBreakProfile);
    LUA_DECLARE(GetObjectBreakHealth);
    LUA_DECLARE(SetObjectBreakHealth);
    LUA_DECLARE(ResetObjectBreakHealth);
    LUA_DECLARE(ClearObjectBreakProfile);
};