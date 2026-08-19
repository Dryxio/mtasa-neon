/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  PURPOSE:     Managed rope Lua definitions
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CLuaRopeDefs.h"
#include "../CDummy.h"
#include "../CElementGroup.h"
#include "../CResource.h"
#include "../packets/CEntityAddPacket.h"
#include "../CStaticFunctionDefinitions.h"
#include <CScriptArgReader.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace
{
constexpr const char* KEY_DURATION = "__neon_rope_duration";
constexpr const char* KEY_REMAINING = "__neon_rope_remaining";
constexpr const char* KEY_EXPIRY = "__neon_rope_expiry";  // Server-local absolute clock; clients receive only remaining time.
constexpr const char* KEY_TYPE = "__neon_rope_type";
constexpr const char* KEY_HOLDER = "__neon_rope_holder";
constexpr const char* KEY_OFFSET_X = "__neon_rope_offset_x";
constexpr const char* KEY_OFFSET_Y = "__neon_rope_offset_y";
constexpr const char* KEY_OFFSET_Z = "__neon_rope_offset_z";
constexpr const char* KEY_VELOCITY_X = "__neon_rope_velocity_x";
constexpr const char* KEY_VELOCITY_Y = "__neon_rope_velocity_y";
constexpr const char* KEY_VELOCITY_Z = "__neon_rope_velocity_z";
constexpr const char* KEY_FIXED_NODE = "__neon_rope_fixed_node";
constexpr const char* KEY_SIT_ON_GROUND = "__neon_rope_sit_on_ground";
constexpr const char* KEY_WINCH_HEIGHT = "__neon_rope_winch_height";
constexpr const char* KEY_LENGTH = "__neon_rope_length";
constexpr const char* KEY_CARRIED = "__neon_rope_carried";
constexpr const char* KEY_PHYSICS = "__neon_rope_physics";

struct SRopeRecord
{
    CDummy*    pRope{};
    CResource* pResource{};
};

std::unordered_map<CDummy*, SRopeRecord> g_Ropes;

double NowMs()
{
    return static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

bool IsRope(CElement* pElement)
{
    return pElement && pElement->GetType() == CElement::DUMMY && pElement->GetTypeName() == "rope";
}

bool IsPhysicalRopeElement(CElement* pElement)
{
    return pElement && (IS_OBJECT(pElement) || IS_VEHICLE(pElement));
}

void StoreNumber(CElement* pRope, const char* key, double value, ESyncType syncType)
{
    CLuaArgument argument;
    argument.ReadNumber(value);
    pRope->GetCustomDataManager().Set(key, argument, syncType);
}

void SetInitialNumber(CElement* pRope, const char* key, double value)
{
    StoreNumber(pRope, key, value, ESyncType::BROADCAST);
}

void SetInitialBool(CElement* pRope, const char* key, bool value)
{
    CLuaArgument argument;
    argument.ReadBool(value);
    pRope->GetCustomDataManager().Set(key, argument, ESyncType::BROADCAST);
}

void SetInitialElement(CElement* pRope, const char* key, CElement* value)
{
    if (!value)
        return;
    CLuaArgument argument;
    argument.ReadElement(value);
    pRope->GetCustomDataManager().Set(key, argument, ESyncType::BROADCAST);
}

void SetNumber(CElement* pRope, const char* key, double value)
{
    CLuaArgument argument;
    argument.ReadNumber(value);
    CStaticFunctionDefinitions::SetElementData(pRope, key, argument, ESyncType::BROADCAST, std::nullopt);
}

void SetBool(CElement* pRope, const char* key, bool value)
{
    CLuaArgument argument;
    argument.ReadBool(value);
    CStaticFunctionDefinitions::SetElementData(pRope, key, argument, ESyncType::BROADCAST, std::nullopt);
}

void SetElement(CElement* pRope, const char* key, CElement* value)
{
    if (!value)
    {
        CStaticFunctionDefinitions::RemoveElementData(pRope, key);
        return;
    }
    CLuaArgument argument;
    argument.ReadElement(value);
    CStaticFunctionDefinitions::SetElementData(pRope, key, argument, ESyncType::BROADCAST, std::nullopt);
}

bool GetNumber(CElement* pRope, const char* key, double& value)
{
    CLuaArgument* argument = pRope ? pRope->GetCustomData(key, false) : nullptr;
    if (!argument || argument->GetType() != LUA_TNUMBER)
        return false;
    value = argument->GetNumber();
    return true;
}

bool GetBool(CElement* pRope, const char* key, bool& value)
{
    CLuaArgument* argument = pRope ? pRope->GetCustomData(key, false) : nullptr;
    if (!argument || argument->GetType() != LUA_TBOOLEAN)
        return false;
    value = argument->GetBoolean();
    return true;
}

CElement* GetElement(CElement* pRope, const char* key)
{
    CLuaArgument* argument = pRope ? pRope->GetCustomData(key, false) : nullptr;
    return argument ? argument->GetElement() : nullptr;
}

double ReadTableNumber(lua_State* luaVM, int tableIndex, const char* key, double defaultValue)
{
    lua_getfield(luaVM, tableIndex, key);
    const double value = lua_isnumber(luaVM, -1) ? lua_tonumber(luaVM, -1) : defaultValue;
    lua_pop(luaVM, 1);
    return std::isfinite(value) ? value : defaultValue;
}

bool ReadTableBool(lua_State* luaVM, int tableIndex, const char* key, bool defaultValue)
{
    lua_getfield(luaVM, tableIndex, key);
    const bool value = lua_isboolean(luaVM, -1) ? lua_toboolean(luaVM, -1) != 0 : defaultValue;
    lua_pop(luaVM, 1);
    return value;
}

CElement* ReadTableElement(lua_State* luaVM, int tableIndex, const char* key)
{
    lua_getfield(luaVM, tableIndex, key);
    CElement* value = lua_toelement(luaVM, -1);
    lua_pop(luaVM, 1);
    return value;
}

bool ReadVectorTable(lua_State* luaVM, int tableIndex, const char* key, CVector& value)
{
    lua_getfield(luaVM, tableIndex, key);
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

void PushVector3(lua_State* luaVM, const CVector& value)
{
    lua_getglobal(luaVM, "Vector3");
    lua_pushnumber(luaVM, value.fX);
    lua_pushnumber(luaVM, value.fY);
    lua_pushnumber(luaVM, value.fZ);
    lua_call(luaVM, 3, 1);
}

int RopeTypeFromName(const char* name)
{
    if (!name) return 0;
    if (strcmp(name, "winchMagnet") == 0) return 1;
    if (strcmp(name, "harness") == 0) return 2;
    if (strcmp(name, "miniMagnet") == 0) return 3;
    if (strcmp(name, "dockCrane") == 0) return 4;
    if (strcmp(name, "wreckingBall") == 0) return 5;
    if (strcmp(name, "quarryCrane") == 0) return 6;
    if (strcmp(name, "vegasCrane") == 0) return 7;
    if (strcmp(name, "swat") == 0) return 8;
    return 0;
}

const char* RopeTypeName(int type)
{
    switch (type)
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

CElement* ReadRope(lua_State* luaVM)
{
    CElement* rope = lua_toelement(luaVM, 1);
    return IsRope(rope) ? rope : nullptr;
}

void SetInitialVector(CElement* rope, const char* xKey, const char* yKey, const char* zKey, const CVector& value)
{
    SetInitialNumber(rope, xKey, value.fX);
    SetInitialNumber(rope, yKey, value.fY);
    SetInitialNumber(rope, zKey, value.fZ);
}

void SetVector(CElement* rope, const char* xKey, const char* yKey, const char* zKey, const CVector& value)
{
    SetNumber(rope, xKey, value.fX);
    SetNumber(rope, yKey, value.fY);
    SetNumber(rope, zKey, value.fZ);
}

CVector GetVector(CElement* rope, const char* xKey, const char* yKey, const char* zKey)
{
    double x = 0.0, y = 0.0, z = 0.0;
    GetNumber(rope, xKey, x);
    GetNumber(rope, yKey, y);
    GetNumber(rope, zKey, z);
    return CVector(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
}

CDummy* CreateRopeElement(CResource* resource, const CVector& position, int type, double duration, CElement* holder, const CVector& offset,
                          const CVector& velocity, unsigned int fixedNode, bool sitOnGround, double winchHeight, double length, CElement* carried,
                          bool physics, unsigned short dimension = 0, unsigned char interior = 0)
{
    if (!resource)
        return nullptr;

    CDummy* rope = new CDummy(g_pGame->GetGroups(), resource->GetDynamicElementRoot());
    rope->SetTypeName("rope");
    rope->SetPosition(position);
    rope->SetDimension(dimension);
    rope->SetInterior(interior);

    // All client-visible state is present in the EntityAdd snapshot. This is what
    // makes late join and resource restart deterministic without a rope-specific RPC.
    SetInitialNumber(rope, KEY_TYPE, type);
    SetInitialNumber(rope, KEY_DURATION, duration);
    SetInitialNumber(rope, KEY_REMAINING, duration);
    StoreNumber(rope, KEY_EXPIRY, duration > 0.0 ? NowMs() + duration : 0.0, ESyncType::LOCAL);
    SetInitialElement(rope, KEY_HOLDER, holder);
    SetInitialVector(rope, KEY_OFFSET_X, KEY_OFFSET_Y, KEY_OFFSET_Z, offset);
    SetInitialVector(rope, KEY_VELOCITY_X, KEY_VELOCITY_Y, KEY_VELOCITY_Z, velocity);
    SetInitialNumber(rope, KEY_FIXED_NODE, fixedNode);
    SetInitialBool(rope, KEY_SIT_ON_GROUND, sitOnGround);
    SetInitialNumber(rope, KEY_WINCH_HEIGHT, winchHeight);
    SetInitialNumber(rope, KEY_LENGTH, length);
    SetInitialElement(rope, KEY_CARRIED, carried);
    SetInitialBool(rope, KEY_PHYSICS, physics);

    if (CElementGroup* group = resource->GetElementGroup())
        group->Add(rope);

    g_Ropes.emplace(rope, SRopeRecord{rope, resource});

    if (resource->IsClientSynced())
    {
        CEntityAddPacket packet;
        packet.Add(rope);
        g_pGame->GetPlayerManager()->BroadcastOnlyJoined(packet);
    }
    return rope;
}

bool DestroyManagedRope(CDummy* rope)
{
    if (!rope || !IsRope(rope))
        return false;
    g_Ropes.erase(rope);
    return CStaticFunctionDefinitions::DestroyElement(rope);
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
    };
    for (const auto& [name, function] : functions)
        CLuaCFunctions::AddFunction(name, function);
}

void CLuaRopeDefs::OnRopeDestroyed(CDummy* pRope)
{
    g_Ropes.erase(pRope);
}

void CLuaRopeDefs::DoPulse()
{
    const double now = NowMs();
    std::vector<CDummy*> expired;
    for (auto& [rope, record] : g_Ropes)
    {
        if (!rope || rope->IsBeingDeleted())
            continue;

        double duration = 0.0;
        double expiry = 0.0;
        GetNumber(rope, KEY_DURATION, duration);
        GetNumber(rope, KEY_EXPIRY, expiry);
        if (duration <= 0.0)
            continue;

        const double remaining = expiry > 0.0 ? std::max(0.0, expiry - now) : 0.0;
        // Store without broadcasting every pulse. Existing clients count down
        // locally while this fresh value is available to a late join snapshot.
        StoreNumber(rope, KEY_REMAINING, remaining, ESyncType::BROADCAST);
        if (remaining <= 0.0)
            expired.push_back(rope);
    }

    for (CDummy* rope : expired)
        DestroyManagedRope(rope);
}

int CLuaRopeDefs::CreateRope(lua_State* luaVM)
{
    CVector position;
    CScriptArgReader args(luaVM);
    args.ReadVector3D(position);
    if (args.HasErrors())
    {
        m_pScriptDebugging->LogCustom(luaVM, args.GetFullErrorMessage());
        lua_pushboolean(luaVM, false);
        return 1;
    }

    int type = 8;
    double duration = 0.0;
    double winchHeight = 0.5;
    double length = 0.0;
    unsigned int fixedNode = 0;
    bool sitOnGround = false;
    bool physics = true;
    CVector offset;
    CVector velocity;
    CElement* holder = nullptr;
    CElement* carried = nullptr;

    if (lua_istable(luaVM, 4))
    {
        lua_getfield(luaVM, 4, "type");
        if (lua_isstring(luaVM, -1))
            type = RopeTypeFromName(lua_tostring(luaVM, -1));
        lua_pop(luaVM, 1);

        duration = std::max(0.0, ReadTableNumber(luaVM, 4, "duration", duration));
        winchHeight = ReadTableNumber(luaVM, 4, "winchHeight", winchHeight);
        length = std::max(0.0, ReadTableNumber(luaVM, 4, "length", length));
        fixedNode = static_cast<unsigned int>(std::clamp(ReadTableNumber(luaVM, 4, "fixedNode", fixedNode), 0.0, 30.0));
        sitOnGround = ReadTableBool(luaVM, 4, "sitOnGround", sitOnGround);
        physics = ReadTableBool(luaVM, 4, "physics", physics);
        ReadVectorTable(luaVM, 4, "holderOffset", offset);
        ReadVectorTable(luaVM, 4, "topVelocity", velocity);
        holder = ReadTableElement(luaVM, 4, "holder");
        carried = ReadTableElement(luaVM, 4, "carriedElement");
    }

    if (type < 1 || type > 8 || winchHeight < 0.01 || (holder && !IsPhysicalRopeElement(holder)) || (carried && !IsPhysicalRopeElement(carried)))
    {
        lua_pushboolean(luaVM, false);
        return 1;
    }

    CLuaMain* luaMain = m_pLuaManager->GetVirtualMachine(luaVM);
    CResource* resource = luaMain ? luaMain->GetResource() : nullptr;
    CDummy* rope = CreateRopeElement(resource, position, type, duration, holder, offset, velocity, fixedNode, sitOnGround, winchHeight, length, carried, physics);
    if (rope)
        lua_pushelement(luaVM, rope);
    else
        lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaRopeDefs::GetRopeType(lua_State* luaVM)
{
    CElement* rope = ReadRope(luaVM);
    double value = 0.0;
    const char* name = rope && GetNumber(rope, KEY_TYPE, value) ? RopeTypeName(static_cast<int>(value)) : nullptr;
    if (name) lua_pushstring(luaVM, name); else lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaRopeDefs::SetRopeType(lua_State* luaVM)
{
    CElement* rope = ReadRope(luaVM);
    const int type = lua_isstring(luaVM, 2) ? RopeTypeFromName(lua_tostring(luaVM, 2)) : 0;
    const bool ok = rope && type > 0;
    if (ok) SetNumber(rope, KEY_TYPE, type);
    lua_pushboolean(luaVM, ok);
    return 1;
}

#define ROPE_NUMBER_GETTER(Method, Key) \
    int CLuaRopeDefs::Method(lua_State* luaVM) { CElement* rope = ReadRope(luaVM); double value = 0.0; if (rope && GetNumber(rope, Key, value)) lua_pushnumber(luaVM, value); else lua_pushboolean(luaVM, false); return 1; }

ROPE_NUMBER_GETTER(GetRopeDuration, KEY_DURATION)
ROPE_NUMBER_GETTER(GetRopeRemainingTime, KEY_REMAINING)
ROPE_NUMBER_GETTER(GetRopeWinchHeight, KEY_WINCH_HEIGHT)
ROPE_NUMBER_GETTER(GetRopeLength, KEY_LENGTH)
ROPE_NUMBER_GETTER(GetRopeFixedNode, KEY_FIXED_NODE)

int CLuaRopeDefs::SetRopeDuration(lua_State* luaVM)
{
    CElement* rope = ReadRope(luaVM); double value = luaL_optnumber(luaVM, 2, -1.0); const bool ok = rope && value >= 0.0;
    if (ok) { SetNumber(rope, KEY_DURATION, value); SetNumber(rope, KEY_REMAINING, value); StoreNumber(rope, KEY_EXPIRY, value > 0.0 ? NowMs() + value : 0.0, ESyncType::LOCAL); }
    lua_pushboolean(luaVM, ok); return 1;
}

int CLuaRopeDefs::SetRopeRemainingTime(lua_State* luaVM)
{
    CElement* rope = ReadRope(luaVM); double value = luaL_optnumber(luaVM, 2, -1.0); const bool ok = rope && value >= 0.0;
    if (ok) { SetNumber(rope, KEY_REMAINING, value); double duration = 0.0; GetNumber(rope, KEY_DURATION, duration); if (duration <= 0.0 && value > 0.0) SetNumber(rope, KEY_DURATION, value); StoreNumber(rope, KEY_EXPIRY, value > 0.0 ? NowMs() + value : 0.0, ESyncType::LOCAL); }
    lua_pushboolean(luaVM, ok); return 1;
}

int CLuaRopeDefs::GetRopeHolder(lua_State* luaVM)
{
    CElement* rope = ReadRope(luaVM); CElement* value = rope ? GetElement(rope, KEY_HOLDER) : nullptr; if (value) lua_pushelement(luaVM, value); else lua_pushboolean(luaVM, false); return 1;
}

int CLuaRopeDefs::SetRopeHolder(lua_State* luaVM)
{
    CElement* rope = ReadRope(luaVM); CElement* value = lua_isnoneornil(luaVM, 2) ? nullptr : lua_toelement(luaVM, 2);
    const bool ok = rope && (lua_isnoneornil(luaVM, 2) || IsPhysicalRopeElement(value)); if (ok) SetElement(rope, KEY_HOLDER, value); lua_pushboolean(luaVM, ok); return 1;
}

int CLuaRopeDefs::GetRopeHolderOffset(lua_State* luaVM)
{
    CElement* rope = ReadRope(luaVM); if (!rope) { lua_pushboolean(luaVM, false); return 1; } PushVector3(luaVM, GetVector(rope, KEY_OFFSET_X, KEY_OFFSET_Y, KEY_OFFSET_Z)); return 1;
}

int CLuaRopeDefs::SetRopeHolderOffset(lua_State* luaVM)
{
    CElement* rope = nullptr; CVector value; CScriptArgReader args(luaVM); args.ReadUserData(rope); args.ReadVector3D(value);
    const bool ok = !args.HasErrors() && IsRope(rope); if (ok) SetVector(rope, KEY_OFFSET_X, KEY_OFFSET_Y, KEY_OFFSET_Z, value); lua_pushboolean(luaVM, ok); return 1;
}

int CLuaRopeDefs::GetRopeTopVelocity(lua_State* luaVM)
{
    CElement* rope = ReadRope(luaVM); if (!rope) { lua_pushboolean(luaVM, false); return 1; } PushVector3(luaVM, GetVector(rope, KEY_VELOCITY_X, KEY_VELOCITY_Y, KEY_VELOCITY_Z)); return 1;
}

int CLuaRopeDefs::SetRopeTopVelocity(lua_State* luaVM)
{
    CElement* rope = nullptr; CVector value; CScriptArgReader args(luaVM); args.ReadUserData(rope); args.ReadVector3D(value);
    const bool ok = !args.HasErrors() && IsRope(rope); if (ok) SetVector(rope, KEY_VELOCITY_X, KEY_VELOCITY_Y, KEY_VELOCITY_Z, value); lua_pushboolean(luaVM, ok); return 1;
}

int CLuaRopeDefs::SetRopeWinchHeight(lua_State* luaVM)
{
    CElement* rope = ReadRope(luaVM); double value = luaL_optnumber(luaVM, 2, 0.0); const bool ok = rope && value >= 0.01; if (ok) SetNumber(rope, KEY_WINCH_HEIGHT, value); lua_pushboolean(luaVM, ok); return 1;
}

int CLuaRopeDefs::SetRopeLength(lua_State* luaVM)
{
    CElement* rope = ReadRope(luaVM); double value = luaL_optnumber(luaVM, 2, -1.0); const bool ok = rope && value >= 0.0; if (ok) SetNumber(rope, KEY_LENGTH, value); lua_pushboolean(luaVM, ok); return 1;
}

int CLuaRopeDefs::SetRopeFixedNode(lua_State* luaVM)
{
    CElement* rope = ReadRope(luaVM); int value = static_cast<int>(luaL_optinteger(luaVM, 2, -1)); const bool ok = rope && value >= 0 && value <= 30; if (ok) SetNumber(rope, KEY_FIXED_NODE, value); lua_pushboolean(luaVM, ok); return 1;
}

int CLuaRopeDefs::IsRopeOnGround(lua_State* luaVM)
{
    CElement* rope = ReadRope(luaVM); bool value = false; if (rope && GetBool(rope, KEY_SIT_ON_GROUND, value)) lua_pushboolean(luaVM, value); else lua_pushboolean(luaVM, false); return 1;
}

int CLuaRopeDefs::SetRopeOnGround(lua_State* luaVM)
{
    CElement* rope = nullptr; bool value = false; CScriptArgReader args(luaVM); args.ReadUserData(rope); args.ReadBool(value); const bool ok = !args.HasErrors() && IsRope(rope); if (ok) SetBool(rope, KEY_SIT_ON_GROUND, value); lua_pushboolean(luaVM, ok); return 1;
}

int CLuaRopeDefs::IsRopePhysicsEnabled(lua_State* luaVM)
{
    CElement* rope = ReadRope(luaVM); bool value = false; if (rope && GetBool(rope, KEY_PHYSICS, value)) lua_pushboolean(luaVM, value); else lua_pushboolean(luaVM, false); return 1;
}

int CLuaRopeDefs::SetRopePhysicsEnabled(lua_State* luaVM)
{
    CElement* rope = nullptr; bool value = false; CScriptArgReader args(luaVM); args.ReadUserData(rope); args.ReadBool(value); const bool ok = !args.HasErrors() && IsRope(rope); if (ok) SetBool(rope, KEY_PHYSICS, value); lua_pushboolean(luaVM, ok); return 1;
}

int CLuaRopeDefs::GetRopeCarriedElement(lua_State* luaVM)
{
    CElement* rope = ReadRope(luaVM); CElement* value = rope ? GetElement(rope, KEY_CARRIED) : nullptr; if (value) lua_pushelement(luaVM, value); else lua_pushboolean(luaVM, false); return 1;
}

int CLuaRopeDefs::AttachElementToRope(lua_State* luaVM)
{
    CElement* rope = ReadRope(luaVM); CElement* value = lua_toelement(luaVM, 2); const bool ok = rope && IsPhysicalRopeElement(value); if (ok) SetElement(rope, KEY_CARRIED, value); lua_pushboolean(luaVM, ok); return 1;
}

int CLuaRopeDefs::DetachElementFromRope(lua_State* luaVM)
{
    CElement* rope = ReadRope(luaVM); const bool ok = rope != nullptr; if (ok) SetElement(rope, KEY_CARRIED, nullptr); lua_pushboolean(luaVM, ok); return 1;
}
