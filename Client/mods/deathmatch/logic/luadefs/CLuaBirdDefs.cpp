/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.x
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/mods/deathmatch/logic/luadefs/CLuaBirdDefs.cpp
 *  PURPOSE:     Lua managed bird definitions
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CLuaBirdDefs.h"
#include "../CClientBird.h"
#include "../CClientBirdManager.h"
#include <cmath>

namespace
{
    CClientBird* ReadBird(CScriptArgReader& argStream)
    {
        CClientEntity* pEntity = nullptr;
        argStream.ReadUserData(pEntity);
        if (argStream.HasErrors() || !pEntity || pEntity->GetTypeHash() != CClientEntity::GetTypeHashFromString("bird"))
            return nullptr;
        return static_cast<CClientBird*>(pEntity);
    }

    void PushVector3(lua_State* luaVM, const CVector& value)
    {
        lua_getglobal(luaVM, "Vector3");
        lua_pushnumber(luaVM, value.fX);
        lua_pushnumber(luaVM, value.fY);
        lua_pushnumber(luaVM, value.fZ);
        lua_call(luaVM, 3, 1);
    }

    bool ReadVectorTable(lua_State* luaVM, int tableIndex, const char* field, CVector& value)
    {
        tableIndex = lua_absindex(luaVM, tableIndex);
        lua_getfield(luaVM, tableIndex, field);
        if (!lua_istable(luaVM, -1))
        {
            lua_pop(luaVM, 1);
            return false;
        }

        auto readComponent = [luaVM](const char* name, int arrayIndex, float& out) {
            lua_getfield(luaVM, -1, name);
            if (!lua_isnumber(luaVM, -1))
            {
                lua_pop(luaVM, 1);
                lua_rawgeti(luaVM, -1, arrayIndex);
            }
            if (!lua_isnumber(luaVM, -1))
            {
                lua_pop(luaVM, 1);
                return false;
            }
            out = static_cast<float>(lua_tonumber(luaVM, -1));
            lua_pop(luaVM, 1);
            return std::isfinite(out);
        };

        const bool ok = readComponent("x", 1, value.fX) && readComponent("y", 2, value.fY) && readComponent("z", 3, value.fZ);
        lua_pop(luaVM, 1);
        return ok;
    }

    bool ReadNumberField(lua_State* luaVM, int tableIndex, const char* field, float& value)
    {
        tableIndex = lua_absindex(luaVM, tableIndex);
        lua_getfield(luaVM, tableIndex, field);
        if (!lua_isnumber(luaVM, -1))
        {
            lua_pop(luaVM, 1);
            return false;
        }
        value = static_cast<float>(lua_tonumber(luaVM, -1));
        lua_pop(luaVM, 1);
        return std::isfinite(value);
    }

    bool ReadBoolField(lua_State* luaVM, int tableIndex, const char* field, bool& value)
    {
        tableIndex = lua_absindex(luaVM, tableIndex);
        lua_getfield(luaVM, tableIndex, field);
        if (!lua_isboolean(luaVM, -1))
        {
            lua_pop(luaVM, 1);
            return false;
        }
        value = lua_toboolean(luaVM, -1) != 0;
        lua_pop(luaVM, 1);
        return true;
    }

    std::uint32_t PackColor(int r, int g, int b)
    {
        r = std::max(0, std::min(r, 255));
        g = std::max(0, std::min(g, 255));
        b = std::max(0, std::min(b, 255));
        return 0xFF000000u | (static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b);
    }

    bool ReadColorField(lua_State* luaVM, int tableIndex, const char* field, std::uint32_t& color)
    {
        tableIndex = lua_absindex(luaVM, tableIndex);
        lua_getfield(luaVM, tableIndex, field);
        if (!lua_istable(luaVM, -1))
        {
            lua_pop(luaVM, 1);
            return false;
        }

        int components[3]{};
        const char* names[3] = {"r", "g", "b"};
        for (int i = 0; i < 3; ++i)
        {
            lua_getfield(luaVM, -1, names[i]);
            if (!lua_isnumber(luaVM, -1))
            {
                lua_pop(luaVM, 1);
                lua_rawgeti(luaVM, -1, i + 1);
            }
            if (!lua_isnumber(luaVM, -1))
            {
                lua_pop(luaVM, 2);
                return false;
            }
            components[i] = static_cast<int>(lua_tonumber(luaVM, -1));
            lua_pop(luaVM, 1);
        }

        lua_pop(luaVM, 1);
        color = PackColor(components[0], components[1], components[2]);
        return true;
    }

    void PushColor(lua_State* luaVM, std::uint32_t color)
    {
        lua_pushnumber(luaVM, (color >> 16) & 0xFF);
        lua_pushnumber(luaVM, (color >> 8) & 0xFF);
        lua_pushnumber(luaVM, color & 0xFF);
    }
}

void CLuaBirdDefs::LoadFunctions()
{
    constexpr static const std::pair<const char*, lua_CFunction> functions[]{
        {"createBird", CreateBird}, {"getBirdVelocity", GetBirdVelocity}, {"setBirdVelocity", SetBirdVelocity},
        {"getBirdTargetVelocity", GetBirdTargetVelocity}, {"setBirdTargetVelocity", SetBirdTargetVelocity},
        {"getBirdSize", GetBirdSize}, {"setBirdSize", SetBirdSize}, {"getBirdColors", GetBirdColors}, {"setBirdColors", SetBirdColors},
        {"getBirdWingBeatTime", GetBirdWingBeatTime}, {"setBirdWingBeatTime", SetBirdWingBeatTime},
        {"getBirdRenderDistance", GetBirdRenderDistance}, {"setBirdRenderDistance", SetBirdRenderDistance},
        {"isBirdCurvedFlightEnabled", IsBirdCurvedFlightEnabled}, {"setBirdCurvedFlightEnabled", SetBirdCurvedFlightEnabled},
        {"isBirdShootable", IsBirdShootable}, {"setBirdShootable", SetBirdShootable},
        {"isBirdMovementEnabled", IsBirdMovementEnabled}, {"setBirdMovementEnabled", SetBirdMovementEnabled},
        {"processBirdGunShot", ProcessBirdGunShot},
    };

    for (const auto& [name, function] : functions)
        CLuaCFunctions::AddFunction(name, function);
}

void CLuaBirdDefs::AddClass(lua_State* luaVM)
{
    lua_newclass(luaVM);
    lua_classfunction(luaVM, "create", "createBird");
    lua_classfunction(luaVM, "getVelocity", "getBirdVelocity");
    lua_classfunction(luaVM, "setVelocity", "setBirdVelocity");
    lua_classfunction(luaVM, "getTargetVelocity", "getBirdTargetVelocity");
    lua_classfunction(luaVM, "setTargetVelocity", "setBirdTargetVelocity");
    lua_classfunction(luaVM, "getColors", "getBirdColors");
    lua_classfunction(luaVM, "setColors", "setBirdColors");
    lua_classvariable(luaVM, "size", "setBirdSize", "getBirdSize");
    lua_classvariable(luaVM, "wingBeatTime", "setBirdWingBeatTime", "getBirdWingBeatTime");
    lua_classvariable(luaVM, "renderDistance", "setBirdRenderDistance", "getBirdRenderDistance");
    lua_classvariable(luaVM, "curvedFlight", "setBirdCurvedFlightEnabled", "isBirdCurvedFlightEnabled");
    lua_classvariable(luaVM, "shootable", "setBirdShootable", "isBirdShootable");
    lua_classvariable(luaVM, "movementEnabled", "setBirdMovementEnabled", "isBirdMovementEnabled");
    lua_registerclass(luaVM, "Bird", "Element");
}

int CLuaBirdDefs::CreateBird(lua_State* luaVM)
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
    CScriptArgReader argStream(luaVM);
    argStream.ReadNumber(x);
    argStream.ReadNumber(y);
    argStream.ReadNumber(z);
    if (argStream.HasErrors() || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
    {
        lua_pushboolean(luaVM, false);
        return 1;
    }

    CVector position(x, y, z);
    float speed = 5.3f;
    float size = 0.55f;
    float renderDistance = 80.0f;
    std::uint32_t bodyColor = PackColor(210, 210, 210);
    std::uint32_t wingColor = PackColor(235, 235, 235);
    std::uint32_t wingBeatTime = 400;
    bool curved = false;
    bool shootable = true;
    bool movement = true;
    CVector velocity(0.0f, speed, 0.0f);
    CVector targetVelocity = velocity;

    if (lua_istable(luaVM, 4))
    {
        lua_getfield(luaVM, 4, "preset");
        if (lua_isstring(luaVM, -1))
        {
            const char* preset = lua_tostring(luaVM, -1);
            if (strcmp(preset, "water") == 0)
            {
                speed = 4.3f; size = 1.0f; renderDistance = 90.0f; wingBeatTime = 820;
                bodyColor = PackColor(130, 130, 130); wingColor = PackColor(220, 220, 220);
            }
            else if (strcmp(preset, "desert") == 0)
            {
                speed = 3.3f; size = 2.2f; renderDistance = 160.0f; wingBeatTime = 1320; curved = true;
                bodyColor = PackColor(70, 38, 25); wingColor = PackColor(120, 45, 28);
            }
        }
        lua_pop(luaVM, 1);

        ReadNumberField(luaVM, 4, "speed", speed);
        ReadNumberField(luaVM, 4, "size", size);
        ReadNumberField(luaVM, 4, "renderDistance", renderDistance);
        float wingBeat = static_cast<float>(wingBeatTime);
        if (ReadNumberField(luaVM, 4, "wingBeatTime", wingBeat) && wingBeat > 0.0f)
            wingBeatTime = static_cast<std::uint32_t>(wingBeat);
        ReadBoolField(luaVM, 4, "curvedFlight", curved);
        ReadBoolField(luaVM, 4, "shootable", shootable);
        ReadBoolField(luaVM, 4, "movementEnabled", movement);
        ReadColorField(luaVM, 4, "bodyColor", bodyColor);
        ReadColorField(luaVM, 4, "wingColor", wingColor);

        velocity = CVector(0.0f, speed, 0.0f);
        targetVelocity = velocity;
        ReadVectorTable(luaVM, 4, "velocity", velocity);
        targetVelocity = velocity;
        ReadVectorTable(luaVM, 4, "targetVelocity", targetVelocity);

        CVector target;
        if (ReadVectorTable(luaVM, 4, "target", target))
        {
            CVector direction(target.fX - x, target.fY - y, target.fZ - z);
            const float length = std::sqrt(direction.fX * direction.fX + direction.fY * direction.fY + direction.fZ * direction.fZ);
            if (length > 0.001f)
            {
                direction.fX = direction.fX / length * speed;
                direction.fY = direction.fY / length * speed;
                direction.fZ = direction.fZ / length * speed;
                velocity = direction;
                targetVelocity = direction;
            }
        }
    }

    CLuaMain* pLuaMain = m_pLuaManager->GetVirtualMachine(luaVM);
    CResource* pResource = pLuaMain ? pLuaMain->GetResource() : nullptr;
    CClientBird* pBird = pResource ? CClientBirdManager::GetSingleton().Create(m_pManager, INVALID_ELEMENT_ID) : nullptr;
    if (pBird && pBird->Initialize(position, velocity, targetVelocity, size, renderDistance, bodyColor, wingColor, wingBeatTime, curved, shootable, movement))
    {
        pBird->SetParent(pResource->GetResourceDynamicEntity());
        if (CElementGroup* pGroup = pResource->GetElementGroup())
            pGroup->Add(pBird);
        lua_pushelement(luaVM, pBird);
        return 1;
    }

    delete pBird;
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaBirdDefs::GetBirdVelocity(lua_State* luaVM)
{
    CScriptArgReader args(luaVM); CClientBird* bird = ReadBird(args);
    if (bird) { PushVector3(luaVM, bird->GetVelocity()); return 1; }
    lua_pushboolean(luaVM, false); return 1;
}

int CLuaBirdDefs::SetBirdVelocity(lua_State* luaVM)
{
    CScriptArgReader args(luaVM); CClientBird* bird = ReadBird(args); CVector value; args.ReadVector3D(value);
    lua_pushboolean(luaVM, bird && !args.HasErrors() && bird->SetVelocity(value)); return 1;
}

int CLuaBirdDefs::GetBirdTargetVelocity(lua_State* luaVM)
{
    CScriptArgReader args(luaVM); CClientBird* bird = ReadBird(args);
    if (bird) { PushVector3(luaVM, bird->GetTargetVelocity()); return 1; }
    lua_pushboolean(luaVM, false); return 1;
}

int CLuaBirdDefs::SetBirdTargetVelocity(lua_State* luaVM)
{
    CScriptArgReader args(luaVM); CClientBird* bird = ReadBird(args); CVector value; args.ReadVector3D(value);
    lua_pushboolean(luaVM, bird && !args.HasErrors() && bird->SetTargetVelocity(value)); return 1;
}

int CLuaBirdDefs::GetBirdSize(lua_State* luaVM)
{
    CScriptArgReader args(luaVM); CClientBird* bird = ReadBird(args); if (bird) lua_pushnumber(luaVM, bird->GetSize()); else lua_pushboolean(luaVM, false); return 1;
}

int CLuaBirdDefs::SetBirdSize(lua_State* luaVM)
{
    CScriptArgReader args(luaVM); CClientBird* bird = ReadBird(args); float value = 0; args.ReadNumber(value);
    lua_pushboolean(luaVM, bird && !args.HasErrors() && bird->SetSize(value)); return 1;
}

int CLuaBirdDefs::GetBirdColors(lua_State* luaVM)
{
    CScriptArgReader args(luaVM); CClientBird* bird = ReadBird(args);
    if (!bird) { lua_pushboolean(luaVM, false); return 1; }
    PushColor(luaVM, bird->GetBodyColor()); PushColor(luaVM, bird->GetWingColor()); return 6;
}

int CLuaBirdDefs::SetBirdColors(lua_State* luaVM)
{
    CScriptArgReader args(luaVM); CClientBird* bird = ReadBird(args); int r,g,b,wr,wg,wb;
    args.ReadNumber(r); args.ReadNumber(g); args.ReadNumber(b); args.ReadNumber(wr); args.ReadNumber(wg); args.ReadNumber(wb);
    if (!bird || args.HasErrors()) { lua_pushboolean(luaVM, false); return 1; }
    bird->SetColors(PackColor(r,g,b), PackColor(wr,wg,wb)); lua_pushboolean(luaVM, true); return 1;
}

int CLuaBirdDefs::GetBirdWingBeatTime(lua_State* luaVM)
{
    CScriptArgReader args(luaVM); CClientBird* bird = ReadBird(args); if (bird) lua_pushnumber(luaVM, bird->GetWingBeatTime()); else lua_pushboolean(luaVM, false); return 1;
}

int CLuaBirdDefs::SetBirdWingBeatTime(lua_State* luaVM)
{
    CScriptArgReader args(luaVM); CClientBird* bird = ReadBird(args); unsigned int value = 0; args.ReadNumber(value);
    lua_pushboolean(luaVM, bird && !args.HasErrors() && bird->SetWingBeatTime(value)); return 1;
}

int CLuaBirdDefs::GetBirdRenderDistance(lua_State* luaVM)
{
    CScriptArgReader args(luaVM); CClientBird* bird = ReadBird(args); if (bird) lua_pushnumber(luaVM, bird->GetRenderDistance()); else lua_pushboolean(luaVM, false); return 1;
}

int CLuaBirdDefs::SetBirdRenderDistance(lua_State* luaVM)
{
    CScriptArgReader args(luaVM); CClientBird* bird = ReadBird(args); float value = 0; args.ReadNumber(value);
    lua_pushboolean(luaVM, bird && !args.HasErrors() && bird->SetRenderDistance(value)); return 1;
}

int CLuaBirdDefs::IsBirdCurvedFlightEnabled(lua_State* luaVM)
{
    CScriptArgReader args(luaVM); CClientBird* bird = ReadBird(args); if (bird) lua_pushboolean(luaVM, bird->IsCurvedFlightEnabled()); else lua_pushboolean(luaVM, false); return 1;
}

int CLuaBirdDefs::SetBirdCurvedFlightEnabled(lua_State* luaVM)
{
    CScriptArgReader args(luaVM); CClientBird* bird = ReadBird(args); bool value = false; args.ReadBool(value);
    if (bird && !args.HasErrors()) { bird->SetCurvedFlightEnabled(value); lua_pushboolean(luaVM, true); } else lua_pushboolean(luaVM, false); return 1;
}

int CLuaBirdDefs::IsBirdShootable(lua_State* luaVM)
{
    CScriptArgReader args(luaVM); CClientBird* bird = ReadBird(args); if (bird) lua_pushboolean(luaVM, bird->IsShootable()); else lua_pushboolean(luaVM, false); return 1;
}

int CLuaBirdDefs::SetBirdShootable(lua_State* luaVM)
{
    CScriptArgReader args(luaVM); CClientBird* bird = ReadBird(args); bool value = false; args.ReadBool(value);
    if (bird && !args.HasErrors()) { bird->SetShootable(value); lua_pushboolean(luaVM, true); } else lua_pushboolean(luaVM, false); return 1;
}

int CLuaBirdDefs::IsBirdMovementEnabled(lua_State* luaVM)
{
    CScriptArgReader args(luaVM); CClientBird* bird = ReadBird(args); if (bird) lua_pushboolean(luaVM, bird->IsMovementEnabled()); else lua_pushboolean(luaVM, false); return 1;
}

int CLuaBirdDefs::SetBirdMovementEnabled(lua_State* luaVM)
{
    CScriptArgReader args(luaVM); CClientBird* bird = ReadBird(args); bool value = false; args.ReadBool(value);
    if (bird && !args.HasErrors()) { bird->SetMovementEnabled(value); lua_pushboolean(luaVM, true); } else lua_pushboolean(luaVM, false); return 1;
}

int CLuaBirdDefs::ProcessBirdGunShot(lua_State* luaVM)
{
    CScriptArgReader args(luaVM); CVector start, end; int weapon = 0;
    args.ReadVector3D(start); args.ReadVector3D(end); args.ReadNumber(weapon, 0);
    CClientPlayer* localPlayer = m_pManager ? m_pManager->GetPlayerManager()->GetLocalPlayer() : nullptr;
    if (!weapon && localPlayer) weapon = static_cast<int>(localPlayer->GetCurrentWeaponType());
    lua_pushboolean(luaVM, !args.HasErrors() && CClientBirdManager::GetSingleton().HandleGunShot(start, end, localPlayer, weapon));
    return 1;
}
