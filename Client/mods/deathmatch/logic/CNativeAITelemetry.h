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

enum class ENativeAITelemetryCategory : std::uint8_t
{
    TASK,
    PRESENTATION,
    NETWORK,
    OWNERSHIP,
    GROUP_DECISION,
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
    const SNativeTaskAnimationPresentationSync* animation{};
};

class CNativeAITelemetry final
{
public:
    static bool          IsEnabled(ENativeAITelemetryCategory category) noexcept;
    static std::uint64_t NextPacketSequence() noexcept;
    static std::uint64_t MakeSampleKey(const char* lane, const NetBitStreamInterface& bitStream, int startBit, int endBit) noexcept;
    static void          RecordPedEvent(ENativeAITelemetryCategory category, const char* event, CClientPed* ped,
                                        const SNativeAITelemetryPacket* packet = nullptr) noexcept;
    static void          RecordGroupDecision(const SNativeAIGroupDecision& decision) noexcept;
};
