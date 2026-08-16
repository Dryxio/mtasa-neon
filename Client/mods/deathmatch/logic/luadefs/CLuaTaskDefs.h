/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *               (Shared logic for modifications)
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/shared_logic/luadefs/CLuaTaskDefs.h
 *  PURPOSE:     Lua task definitions class header
 *
 *****************************************************************************/

#pragma once

#include "CLuaDefs.h"
#include "lua/LuaCommon.h"
#include <optional>
#include <string>
#include <variant>

class CClientPed;
class CClientVehicle;

class CLuaTaskDefs : public CLuaDefs
{
public:
    static void LoadFunctions();

    // New native story tasks live on the public task surface rather than in a
    // mission-specific resource. INTRO1 is the first consumer of escort_left.
    static bool SetPedDriveMission(CClientPed* ped, CClientVehicle* vehicle, CClientVehicle* targetVehicle, std::string mission, float speed,
                                   std::optional<std::string> drivingStyle);
    static bool SetVehicleStraightLineDistance(CClientVehicle* vehicle, unsigned int distance);
    static std::variant<bool, unsigned int> GetVehicleStraightLineDistance(CClientVehicle* vehicle);
    static bool SetPedPhysicalProofs(CClientPed* ped, bool bullet, bool fire, bool explosion, bool collision, bool melee);

    static int createTaskInstance(lua_State* luaVM);

    static int getTaskName(lua_State* luaVM);

    static int getTaskParameter(lua_State* luaVM);
    static int getTaskParameters(lua_State* luaVM);
    static int setTaskParameters(lua_State* luaVM);
    static int clearTaskParameters(lua_State* luaVM);

    static int runTaskFunction(lua_State* luaVM);

    // Should be in player defs
    static int getPlayerTaskInstance(lua_State* luaVM);
    static int setPlayerTask(lua_State* luaVM);
};
