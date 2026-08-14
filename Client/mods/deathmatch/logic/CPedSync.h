/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/CPedSync.h
 *  PURPOSE:     Header for ped sync class
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <CClientCommon.h>
#include <cstdint>
#include <unordered_map>
#include "CClientVehicle.h"

class CPedSync
{
public:
    CPedSync(CClientPedManager* pPedManager);
    ~CPedSync();

    bool ProcessPacket(unsigned char ucPacketID, NetBitStreamInterface& bitStream);
    void DoPulse();

    void AddPed(CClientPed* pPed);
    void RemovePed(CClientPed* pPed);

    std::list<CClientPed*>::const_iterator IterBegin() { return m_List.begin(); };
    std::list<CClientPed*>::const_iterator IterEnd() { return m_List.end(); };
    CMappedList<CClientPed*>               GetList() { return m_List; };

    bool Exists(CClientPed* pPed);

private:
    struct SNativeTaskLocomotionBurstState
    {
        bool           initialized{};
        unsigned long  burstUntil{};
        unsigned long  cooldownUntil{};
        unsigned long  lastSentAt{};
        unsigned long  burstHardUntil{};
        bool           spatialTransient{};
        bool           pendingTransientTransition{};
        unsigned short transientAnimGroup{};
        unsigned short transientAnimId{};
    };

    void Packet_PedStartSync(NetBitStreamInterface& BitStream);
    void Packet_PedStopSync(NetBitStreamInterface& BitStream);
    void Packet_PedSync(NetBitStreamInterface& BitStream);

    void Update();
    void UpdateNativeTaskAnimationPresentation();
    void UpdateNativeTaskLocomotionBurst(unsigned long currentTime);
    void WritePedInformation(NetBitStreamInterface* pBitStream, CClientPed* pPed, std::uint64_t telemetryPacketSequence);
    bool WriteNativeTaskAnimationPresentation(NetBitStreamInterface* pBitStream, CClientPed* pPed, bool& reliableSemanticTransition,
                                              std::uint64_t telemetryPacketSequence);
    bool WriteNativeTaskLocomotionBurst(NetBitStreamInterface* pBitStream, CClientPed* pPed, std::uint64_t telemetryPacketSequence);

    CClientPedManager*                                               m_pPedManager;
    CMappedList<CClientPed*>                                         m_List;
    unsigned long                                                    m_ulLastSyncTime;
    unsigned long                                                    m_ulLastNativeTaskAnimationSyncTime;
    unsigned long                                                    m_ulLastNativeTaskLocomotionBurstPollTime;
    std::unordered_map<CClientPed*, SNativeTaskLocomotionBurstState> m_NativeTaskLocomotionBurstStates;
    std::unordered_map<CClientPed*, unsigned long>                   m_NativeTaskLocomotionSpatialReceiveTimes;
};
