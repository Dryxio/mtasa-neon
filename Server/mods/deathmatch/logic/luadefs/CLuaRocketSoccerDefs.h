/*****************************************************************************
 * PROJECT: Multi Theft Auto: Neon
 * LICENSE: See LICENSE in the top level directory
 *****************************************************************************/
#pragma once
#include "CLuaDefs.h"

class CLuaRocketSoccerDefs : public CLuaDefs
{
public:
    static void LoadFunctions();
    static int  Create(lua_State* vm);
    static int  Step(lua_State* vm);
    static int  Checkpoint(lua_State* vm);
    static int  RebaseCheckpoint(lua_State* vm);
    static int  Reset(lua_State* vm);
    static int  Destroy(lua_State* vm);
};
