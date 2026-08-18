/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/mods/deathmatch/logic/luadefs/CLuaFireDefs.h
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/
#pragma once

#include "CLuaDefs.h"

class CLuaFireDefs : public CLuaDefs
{
public:
    static void LoadFunctions();

    LUA_DECLARE(CreateFire);
    LUA_DECLARE(ExtinguishFire);
    LUA_DECLARE(GetFireDuration);
    LUA_DECLARE(SetFireDuration);
    LUA_DECLARE(GetFireRemainingTime);
    LUA_DECLARE(SetFireRemainingTime);
    LUA_DECLARE(GetFireStrength);
    LUA_DECLARE(SetFireStrength);
    LUA_DECLARE(IsFireDamageEnabled);
    LUA_DECLARE(SetFireDamageEnabled);
    LUA_DECLARE(GetFireDamageTargets);
    LUA_DECLARE(SetFireDamageTargets);
    LUA_DECLARE(IsFireSpreadEnabled);
    LUA_DECLARE(SetFireSpreadEnabled);
    LUA_DECLARE(GetFireMaxGenerations);
    LUA_DECLARE(SetFireMaxGenerations);
    LUA_DECLARE(GetFireSource);
    LUA_DECLARE(SetFireSource);
    LUA_DECLARE(GetFireTarget);
    LUA_DECLARE(SetFireTarget);
};
