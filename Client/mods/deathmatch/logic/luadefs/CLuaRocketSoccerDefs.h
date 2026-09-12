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
    static int  Create(lua_State* luaVM);
    static int  Step(lua_State* luaVM);
    static int  Predict(lua_State* luaVM);
    static int  Rebase(lua_State* luaVM);
    static int  Reset(lua_State* luaVM);
    static int  Destroy(lua_State* luaVM);
    static int  Command(lua_State* luaVM);
    static int  Gamepad(lua_State* luaVM);
    static int  Pads(lua_State* luaVM);
    static int  CameraStep(lua_State* luaVM);
    static int  GetArenaMesh(lua_State* luaVM);
};
