/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.x
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/mods/deathmatch/logic/CClientBird.cpp
 *  PURPOSE:     Client managed bird element
 *
 *****************************************************************************/

#include <StdInc.h>
#include "CClientBird.h"
#include "CClientBirdManager.h"

namespace
{
    bool IsFiniteVector(const CVector& value)
    {
        return std::isfinite(value.fX) && std::isfinite(value.fY) && std::isfinite(value.fZ);
    }
}

CClientBird::CClientBird(CClientManager* pManager, ElementID ID) : CClientEntity(ID)
{
    m_pManager = pManager;
    m_pBirdManager = &CClientBirdManager::GetSingleton();
    SetTypeName("bird");
    m_pBirdManager->AddToList(this);
}

CClientBird::~CClientBird()
{
    Unlink();
}

bool CClientBird::Initialize(const CVector& position, const CVector& velocity, const CVector& targetVelocity, float size, float renderDistance,
                             std::uint32_t bodyColor, std::uint32_t wingColor, std::uint32_t wingBeatTime, bool curvedFlight, bool shootable,
                             bool movementEnabled)
{
    if (!IsFiniteVector(position) || !IsFiniteVector(velocity) || !IsFiniteVector(targetVelocity) || !std::isfinite(size) ||
        !std::isfinite(renderDistance) || size <= 0.0f || size > 20.0f || renderDistance <= 0.0f || renderDistance > 5000.0f || wingBeatTime == 0)
        return false;

    m_vecPosition = position;
    m_vecVelocity = velocity;
    m_vecTargetVelocity = targetVelocity;
    m_fSize = size;
    m_fRenderDistance = renderDistance;
    m_ulBodyColor = bodyColor;
    m_ulWingColor = wingColor;
    m_uiWingBeatTime = wingBeatTime;
    m_bCurvedFlight = curvedFlight;
    m_bShootable = shootable;
    m_bMovementEnabled = movementEnabled;
    return true;
}

void CClientBird::SetPosition(const CVector& position)
{
    if (IsFiniteVector(position))
        m_vecPosition = position;
}

bool CClientBird::SetVelocity(const CVector& velocity)
{
    if (!IsFiniteVector(velocity))
        return false;
    m_vecVelocity = velocity;
    return true;
}

bool CClientBird::SetTargetVelocity(const CVector& velocity)
{
    if (!IsFiniteVector(velocity))
        return false;
    m_vecTargetVelocity = velocity;
    return true;
}

bool CClientBird::SetSize(float size)
{
    if (!std::isfinite(size) || size <= 0.0f || size > 20.0f)
        return false;
    m_fSize = size;
    return true;
}

bool CClientBird::SetRenderDistance(float distance)
{
    if (!std::isfinite(distance) || distance <= 0.0f || distance > 5000.0f)
        return false;
    m_fRenderDistance = distance;
    return true;
}

void CClientBird::SetColors(std::uint32_t bodyColor, std::uint32_t wingColor)
{
    m_ulBodyColor = bodyColor;
    m_ulWingColor = wingColor;
}

bool CClientBird::SetWingBeatTime(std::uint32_t milliseconds)
{
    if (milliseconds == 0 || milliseconds > 600000)
        return false;
    m_uiWingBeatTime = milliseconds;
    return true;
}

void CClientBird::Unlink()
{
    if (m_pBirdManager)
    {
        m_pBirdManager->RemoveFromList(this);
        m_pBirdManager = nullptr;
    }
}
