/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/core/CDebugEventStore.h
 *  PURPOSE:     Bounded structured diagnostic history and incident capture
 *
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include <net/SDebugEvent.h>

struct SStoredDebugEvent : public SDebugEvent
{
    std::uint64_t entryId{};
    std::uint32_t firstSeen{};
    std::uint32_t lastSeen{};
};

class CDebugEventStore
{
public:
    static constexpr std::size_t   DEFAULT_CAPACITY = 10000;
    static constexpr std::uint32_t CAPTURE_PREROLL_MS = 30000;
    static constexpr std::uint32_t CAPTURE_MAX_DURATION_MS = 120000;

    explicit CDebugEventStore(std::size_t capacity = DEFAULT_CAPACITY);

    void Add(const SDebugEvent& event);
    void Clear();

    const std::deque<SStoredDebugEvent>& GetEvents() const noexcept { return m_events; }
    std::uint64_t                        GetRevision() const noexcept { return m_revision; }
    std::uint64_t                        GetDroppedCount() const noexcept { return m_droppedCount; }

    bool                                  StartCapture(std::uint32_t now);
    bool                                  StopCapture(std::uint32_t now);
    bool                                  IsCaptureActive() const noexcept { return m_captureActive; }
    bool                                  HasCapture() const noexcept { return m_captureAvailable; }
    const std::vector<SStoredDebugEvent>& GetCapture() const noexcept { return m_capture; }
    void                                  ClearCapture();

    static std::string MakeSignature(const SDebugEvent& event);

private:
    void TrimToCapacity();

    std::size_t                    m_capacity;
    std::deque<SStoredDebugEvent>  m_events;
    std::vector<SStoredDebugEvent> m_capture;
    std::uint64_t                  m_revision{};
    std::uint64_t                  m_droppedCount{};
    bool                           m_captureActive{};
    bool                           m_captureAvailable{};
    std::uint32_t                  m_captureStartedAt{};
    std::uint64_t                  m_nextEntryId{};
};
