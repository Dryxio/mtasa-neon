/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  PURPOSE:     Opt-in structured diagnostics for native AI synchronization
 *
 *****************************************************************************/

#pragma once

#include <cstdint>
#include <net/SyncStructures.h>

class CClientPed;
class NetBitStreamInterface;
struct SNativeAIGroupDecision;
struct SNativeInstantHitResolved;

enum class ENativeAITelemetryCategory : std::uint8_t
{
    TASK,
    PRESENTATION,
    NETWORK,
    OWNERSHIP,
    GROUP_DECISION,
    WEAPON,
};

// Packet metadata is deliberately transport-agnostic. localSequence numbers a
// packet inside one process; sampleKey fingerprints the exact semantic record
// so the owner and observer can correlate it without changing the wire format.
struct SNativeAITelemetryPacket
{
    std::uint64_t localSequence{};
    std::uint64_t sampleKey{};
    const char*   lane{"none"};
    const char*   direction{"none"};
    const char*   reliability{"unknown"};
    const char*   ordering{"unknown"};
    std::uint8_t  syncContext{};
    std::uint8_t  flags{};
    std::uint8_t  flags2{};

    bool    hasPosition{};
    CVector position{};
    bool    hasHeading{};
    float   heading{};
    bool    hasVelocity{};
    CVector velocity{};

    const SNativeTaskLocomotionSync*            locomotion{};
    const SNativeTaskWeaponPresentationSync*    weaponPresentation{};
    const SNativeTaskAnimationPresentationSync* animation{};
};

// Captures the rotation state after GTA has processed peds for the frame. The
// receive metadata identifies the exact network target which fed the current
// interpolation, while matrixHeading records what an observer can render.
struct SNativeAIRotationTelemetry
{
    std::uint64_t lastReceiveSequence{};
    std::uint64_t lastReceiveSampleKey{};
    std::uint32_t lastReceiveAtMs{};
    std::uint32_t sampleAgeMs{};
    std::uint32_t receiveIntervalMs{};
    std::uint32_t spatialSyncRateMs{};
    std::uint32_t interpolationBeginMs{};
    std::uint32_t interpolationEndMs{};

    float currentHeading{};
    float targetHeading{};
    float matrixHeading{};
    float interpolationBeginHeading{};
    float interpolationTargetHeading{};
    float networkSampleHeading{};

    std::uint32_t animationMode{};
    std::uint16_t animationGroup{};
    std::uint16_t animationId{};

    bool hasNetworkSample{};
    bool networkHeadingApplied{};
    bool interpolationActive{};
    bool remoteStreamInFence{};
    bool remoteReplicaPhysicsFence{};
    bool nativeCollisionAuthorityFence{};
    bool ownerCollisionFence{};
    bool animationPresentationActive{};
};

class CNativeAITelemetry final
{
public:
    static bool          IsEnabled(ENativeAITelemetryCategory category) noexcept;
    static std::uint64_t NextPacketSequence() noexcept;
    static std::uint64_t MakeSampleKey(const char* lane, const NetBitStreamInterface& bitStream, int startBit, int endBit) noexcept;
    static void          RecordPedEvent(ENativeAITelemetryCategory category, const char* event, CClientPed* ped,
                                        const SNativeAITelemetryPacket* packet = nullptr) noexcept;
    static void          RecordPedRotationEvent(const char* event, CClientPed* ped, const SNativeAIRotationTelemetry& rotation) noexcept;
    static void          RecordGroupDecision(const SNativeAIGroupDecision& decision) noexcept;
    static void          RecordInstantHitResolved(const SNativeInstantHitResolved& resolved) noexcept;
};
