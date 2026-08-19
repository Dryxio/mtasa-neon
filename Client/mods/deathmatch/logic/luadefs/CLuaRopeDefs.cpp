/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  PURPOSE:     Managed rope Lua definitions
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CLuaRopeDefs.h"
#include "../CClientRopeManager.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
using RopeManager = CClientRopeManager;

bool IsRope(CClientEntity* pElement)
{
    return RopeManager::IsRopeElement(pElement);
}

bool CanMutate(CClientEntity* pRope)
{
    return pRope && pRope->IsLocalEntity();
}

void SetNumber(CClientEntity* pRope, const char* szKey, double dValue)
{
    CLuaArgument argument;
    argument.ReadNumber(dValue);
    pRope->SetCustomData(szKey, argument, false);
}

void SetBool(CClientEntity* pRope, const char* szKey, bool bValue)
{
    CLuaArgument argument;
    argument.ReadBool(bValue);
    pRope->SetCustomData(szKey, argument, false);
}

void SetElement(CClientEntity* pRope, const char* szKey, CClientEntity* pValue)
{
    if (!pValue)
    {
        pRope->DeleteCustomData(szKey);
        return;
    }

    CLuaArgument argument;
    argument.ReadElement(pValue);
    pRope->SetCustomData(szKey, argument, false);
}

bool GetNumber(CClientEntity* pRope, const char* szKey, double& dValue)
{
    CLuaArgument* pArgument = pRope ? pRope->GetCustomData(szKey, false) : nullptr;
    if (!pArgument || pArgument->GetType() != LUA_TNUMBER)
        return false;
    dValue = pArgument->GetNumber();
    return true;
}

bool GetBool(CClientEntity* pRope, const char* szKey, bool& bValue)
{
    CLuaArgument* pArgument = pRope ? pRope->GetCustomData(szKey, false) : nullptr;
    if (!pArgument || pArgument->GetType() != LUA_TBOOLEAN)
        return false;
    bValue = pArgument->GetBoolean();
    return true;
}

CClientEntity* GetElement(CClientEntity* pRope, const char* szKey)
{
    CLuaArgument* pArgument = pRope ? pRope->GetCustomData(szKey, false) : nullptr;
    return pArgument ? pArgument->GetElement() : nullptr;
}

bool ReadNumberField(lua_State* luaVM, int iTable, const char* szField, double& dValue)
{
    lua_getfield(luaVM, iTable, szField);
    if (!lua_isnumber(luaVM, -1))
    {
        lua_pop(luaVM, 1);
        return false;
    }
    dValue = lua_tonumber(luaVM, -1);
    lua_pop(luaVM, 1);
    return std::isfinite(dValue);
}

bool ReadBoolField(lua_State* luaVM, int iTable, const char* szField, bool& bValue)
{
    lua_getfield(luaVM, iTable, szField);
    if (!lua_isboolean(luaVM, -1))
    {
        lua_pop(luaVM, 1);
        return false;
    }
    bValue = lua_toboolean(luaVM, -1) != 0;
    lua_pop(luaVM, 1);
    return true;
}

CClientEntity* ReadElementField(lua_State* luaVM, int iTable, const char* szField)
{
    lua_getfield(luaVM, iTable, szField);
    CClientEntity* pValue = lua_toelement(luaVM, -1);
    lua_pop(luaVM, 1);
    return pValue;
}

bool ReadVectorTable(lua_State* luaVM, int iTable, const char* szField, CVector& vecValue)
{
    lua_getfield(luaVM, iTable, szField);
    if (!lua_istable(luaVM, -1))
    {
        lua_pop(luaVM, 1);
        return false;
    }

    auto readComponent = [luaVM](const char* szName, int iArrayIndex, float& fOut) {
        lua_getfield(luaVM, -1, szName);
        if (!lua_isnumber(luaVM, -1))
        {
            lua_pop(luaVM, 1);
            lua_rawgeti(luaVM, -1, iArrayIndex);
        }
        if (!lua_isnumber(luaVM, -1))
        {
            lua_pop(luaVM, 1);
            return false;
        }
        fOut = static_cast<float>(lua_tonumber(luaVM, -1));
        lua_pop(luaVM, 1);
        return std::isfinite(fOut);
    };

    const bool ok = readComponent("x", 1, vecValue.fX) && readComponent("y", 2, vecValue.fY) && readComponent("z", 3, vecValue.fZ);
    lua_pop(luaVM, 1);
    return ok;
}

void PushVector3(lua_State* luaVM, const CVector& vecValue)
{
    lua_getglobal(luaVM, "Vector3");
    lua_pushnumber(luaVM, vecValue.fX);
    lua_pushnumber(luaVM, vecValue.fY);
    lua_pushnumber(luaVM, vecValue.fZ);
    lua_call(luaVM, 3, 1);
}

int RopeTypeFromName(const char* szName)
{
    if (!szName)
        return 0;
    if (strcmp(szName, "winchMagnet") == 0) return 1;
    if (strcmp(szName, "harness") == 0) return 2;
    if (strcmp(szName, "miniMagnet") == 0) return 3;
    if (strcmp(szName, "dockCrane") == 0) return 4;
    if (strcmp(szName, "wreckingBall") == 0) return 5;
    if (strcmp(szName, "quarryCrane") == 0) return 6;
    if (strcmp(szName, "vegasCrane") == 0) return 7;
    if (strcmp(szName, "swat") == 0) return 8;
    return 0;
}

const char* RopeTypeName(int iType)
{
    switch (iType)
    {
        case 1: return "winchMagnet";
        case 2: return "harness";
        case 3: return "miniMagnet";
        case 4: return "dockCrane";
        case 5: return "wreckingBall";
        case 6: return "quarryCrane";
        case 7: return "vegasCrane";
        case 8: return "swat";
        default: return nullptr;
    }
}

bool IsPhysicalRopeElement(CClientEntity* pElement)
{
    return pElement && (pElement->GetType() == CCLIENTOBJECT || pElement->GetType() == CCLIENTVEHICLE);
}

CClientEntity* ReadRope(lua_State* luaVM)
{
    CClientEntity* pRope = lua_toelement(luaVM, 1);
    return IsRope(pRope) ? pRope : nullptr;
}

void SetVector(CClientEntity* pRope, const char* xKey, const char* yKey, const char* zKey, const CVector& vecValue)
{
    SetNumber(pRope, xKey, vecValue.fX);
    SetNumber(pRope, yKey, vecValue.fY);
    SetNumber(pRope, zKey, vecValue.fZ);
}

CVector GetVector(CClientEntity* pRope, const char* xKey, const char* yKey, const char* zKey)
{
    double x = 0.0, y = 0.0, z = 0.0;
    GetNumber(pRope, xKey, x);
    GetNumber(pRope, yKey, y);
    GetNumber(pRope, zKey, z);
    return CVector(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
}
}  // namespace

void CLuaRopeDefs::LoadFunctions()
{
    constexpr static const std::pair<const char*, lua_CFunction> functions[]{
        {"createRope", CreateRope},
        {"getRopeType", GetRopeType}, {"setRopeType", SetRopeType},
        {"getRopeDuration", GetRopeDuration}, {"setRopeDuration", SetRopeDuration},
        {"getRopeRemainingTime", GetRopeRemainingTime}, {"setRopeRemainingTime", SetRopeRemainingTime},
        {"getRopeHolder", GetRopeHolder}, {"setRopeHolder", SetRopeHolder},
        {"getRopeHolderOffset", GetRopeHolderOffset}, {"setRopeHolderOffset", SetRopeHolderOffset},
        {"getRopeTopVelocity", GetRopeTopVelocity}, {"setRopeTopVelocity", SetRopeTopVelocity},
        {"getRopeWinchHeight", GetRopeWinchHeight}, {"setRopeWinchHeight", SetRopeWinchHeight},
        {"getRopeLength", GetRopeLength}, {"setRopeLength", SetRopeLength},
        {"getRopeFixedNode", GetRopeFixedNode}, {"setRopeFixedNode", SetRopeFixedNode},
        {"isRopeOnGround", IsRopeOnGround}, {"setRopeOnGround", SetRopeOnGround},
        {"isRopePhysicsEnabled", IsRopePhysicsEnabled}, {"setRopePhysicsEnabled", SetRopePhysicsEnabled},
        {"getRopeCarriedElement", GetRopeCarriedElement}, {"attachElementToRope", AttachElementToRope}, {"detachElementFromRope", DetachElementFromRope},
        {"getRopePositionAt", GetRopePositionAt}, {"isRopeActive", IsRopeActive},
    };
    for (const auto& [name, function] : functions)
        CLuaCFunctions::AddFunction(name, function);
}

int CLuaRopeDefs::CreateRope(lua_State* luaVM)
{
    CVector position;
    CScriptArgReader args(luaVM);
    args.ReadVector3D(position);
    if (args.HasErrors())
    {
        lua_pushboolean(luaVM, false);
        return 1;
    }

    int type = 8;
    double duration = 0.0;
    double winchHeight = 0.5;
    double length = 0.0;
    double fixedNode = 0.0;
    bool sitOnGround = false;
    bool physics = true;
    CVector offset;
    CVector velocity;
    CClientEntity* pHolder = nullptr;
    CClientEntity* pCarried = nullptr;

    if (lua_istable(luaVM, 4))
    {
        lua_getfield(luaVM, 4, "type");
        if (lua_isstring(luaVM, -1))
            type = RopeTypeFromName(lua_tostring(luaVM, -1));
        lua_pop(luaVM, 1);
        if (type < 1 || type > 8)
        {
            lua_pushboolean(luaVM, false);
            return 1;
        }

        ReadNumberField(luaVM, 4, "duration", duration);
        ReadNumberField(luaVM, 4, "winchHeight", winchHeight);
        ReadNumberField(luaVM, 4, "length", length);
        ReadNumberField(luaVM, 4, "fixedNode", fixedNode);
        ReadBoolField(luaVM, 4, "sitOnGround", sitOnGround);
        ReadBoolField(luaVM, 4, "physics", physics);
        ReadVectorTable(luaVM, 4, "holderOffset", offset);
        ReadVectorTable(luaVM, 4, "topVelocity", velocity);
        pHolder = ReadElementField(luaVM, 4, "holder");
        pCarried = ReadElementField(luaVM, 4, "carriedElement");
    }

    if (duration < 0.0 || winchHeight < 0.01 || fixedNode < 0.0 || fixedNode > 30.0 || (pHolder && !IsPhysicalRopeElement(pHolder)) ||
        (pCarried && !IsPhysicalRopeElement(pCarried)))
    {
        lua_pushboolean(luaVM, false);
        return 1;
    }

    CResource& resource = lua_getownerresource(luaVM);
    CClientDummy* pRope = CStaticFunctionDefinitions::CreateElement(resource, "rope", "");
    if (!pRope)
    {
        lua_pushboolean(luaVM, false);
        return 1;
    }

    pRope->SetPosition(position);
    SetNumber(pRope, RopeManager::KEY_TYPE, type);
    SetNumber(pRope, RopeManager::KEY_DURATION, duration);
    SetNumber(pRope, RopeManager::KEY_REMAINING, duration);
    SetNumber(pRope, RopeManager::KEY_FIXED_NODE, fixedNode);
    SetBool(pRope, RopeManager::KEY_SIT_ON_GROUND, sitOnGround);
    SetNumber(pRope, RopeManager::KEY_WINCH_HEIGHT, winchHeight);
    SetNumber(pRope, RopeManager::KEY_LENGTH, std::max(0.0, length));
    SetBool(pRope, RopeManager::KEY_PHYSICS, physics);
    SetVector(pRope, RopeManager::KEY_OFFSET_X, RopeManager::KEY_OFFSET_Y, RopeManager::KEY_OFFSET_Z, offset);
    SetVector(pRope, RopeManager::KEY_VELOCITY_X, RopeManager::KEY_VELOCITY_Y, RopeManager::KEY_VELOCITY_Z, velocity);
    SetElement(pRope, RopeManager::KEY_HOLDER, pHolder);
    SetElement(pRope, RopeManager::KEY_CARRIED, pCarried);

    pRope->SetParent(resource.GetResourceDynamicEntity());
    if (CElementGroup* pGroup = resource.GetElementGroup())
        pGroup->Add(pRope);

    lua_pushelement(luaVM, pRope);
    return 1;
}

int CLuaRopeDefs::GetRopeType(lua_State* luaVM)
{
    CClientEntity* pRope = ReadRope(luaVM);
    double value = 0.0;
    const char* name = pRope && GetNumber(pRope, RopeManager::KEY_TYPE, value) ? RopeTypeName(static_cast<int>(value)) : nullptr;
    if (name) lua_pushstring(luaVM, name); else lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaRopeDefs::SetRopeType(lua_State* luaVM)
{
    CClientEntity* pRope = ReadRope(luaVM);
    const char* name = lua_isstring(luaVM, 2) ? lua_tostring(luaVM, 2) : nullptr;
    const int type = RopeTypeFromName(name);
    const bool ok = pRope && CanMutate(pRope) && type > 0;
    if (ok) SetNumber(pRope, RopeManager::KEY_TYPE, type);
    lua_pushboolean(luaVM, ok);
    return 1;
}

#define ROPE_NUMBER_GETTER(Method, Key) \
    int CLuaRopeDefs::Method(lua_State* luaVM) { CClientEntity* pRope = ReadRope(luaVM); double v = 0.0; if (pRope && GetNumber(pRope, Key, v)) lua_pushnumber(luaVM, v); else lua_pushboolean(luaVM, false); return 1; }

ROPE_NUMBER_GETTER(GetRopeDuration, RopeManager::KEY_DURATION)
ROPE_NUMBER_GETTER(GetRopeRemainingTime, RopeManager::KEY_REMAINING)
ROPE_NUMBER_GETTER(GetRopeWinchHeight, RopeManager::KEY_WINCH_HEIGHT)
ROPE_NUMBER_GETTER(GetRopeLength, RopeManager::KEY_LENGTH)
ROPE_NUMBER_GETTER(GetRopeFixedNode, RopeManager::KEY_FIXED_NODE)

int CLuaRopeDefs::SetRopeDuration(lua_State* luaVM)
{
    CClientEntity* pRope = ReadRope(luaVM); double value = luaL_optnumber(luaVM, 2, -1.0);
    const bool ok = pRope && CanMutate(pRope) && value >= 0.0;
    if (ok) { SetNumber(pRope, RopeManager::KEY_DURATION, value); SetNumber(pRope, RopeManager::KEY_REMAINING, value); }
    lua_pushboolean(luaVM, ok); return 1;
}

int CLuaRopeDefs::SetRopeRemainingTime(lua_State* luaVM)
{
    CClientEntity* pRope = ReadRope(luaVM); double value = luaL_optnumber(luaVM, 2, -1.0);
    const bool ok = pRope && CanMutate(pRope) && value >= 0.0;
    if (ok) SetNumber(pRope, RopeManager::KEY_REMAINING, value);
    lua_pushboolean(luaVM, ok); return 1;
}

int CLuaRopeDefs::SetRopeWinchHeight(lua_State* luaVM)
{
    CClientEntity* pRope = ReadRope(luaVM); double value = luaL_optnumber(luaVM, 2, 0.0);
    const bool ok = pRope && CanMutate(pRope) && value >= 0.01;
    if (ok) SetNumber(pRope, RopeManager::KEY_WINCH_HEIGHT, value);
    lua_pushboolean(luaVM, ok); return 1;
}

int CLuaRopeDefs::SetRopeLength(lua_State* luaVM)
{
    CClientEntity* pRope = ReadRope(luaVM); double value = luaL_optnumber(luaVM, 2, 0.0);
    const bool ok = pRope && CanMutate(pRope) && value >= 0.0;
    if (ok) SetNumber(pRope, RopeManager::KEY_LENGTH, value);
    lua_pushboolean(luaVM, ok); return 1;
}

int CLuaRopeDefs::SetRopeFixedNode(lua_State* luaVM)
{
    CClientEntity* pRope = ReadRope(luaVM); int value = static_cast<int>(luaL_optinteger(luaVM, 2, -1));
    const bool ok = pRope && CanMutate(pRope) && value >= 0 && value <= 30;
    if (ok) SetNumber(pRope, RopeManager::KEY_FIXED_NODE, value);
    lua_pushboolean(luaVM, ok); return 1;
}

int CLuaRopeDefs::GetRopeHolder(lua_State* luaVM)
{
    CClientEntity* pRope = ReadRope(luaVM); CClientEntity* pValue = pRope ? GetElement(pRope, RopeManager::KEY_HOLDER) : nullptr;
    if (pValue) lua_pushelement(luaVM, pValue); else lua_pushboolean(luaVM, false); return 1;
}

int CLuaRopeDefs::SetRopeHolder(lua_State* luaVM)
{
    CClientEntity* pRope = ReadRope(luaVM);
    CClientEntity* pHolder = lua_isnoneornil(luaVM, 2) ? nullptr : lua_toelement(luaVM, 2);
    const bool ok = pRope && CanMutate(pRope) && (lua_isnoneornil(luaVM, 2) || IsPhysicalRopeElement(pHolder));
    if (ok) SetElement(pRope, RopeManager::KEY_HOLDER, pHolder);
    lua_pushboolean(luaVM, ok); return 1;
}

int CLuaRopeDefs::GetRopeHolderOffset(lua_State* luaVM)
{
    CClientEntity* pRope = ReadRope(luaVM); if (!pRope) { lua_pushboolean(luaVM, false); return 1; }
    PushVector3(luaVM, GetVector(pRope, RopeManager::KEY_OFFSET_X, RopeManager::KEY_OFFSET_Y, RopeManager::KEY_OFFSET_Z)); return 1;
}

int CLuaRopeDefs::SetRopeHolderOffset(lua_State* luaVM)
{
    CClientEntity* pRope = nullptr; CVector value; CScriptArgReader args(luaVM); args.ReadUserData(pRope); args.ReadVector3D(value);
    const bool ok = !args.HasErrors() && IsRope(pRope) && CanMutate(pRope);
    if (ok) SetVector(pRope, RopeManager::KEY_OFFSET_X, RopeManager::KEY_OFFSET_Y, RopeManager::KEY_OFFSET_Z, value);
    lua_pushboolean(luaVM, ok); return 1;
}

int CLuaRopeDefs::GetRopeTopVelocity(lua_State* luaVM)
{
    CClientEntity* pRope = ReadRope(luaVM); if (!pRope) { lua_pushboolean(luaVM, false); return 1; }
    PushVector3(luaVM, GetVector(pRope, RopeManager::KEY_VELOCITY_X, RopeManager::KEY_VELOCITY_Y, RopeManager::KEY_VELOCITY_Z)); return 1;
}

int CLuaRopeDefs::SetRopeTopVelocity(lua_State* luaVM)
{
    CClientEntity* pRope = nullptr; CVector value; CScriptArgReader args(luaVM); args.ReadUserData(pRope); args.ReadVector3D(value);
    const bool ok = !args.HasErrors() && IsRope(pRope) && CanMutate(pRope);
    if (ok) SetVector(pRope, RopeManager::KEY_VELOCITY_X, RopeManager::KEY_VELOCITY_Y, RopeManager::KEY_VELOCITY_Z, value);
    lua_pushboolean(luaVM, ok); return 1;
}

int CLuaRopeDefs::IsRopeOnGround(lua_State* luaVM)
{
    CClientEntity* pRope = ReadRope(luaVM); bool value = false; if (pRope && GetBool(pRope, RopeManager::KEY_SIT_ON_GROUND, value)) lua_pushboolean(luaVM, value); else lua_pushboolean(luaVM, false); return 1;
}

int CLuaRopeDefs::SetRopeOnGround(lua_State* luaVM)
{
    CClientEntity* pRope = nullptr; bool value = false; CScriptArgReader args(luaVM); args.ReadUserData(pRope); args.ReadBool(value);
    const bool ok = !args.HasErrors() && IsRope(pRope) && CanMutate(pRope); if (ok) SetBool(pRope, RopeManager::KEY_SIT_ON_GROUND, value); lua_pushboolean(luaVM, ok); return 1;
}

int CLuaRopeDefs::IsRopePhysicsEnabled(lua_State* luaVM)
{
    CClientEntity* pRope = ReadRope(luaVM); bool value = false; if (pRope && GetBool(pRope, RopeManager::KEY_PHYSICS, value)) lua_pushboolean(luaVM, value); else lua_pushboolean(luaVM, false); return 1;
}

int CLuaRopeDefs::SetRopePhysicsEnabled(lua_State* luaVM)
{
    CClientEntity* pRope = nullptr; bool value = false; CScriptArgReader args(luaVM); args.ReadUserData(pRope); args.ReadBool(value);
    const bool ok = !args.HasErrors() && IsRope(pRope) && CanMutate(pRope); if (ok) SetBool(pRope, RopeManager::KEY_PHYSICS, value); lua_pushboolean(luaVM, ok); return 1;
}

int CLuaRopeDefs::GetRopeCarriedElement(lua_State* luaVM)
{
    CClientEntity* pRope = ReadRope(luaVM); CClientEntity* pValue = pRope ? GetElement(pRope, RopeManager::KEY_CARRIED) : nullptr;
    if (pValue) lua_pushelement(luaVM, pValue); else lua_pushboolean(luaVM, false); return 1;
}

int CLuaRopeDefs::AttachElementToRope(lua_State* luaVM)
{
    CClientEntity* pRope = ReadRope(luaVM); CClientEntity* pValue = lua_toelement(luaVM, 2);
    const bool ok = pRope && CanMutate(pRope) && IsPhysicalRopeElement(pValue); if (ok) SetElement(pRope, RopeManager::KEY_CARRIED, pValue); lua_pushboolean(luaVM, ok); return 1;
}

int CLuaRopeDefs::DetachElementFromRope(lua_State* luaVM)
{
    CClientEntity* pRope = ReadRope(luaVM); const bool ok = pRope && CanMutate(pRope); if (ok) SetElement(pRope, RopeManager::KEY_CARRIED, nullptr); lua_pushboolean(luaVM, ok); return 1;
}

int CLuaRopeDefs::GetRopePositionAt(lua_State* luaVM)
{
    CClientEntity* pRope = ReadRope(luaVM); const float progress = static_cast<float>(luaL_optnumber(luaVM, 2, -1.0));
    CVector position, velocity;
    if (!pRope || progress < 0.0f || progress > 1.0f || !RopeManager::GetSingleton().GetPositionAt(pRope, progress, position, &velocity))
    {
        lua_pushboolean(luaVM, false); return 1;
    }
    PushVector3(luaVM, position); PushVector3(luaVM, velocity); return 2;
}

int CLuaRopeDefs::IsRopeActive(lua_State* luaVM)
{
    CClientEntity* pRope = ReadRope(luaVM); lua_pushboolean(luaVM, pRope && RopeManager::GetSingleton().IsActive(pRope)); return 1;
}
