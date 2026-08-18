/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.x
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/mods/deathmatch/logic/CClientBird.h
 *  PURPOSE:     Client managed bird element
 *
 *****************************************************************************/

#pragma once

#include "CClientEntity.h"
#include <cstdint>

class CClientBirdManager;

class CClientBird final : public CClientEntity
{
    friend class CClientBirdManager;

public:
    CClientBird(CClientManager* pManager, ElementID ID);
    ~CClientBird();

    eClientEntityType GetType() const { return CCLIENTDUMMY; }

    bool Initialize(const CVector& position, const CVector& velocity, const CVector& targetVelocity, float size, float renderDistance,
                    std::uint32_t bodyColor, std::uint32_t wingColor, std::uint32_t wingBeatTime, bool curvedFlight, bool shootable,
                    bool movementEnabled);

    void GetPosition(CVector& position) const override { position = m_vecPosition; }
    void SetPosition(const CVector& position) override;

    const CVector& GetVelocity() const { return m_vecVelocity; }
    bool           SetVelocity(const CVector& velocity);
    const CVector& GetTargetVelocity() const { return m_vecTargetVelocity; }
    bool           SetTargetVelocity(const CVector& velocity);

    float GetSize() const { return m_fSize; }
    bool  SetSize(float size);

    float GetRenderDistance() const { return m_fRenderDistance; }
    bool  SetRenderDistance(float distance);

    std::uint32_t GetBodyColor() const { return m_ulBodyColor; }
    std::uint32_t GetWingColor() const { return m_ulWingColor; }
    void          SetColors(std::uint32_t bodyColor, std::uint32_t wingColor);

    std::uint32_t GetWingBeatTime() const { return m_uiWingBeatTime; }
    bool          SetWingBeatTime(std::uint32_t milliseconds);

    bool IsCurvedFlightEnabled() const { return m_bCurvedFlight; }
    void SetCurvedFlightEnabled(bool enabled) { m_bCurvedFlight = enabled; }

    bool IsShootable() const { return m_bShootable; }
    void SetShootable(bool enabled) { m_bShootable = enabled; }

    bool IsMovementEnabled() const { return m_bMovementEnabled; }
    void SetMovementEnabled(bool enabled) { m_bMovementEnabled = enabled; }

    void Unlink();

private:
    CClientBirdManager* m_pBirdManager = nullptr;
    CVector             m_vecPosition;
    CVector             m_vecVelocity;
    CVector             m_vecTargetVelocity;
    float               m_fSize = 0.55f;
    float               m_fRenderDistance = 80.0f;
    std::uint32_t       m_ulBodyColor = 0xFFD0D0D0;
    std::uint32_t       m_ulWingColor = 0xFFE8E8E8;
    std::uint32_t       m_uiWingBeatTime = 400;
    bool                m_bCurvedFlight = false;
    bool                m_bShootable = true;
    bool                m_bMovementEnabled = true;
};
