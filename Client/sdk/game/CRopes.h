/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        sdk/game/CRopes.h
 *  PURPOSE:     Rope entity interface
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <cstdint>

class CVector;
class CEntitySAInterface;

enum class eRopeType : std::uint8_t
{
    NONE = 0,
    WINCH_MAGNET = 1,
    HARNESS = 2,
    MINI_MAGNET = 3,
    DOCK_CRANE = 4,
    WRECKING_BALL = 5,
    QUARRY_CRANE = 6,
    VEGAS_CRANE = 7,
    SWAT = 8,
};

class CRopes
{
public:
    virtual int CreateRopeForSwatPed(const CVector& vecPosition, DWORD dwDuration = 4000) = 0;

    // Managed ropes deliberately use the game's existing eight-slot CRopes pool.
    // Callers must treat the ID as an opaque client-local lease key and never expose
    // it to Lua or assume that a particular native slot remains stable.
    virtual bool RegisterRope(std::uint32_t uiId, eRopeType type, const CVector& vecPosition, bool bExpires, std::uint8_t ucFixedNode,
                              bool bSitOnGround, CEntitySAInterface* pHolder, std::uint32_t uiLifetime) = 0;
    virtual bool RemoveRope(std::uint32_t uiId) = 0;
    virtual void RemoveEntityRope(CEntitySAInterface* pObjectSA) = 0;

    virtual int  FindRope(std::uint32_t uiId) const = 0;
    virtual bool FindCoorsAlongRope(std::uint32_t uiId, float fProgress, CVector& vecPosition, CVector* pVelocity = nullptr) const = 0;
    virtual bool SetSpeedOfTopNode(std::uint32_t uiId, const CVector& vecVelocity) = 0;

    virtual bool GetWinchHeight(std::uint32_t uiId, float& fHeight) const = 0;
    virtual bool SetWinchHeight(std::uint32_t uiId, float fHeight) = 0;
    virtual bool GetRopeLength(std::uint32_t uiId, float& fLength) const = 0;
    virtual bool SetRopeLength(std::uint32_t uiId, float fLength) = 0;

    virtual bool PickUpEntity(std::uint32_t uiId, CEntitySAInterface* pEntity) = 0;
    virtual bool ReleasePickedUpEntity(std::uint32_t uiId) = 0;
    virtual CEntitySAInterface* GetPickedUpEntity(std::uint32_t uiId) const = 0;

    virtual bool GetNode(std::uint32_t uiId, std::uint8_t ucNode, CVector& vecPosition, CVector& vecVelocity) const = 0;
    virtual unsigned int GetFreeRopeCount() const = 0;
};
