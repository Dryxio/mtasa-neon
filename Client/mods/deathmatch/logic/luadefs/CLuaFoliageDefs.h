/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.x
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/mods/deathmatch/logic/luadefs/CLuaFoliageDefs.h
 *  PURPOSE:     Lua custom foliage definitions
 *
 *****************************************************************************/

#pragma once

#include "CLuaDefs.h"

class CLuaFoliageDefs : public CLuaDefs
{
public:
    static void LoadFunctions();
    static void AddClass(lua_State* luaVM);

    LUA_DECLARE(CreateFoliage);
    LUA_DECLARE(GetFoliageSurface);
    LUA_DECLARE(SetFoliageSurface);
    LUA_DECLARE(GetFoliageVertices);
    LUA_DECLARE(SetFoliageVertices);
    LUA_DECLARE(GetFoliageDensity);
    LUA_DECLARE(SetFoliageDensity);
};
