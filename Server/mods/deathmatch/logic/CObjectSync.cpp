/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/CObjectSync.cpp
 *  PURPOSE:     Object sync class
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CObjectSync.h"

#include <cmath>

#ifdef WITH_OBJECT_SYNC

    #define SYNC_RATE                500
    #define MAX_PLAYER_SYNC_DISTANCE 100.0f

namespace
{
    constexpr float MAX_DYNAMIC_OBJECT_VELOCITY = 25.0f;
    constexpr float MAX_DYNAMIC_OBJECT_TURN_VELOCITY = 20.0f;

    bool IsSaneVector(const CVector& vec, float maxMagnitude)
    {
        if (!std::isfinite(vec.fX) || !std::isfinite(vec.fY) || !std::isfinite(vec.fZ))
            return false;

        const float magnitudeSquared = vec.fX * vec.fX + vec.fY * vec.fY + vec.fZ * vec.fZ;
        return magnitudeSquared <= maxMagnitude * maxMagnitude;
    }
}

CObjectSync::CObjectSync(CPlayerManager* pPlayerManager, CObjectManager* pObjectManager)
{
    m_pPlayerManager = pPlayerManager;
    m_pObjectManager = pObjectManager;
}

void CObjectSync::DoPulse()
{
    if (m_UpdateTimer.Get() > SYNC_RATE)
    {
        m_UpdateTimer.Reset();
        Update();
    }
}

bool CObjectSync::ProcessPacket(CPacket& Packet)
{
    if (Packet.GetPacketID() == PACKET_ID_OBJECT_SYNC)
    {
        Packet_ObjectSync(static_cast<CObjectSyncPacket&>(Packet));
        return true;
    }

    return false;
}

void CObjectSync::OverrideSyncer(CObject* pObject, CPlayer* pPlayer, bool bPersist)
{
    CPlayer* pSyncer = pObject->GetSyncer();
    if (pSyncer)
    {
        if (pSyncer == pPlayer)
        {
            if (!bPersist)
                SetSyncerAsPersistent(false);
            return;
        }

        StopSync(pObject);
    }

    if (pPlayer)
    {
        StartSync(pPlayer, pObject);
        SetSyncerAsPersistent(bPersist);
    }
}

void CObjectSync::Update()
{
    for (auto iter = m_pObjectManager->IterBegin(); iter != m_pObjectManager->IterEnd(); ++iter)
        UpdateObject(*iter);
}

void CObjectSync::UpdateObject(CObject* pObject)
{
    CPlayer* pSyncer = pObject->GetSyncer();

    if (!pObject->IsSyncable() || (!pObject->IsDynamicPhysics() && pObject->IsStatic() && !pObject->IsBreakable()))
    {
        if (pSyncer)
            StopSync(pObject);
        return;
    }

    if (pSyncer)
    {
        if ((!IsSyncerPersistent() && !IsPointNearPoint3D(pSyncer->GetPosition(), pObject->GetPosition(), MAX_PLAYER_SYNC_DISTANCE)) ||
            pObject->GetDimension() != pSyncer->GetDimension())
        {
            StopSync(pObject);
            FindSyncer(pObject);
        }
    }
    else
    {
        FindSyncer(pObject);
    }
}

void CObjectSync::FindSyncer(CObject* pObject)
{
    if (CPlayer* pPlayer = FindPlayerCloseToObject(pObject, MAX_PLAYER_SYNC_DISTANCE))
        StartSync(pPlayer, pObject);
}

void CObjectSync::StartSync(CPlayer* pPlayer, CObject* pObject)
{
    if (!pObject->IsSyncable())
        return;

    pPlayer->Send(CObjectStartSyncPacket(pObject));
    pObject->SetSyncer(pPlayer);

    CLuaArguments Arguments;
    Arguments.PushElement(pPlayer);
    pObject->CallEvent("onElementStartSync", Arguments);
}

void CObjectSync::StopSync(CObject* pObject)
{
    CPlayer* pSyncer = pObject->GetSyncer();
    pSyncer->Send(CObjectStopSyncPacket(pObject));
    pObject->SetSyncer(NULL);
    SetSyncerAsPersistent(false);

    CLuaArguments Arguments;
    Arguments.PushElement(pSyncer);
    pObject->CallEvent("onElementStopSync", Arguments);
}

CPlayer* CObjectSync::FindPlayerCloseToObject(CObject* pObject, float fMaxDistance)
{
    CVector  vecPosition = pObject->GetPosition();
    CPlayer* pSyncer = NULL;

    for (auto iter = m_pPlayerManager->IterBegin(); iter != m_pPlayerManager->IterEnd(); ++iter)
    {
        CPlayer* pPlayer = *iter;
        if (pPlayer->IsJoined() && IsPointNearPoint3D(vecPosition, pPlayer->GetPosition(), fMaxDistance) && pPlayer->GetDimension() == pObject->GetDimension())
        {
            if (!pSyncer || pPlayer->CountSyncingObjects() < pSyncer->CountSyncingObjects())
                pSyncer = pPlayer;
        }
    }

    return pSyncer;
}

void CObjectSync::Packet_ObjectSync(CObjectSyncPacket& Packet)
{
    CPlayer* pPlayer = Packet.GetSourcePlayer();
    if (!pPlayer || !pPlayer->IsJoined())
        return;

    for (auto iter = Packet.IterBegin(); iter != Packet.IterEnd(); ++iter)
    {
        CObjectSyncPacket::SyncData* pData = *iter;
        CElement*                    pElement = CElementIDs::GetElement(pData->ID);
        if (!pElement || !IS_OBJECT(pElement))
            continue;

        CObject* pObject = static_cast<CObject*>(pElement);
        if (pObject->GetSyncer() != pPlayer || !pObject->CanUpdateSync(pData->ucSyncTimeContext))
            continue;

        if (pData->ucFlags & 0x8)
        {
            if (!pObject->IsDynamicPhysics() || !IsSaneVector(pData->vecVelocity, MAX_DYNAMIC_OBJECT_VELOCITY))
                pData->ucFlags &= ~0x8;
            else
                pObject->SetPhysicsVelocity(pData->vecVelocity);
        }

        if (pData->ucFlags & 0x10)
        {
            if (!pObject->IsDynamicPhysics() || !IsSaneVector(pData->vecTurnVelocity, MAX_DYNAMIC_OBJECT_TURN_VELOCITY))
                pData->ucFlags &= ~0x10;
            else
                pObject->SetPhysicsTurnVelocity(pData->vecTurnVelocity);
        }

        if (pData->ucFlags & 0x1)
        {
            pObject->SetPosition(pData->vecPosition);
            g_pGame->GetColManager()->DoHitDetection(pObject->GetPosition(), pObject);
        }
        if (pData->ucFlags & 0x2)
            pObject->SetRotation(pData->vecRotation);
        if (pData->ucFlags & 0x4)
            pObject->SetHealth(pData->fHealth);

        pData->bSend = pData->ucFlags != 0;
    }

    m_pPlayerManager->BroadcastOnlyJoined(Packet, pPlayer);
}

#endif
