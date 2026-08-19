/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  PURPOSE:     Managed rope presentation and native-slot leasing
 *
 *****************************************************************************/

#pragma once

#include <cstdint>
#include <vector>

class CClientDummy;
class CClientEntity;
class CClientManager;
class CVector;

class CClientRopeManager
{
public:
    explicit CClientRopeManager(CClientManager* pManager);
    ~CClientRopeManager();

    void Register(CClientDummy* pRope);
    void Unregister(CClientDummy* pRope);
    void DoPulse();

    static bool IsRopeElement(const CClientEntity* pElement);

    bool IsActive(const CClientEntity* pElement) const;
    bool GetPositionAt(const CClientEntity* pElement, float fProgress, CVector& vecPosition, CVector* pVelocity = nullptr) const;

    static constexpr const char* KEY_DURATION = "__neon_rope_duration";
    static constexpr const char* KEY_REMAINING = "__neon_rope_remaining";
    static constexpr const char* KEY_TYPE = "__neon_rope_type";
    static constexpr const char* KEY_HOLDER = "__neon_rope_holder";
    static constexpr const char* KEY_OFFSET_X = "__neon_rope_offset_x";
    static constexpr const char* KEY_OFFSET_Y = "__neon_rope_offset_y";
    static constexpr const char* KEY_OFFSET_Z = "__neon_rope_offset_z";
    static constexpr const char* KEY_VELOCITY_X = "__neon_rope_velocity_x";
    static constexpr const char* KEY_VELOCITY_Y = "__neon_rope_velocity_y";
    static constexpr const char* KEY_VELOCITY_Z = "__neon_rope_velocity_z";
    static constexpr const char* KEY_FIXED_NODE = "__neon_rope_fixed_node";
    static constexpr const char* KEY_SIT_ON_GROUND = "__neon_rope_sit_on_ground";
    static constexpr const char* KEY_WINCH_HEIGHT = "__neon_rope_winch_height";
    static constexpr const char* KEY_LENGTH = "__neon_rope_length";
    static constexpr const char* KEY_CARRIED = "__neon_rope_carried";
    static constexpr const char* KEY_PHYSICS = "__neon_rope_physics";

private:
    struct SRopeEntry
    {
        CClientDummy*     pElement{};
        std::uint32_t     uiNativeId{};
        unsigned long long ullLastPulse{};
        bool              bLeased{};
        bool              bPhysicsAttached{};
    };

    SRopeEntry* FindEntry(const CClientEntity* pElement);
    const SRopeEntry* FindEntry(const CClientEntity* pElement) const;

    bool ResolveAnchor(CClientDummy* pRope, CVector& vecAnchor, CClientEntity*& pHolder) const;
    bool ShouldOwnPhysics(CClientEntity* pEntity) const;
    bool UpdateNative(SRopeEntry& entry, const CVector& vecAnchor, CClientEntity* pHolder);
    bool AcquireLease(SRopeEntry& entry, const CVector& vecAnchor, CClientEntity* pHolder);
    void ReleaseLease(SRopeEntry& entry);
    std::uint32_t AllocateNativeId();

    CClientManager*         m_pManager{};
    std::vector<SRopeEntry> m_Ropes;
    std::uint32_t           m_uiNextNativeId{0x4E520000u};
};
