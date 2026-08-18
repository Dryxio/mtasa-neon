/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  PURPOSE:     Managed fire presentation and gameplay policy
 *
 *****************************************************************************/
#pragma once

#include <unordered_map>
#include <vector>

class CClientDummy;
class CClientEntity;
class CClientManager;
class CFxSystem;

class CClientFireManager
{
public:
    enum eDamageTarget : unsigned char
    {
        DAMAGE_PLAYERS = 1 << 0,
        DAMAGE_PEDS = 1 << 1,
        DAMAGE_VEHICLES = 1 << 2,
        DAMAGE_OBJECTS = 1 << 3,
        DAMAGE_ALL = DAMAGE_PLAYERS | DAMAGE_PEDS | DAMAGE_VEHICLES | DAMAGE_OBJECTS,
    };

    explicit CClientFireManager(CClientManager* pManager);
    ~CClientFireManager();

    void Register(CClientDummy* pFire);
    void Unregister(CClientDummy* pFire);
    void DoPulse();

    static bool IsFireElement(const CClientEntity* pElement);

    static constexpr const char* KEY_DURATION = "__neon_fire_duration";
    static constexpr const char* KEY_EXPIRY = "__neon_fire_expiry";
    static constexpr const char* KEY_STRENGTH = "__neon_fire_strength";
    static constexpr const char* KEY_DAMAGE = "__neon_fire_damage";
    static constexpr const char* KEY_DAMAGE_MASK = "__neon_fire_damage_mask";
    static constexpr const char* KEY_SPREAD = "__neon_fire_spread";
    static constexpr const char* KEY_MAX_GENERATIONS = "__neon_fire_max_generations";
    static constexpr const char* KEY_GENERATION = "__neon_fire_generation";
    static constexpr const char* KEY_SOURCE = "__neon_fire_source";
    static constexpr const char* KEY_TARGET = "__neon_fire_target";

private:
    struct SFireEntry
    {
        CClientDummy* pElement{};
        CFxSystem*    pFxSystem{};
        int           iFxTier{-1};
        unsigned long long ullLastDamagePulse{};
        std::unordered_map<CClientEntity*, unsigned long long> LastVictimPulse;
    };

    void ProcessEntry(SFireEntry& entry, std::vector<CClientDummy*>& expiredLocalFires);
    void RecreateFx(SFireEntry& entry, int iTier, const CVector& vecPosition);
    void DestroyFx(SFireEntry& entry);
    void ProcessDamage(SFireEntry& entry, const CVector& vecPosition, float fStrength, unsigned char ucMask);
    void TryDamage(SFireEntry& entry, CClientEntity* pVictim, const CVector& vecPosition, float fRadiusSq, unsigned char ucMask, float fDamage);

    CClientManager*                         m_pManager{};
    std::vector<SFireEntry>                 m_Fires;
};
