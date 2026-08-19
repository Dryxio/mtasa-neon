/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CRopesSA.h
 *  PURPOSE:     Header file for rope entity class
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <CVector.h>
#include <game/CRopes.h>
#include <cstddef>
#include <cstdint>

#define ROPES_COUNT 8

#define FUNC_CRopes_CreateRopeForSwatPed 0x558D10
#define FUNC_CRopes_FindCoorsAlongRope   0x555E40
#define FUNC_CRopes_SetSpeedOfTopNode    0x555DF0
#define FUNC_CRopes_FindRope             0x556000
#define FUNC_CRope_ReleasePickedUpObject 0x556030
#define FUNC_CRope_Remove                0x556780
#define FUNC_CRope_PickUpObject          0x5569C0
#define FUNC_CRopes_RegisterRope         0x556B40

class CRopesSAInterface
{
public:
    // The Android symbols and gta-reversed both establish the 0x328 layout.
    // Older MTA names treated the second array and the ID as unrelated fields,
    // which made generic rope scripting unsafe even though the byte layout was right.
    CVector m_vecNodes[32];
    CVector m_vecNodeSpeeds[32];

    std::uint32_t m_uiId;
    float         m_fGroundHeight;
    float         m_fMassOrRopeLength;
    float         m_fNativeSegmentLength;

    CEntitySAInterface* m_pHolder;
    CEntitySAInterface* m_pHookObject;
    CEntitySAInterface* m_pCarriedEntity;

    float         m_fWinchHeight;
    std::uint32_t m_uiKeepAliveUntil;
    std::uint8_t  m_ucFixedNode;
    std::uint8_t  m_ucRopeType;
    std::uint8_t  m_ucWinchDisabled;
    std::uint8_t  m_ucFlags;
};
static_assert(sizeof(CRopesSAInterface) == 0x328, "Invalid size for CRopesSAInterface");
static_assert(offsetof(CRopesSAInterface, m_vecNodeSpeeds) == 0x180, "Invalid rope speed offset");
static_assert(offsetof(CRopesSAInterface, m_uiId) == 0x300, "Invalid rope ID offset");
static_assert(offsetof(CRopesSAInterface, m_pHolder) == 0x310, "Invalid rope holder offset");
static_assert(offsetof(CRopesSAInterface, m_pHookObject) == 0x314, "Invalid rope hook offset");
static_assert(offsetof(CRopesSAInterface, m_pCarriedEntity) == 0x318, "Invalid carried entity offset");
static_assert(offsetof(CRopesSAInterface, m_fWinchHeight) == 0x31C, "Invalid winch-height offset");
static_assert(offsetof(CRopesSAInterface, m_ucFixedNode) == 0x324, "Invalid fixed-node offset");
static_assert(offsetof(CRopesSAInterface, m_ucRopeType) == 0x325, "Invalid rope-type offset");

class CRopesSA : public CRopes
{
public:
    int CreateRopeForSwatPed(const CVector& vecPosition, DWORD dwDuration = 4000) override;

    bool RegisterRope(std::uint32_t uiId, eRopeType type, const CVector& vecPosition, bool bExpires, std::uint8_t ucFixedNode, bool bSitOnGround,
                      CEntitySAInterface* pHolder, std::uint32_t uiLifetime) override;
    bool RemoveRope(std::uint32_t uiId) override;
    void RemoveEntityRope(CEntitySAInterface* pObject) override;

    int  FindRope(std::uint32_t uiId) const override;
    bool FindCoorsAlongRope(std::uint32_t uiId, float fProgress, CVector& vecPosition, CVector* pVelocity = nullptr) const override;
    bool SetSpeedOfTopNode(std::uint32_t uiId, const CVector& vecVelocity) override;

    bool GetWinchHeight(std::uint32_t uiId, float& fHeight) const override;
    bool SetWinchHeight(std::uint32_t uiId, float fHeight) override;
    bool GetRopeLength(std::uint32_t uiId, float& fLength) const override;
    bool SetRopeLength(std::uint32_t uiId, float fLength) override;

    bool PickUpEntity(std::uint32_t uiId, CEntitySAInterface* pEntity) override;
    bool ReleasePickedUpEntity(std::uint32_t uiId) override;
    CEntitySAInterface* GetPickedUpEntity(std::uint32_t uiId) const override;

    bool GetNode(std::uint32_t uiId, std::uint8_t ucNode, CVector& vecPosition, CVector& vecVelocity) const override;
    unsigned int GetFreeRopeCount() const override;

private:
    CRopesSAInterface* GetRope(std::uint32_t uiId);
    const CRopesSAInterface* GetRope(std::uint32_t uiId) const;

    static CRopesSAInterface (&ms_aRopes)[ROPES_COUNT];
};
