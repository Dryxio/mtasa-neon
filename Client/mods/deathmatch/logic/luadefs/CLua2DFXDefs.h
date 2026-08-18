/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.x / Neon
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/mods/deathmatch/logic/luadefs/CLua2DFXDefs.h
 *
 *****************************************************************************/
#pragma once

#include "CLuaDefs.h"

class CLua2DFXDefs : public CLuaDefs
{
public:
    static void LoadFunctions();

    LUA_DECLARE(AddModel2DFX);
    LUA_DECLARE(RemoveModel2DFX);
    LUA_DECLARE(RestoreModel2DFX);
    LUA_DECLARE(ResetModel2DFXEffects);
    LUA_DECLARE(SetModel2DFXPosition);
    LUA_DECLARE(SetModel2DFXProperty);
    LUA_DECLARE(ResetModel2DFXProperty);
    LUA_DECLARE(ResetModel2DFXPosition);
    LUA_DECLARE(GetModel2DFXPosition);
    LUA_DECLARE(GetModel2DFXProperty);
    LUA_DECLARE(GetModel2DFXEffects);
    LUA_DECLARE(GetModel2DFXCount);
    LUA_DECLARE(GetModel2DFXType);
};
