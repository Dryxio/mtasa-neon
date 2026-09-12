/*****************************************************************************
 * PROJECT: Multi Theft Auto: Neon
 * LICENSE: See LICENSE in the top level directory
 *****************************************************************************/
#include "StdInc.h"
#include "CLuaRocketSoccerDefs.h"
#include "../../../../../vendor/rocketweb/Kernel.h"
#include <algorithm>
#include <cmath>
#include <zlib.h>
#include <vector>
#include <exception>

namespace
{
    constexpr const char* SceneType = "Neon.RocketServer.Scene";
    constexpr const char* SceneKey = "Neon.RocketServer.Scenes";
    constexpr int         MaxScenes = 16;
    struct Scene
    {
        NeonRocketKernel* kernel;
    };
    Scene* Handle(lua_State* vm)
    {
        auto* scene = static_cast<Scene*>(luaL_checkudata(vm, 1, SceneType));
        if (!scene->kernel)
            luaL_error(vm, "Rocket server scene has been destroyed");
        return scene;
    }
    int Collect(lua_State* vm)
    {
        auto* scene = static_cast<Scene*>(luaL_checkudata(vm, 1, SceneType));
        nrk_destroy(scene->kernel);
        scene->kernel = nullptr;
        return 0;
    }
    int State(lua_State* vm, Scene* scene)
    {
        float values[1024];
        int   count = nrk_state(scene->kernel, values, 1024);
        if (!count)
            return luaL_error(vm, "Rocket server state: %s", nrk_error(scene->kernel));
        lua_createtable(vm, count, 0);
        for (int i = 0; i < count; ++i)
        {
            lua_pushnumber(vm, values[i]);
            lua_rawseti(vm, -2, i + 1);
        }
        // Regulation ends on the actual physics contact, including a bounce
        // between snapshots. Height alone cannot distinguish that contact.
        lua_pushboolean(vm, nrk_ball_grounded(scene->kernel) == 1);
        lua_setfield(vm, -2, "ballOnGround");
        return 1;
    }
}
void CLuaRocketSoccerDefs::LoadFunctions()
{
    CLuaCFunctions::AddFunction("rocketServerCreate", Create);
    CLuaCFunctions::AddFunction("rocketServerStep", Step);
    CLuaCFunctions::AddFunction("rocketServerCheckpoint", Checkpoint);
    CLuaCFunctions::AddFunction("rocketServerRebaseCheckpoint", RebaseCheckpoint);
    CLuaCFunctions::AddFunction("rocketServerReset", Reset);
    CLuaCFunctions::AddFunction("rocketServerDestroy", Destroy);
}
int CLuaRocketSoccerDefs::Create(lua_State* vm)
{
    // Coroutines share this bounded registry. Weak values let abandoned handles
    // be collected; resource shutdown also frees every surviving native world.
    lua_getfield(vm, LUA_REGISTRYINDEX, SceneKey);
    if (lua_isnil(vm, -1))
    {
        lua_pop(vm, 1);
        lua_newtable(vm);
        lua_newtable(vm);
        lua_pushliteral(vm, "v");
        lua_setfield(vm, -2, "__mode");
        lua_setmetatable(vm, -2);
        lua_pushvalue(vm, -1);
        lua_setfield(vm, LUA_REGISTRYINDEX, SceneKey);
    }
    int owners = lua_gettop(vm);
    int slot = 0;
    for (int i = 1; i <= MaxScenes; ++i)
    {
        lua_rawgeti(vm, owners, i);
        auto* existing = static_cast<Scene*>(lua_touserdata(vm, -1));
        bool  available = !existing || !existing->kernel;
        lua_pop(vm, 1);
        if (available)
        {
            slot = i;
            break;
        }
    }
    if (!slot)
        return luaL_error(vm, "This resource already owns %d Rocket server scenes", MaxScenes);
    if (luaL_newmetatable(vm, SceneType))
    {
        lua_pushcfunction(vm, Collect);
        lua_setfield(vm, -2, "__gc");
        lua_pushliteral(vm, "Rocket server scene");
        lua_setfield(vm, -2, "__metatable");
    }
    lua_pop(vm, 1);
    auto* scene = static_cast<Scene*>(lua_newuserdata(vm, sizeof(Scene)));
    scene->kernel = nullptr;
    luaL_getmetatable(vm, SceneType);
    lua_setmetatable(vm, -2);
    scene->kernel = nrk_create();
    if (!scene->kernel)
        return luaL_error(vm, "Cannot create Rocket server kernel");
    // Both supplied car bodies use Octane physics. Team order is fixed by the kernel.
    if (!nrk_command(scene->kernel, 0, 0, 1) || !nrk_command(scene->kernel, 5, 0, 0) || !nrk_command(scene->kernel, 2, 0, 0) ||
        !nrk_checkpoint_prepare(scene->kernel))
        return luaL_error(vm, "Cannot configure Rocket server: %s", nrk_error(scene->kernel));
    lua_pushvalue(vm, -1);
    lua_rawseti(vm, owners, slot);
    return 1;
}
int CLuaRocketSoccerDefs::Step(lua_State* vm)
{
    auto*  scene = Handle(vm);
    double requested = luaL_checknumber(vm, 2);
    if (!std::isfinite(requested) || requested < 0 || requested > 12 || requested != std::floor(requested))
        return luaL_error(vm, "Expected 0..12 whole physics ticks");
    luaL_checktype(vm, 3, LUA_TTABLE);
    float allControls[2][8]{};
    for (int car = 0; car < 2; ++car)
    {
        auto& controls = allControls[car];
        lua_rawgeti(vm, 3, car + 1);
        if (lua_istable(vm, -1))
        {
            for (int i = 0; i < 8; ++i)
            {
                lua_rawgeti(vm, -1, i + 1);
                double value = lua_isnumber(vm, -1) ? lua_tonumber(vm, -1) : 0;
                controls[i] = std::isfinite(value) ? static_cast<float>(std::clamp(value, -1.0, 1.0)) : 0;
                if (i >= 5)
                    controls[i] = controls[i] > 0 ? 1.f : 0.f;
                lua_pop(vm, 1);
            }
        }
        lua_pop(vm, 1);
    }
    // Match the client's per-tick input application. The reference step updates
    // transient control/event state, so batching its tick argument changes behavior.
    for (int tick = 0; tick < static_cast<int>(requested); ++tick)
    {
        for (int car = 0; car < 2; ++car)
        {
            if (!nrk_controls(scene->kernel, car, allControls[car]))
                return luaL_error(vm, "Cannot apply Rocket controls");
        }
        if (!nrk_command(scene->kernel, 1, 1, 0))
            return luaL_error(vm, "Rocket server step: %s", nrk_error(scene->kernel));
    }
    return State(vm, scene);
}
int CLuaRocketSoccerDefs::Reset(lua_State* vm)
{
    auto* scene = Handle(vm);
    if (!nrk_command(scene->kernel, 3, 0, 0) || !nrk_command(scene->kernel, 2, 0, 0))
        return luaL_error(vm, "Cannot reset Rocket server scene");
    return State(vm, scene);
}
int CLuaRocketSoccerDefs::Destroy(lua_State* vm)
{
    Collect(vm);
    lua_pushboolean(vm, true);
    return 1;
}

int CLuaRocketSoccerDefs::Checkpoint(lua_State* vm)
{
    auto* scene = Handle(vm);
    // Compress once per published world, then reuse the same blob for both peers.
    // The fixed output bound prevents unbounded allocation at the Lua boundary.
    bool failed = false;
    try
    {
        std::vector<unsigned char> raw(512 * 1024), compressed(512 * 1024);
        size_t                     size = nrk_checkpoint_save(scene->kernel, raw.data(), raw.size());
        uLongf                     length = static_cast<uLongf>(compressed.size());
        failed = !size || compress2(compressed.data(), &length, raw.data(), static_cast<uLong>(size), Z_BEST_SPEED) != Z_OK;
        if (!failed && lua_toboolean(vm, 2))
            failed = !nrk_checkpoint_rebase(scene->kernel, raw.data(), size);
        if (!failed)
            lua_pushlstring(vm, reinterpret_cast<const char*>(compressed.data()), length);
    }
    catch (const std::exception&)
    {
        failed = true;
    }
    return failed ? luaL_error(vm, "Cannot encode Rocket prediction checkpoint") : 1;
}

int CLuaRocketSoccerDefs::RebaseCheckpoint(lua_State* vm)
{
    lua_settop(vm, 1);
    lua_pushboolean(vm, true);
    return Checkpoint(vm);
}
