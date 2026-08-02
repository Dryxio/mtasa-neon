/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/CPedSync.cpp
 *  PURPOSE:     Ped synchronization handler
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"

using std::list;

extern CClientGame* g_pClientGame;

#define PED_SYNC_RATE (g_TickRateSettings.iPedSync)

namespace
{
    constexpr unsigned long NATIVE_TASK_ANIMATION_SYNC_RATE = 100;
    constexpr unsigned long NATIVE_TASK_LOCOMOTION_BURST_RATE = 100;
    constexpr unsigned long NATIVE_TASK_LOCOMOTION_BURST_DURATION = 800;
    constexpr unsigned long NATIVE_TASK_LOCOMOTION_BURST_COOLDOWN = 400;
    constexpr std::size_t   NATIVE_TASK_LOCOMOTION_BURST_PED_BUDGET = 16;
    constexpr float         NATIVE_TASK_LOCOMOTION_BURST_FAST_SPEED = 0.045f;
    constexpr float         NATIVE_TASK_LOCOMOTION_BURST_DIRECTION_SPEED = 0.015f;
    constexpr float         NATIVE_TASK_LOCOMOTION_BURST_SPEED_DELTA = 0.015f;
    constexpr float         NATIVE_TASK_LOCOMOTION_BURST_RELATIVE_SPEED_DELTA = 0.004f;
    constexpr float         NATIVE_TASK_LOCOMOTION_BURST_SPEED_DELTA_RATIO = 0.40f;
    constexpr float         NATIVE_TASK_LOCOMOTION_BURST_DIRECTION_DOT = 0.9063078f;  // 25 degrees

    float GetPlanarSpeed(const CVector& velocity)
    {
        return std::sqrt(velocity.fX * velocity.fX + velocity.fY * velocity.fY);
    }

    bool HasSignificantLocomotionDirectionChange(const CVector& previousVelocity, const CVector& currentVelocity)
    {
        const float previousSpeed = GetPlanarSpeed(previousVelocity);
        const float currentSpeed = GetPlanarSpeed(currentVelocity);
        if (previousSpeed < NATIVE_TASK_LOCOMOTION_BURST_DIRECTION_SPEED || currentSpeed < NATIVE_TASK_LOCOMOTION_BURST_DIRECTION_SPEED)
            return false;

        const float directionDot = (previousVelocity.fX * currentVelocity.fX + previousVelocity.fY * currentVelocity.fY) / (previousSpeed * currentSpeed);
        return directionDot < NATIVE_TASK_LOCOMOTION_BURST_DIRECTION_DOT;
    }

    bool IsNativeTaskLocomotionBurstTraceEnabled()
    {
        static const bool enabled = FileExists(SharedUtil::CalcMTASAPath("mta\\logs\\native-task-locomotion-trace.enable"));
        return enabled;
    }

    bool IsNativeTaskLocomotionBurstEligible(CClientPed* pPed)
    {
        if (pPed->GetType() != CCLIENTPED || !pPed->IsSyncing() || !pPed->GetGamePlayer() || pPed->GetRealOccupiedVehicle() || pPed->IsGettingIntoVehicle() ||
            pPed->IsGettingOutOfVehicle() || pPed->IsDucked() || pPed->IsDead() || pPed->HasSyncedAnim())
        {
            return false;
        }

        // Physical reactions and synchronized animations have their own
        // presentation lanes. Letting their actors reserve locomotion burst slots
        // would starve nearby fleeing peds without producing a usable sample.
        return pPed->GetNativeTaskAnimationPresentation().state == eNativeTaskAnimationPresentationState::NONE;
    }
}

CPedSync::CPedSync(CClientPedManager* pPedManager)
{
    m_pPedManager = pPedManager;
    m_ulLastSyncTime = 0;
    m_ulLastNativeTaskAnimationSyncTime = 0;
    m_ulLastNativeTaskLocomotionBurstPollTime = 0;
}

CPedSync::~CPedSync()
{
}

bool CPedSync::ProcessPacket(unsigned char ucPacketID, NetBitStreamInterface& BitStream)
{
    switch (ucPacketID)
    {
        case PACKET_ID_PED_STARTSYNC:
        {
            Packet_PedStartSync(BitStream);
            return true;
        }

        case PACKET_ID_PED_STOPSYNC:
        {
            Packet_PedStopSync(BitStream);
            return true;
        }

        case PACKET_ID_PED_SYNC:
        {
            Packet_PedSync(BitStream);
            return true;
        }
    }

    return false;
}

void CPedSync::DoPulse()
{
    // Got any items?
    if (m_List.size() > 0)
    {
        for (auto iter = m_List.begin(); iter != m_List.end(); ++iter)
        {
            CClientPed* pPed = *iter;
            // Update enter/exit sequence
            pPed->UpdateVehicleInOut();
        }
    }

    // Has it been long enough since our last state's sync?
    unsigned long ulCurrentTime = CClientTime::GetTime();
    bool          sentOrdinarySync = false;
    if (ulCurrentTime >= m_ulLastSyncTime + PED_SYNC_RATE)
    {
        Update();
        m_ulLastSyncTime = ulCurrentTime;
        m_ulLastNativeTaskLocomotionBurstPollTime = ulCurrentTime;
        sentOrdinarySync = true;
    }

    // Native fight strikes and shuffles can begin and finish inside the
    // ordinary ped-sync interval. Sample presentation independently so an
    // observer sees those short clips without increasing spatial bandwidth.
    const unsigned long nativeTaskAnimationSyncRate = std::min<unsigned long>(PED_SYNC_RATE, NATIVE_TASK_ANIMATION_SYNC_RATE);
    if (ulCurrentTime >= m_ulLastNativeTaskAnimationSyncTime + nativeTaskAnimationSyncRate)
    {
        UpdateNativeTaskAnimationPresentation();
        m_ulLastNativeTaskAnimationSyncTime = ulCurrentTime;
    }

    if (!sentOrdinarySync && PED_SYNC_RATE > NATIVE_TASK_LOCOMOTION_BURST_RATE &&
        ulCurrentTime >= m_ulLastNativeTaskLocomotionBurstPollTime + NATIVE_TASK_LOCOMOTION_BURST_RATE)
    {
        UpdateNativeTaskLocomotionBurst(ulCurrentTime);
        m_ulLastNativeTaskLocomotionBurstPollTime = ulCurrentTime;
    }
}

void CPedSync::AddPed(CClientPed* pPed)
{
    m_List.push_front(pPed);
    // The previous syncer may have left an active presentation sample on
    // viewers. Make the new owner publish its current state even when that
    // state is NONE, so ownership migration clears the old locomotion without
    // waiting for the receiver lease.
    pPed->m_LastSyncedData->nativeTaskLocomotionResetPending = true;
    pPed->m_LastSyncedData->nativeTaskWeaponPresentationResetPending = true;
    pPed->m_LastSyncedData->nativeTaskAnimationPresentationResetPending = true;
    m_NativeTaskLocomotionBurstStates.erase(pPed);
    m_NativeTaskLocomotionSpatialReceiveTimes.erase(pPed);
    pPed->SetSyncing(true);
}

void CPedSync::RemovePed(CClientPed* pPed)
{
    if (!m_List.empty())
        m_List.remove(pPed);

    m_NativeTaskLocomotionBurstStates.erase(pPed);
    m_NativeTaskLocomotionSpatialReceiveTimes.erase(pPed);
    pPed->SetSyncing(false);
}

bool CPedSync::Exists(CClientPed* pPed)
{
    return m_List.Contains(pPed);
}

void CPedSync::Packet_PedStartSync(NetBitStreamInterface& BitStream)
{
    // Read out the element id
    ElementID ID;
    if (BitStream.Read(ID))
    {
        // Grab the ped
        CClientPed* pPed = static_cast<CClientPed*>(m_pPedManager->Get(ID));
        if (pPed)
        {
            // Read out the position
            CVector vecPosition;
            BitStream.Read(vecPosition.fX);
            BitStream.Read(vecPosition.fY);
            BitStream.Read(vecPosition.fZ);

            // And rotation
            float fRotation;
            BitStream.Read(fRotation);

            // And the velocity
            CVector vecVelocity;
            BitStream.Read(vecVelocity.fX);
            BitStream.Read(vecVelocity.fY);
            BitStream.Read(vecVelocity.fZ);

            // And health/armor
            float fHealth, fArmor;
            BitStream.Read(fHealth);
            BitStream.Read(fArmor);

            float cameraRotation{};
            BitStream.Read(cameraRotation);
            pPed->SetCameraRotation(cameraRotation);

            // Set data
            pPed->SetPosition(vecPosition);
            pPed->SetCurrentRotation(fRotation);
            pPed->SetMoveSpeed(vecVelocity);

            // Unlock health and armour for the syncer
            pPed->UnlockHealth();
            pPed->UnlockArmor();

            // Set the new health
            pPed->SetHealth(fHealth);
            pPed->SetArmor(fArmor);

            AddPed(pPed);
        }
    }
}

void CPedSync::Packet_PedStopSync(NetBitStreamInterface& BitStream)
{
    // Read out the ped id
    ElementID ID;
    if (BitStream.Read(ID))
    {
        // Grab the ped
        CClientPed* pPed = static_cast<CClientPed*>(m_pPedManager->Get(ID));
        if (pPed)
        {
            // Lock health and armour
            pPed->LockHealth(pPed->GetHealth());
            pPed->LockArmor(pPed->GetArmor());

            // Stop syncing it
            RemovePed(pPed);
        }
    }
}

void CPedSync::Packet_PedSync(NetBitStreamInterface& BitStream)
{
    // While we're not out of peds
    while (BitStream.GetNumberOfUnreadBits() > 32)
    {
        // Read out the ped id
        ElementID ID;
        if (BitStream.Read(ID))
        {
            // Read out the sync time context. See CClientEntity for documentation on that.
            unsigned char ucSyncTimeContext = 0;
            BitStream.Read(ucSyncTimeContext);

            unsigned char ucFlags = 0;
            BitStream.Read(ucFlags);

            std::uint8_t flags2{};
            BitStream.Read(flags2);

            SNativeTaskLocomotionSync nativeTaskLocomotion;
            if ((flags2 & 0x02) && BitStream.Can(eBitStreamVersion::NativeTaskLocomotionPresentation) && !BitStream.Read(&nativeTaskLocomotion))
                return;

            SNativeTaskWeaponPresentationSync nativeTaskWeaponPresentation;
            if ((flags2 & 0x04) && BitStream.Can(eBitStreamVersion::NativeTaskWeaponPresentation) && !BitStream.Read(&nativeTaskWeaponPresentation))
                return;

            SNativeTaskAnimationPresentationSync nativeTaskAnimationPresentation;
            if ((flags2 & 0x08) && BitStream.Can(eBitStreamVersion::NativeTaskAnimationPresentation) && !BitStream.Read(&nativeTaskAnimationPresentation))
                return;

            CVector vecPosition{CVector::NoInit{}}, vecMoveSpeed{CVector::NoInit{}};
            float   fRotation, fHealth, fArmor;
            bool    bOnFire;
            bool    bIsInWater;
            float   cameraRotation;

            // Read out the position
            SPositionSync position(false);
            if (ucFlags & 0x01)
                BitStream.Read(&position);

            // And rotation
            SPedRotationSync rotation;
            if (ucFlags & 0x02)
                BitStream.Read(&rotation);

            // And the move speed
            SVelocitySync velocity;
            if (ucFlags & 0x04)
                BitStream.Read(&velocity);

            vecPosition = position.data.vecPosition;
            fRotation = rotation.data.fRotation;
            vecMoveSpeed = velocity.data.vecVelocity;

            // And health with armour
            if (ucFlags & 0x08)
                BitStream.Read(fHealth);
            if (ucFlags & 0x10)
                BitStream.Read(fArmor);

            if (flags2 & 0x01)
            {
                SCameraRotationSync camRotation;
                BitStream.Read(&camRotation);
                cameraRotation = camRotation.data.fRotation;
            }

            // And the burning state
            if (ucFlags & 0x20)
                BitStream.ReadBit(bOnFire);

            // And the in water state
            if (ucFlags & 0x40)
                BitStream.ReadBit(bIsInWater);

            // Grab the ped. Only update the sync if this packet is from the same context.
            CClientPed* pPed = m_pPedManager->Get(ID);
            if (pPed && pPed->CanUpdateSync(ucSyncTimeContext))
            {
                unsigned long spatialSyncRate = PED_SYNC_RATE;
                if (ucFlags & (0x01 | 0x02 | 0x04))
                {
                    // A transient owner burst uses the ordinary sequenced
                    // spatial lane to preserve packet order. Infer its cadence
                    // locally so repeated 100 ms targets do not retain the
                    // normal 400 ms interpolation lag after the turn is over.
                    const unsigned long currentTime = CClientTime::GetTime();
                    auto&               lastReceiveTime = m_NativeTaskLocomotionSpatialReceiveTimes[pPed];
                    if (lastReceiveTime != 0 && currentTime > lastReceiveTime)
                    {
                        const unsigned long receiveInterval = currentTime - lastReceiveTime;
                        if (receiveInterval < static_cast<unsigned long>(PED_SYNC_RATE))
                            spatialSyncRate = std::clamp(receiveInterval, 50UL, static_cast<unsigned long>(PED_SYNC_RATE));
                    }
                    lastReceiveTime = currentTime;
                }
                const bool lockSyncedAnimationTransform = pPed->HasSyncedAnim() && !pPed->GetAnimationCache().bUpdatePosition;
                if ((ucFlags & 0x01) && !lockSyncedAnimationTransform)
                    pPed->SetTargetPosition(vecPosition, spatialSyncRate);
                if ((ucFlags & 0x02) && !lockSyncedAnimationTransform)
                    pPed->SetTargetRotation(spatialSyncRate, fRotation, std::nullopt);
                if ((ucFlags & 0x04) && !lockSyncedAnimationTransform)
                {
                    pPed->SetMoveSpeed(vecMoveSpeed);
                    pPed->SetNativeTaskLocomotionAuthoritativeVelocity(vecMoveSpeed);
                }
                if (ucFlags & 0x08)
                    pPed->LockHealth(fHealth);
                if (ucFlags & 0x10)
                    pPed->LockArmor(fArmor);
                if ((flags2 & 0x01) && !lockSyncedAnimationTransform)
                    pPed->SetTargetRotation(spatialSyncRate, std::nullopt, cameraRotation);
                if ((flags2 & 0x02) && BitStream.Can(eBitStreamVersion::NativeTaskLocomotionPresentation))
                    pPed->SetNativeTaskLocomotionPresentation(nativeTaskLocomotion, "ped_sync");
                if ((flags2 & 0x04) && BitStream.Can(eBitStreamVersion::NativeTaskWeaponPresentation))
                    pPed->SetNativeTaskWeaponPresentation(nativeTaskWeaponPresentation, "ped_sync");
                if ((flags2 & 0x08) && BitStream.Can(eBitStreamVersion::NativeTaskAnimationPresentation))
                {
                    const char* source = flags2 & PED_SYNC_FLAG2_NATIVE_TASK_ANIMATION_LANE ? "ped_sync_fast" : "ped_sync";
                    pPed->SetNativeTaskAnimationPresentation(nativeTaskAnimationPresentation, source);
                }
                // The syncer uses this bit when a synchronized animation ends
                // or is overridden. Mirror the cleanup locally; otherwise the
                // remote ped remains permanently excluded from native-task
                // locomotion presentation even though its anim task is gone.
                if (ucFlags & 0x80)
                {
                    pPed->KillAnimation();
                    pPed->SetHasSyncedAnim(false);
                    pPed->m_animationOverridedByClient = false;
                }
                if (ucFlags & 0x20)
                    pPed->SetOnFire(bOnFire);
                if (ucFlags & 0x40)
                    pPed->SetInWater(bIsInWater);
            }
        }
    }
}

void CPedSync::UpdateNativeTaskLocomotionBurst(unsigned long currentTime)
{
    if (m_List.empty())
        return;

    struct SBurstCandidate
    {
        CClientPed*                      ped;
        SNativeTaskLocomotionBurstState* state;
        bool                             modeChanged;
    };

    std::vector<SBurstCandidate> candidates;
    candidates.reserve(m_List.size());

    for (CClientPed* pPed : m_List)
    {
        auto& state = m_NativeTaskLocomotionBurstStates[pPed];
        if (!IsNativeTaskLocomotionBurstEligible(pPed))
        {
            // Eligibility can change temporarily during a fall, get-up or
            // vehicle transition. Cancel its old window so only actors that
            // can actually serialize compete for the bounded global budget.
            state = {};
            continue;
        }

        CVector currentVelocity;
        pPed->GetMoveSpeed(currentVelocity);
        const SNativeTaskLocomotionSync  locomotion = pPed->GetNativeTaskLocomotion();
        const CVector&                   previousVelocity = pPed->m_LastSyncedData->vVelocity;
        const SNativeTaskLocomotionSync& previousLocomotion = pPed->m_LastSyncedData->nativeTaskLocomotion;
        const float                      previousSpeed = GetPlanarSpeed(previousVelocity);
        const float                      currentSpeed = GetPlanarSpeed(currentVelocity);
        const bool finiteSample = std::isfinite(currentVelocity.fX) && std::isfinite(currentVelocity.fY) && std::isfinite(currentVelocity.fZ) &&
                                  std::isfinite(previousVelocity.fX) && std::isfinite(previousVelocity.fY) && std::isfinite(previousVelocity.fZ);
        const bool hasLocomotion =
            locomotion.data.uiMode != SNativeTaskLocomotionSync::NONE || previousLocomotion.data.uiMode != SNativeTaskLocomotionSync::NONE;
        const bool modeChanged = locomotion.data.uiMode != previousLocomotion.data.uiMode;

        bool significantTransition = false;
        if (finiteSample && hasLocomotion && !state.initialized)
        {
            significantTransition = locomotion.data.uiMode != SNativeTaskLocomotionSync::NONE && currentSpeed >= NATIVE_TASK_LOCOMOTION_BURST_FAST_SPEED;
        }
        else if (finiteSample && hasLocomotion)
        {
            const float speedDelta = std::abs(currentSpeed - previousSpeed);
            const float maximumSpeed = std::max(previousSpeed, currentSpeed);
            const bool  speedChanged = speedDelta >= NATIVE_TASK_LOCOMOTION_BURST_SPEED_DELTA ||
                                      (speedDelta >= NATIVE_TASK_LOCOMOTION_BURST_RELATIVE_SPEED_DELTA && maximumSpeed > 0.0f &&
                                       speedDelta / maximumSpeed >= NATIVE_TASK_LOCOMOTION_BURST_SPEED_DELTA_RATIO);
            const bool directionChanged = HasSignificantLocomotionDirectionChange(previousVelocity, currentVelocity);
            significantTransition = modeChanged || speedChanged || directionChanged;
        }

        if (state.burstUntil != 0 && currentTime >= state.burstUntil)
        {
            state.burstUntil = 0;
            state.cooldownUntil = currentTime + NATIVE_TASK_LOCOMOTION_BURST_COOLDOWN;
        }

        if (significantTransition && currentTime >= state.cooldownUntil)
        {
            if (state.burstUntil == 0)
            {
                state.burstUntil = currentTime + NATIVE_TASK_LOCOMOTION_BURST_DURATION;
                if (IsNativeTaskLocomotionBurstTraceEnabled())
                {
                    g_pCore->GetConsole()->Printf(
                        "[native-task-locomotion][burst-trigger] profile=%s pid=%u ped=%u model=%lu reason=%s previousMode=%u currentMode=%u "
                        "previousSpeed=%.4f currentSpeed=%.4f duration=%lu",
                        g_pCore->IsSecondaryClient() ? "cl2" : "primary", static_cast<unsigned int>(GetCurrentProcessId()), pPed->GetID().Value(),
                        pPed->GetModel(), modeChanged ? "mode" : "kinematic", previousLocomotion.data.uiMode, locomotion.data.uiMode, previousSpeed,
                        currentSpeed, NATIVE_TASK_LOCOMOTION_BURST_DURATION);
                }
            }
        }

        state.initialized = true;

        if (state.burstUntil != 0 && currentTime < state.burstUntil && currentTime >= state.lastSentAt + NATIVE_TASK_LOCOMOTION_BURST_RATE)
            candidates.push_back({pPed, &state, modeChanged});
    }

    if (candidates.empty())
        return;

    // A single owner can sync many scripted peds. Prioritize the least
    // recently refreshed burst actors so one crowded encounter cannot turn a
    // transient quality improvement into an unbounded bandwidth multiplier.
    std::sort(candidates.begin(), candidates.end(),
              [](const SBurstCandidate& left, const SBurstCandidate& right)
              {
                  if (left.modeChanged != right.modeChanged)
                      return left.modeChanged;
                  return left.state->lastSentAt < right.state->lastSentAt;
              });
    const std::size_t deferredCandidateCount =
        candidates.size() > NATIVE_TASK_LOCOMOTION_BURST_PED_BUDGET ? candidates.size() - NATIVE_TASK_LOCOMOTION_BURST_PED_BUDGET : 0;
    if (candidates.size() > NATIVE_TASK_LOCOMOTION_BURST_PED_BUDGET)
        candidates.resize(NATIVE_TASK_LOCOMOTION_BURST_PED_BUDGET);

    NetBitStreamInterface* pBitStream = g_pNet->AllocateNetBitStream();
    if (!pBitStream)
        return;

    std::size_t writtenPedCount = 0;
    for (const SBurstCandidate& candidate : candidates)
    {
        if (WriteNativeTaskLocomotionBurst(pBitStream, candidate.ped))
        {
            candidate.state->lastSentAt = currentTime;
            ++writtenPedCount;
        }
    }

    if (writtenPedCount != 0)
    {
        g_pNet->SendPacket(PACKET_ID_PED_SYNC, pBitStream, PACKET_PRIORITY_MEDIUM, PACKET_RELIABILITY_UNRELIABLE_SEQUENCED);
        if (IsNativeTaskLocomotionBurstTraceEnabled())
        {
            g_pCore->GetConsole()->Printf("[native-task-locomotion][burst-send] profile=%s pid=%u peds=%u deferred=%u rate=%lu",
                                          g_pCore->IsSecondaryClient() ? "cl2" : "primary", static_cast<unsigned int>(GetCurrentProcessId()),
                                          static_cast<unsigned int>(writtenPedCount), static_cast<unsigned int>(deferredCandidateCount),
                                          NATIVE_TASK_LOCOMOTION_BURST_RATE);
        }
    }
    g_pNet->DeallocateNetBitStream(pBitStream);
}

bool CPedSync::WriteNativeTaskLocomotionBurst(NetBitStreamInterface* pBitStream, CClientPed* pPed)
{
    if (!pBitStream->Can(eBitStreamVersion::NativeTaskLocomotionPresentation) || !IsNativeTaskLocomotionBurstEligible(pPed))
        return false;

    CVector position;
    CVector velocity;
    pPed->GetPosition(position);
    pPed->GetMoveSpeed(velocity);
    const SNativeTaskLocomotionSync locomotion = pPed->GetNativeTaskLocomotion();
    const float                     rotation = pPed->GetCurrentRotation();
    const float                     cameraRotation = pPed->GetCameraRotation();
    if (!std::isfinite(position.fX) || !std::isfinite(position.fY) || !std::isfinite(position.fZ) || !std::isfinite(velocity.fX) ||
        !std::isfinite(velocity.fY) || !std::isfinite(velocity.fZ) || !std::isfinite(rotation) || !std::isfinite(cameraRotation))
    {
        return false;
    }

    pBitStream->Write(pPed->GetID());
    pBitStream->Write(pPed->GetSyncTimeContext());
    pBitStream->Write(static_cast<unsigned char>(0x01 | 0x02 | 0x04));
    pBitStream->Write(static_cast<std::uint8_t>(0x01 | 0x02));
    pBitStream->Write(&locomotion);

    SPositionSync positionSync(false);
    positionSync.data.vecPosition = position;
    pBitStream->Write(&positionSync);

    SPedRotationSync rotationSync;
    rotationSync.data.fRotation = rotation;
    pBitStream->Write(&rotationSync);

    SVelocitySync velocitySync;
    velocitySync.data.vecVelocity = velocity;
    pBitStream->Write(&velocitySync);

    SCameraRotationSync cameraRotationSync;
    cameraRotationSync.data.fRotation = cameraRotation;
    pBitStream->Write(&cameraRotationSync);

    // The burst shares the ordinary spatial sequence, so this is now the
    // latest transmitted state for both the next burst sample and the normal
    // 400 ms delta writer. This also makes gradual turns accumulate against
    // the last packet observers could actually have received.
    pPed->m_LastSyncedData->vPosition = position;
    pPed->m_LastSyncedData->fRotation = rotationSync.data.fRotation;
    pPed->m_LastSyncedData->vVelocity = velocity;
    pPed->m_LastSyncedData->cameraRotation = cameraRotationSync.data.fRotation;
    pPed->m_LastSyncedData->nativeTaskLocomotion = locomotion;
    pPed->m_LastSyncedData->nativeTaskLocomotionResetPending = false;
    return true;
}

void CPedSync::Update()
{
    // Got any items?
    if (m_List.size() > 0)
    {
        // Create a packet
        NetBitStreamInterface* pBitStream = g_pNet->AllocateNetBitStream();
        if (pBitStream)
        {
            // Write each ped to it
            list<CClientPed*>::const_iterator iter = m_List.begin();
            for (; iter != m_List.end(); ++iter)
            {
                WritePedInformation(pBitStream, *iter);
                m_NativeTaskLocomotionBurstStates[*iter].initialized = true;
            }

            // Send and destroy the packet
            g_pNet->SendPacket(PACKET_ID_PED_SYNC, pBitStream, PACKET_PRIORITY_MEDIUM, PACKET_RELIABILITY_UNRELIABLE_SEQUENCED);
            g_pNet->DeallocateNetBitStream(pBitStream);
        }
    }
}

void CPedSync::UpdateNativeTaskAnimationPresentation()
{
    if (m_List.empty())
        return;

    NetBitStreamInterface* pBitStream = g_pNet->AllocateNetBitStream();
    if (!pBitStream)
        return;

    bool wrotePresentation = false;
    for (CClientPed* pPed : m_List)
        wrotePresentation |= WriteNativeTaskAnimationPresentation(pBitStream, pPed);

    if (wrotePresentation)
    {
        g_pNet->SendPacket(PACKET_ID_PED_SYNC, pBitStream, PACKET_PRIORITY_MEDIUM, PACKET_RELIABILITY_UNRELIABLE_SEQUENCED,
                           PACKET_ORDERING_NATIVE_TASK_PRESENTATION);
    }
    g_pNet->DeallocateNetBitStream(pBitStream);
}

bool CPedSync::WriteNativeTaskAnimationPresentation(NetBitStreamInterface* pBitStream, CClientPed* pPed)
{
    if (!pBitStream->Can(eBitStreamVersion::NativeTaskAnimationPresentation))
        return false;

    const SNativeTaskAnimationPresentationResult presentationResult = pPed->GetNativeTaskAnimationPresentation();
    SNativeTaskAnimationPresentationSync         presentation = presentationResult.presentation;
    const SNativeTaskAnimationPresentationSync&  lastPresentation = pPed->m_LastSyncedData->nativeTaskAnimationPresentation;
    const bool                                   resetPending = pPed->m_LastSyncedData->nativeTaskAnimationPresentationResetPending;
    const bool                                   lastPresentationWasPhysical = pPed->m_LastSyncedData->nativeTaskAnimationPresentationWasPhysical;
    const bool canBridgeNativeTaskTransition = pPed->GetGamePlayer() && !pPed->GetRealOccupiedVehicle() && !pPed->IsGettingIntoVehicle() &&
                                               !pPed->IsGettingOutOfVehicle() && !pPed->IsDead() && !pPed->HasSyncedAnim();
    bool forcePhysicalTransitionHold = false;

    if (presentationResult.state == eNativeTaskAnimationPresentationState::HOLD_LAST_PHYSICAL_FRAME && !resetPending && canBridgeNativeTaskTransition)
    {
        if (lastPresentationWasPhysical && lastPresentation.data.uiMode == SNativeTaskAnimationPresentationSync::ANIMATION)
        {
            // FALL_AND_GET_UP has a real one-frame (and potentially longer
            // under load) association gap between its simple tasks. Refresh
            // the terminal FALL pose until GET_UP is ready instead of
            // exposing the observer's upright base pose between both phases.
            presentation = lastPresentation;
            presentation.data.fProgress = 1.0f;
            presentation.data.fBlendAmount = 1.0f;
            forcePhysicalTransitionHold = true;
        }
    }

    const bool changed = presentation.data.uiMode != lastPresentation.data.uiMode || presentation.data.usAnimGroup != lastPresentation.data.usAnimGroup ||
                         presentation.data.usAnimId != lastPresentation.data.usAnimId || presentation.data.fProgress != lastPresentation.data.fProgress ||
                         presentation.data.fSpeed != lastPresentation.data.fSpeed || presentation.data.fBlendAmount != lastPresentation.data.fBlendAmount ||
                         presentation.data.fHeading != lastPresentation.data.fHeading;
    if (!forcePhysicalTransitionHold && presentation.data.uiMode == SNativeTaskAnimationPresentationSync::NONE && !changed &&
        !pPed->m_LastSyncedData->nativeTaskAnimationPresentationResetPending)
    {
        return false;
    }

    pBitStream->Write(pPed->GetID());
    pBitStream->Write(pPed->GetSyncTimeContext());
    pBitStream->Write(static_cast<unsigned char>(0));
    pBitStream->Write(static_cast<std::uint8_t>(0x08 | PED_SYNC_FLAG2_NATIVE_TASK_ANIMATION_LANE));
    pBitStream->Write(&presentation);

    pPed->m_LastSyncedData->nativeTaskAnimationPresentation = presentation;
    pPed->m_LastSyncedData->nativeTaskAnimationPresentationResetPending = false;
    if (!forcePhysicalTransitionHold)
        pPed->m_LastSyncedData->nativeTaskAnimationPresentationWasPhysical = presentationResult.physical;
    return true;
}

void CPedSync::WritePedInformation(NetBitStreamInterface* pBitStream, CClientPed* pPed)
{
    CVector vecPosition;
    pPed->GetPosition(vecPosition);
    CVector vecVelocity;
    pPed->GetMoveSpeed(vecVelocity);

    // Server-authored named animations without root motion must keep the
    // transform that accompanied their RPC. GTA can reset its transient
    // current-rotation field while such an animation is running even though
    // the rendered matrix is unchanged; publishing that field turns remote
    // actors toward zero and also corrupts the server's authoritative state.
    const bool lockSyncedAnimationTransform = pPed->HasSyncedAnim() && !pPed->GetAnimationCache().bUpdatePosition;

    unsigned char ucFlags = 0;
    if (!lockSyncedAnimationTransform && vecPosition != pPed->m_LastSyncedData->vPosition)
        ucFlags |= 0x01;
    if (!lockSyncedAnimationTransform && pPed->GetCurrentRotation() != pPed->m_LastSyncedData->fRotation)
        ucFlags |= 0x02;
    if (!lockSyncedAnimationTransform && vecVelocity != pPed->m_LastSyncedData->vVelocity)
        ucFlags |= 0x04;
    if (pPed->GetHealth() != pPed->m_LastSyncedData->fHealth)
        ucFlags |= 0x08;
    if (pPed->GetArmor() != pPed->m_LastSyncedData->fArmour)
        ucFlags |= 0x10;
    if (pPed->IsOnFire() != pPed->m_LastSyncedData->bOnFire)
        ucFlags |= 0x20;
    if (pPed->IsInWater() != pPed->m_LastSyncedData->bIsInWater)
        ucFlags |= 0x40;
    if (pPed->IsReloadingWeapon() != pPed->m_LastSyncedData->isReloadingWeapon)
        ucFlags |= 0x60;
    if (pPed->HasSyncedAnim() && (!pPed->IsRunningAnimation() || pPed->m_animationOverridedByClient))
        ucFlags |= 0x80;

    std::uint8_t flags2{};
    if (!lockSyncedAnimationTransform && !IsNearlyEqual(pPed->GetCameraRotation(), pPed->m_LastSyncedData->cameraRotation))
        flags2 |= 0x01;

    const SNativeTaskLocomotionSync  nativeTaskLocomotion = pPed->GetNativeTaskLocomotion();
    const SNativeTaskLocomotionSync& lastNativeTaskLocomotion = pPed->m_LastSyncedData->nativeTaskLocomotion;
    const bool                       nativeTaskLocomotionChanged = nativeTaskLocomotion.data.uiMode != lastNativeTaskLocomotion.data.uiMode ||
                                             nativeTaskLocomotion.data.sLeftStickX != lastNativeTaskLocomotion.data.sLeftStickX ||
                                             nativeTaskLocomotion.data.sLeftStickY != lastNativeTaskLocomotion.data.sLeftStickY;
    if (pBitStream->Can(eBitStreamVersion::NativeTaskLocomotionPresentation) &&
        (nativeTaskLocomotion.data.uiMode != SNativeTaskLocomotionSync::NONE || nativeTaskLocomotionChanged ||
         pPed->m_LastSyncedData->nativeTaskLocomotionResetPending))
    {
        flags2 |= 0x02;
    }

    if (!lockSyncedAnimationTransform && nativeTaskLocomotion.data.uiMode != SNativeTaskLocomotionSync::NONE)
    {
        // Native locomotion is already refreshed every ped tick. Couple its
        // translation ceiling to the same snapshot even when velocity itself
        // is unchanged, so packet loss and A-to-B ownership handoffs cannot
        // leave a persistent observer capped by an older syncer's velocity.
        ucFlags |= 0x04;
    }

    const SNativeTaskWeaponPresentationSync  nativeTaskWeaponPresentation = pPed->GetNativeTaskWeaponPresentation();
    const SNativeTaskWeaponPresentationSync& lastNativeTaskWeaponPresentation = pPed->m_LastSyncedData->nativeTaskWeaponPresentation;
    const bool                               nativeTaskWeaponPresentationChanged =
        nativeTaskWeaponPresentation.data.uiMode != lastNativeTaskWeaponPresentation.data.uiMode ||
        nativeTaskWeaponPresentation.data.ucWeaponType != lastNativeTaskWeaponPresentation.data.ucWeaponType ||
        nativeTaskWeaponPresentation.data.usBurstLength != lastNativeTaskWeaponPresentation.data.usBurstLength ||
        nativeTaskWeaponPresentation.data.ucShootingRate != lastNativeTaskWeaponPresentation.data.ucShootingRate ||
        nativeTaskWeaponPresentation.data.vecTarget != lastNativeTaskWeaponPresentation.data.vecTarget ||
        nativeTaskWeaponPresentation.data.fAbortRange != lastNativeTaskWeaponPresentation.data.fAbortRange ||
        nativeTaskWeaponPresentation.data.ucFrequencyPercentage != lastNativeTaskWeaponPresentation.data.ucFrequencyPercentage ||
        nativeTaskWeaponPresentation.data.ucDriveByStyle != lastNativeTaskWeaponPresentation.data.ucDriveByStyle ||
        nativeTaskWeaponPresentation.data.bSeatRHS != lastNativeTaskWeaponPresentation.data.bSeatRHS;
    if (pBitStream->Can(eBitStreamVersion::NativeTaskWeaponPresentation) &&
        (nativeTaskWeaponPresentation.data.uiMode != SNativeTaskWeaponPresentationSync::NONE || nativeTaskWeaponPresentationChanged ||
         pPed->m_LastSyncedData->nativeTaskWeaponPresentationResetPending))
    {
        flags2 |= 0x04;
    }

    // Do we really have to sync this ped?
    if (ucFlags == 0 && flags2 == 0)
        return;

    // Write the ped id
    pBitStream->Write(pPed->GetID());

    // Write the sync time context
    pBitStream->Write(pPed->GetSyncTimeContext());

    // Write flags
    pBitStream->Write(ucFlags);

    // Write flags 2
    pBitStream->Write(flags2);

    if (flags2 & 0x02)
    {
        pBitStream->Write(&nativeTaskLocomotion);
        pPed->m_LastSyncedData->nativeTaskLocomotion = nativeTaskLocomotion;
        pPed->m_LastSyncedData->nativeTaskLocomotionResetPending = false;
    }

    if (flags2 & 0x04)
    {
        pBitStream->Write(&nativeTaskWeaponPresentation);
        pPed->m_LastSyncedData->nativeTaskWeaponPresentation = nativeTaskWeaponPresentation;
        pPed->m_LastSyncedData->nativeTaskWeaponPresentationResetPending = false;
    }

    // Write position if needed
    if (ucFlags & 0x01)
    {
        SPositionSync position(false);
        position.data.vecPosition = vecPosition;
        pBitStream->Write(&position);

        pPed->m_LastSyncedData->vPosition = vecPosition;
    }

    if (ucFlags & 0x02)
    {
        SPedRotationSync rotation;
        rotation.data.fRotation = pPed->GetCurrentRotation();
        pBitStream->Write(&rotation);

        pPed->m_LastSyncedData->fRotation = pPed->GetCurrentRotation();
    }

    // Write velocity
    if (ucFlags & 0x04)
    {
        SVelocitySync velocity;
        velocity.data.vecVelocity = vecVelocity;
        pBitStream->Write(&velocity);

        pPed->m_LastSyncedData->vVelocity = vecVelocity;
    }

    // And health
    if (ucFlags & 0x08)
    {
        pBitStream->Write(pPed->GetHealth());
        pPed->m_LastSyncedData->fHealth = pPed->GetHealth();
    }

    if (ucFlags & 0x10)
    {
        pBitStream->Write(pPed->GetArmor());
        pPed->m_LastSyncedData->fArmour = pPed->GetArmor();
    }

    if (flags2 & 0x01)
    {
        SCameraRotationSync camRotation;
        camRotation.data.fRotation = pPed->GetCameraRotation();
        pBitStream->Write(&camRotation);
        pPed->m_LastSyncedData->cameraRotation = camRotation.data.fRotation;
    }

    if (ucFlags & 0x20)
    {
        pBitStream->WriteBit(pPed->IsOnFire());
        pPed->m_LastSyncedData->bOnFire = pPed->IsOnFire();
    }

    if (ucFlags & 0x40)
    {
        pBitStream->WriteBit(pPed->IsInWater());
        pPed->m_LastSyncedData->bIsInWater = pPed->IsInWater();
    }

    if (ucFlags & 0x60)
    {
        bool isReloadingWeapon = pPed->IsReloadingWeapon();

        pBitStream->WriteBit(isReloadingWeapon);
        pPed->m_LastSyncedData->isReloadingWeapon = isReloadingWeapon;
    }

    // The animation has been overwritten or interrupted by the client
    if (ucFlags & 0x80)
    {
        pPed->SetHasSyncedAnim(false);
        pPed->m_animationOverridedByClient = false;
    }
}
