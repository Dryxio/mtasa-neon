/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *               (Shared logic for modifications)
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/shared_logic/luadefs/CLuaTaskDefs.cpp
 *  PURPOSE:     Lua task definitions class
 *
 *****************************************************************************/

#include <StdInc.h>
#include "CDeathmatchVehicle.h"
#include "lua/CLuaFunctionParser.h"
#include <game/CTasks.h>
#include <game/TaskCar.h>

namespace
{
    constexpr int CAR_MISSION_ESCORT_LEFT = 29;

    bool OwnsDrivenVehicle(CClientPed* ped, CClientVehicle* vehicle)
    {
        auto* deathmatchVehicle = dynamic_cast<CDeathmatchVehicle*>(vehicle);
        return vehicle && (vehicle->IsLocalEntity() || vehicle->GetOccupant(0) == ped || (deathmatchVehicle && deathmatchVehicle->IsSyncing()));
    }

    bool ParseDrivingStyle(const std::string& name, int& style)
    {
        if (stricmp(name.c_str(), "stop_for_cars") == 0)
            style = DRIVING_STYLE_STOP_FOR_CARS;
        else if (stricmp(name.c_str(), "slow_down_for_cars") == 0)
            style = DRIVING_STYLE_SLOW_DOWN_FOR_CARS;
        else if (stricmp(name.c_str(), "avoid_cars") == 0)
            style = DRIVING_STYLE_AVOID_CARS;
        else if (stricmp(name.c_str(), "plough_through") == 0)
            style = DRIVING_STYLE_PLOUGH_THROUGH;
        else if (stricmp(name.c_str(), "stop_for_cars_ignore_lights") == 0)
            style = DRIVING_STYLE_STOP_FOR_CARS_IGNORE_LIGHTS;
        else if (stricmp(name.c_str(), "avoid_cars_obey_lights") == 0)
            style = DRIVING_STYLE_AVOID_CARS_OBEY_LIGHTS;
        else if (stricmp(name.c_str(), "avoid_cars_stop_for_peds_obey_lights") == 0)
            style = DRIVING_STYLE_AVOID_CARS_STOP_FOR_PEDS_OBEY_LIGHTS;
        else
            return false;
        return true;
    }

    bool DispatchPedScriptCommandTask(CPed* ped, CTask* task)
    {
        if (!task)
            return false;
        if (!g_pGame->GetTasks()->AddPedScriptCommandTask(ped, task))
        {
            task->Destroy();
            return false;
        }
        return true;
    }
}

void CLuaTaskDefs::LoadFunctions()
{
    // The historical task-instance surface stays disabled. New native tasks
    // use explicit validated APIs with ownership and synchronization contracts.
    CLuaCFunctions::AddFunction("setPedDriveMission", ArgumentParser<SetPedDriveMission>);

    // ChrML: Disabled for dp3
    /*
    CLuaCFunctions::AddFunction ( "createTaskInstance", CLuaTaskDefs::createTaskInstance );

    CLuaCFunctions::AddFunction ( "getTaskName", CLuaTaskDefs::getTaskName );

    CLuaCFunctions::AddFunction ( "getTaskParameter", CLuaTaskDefs::getTaskParameter );
    CLuaCFunctions::AddFunction ( "getTaskParameters", CLuaTaskDefs::getTaskParameters );
    CLuaCFunctions::AddFunction ( "setTaskParameters", CLuaTaskDefs::setTaskParameters );
    CLuaCFunctions::AddFunction ( "clearTaskParameters", CLuaTaskDefs::clearTaskParameters );

    CLuaCFunctions::AddFunction ( "runTaskFunction", CLuaTaskDefs::runTaskFunction );

    CLuaCFunctions::AddFunction ( "setPlayerTask", CLuaTaskDefs::setPlayerTask );
    */
}

bool CLuaTaskDefs::SetPedDriveMission(CClientPed* ped, CClientVehicle* vehicle, CClientVehicle* targetVehicle, std::string mission, float speed,
                                      std::optional<std::string> drivingStyle)
{
    if (!ped || !vehicle || !targetVehicle || vehicle == targetVehicle || !ped->IsStreamedIn() || ped->IsDead() || !ped->GetGamePlayer() ||
        (!ped->IsLocalPlayer() && !ped->IsLocalEntity() && !ped->IsSyncing()) || !vehicle->IsStreamedIn() || vehicle->IsBlown() ||
        !vehicle->GetGameVehicle() || !targetVehicle->IsStreamedIn() || targetVehicle->IsBlown() || !targetVehicle->GetGameVehicle() ||
        ped->GetOccupiedVehicle() != vehicle || vehicle->GetOccupant(0) != ped || !OwnsDrivenVehicle(ped, vehicle) || !std::isfinite(speed) || speed < 0.0f ||
        speed >= 255.0f)
    {
        return false;
    }

    // Do not claim the whole eCarMission enum merely because GTA accepts an
    // integer. INTRO1 proves MISSION_ESCORT_LEFT first; later consumers can add
    // individually audited mission modes and target contracts.
    if (stricmp(mission.c_str(), "escort_left") != 0)
        return false;

    int style = DRIVING_STYLE_STOP_FOR_CARS;
    if (!ParseDrivingStyle(drivingStyle.value_or("stop_for_cars"), style))
        return false;

    auto* task = g_pGame->GetTasks()->CreateTaskComplexCarDriveMission(vehicle->GetGameVehicle(), targetVehicle->GetGameVehicle(), CAR_MISSION_ESCORT_LEFT,
                                                                       style, speed);
    return DispatchPedScriptCommandTask(ped->GetGamePlayer(), task);
}

int CLuaTaskDefs::createTaskInstance(lua_State* luaVM)
{
    // Verify the argument
    SString          strTaskName = "";
    CScriptArgReader argStream(luaVM);
    argStream.ReadString(strTaskName);

    if (!argStream.HasErrors() && argStream.NextIsTable())
    {
        // Grab the task name
        CClientTask Task(m_pManager);
        Task.SetTaskName(strTaskName);

        // Generate an unique identifier
        Task.SetUniqueIdentifier(CClientTask::GenerateUniqueIdentifier());

        if (Task.ReadParameters(luaVM, 2, true))
        {
            lua_newtable(luaVM);
            Task.Write(luaVM, -1);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaTaskDefs::getTaskName(lua_State* luaVM)
{
    CScriptArgReader argStream(luaVM);

    if (argStream.NextIsTable())
    {
        CClientTask Task(m_pManager);
        if (Task.Read(luaVM, 1, true))
        {
            lua_pushstring(luaVM, Task.GetTaskName());
            return 1;
        }
    }

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaTaskDefs::getTaskParameter(lua_State* luaVM)
{
    CScriptArgReader argStream(luaVM);

    if (argStream.NextIsTable() && argStream.NextIsString(1))
    {
        CClientTask Task(m_pManager);
        if (Task.Read(luaVM, 1, true))
        {
            SString strKey = "";
            argStream.ReadString(strKey);
            CLuaArgument* pValue = Task.GetParameter(strKey);
            if (pValue)
            {
                pValue->Push(luaVM);
                return 1;
            }
        }
    }

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaTaskDefs::getTaskParameters(lua_State* luaVM)
{
    CScriptArgReader argStream(luaVM);

    if (argStream.NextIsTable())
    {
        CClientTask Task(m_pManager);
        if (Task.Read(luaVM, 1, true))
        {
            lua_newtable(luaVM);
            Task.WriteParameters(luaVM, -1);
            return 1;
        }
    }

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaTaskDefs::setTaskParameters(lua_State* luaVM)
{
    CScriptArgReader argStream(luaVM);

    if (argStream.NextIsTable() && argStream.NextIsTable(1))
    {
        CClientTask Task(m_pManager);
        if (Task.Read(luaVM, 1, true))
        {
            Task.ReadParameters(luaVM, 2, false);
            Task.Write(luaVM, 1);
            lua_pushboolean(luaVM, true);
            return 1;
        }
    }

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaTaskDefs::clearTaskParameters(lua_State* luaVM)
{
    CScriptArgReader argStream(luaVM);

    if (argStream.NextIsTable())
    {
        CClientTask Task(m_pManager);
        if (Task.Read(luaVM, 1, true))
        {
            Task.ClearParameters();
            Task.Write(luaVM, 1);
            lua_pushboolean(luaVM, true);
            return 1;
        }
    }

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaTaskDefs::runTaskFunction(lua_State* luaVM)
{
    return 0;
}

int CLuaTaskDefs::getPlayerTaskInstance(lua_State* luaVM)
{
    return 0;
}

int CLuaTaskDefs::setPlayerTask(lua_State* luaVM)
{
    // bool setPlayerTask ( ped thePed, taskinstance task )
    CClientEntity*   pEntity = NULL;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pEntity);

    if (argStream.NextIsTable())
    {
        if (pEntity && pEntity->GetType() == CCLIENTPLAYER)
        {
            CClientPlayer* pPlayer = static_cast<CClientPlayer*>(pEntity);
            CClientTask    Task(m_pManager);
            if (Task.Read(luaVM, 2, true))
            {
                bool bSuccess = Task.ApplyTask(*pPlayer);
                lua_pushboolean(luaVM, bSuccess);
                return 1;
            }
        }
    }

    lua_pushboolean(luaVM, false);
    return 1;
}
