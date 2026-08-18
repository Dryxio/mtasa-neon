/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.x
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/mods/deathmatch/logic/luadefs/CLuaBreakEffectDefs.cpp
 *  PURPOSE:     Lua definitions for generic managed object fracture effects
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CLuaBreakEffectDefs.h"
#include "../CClientBreakEffect.h"
#include "../CClientBreakEffectManager.h"
#include "../CClientGame.h"
#include "../CClientObject.h"
#include "../CClientObjectManager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace
{
    struct SObjectBreakProfile
    {
        CClientObject*       object = nullptr;
        unsigned short       model = 0;
        float                maxHealth = 1000.0f;
        float                health = 1000.0f;
        bool                 native = true;
        bool                 hasDamageMultiplier = false;
        float                damageMultiplier = 1.0f;
        float                instantBreakThreshold = 150.0f;
        bool                 hasThresholdOverride = false;
        SManagedBreakOptions fracture;

        unsigned long long lastVehicleDamageTick = 0;
        CEntitySAInterface* lastVehicleAttacker = nullptr;
        CVector             lastImpactPosition;
        CVector             lastImpactVelocity;
        bool                hasLastImpactPosition = false;
        bool                hasLastImpactVelocity = false;
    };

    std::vector<SObjectBreakProfile> g_ObjectBreakProfiles;

    CClientBreakEffect* ReadBreakEffect(CScriptArgReader& args)
    {
        CClientEntity* entity = nullptr;
        args.ReadUserData(entity);
        if (args.HasErrors() || !entity || entity->GetTypeHash() != CClientEntity::GetTypeHashFromString("break-effect"))
            return nullptr;
        return static_cast<CClientBreakEffect*>(entity);
    }

    bool ReadNumberField(lua_State* luaVM, int tableIndex, const char* name, float& value)
    {
        tableIndex = lua_absindex(luaVM, tableIndex);
        lua_getfield(luaVM, tableIndex, name);
        if (!lua_isnumber(luaVM, -1))
        {
            lua_pop(luaVM, 1);
            return false;
        }
        value = static_cast<float>(lua_tonumber(luaVM, -1));
        lua_pop(luaVM, 1);
        return std::isfinite(value);
    }

    bool ReadIntegerField(lua_State* luaVM, int tableIndex, const char* name, std::uint32_t& value)
    {
        tableIndex = lua_absindex(luaVM, tableIndex);
        lua_getfield(luaVM, tableIndex, name);
        if (!lua_isnumber(luaVM, -1))
        {
            lua_pop(luaVM, 1);
            return false;
        }
        const lua_Number number = lua_tonumber(luaVM, -1);
        lua_pop(luaVM, 1);
        if (!std::isfinite(static_cast<double>(number)) || number < 0.0 || number > 4294967295.0)
            return false;
        value = static_cast<std::uint32_t>(number);
        return true;
    }

    bool ReadBoolField(lua_State* luaVM, int tableIndex, const char* name, bool& value)
    {
        tableIndex = lua_absindex(luaVM, tableIndex);
        lua_getfield(luaVM, tableIndex, name);
        if (!lua_isboolean(luaVM, -1))
        {
            lua_pop(luaVM, 1);
            return false;
        }
        value = lua_toboolean(luaVM, -1) != 0;
        lua_pop(luaVM, 1);
        return true;
    }

    bool HasField(lua_State* luaVM, int tableIndex, const char* name)
    {
        tableIndex = lua_absindex(luaVM, tableIndex);
        lua_getfield(luaVM, tableIndex, name);
        const bool present = !lua_isnil(luaVM, -1);
        lua_pop(luaVM, 1);
        return present;
    }

    bool ReadVectorField(lua_State* luaVM, int tableIndex, const char* name, CVector& value)
    {
        tableIndex = lua_absindex(luaVM, tableIndex);
        lua_getfield(luaVM, tableIndex, name);
        if (!lua_istable(luaVM, -1))
        {
            lua_pop(luaVM, 1);
            return false;
        }

        auto readComponent = [luaVM](const char* component, int arrayIndex, float& output)
        {
            lua_getfield(luaVM, -1, component);
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
            output = static_cast<float>(lua_tonumber(luaVM, -1));
            lua_pop(luaVM, 1);
            return std::isfinite(output);
        };

        const bool ok = readComponent("x", 1, value.fX) && readComponent("y", 2, value.fY) && readComponent("z", 3, value.fZ);
        lua_pop(luaVM, 1);
        return ok;
    }

    bool ParseOptions(lua_State* luaVM, int tableIndex, SManagedBreakOptions& options, SString& error)
    {
        if (!lua_istable(luaVM, tableIndex))
            return true;

        std::uint32_t integerValue = 0;
        if (ReadIntegerField(luaVM, tableIndex, "fragments", integerValue))
        {
            if (integerValue < 1 || integerValue > CClientBreakEffectManager::MAX_FRAGMENTS_PER_EFFECT)
            {
                error = "fragments must be between 1 and 64";
                return false;
            }
            options.fragments = integerValue;
        }
        if (ReadIntegerField(luaVM, tableIndex, "lifetime", integerValue))
        {
            if (integerValue < 100 || integerValue > 600000)
            {
                error = "lifetime must be between 100 and 600000 milliseconds";
                return false;
            }
            options.lifetimeMs = integerValue;
        }
        if (ReadIntegerField(luaVM, tableIndex, "seed", integerValue))
            options.seed = integerValue;

        ReadNumberField(luaVM, tableIndex, "force", options.force);
        ReadNumberField(luaVM, tableIndex, "randomness", options.randomness);
        ReadNumberField(luaVM, tableIndex, "gravity", options.gravity);
        ReadNumberField(luaVM, tableIndex, "bounce", options.bounce);
        ReadNumberField(luaVM, tableIndex, "drag", options.drag);
        ReadNumberField(luaVM, tableIndex, "renderDistance", options.renderDistance);
        ReadVectorField(luaVM, tableIndex, "velocity", options.velocity);
        if (ReadVectorField(luaVM, tableIndex, "impactPosition", options.impactPosition))
            options.hasImpactPosition = true;
        ReadBoolField(luaVM, tableIndex, "hideOriginal", options.hideOriginal);
        ReadBoolField(luaVM, tableIndex, "disableOriginalCollision", options.disableOriginalCollision);

        if (options.force < 0.0f || options.randomness < 0.0f || options.gravity < 0.0f || options.bounce < 0.0f || options.bounce > 1.5f ||
            options.drag < 0.0f || options.renderDistance <= 0.0f)
        {
            error = "invalid managed break effect numeric option";
            return false;
        }
        return true;
    }

    void PruneObjectBreakProfiles()
    {
        if (!g_pClientGame || !g_pClientGame->GetObjectManager())
            return;

        CClientObjectManager* objectManager = g_pClientGame->GetObjectManager();
        g_ObjectBreakProfiles.erase(
            std::remove_if(g_ObjectBreakProfiles.begin(), g_ObjectBreakProfiles.end(), [objectManager](const SObjectBreakProfile& profile)
            {
                return !profile.object || !objectManager->Exists(profile.object) || profile.object->GetModel() != profile.model;
            }),
            g_ObjectBreakProfiles.end());
    }

    SObjectBreakProfile* FindObjectBreakProfile(CClientObject* object)
    {
        PruneObjectBreakProfiles();
        const auto it = std::find_if(g_ObjectBreakProfiles.begin(), g_ObjectBreakProfiles.end(), [object](const SObjectBreakProfile& profile)
                                     { return profile.object == object; });
        return it == g_ObjectBreakProfiles.end() ? nullptr : &*it;
    }

    void EraseObjectBreakProfile(CClientObject* object)
    {
        g_ObjectBreakProfiles.erase(
            std::remove_if(g_ObjectBreakProfiles.begin(), g_ObjectBreakProfiles.end(), [object](const SObjectBreakProfile& profile)
                           { return profile.object == object; }),
            g_ObjectBreakProfiles.end());
    }

    CClientBreakEffect* FractureProfile(SObjectBreakProfile& profile, CClientEntity* attacker)
    {
        if (!profile.object || !g_pClientGame || !g_pClientGame->GetManager())
            return nullptr;

        SManagedBreakOptions options = profile.fracture;
        if (!options.hasImpactPosition)
        {
            if (profile.hasLastImpactPosition)
            {
                options.impactPosition = profile.lastImpactPosition;
                options.hasImpactPosition = true;
            }
            else if (attacker)
            {
                attacker->GetPosition(options.impactPosition);
                options.hasImpactPosition = true;
            }
        }
        if (profile.hasLastImpactVelocity)
        {
            options.velocity.fX += profile.lastImpactVelocity.fX;
            options.velocity.fY += profile.lastImpactVelocity.fY;
            options.velocity.fZ += profile.lastImpactVelocity.fZ;
        }

        auto& manager = CClientBreakEffectManager::GetSingleton();
        const std::size_t cacheSizeBefore = manager.GetCacheEntryCount();
        CClientBreakEffect* effect = manager.CreateFromObject(g_pClientGame->GetManager(), profile.object, INVALID_ELEMENT_ID, options);
        if (!effect && manager.GetCacheEntryCount() > cacheSizeBefore)
            effect = manager.CreateFromObject(g_pClientGame->GetManager(), profile.object, INVALID_ELEMENT_ID, options);

        if (effect && profile.object->GetParent())
            effect->SetParent(profile.object->GetParent());
        return effect;
    }

    bool ApplyManagedDamage(SObjectBreakProfile& profile, float effectiveDamage, CClientEntity* attacker, const CVector* impactPosition = nullptr,
                            const CVector* impactVelocity = nullptr)
    {
        if (!std::isfinite(effectiveDamage) || effectiveDamage <= 0.0f)
            return false;

        if (impactPosition)
        {
            profile.lastImpactPosition = *impactPosition;
            profile.hasLastImpactPosition = true;
        }
        if (impactVelocity)
        {
            profile.lastImpactVelocity = *impactVelocity;
            profile.hasLastImpactVelocity = true;
        }

        profile.health = std::max(0.0f, profile.health - effectiveDamage);
        const bool thresholdBreak = (profile.native || profile.hasThresholdOverride) && effectiveDamage > profile.instantBreakThreshold;
        const bool healthBreak = profile.health <= 0.0f;
        return (thresholdBreak || healthBreak) && FractureProfile(profile, attacker) != nullptr;
    }

    bool ManagedObjectDamageHandler(CObjectSAInterface* objectInterface, float loss, CEntitySAInterface* attackerInterface)
    {
        // Preserve MTA's existing cancellable onClientObjectDamage event.
        if (!CClientGame::StaticObjectDamageHandler(objectInterface, loss, attackerInterface))
            return false;

        if (!objectInterface || !g_pGame || !g_pGame->GetPools())
            return true;

        CClientEntity* clientEntity = g_pGame->GetPools()->GetClientEntity(reinterpret_cast<DWORD*>(objectInterface));
        if (!clientEntity || clientEntity->GetType() != CCLIENTOBJECT)
            return true;

        auto* object = static_cast<CClientObject*>(clientEntity);
        SObjectBreakProfile* profile = FindObjectBreakProfile(object);
        if (!profile)
            return true;

        // Vehicle collision is also hooked because arbitrary DFFs do not all
        // enter CObject::ObjectDamage from physical impacts. If GTA does call
        // ObjectDamage for the same collision, consume the callback without
        // charging managed health a second time.
        const unsigned long long now = GetTickCount64_();
        if (profile->lastVehicleAttacker == attackerInterface && profile->lastVehicleDamageTick != 0 && now - profile->lastVehicleDamageTick <= 100)
        {
            profile->lastVehicleAttacker = nullptr;
            profile->lastVehicleDamageTick = 0;
            return false;
        }

        // fLoss is GTA's predicted per-call health loss, captured by MTA before
        // CObject commits it. It therefore already contains object.dat's native
        // collision damage multiplier and gun-break-mode damage choice.
        float effectiveDamage = std::max(0.0f, loss);
        if (profile->hasDamageMultiplier)
            effectiveDamage *= profile->damageMultiplier;

        CClientEntity* attacker = attackerInterface ? g_pGame->GetPools()->GetClientEntity(reinterpret_cast<DWORD*>(attackerInterface)) : nullptr;
        if (ApplyManagedDamage(*profile, effectiveDamage, attacker))
            EraseObjectBreakProfile(object);

        // Managed profiles own durability. Cancelling here prevents GTA from
        // applying the same loss or entering its fixed native break manager.
        return false;
    }

    bool ManagedVehicleCollisionHandler(CVehicleSAInterface*& collidingVehicle, CEntitySAInterface* collidedWith, int modelIndex, float damageImpulse,
                                        float collidingDamageImpulse, uint16 pieceType, CVector collisionPosition, CVector collisionVelocity, bool isProjectile)
    {
        if (!CClientGame::StaticVehicleCollisionHandler(collidingVehicle, collidedWith, modelIndex, damageImpulse, collidingDamageImpulse, pieceType,
                                                        collisionPosition, collisionVelocity, isProjectile))
            return false;

        if (!collidingVehicle || !collidedWith || !g_pGame || !g_pGame->GetPools() || damageImpulse <= 5.0f)
            return true;

        CClientEntity* collidedEntity = g_pGame->GetPools()->GetClientEntity(reinterpret_cast<DWORD*>(collidedWith));
        if (!collidedEntity || collidedEntity->GetType() != CCLIENTOBJECT)
            return true;

        auto* object = static_cast<CClientObject*>(collidedEntity);
        SObjectBreakProfile* profile = FindObjectBreakProfile(object);
        if (!profile)
            return true;

        float effectiveDamage = damageImpulse;
        if (profile->hasDamageMultiplier)
            effectiveDamage *= profile->damageMultiplier;

        profile->lastVehicleDamageTick = GetTickCount64_();
        profile->lastVehicleAttacker = reinterpret_cast<CEntitySAInterface*>(collidingVehicle);
        CClientEntity* attacker = g_pGame->GetPools()->GetClientEntity(reinterpret_cast<DWORD*>(collidingVehicle));
        if (ApplyManagedDamage(*profile, effectiveDamage, attacker, &collisionPosition, &collisionVelocity))
            EraseObjectBreakProfile(object);

        return true;
    }

    void EnsureObjectDamageHooks()
    {
        if (!g_pMultiplayer)
            return;
        g_pMultiplayer->SetObjectDamageHandler(ManagedObjectDamageHandler);
        g_pMultiplayer->SetVehicleCollisionHandler(ManagedVehicleCollisionHandler);
    }

    bool ParseBreakProfile(lua_State* luaVM, int tableIndex, CClientObject* object, SObjectBreakProfile& profile, SString& error)
    {
        if (!lua_istable(luaVM, tableIndex))
        {
            error = "Expected break profile table";
            return false;
        }

        profile.object = object;
        profile.model = object->GetModel();

        bool healthProvided = false;
        bool maxHealthProvided = false;
        float value = 0.0f;

        if (HasField(luaVM, tableIndex, "native"))
        {
            if (!ReadBoolField(luaVM, tableIndex, "native", profile.native))
            {
                error = "native must be a boolean";
                return false;
            }
        }

        if (HasField(luaVM, tableIndex, "health"))
        {
            if (!ReadNumberField(luaVM, tableIndex, "health", value) || value < 0.0f)
            {
                error = "health must be a finite number >= 0";
                return false;
            }
            profile.health = value;
            healthProvided = true;
        }

        if (HasField(luaVM, tableIndex, "maxHealth"))
        {
            if (!ReadNumberField(luaVM, tableIndex, "maxHealth", value) || value <= 0.0f)
            {
                error = "maxHealth must be a finite number > 0";
                return false;
            }
            profile.maxHealth = value;
            maxHealthProvided = true;
        }

        if (healthProvided && !maxHealthProvided)
            profile.maxHealth = std::max(profile.health, 0.0001f);
        else if (!healthProvided && maxHealthProvided)
            profile.health = profile.maxHealth;

        if (profile.health > profile.maxHealth)
        {
            error = "health cannot exceed maxHealth";
            return false;
        }

        if (HasField(luaVM, tableIndex, "damageMultiplier"))
        {
            if (!ReadNumberField(luaVM, tableIndex, "damageMultiplier", profile.damageMultiplier) || profile.damageMultiplier < 0.0f)
            {
                error = "damageMultiplier must be a finite number >= 0";
                return false;
            }
            profile.hasDamageMultiplier = true;
        }

        if (HasField(luaVM, tableIndex, "instantBreakThreshold"))
        {
            if (!ReadNumberField(luaVM, tableIndex, "instantBreakThreshold", profile.instantBreakThreshold) || profile.instantBreakThreshold < 0.0f)
            {
                error = "instantBreakThreshold must be a finite number >= 0";
                return false;
            }
            profile.hasThresholdOverride = true;
        }

        const int absoluteIndex = lua_absindex(luaVM, tableIndex);
        lua_getfield(luaVM, absoluteIndex, "fracture");
        if (!lua_isnil(luaVM, -1))
        {
            if (!lua_istable(luaVM, -1))
            {
                lua_pop(luaVM, 1);
                error = "fracture must be a table";
                return false;
            }
            if (!ParseOptions(luaVM, -1, profile.fracture, error))
            {
                lua_pop(luaVM, 1);
                return false;
            }
        }
        lua_pop(luaVM, 1);
        return true;
    }

    void PushBreakProfile(lua_State* luaVM, const SObjectBreakProfile& profile)
    {
        lua_newtable(luaVM);

        lua_pushnumber(luaVM, profile.health);
        lua_setfield(luaVM, -2, "health");
        lua_pushnumber(luaVM, profile.maxHealth);
        lua_setfield(luaVM, -2, "maxHealth");
        lua_pushboolean(luaVM, profile.native);
        lua_setfield(luaVM, -2, "native");
        lua_pushnumber(luaVM, profile.instantBreakThreshold);
        lua_setfield(luaVM, -2, "instantBreakThreshold");
        if (profile.hasDamageMultiplier)
            lua_pushnumber(luaVM, profile.damageMultiplier);
        else
            lua_pushnil(luaVM);
        lua_setfield(luaVM, -2, "damageMultiplier");

        lua_newtable(luaVM);
        lua_pushnumber(luaVM, static_cast<lua_Number>(profile.fracture.fragments));
        lua_setfield(luaVM, -2, "fragments");
        lua_pushnumber(luaVM, profile.fracture.force);
        lua_setfield(luaVM, -2, "force");
        lua_pushnumber(luaVM, profile.fracture.randomness);
        lua_setfield(luaVM, -2, "randomness");
        lua_pushnumber(luaVM, static_cast<lua_Number>(profile.fracture.lifetimeMs));
        lua_setfield(luaVM, -2, "lifetime");
        lua_pushnumber(luaVM, profile.fracture.gravity);
        lua_setfield(luaVM, -2, "gravity");
        lua_pushnumber(luaVM, profile.fracture.bounce);
        lua_setfield(luaVM, -2, "bounce");
        lua_pushnumber(luaVM, profile.fracture.drag);
        lua_setfield(luaVM, -2, "drag");
        lua_pushnumber(luaVM, profile.fracture.renderDistance);
        lua_setfield(luaVM, -2, "renderDistance");
        lua_pushnumber(luaVM, static_cast<lua_Number>(profile.fracture.seed));
        lua_setfield(luaVM, -2, "seed");
        lua_pushboolean(luaVM, profile.fracture.hideOriginal);
        lua_setfield(luaVM, -2, "hideOriginal");
        lua_pushboolean(luaVM, profile.fracture.disableOriginalCollision);
        lua_setfield(luaVM, -2, "disableOriginalCollision");
        lua_setfield(luaVM, -2, "fracture");
    }
}

void CLuaBreakEffectDefs::LoadFunctions()
{
    constexpr static const std::pair<const char*, lua_CFunction> functions[]{
        {"createObjectBreakEffect", CreateObjectBreakEffect},
        {"getBreakEffectFragmentCount", GetBreakEffectFragmentCount},
        {"getBreakEffectSourceTriangleCount", GetBreakEffectSourceTriangleCount},
        {"getBreakEffectSleepingFragmentCount", GetBreakEffectSleepingFragmentCount},
        {"getBreakEffectCacheHit", GetBreakEffectCacheHit},
        {"isBreakEffectPaused", IsBreakEffectPaused},
        {"setBreakEffectPaused", SetBreakEffectPaused},
        {"getBreakEffectCacheSize", GetBreakEffectCacheSize},
        {"clearBreakEffectCache", ClearBreakEffectCache},
        {"setObjectBreakProfile", SetObjectBreakProfile},
        {"getObjectBreakProfile", GetObjectBreakProfile},
        {"getObjectBreakHealth", GetObjectBreakHealth},
        {"setObjectBreakHealth", SetObjectBreakHealth},
        {"resetObjectBreakHealth", ResetObjectBreakHealth},
        {"clearObjectBreakProfile", ClearObjectBreakProfile},
    };

    for (const auto& [name, function] : functions)
        CLuaCFunctions::AddFunction(name, function);
}

void CLuaBreakEffectDefs::AddClass(lua_State* luaVM)
{
    lua_newclass(luaVM);
    lua_classfunction(luaVM, "create", "createObjectBreakEffect");
    lua_classfunction(luaVM, "getFragmentCount", "getBreakEffectFragmentCount");
    lua_classfunction(luaVM, "getSourceTriangleCount", "getBreakEffectSourceTriangleCount");
    lua_classfunction(luaVM, "getSleepingFragmentCount", "getBreakEffectSleepingFragmentCount");
    lua_classfunction(luaVM, "getCacheHit", "getBreakEffectCacheHit");
    lua_classfunction(luaVM, "isPaused", "isBreakEffectPaused");
    lua_classfunction(luaVM, "setPaused", "setBreakEffectPaused");
    lua_classvariable(luaVM, "paused", "setBreakEffectPaused", "isBreakEffectPaused");
    lua_registerclass(luaVM, "BreakEffect", "Element");
}

int CLuaBreakEffectDefs::CreateObjectBreakEffect(lua_State* luaVM)
{
    CClientObject* object = nullptr;
    CScriptArgReader args(luaVM);
    args.ReadUserData(object);
    if (args.HasErrors() || !object)
    {
        m_pScriptDebugging->LogBadType(luaVM);
        lua_pushboolean(luaVM, false);
        return 1;
    }

    SManagedBreakOptions options;
    SString error;
    if (lua_gettop(luaVM) >= 2 && !lua_isnil(luaVM, 2) && !lua_istable(luaVM, 2))
    {
        m_pScriptDebugging->LogCustom(luaVM, "Expected options table at argument 2");
        lua_pushboolean(luaVM, false);
        return 1;
    }
    if (!ParseOptions(luaVM, 2, options, error))
    {
        m_pScriptDebugging->LogCustom(luaVM, error);
        lua_pushboolean(luaVM, false);
        return 1;
    }

    CLuaMain* luaMain = m_pLuaManager->GetVirtualMachine(luaVM);
    CResource* resource = luaMain ? luaMain->GetResource() : nullptr;
    CClientBreakEffect* effect = nullptr;
    if (resource)
    {
        auto& manager = CClientBreakEffectManager::GetSingleton();
        const std::size_t cacheSizeBefore = manager.GetCacheEntryCount();
        effect = manager.CreateFromObject(m_pManager, object, INVALID_ELEMENT_ID, options);

        if (!effect && manager.GetCacheEntryCount() > cacheSizeBefore)
            effect = manager.CreateFromObject(m_pManager, object, INVALID_ELEMENT_ID, options);
    }
    if (!effect)
    {
        m_pScriptDebugging->LogWarning(luaVM, "Unable to fracture object: object must be streamed and contain valid static RenderWare geometry");
        lua_pushboolean(luaVM, false);
        return 1;
    }

    effect->SetParent(resource->GetResourceDynamicEntity());
    if (CElementGroup* group = resource->GetElementGroup())
        group->Add(effect);
    lua_pushelement(luaVM, effect);
    return 1;
}

int CLuaBreakEffectDefs::GetBreakEffectFragmentCount(lua_State* luaVM)
{
    CScriptArgReader args(luaVM);
    CClientBreakEffect* effect = ReadBreakEffect(args);
    if (effect)
        lua_pushnumber(luaVM, static_cast<lua_Number>(effect->GetFragmentCount()));
    else
        lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaBreakEffectDefs::GetBreakEffectSourceTriangleCount(lua_State* luaVM)
{
    CScriptArgReader args(luaVM);
    CClientBreakEffect* effect = ReadBreakEffect(args);
    if (effect)
        lua_pushnumber(luaVM, static_cast<lua_Number>(effect->GetSourceTriangleCount()));
    else
        lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaBreakEffectDefs::GetBreakEffectSleepingFragmentCount(lua_State* luaVM)
{
    CScriptArgReader args(luaVM);
    CClientBreakEffect* effect = ReadBreakEffect(args);
    if (effect)
        lua_pushnumber(luaVM, static_cast<lua_Number>(effect->GetSleepingFragmentCount()));
    else
        lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaBreakEffectDefs::GetBreakEffectCacheHit(lua_State* luaVM)
{
    CScriptArgReader args(luaVM);
    CClientBreakEffect* effect = ReadBreakEffect(args);
    lua_pushboolean(luaVM, effect && effect->WasCacheHit());
    return 1;
}

int CLuaBreakEffectDefs::IsBreakEffectPaused(lua_State* luaVM)
{
    CScriptArgReader args(luaVM);
    CClientBreakEffect* effect = ReadBreakEffect(args);
    lua_pushboolean(luaVM, effect && effect->IsPaused());
    return 1;
}

int CLuaBreakEffectDefs::SetBreakEffectPaused(lua_State* luaVM)
{
    CScriptArgReader args(luaVM);
    CClientBreakEffect* effect = ReadBreakEffect(args);
    bool paused = false;
    args.ReadBool(paused);
    if (!effect || args.HasErrors())
    {
        lua_pushboolean(luaVM, false);
        return 1;
    }
    effect->SetPaused(paused);
    lua_pushboolean(luaVM, true);
    return 1;
}

int CLuaBreakEffectDefs::GetBreakEffectCacheSize(lua_State* luaVM)
{
    (void)luaVM;
    lua_pushnumber(luaVM, static_cast<lua_Number>(CClientBreakEffectManager::GetSingleton().GetCacheEntryCount()));
    return 1;
}

int CLuaBreakEffectDefs::ClearBreakEffectCache(lua_State* luaVM)
{
    CClientBreakEffectManager::GetSingleton().ClearCache();
    lua_pushboolean(luaVM, true);
    return 1;
}

int CLuaBreakEffectDefs::SetObjectBreakProfile(lua_State* luaVM)
{
    CClientObject* object = nullptr;
    CScriptArgReader args(luaVM);
    args.ReadUserData(object);
    if (args.HasErrors() || !object)
    {
        m_pScriptDebugging->LogBadType(luaVM);
        lua_pushboolean(luaVM, false);
        return 1;
    }

    if (lua_gettop(luaVM) < 2 || !lua_istable(luaVM, 2))
    {
        m_pScriptDebugging->LogCustom(luaVM, "Expected break profile table at argument 2");
        lua_pushboolean(luaVM, false);
        return 1;
    }

    SObjectBreakProfile profile;
    SString error;
    if (!ParseBreakProfile(luaVM, 2, object, profile, error))
    {
        m_pScriptDebugging->LogCustom(luaVM, error);
        lua_pushboolean(luaVM, false);
        return 1;
    }

    EraseObjectBreakProfile(object);
    g_ObjectBreakProfiles.push_back(profile);
    EnsureObjectDamageHooks();

    if (g_ObjectBreakProfiles.back().health <= 0.0f)
    {
        if (FractureProfile(g_ObjectBreakProfiles.back(), nullptr))
            EraseObjectBreakProfile(object);
    }

    lua_pushboolean(luaVM, true);
    return 1;
}

int CLuaBreakEffectDefs::GetObjectBreakProfile(lua_State* luaVM)
{
    CClientObject* object = nullptr;
    CScriptArgReader args(luaVM);
    args.ReadUserData(object);
    if (args.HasErrors() || !object)
    {
        lua_pushboolean(luaVM, false);
        return 1;
    }

    SObjectBreakProfile* profile = FindObjectBreakProfile(object);
    if (!profile)
    {
        lua_pushboolean(luaVM, false);
        return 1;
    }

    PushBreakProfile(luaVM, *profile);
    return 1;
}

int CLuaBreakEffectDefs::GetObjectBreakHealth(lua_State* luaVM)
{
    CClientObject* object = nullptr;
    CScriptArgReader args(luaVM);
    args.ReadUserData(object);
    SObjectBreakProfile* profile = (!args.HasErrors() && object) ? FindObjectBreakProfile(object) : nullptr;
    if (!profile)
    {
        lua_pushboolean(luaVM, false);
        return 1;
    }

    lua_pushnumber(luaVM, profile->health);
    return 1;
}

int CLuaBreakEffectDefs::SetObjectBreakHealth(lua_State* luaVM)
{
    CClientObject* object = nullptr;
    float health = 0.0f;
    CScriptArgReader args(luaVM);
    args.ReadUserData(object);
    args.ReadNumber(health);
    if (args.HasErrors() || !object || !std::isfinite(health))
    {
        lua_pushboolean(luaVM, false);
        return 1;
    }

    SObjectBreakProfile* profile = FindObjectBreakProfile(object);
    if (!profile || health < 0.0f || health > profile->maxHealth)
    {
        lua_pushboolean(luaVM, false);
        return 1;
    }

    profile->health = health;
    if (profile->health <= 0.0f && FractureProfile(*profile, nullptr))
        EraseObjectBreakProfile(object);

    lua_pushboolean(luaVM, true);
    return 1;
}

int CLuaBreakEffectDefs::ResetObjectBreakHealth(lua_State* luaVM)
{
    CClientObject* object = nullptr;
    CScriptArgReader args(luaVM);
    args.ReadUserData(object);
    SObjectBreakProfile* profile = (!args.HasErrors() && object) ? FindObjectBreakProfile(object) : nullptr;
    if (!profile)
    {
        lua_pushboolean(luaVM, false);
        return 1;
    }

    profile->health = profile->maxHealth;
    lua_pushboolean(luaVM, true);
    return 1;
}

int CLuaBreakEffectDefs::ClearObjectBreakProfile(lua_State* luaVM)
{
    CClientObject* object = nullptr;
    CScriptArgReader args(luaVM);
    args.ReadUserData(object);
    if (args.HasErrors() || !object || !FindObjectBreakProfile(object))
    {
        lua_pushboolean(luaVM, false);
        return 1;
    }

    EraseObjectBreakProfile(object);
    lua_pushboolean(luaVM, true);
    return 1;
}
