/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *               (Shared logic for modifications)
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/shared_logic/luadefs/CLuaCameraDefs.h
 *  PURPOSE:     Lua camera definitions class header
 *
 *****************************************************************************/

#pragma once
#include "CLuaDefs.h"
#include <lua/CLuaMultiReturn.h>
#include <optional>

class CLuaCameraDefs : public CLuaDefs
{
public:
    static void LoadFunctions();
    static void AddClass(lua_State* luaVM);

    static bool SetCameraViewMode(std::optional<unsigned char> usVehicleViewMode, std::optional<unsigned char> usPedViewMode);
    static CLuaMultiReturn<unsigned char, unsigned char> GetCameraViewMode();

    // Cam get funcs
    static std::variant<CClientCamera*, bool>                                      GetCamera();
    static CLuaMultiReturn<float, float, float, float, float, float, float, float> GetCameraMatrix();
    static CMatrix                                                                 OOP_GetCameraMatrix();
    static std::variant<CClientEntity*, bool>                                      GetCameraTarget();
    static unsigned char                                                           GetCameraInterior();
    static std::string                                                             GetCameraGoggleEffect();
    LUA_DECLARE(GetCameraFieldOfView);
    static unsigned char GetCameraDrunkLevel();

    // Cam set funcs
    LUA_DECLARE(SetCameraMatrix);
    LUA_DECLARE(SetCameraTarget);
    LUA_DECLARE(SetCameraInterior);
    LUA_DECLARE(SetCameraFieldOfView);
    LUA_DECLARE(FadeCamera);
    LUA_DECLARE(SetCameraClip);
    LUA_DECLARE(GetCameraClip);
    LUA_DECLARE(SetCameraGoggleEffect);
    static bool SetCameraDrunkLevel(short drunkLevel);

    // Cam do funcs
    static bool ShakeCamera(float radius, std::optional<float> x, std::optional<float> y, std::optional<float> z) noexcept;
    static bool ResetShakeCamera() noexcept;

    static std::variant<unsigned int, bool> AcquireScriptCamera(lua_State* luaVM, std::optional<bool> inhibitControls);
    static bool                             ReleaseScriptCamera(lua_State* luaVM, unsigned int token, std::optional<bool> preserveFade);
    static bool                             IsScriptCameraLeaseActive(lua_State* luaVM, unsigned int token);
    static bool SetScriptCameraFixed(lua_State* luaVM, unsigned int token, CVector position, CVector target, std::optional<CVector> upOffset,
                                     std::optional<bool> jumpCut);
    static bool MoveScriptCamera(lua_State* luaVM, unsigned int token, CVector from, CVector to, int durationMs, std::optional<bool> ease);
    static bool TrackScriptCamera(lua_State* luaVM, unsigned int token, CVector from, CVector to, int durationMs, std::optional<bool> ease);
    static bool SetScriptCameraPersist(lua_State* luaVM, unsigned int token, bool position, bool target);
    static bool ResetScriptCamera(lua_State* luaVM, unsigned int token);
    static bool FadeScriptCamera(lua_State* luaVM, unsigned int token, bool fadeIn, float durationSeconds, std::optional<unsigned char> red,
                                 std::optional<unsigned char> green, std::optional<unsigned char> blue);
    static bool IsScriptCameraFading(lua_State* luaVM, unsigned int token);
    static bool IsScriptCameraMoveRunning(lua_State* luaVM, unsigned int token);
    static bool IsScriptCameraTrackRunning(lua_State* luaVM, unsigned int token);
    static bool SetScriptCameraWidescreen(lua_State* luaVM, unsigned int token, bool enabled);
    static bool SetScriptCameraNearClip(lua_State* luaVM, unsigned int token, std::variant<bool, float> distance);

    static std::variant<unsigned int, bool> RequestFileCutscene(lua_State* luaVM, std::string name);
    static bool                             IsFileCutsceneLeaseActive(lua_State* luaVM, unsigned int token);
    static bool                             IsFileCutsceneLoaded(lua_State* luaVM, unsigned int token);
    static bool                             StartFileCutscene(lua_State* luaVM, unsigned int token);
    static bool FadeFileCutscene(lua_State* luaVM, unsigned int token, bool fadeIn, float durationSeconds, std::optional<unsigned char> red,
                                 std::optional<unsigned char> green, std::optional<unsigned char> blue);
    static bool IsFileCutsceneFading(lua_State* luaVM, unsigned int token);
    static bool IsFileCutsceneFinished(lua_State* luaVM, unsigned int token);
    static bool IsFileCutsceneSkipInputPressed(lua_State* luaVM, unsigned int token);
    static bool WasFileCutsceneSkipped(lua_State* luaVM, unsigned int token);
    static bool SkipFileCutscene(lua_State* luaVM, unsigned int token);
    static bool ReleaseFileCutscene(lua_State* luaVM, unsigned int token, std::optional<bool> preserveFade);

    // For OOP only
    LUA_DECLARE(OOP_GetCameraPosition);
    LUA_DECLARE(OOP_SetCameraPosition);
    LUA_DECLARE(OOP_GetCameraRotation);
    LUA_DECLARE(OOP_SetCameraRotation);

    static const SString& GetElementType();
};
