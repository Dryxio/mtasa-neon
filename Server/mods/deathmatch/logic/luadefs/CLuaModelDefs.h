/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Server/mods/deathmatch/logic/luadefs/CLuaModelDefs.h
 *  PURPOSE:     Server custom model Lua definitions
 *
 *****************************************************************************/

#pragma once

#include "CLuaDefs.h"

class CLuaModelDefs : public CLuaDefs
{
public:
    static void LoadFunctions();

    LUA_DECLARE(EngineRequestModel)
    LUA_DECLARE(EngineFreeModel)
    LUA_DECLARE(EngineGetModelParent)
    LUA_DECLARE(EngineGetModelName)
};
