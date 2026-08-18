/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.x
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/mods/deathmatch/logic/luadefs/CLuaBirdDefs.h
 *  PURPOSE:     Lua managed bird definitions
 *
 *****************************************************************************/

#pragma once

#include "CLuaDefs.h"

#ifndef lua_absindex
#define lua_absindex(L, i) ((i) > 0 || (i) <= LUA_REGISTRYINDEX ? (i) : lua_gettop(L) + (i) + 1)
#endif

class CLuaBirdDefs : public CLuaDefs
{
public:
    static void LoadFunctions();
    static void AddClass(lua_State* luaVM);

    LUA_DECLARE(CreateBird);
    LUA_DECLARE(GetBirdVelocity);
    LUA_DECLARE(SetBirdVelocity);
    LUA_DECLARE(GetBirdTargetVelocity);
    LUA_DECLARE(SetBirdTargetVelocity);
    LUA_DECLARE(GetBirdSize);
    LUA_DECLARE(SetBirdSize);
    LUA_DECLARE(GetBirdColors);
    LUA_DECLARE(SetBirdColors);
    LUA_DECLARE(GetBirdWingBeatTime);
    LUA_DECLARE(SetBirdWingBeatTime);
    LUA_DECLARE(GetBirdRenderDistance);
    LUA_DECLARE(SetBirdRenderDistance);
    LUA_DECLARE(IsBirdCurvedFlightEnabled);
    LUA_DECLARE(SetBirdCurvedFlightEnabled);
    LUA_DECLARE(IsBirdShootable);
    LUA_DECLARE(SetBirdShootable);
    LUA_DECLARE(IsBirdMovementEnabled);
    LUA_DECLARE(SetBirdMovementEnabled);
    LUA_DECLARE(ProcessBirdGunShot);
};
