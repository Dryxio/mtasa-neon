/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/shared_logic/luadefs/CLuaPedDefs.cpp
 *  PURPOSE:     Lua ped definitions class
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include <game/CWeapon.h>
#include "lua/CLuaFunctionParser.h"
#include <game/CTasks.h>
#include <game/TaskAttack.h>
#include <game/TaskBasic.h>
#include <game/TaskCar.h>
#include <game/TaskGoTo.h>
#include <game/CAnimManager.h>
#include "CDeathmatchVehicle.h"
#include "CLuaPedDefs.h"

#define MIN_CLIENT_REQ_REMOVEPEDFROMVEHICLE_CLIENTSIDE "1.3.0-9.04482"
#define MIN_CLIENT_REQ_WARPPEDINTOVEHICLE_CLIENTSIDE   "1.3.0-9.04482"

namespace
{
    enum class ePedSequenceTask
    {
        LEAVE_CAR,
        GO_TO,
        SHOOT_AT,
        DRIVE_TO,
        LEAVE_CAR_IMMEDIATELY,
        SMART_FLEE,
        DIE,
    };

    struct SPedSequenceTask
    {
        ePedSequenceTask type{};
        CClientVehicle*  vehicle{};
        CVector          target{};
        int              moveState{PedMoveState::PEDMOVE_WALK};
        float            radius{0.5f};
        float            slowdownRadius{2.0f};
        int              timeout{-2};
        int              duration{1000};
        int              burstLength{5};
        float            speed{};
        int              driveMode{};
        int              desiredVehicleModel{-1};
        int              drivingStyle{DRIVING_STYLE_STOP_FOR_CARS};
        CClientPed*      pedTarget{};
        float            safeDistance{100.0f};
    };

    bool ParseDrivingStyle(const std::variant<std::string, int>& value, int& style)
    {
        if (std::holds_alternative<int>(value))
        {
            style = std::get<int>(value);
        }
        else
        {
            const std::string& name = std::get<std::string>(value);
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
        }
        return style >= DRIVING_STYLE_STOP_FOR_CARS && style <= DRIVING_STYLE_AVOID_CARS_STOP_FOR_PEDS_OBEY_LIGHTS;
    }

    bool ParseDriveMode(const std::variant<std::string, int>& value, int& mode)
    {
        if (std::holds_alternative<int>(value))
        {
            mode = std::get<int>(value);
        }
        else
        {
            const std::string& name = std::get<std::string>(value);
            if (stricmp(name.c_str(), "normal") == 0)
                mode = 0;
            else if (stricmp(name.c_str(), "accurate") == 0)
                mode = 1;
            else if (stricmp(name.c_str(), "straight_line") == 0)
                mode = 2;
            else if (stricmp(name.c_str(), "racing") == 0)
                mode = 3;
            else
                return false;
        }
        return mode >= 0 && mode <= 3;
    }

    bool ParseDriveByStyle(const std::variant<std::string, int>& value, int& style)
    {
        if (std::holds_alternative<int>(value))
        {
            style = std::get<int>(value);
        }
        else
        {
            const std::string& name = std::get<std::string>(value);
            if (stricmp(name.c_str(), "fixed_lhs") == 0)
                style = 0;
            else if (stricmp(name.c_str(), "fixed_rhs") == 0)
                style = 1;
            else if (stricmp(name.c_str(), "start_from_lhs") == 0)
                style = 2;
            else if (stricmp(name.c_str(), "start_from_rhs") == 0)
                style = 3;
            else if (stricmp(name.c_str(), "ai_side") == 0)
                style = 4;
            else if (stricmp(name.c_str(), "fixed_fwd") == 0)
                style = 5;
            else if (stricmp(name.c_str(), "fixed_back") == 0)
                style = 6;
            else if (stricmp(name.c_str(), "ai_fwd_back") == 0)
                style = 7;
            else if (stricmp(name.c_str(), "ai_all_directions") == 0)
                style = 8;
            else
                return false;
        }
        return style >= 0 && style <= 8;
    }

    bool ReadSequenceChoice(lua_State* luaVM, int tableIndex, const char* name, const char* defaultName, bool driveMode, int& result, SString& error)
    {
        lua_getfield(luaVM, tableIndex, name);
        std::variant<std::string, int> value;
        if (lua_isnoneornil(luaVM, -1))
            value = std::string(defaultName);
        else if (lua_isnumber(luaVM, -1))
        {
            const double numericValue = lua_tonumber(luaVM, -1);
            if (!std::isfinite(numericValue) || numericValue != std::floor(numericValue))
            {
                lua_pop(luaVM, 1);
                error = SString("field '%s' must be an integer or supported name", name);
                return false;
            }
            value = static_cast<int>(numericValue);
        }
        else if (lua_isstring(luaVM, -1))
            value = std::string(lua_tostring(luaVM, -1));
        else
        {
            lua_pop(luaVM, 1);
            error = SString("field '%s' must be an integer or supported name", name);
            return false;
        }
        lua_pop(luaVM, 1);

        const bool valid = driveMode ? ParseDriveMode(value, result) : ParseDrivingStyle(value, result);
        if (!valid)
            error = SString("field '%s' is outside the native range", name);
        return valid;
    }

    bool OwnsDrivenVehicle(CClientPed* ped, CClientVehicle* vehicle)
    {
        auto* deathmatchVehicle = dynamic_cast<CDeathmatchVehicle*>(vehicle);
        return vehicle && (vehicle->IsLocalEntity() || vehicle->GetOccupant(0) == ped || (deathmatchVehicle && deathmatchVehicle->IsSyncing()));
    }

    bool ReadSequenceNumber(lua_State* luaVM, int tableIndex, const char* name, double& value, double defaultValue, bool required, SString& error)
    {
        lua_getfield(luaVM, tableIndex, name);
        if (lua_isnoneornil(luaVM, -1))
        {
            lua_pop(luaVM, 1);
            if (required)
            {
                error = SString("missing numeric field '%s'", name);
                return false;
            }
            value = defaultValue;
            return true;
        }
        if (!lua_isnumber(luaVM, -1))
        {
            lua_pop(luaVM, 1);
            error = SString("field '%s' must be a number", name);
            return false;
        }
        value = lua_tonumber(luaVM, -1);
        lua_pop(luaVM, 1);
        if (!std::isfinite(value))
        {
            error = SString("field '%s' must be finite", name);
            return false;
        }
        return true;
    }

    bool ReadSequenceTarget(lua_State* luaVM, int tableIndex, CVector& target, SString& error)
    {
        double x, y, z;
        if (!ReadSequenceNumber(luaVM, tableIndex, "x", x, 0.0, true, error) || !ReadSequenceNumber(luaVM, tableIndex, "y", y, 0.0, true, error) ||
            !ReadSequenceNumber(luaVM, tableIndex, "z", z, 0.0, true, error))
        {
            return false;
        }
        target = CVector(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
        return true;
    }

    bool DispatchPedScriptCommandTask(CPed* ped, CTask* task)
    {
        if (!task)
            return false;

        // GTA consumes the factory-created task once native scripted dispatch
        // begins. Validation failures leave ownership here and require explicit
        // cleanup; success only means queued, not active.
        if (!g_pGame->GetTasks()->AddPedScriptCommandTask(ped, task))
        {
            task->Destroy();
            return false;
        }
        return true;
    }

    CTaskComplexFacial* GetPedFacialTask(CClientPed* ped, bool requireSubTask)
    {
        // Facial animation is local presentation, so every participant may
        // drive a streamed script ped's controller. Remote player elements are
        // excluded because their facial state belongs to another client.
        if (!ped || !ped->IsStreamedIn() || ped->IsDead() || !ped->GetGamePlayer() || (!ped->IsLocalPlayer() && ped->GetType() != CCLIENTPED))
            return nullptr;

        CTaskManager* taskManager = ped->GetTaskManager();
        CTask*        task = taskManager ? taskManager->GetTaskSecondary(TASK_SECONDARY_FACIAL_COMPLEX) : nullptr;
        if (!task || task->GetTaskType() != TASK_COMPLEX_FACIAL || (requireSubTask && !task->GetSubTask()))
            return nullptr;

        return dynamic_cast<CTaskComplexFacial*>(task);
    }
}  // namespace

void CLuaPedDefs::LoadFunctions()
{
    constexpr static const std::pair<const char*, lua_CFunction> functions[]{
        {"createPed", CreatePed},
        {"detonateSatchels", DetonateSatchels},
        {"killPed", KillPed},
        {"resetPedVoice", ArgumentParser<ResetPedVoice>},
        {"updateElementRpHAnim", ArgumentParser<UpdateElementRpHAnim>},
        {"addPedClothes", AddPedClothes},
        {"removePedClothes", RemovePedClothes},
        {"warpPedIntoVehicle", WarpPedIntoVehicle},
        {"removePedFromVehicle", RemovePedFromVehicle},
        {"givePedWeapon", GivePedWeapon},

        {"setPedVoice", SetPedVoice},
        {"setElementBonePosition", ArgumentParserWarn<false, SetElementBonePosition>},
        {"setElementBoneRotation", ArgumentParserWarn<false, SetElementBoneRotation>},
        {"setElementBoneQuaternion", ArgumentParserWarn<false, SetElementBoneQuaternion>},
        {"setElementBoneMatrix", ArgumentParserWarn<false, SetElementBoneMatrix>},
        {"setPedRotation", SetPedRotation},
        {"setPedWeaponSlot", SetPedWeaponSlot},
        {"setPedCanBeKnockedOffBike", SetPedCanBeKnockedOffBike},
        {"setPedAnimation", SetPedAnimation},
        {"setPedAnimationProgress", SetPedAnimationProgress},
        {"setPedAnimationSpeed", SetPedAnimationSpeed},
        {"setPedWalkingStyle", SetPedMoveAnim},
        {"setPedUseNativeWalkingStyle", SetPedUseNativeWalkingStyle},
        {"setPedControlState", ArgumentParserWarn<false, SetPedControlState>},
        {"setPedAnalogControlState", SetPedAnalogControlState},
        {"setPedDoingGangDriveby", SetPedDoingGangDriveby},
        {"setPedFightingStyle", ArgumentParser<SetPedFightingStyle>},
        {"setPedLookAt", SetPedLookAt},
        {"setPedHeadless", SetPedHeadless},
        {"setPedFrozen", SetPedFrozen},
        {"setPedFootBloodEnabled", SetPedFootBloodEnabled},
        {"setPedCameraRotation", SetPedCameraRotation},
        {"setPedAimTarget", SetPedAimTarget},
        {"setPedStat", SetPedStat},
        {"setPedOxygenLevel", SetPedOxygenLevel},
        {"setPedArmor", ArgumentParser<SetPedArmor>},
        {"setPedEnterVehicle", ArgumentParser<SetPedEnterVehicle>},
        {"setPedExitVehicle", ArgumentParser<SetPedExitVehicle>},
        {"setPedWeaponShootingRate", ArgumentParser<SetPedWeaponShootingRate>},
        {"setPedWeaponAccuracy", ArgumentParser<SetPedWeaponAccuracy>},
        {"setPedGoTo", ArgumentParser<SetPedGoTo>},
        {"setPedChatWith", ArgumentParser<SetPedChatWith>},
        {"setPedStandStill", ArgumentParser<SetPedStandStill>},
        {"setPedTurnToFace", ArgumentParser<SetPedTurnToFace>},
        {"setPedGoToOffset", ArgumentParser<SetPedGoToOffset>},
        {"setPedKillOnFoot", ArgumentParser<SetPedKillOnFoot>},
        {"setPedWander", ArgumentParser<SetPedWander>},
        {"setPedScriptedSpeechMuted", ArgumentParser<SetPedScriptedSpeechMuted>},
        {"setPedFacialTalk", ArgumentParser<SetPedFacialTalk>},
        {"stopPedFacialTalk", ArgumentParser<StopPedFacialTalk>},
        {"setPedShootAt", ArgumentParser<SetPedShootAt>},
        {"setPedTaskSequence", SetPedTaskSequence},
        {"setPedDriveWander", ArgumentParser<SetPedDriveWander>},
        {"setPedDriveTo", ArgumentParser<SetPedDriveTo>},
        {"setPedDriveBy", ArgumentParser<SetPedDriveBy>},
        {"setPedMissionActor", ArgumentParser<SetPedMissionActor>},
        {"setPedStoryProtected", ArgumentParser<SetPedStoryProtected>},
        {"setPedSuffersCriticalHits", ArgumentParser<SetPedSuffersCriticalHits>},
        {"setPedStayInSamePlace", ArgumentParser<SetPedStayInSamePlace>},
        {"setPedNeverTargeted", ArgumentParser<SetPedNeverTargeted>},
        {"setPedBleeding", ArgumentParser<SetPedBleeding>},
        {"playPedVoiceLine", ArgumentParser<PlayPedVoiceLine>},

        {"getPedVoice", GetPedVoice},
        {"getElementBonePosition", ArgumentParserWarn<false, GetElementBonePosition>},
        {"getElementBoneRotation", ArgumentParserWarn<false, GetElementBoneRotation>},
        {"getElementBoneQuaternion", ArgumentParserWarn<false, GetElementBoneQuaternion>},
        {"getElementBoneMatrix", ArgumentParserWarn<false, GetElementBoneMatrix>},
        {"getPedRotation", GetPedRotation},
        {"getPedWeaponSlot", GetPedWeaponSlot},
        {"canPedBeKnockedOffBike", CanPedBeKnockedOffBike},
        {"getPedAnimation", GetPedAnimation},
        {"getPedAnimationProgress", ArgumentParser<GetPedAnimationProgress>},
        {"getPedAnimationSpeed", ArgumentParser<GetPedAnimationSpeed>},
        {"getPedAnimationLength", ArgumentParser<GetPedAnimationLength>},
        {"getPedWalkingStyle", GetPedMoveAnim},
        {"isPedUsingNativeWalkingStyle", IsPedUsingNativeWalkingStyle},
        {"getPedControlState", ArgumentParserWarn<false, GetPedControlState>},
        {"getPedAnalogControlState", GetPedAnalogControlState},
        {"isPedDoingGangDriveby", IsPedDoingGangDriveby},
        {"getPedFightingStyle", GetPedFightingStyle},

        {"isPedHeadless", IsPedHeadless},
        {"isPedFrozen", IsPedFrozen},
        {"isPedFootBloodEnabled", IsPedFootBloodEnabled},
        {"getPedCameraRotation", GetPedCameraRotation},
        {"isPedMissionActor", ArgumentParser<IsPedMissionActor>},
        {"isPedStoryProtected", ArgumentParser<IsPedStoryProtected>},
        {"getPedSuffersCriticalHits", ArgumentParser<GetPedSuffersCriticalHits>},
        {"getPedStayInSamePlace", ArgumentParser<GetPedStayInSamePlace>},
        {"isPedNeverTargeted", ArgumentParser<IsPedNeverTargeted>},

        {"getPedStat", GetPedStat},
        {"getPedOxygenLevel", GetPedOxygenLevel},
        {"getPedArmor", ArgumentParserWarn<false, GetPedArmor>},
        {"isPedBleeding", ArgumentParser<IsPedBleeding>},

        {"getPedContactElement", GetPedContactElement},
        {"getPedTask", GetPedTask},
        {"getPedSimplestTask", GetPedSimplestTask},
        {"getPedTarget", GetPedTarget},
        {"getPedTargetStart", GetPedTargetStart},
        {"getPedTargetEnd", GetPedTargetEnd},
        {"getPedTargetCollision", GetPedTargetCollision},
        {"getPedWeapon", GetPedWeapon},
        {"getPedAmmoInClip", GetPedAmmoInClip},
        {"getPedTotalAmmo", GetPedTotalAmmo},
        {"getPedOccupiedVehicle", GetPedOccupiedVehicle},
        {"getPedOccupiedVehicleSeat", GetPedOccupiedVehicleSeat},
        {"getPedTaskSequenceProgress", ArgumentParser<GetPedTaskSequenceProgress>},
        {"getPedBonePosition", GetPedBonePosition},
        {"getPedClothes", GetPedClothes},
        {"getPedMoveState", GetPedMoveState},

        {"doesPedHaveJetPack", DoesPedHaveJetPack},
        {"isPedInVehicle", IsPedInVehicle},
        {"isPedWearingJetpack", DoesPedHaveJetPack},
        {"isPedOnGround", IsPedOnGround},
        {"isPedDoingTask", IsPedDoingTask},
        {"isPedChoking", IsPedChoking},
        {"isPedDucked", IsPedDucked},
        {"isPedDead", IsPedDead},
        {"isPedReloadingWeapon", ArgumentParserWarn<false, IsPedReloadingWeapon>},
        {"killPedTask", ArgumentParser<killPedTask>},
    };

    // Add functions
    for (const auto& [name, func] : functions)
        CLuaCFunctions::AddFunction(name, func);
}

void CLuaPedDefs::AddClass(lua_State* luaVM)
{
    lua_newclass(luaVM);

    lua_classfunction(luaVM, "create", "createPed");
    lua_classfunction(luaVM, "kill", "killPed");

    lua_classfunction(luaVM, "getBodyPartName", "getBodyPartName");
    lua_classfunction(luaVM, "getClothesTypeName", "getClothesTypeName");
    lua_classfunction(luaVM, "getValidModels", "getValidPedModels");
    lua_classfunction(luaVM, "getTypeIndexFromClothes", "getTypeIndexFromClothes");
    lua_classfunction(luaVM, "getClothesByTypeIndex", "getClothesByTypeIndex");
    lua_classvariable(luaVM, "validModels", NULL, "getValidPedModels");

    lua_classfunction(luaVM, "canBeKnockedOffBike", "canPedBeKnockedOffBike");
    lua_classfunction(luaVM, "doesHaveJetPack", "doesPedHaveJetPack");
    lua_classfunction(luaVM, "isWearingJetpack", "isPedWearingJetpack");  // introduced in 1.5.5-9.13846
    lua_classfunction(luaVM, "getAmmoInClip", "getPedAmmoInClip");
    lua_classfunction(luaVM, "getAnalogControlState", "getPedAnalogControlState");
    lua_classfunction(luaVM, "getAnimation", "getPedAnimation");
    lua_classfunction(luaVM, "getArmor", "getPedArmor");
    lua_classfunction(luaVM, "getFightingStyle", "getPedFightingStyle");
    lua_classfunction(luaVM, "getClothes", "getPedClothes");
    lua_classfunction(luaVM, "addClothes", "addPedClothes");
    lua_classfunction(luaVM, "removeClothes", "removePedClothes");
    lua_classfunction(luaVM, "getContactElement", "getPedContactElement");
    lua_classfunction(luaVM, "getControlState", "getPedControlState");
    lua_classfunction(luaVM, "getMoveState", "getPedMoveState");
    lua_classfunction(luaVM, "getOccupiedVehicle", "getPedOccupiedVehicle");
    lua_classfunction(luaVM, "getOccupiedVehicleSeat", "getPedOccupiedVehicleSeat");
    lua_classfunction(luaVM, "getOxygenLevel", "getPedOxygenLevel");
    lua_classfunction(luaVM, "getStat", "getPedStat");
    lua_classfunction(luaVM, "getTarget", "getPedTarget");
    lua_classfunction(luaVM, "getTargetCollision", OOP_GetPedTargetCollision);
    lua_classfunction(luaVM, "getSimplestTask", "getPedSimplestTask");
    lua_classfunction(luaVM, "getTask", "getPedTask");
    lua_classfunction(luaVM, "getTotalAmmo", "getPedTotalAmmo");
    lua_classfunction(luaVM, "getVoice", "getPedVoice");
    lua_classfunction(luaVM, "resetVoice", "resetPedVoice");
    lua_classfunction(luaVM, "getWeapon", "getPedWeapon");
    lua_classfunction(luaVM, "isChocking", "isPedChoking");
    lua_classfunction(luaVM, "isDoingGangDriveby", "isPedDoingGangDriveby");
    lua_classfunction(luaVM, "isDoingTask", "isPedDoingTask");
    lua_classfunction(luaVM, "isDucked", "isPedDucked");
    lua_classfunction(luaVM, "isHeadless", "isPedHeadless");
    lua_classfunction(luaVM, "isInVehicle", "isPedInVehicle");
    lua_classfunction(luaVM, "isOnFire", "isPedOnFire");
    lua_classfunction(luaVM, "isOnGround", "isPedOnGround");
    lua_classfunction(luaVM, "isTargetingMarkerEnabled", "isPedTargetingMarkerEnabled");
    lua_classfunction(luaVM, "isDead", "isPedDead");
    lua_classfunction(luaVM, "setFootBloodEnabled", "setPedFootBloodEnabled");
    lua_classfunction(luaVM, "getTargetEnd", OOP_GetPedTargetEnd);
    lua_classfunction(luaVM, "getTargetStart", OOP_GetPedTargetStart);
    lua_classfunction(luaVM, "getWeaponMuzzlePosition", "getPedWeaponMuzzlePosition");
    lua_classfunction(luaVM, "getBonePosition", OOP_GetPedBonePosition);
    lua_classfunction(luaVM, "getCameraRotation", "getPedCameraRotation");
    lua_classfunction(luaVM, "getWeaponSlot", "getPedWeaponSlot");
    lua_classfunction(luaVM, "getWalkingStyle", "getPedWalkingStyle");
    lua_classfunction(luaVM, "isBleeding", "isPedBleeding");
    lua_classfunction(luaVM, "isMissionActor", "isPedMissionActor");
    lua_classfunction(luaVM, "isUsingNativeWalkingStyle", "isPedUsingNativeWalkingStyle");

    lua_classfunction(luaVM, "setCanBeKnockedOffBike", "setPedCanBeKnockedOffBike");
    lua_classfunction(luaVM, "setAnalogControlState", "setPedAnalogControlState");
    lua_classfunction(luaVM, "setAnimation", "setPedAnimation");
    lua_classfunction(luaVM, "setAnimationProgress", "setPedAnimationProgress");
    lua_classfunction(luaVM, "setAnimationSpeed", "setPedAnimationSpeed");
    lua_classfunction(luaVM, "setCameraRotation", "setPedCameraRotation");
    lua_classfunction(luaVM, "setControlState", "setPedControlState");
    lua_classfunction(luaVM, "warpIntoVehicle", "warpPedIntoVehicle");
    lua_classfunction(luaVM, "setOxygenLevel", "setPedOxygenLevel");
    lua_classfunction(luaVM, "setArmor", "setPedArmor");
    lua_classfunction(luaVM, "setWeaponSlot", "setPedWeaponSlot");
    lua_classfunction(luaVM, "setDoingGangDriveby", "setPedDoingGangDriveby");
    lua_classfunction(luaVM, "setFightingStyle", "setPedFightingStyle");
    lua_classfunction(luaVM, "setHeadless", "setPedHeadless");
    lua_classfunction(luaVM, "setOnFire", "setPedOnFire");
    lua_classfunction(luaVM, "setTargetingMarkerEnabled", "setPedTargetingMarkerEnabled");
    lua_classfunction(luaVM, "setVoice", "setPedVoice");
    lua_classfunction(luaVM, "removeFromVehicle", "removePedFromVehicle");
    lua_classfunction(luaVM, "setAimTarget", "setPedAimTarget");
    lua_classfunction(luaVM, "setLookAt", "setPedLookAt");
    lua_classfunction(luaVM, "setWalkingStyle", "setPedWalkingStyle");
    lua_classfunction(luaVM, "setUseNativeWalkingStyle", "setPedUseNativeWalkingStyle");
    lua_classfunction(luaVM, "setStat", "setPedStat");
    lua_classfunction(luaVM, "giveWeapon", "givePedWeapon");
    lua_classfunction(luaVM, "isReloadingWeapon", "isPedReloadingWeapon");
    lua_classfunction(luaVM, "setEnterVehicle", "setPedEnterVehicle");
    lua_classfunction(luaVM, "setExitVehicle", "setPedExitVehicle");
    lua_classfunction(luaVM, "setWeaponShootingRate", "setPedWeaponShootingRate");
    lua_classfunction(luaVM, "setWeaponAccuracy", "setPedWeaponAccuracy");
    lua_classfunction(luaVM, "setGoTo", "setPedGoTo");
    lua_classfunction(luaVM, "setChatWith", "setPedChatWith");
    lua_classfunction(luaVM, "setStandStill", "setPedStandStill");
    lua_classfunction(luaVM, "setTurnToFace", "setPedTurnToFace");
    lua_classfunction(luaVM, "setGoToOffset", "setPedGoToOffset");
    lua_classfunction(luaVM, "setKillOnFoot", "setPedKillOnFoot");
    lua_classfunction(luaVM, "setWander", "setPedWander");
    lua_classfunction(luaVM, "setScriptedSpeechMuted", "setPedScriptedSpeechMuted");
    lua_classfunction(luaVM, "setFacialTalk", "setPedFacialTalk");
    lua_classfunction(luaVM, "stopFacialTalk", "stopPedFacialTalk");
    lua_classfunction(luaVM, "setShootAt", "setPedShootAt");
    lua_classfunction(luaVM, "setTaskSequence", "setPedTaskSequence");
    lua_classfunction(luaVM, "getTaskSequenceProgress", "getPedTaskSequenceProgress");
    lua_classfunction(luaVM, "setDriveWander", "setPedDriveWander");
    lua_classfunction(luaVM, "setDriveTo", "setPedDriveTo");
    lua_classfunction(luaVM, "setDriveBy", "setPedDriveBy");
    lua_classfunction(luaVM, "setMissionActor", "setPedMissionActor");
    lua_classfunction(luaVM, "setBleeding", "setPedBleeding");
    lua_classfunction(luaVM, "playVoiceLine", "playPedVoiceLine");

    lua_classvariable(luaVM, "vehicle", OOP_WarpPedIntoVehicle, GetPedOccupiedVehicle);
    lua_classvariable(luaVM, "vehicleSeat", NULL, "getPedOccupiedVehicleSeat");
    lua_classvariable(luaVM, "canBeKnockedOffBike", "setPedCanBeKnockedOffBike", "canPedBeKnockedOffBike");
    lua_classvariable(luaVM, "hasJetPack", NULL, "doesPedHaveJetPack");
    lua_classvariable(luaVM, "jetpack", NULL, "isPedWearingJetpack");  // introduced in 1.5.5-9.13846
    lua_classvariable(luaVM, "armor", "setPedArmor", "getPedArmor");
    lua_classvariable(luaVM, "fightingStyle", "setPedFightingStyle", "getPedFightingStyle");
    lua_classvariable(luaVM, "cameraRotation", "setPedCameraRotation", "getPedCameraRotation");
    lua_classvariable(luaVM, "contactElement", NULL, "getPedContactElement");
    lua_classvariable(luaVM, "moveState", NULL, "getPedMoveState");
    lua_classvariable(luaVM, "oxygenLevel", "setPedOxygenLevel", "getPedOxygenLevel");
    lua_classvariable(luaVM, "target", NULL, "getPedTarget");
    lua_classvariable(luaVM, "simplestTask", NULL, "getPedSimplestTask");
    lua_classvariable(luaVM, "choking", NULL, "isPedChoking");
    lua_classvariable(luaVM, "doingGangDriveby", "setPedDoingGangDriveby", "isPedDoingGangDriveby");
    lua_classvariable(luaVM, "ducked", NULL, "isPedDucked");
    lua_classvariable(luaVM, "headless", "setPedHeadless", "isPedHeadless");
    lua_classvariable(luaVM, "inVehicle", NULL, "isPedInVehicle");
    lua_classvariable(luaVM, "onFire", "setPedOnFire", "isPedOnFire");
    lua_classvariable(luaVM, "onGround", NULL, "isPedOnGround");
    lua_classvariable(luaVM, "dead", NULL, "isPedDead");
    lua_classvariable(luaVM, "targetingMarker", "setPedTargetingMarkerEnabled", "isPedTargetingMarkerEnabled");
    lua_classvariable(luaVM, "footBlood", "setPedFootBloodEnabled", NULL);
    lua_classvariable(luaVM, "bleeding", "setPedBleeding", "isPedBleeding");
    lua_classvariable(luaVM, "missionActor", "setPedMissionActor", "isPedMissionActor");
    lua_classvariable(luaVM, "targetCollision", nullptr, OOP_GetPedTargetCollision);
    lua_classvariable(luaVM, "targetEnd", nullptr, OOP_GetPedTargetEnd);
    lua_classvariable(luaVM, "targetStart", nullptr, OOP_GetPedTargetStart);
    // lua_classvariable ( luaVM, "muzzlePosition", NULL, "getPedWeaponMuzzlePosition" ); // TODO: needs to return a vector3 for oop
    lua_classvariable(luaVM, "weaponSlot", "setPedWeaponSlot", "getPedWeaponSlot");
    lua_classvariable(luaVM, "walkingStyle", "setPedWalkingStyle", "getPedWalkingStyle");
    lua_classvariable(luaVM, "usingNativeWalkingStyle", "setPedUseNativeWalkingStyle", "isPedUsingNativeWalkingStyle");
    lua_classvariable(luaVM, "reloadingWeapon", nullptr, "isPedReloadingWeapon");

    lua_registerclass(luaVM, "Ped", "Element");
}

bool CLuaPedDefs::ResetPedVoice(CClientPed* ped)
{
    short szOldType, szNewType, szOldVoice, szNewVoice;
    ped->GetVoice(&szOldType, &szOldVoice);
    ped->ResetVoice();
    ped->GetVoice(&szNewType, &szNewVoice);
    return szNewType != szOldType && szNewVoice != szOldVoice;
}

int CLuaPedDefs::GetPedVoice(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (!argStream.HasErrors())
    {
        if (!pPed->IsSpeechEnabled())
        {
            lua_pushstring(luaVM, "PED_TYPE_DISABLED");
            return 1;
        }
        else
        {
            const char* szVoiceType = 0;
            const char* szVoiceBank = 0;
            pPed->GetVoice(&szVoiceType, &szVoiceBank);
            if (szVoiceType && szVoiceBank)
            {
                lua_pushstring(luaVM, szVoiceType);
                lua_pushstring(luaVM, szVoiceBank);
                return 2;
            }
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::SetPedVoice(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    SString          strVoiceType = "", strVoiceBank = "";
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);
    argStream.ReadString(strVoiceType);
    argStream.ReadString(strVoiceBank, "");

    if (!argStream.HasErrors())
    {
        const char* szVoiceType = strVoiceType.c_str();
        const char* szVoiceBank = strVoiceBank == "" ? NULL : strVoiceBank.c_str();

        if (szVoiceType)
        {
            if (!stricmp(szVoiceType, "PED_TYPE_DISABLED"))
            {
                pPed->SetSpeechEnabled(false);
            }

            else if (szVoiceBank)
            {
                pPed->SetSpeechEnabled(true);
                pPed->SetVoice(szVoiceType, szVoiceBank);
            }

            lua_pushboolean(luaVM, true);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::GetPedWeapon(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    unsigned char    ucSlot = 0xFF;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);
    argStream.ReadNumber(ucSlot, 0xFF);

    if (!argStream.HasErrors())
    {
        if (ucSlot == 0xFF)
            ucSlot = pPed->GetCurrentWeaponSlot();

        unsigned char ucWeapon = pPed->GetWeaponType((eWeaponSlot)ucSlot);
        lua_pushnumber(luaVM, ucWeapon);
        return 1;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::GetPedWeaponSlot(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (!argStream.HasErrors())
    {
        // Grab his current slot
        int iSlot = pPed->GetCurrentWeaponSlot();
        lua_pushnumber(luaVM, iSlot);
        return 1;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::GetPedAmmoInClip(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    unsigned char    ucSlot = 0xFF;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);
    argStream.ReadNumber(ucSlot, 0xFF);

    if (!argStream.HasErrors())
    {
        // Got a second argument too (slot)?
        ucSlot = ucSlot == 0xFF ? pPed->GetCurrentWeaponSlot() : ucSlot;

        CWeapon* pWeapon = pPed->GetWeapon((eWeaponSlot)ucSlot);
        if (pWeapon)
        {
            unsigned short usAmmo = static_cast<unsigned short>(pWeapon->GetAmmoInClip());
            lua_pushnumber(luaVM, usAmmo);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::GetPedTotalAmmo(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    unsigned char    ucSlot = 0xFF;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);
    argStream.ReadNumber(ucSlot, 0xFF);

    if (!argStream.HasErrors())
    {
        // Got a second argument too (slot)?
        ucSlot = ucSlot == 0xFF ? pPed->GetCurrentWeaponSlot() : ucSlot;

        // Grab the ammo and return
        CWeapon* pWeapon = pPed->GetWeapon((eWeaponSlot)ucSlot);
        if (pWeapon)
        {
            // Keep server and client synced
            unsigned short usAmmo = 1;
            if (CWeaponNames::DoesSlotHaveAmmo(ucSlot))
                usAmmo = static_cast<unsigned short>(pWeapon->GetAmmoTotal());

            lua_pushnumber(luaVM, usAmmo);
            return 1;
        }
        else if (ucSlot < WEAPONSLOT_MAX && pPed->m_usWeaponAmmo[ucSlot])
        {
            // The ped musn't be streamed in, so we can get the stored value instead
            ushort usAmmo = 1;

            if (CWeaponNames::DoesSlotHaveAmmo(ucSlot))
                usAmmo = pPed->m_usWeaponAmmo[ucSlot];

            lua_pushnumber(luaVM, usAmmo);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::GetPedWeaponMuzzlePosition(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (!argStream.HasErrors())
    {
        CVector vecMuzzlePos;
        if (CStaticFunctionDefinitions::GetPedWeaponMuzzlePosition(*pPed, vecMuzzlePos))
        {
            lua_pushnumber(luaVM, vecMuzzlePos.fX);
            lua_pushnumber(luaVM, vecMuzzlePos.fY);
            lua_pushnumber(luaVM, vecMuzzlePos.fZ);
            return 3;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::GetPedOccupiedVehicle(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (!argStream.HasErrors())
    {
        // Grab his occupied vehicle
        CClientVehicle* pVehicle = pPed->GetOccupiedVehicle();
        if (pVehicle)
        {
            lua_pushelement(luaVM, pVehicle);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::GetPedOccupiedVehicleSeat(lua_State* luaVM)
{
    CClientPed* pPed = NULL;

    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (!argStream.HasErrors())
    {
        unsigned int uiVehicleSeat;
        if (CStaticFunctionDefinitions::GetPedOccupiedVehicleSeat(*pPed, uiVehicleSeat))
        {
            lua_pushnumber(luaVM, uiVehicleSeat);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::GetPedTask(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    SString          strPriority = "";
    unsigned int     uiTaskType = 0;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);
    argStream.ReadString(strPriority);
    argStream.ReadNumber(uiTaskType);

    if (!argStream.HasErrors())
    {
        // Any priority specified?
        if (strPriority != "")
        {
            // Primary or secondary task grabbed?
            bool bPrimary = false;
            if ((bPrimary = !stricmp(strPriority.c_str(), "primary")) || (!stricmp(strPriority.c_str(), "secondary")))
            {
                // Grab the taskname list and return it
                std::vector<SString> taskHierarchy;
                if (CStaticFunctionDefinitions::GetPedTask(*pPed, bPrimary, uiTaskType, taskHierarchy))
                {
                    for (uint i = 0; i < taskHierarchy.size(); i++)
                        lua_pushstring(luaVM, taskHierarchy[i]);
                    return taskHierarchy.size();
                }
            }
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::GetPedSimplestTask(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (!argStream.HasErrors())
    {
        // Grab his simplest task and return it
        const char* szTaskName = CStaticFunctionDefinitions::GetPedSimplestTask(*pPed);
        if (szTaskName)
        {
            lua_pushstring(luaVM, szTaskName);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::IsPedDoingTask(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    SString          strTaskName = "";
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);
    argStream.ReadString(strTaskName);

    if (!argStream.HasErrors())
    {
        // Check whether he's doing that task or not
        bool bIsDoingTask;
        if (CStaticFunctionDefinitions::IsPedDoingTask(*pPed, strTaskName.c_str(), bIsDoingTask))
        {
            lua_pushboolean(luaVM, bIsDoingTask);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::GetPedTarget(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (!argStream.HasErrors())
    {
        // Grab his target element
        CClientEntity* pEntity = CStaticFunctionDefinitions::GetPedTarget(*pPed);
        if (pEntity)
        {
            lua_pushelement(luaVM, pEntity);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::GetPedTargetStart(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = nullptr;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (!argStream.HasErrors())
    {
        // Grab his start aim position and return it
        CVector vecStart;
        pPed->GetShotData(&vecStart);

        lua_pushnumber(luaVM, vecStart.fX);
        lua_pushnumber(luaVM, vecStart.fY);
        lua_pushnumber(luaVM, vecStart.fZ);
        return 3;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::OOP_GetPedTargetStart(lua_State* luaVM)
{
    CClientPed*      pPed = nullptr;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (argStream.HasErrors())
        return luaL_error(luaVM, argStream.GetFullErrorMessage());

    CVector vecStart;

    if (pPed->GetShotData(&vecStart))
    {
        lua_pushvector(luaVM, vecStart);
        return 1;
    }

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::GetPedTargetEnd(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = nullptr;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (!argStream.HasErrors())
    {
        // Grab the ped end target position and return it
        CVector vecEnd;
        pPed->GetShotData(nullptr, &vecEnd);

        lua_pushnumber(luaVM, vecEnd.fX);
        lua_pushnumber(luaVM, vecEnd.fY);
        lua_pushnumber(luaVM, vecEnd.fZ);
        return 3;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::OOP_GetPedTargetEnd(lua_State* luaVM)
{
    CClientPed*      pPed = nullptr;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (argStream.HasErrors())
        return luaL_error(luaVM, argStream.GetFullErrorMessage());

    CVector vecEnd;

    if (pPed->GetShotData(nullptr, &vecEnd))
    {
        lua_pushvector(luaVM, vecEnd);
        return 1;
    }

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::GetPedTargetCollision(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = nullptr;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (!argStream.HasErrors())
    {
        // Grab his target collision and return it
        CVector vecCollision;
        if (CStaticFunctionDefinitions::GetPedTargetCollision(*pPed, vecCollision))
        {
            lua_pushnumber(luaVM, vecCollision.fX);
            lua_pushnumber(luaVM, vecCollision.fY);
            lua_pushnumber(luaVM, vecCollision.fZ);
            return 3;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::OOP_GetPedTargetCollision(lua_State* luaVM)
{
    CClientPed*      pPed = nullptr;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (argStream.HasErrors())
        return luaL_error(luaVM, argStream.GetFullErrorMessage());

    CVector vecCollision;
    if (CStaticFunctionDefinitions::GetPedTargetCollision(*pPed, vecCollision))
    {
        lua_pushvector(luaVM, vecCollision);
        return 1;
    }

    lua_pushboolean(luaVM, false);
    return 1;
}

float CLuaPedDefs::GetPedArmor(CClientPed* const ped) noexcept
{
    return ped->GetArmor();
}

int CLuaPedDefs::GetPedStat(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    unsigned short   usStat = 0;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);
    argStream.ReadNumber(usStat);

    if (!argStream.HasErrors())
    {
        // Check the stat
        if (usStat < NUM_PLAYER_STATS)
        {
            float fValue = pPed->GetStat(usStat);
            lua_pushnumber(luaVM, fValue);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::IsPedChoking(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (!argStream.HasErrors())
    {
        // Return whether he's choking or not
        lua_pushboolean(luaVM, pPed->IsChoking());
        return 1;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushnil(luaVM);
    return 1;
}

int CLuaPedDefs::IsPedDucked(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (!argStream.HasErrors())
    {
        // Grab his ducked state
        bool bDucked = pPed->IsDucked();
        lua_pushboolean(luaVM, bDucked);
        return 1;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushnil(luaVM);
    return 1;
}

int CLuaPedDefs::IsPedInVehicle(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (!argStream.HasErrors())
    {
        // Find out whether he's in a vehicle or not
        bool bInVehicle;
        if (CStaticFunctionDefinitions::IsPedInVehicle(*pPed, bInVehicle))
        {
            // Return that state
            lua_pushboolean(luaVM, bInVehicle);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushnil(luaVM);
    return 1;
}

int CLuaPedDefs::DoesPedHaveJetPack(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (!argStream.HasErrors())
    {
        // Find out whether he has a jetpack or not and return it
        bool bHasJetPack = pPed->HasJetPack();
        lua_pushboolean(luaVM, bHasJetPack);
        return 1;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::IsPedOnGround(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    bool             checkVehicles = false;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);
    argStream.ReadBool(checkVehicles, false);

    if (!argStream.HasErrors())
    {
        // Find out whether he's on the ground or not and return it
        bool bOnGround = pPed->IsOnGround(checkVehicles);
        lua_pushboolean(luaVM, bOnGround);
        return 1;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::GetPedContactElement(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (!argStream.HasErrors())
    {
        CClientEntity* pEntity = pPed->GetContactEntity();
        if (pEntity)
        {
            lua_pushelement(luaVM, pEntity);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::GetPedRotation(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (!argStream.HasErrors())
    {
        float fRotation = ConvertRadiansToDegrees(pPed->GetCurrentRotation());
        lua_pushnumber(luaVM, fRotation);
        return 1;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::CanPedBeKnockedOffBike(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (!argStream.HasErrors())
    {
        bool bCanBeKnockedOffBike = pPed->GetCanBeKnockedOffBike();
        lua_pushboolean(luaVM, bCanBeKnockedOffBike);
        return 1;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

std::variant<bool, CLuaMultiReturn<float, float, float>> CLuaPedDefs::GetElementBonePosition(CClientPed* ped, const std::uint16_t bone)
{
    if (bone < BONE_ROOT || bone > BONE_LEFTBREAST)
        throw std::invalid_argument("Invalid bone: " + std::to_string(bone));

    CEntity* entity = ped->GetGameEntity();
    CVector  position;

    if (!entity || !entity->GetBonePosition(static_cast<eBone>(bone), position))
        return false;

    return CLuaMultiReturn<float, float, float>(position.fX, position.fY, position.fZ);
}

std::variant<bool, CLuaMultiReturn<float, float, float>> CLuaPedDefs::GetElementBoneRotation(CClientPed* ped, const std::uint16_t bone)
{
    if (bone < BONE_ROOT || bone > BONE_LEFTBREAST)
        throw std::invalid_argument("Invalid bone: " + std::to_string(bone));

    CEntity* entity = ped->GetGameEntity();
    float    yaw = 0.0f;
    float    pitch = 0.0f;
    float    roll = 0.0f;

    if (!entity || !entity->GetBoneRotation(static_cast<eBone>(bone), yaw, pitch, roll))
        return false;

    return CLuaMultiReturn<float, float, float>(yaw, pitch, roll);
}

std::variant<bool, CLuaMultiReturn<float, float, float, float>> CLuaPedDefs::GetElementBoneQuaternion(CClientPed* ped, const std::uint16_t bone)
{
    if (bone < BONE_ROOT || bone > BONE_LEFTBREAST)
        throw std::invalid_argument("Invalid bone: " + std::to_string(bone));

    CEntity* entity = ped->GetGameEntity();
    float    x = 0.0f;
    float    y = 0.0f;
    float    z = 0.0f;
    float    w = 0.0f;

    if (!entity || !entity->GetBoneRotationQuat(static_cast<eBone>(bone), x, y, z, w))
        return false;

    return CLuaMultiReturn<float, float, float, float>(x, y, z, w);
}

std::variant<bool, std::array<std::array<float, 4>, 4>> CLuaPedDefs::GetElementBoneMatrix(CClientPed* ped, const std::uint16_t bone)
{
    if (bone < BONE_ROOT || bone > BONE_LEFTBREAST)
        throw std::invalid_argument("Invalid bone: " + std::to_string(bone));

    CEntity* entity = ped->GetGameEntity();

    if (!entity)
        return false;

    RwMatrix* rwmatrix = entity->GetBoneRwMatrix(static_cast<eBone>(bone));

    if (!rwmatrix)
        return false;

    CMatrix matrix;

    g_pGame->GetRenderWare()->RwMatrixToCMatrix(*rwmatrix, matrix);

    return matrix.To4x4Array();
}

bool CLuaPedDefs::SetElementBonePosition(CClientPed* ped, const std::uint16_t bone, const CVector position)
{
    if (bone < BONE_ROOT || bone > BONE_LEFTBREAST)
        throw std::invalid_argument("Invalid bone: " + std::to_string(bone));

    CEntity* entity = ped->GetGameEntity();

    if (!entity)
        return false;

    return entity->SetBonePosition(static_cast<eBone>(bone), position);
}

bool CLuaPedDefs::SetElementBoneRotation(CClientPed* ped, const std::uint16_t bone, const float yaw, const float pitch, const float roll)
{
    if (bone < BONE_ROOT || bone > BONE_LEFTBREAST)
        throw std::invalid_argument("Invalid bone: " + std::to_string(bone));

    CEntity* entity = ped->GetGameEntity();

    if (!entity)
        return false;

    return entity->SetBoneRotation(static_cast<eBone>(bone), yaw, pitch, roll);
}

bool CLuaPedDefs::SetElementBoneQuaternion(CClientPed* ped, const std::uint16_t bone, const float x, const float y, const float z, const float w)
{
    if (bone < BONE_ROOT || bone > BONE_LEFTBREAST)
        throw std::invalid_argument("Invalid bone: " + std::to_string(bone));

    CEntity* entity = ped->GetGameEntity();

    if (!entity)
        return false;

    return entity->SetBoneRotationQuat(static_cast<eBone>(bone), x, y, z, w);
}

bool CLuaPedDefs::SetElementBoneMatrix(CClientPed* ped, const std::uint16_t bone, const CMatrix matrix)
{
    if (bone < BONE_ROOT || bone > BONE_LEFTBREAST)
        throw std::invalid_argument("Invalid bone: " + std::to_string(bone));

    CEntity* entity = ped->GetGameEntity();

    if (!entity)
        return false;

    return entity->SetBoneMatrix(static_cast<eBone>(bone), matrix);
}

bool CLuaPedDefs::UpdateElementRpHAnim(CClientPed* ped)
{
    CEntity* entity = ped->GetGameEntity();

    if (!entity)
        return false;

    entity->UpdateRpHAnim();

    if (entity->GetModelIndex() != 0)
        return true;

    RpClump* clump = entity->GetRpClump();

    if (clump)
    {
        ((void(__cdecl*)(RpClump*))0x5DF560)(clump);  // CPed::ShoulderBoneRotation
    }

    return true;
}

int CLuaPedDefs::GetPedBonePosition(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    unsigned char    ucBone = 0;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);
    argStream.ReadNumber(ucBone);

    if (!argStream.HasErrors())
    {
        if (ucBone <= BONE_RIGHTFOOT)
        {
            eBone   bone = (eBone)ucBone;
            CVector vecPosition;
            if (CStaticFunctionDefinitions::GetPedBonePosition(*pPed, bone, vecPosition))
            {
                lua_pushnumber(luaVM, vecPosition.fX);
                lua_pushnumber(luaVM, vecPosition.fY);
                lua_pushnumber(luaVM, vecPosition.fZ);
                return 3;
            }
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::OOP_GetPedBonePosition(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    unsigned char    ucBone = 0;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);
    argStream.ReadNumber(ucBone);

    if (!argStream.HasErrors())
    {
        if (ucBone <= BONE_RIGHTFOOT)
        {
            eBone   bone = (eBone)ucBone;
            CVector vecPosition;
            if (CStaticFunctionDefinitions::GetPedBonePosition(*pPed, bone, vecPosition))
            {
                lua_pushvector(luaVM, vecPosition);
                return 1;
            }
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::SetPedWeaponSlot(lua_State* luaVM)
{
    // Verify the argument
    CClientEntity*   pElement = NULL;
    int              iSlot = 0;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pElement);
    argStream.ReadNumber(iSlot);

    if (!argStream.HasErrors())
    {
        // Valid slot?
        if (iSlot >= 0)
        {
            // Set his slot
            if (CStaticFunctionDefinitions::SetPedWeaponSlot(*pElement, iSlot))
            {
                lua_pushboolean(luaVM, true);
                return 1;
            }
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::GivePedWeapon(lua_State* luaVM)
{
    // Verify the argument
    CClientEntity*   pEntity = NULL;
    eWeaponType      weaponType;
    ushort           usAmmo = 0;
    bool             bSetAsCurrent = false;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pEntity);
    argStream.ReadEnumStringOrNumber(weaponType);
    argStream.ReadNumber(usAmmo, 30);
    argStream.ReadBool(bSetAsCurrent, false);

    if (!argStream.HasErrors())
    {
        if (CStaticFunctionDefinitions::GivePedWeapon(*pEntity, weaponType, usAmmo, bSetAsCurrent))
        {
            lua_pushboolean(luaVM, true);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

bool CLuaPedDefs::IsPedReloadingWeapon(CClientPed* const ped) noexcept
{
    return ped->IsReloadingWeapon();
}

int CLuaPedDefs::GetPedClothes(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    unsigned char    ucType = 0;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);
    argStream.ReadNumber(ucType);

    if (!argStream.HasErrors())
    {
        SString strTexture, strModel;
        if (CStaticFunctionDefinitions::GetPedClothes(*pPed, ucType, strTexture, strModel))
        {
            lua_pushstring(luaVM, strTexture);
            lua_pushstring(luaVM, strModel);
            return 2;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

bool CLuaPedDefs::GetPedControlState(std::variant<CClientPed*, std::string> first, std::optional<std::string> maybeControl)
{
    CClientPed* ped{};
    std::string control{};

    if (std::holds_alternative<CClientPed*>(first))
    {
        if (!maybeControl.has_value())
            throw std::invalid_argument("Expected control name at argument 2");

        ped = std::get<CClientPed*>(first);
        control = maybeControl.value();
    }
    else if (std::holds_alternative<std::string>(first))
    {
        ped = CStaticFunctionDefinitions::GetLocalPlayer();
        control = std::get<std::string>(first);
    }
    else
    {
        throw std::invalid_argument("Expected ped or control name at argument 1");
    }

    bool state;

    if (!CStaticFunctionDefinitions::GetPedControlState(*ped, control, state))
        return false;

    return state;
}

int CLuaPedDefs::GetPedAnalogControlState(lua_State* luaVM)
{
    SString          strControlState = "";
    float            fState = 0.0f;
    CClientPed*      pPed = NULL;
    bool             bRawInput;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);
    argStream.ReadString(strControlState);
    argStream.ReadBool(bRawInput, false);

    if (!argStream.HasErrors())
    {
        float fState;
        if (CStaticFunctionDefinitions::GetPedAnalogControlState(*pPed, strControlState, fState, bRawInput))
        {
            lua_pushnumber(luaVM, fState);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::IsPedDoingGangDriveby(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (!argStream.HasErrors())
    {
        bool bDoingGangDriveby;
        if (CStaticFunctionDefinitions::IsPedDoingGangDriveby(*pPed, bDoingGangDriveby))
        {
            lua_pushboolean(luaVM, bDoingGangDriveby);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::GetPedFightingStyle(lua_State* luaVM)
{
    CClientPed*      pPed;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (!argStream.HasErrors())
    {
        unsigned char ucStyle;
        if (CStaticFunctionDefinitions::GetPedFightingStyle(*pPed, ucStyle))
        {
            lua_pushnumber(luaVM, ucStyle);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::SetPedAnalogControlState(lua_State* luaVM)
{
    SString          strControlState = "";
    float            fState = 0.0f;
    CClientEntity*   pEntity = NULL;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pEntity);
    argStream.ReadString(strControlState);
    argStream.ReadNumber(fState);

    if (!argStream.HasErrors())
    {
        if (CStaticFunctionDefinitions::SetPedAnalogControlState(*pEntity, strControlState, fState))
        {
            lua_pushboolean(luaVM, true);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::GetPedAnimation(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (!argStream.HasErrors())
    {
        SString strBlockName, strAnimName;
        if (pPed->GetRunningAnimationName(strBlockName, strAnimName))
        {
            const SAnimationCache& animationCache = pPed->GetAnimationCache();
            lua_pushstring(luaVM, strBlockName);
            lua_pushstring(luaVM, strAnimName);
            lua_newtable(luaVM);
            lua_pushinteger(luaVM, animationCache.iTime);
            lua_setfield(luaVM, -2, "time");
            lua_pushboolean(luaVM, animationCache.bLoop);
            lua_setfield(luaVM, -2, "loop");
            lua_pushboolean(luaVM, animationCache.bUpdatePosition);
            lua_setfield(luaVM, -2, "updatePosition");
            lua_pushboolean(luaVM, animationCache.bInterruptible);
            lua_setfield(luaVM, -2, "interruptable");
            lua_pushboolean(luaVM, animationCache.bFreezeLastFrame);
            lua_setfield(luaVM, -2, "freezeLastFrame");
            lua_pushinteger(luaVM, animationCache.iBlend);
            lua_setfield(luaVM, -2, "blendTime");
            lua_pushboolean(luaVM, pPed->IsTaskToBeRestoredOnAnimEnd());
            lua_setfield(luaVM, -2, "restoreTaskOnAnimEnd");
            return 3;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::GetPedMoveState(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (!argStream.HasErrors())
    {
        std::string strMoveState;
        if (CStaticFunctionDefinitions::GetPedMoveState(*pPed, strMoveState))
        {
            lua_pushstring(luaVM, strMoveState.c_str());
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::GetPedMoveAnim(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (!argStream.HasErrors())
    {
        unsigned int iMoveAnim;
        if (CStaticFunctionDefinitions::GetPedMoveAnim(*pPed, iMoveAnim))
        {
            lua_pushnumber(luaVM, iMoveAnim);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::IsPedUsingNativeWalkingStyle(lua_State* luaVM)
{
    CClientPed* pPed = nullptr;

    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (!argStream.HasErrors())
    {
        bool bEnabled;
        if (CStaticFunctionDefinitions::IsPedUsingNativeWalkingStyle(*pPed, bEnabled))
        {
            lua_pushboolean(luaVM, bEnabled);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::IsPedHeadless(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (!argStream.HasErrors())
    {
        bool bHeadless;
        if (CStaticFunctionDefinitions::IsPedHeadless(*pPed, bHeadless))
        {
            lua_pushboolean(luaVM, bHeadless);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::IsPedFrozen(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (!argStream.HasErrors())
    {
        bool bFrozen;
        if (CStaticFunctionDefinitions::IsPedFrozen(*pPed, bFrozen))
        {
            lua_pushboolean(luaVM, bFrozen);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::IsPedFootBloodEnabled(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (!argStream.HasErrors())
    {
        bool bHasFootBlood = false;
        if (CStaticFunctionDefinitions::IsPedFootBloodEnabled(*pPed, bHasFootBlood))
        {
            lua_pushboolean(luaVM, bHasFootBlood);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

bool CLuaPedDefs::IsPedBleeding(CClientPed* pPed)
{
    return pPed->IsBleeding();
}

int CLuaPedDefs::GetPedCameraRotation(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (!argStream.HasErrors())
    {
        float fRotation = 0.0f;
        if (CStaticFunctionDefinitions::GetPedCameraRotation(*pPed, fRotation))
        {
            lua_pushnumber(luaVM, fRotation);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::IsPedOnFire(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (!argStream.HasErrors())
    {
        bool bOnFire;
        if (CStaticFunctionDefinitions::IsPedOnFire(*pPed, bOnFire))
        {
            lua_pushboolean(luaVM, bOnFire);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::SetPedOnFire(lua_State* luaVM)
{
    // Verify the argument
    CClientEntity*   pEntity = NULL;
    bool             bOnFire = false;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pEntity);
    argStream.ReadBool(bOnFire);

    if (!argStream.HasErrors())
    {
        if (CStaticFunctionDefinitions::SetPedOnFire(*pEntity, bOnFire))
        {
            lua_pushboolean(luaVM, true);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::WarpPedIntoVehicle(lua_State* luaVM)
{
    //  warpPedIntoVehicle ( element ped, element vehicle, int seat )
    CClientPed*     pPed;
    CClientVehicle* pVehicle;
    uint            uiSeat;

    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);
    argStream.ReadUserData(pVehicle);
    argStream.ReadNumber(uiSeat, 0);

    MinClientReqCheck(argStream, MIN_CLIENT_REQ_REMOVEPEDFROMVEHICLE_CLIENTSIDE, "function is being called client side");

    if (!argStream.HasErrors())
    {
        if (!pPed->IsLocalEntity() || !pVehicle->IsLocalEntity())
            argStream.SetCustomError("This client side function will only work with client created peds and vehicles");
    }

    if (!argStream.HasErrors())
    {
        if (CStaticFunctionDefinitions::WarpPedIntoVehicle(pPed, pVehicle, uiSeat))
        {
            lua_pushboolean(luaVM, true);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::OOP_WarpPedIntoVehicle(lua_State* luaVM)
{
    //  ped.vehicle = element vehicle
    //  ped.vehicle = nil
    CClientPed*     pPed;
    CClientVehicle* pVehicle;
    uint            uiSeat = 0;

    CScriptArgReader argStream(luaVM);

    argStream.ReadUserData(pPed);
    argStream.ReadUserData(pVehicle, NULL);
    if (pVehicle != NULL)
    {
        MinClientReqCheck(argStream, MIN_CLIENT_REQ_WARPPEDINTOVEHICLE_CLIENTSIDE, "function is being called client side");
        if (!argStream.HasErrors())
        {
            if (!pPed->IsLocalEntity() || !pVehicle->IsLocalEntity())
                argStream.SetCustomError("This client side function will only work with client created peds and vehicles");
        }

        if (!argStream.HasErrors())
        {
            if (CStaticFunctionDefinitions::WarpPedIntoVehicle(pPed, pVehicle, uiSeat))
            {
                lua_pushboolean(luaVM, true);
                return 1;
            }
        }
        else
            m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());
    }
    else
    {
        if (!argStream.HasErrors())
        {
            if (!pPed->IsLocalEntity())
                argStream.SetCustomError("This client side function will only work with client created peds");
        }

        if (!argStream.HasErrors())
        {
            if (CStaticFunctionDefinitions::RemovePedFromVehicle(pPed))
            {
                lua_pushboolean(luaVM, true);
                return 1;
            }
        }
        else
            m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());
    }

    lua_pushboolean(luaVM, false);

    return 1;
}

int CLuaPedDefs::RemovePedFromVehicle(lua_State* luaVM)
{
    //  removePedFromVehicle ( element ped )
    CClientPed* pPed;

    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    MinClientReqCheck(argStream, MIN_CLIENT_REQ_WARPPEDINTOVEHICLE_CLIENTSIDE, "function is being called client side");

    if (!argStream.HasErrors())
    {
        if (!pPed->IsLocalEntity())
            argStream.SetCustomError("This client side function will only work with client created peds");
    }

    if (!argStream.HasErrors())
    {
        if (CStaticFunctionDefinitions::RemovePedFromVehicle(pPed))
        {
            lua_pushboolean(luaVM, true);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::GetPedOxygenLevel(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (!argStream.HasErrors())
    {
        float fOxygen;
        if (CStaticFunctionDefinitions::GetPedOxygenLevel(*pPed, fOxygen))
        {
            lua_pushnumber(luaVM, fOxygen);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::IsPedDead(lua_State* luaVM)
{
    //  bool isPedDead ( ped thePed )
    CClientPed*      pPed;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pPed);

    if (!argStream.HasErrors())
    {
        // Grab his dead state and return it
        bool bDead = pPed->IsDead() || pPed->IsDying();

        // Cover the window between network death and GTA processing it (#4147).
        // Don't apply if GTA has already processed a revival (health > 0 and not
        // in a death task) - IsDeadOnNetwork would be a stale server-side artifact.
        if (auto pPlayer = dynamic_cast<CClientPlayer*>(pPed))
        {
            if (pPlayer->IsDeadOnNetwork() && (pPed->GetHealth() <= 0.0f || bDead))
                bDead = true;
        }

        lua_pushboolean(luaVM, bDead);
        return 1;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushnil(luaVM);
    return 1;
}

int CLuaPedDefs::AddPedClothes(lua_State* luaVM)
{
    // Verify the argument
    CClientEntity*   pEntity = NULL;
    SString          strTexture = "", strModel = "";
    unsigned char    ucType = 0;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pEntity);
    argStream.ReadString(strTexture);
    argStream.ReadString(strModel);
    argStream.ReadNumber(ucType);

    if (!argStream.HasErrors())
    {
        if (CStaticFunctionDefinitions::AddPedClothes(*pEntity, strTexture, strModel, ucType))
        {
            lua_pushboolean(luaVM, true);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::RemovePedClothes(lua_State* luaVM)
{
    // Verify the argument
    CClientEntity*   pEntity = NULL;
    unsigned char    ucType = 0;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pEntity);
    argStream.ReadNumber(ucType);

    if (!argStream.HasErrors())
    {
        if (CStaticFunctionDefinitions::RemovePedClothes(*pEntity, ucType))
        {
            lua_pushboolean(luaVM, true);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

bool CLuaPedDefs::SetPedControlState(std::variant<CClientPed*, std::string> first, std::variant<std::string, bool> second, std::optional<bool> maybeState)
{
    CClientPed* ped{};
    std::string control{};
    bool        state{};

    if (std::holds_alternative<CClientPed*>(first))
    {
        if (!std::holds_alternative<std::string>(second))
            throw std::invalid_argument("Expected control name at argument 2");

        if (!maybeState.has_value())
            throw std::invalid_argument("Expected state boolean at argument 3");

        ped = std::get<CClientPed*>(first);
        control = std::get<std::string>(second);
        state = maybeState.value();
    }
    else if (std::holds_alternative<std::string>(first))
    {
        if (!std::holds_alternative<bool>(second))
            throw std::invalid_argument("Expected state boolean at argument 2");

        ped = CStaticFunctionDefinitions::GetLocalPlayer();
        control = std::get<std::string>(first);
        state = std::get<bool>(second);
    }
    else
    {
        throw std::invalid_argument("Expected ped or control name at argument 1");
    }

    return CStaticFunctionDefinitions::SetPedControlState(*ped, control, state);
}

int CLuaPedDefs::SetPedDoingGangDriveby(lua_State* luaVM)
{
    // Verify the argument
    CClientEntity*   pEntity = NULL;
    bool             bDoingGangDriveby = false;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pEntity);
    argStream.ReadBool(bDoingGangDriveby);

    if (!argStream.HasErrors())
    {
        if (CStaticFunctionDefinitions::SetPedDoingGangDriveby(*pEntity, bDoingGangDriveby))
        {
            lua_pushboolean(luaVM, true);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

bool CLuaPedDefs::SetPedFightingStyle(CClientEntity* const entity, const unsigned int style)
{
    // Is valid style?
    if (style < 4 || style > 16)
        throw std::invalid_argument("Style can only be between 4 and 16");

    return CStaticFunctionDefinitions::SetPedFightingStyle(*entity, static_cast<unsigned char>(style));
}

int CLuaPedDefs::SetPedLookAt(lua_State* luaVM)
{
    // Verify the argument
    CClientEntity*   pEntity = NULL;
    CVector          vecPosition;
    int              iTime = 3000;
    int              iBlend = 1000;
    CClientEntity*   pTarget = NULL;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pEntity);
    argStream.ReadVector3D(vecPosition);
    argStream.ReadNumber(iTime, 3000);
    if (argStream.NextIsUserData())
    {
        argStream.ReadUserData(pTarget);
    }
    else
    {
        argStream.ReadNumber(iBlend, 1000);
        argStream.ReadUserData(pTarget, NULL);
    }

    if (!argStream.HasErrors())
    {
        if (CStaticFunctionDefinitions::SetPedLookAt(*pEntity, vecPosition, iTime, iBlend, pTarget))
        {
            lua_pushboolean(luaVM, true);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::SetPedHeadless(lua_State* luaVM)
{
    // Verify the argument
    CClientEntity*   pEntity = NULL;
    bool             bHeadless = false;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pEntity);
    argStream.ReadBool(bHeadless);

    if (!argStream.HasErrors())
    {
        if (CStaticFunctionDefinitions::SetPedHeadless(*pEntity, bHeadless))
        {
            lua_pushboolean(luaVM, true);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::SetPedFrozen(lua_State* luaVM)
{
    // Verify the argument
    CClientEntity*   pEntity = NULL;
    bool             bFrozen = false;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pEntity);
    argStream.ReadBool(bFrozen);

    if (!argStream.HasErrors())
    {
        if (CStaticFunctionDefinitions::SetPedFrozen(*pEntity, bFrozen))
        {
            lua_pushboolean(luaVM, true);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::SetPedFootBloodEnabled(lua_State* luaVM)
{
    // Verify the argument
    CClientEntity*   pEntity = NULL;
    bool             bHasFootBlood = false;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pEntity);
    argStream.ReadBool(bHasFootBlood);

    if (!argStream.HasErrors())
    {
        if (CStaticFunctionDefinitions::SetPedFootBloodEnabled(*pEntity, bHasFootBlood))
        {
            lua_pushboolean(luaVM, true);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

bool CLuaPedDefs::SetPedBleeding(CClientPed* ped, bool bleeding)
{
    ped->SetBleeding(bleeding);
    return true;
}

int CLuaPedDefs::SetPedCameraRotation(lua_State* luaVM)
{
    //  bool setPedCameraRotation ( ped thePed, float cameraRotation )
    CClientEntity* pEntity;
    float          fRotation;

    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pEntity);
    argStream.ReadNumber(fRotation);

    if (!argStream.HasErrors())
    {
        if (CStaticFunctionDefinitions::SetPedCameraRotation(*pEntity, fRotation))
        {
            lua_pushboolean(luaVM, true);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::SetPedAimTarget(lua_State* luaVM)
{
    // Verify the argument
    CClientEntity*   pEntity = NULL;
    CVector          vecTarget;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pEntity);
    argStream.ReadVector3D(vecTarget);

    if (!argStream.HasErrors())
    {
        if (CStaticFunctionDefinitions::SetPedAimTarget(*pEntity, vecTarget))
        {
            lua_pushboolean(luaVM, true);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::SetPedStat(lua_State* luaVM)
{
    // Verify the argument
    CClientEntity*   pEntity = NULL;
    unsigned short   usStat = 0;
    float            fValue = 0;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pEntity);
    argStream.ReadNumber(usStat);
    argStream.ReadNumber(fValue);

    if (!argStream.HasErrors())
    {
        // Check the stat and value
        if (usStat > NUM_PLAYER_STATS - 1 || fValue < 0.0f || fValue > 1000.0f)
            argStream.SetCustomError("Stat must be 0 to 342 and value must be 0 to 1000.");
        else if (CStaticFunctionDefinitions::SetPedStat(*pEntity, usStat, fValue))
        {
            lua_pushboolean(luaVM, true);
            return 1;
        }
    }

    if (argStream.HasErrors())
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::KillPed(lua_State* luaVM)
{
    CClientEntity* pEntity = NULL;
    CClientEntity* pKiller = NULL;
    unsigned char  ucKillerWeapon;
    unsigned char  ucBodyPart;
    bool           bStealth;

    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pEntity);
    argStream.ReadUserData(pKiller, NULL);
    argStream.ReadNumber(ucKillerWeapon, 0xFF);
    argStream.ReadNumber(ucBodyPart, 0xFF);
    argStream.ReadBool(bStealth, false);

    if (!argStream.HasErrors())
        if (!pEntity->IsLocalEntity())
            argStream.SetCustomError("This client side function will only work with client created peds");

    if (!argStream.HasErrors())
    {
        if (CStaticFunctionDefinitions::KillPed(*pEntity, pKiller, ucKillerWeapon, ucBodyPart, bStealth))
        {
            lua_pushboolean(luaVM, true);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::SetPedRotation(lua_State* luaVM)
{
    //  setPedRotation ( element ped, float rotation [, bool fixPedRotation = false ] )
    CClientEntity* pEntity;
    float          fRotation;
    bool           bNewWay;

    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pEntity);
    argStream.ReadNumber(fRotation);
    argStream.ReadBool(bNewWay, false);

    if (!argStream.HasErrors())
    {
        if (CStaticFunctionDefinitions::SetPedRotation(*pEntity, fRotation, bNewWay))
        {
            lua_pushboolean(luaVM, true);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::SetPedCanBeKnockedOffBike(lua_State* luaVM)
{
    // Verify the argument
    CClientEntity*   pEntity = NULL;
    bool             bCanBeKnockedOffBike = false;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pEntity);
    argStream.ReadBool(bCanBeKnockedOffBike);

    if (!argStream.HasErrors())
    {
        // Set the new rotation
        if (CStaticFunctionDefinitions::SetPedCanBeKnockedOffBike(*pEntity, bCanBeKnockedOffBike))
        {
            lua_pushboolean(luaVM, true);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::SetPedAnimation(lua_State* luaVM)
{
    // Verify the argument
    CClientEntity* pEntity = NULL;
    bool           bDummy;
    SString        strBlockName = "";
    SString        strAnimName = "";
    int            iTime = -1;
    int            iBlend = 250;
    bool           bLoop = true;
    bool           bUpdatePosition = true;
    bool           bInterruptible = true;
    bool           bFreezeLastFrame = true;
    bool           bTaskToBeRestoredOnAnimEnd = false;

    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pEntity);
    if (argStream.NextIsBool())
        argStream.ReadBool(bDummy);  // Wiki used setPedAnimation(source,false) as an example
    else if (argStream.NextIsNil())
        argStream.m_iIndex++;  // Wiki docs said blockName could be nil
    else
        argStream.ReadString(strBlockName, "");
    argStream.ReadString(strAnimName, "");
    argStream.ReadNumber(iTime, -1);
    argStream.ReadBool(bLoop, true);
    argStream.ReadBool(bUpdatePosition, true);
    argStream.ReadBool(bInterruptible, true);
    argStream.ReadBool(bFreezeLastFrame, true);
    argStream.ReadNumber(iBlend, 250);
    argStream.ReadBool(bTaskToBeRestoredOnAnimEnd, false);

    if (!argStream.HasErrors())
    {
        if (CStaticFunctionDefinitions::SetPedAnimation(*pEntity, strBlockName == "" ? NULL : strBlockName.c_str(),
                                                        strAnimName == "" ? NULL : strAnimName.c_str(), iTime, iBlend, bLoop, bUpdatePosition, bInterruptible,
                                                        bFreezeLastFrame))
        {
            CClientPed* pPed = static_cast<CClientPed*>(pEntity);
            if (pPed->IsDucked())
            {
                pPed->SetTaskTypeToBeRestoredOnAnimEnd((eTaskType)TASK_SIMPLE_DUCK);
            }
            else
            {
                bTaskToBeRestoredOnAnimEnd = false;
            }

            pPed->SetTaskToBeRestoredOnAnimEnd(bTaskToBeRestoredOnAnimEnd);

            if (pPed->HasSyncedAnim())
                pPed->m_animationOverridedByClient = true;

            lua_pushboolean(luaVM, true);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::SetPedAnimationProgress(lua_State* luaVM)
{
    //  bool setPedAnimationProgress ( ped thePed, string animName, float progress )
    CClientEntity* pEntity;
    SString        strAnimName;
    float          fProgress;

    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pEntity);
    argStream.ReadString(strAnimName, "");
    argStream.ReadNumber(fProgress, 0.0f);

    if (!argStream.HasErrors())
    {
        if (CStaticFunctionDefinitions::SetPedAnimationProgress(*pEntity, strAnimName, fProgress))
        {
            lua_pushboolean(luaVM, true);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

float CLuaPedDefs::GetPedAnimationProgress(CClientPed* ped)
{
    CTask*       currentTask = ped->GetTaskManager()->GetActiveTask();
    std::int32_t type = currentTask->GetTaskType();

    // check if animation (task type is 401)
    if (type != 401)
        return -1.0f;

    auto* animation = dynamic_cast<CTaskSimpleRunNamedAnim*>(currentTask);
    if (!animation)
        return -1.0f;

    auto animAssociation = g_pGame->GetAnimManager()->RpAnimBlendClumpGetAssociation(ped->GetClump(), animation->GetAnimName());
    if (!animAssociation)
        return -1.0f;

    return animAssociation->GetCurrentProgress() / animAssociation->GetLength();
}

float CLuaPedDefs::GetPedAnimationSpeed(CClientPed* ped)
{
    CTask*       currentTask = ped->GetTaskManager()->GetActiveTask();
    std::int32_t type = currentTask->GetTaskType();

    // check if animation (task type is 401)
    if (type != 401)
        return -1.0f;

    auto* animation = dynamic_cast<CTaskSimpleRunNamedAnim*>(currentTask);
    if (!animation)
        return -1.0f;

    auto animAssociation = g_pGame->GetAnimManager()->RpAnimBlendClumpGetAssociation(ped->GetClump(), animation->GetAnimName());
    if (!animAssociation)
        return -1.0f;

    return animAssociation->GetCurrentSpeed();
}

float CLuaPedDefs::GetPedAnimationLength(CClientPed* ped)
{
    CTask*       currentTask = ped->GetTaskManager()->GetActiveTask();
    std::int32_t type = currentTask->GetTaskType();

    // check if animation (task type is 401)
    if (type != 401)
        return -1.0f;

    auto* animation = dynamic_cast<CTaskSimpleRunNamedAnim*>(currentTask);
    if (!animation)
        return -1.0f;

    auto animAssociation = g_pGame->GetAnimManager()->RpAnimBlendClumpGetAssociation(ped->GetClump(), animation->GetAnimName());
    if (!animAssociation)
        return -1.0f;

    return animAssociation->GetLength();
}

int CLuaPedDefs::SetPedAnimationSpeed(lua_State* luaVM)
{
    CClientEntity* pEntity;
    SString        strAnimName;
    float          fSpeed;

    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pEntity);
    argStream.ReadString(strAnimName, "");
    argStream.ReadNumber(fSpeed, 1.0f);

    if (!argStream.HasErrors())
    {
        if (!strAnimName.empty() && fSpeed >= 0.0f && fSpeed <= 10.0f)
        {
            if (CStaticFunctionDefinitions::SetPedAnimationSpeed(*pEntity, strAnimName, fSpeed))
            {
                lua_pushboolean(luaVM, true);
                return 1;
            }
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::SetPedMoveAnim(lua_State* luaVM)
{
    // Verify the argument
    CClientEntity*   pEntity = NULL;
    unsigned int     uiMoveAnim = 0;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pEntity);
    argStream.ReadNumber(uiMoveAnim);

    if (!argStream.HasErrors())
    {
        if (CStaticFunctionDefinitions::SetPedMoveAnim(*pEntity, uiMoveAnim))
        {
            lua_pushboolean(luaVM, true);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::SetPedUseNativeWalkingStyle(lua_State* luaVM)
{
    CClientEntity* pEntity = nullptr;
    bool           bEnabled;

    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pEntity);
    argStream.ReadBool(bEnabled);

    if (!argStream.HasErrors())
    {
        if (CStaticFunctionDefinitions::SetPedUseNativeWalkingStyle(*pEntity, bEnabled))
        {
            lua_pushboolean(luaVM, true);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

bool CLuaPedDefs::SetPedArmor(CClientPed* const ped, const float armor)
{
    if (armor < 0.0f)
        throw std::invalid_argument("Armor must be greater than or equal to 0");

    if (armor > 100.0f)
        throw std::invalid_argument("Armor must be less than or equal to 100");

    ped->SetArmor(armor);
    return true;
}

int CLuaPedDefs::SetPedOxygenLevel(lua_State* luaVM)
{
    // Verify the argument
    CClientEntity*   pEntity = NULL;
    float            fOxygen = 0.0f;
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pEntity);
    argStream.ReadNumber(fOxygen);

    if (!argStream.HasErrors())
    {
        if (CStaticFunctionDefinitions::SetPedOxygenLevel(*pEntity, fOxygen))
        {
            lua_pushboolean(luaVM, true);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::CreatePed(lua_State* luaVM)
{
    // Verify the argument
    CClientPed*      pPed = NULL;
    unsigned long    ulModel = 0;
    CVector          vecPosition;
    float            fRotation = 0.0f;
    CScriptArgReader argStream(luaVM);
    argStream.ReadNumber(ulModel);
    argStream.ReadVector3D(vecPosition);
    argStream.ReadNumber(fRotation, 0.0f);

    if (!argStream.HasErrors())
    {
        CLuaMain* pLuaMain = m_pLuaManager->GetVirtualMachine(luaVM);
        if (pLuaMain)
        {
            CResource* pResource = pLuaMain->GetResource();
            if (pResource)
            {
                // Create it
                CClientPed* pPed = CStaticFunctionDefinitions::CreatePed(*pResource, ulModel, vecPosition, fRotation);
                if (pPed)
                {
                    // Return it
                    lua_pushelement(luaVM, pPed);
                    return 1;
                }
            }
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaPedDefs::DetonateSatchels(lua_State* luaVM)
{
    if (CStaticFunctionDefinitions::DetonateSatchels())
    {
        lua_pushboolean(luaVM, true);
        return 1;
    }
    lua_pushboolean(luaVM, false);
    return 1;
}

bool CLuaPedDefs::SetPedEnterVehicle(CClientPed* pPed, std::optional<CClientVehicle*> pOptVehicle,
                                     std::optional<std::variant<bool, unsigned int>> seatOrPassenger)
{
    CClientVehicle*             pVehicle = pOptVehicle.value_or(nullptr);
    bool                        bPassenger = false;
    std::optional<unsigned int> optSeat;

    // Parse third argument: either a bool (passenger flag) or int (seat number)
    if (seatOrPassenger.has_value())
    {
        if (std::holds_alternative<bool>(seatOrPassenger.value()))
        {
            // Third argument is bool - treat as passenger flag
            bPassenger = std::get<bool>(seatOrPassenger.value());
        }
        else if (std::holds_alternative<unsigned int>(seatOrPassenger.value()))
        {
            // Third argument is int - treat as seat number
            optSeat = std::get<unsigned int>(seatOrPassenger.value());
        }
    }

    return pPed->EnterVehicle(pVehicle, bPassenger, optSeat);
}

bool CLuaPedDefs::SetPedExitVehicle(CClientPed* pPed)
{
    return pPed->ExitVehicle();
}

bool CLuaPedDefs::SetPedWeaponShootingRate(CClientPed* ped, int rate)
{
    if (!ped->IsStreamedIn() || ped->IsDead() || (!ped->IsLocalPlayer() && !ped->IsLocalEntity() && !ped->IsSyncing()) || rate < 0 || rate > 255)
        return false;

    // Only the client that owns the ped may change the byte consumed by GTA's
    // native GunControl task. Keeping this separate from setPedShootAt avoids a
    // hidden persistent combat-stat side effect on otherwise generic tasks.
    ped->GetGamePlayer()->SetWeaponShootingRate(static_cast<std::uint8_t>(rate));
    return true;
}

bool CLuaPedDefs::SetPedWeaponAccuracy(CClientPed* ped, int accuracy)
{
    if (!ped->IsStreamedIn() || ped->IsDead() || (!ped->IsLocalPlayer() && !ped->IsLocalEntity() && !ped->IsSyncing()) || accuracy < 0 || accuracy > 255)
        return false;

    // Shot spread is evaluated inside GTA on the client simulating the ped.
    // Restricting this persistent stat to that owner avoids divergent combat.
    ped->GetGamePlayer()->SetWeaponAccuracy(static_cast<std::uint8_t>(accuracy));
    return true;
}

bool CLuaPedDefs::SetPedGoTo(CClientPed* ped, CVector target, std::optional<std::string> movement, std::optional<float> radius,
                             std::optional<float> slowdownRadius, std::optional<int> timeout)
{
    if (!ped->IsStreamedIn() || ped->IsDead() || (!ped->IsLocalPlayer() && !ped->IsLocalEntity() && !ped->IsSyncing()))
        return false;

    const float taskRadius = radius.value_or(0.5f);
    const float taskSlowdownRadius = slowdownRadius.value_or(2.0f);
    const int   taskTimeout = timeout.value_or(-2);
    if (!std::isfinite(target.fX) || !std::isfinite(target.fY) || !std::isfinite(target.fZ) || !std::isfinite(taskRadius) ||
        !std::isfinite(taskSlowdownRadius) || taskRadius <= 0.0f || taskSlowdownRadius < taskRadius || taskTimeout < -2)
    {
        return false;
    }

    int               moveState;
    const std::string taskMovement = movement.value_or("walk");
    if (stricmp(taskMovement.c_str(), "walk") == 0)
        moveState = PedMoveState::PEDMOVE_WALK;
    else if (stricmp(taskMovement.c_str(), "run") == 0)
        moveState = PedMoveState::PEDMOVE_RUN;
    else if (stricmp(taskMovement.c_str(), "sprint") == 0)
        moveState = PedMoveState::PEDMOVE_SPRINT;
    else
        return false;

    auto* task = g_pGame->GetTasks()->CreateTaskComplexGoToPointAndStandStill(moveState, target, taskRadius, taskSlowdownRadius, taskTimeout);
    return DispatchPedScriptCommandTask(ped->GetGamePlayer(), task);
}

bool CLuaPedDefs::SetPedChatWith(CClientPed* ped, CClientPed* partner, bool leadSpeaker, std::optional<bool> updateDirection,
                                 std::optional<bool> conversationEnabled)
{
    if (!ped || !partner || ped == partner || !ped->IsStreamedIn() || !partner->IsStreamedIn() || ped->IsDead() || partner->IsDead() || !ped->GetGamePlayer() ||
        !partner->GetGamePlayer() || (!ped->IsLocalPlayer() && !ped->IsLocalEntity() && !ped->IsSyncing()))
    {
        return false;
    }

    auto* task = g_pGame->GetTasks()->CreateTaskComplexPartnerChatEx(partner->GetGamePlayer(), leadSpeaker, updateDirection.value_or(true),
                                                                     conversationEnabled.value_or(true));
    return DispatchPedScriptCommandTask(ped->GetGamePlayer(), task);
}

bool CLuaPedDefs::SetPedStandStill(CClientPed* ped, std::optional<int> duration)
{
    const int taskDuration = duration.value_or(0);
    if (!ped || !ped->IsStreamedIn() || ped->IsDead() || !ped->GetGamePlayer() || (!ped->IsLocalPlayer() && !ped->IsLocalEntity() && !ped->IsSyncing()) ||
        taskDuration < 0)
    {
        return false;
    }

    auto* task = g_pGame->GetTasks()->CreateTaskSimpleStandStill(taskDuration);
    return DispatchPedScriptCommandTask(ped->GetGamePlayer(), task);
}

bool CLuaPedDefs::SetPedTurnToFace(CClientPed* ped, CClientPed* target)
{
    if (!ped || !target || ped == target || !ped->IsStreamedIn() || !target->IsStreamedIn() || ped->IsDead() || target->IsDead() || !ped->GetGamePlayer() ||
        !target->GetGamePlayer() || (!ped->IsLocalPlayer() && !ped->IsLocalEntity() && !ped->IsSyncing()))
    {
        return false;
    }

    auto* task = g_pGame->GetTasks()->CreateTaskComplexTurnToFaceEntity(target->GetGamePlayer());
    return DispatchPedScriptCommandTask(ped->GetGamePlayer(), task);
}

bool CLuaPedDefs::SetPedGoToOffset(CClientPed* ped, CClientPed* target, std::optional<int> timeout, std::optional<float> radius, std::optional<float> angle,
                                   std::optional<bool> repeatTask)
{
    const int   taskTimeout = timeout.value_or(-1);
    const float taskRadius = radius.value_or(0.5f);
    const float taskAngle = angle.value_or(0.0f);
    if (!ped || !target || ped == target || !ped->IsStreamedIn() || !target->IsStreamedIn() || ped->IsDead() || target->IsDead() || !ped->GetGamePlayer() ||
        !target->GetGamePlayer() || (!ped->IsLocalPlayer() && !ped->IsLocalEntity() && !ped->IsSyncing()) || taskTimeout < -1 || !std::isfinite(taskRadius) ||
        taskRadius <= 0.0f || !std::isfinite(taskAngle))
    {
        return false;
    }

    auto* task =
        g_pGame->GetTasks()->CreateTaskComplexGoToEntityOffset(target->GetGamePlayer(), taskTimeout, taskRadius, taskAngle, repeatTask.value_or(false));
    return DispatchPedScriptCommandTask(ped->GetGamePlayer(), task);
}

bool CLuaPedDefs::SetPedKillOnFoot(CClientPed* ped, CClientPed* target)
{
    if (!ped || !target || ped == target || !ped->IsStreamedIn() || !target->IsStreamedIn() || ped->IsDead() || target->IsDead() || !ped->GetGamePlayer() ||
        !target->GetGamePlayer() || (!ped->IsLocalPlayer() && !ped->IsLocalEntity() && !ped->IsSyncing()))
    {
        return false;
    }

    auto* task = g_pGame->GetTasks()->CreateTaskComplexKillPedOnFoot(target->GetGamePlayer());
    return DispatchPedScriptCommandTask(ped->GetGamePlayer(), task);
}

bool CLuaPedDefs::SetPedWander(CClientPed* ped, std::optional<std::string> movement, std::optional<int> direction, std::optional<bool> wanderSensibly)
{
    if (!ped || !ped->IsStreamedIn() || ped->IsDead() || !ped->GetGamePlayer() || (!ped->IsLocalPlayer() && !ped->IsLocalEntity() && !ped->IsSyncing()))
    {
        return false;
    }

    int               moveState;
    const std::string taskMovement = movement.value_or("walk");
    if (stricmp(taskMovement.c_str(), "walk") == 0)
        moveState = PedMoveState::PEDMOVE_WALK;
    else if (stricmp(taskMovement.c_str(), "run") == 0)
        moveState = PedMoveState::PEDMOVE_RUN;
    else
        return false;

    const int taskDirection = direction.value_or(-1);
    if (taskDirection < -1 || taskDirection > 7)
        return false;

    auto* task = g_pGame->GetTasks()->CreateTaskComplexWanderStandard(moveState, static_cast<char>(taskDirection), wanderSensibly.value_or(true));
    return DispatchPedScriptCommandTask(ped->GetGamePlayer(), task);
}

bool CLuaPedDefs::SetPedScriptedSpeechMuted(CClientPed* ped, bool muted)
{
    if (!ped || !ped->IsStreamedIn() || ped->IsDead() || !ped->GetGamePlayer() || (!ped->IsLocalPlayer() && !ped->IsLocalEntity() && !ped->IsSyncing()))
    {
        return false;
    }

    if (muted)
        ped->GetGamePlayer()->DisableSpeechForScript(false);
    else
        ped->GetGamePlayer()->EnableSpeechForScript();
    return true;
}

bool CLuaPedDefs::SetPedFacialTalk(CClientPed* ped, int duration)
{
    if (duration < 0)
        return false;

    CTaskComplexFacial* facialTask = GetPedFacialTask(ped, false);
    if (!facialTask)
        return false;

    // Opcodes 0967/0968 use request B NONE with a zero duration. Keeping those
    // exact arguments avoids inheriting a stale chained expression.
    facialTask->SetRequest(eFacialExpression::TALKING, duration, eFacialExpression::NONE, 0);
    return true;
}

bool CLuaPedDefs::StopPedFacialTalk(CClientPed* ped)
{
    CTaskComplexFacial* facialTask = GetPedFacialTask(ped, true);
    if (!facialTask)
        return false;

    facialTask->StopAll();
    return true;
}

bool CLuaPedDefs::SetPedShootAt(CClientPed* ped, CVector target, std::optional<int> duration, std::optional<int> burstLength)
{
    if (!ped->IsStreamedIn() || ped->IsDead() || (!ped->IsLocalPlayer() && !ped->IsLocalEntity() && !ped->IsSyncing()))
        return false;

    const int taskDuration = duration.value_or(1000);
    const int taskBurstLength = burstLength.value_or(5);
    // The verified GTA constructor uses a zero XY pair as its "no coordinate"
    // sentinel, so accepting (0, 0, z) would silently discard the Lua target.
    if (!std::isfinite(target.fX) || !std::isfinite(target.fY) || !std::isfinite(target.fZ) || (target.fX == 0.0f && target.fY == 0.0f) ||
        taskBurstLength < 1 || taskBurstLength > 32767)
    {
        return false;
    }

    auto* task = g_pGame->GetTasks()->CreateTaskSimpleGunControl(nullptr, &target, nullptr, static_cast<char>(GCOMMAND_FIREBURST),
                                                                 static_cast<short>(taskBurstLength), taskDuration);
    return DispatchPedScriptCommandTask(ped->GetGamePlayer(), task);
}

int CLuaPedDefs::SetPedTaskSequence(lua_State* luaVM)
{
    auto fail = [luaVM](const SString& reason)
    {
        m_pScriptDebugging->LogError(luaVM, "setPedTaskSequence: %s", reason.c_str());
        lua_pushboolean(luaVM, false);
        return 1;
    };

    auto* ped = dynamic_cast<CClientPed*>(lua_toelement(luaVM, 1));
    if (!ped)
        return fail("argument 1 must be a ped");
    if (!ped->IsStreamedIn() || ped->IsDead() || !ped->GetGamePlayer() || (!ped->IsLocalPlayer() && !ped->IsLocalEntity() && !ped->IsSyncing()))
        return fail("ped is not a live local or synchronized streamed ped");
    if (!lua_istable(luaVM, 2))
        return fail("argument 2 must be a sequence task table");

    const size_t taskCount = lua_objlen(luaVM, 2);
    if (taskCount == 0 || taskCount > 8)
        return fail("sequence must contain between 1 and 8 tasks");

    bool repeat = false;
    if (!lua_isnoneornil(luaVM, 3))
    {
        if (!lua_isboolean(luaVM, 3))
            return fail("argument 3 must be a boolean");
        repeat = lua_toboolean(luaVM, 3) != 0;
    }

    std::array<SPedSequenceTask, 8> descriptions;
    for (size_t i = 0; i < taskCount; ++i)
    {
        lua_rawgeti(luaVM, 2, i + 1);
        if (!lua_istable(luaVM, -1))
        {
            lua_pop(luaVM, 1);
            return fail(SString("task %u must be a table", i + 1));
        }

        const int descriptorIndex = lua_gettop(luaVM);
        lua_getfield(luaVM, descriptorIndex, "task");
        const char* taskName = lua_isstring(luaVM, -1) ? lua_tostring(luaVM, -1) : nullptr;
        if (!taskName)
        {
            lua_pop(luaVM, 2);
            return fail(SString("task %u is missing string field 'task'", i + 1));
        }
        const SString taskNameCopy = taskName;
        lua_pop(luaVM, 1);

        SPedSequenceTask& description = descriptions[i];
        SString           error;
        if (stricmp(taskNameCopy.c_str(), "leave_car") == 0)
        {
            description.type = ePedSequenceTask::LEAVE_CAR;
            lua_getfield(luaVM, descriptorIndex, "vehicle");
            description.vehicle = dynamic_cast<CClientVehicle*>(lua_toelement(luaVM, -1));
            lua_pop(luaVM, 1);
            if (!description.vehicle || !description.vehicle->IsStreamedIn() || !description.vehicle->GetGameVehicle())
                error = "field 'vehicle' must be a streamed vehicle";
        }
        else if (stricmp(taskNameCopy.c_str(), "go_to") == 0)
        {
            description.type = ePedSequenceTask::GO_TO;
            if (ReadSequenceTarget(luaVM, descriptorIndex, description.target, error))
            {
                lua_getfield(luaVM, descriptorIndex, "movement");
                const char* movement = lua_isnoneornil(luaVM, -1) ? "walk" : (lua_isstring(luaVM, -1) ? lua_tostring(luaVM, -1) : nullptr);
                if (!movement)
                    error = "field 'movement' must be walk, run, or sprint";
                else if (stricmp(movement, "walk") == 0)
                    description.moveState = PedMoveState::PEDMOVE_WALK;
                else if (stricmp(movement, "run") == 0)
                    description.moveState = PedMoveState::PEDMOVE_RUN;
                else if (stricmp(movement, "sprint") == 0)
                    description.moveState = PedMoveState::PEDMOVE_SPRINT;
                else
                    error = "field 'movement' must be walk, run, or sprint";
                lua_pop(luaVM, 1);

                double     radius, slowdownRadius, timeout;
                const bool valuesRead = error.empty() && ReadSequenceNumber(luaVM, descriptorIndex, "radius", radius, 0.5, false, error) &&
                                        ReadSequenceNumber(luaVM, descriptorIndex, "slowdownRadius", slowdownRadius, 2.0, false, error) &&
                                        ReadSequenceNumber(luaVM, descriptorIndex, "timeout", timeout, -2.0, false, error);
                if (valuesRead)
                {
                    description.radius = static_cast<float>(radius);
                    description.slowdownRadius = static_cast<float>(slowdownRadius);
                    description.timeout = static_cast<int>(timeout);
                    if (description.radius <= 0.0f || description.slowdownRadius < description.radius || description.timeout < -2 ||
                        timeout != std::floor(timeout))
                        error = "go_to radius, slowdownRadius, or timeout is outside the native range";
                }
            }
        }
        else if (stricmp(taskNameCopy.c_str(), "shoot_at") == 0)
        {
            description.type = ePedSequenceTask::SHOOT_AT;
            if (ReadSequenceTarget(luaVM, descriptorIndex, description.target, error))
            {
                double     duration, burstLength;
                const bool valuesRead = ReadSequenceNumber(luaVM, descriptorIndex, "duration", duration, 1000.0, false, error) &&
                                        ReadSequenceNumber(luaVM, descriptorIndex, "burstLength", burstLength, 5.0, false, error);
                if (valuesRead)
                {
                    description.duration = static_cast<int>(duration);
                    description.burstLength = static_cast<int>(burstLength);
                    if ((description.target.fX == 0.0f && description.target.fY == 0.0f) || description.burstLength < 1 || description.burstLength > 32767 ||
                        duration != std::floor(duration) || burstLength != std::floor(burstLength))
                    {
                        error = "shoot_at target or burstLength is outside the native range";
                    }
                }
            }
        }
        else if (stricmp(taskNameCopy.c_str(), "drive_to") == 0)
        {
            description.type = ePedSequenceTask::DRIVE_TO;
            if (ReadSequenceTarget(luaVM, descriptorIndex, description.target, error))
            {
                double     speed, vehicleModel;
                const bool valuesRead = ReadSequenceNumber(luaVM, descriptorIndex, "speed", speed, 0.0, true, error) &&
                                        ReadSequenceNumber(luaVM, descriptorIndex, "vehicleModel", vehicleModel, -1.0, false, error) &&
                                        ReadSequenceChoice(luaVM, descriptorIndex, "mode", "normal", true, description.driveMode, error) &&
                                        ReadSequenceChoice(luaVM, descriptorIndex, "drivingStyle", "stop_for_cars", false, description.drivingStyle, error);
                if (valuesRead)
                {
                    description.speed = static_cast<float>(speed);
                    description.desiredVehicleModel = static_cast<int>(vehicleModel);
                    if (description.speed < 0.0f || description.speed >= 255.0f || vehicleModel != std::floor(vehicleModel) ||
                        (description.desiredVehicleModel != -1 && !CClientVehicleManager::IsValidModel(description.desiredVehicleModel)))
                    {
                        error = "drive_to speed or vehicleModel is outside the native range";
                    }
                }
            }
        }
        else if (stricmp(taskNameCopy.c_str(), "leave_car_immediately") == 0)
        {
            description.type = ePedSequenceTask::LEAVE_CAR_IMMEDIATELY;
            lua_getfield(luaVM, descriptorIndex, "vehicle");
            description.vehicle = dynamic_cast<CClientVehicle*>(lua_toelement(luaVM, -1));
            lua_pop(luaVM, 1);
            if (!description.vehicle || !description.vehicle->IsStreamedIn() || !description.vehicle->GetGameVehicle())
                error = "field 'vehicle' must be a streamed vehicle";
        }
        else if (stricmp(taskNameCopy.c_str(), "smart_flee") == 0)
        {
            description.type = ePedSequenceTask::SMART_FLEE;
            lua_getfield(luaVM, descriptorIndex, "target");
            description.pedTarget = dynamic_cast<CClientPed*>(lua_toelement(luaVM, -1));
            lua_pop(luaVM, 1);
            if (!description.pedTarget || !description.pedTarget->IsStreamedIn() || !description.pedTarget->GetGamePlayer())
                error = "field 'target' must be a streamed ped";

            double safeDistance, duration;
            if (error.empty() && ReadSequenceNumber(luaVM, descriptorIndex, "safeDistance", safeDistance, 100.0, false, error) &&
                ReadSequenceNumber(luaVM, descriptorIndex, "duration", duration, 1000000.0, false, error))
            {
                description.safeDistance = static_cast<float>(safeDistance);
                description.duration = static_cast<int>(duration);
                if (description.safeDistance <= 0.0f || duration != std::floor(duration) || description.duration < -1)
                    error = "smart_flee safeDistance or duration is outside the native range";
            }
        }
        else if (stricmp(taskNameCopy.c_str(), "die") == 0)
        {
            description.type = ePedSequenceTask::DIE;
        }
        else
        {
            error = SString("unsupported task type '%s'", taskNameCopy.c_str());
        }

        lua_pop(luaVM, 1);
        if (!error.empty())
            return fail(SString("task %u: %s", i + 1, error.c_str()));
    }

    const bool hasDriveTo = std::any_of(descriptions.begin(), descriptions.begin() + taskCount,
                                        [](const SPedSequenceTask& description) { return description.type == ePedSequenceTask::DRIVE_TO; });
    if (hasDriveTo)
    {
        CClientVehicle* vehicle = ped->GetOccupiedVehicle();
        if (!vehicle || !vehicle->IsStreamedIn() || vehicle->IsBlown() || !vehicle->GetGameVehicle() || vehicle->GetOccupant(0) != ped ||
            !OwnsDrivenVehicle(ped, vehicle))
        {
            return fail("drive_to requires the synchronized ped to own and drive its streamed vehicle");
        }
    }

    std::array<CTask*, 8> tasks{};
    for (size_t i = 0; i < taskCount; ++i)
    {
        const SPedSequenceTask& description = descriptions[i];
        switch (description.type)
        {
            case ePedSequenceTask::LEAVE_CAR:
                tasks[i] = g_pGame->GetTasks()->CreateTaskComplexLeaveCar(description.vehicle->GetGameVehicle());
                break;
            case ePedSequenceTask::GO_TO:
                tasks[i] = g_pGame->GetTasks()->CreateTaskComplexGoToPointAndStandStill(description.moveState, description.target, description.radius,
                                                                                        description.slowdownRadius, description.timeout);
                break;
            case ePedSequenceTask::SHOOT_AT:
                tasks[i] = g_pGame->GetTasks()->CreateTaskSimpleGunControl(nullptr, &description.target, nullptr, static_cast<char>(GCOMMAND_FIREBURST),
                                                                           static_cast<short>(description.burstLength), description.duration);
                break;
            case ePedSequenceTask::DRIVE_TO:
                // Null matches SCM sequence placeholders (-1, -1). GTA binds
                // the ped's current vehicle when this child becomes active.
                tasks[i] = g_pGame->GetTasks()->CreateTaskComplexCarDriveToPoint(nullptr, description.target, description.speed, description.driveMode,
                                                                                 description.desiredVehicleModel, -1.0f, description.drivingStyle);
                break;
            case ePedSequenceTask::LEAVE_CAR_IMMEDIATELY:
                tasks[i] = g_pGame->GetTasks()->CreateTaskComplexLeaveCar(description.vehicle->GetGameVehicle(), 0, 0, false, false);
                break;
            case ePedSequenceTask::SMART_FLEE:
                tasks[i] = g_pGame->GetTasks()->CreateTaskComplexSmartFleeEntity(description.pedTarget->GetGamePlayer(), true, description.safeDistance,
                                                                                 description.duration, 1000, 1.0f);
                break;
            case ePedSequenceTask::DIE:
                // Opcode 05BE constructs the unarmed PED/KO_SHOT_FRONT_0 task
                // with blend 4.0 and zero animation speed.
                tasks[i] = g_pGame->GetTasks()->CreateTaskComplexDie(WEAPONTYPE_UNARMED, 0, 15, 4.0f, 0.0f, false, false, 0, false);
                break;
        }

        if (!tasks[i])
        {
            for (size_t j = 0; j < i; ++j)
                tasks[j]->Destroy();
            return fail(SString("native task factory refused task %u", i + 1));
        }
    }

    CTask* sequence = g_pGame->GetTasks()->CreateTaskComplexSequence(tasks.data(), taskCount, repeat);
    lua_pushboolean(luaVM, DispatchPedScriptCommandTask(ped->GetGamePlayer(), sequence));
    return 1;
}

int CLuaPedDefs::GetPedTaskSequenceProgress(CClientPed* ped)
{
    if (!ped || !ped->IsStreamedIn() || !ped->GetGamePlayer())
        return -1;
    return g_pGame->GetTasks()->GetTaskSequenceProgress(ped->GetGamePlayer());
}

bool CLuaPedDefs::SetPedDriveWander(CClientPed* ped, CClientVehicle* vehicle, float speed, std::optional<std::variant<std::string, int>> drivingStyle)
{
    if (!ped->IsStreamedIn() || ped->IsDead() || !vehicle->IsStreamedIn() || vehicle->IsBlown() || !ped->GetGamePlayer() || !vehicle->GetGameVehicle() ||
        (!ped->IsLocalPlayer() && !ped->IsLocalEntity() && !ped->IsSyncing()) || ped->GetOccupiedVehicle() != vehicle || !std::isfinite(speed) ||
        speed < 0.0f || speed > 255.0f)
    {
        return false;
    }

    // Wander changes the vehicle autopilot, not just the passenger ped. Refuse
    // to run it where another client owns the unoccupied vehicle, otherwise its
    // next sync packet would overwrite the native road AI movement.
    if (!OwnsDrivenVehicle(ped, vehicle) || (vehicle->GetOccupant(0) && vehicle->GetOccupant(0) != ped))
        return false;

    int style = DRIVING_STYLE_STOP_FOR_CARS;
    if (drivingStyle.has_value() && !ParseDrivingStyle(*drivingStyle, style))
        return false;

    auto* task = g_pGame->GetTasks()->CreateTaskComplexCarDriveWander(vehicle->GetGameVehicle(), speed, style);
    return DispatchPedScriptCommandTask(ped->GetGamePlayer(), task);
}

bool CLuaPedDefs::SetPedDriveTo(CClientPed* ped, CClientVehicle* vehicle, CVector target, float speed, std::optional<std::variant<std::string, int>> driveMode,
                                std::optional<std::variant<std::string, int>> drivingStyle)
{
    if (!ped->IsStreamedIn() || ped->IsDead() || !vehicle->IsStreamedIn() || vehicle->IsBlown() || !ped->GetGamePlayer() || !vehicle->GetGameVehicle() ||
        (!ped->IsLocalPlayer() && !ped->IsLocalEntity() && !ped->IsSyncing()) || ped->GetOccupiedVehicle() != vehicle || vehicle->GetOccupant(0) != ped ||
        !OwnsDrivenVehicle(ped, vehicle) || !std::isfinite(target.fX) || !std::isfinite(target.fY) || !std::isfinite(target.fZ) || !std::isfinite(speed) ||
        speed < 0.0f || speed >= 255.0f)
    {
        return false;
    }

    int mode = 0;
    int style = DRIVING_STYLE_STOP_FOR_CARS;
    if ((driveMode.has_value() && !ParseDriveMode(*driveMode, mode)) || (drivingStyle.has_value() && !ParseDrivingStyle(*drivingStyle, style)))
        return false;

    auto* task = g_pGame->GetTasks()->CreateTaskComplexCarDriveToPoint(vehicle->GetGameVehicle(), target, speed, mode, -1, -1.0f, style);
    return DispatchPedScriptCommandTask(ped->GetGamePlayer(), task);
}

bool CLuaPedDefs::SetPedDriveBy(CClientPed* ped, std::variant<CClientPed*, CClientVehicle*, CVector> target, float abortRange,
                                std::optional<std::variant<std::string, int>> driveByStyle, std::optional<bool> seatRHS, std::optional<int> frequencyPercentage)
{
    if (!ped || ped->GetType() != CCLIENTPED || !ped->IsStreamedIn() || ped->IsDead() || !ped->GetGamePlayer() ||
        (!ped->IsLocalEntity() && !ped->IsSyncing()) || !std::isfinite(abortRange) || abortRange < 0.0f)
    {
        return false;
    }

    CClientVehicle* occupiedVehicle = ped->GetOccupiedVehicle();
    if (!occupiedVehicle || !occupiedVehicle->IsStreamedIn() || occupiedVehicle->IsBlown() || !occupiedVehicle->GetGameVehicle() ||
        !OwnsDrivenVehicle(ped, occupiedVehicle))
    {
        return false;
    }

    int style = 8;
    if (driveByStyle.has_value() && !ParseDriveByStyle(*driveByStyle, style))
        return false;

    const int frequency = frequencyPercentage.value_or(100);
    if (frequency < 0 || frequency > 100)
        return false;

    CEntity*       nativeTarget = nullptr;
    CVector        targetPosition{};
    const CVector* nativeTargetPosition = nullptr;
    if (std::holds_alternative<CClientPed*>(target))
    {
        CClientPed* targetPed = std::get<CClientPed*>(target);
        if (!targetPed || targetPed == ped || !targetPed->IsStreamedIn() || targetPed->IsDead() || !targetPed->GetGamePlayer())
            return false;
        nativeTarget = targetPed->GetGamePlayer();
    }
    else if (std::holds_alternative<CClientVehicle*>(target))
    {
        CClientVehicle* targetVehicle = std::get<CClientVehicle*>(target);
        if (!targetVehicle || targetVehicle == occupiedVehicle || !targetVehicle->IsStreamedIn() || targetVehicle->IsBlown() ||
            !targetVehicle->GetGameVehicle())
            return false;
        nativeTarget = targetVehicle->GetGameVehicle();
    }
    else
    {
        targetPosition = std::get<CVector>(target);
        if (!std::isfinite(targetPosition.fX) || !std::isfinite(targetPosition.fY) || !std::isfinite(targetPosition.fZ))
            return false;
        nativeTargetPosition = &targetPosition;
    }

    auto* task = g_pGame->GetTasks()->CreateTaskSimpleGangDriveBy(nativeTarget, nativeTargetPosition, abortRange, static_cast<char>(frequency),
                                                                  static_cast<char>(style), seatRHS.value_or(false));
    if (task)
        task->SetFromScriptCommand(true);
    return DispatchPedScriptCommandTask(ped->GetGamePlayer(), task);
}

bool CLuaPedDefs::IsPedMissionActor(CClientPed* ped)
{
    // Player elements share CClientPed internals but must retain GTA's player
    // classification; this policy is only for script-created ped elements.
    return ped && ped->GetType() == CCLIENTPED && ped->IsMissionActor();
}

bool CLuaPedDefs::IsPedStoryProtected(CClientPed* ped)
{
    return ped && ped->GetType() == CCLIENTPED && ped->IsStoryProtected();
}

bool CLuaPedDefs::GetPedSuffersCriticalHits(CClientPed* ped)
{
    return ped && ped->GetType() == CCLIENTPED && ped->GetSuffersCriticalHits();
}

bool CLuaPedDefs::GetPedStayInSamePlace(CClientPed* ped)
{
    return ped && ped->GetType() == CCLIENTPED && ped->GetStayInSamePlace();
}

bool CLuaPedDefs::IsPedNeverTargeted(CClientPed* ped)
{
    return ped && ped->GetType() == CCLIENTPED && ped->IsNeverTargeted();
}

bool CLuaPedDefs::SetPedMissionActor(CClientPed* ped, bool enabled)
{
    if (!ped || ped->GetType() != CCLIENTPED)
        return false;

    // The policy is stored on the MTA element and reapplied after native model
    // recreation, so callers may set it before the ped is streamed in.
    return ped->SetMissionActor(enabled);
}

bool CLuaPedDefs::SetPedStoryProtected(CClientPed* ped, bool enabled)
{
    if (!ped || ped->GetType() != CCLIENTPED)
        return false;

    // Persist the policy on the MTA element because streaming and model swaps
    // replace the underlying GTA CPed instance.
    return ped->SetStoryProtected(enabled);
}

bool CLuaPedDefs::SetPedSuffersCriticalHits(CClientPed* ped, bool suffersCriticalHits)
{
    return ped && ped->GetType() == CCLIENTPED && ped->SetSuffersCriticalHits(suffersCriticalHits);
}

bool CLuaPedDefs::SetPedStayInSamePlace(CClientPed* ped, bool stayInSamePlace)
{
    return ped && ped->GetType() == CCLIENTPED && ped->SetStayInSamePlace(stayInSamePlace);
}

bool CLuaPedDefs::SetPedNeverTargeted(CClientPed* ped, bool neverTargeted)
{
    return ped && ped->GetType() == CCLIENTPED && ped->SetNeverTargeted(neverTargeted);
}

bool CLuaPedDefs::killPedTask(CClientPed* ped, taskType taskType, std::uint8_t taskNumber, std::optional<bool> gracefully)
{
    switch (taskType)
    {
        case taskType::PRIMARY_TASK:
        {
            if (taskNumber == TASK_PRIORITY_DEFAULT)
                throw LuaFunctionError("Killing TASK_PRIORITY_DEFAULT is not allowed");

            if (taskNumber >= TASK_PRIORITY_MAX)
                throw LuaFunctionError("Invalid task slot number");

            return ped->KillTask(taskNumber, gracefully.value_or(true));
        }
        case taskType::SECONDARY_TASK:
        {
            if (taskNumber >= TASK_SECONDARY_MAX)
                throw LuaFunctionError("Invalid task slot number");

            return ped->KillTaskSecondary(taskNumber, gracefully.value_or(true));
        }
        default:
            return false;
    }
}

void CLuaPedDefs::PlayPedVoiceLine(CClientPed* ped, int speechId, std::optional<float> probability)
{
    auto speechContextId = static_cast<ePedSpeechContext>(speechId);
    if (speechContextId < ePedSpeechContext::NOTHING || speechContextId >= ePedSpeechContext::NUM_PED_CONTEXT)
        throw LuaFunctionError("The argument speechId is invalid. The valid range is 0-359.");

    if (probability.has_value() && probability < 0.0f)
        throw LuaFunctionError("The argument probability cannot have a negative value.");

    ped->Say(speechContextId, probability.value_or(1.0f));
}
