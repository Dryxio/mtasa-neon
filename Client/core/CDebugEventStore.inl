#include <algorithm>
#include <limits>

CDebugEventStore::CDebugEventStore(std::size_t capacity) : m_capacity(std::max<std::size_t>(capacity, 1))
{
}

std::string CDebugEventStore::MakeSignature(const SDebugEvent& event)
{
    std::string signature;
    signature.reserve(event.resource.size() + event.file.size() + event.message.size() + event.context.size() + 48);
    signature.append(std::to_string(static_cast<unsigned>(event.side))).push_back('\x1f');
    signature.append(std::to_string(static_cast<unsigned>(event.severity))).push_back('\x1f');
    signature.append(event.resource).push_back('\x1f');
    signature.append(event.file).push_back('\x1f');
    signature.append(std::to_string(event.line)).push_back('\x1f');
    signature.append(event.message).push_back('\x1f');
    signature.append(event.category).push_back('\x1f');
    signature.append(event.context).push_back('\x1f');
    signature.append(event.correlationId);
    return signature;
}

void CDebugEventStore::Add(const SDebugEvent& event)
{
    const std::uint32_t timestamp = event.timestamp;
    if (!m_events.empty() && MakeSignature(m_events.back()) == MakeSignature(event))
    {
        auto& stored = m_events.back();
        stored.repeatCount = static_cast<std::uint32_t>(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(stored.repeatCount) + std::max<std::uint32_t>(event.repeatCount, 1), std::numeric_limits<std::uint32_t>::max()));
        stored.lastSeen = timestamp;
        stored.timestamp = timestamp;
        stored.sequence = event.sequence;
        if (m_captureActive)
        {
            if (!m_capture.empty() && MakeSignature(m_capture.back()) == MakeSignature(event))
                m_capture.back() = stored;
            else
                m_capture.emplace_back(stored);
        }
    }
    else
    {
        SStoredDebugEvent stored;
        static_cast<SDebugEvent&>(stored) = event;
        // Producer sequence numbers restart when a scripting runtime is recreated.
        // A core-owned identity keeps separate history rows independently selectable
        // across reconnects and server restarts.
        stored.entryId = ++m_nextEntryId;
        stored.repeatCount = std::max<std::uint32_t>(event.repeatCount, 1);
        stored.firstSeen = timestamp;
        stored.lastSeen = timestamp;
        m_events.emplace_back(stored);
        if (m_captureActive)
            m_capture.emplace_back(stored);
    }

    if (m_captureActive && timestamp - m_captureStartedAt >= CAPTURE_MAX_DURATION_MS)
        m_captureActive = false;

    TrimToCapacity();
    ++m_revision;
}

void CDebugEventStore::TrimToCapacity()
{
    while (m_events.size() > m_capacity)
    {
        m_events.pop_front();
        ++m_droppedCount;
    }
    if (m_capture.size() > m_capacity)
    {
        m_droppedCount += m_capture.size() - m_capacity;
        m_capture.erase(m_capture.begin(), m_capture.begin() + (m_capture.size() - m_capacity));
    }
}

void CDebugEventStore::Clear()
{
    m_events.clear();
    m_capture.clear();
    m_captureActive = false;
    m_captureAvailable = false;
    m_droppedCount = 0;
    ++m_revision;
}

bool CDebugEventStore::StartCapture(std::uint32_t now)
{
    if (m_captureActive)
        return false;

    m_capture.clear();
    for (const auto& event : m_events)
    {
        if (now - event.lastSeen <= CAPTURE_PREROLL_MS)
            m_capture.push_back(event);
    }
    m_captureStartedAt = now;
    m_captureActive = true;
    m_captureAvailable = true;
    ++m_revision;
    return true;
}

bool CDebugEventStore::StopCapture(std::uint32_t)
{
    if (!m_captureActive)
        return false;
    m_captureActive = false;
    ++m_revision;
    return true;
}

void CDebugEventStore::ClearCapture()
{
    m_capture.clear();
    m_captureActive = false;
    m_captureAvailable = false;
    ++m_revision;
}
