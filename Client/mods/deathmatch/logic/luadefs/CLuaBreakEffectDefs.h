/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.x
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/mods/deathmatch/logic/luadefs/CLuaBreakEffectDefs.h
 *  PURPOSE:     Lua definitions for generic managed object fracture effects
 *
 *****************************************************************************/

#pragma once

extern "C"
{
    #include "lua.h"
}

class CLuaBreakEffectDefs
{
public:
    static void LoadFunctions();
    static void AddClass(lua_State* luaVM);

    static int CreateObjectBreakEffect(lua_State* luaVM);
    static int GetBreakEffectFragmentCount(lua_State* luaVM);
    static int GetBreakEffectSourceTriangleCount(lua_State* luaVM);
    static int GetBreakEffectSleepingFragmentCount(lua_State* luaVM);
    static int GetBreakEffectCacheHit(lua_State* luaVM);
    static int IsBreakEffectPaused(lua_State* luaVM);
    static int SetBreakEffectPaused(lua_State* luaVM);
    static int GetBreakEffectCacheSize(lua_State* luaVM);
    static int ClearBreakEffectCache(lua_State* luaVM);
};
