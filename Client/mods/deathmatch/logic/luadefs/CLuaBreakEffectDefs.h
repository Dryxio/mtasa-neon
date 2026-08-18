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

class CLuaBreakEffectDefs : public CLuaDefs
{
public:
    static void LoadFunctions();
    static void AddClass(lua_State* luaVM);

    LUA_DECLARE(CreateObjectBreakEffect);
    LUA_DECLARE(GetBreakEffectFragmentCount);
    LUA_DECLARE(GetBreakEffectSourceTriangleCount);
    LUA_DECLARE(GetBreakEffectSleepingFragmentCount);
    LUA_DECLARE(GetBreakEffectCacheHit);
    LUA_DECLARE(IsBreakEffectPaused);
    LUA_DECLARE(SetBreakEffectPaused);
    LUA_DECLARE(GetBreakEffectCacheSize);
    LUA_DECLARE(ClearBreakEffectCache);
};
