/*****************************************************************************
 * PROJECT: Multi Theft Auto: Neon
 * LICENSE: See LICENSE in the top level directory
 *****************************************************************************/
#include "StdInc.h"
#include "CLuaRocketSoccerDefs.h"
#include "../CRocketSoccerSimulation.h"
#include "../CRocketSoccerGamepad.h"
#include <Xinput.h>
#include <exception>
#include <cmath>
#include <cstring>

namespace
{
    constexpr const char* SceneType = "Neon.RocketSoccer.Scene";
    constexpr const char* SceneRegistryKey = "Neon.RocketSoccer.ActiveScene";
    struct SceneHandle
    {
        RocketSoccer::Simulation* simulation;
    };

    SceneHandle* CheckHandle(lua_State* vm)
    {
        auto* handle = static_cast<SceneHandle*>(luaL_checkudata(vm, 1, SceneType));
        if (!handle->simulation)
            luaL_error(vm, "Rocket soccer scene has been destroyed");
        return handle;
    }

    int Collect(lua_State* vm)
    {
        auto* handle = static_cast<SceneHandle*>(luaL_checkudata(vm, 1, SceneType));
        delete handle->simulation;
        handle->simulation = nullptr;
        return 0;
    }

    float ReadAxis(lua_State* vm, const char* name)
    {
        lua_getfield(vm, 3, name);
        float value = lua_isnumber(vm, -1) ? static_cast<float>(lua_tonumber(vm, -1)) : 0;
        lua_pop(vm, 1);
        return value;
    }
    bool ReadButton(lua_State* vm, const char* name)
    {
        lua_getfield(vm, 3, name);
        bool value = lua_isboolean(vm, -1) && lua_toboolean(vm, -1);
        lua_pop(vm, 1);
        return value;
    }
    void PushVector(lua_State* vm, const char* key, const std::array<float, 3>& values)
    {
        lua_createtable(vm, 3, 0);
        for (int i = 0; i < 3; ++i)
        {
            lua_pushnumber(vm, values[i]);
            lua_rawseti(vm, -2, i + 1);
        }
        lua_setfield(vm, -2, key);
    }
    void PushBody(lua_State* vm, const char* key, const RocketSoccer::Body& body)
    {
        lua_createtable(vm, 0, 5);
        PushVector(vm, "position", body.position);
        PushVector(vm, "forward", body.forward);
        PushVector(vm, "right", body.right);
        PushVector(vm, "up", body.up);
        PushVector(vm, "velocity", body.velocity);
        if (key)
            lua_setfield(vm, -2, key);
    }
    void PushNumberField(lua_State* vm, const char* key, float value)
    {
        lua_pushnumber(vm, value);
        lua_setfield(vm, -2, key);
    }
    void PushBooleanField(lua_State* vm, const char* key, bool value)
    {
        lua_pushboolean(vm, value);
        lua_setfield(vm, -2, key);
    }
    void PushDecodedCar(lua_State* vm, const RocketSoccer::Body& body, const float* raw)
    {
        PushBody(vm, nullptr, body);
        PushVector(vm, "angularVelocity", {raw[15], raw[16], raw[17]});
        PushNumberField(vm, "boost", raw[18]);
        PushBooleanField(vm, "grounded", raw[19] == 1);
        PushBooleanField(vm, "supersonic", raw[20] == 1);
        PushBooleanField(vm, "demoed", raw[21] == 1);
        PushBooleanField(vm, "hasFlipOrJump", raw[22] == 1);
        PushBooleanField(vm, "boosting", raw[23] == 1);
        PushBooleanField(vm, "flipping", raw[24] == 1);
        PushVector(vm, "groundNormal", {raw[38], raw[39], raw[40]});

        lua_createtable(vm, 4, 0);
        for (int wheel = 0; wheel < 4; ++wheel)
        {
            const int offset = 26 + wheel * 3;
            lua_createtable(vm, 0, 3);
            PushNumberField(vm, "length", raw[offset]);
            PushNumberField(vm, "steer", raw[offset + 1]);
            PushBooleanField(vm, "contact", raw[offset + 2] == 1);
            lua_rawseti(vm, -2, wheel + 1);
        }
        lua_setfield(vm, -2, "wheels");

        lua_createtable(vm, 0, 11);
        PushNumberField(vm, "flipReset", raw[25]);
        PushNumberField(vm, "jump", raw[41]);
        PushNumberField(vm, "dodge", raw[42]);
        PushNumberField(vm, "doubleJump", raw[43]);
        PushNumberField(vm, "wheelImpact", raw[44]);
        PushNumberField(vm, "wheelImpactSpeed", raw[45]);
        PushNumberField(vm, "ballHit", raw[46]);
        PushNumberField(vm, "ballHitSpeed", raw[47]);
        PushNumberField(vm, "worldImpact", raw[48]);
        PushNumberField(vm, "worldImpactSpeed", raw[49]);
        PushNumberField(vm, "worldSurface", raw[50]);
        lua_setfield(vm, -2, "events");
    }
    int PushCompactSnapshot(lua_State* vm, const RocketSoccer::Snapshot& state)
    {
        lua_createtable(vm, 0, 12);
        const int stateIndex = lua_gettop(vm);
        PushBody(vm, "ball", state.ball);

        lua_createtable(vm, static_cast<int>(state.cars.size()), 0);
        const int carsIndex = lua_gettop(vm);
        for (size_t i = 0; i < state.cars.size(); ++i)
        {
            const float* raw = state.raw.data() + 22 + i * 51;
            PushDecodedCar(vm, state.cars[i], raw);
            if (i == 0)
            {
                lua_pushvalue(vm, -1);
                lua_setfield(vm, stateIndex, "car");
            }
            lua_rawseti(vm, carsIndex, static_cast<int>(i + 1));
        }
        lua_setfield(vm, stateIndex, "cars");

        const float* car = state.raw.data() + 22;
        PushVector(vm, "groundNormal", {car[38], car[39], car[40]});
        PushBooleanField(vm, "supersonic", car[20] == 1);
        PushNumberField(vm, "boost", car[18]);
        PushBooleanField(vm, "grounded", state.grounded);
        PushBooleanField(vm, "boosting", state.boosting);
        PushBooleanField(vm, "ballOnGround", state.ballOnGround);
        lua_pushinteger(vm, state.goalTeam);
        lua_setfield(vm, stateIndex, "goalTeam");
        lua_pushnumber(vm, static_cast<double>(state.ticks));
        lua_setfield(vm, stateIndex, "ticks");

        const int padCount = static_cast<int>(state.raw[3]);
        lua_createtable(vm, padCount, 0);
        for (int i = 0; i < padCount; ++i)
        {
            const int offset = 430 + i * 2;
            lua_createtable(vm, 0, 2);
            PushBooleanField(vm, "active", state.raw[offset] == 1);
            PushNumberField(vm, "cooldown", state.raw[offset + 1]);
            lua_rawseti(vm, -2, i + 1);
        }
        lua_setfield(vm, stateIndex, "pads");
        PushBooleanField(vm, "nativeDecoded", true);
        return 1;
    }
    int PushSnapshot(lua_State* vm, const RocketSoccer::Snapshot& state, bool compact = false)
    {
        if (compact)
            return PushCompactSnapshot(vm, state);
        lua_createtable(vm, 0, 6);
        PushBody(vm, "car", state.car);
        PushBody(vm, "ball", state.ball);
        lua_createtable(vm, static_cast<int>(state.cars.size()), 0);
        for (size_t i = 0; i < state.cars.size(); ++i)
        {
            PushBody(vm, nullptr, state.cars[i]);
            lua_rawseti(vm, -2, static_cast<int>(i + 1));
        }
        lua_setfield(vm, -2, "cars");
        lua_createtable(vm, static_cast<int>(state.raw.size()), 0);
        for (size_t i = 0; i < state.raw.size(); ++i)
        {
            lua_pushnumber(vm, state.raw[i]);
            lua_rawseti(vm, -2, static_cast<int>(i + 1));
        }
        lua_setfield(vm, -2, "raw");
        lua_pushboolean(vm, state.ballOnGround);
        lua_setfield(vm, -2, "ballOnGround");
        lua_pushboolean(vm, state.grounded);
        lua_setfield(vm, -2, "grounded");
        lua_pushboolean(vm, state.boosting);
        lua_setfield(vm, -2, "boosting");
        lua_pushinteger(vm, state.goalTeam);
        lua_setfield(vm, -2, "goalTeam");
        lua_pushnumber(vm, static_cast<double>(state.ticks));
        lua_setfield(vm, -2, "ticks");
        return 1;
    }
}

void CLuaRocketSoccerDefs::LoadFunctions()
{
    CLuaCFunctions::AddFunction("rocketSimGamepad", Gamepad);
    CLuaCFunctions::AddFunction("rocketSimCreate", Create);
    CLuaCFunctions::AddFunction("rocketSimCommand", Command);
    CLuaCFunctions::AddFunction("rocketSimPads", Pads);
    CLuaCFunctions::AddFunction("rocketSimCameraStep", CameraStep);
    CLuaCFunctions::AddFunction("rocketSimStep", Step);
    CLuaCFunctions::AddFunction("rocketSimPredict", Predict);
    CLuaCFunctions::AddFunction("rocketSimRebase", Rebase);
    CLuaCFunctions::AddFunction("rocketSimReset", Reset);
    CLuaCFunctions::AddFunction("rocketSimDestroy", Destroy);
    CLuaCFunctions::AddFunction("rocketSimGetArenaMesh", GetArenaMesh);
}

int CLuaRocketSoccerDefs::Create(lua_State* vm)
{
    // Keep one scene per resource VM, including coroutines. A registry reference
    // makes ownership explicit and lua_close reliably frees it on resource stop.
    lua_getfield(vm, LUA_REGISTRYINDEX, SceneRegistryKey);
    bool exists = !lua_isnil(vm, -1);
    lua_pop(vm, 1);
    if (exists)
        return luaL_error(vm, "This resource already owns a rocket soccer scene");
    if (luaL_newmetatable(vm, SceneType))
    {
        lua_pushcfunction(vm, Collect);
        lua_setfield(vm, -2, "__gc");
        lua_pushliteral(vm, "Rocket soccer scene");
        lua_setfield(vm, -2, "__metatable");
    }
    lua_pop(vm, 1);
    auto* handle = static_cast<SceneHandle*>(lua_newuserdata(vm, sizeof(SceneHandle)));
    handle->simulation = nullptr;
    luaL_getmetatable(vm, SceneType);
    lua_setmetatable(vm, -2);
    // Do not let native exceptions cross Lua/GTA. Report after the catch, since
    // Lua errors longjmp and must not skip the exception object's destruction.
    bool failed = false;
    try
    {
        handle->simulation = new RocketSoccer::Simulation();
    }
    catch (const std::exception& error)
    {
        lua_pushstring(vm, error.what());
        failed = true;
    }
    if (failed)
        return lua_error(vm);
    lua_pushvalue(vm, -1);
    lua_setfield(vm, LUA_REGISTRYINDEX, SceneRegistryKey);
    return 1;
}

int CLuaRocketSoccerDefs::Step(lua_State* vm)
{
    auto* handle = CheckHandle(vm);
    float seconds = static_cast<float>(luaL_checknumber(vm, 2));
    luaL_checktype(vm, 3, LUA_TTABLE);
    RocketSoccer::Controls controls;
    controls.throttle = ReadAxis(vm, "throttle");
    controls.steer = ReadAxis(vm, "steer");
    controls.pitch = ReadAxis(vm, "pitch");
    controls.yaw = ReadAxis(vm, "yaw");
    controls.roll = ReadAxis(vm, "roll");
    controls.jump = ReadButton(vm, "jump");
    controls.boost = ReadButton(vm, "boost");
    controls.handbrake = ReadButton(vm, "handbrake");
    const bool             compact = lua_toboolean(vm, 4) != 0;
    RocketSoccer::Snapshot state;
    bool                   failed = false;
    try
    {
        state = handle->simulation->Step(seconds, controls);
    }
    catch (const std::exception& error)
    {
        lua_pushstring(vm, error.what());
        failed = true;
    }
    if (failed)
        return lua_error(vm);
    return PushSnapshot(vm, state, compact);
}

int CLuaRocketSoccerDefs::Reset(lua_State* vm)
{
    CheckHandle(vm)->simulation->Reset();
    lua_pushboolean(vm, true);
    return 1;
}
int CLuaRocketSoccerDefs::Destroy(lua_State* vm)
{
    // Destruction is idempotent so cleanup can run after a partial start failure.
    Collect(vm);
    lua_getfield(vm, LUA_REGISTRYINDEX, SceneRegistryKey);
    bool isCurrent = lua_rawequal(vm, 1, -1) != 0;
    lua_pop(vm, 1);
    if (isCurrent)
    {
        lua_pushnil(vm);
        lua_setfield(vm, LUA_REGISTRYINDEX, SceneRegistryKey);
    }
    lua_pushboolean(vm, true);
    return 1;
}
int CLuaRocketSoccerDefs::GetArenaMesh(lua_State* vm)
{
    const auto& mesh = RocketSoccer::GetArenaMesh();
    lua_createtable(vm, static_cast<int>(mesh.size()), 0);
    int index = 1;
    for (const auto& triangle : mesh)
    {
        lua_createtable(vm, 10, 0);
        for (int i = 0; i < 9; ++i)
        {
            lua_pushnumber(vm, triangle.vertices[i]);
            lua_rawseti(vm, -2, i + 1);
        }
        lua_pushinteger(vm, triangle.material);
        lua_rawseti(vm, -2, 10);
        lua_rawseti(vm, -2, index++);
    }
    return 1;
}

int CLuaRocketSoccerDefs::Command(lua_State* vm)
{
    auto*       simulation = CheckHandle(vm)->simulation;
    const char* command = luaL_checkstring(vm, 2);
    bool        failed = false;
    int         result = 1;
    try
    {
        if (!std::strcmp(command, "configure"))
            simulation->Configure(lua_toboolean(vm, 3), lua_toboolean(vm, 4));
        else if (!std::strcmp(command, "unlimited"))
            simulation->SetUnlimitedBoost(lua_toboolean(vm, 3));
        else if (!std::strcmp(command, "ball"))
            result = simulation->ControlBall(static_cast<int>(lua_tointeger(vm, 3)));
        else if (!std::strcmp(command, "goal"))
            result = simulation->PollGoal();
        else if (!std::strcmp(command, "resetCamera"))
            simulation->ResetCamera();
        else if (!std::strcmp(command, "kickoff"))
            simulation->Reset(static_cast<int>(lua_tointeger(vm, 3)));
        else if (!std::strcmp(command, "opponent"))
        {
            RocketSoccer::Controls input;
            input.throttle = ReadAxis(vm, "throttle");
            input.steer = ReadAxis(vm, "steer");
            input.pitch = ReadAxis(vm, "pitch");
            input.yaw = ReadAxis(vm, "yaw");
            input.roll = ReadAxis(vm, "roll");
            input.jump = ReadButton(vm, "jump");
            input.boost = ReadButton(vm, "boost");
            input.handbrake = ReadButton(vm, "handbrake");
            simulation->SetControls(1, input);
        }
        else
            result = 0;
    }
    catch (const std::exception& error)
    {
        lua_pushstring(vm, error.what());
        failed = true;
    }
    if (failed)
        return lua_error(vm);
    lua_pushinteger(vm, result);
    return 1;
}
int CLuaRocketSoccerDefs::Pads(lua_State* vm)
{
    auto* simulation = CheckHandle(vm)->simulation;
    bool  failed = false;
    try
    {
        const auto pads = simulation->Pads();
        lua_createtable(vm, static_cast<int>(pads.size()), 0);
        for (size_t i = 0; i < pads.size(); ++i)
        {
            lua_pushnumber(vm, pads[i]);
            lua_rawseti(vm, -2, static_cast<int>(i + 1));
        }
    }
    catch (const std::exception& error)
    {
        lua_pushstring(vm, error.what());
        failed = true;
    }
    return failed ? lua_error(vm) : 1;
}
int CLuaRocketSoccerDefs::CameraStep(lua_State* vm)
{
    auto* simulation = CheckHandle(vm)->simulation;
    luaL_checktype(vm, 2, LUA_TTABLE);
    std::array<double, 28> input{};
    for (int i = 0; i < 28; ++i)
    {
        lua_rawgeti(vm, 2, i + 1);
        input[i] = luaL_checknumber(vm, -1);
        lua_pop(vm, 1);
        if (!std::isfinite(input[i]))
            return luaL_error(vm, "Camera inputs must be finite");
    }
    bool failed = false;
    try
    {
        const auto output = simulation->Camera(input);
        lua_createtable(vm, 42, 0);
        for (int i = 0; i < 42; ++i)
        {
            lua_pushnumber(vm, output[i]);
            lua_rawseti(vm, -2, i + 1);
        }
    }
    catch (const std::exception& error)
    {
        lua_pushstring(vm, error.what());
        failed = true;
    }
    return failed ? lua_error(vm) : 1;
}

int CLuaRocketSoccerDefs::Gamepad(lua_State* vm)
{
    // Keep raw device input independent of GTA's disabled driving controls.
    // Load the OS backend explicitly from System32, without another module ABI.
    using GetState = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
    struct Backend
    {
        HMODULE  module = LoadLibraryExW(L"xinput9_1_0.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        GetState getState = module ? reinterpret_cast<GetState>(GetProcAddress(module, "XInputGetState")) : nullptr;
        ~Backend()
        {
            if (module)
                FreeLibrary(module);
        }
    };
    static Backend backend;
    if (backend.getState)
    {
        for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index)
        {
            XINPUT_STATE state{};
            if (backend.getState(index, &state) != ERROR_SUCCESS)
                continue;
            const auto& p = state.Gamepad;
            const auto  snapshot = RocketSoccer::NormalizeGamepad(p.wButtons, p.bLeftTrigger, p.bRightTrigger, p.sThumbLX, p.sThumbLY, p.sThumbRX, p.sThumbRY);
            lua_createtable(vm, 0, 5);
            lua_pushboolean(vm, true);
            lua_setfield(vm, -2, "connected");
            lua_pushinteger(vm, index);
            lua_setfield(vm, -2, "index");
            lua_pushstring(vm, "XInput");
            lua_setfield(vm, -2, "backend");
            lua_createtable(vm, 4, 0);
            for (int i = 0; i < 4; ++i)
            {
                lua_pushnumber(vm, snapshot.axes[i]);
                lua_rawseti(vm, -2, i + 1);
            }
            lua_setfield(vm, -2, "axes");
            lua_createtable(vm, 17, 0);
            for (int i = 0; i < 17; ++i)
            {
                lua_createtable(vm, 0, 2);
                lua_pushnumber(vm, snapshot.buttons[i]);
                lua_setfield(vm, -2, "value");
                lua_pushboolean(vm, snapshot.buttons[i] > 0.5f);
                lua_setfield(vm, -2, "pressed");
                lua_rawseti(vm, -2, i + 1);
            }
            lua_setfield(vm, -2, "buttons");
            return 1;
        }
    }
    lua_pushboolean(vm, false);
    return 1;
}

int CLuaRocketSoccerDefs::Predict(lua_State* vm)
{
    auto*       handle = CheckHandle(vm);
    size_t      length = 0;
    const char* blob = "";
    if (!lua_isnil(vm, 2) && !(lua_isboolean(vm, 2) && !lua_toboolean(vm, 2)))
        blob = luaL_checklstring(vm, 2, &length);
    luaL_checktype(vm, 3, LUA_TTABLE);
    size_t frameCount = lua_objlen(vm, 3);
    double requestedSlot = luaL_checknumber(vm, 4);
    luaL_checktype(vm, 5, LUA_TTABLE);
    double fraction = luaL_checknumber(vm, 6);
    if (frameCount > 120 || (requestedSlot != 1 && requestedSlot != 2) || length > 512 * 1024 || !std::isfinite(fraction) || fraction < 0 || fraction > 1)
        return luaL_error(vm, "Invalid prediction arguments");
    const int count = static_cast<int>(frameCount), slot = static_cast<int>(requestedSlot) - 1;
    // Validate Lua input before constructing objects that Lua errors would bypass.
    std::array<std::array<float, 8>, 121> values{};
    for (int frame = 0; frame <= count; ++frame)
    {
        if (frame == count)
            lua_pushvalue(vm, 5);
        else
            lua_rawgeti(vm, 3, frame + 1);
        luaL_checktype(vm, -1, LUA_TTABLE);
        for (int axis = 0; axis < 8; ++axis)
        {
            lua_rawgeti(vm, -1, axis + 1);
            double value = lua_isnil(vm, -1) ? 0 : luaL_checknumber(vm, -1);
            if (!std::isfinite(value) || value < -1 || value > 1)
                return luaL_error(vm, "Prediction controls must be finite axes in [-1,1]");
            values[frame][axis] = axis >= 5 ? (value > 0 ? 1.f : 0.f) : static_cast<float>(value);
            lua_pop(vm, 1);
        }
        lua_pop(vm, 1);
    }
    bool failed = false;
    try
    {
        const std::vector<std::array<float, 8>> frames(values.begin(), values.begin() + count);
        const auto                              state = handle->simulation->Predict({blob, length}, frames, slot, values[count], static_cast<float>(fraction));
        PushSnapshot(vm, state);
    }
    catch (const std::exception& error)
    {
        lua_pushstring(vm, error.what());
        failed = true;
    }
    return failed ? lua_error(vm) : 1;
}

int CLuaRocketSoccerDefs::Rebase(lua_State* vm)
{
    auto*       handle = CheckHandle(vm);
    size_t      length = 0;
    const char* data = luaL_checklstring(vm, 2, &length);
    bool        failed = false;
    try
    {
        const auto normalized = handle->simulation->Rebase({data, length});
        lua_pushlstring(vm, normalized.data(), normalized.size());
    }
    catch (const std::exception& error)
    {
        lua_pushstring(vm, error.what());
        failed = true;
    }
    return failed ? lua_error(vm) : 1;
}
