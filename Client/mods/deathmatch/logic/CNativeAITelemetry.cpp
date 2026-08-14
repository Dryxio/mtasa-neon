/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  PURPOSE:     Opt-in structured diagnostics for native AI synchronization
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CNativeAITelemetry.h"

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <io.h>
#include <mutex>
#include <string>

#include <game/CPed.h>
#include <game/CGame.h>
#include <game/CTaskManager.h>
#include <game/Task.h>

namespace
{
    constexpr unsigned int  MAX_LOG_SIZE_KB = 32U * 1024U;
    constexpr std::uint64_t MAX_LOG_BYTES = static_cast<std::uint64_t>(MAX_LOG_SIZE_KB) * 1024ULL;
    constexpr std::size_t   MAX_JSON_LINE_BYTES = 16ULL * 1024ULL;
    constexpr std::uint32_t MAX_EVENTS_PER_SECOND = 4096;
    constexpr std::uint32_t FLUSH_EVENT_INTERVAL = 32;
    constexpr std::uint64_t FLUSH_TIME_INTERVAL_MS = 1000;
    constexpr unsigned int  LOG_BACKUP_COUNT = 3;
    constexpr unsigned int  MAX_TASK_ANCESTRY = 16;

    const char* GetCategoryName(ENativeAITelemetryCategory category)
    {
        switch (category)
        {
            case ENativeAITelemetryCategory::TASK:
                return "task";
            case ENativeAITelemetryCategory::PRESENTATION:
                return "presentation";
            case ENativeAITelemetryCategory::NETWORK:
                return "network";
            case ENativeAITelemetryCategory::OWNERSHIP:
                return "ownership";
            case ENativeAITelemetryCategory::GROUP_DECISION:
                return "group_decision";
            default:
                return "unknown";
        }
    }

    void AppendEscapedJSONString(std::string& output, const char* value)
    {
        output.push_back('"');
        if (value)
        {
            const auto* cursor = reinterpret_cast<const unsigned char*>(value);
            for (; *cursor; ++cursor)
            {
                switch (*cursor)
                {
                    case '"':
                        output += "\\\"";
                        break;
                    case '\\':
                        output += "\\\\";
                        break;
                    case '\b':
                        output += "\\b";
                        break;
                    case '\f':
                        output += "\\f";
                        break;
                    case '\n':
                        output += "\\n";
                        break;
                    case '\r':
                        output += "\\r";
                        break;
                    case '\t':
                        output += "\\t";
                        break;
                    default:
                        if (*cursor < 0x20)
                        {
                            char escaped[7]{};
                            snprintf(escaped, sizeof(escaped), "\\u%04x", *cursor);
                            output += escaped;
                        }
                        else
                            output.push_back(static_cast<char>(*cursor));
                        break;
                }
            }
        }
        output.push_back('"');
    }

    void AppendKey(std::string& output, const char* key, bool& first)
    {
        if (!first)
            output.push_back(',');
        first = false;
        AppendEscapedJSONString(output, key);
        output.push_back(':');
    }

    void AppendString(std::string& output, const char* key, const char* value, bool& first)
    {
        AppendKey(output, key, first);
        if (value)
            AppendEscapedJSONString(output, value);
        else
            output += "null";
    }

    template <class T>
    void AppendInteger(std::string& output, const char* key, T value, bool& first)
    {
        AppendKey(output, key, first);
        output += std::to_string(value);
    }

    void AppendBoolean(std::string& output, const char* key, bool value, bool& first)
    {
        AppendKey(output, key, first);
        output += value ? "true" : "false";
    }

    void AppendFloat(std::string& output, const char* key, float value, bool& first)
    {
        AppendKey(output, key, first);
        if (!std::isfinite(value))
        {
            output += "null";
            return;
        }

        char buffer[48]{};
        snprintf(buffer, sizeof(buffer), "%.6g", static_cast<double>(value));
        output += buffer;
    }

    void AppendVectorArray(std::string& output, const char* key, const CVector& value, bool& first)
    {
        AppendKey(output, key, first);
        char buffer[160]{};
        if (std::isfinite(value.fX) && std::isfinite(value.fY) && std::isfinite(value.fZ))
            snprintf(buffer, sizeof(buffer), "[%.6g,%.6g,%.6g]", static_cast<double>(value.fX), static_cast<double>(value.fY), static_cast<double>(value.fZ));
        else
            snprintf(buffer, sizeof(buffer), "[null,null,null]");
        output += buffer;
    }

    SString GetWallTimeUTC()
    {
        const auto now = std::chrono::system_clock::now();
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        const auto time = std::chrono::system_clock::to_time_t(now);
        std::tm    utc{};
        gmtime_s(&utc, &time);

        char buffer[40]{};
        snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03lldZ", utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min,
                 utc.tm_sec, static_cast<long long>(milliseconds.count()));
        return buffer;
    }

    struct SEnablement
    {
        SEnablement()
        {
            const SString directory = SharedUtil::CalcMTASAPath(PathJoin("mta", "logs"));
            all = FileExists(PathJoin(directory, "native-ai-telemetry.enable"));
            task = all || FileExists(PathJoin(directory, "native-ai-telemetry-task.enable"));
            presentation = all || FileExists(PathJoin(directory, "native-ai-telemetry-presentation.enable"));
            network = all || FileExists(PathJoin(directory, "native-ai-telemetry-network.enable"));
            ownership = all || FileExists(PathJoin(directory, "native-ai-telemetry-ownership.enable"));
            groupDecision = all || FileExists(PathJoin(directory, "native-ai-telemetry-group.enable"));
        }

        bool IsEnabled(ENativeAITelemetryCategory category) const
        {
            switch (category)
            {
                case ENativeAITelemetryCategory::TASK:
                    return task;
                case ENativeAITelemetryCategory::PRESENTATION:
                    return presentation;
                case ENativeAITelemetryCategory::NETWORK:
                    return network;
                case ENativeAITelemetryCategory::OWNERSHIP:
                    return ownership;
                case ENativeAITelemetryCategory::GROUP_DECISION:
                    return groupDecision;
                default:
                    return false;
            }
        }

        bool all{};
        bool task{};
        bool presentation{};
        bool network{};
        bool ownership{};
        bool groupDecision{};
    };

    const SEnablement& GetEnablement()
    {
        static const SEnablement enablement;
        return enablement;
    }

    class CNativeAITelemetryWriter
    {
    public:
        ~CNativeAITelemetryWriter()
        {
            try
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                Close();
            }
            catch (...)
            {
                // Process teardown must not fail because a diagnostic file
                // could not be flushed.
            }
        }

        void Write(ENativeAITelemetryCategory category, const char* event, CClientPed* ped, const SNativeAITelemetryPacket* packet,
                   const SNativeAIGroupDecision* groupDecision = nullptr, CClientPed* sourcePed = nullptr) noexcept
        {
            try
            {
                const std::uint64_t         now = static_cast<std::uint64_t>(GetTickCount64_());
                std::lock_guard<std::mutex> lock(m_mutex);
                if (!AcceptEvent(now))
                    return;

                std::string line;
                line.reserve(4096);
                BuildLine(line, category, event, ped, packet, groupDecision, sourcePed, now);
                if (line.size() > MAX_JSON_LINE_BYTES)
                {
                    ++m_droppedSinceLastRecord;
                    return;
                }
                line.push_back('\n');

                if (!EnsureFile(line.size()))
                {
                    ++m_droppedSinceLastRecord;
                    return;
                }
                if (std::fwrite(line.data(), 1, line.size(), m_file) != line.size())
                {
                    ++m_droppedSinceLastRecord;
                    Close();
                    return;
                }

                m_fileBytes += line.size();
                ++m_eventsSinceFlush;
                if (m_eventsSinceFlush >= FLUSH_EVENT_INTERVAL || now - m_lastFlushAt >= FLUSH_TIME_INTERVAL_MS)
                {
                    std::fflush(m_file);
                    m_eventsSinceFlush = 0;
                    m_lastFlushAt = now;
                }
            }
            catch (...)
            {
                // Diagnostics must never be able to destabilize gameplay.
            }
        }

    private:
        bool AcceptEvent(std::uint64_t now)
        {
            if (m_rateWindowStartedAt == 0 || now - m_rateWindowStartedAt >= 1000)
            {
                m_rateWindowStartedAt = now;
                m_rateWindowEvents = 0;
            }
            if (m_rateWindowEvents >= MAX_EVENTS_PER_SECOND)
            {
                ++m_droppedSinceLastRecord;
                return false;
            }
            ++m_rateWindowEvents;
            return true;
        }

        bool EnsureFile(std::size_t pendingBytes)
        {
            if (m_path.empty())
            {
                const char* profile = g_pCore->IsSecondaryClient() ? "cl2" : "primary";
                m_path = SharedUtil::CalcMTASAPath(PathJoin("mta", "logs", SString("native-ai-telemetry-%s.jsonl", profile)));
            }

            if (!m_file)
            {
                if (FileExists(m_path) && FileSize(m_path) + pendingBytes > MAX_LOG_BYTES)
                    SharedUtil::CycleFile(m_path, MAX_LOG_SIZE_KB, LOG_BACKUP_COUNT);
                else if (FileExists(m_path) && !RepairIncompleteTail())
                    return false;
                m_file = std::fopen(m_path, "ab");
                if (!m_file)
                    return false;
                m_fileBytes = FileExists(m_path) ? FileSize(m_path) : 0;
            }
            else if (m_fileBytes + pendingBytes > MAX_LOG_BYTES)
            {
                Close();
                SharedUtil::CycleFile(m_path, MAX_LOG_SIZE_KB, LOG_BACKUP_COUNT);
                m_file = std::fopen(m_path, "ab");
                m_fileBytes = 0;
            }
            return m_file != nullptr;
        }

        bool RepairIncompleteTail()
        {
            FILE* file = std::fopen(m_path, "r+b");
            if (!file)
                return false;

            if (std::fseek(file, 0, SEEK_END) != 0)
            {
                std::fclose(file);
                return false;
            }
            const long end = std::ftell(file);
            if (end <= 0)
            {
                std::fclose(file);
                return end == 0;
            }

            if (std::fseek(file, end - 1, SEEK_SET) != 0)
            {
                std::fclose(file);
                return false;
            }
            if (std::fgetc(file) == '\n')
            {
                std::fclose(file);
                return true;
            }

            long truncateAt = -1;
            for (long position = end - 2; position >= 0 && end - position <= static_cast<long>(MAX_JSON_LINE_BYTES + 1); --position)
            {
                if (std::fseek(file, position, SEEK_SET) != 0)
                {
                    break;
                }
                if (std::fgetc(file) == '\n')
                {
                    truncateAt = position + 1;
                    break;
                }
            }
            if (truncateAt < 0 && end <= static_cast<long>(MAX_JSON_LINE_BYTES))
                truncateAt = 0;

            // A force-closed client can leave half a JSON object at EOF. Drop
            // only that incomplete record before appending the next process's
            // first line, keeping every prior newline-terminated record valid.
            const bool repaired = truncateAt >= 0 && _chsize_s(_fileno(file), truncateAt) == 0;
            std::fclose(file);
            return repaired;
        }

        void Close()
        {
            if (!m_file)
                return;
            std::fflush(m_file);
            std::fclose(m_file);
            m_file = nullptr;
        }

        void BuildLine(std::string& output, ENativeAITelemetryCategory category, const char* event, CClientPed* ped, const SNativeAITelemetryPacket* packet,
                       const SNativeAIGroupDecision* groupDecision, CClientPed* sourcePed, std::uint64_t monotonicNow)
        {
            output.push_back('{');
            bool first = true;
            AppendString(output, "schema", "neon.native_ai.telemetry", first);
            AppendInteger(output, "schema_version", 1, first);
            AppendInteger(output, "event_sequence", ++m_eventSequence, first);
            const SString wallTime = GetWallTimeUTC();
            AppendString(output, "wall_utc", *wallTime, first);
            AppendInteger(output, "monotonic_ms", monotonicNow, first);
            AppendInteger(output, "process_id", static_cast<unsigned int>(GetCurrentProcessId()), first);
            AppendString(output, "client_identity", g_pCore->IsSecondaryClient() ? "cl2" : "primary", first);
            AppendString(output, "category", GetCategoryName(category), first);
            AppendString(output, "event", event ? event : "unknown", first);
            if (m_droppedSinceLastRecord)
            {
                AppendInteger(output, "dropped_before", m_droppedSinceLastRecord, first);
                m_droppedSinceLastRecord = 0;
            }

            AppendKey(output, "ped", first);
            output.push_back('{');
            bool pedFirst = true;
            if (ped)
            {
                AppendInteger(output, "mta_element_id", ped->GetID().Value(), pedFirst);
                AppendInteger(output, "model", ped->GetModel(), pedFirst);
                AppendBoolean(output, "is_syncer", ped->IsSyncing(), pedFirst);
                AppendString(output, "owner_identity", ped->IsSyncing() ? (g_pCore->IsSecondaryClient() ? "cl2" : "primary") : nullptr, pedFirst);

                int value = 0;
                if (ped->GetCustomDataInt(CStringName("neon:ambientPedTrafficId"), value, false))
                    AppendInteger(output, "traffic_id", value, pedFirst);
                if (ped->GetCustomDataInt(CStringName("neon:ambientPedGroupId"), value, false))
                    AppendInteger(output, "server_group", value, pedFirst);
                if (ped->GetCustomDataInt(CStringName("neon:ambientPedGroupIndex"), value, false))
                    AppendInteger(output, "group_member_index", value, pedFirst);
                if (ped->GetCustomDataInt(CStringName("neon:ambientPedTrafficEpoch"), value, false))
                    AppendInteger(output, "owner_epoch", value, pedFirst);
                SString role;
                if (ped->GetCustomDataString(CStringName("neon:ambientPedGroupRole"), role, false))
                    AppendString(output, "group_role", *role, pedFirst);

                if (ped->GetGamePlayer())
                    AppendInteger(output, "move_state", static_cast<int>(ped->GetGamePlayer()->GetMoveState()), pedFirst);
            }
            output.push_back('}');

            if (ped)
            {
                SString    runId;
                SString    scenarioId;
                SString    actorId;
                SString    actionId;
                SString    step;
                const bool hasRun = ped->GetCustomDataString(CStringName("neon:nativeAIRunId"), runId, false);
                const bool hasScenario = ped->GetCustomDataString(CStringName("neon:nativeAIScenarioId"), scenarioId, false);
                const bool hasActor = ped->GetCustomDataString(CStringName("neon:nativeAIActorId"), actorId, false);
                const bool hasAction = ped->GetCustomDataString(CStringName("neon:nativeAIActionId"), actionId, false);
                const bool hasStep = ped->GetCustomDataString(CStringName("neon:nativeAIStep"), step, false);
                if (hasRun || hasScenario || hasActor || hasAction || hasStep)
                {
                    AppendKey(output, "trace", first);
                    output.push_back('{');
                    bool traceFirst = true;
                    if (hasRun)
                        AppendString(output, "run_id", *runId, traceFirst);
                    if (hasScenario)
                        AppendString(output, "scenario_id", *scenarioId, traceFirst);
                    if (hasActor)
                        AppendString(output, "actor_id", *actorId, traceFirst);
                    if (hasAction)
                        AppendString(output, "action_id", *actionId, traceFirst);
                    if (hasStep)
                        AppendString(output, "step", *step, traceFirst);
                    output.push_back('}');
                }
            }

            AppendKey(output, "task", first);
            output.push_back('{');
            bool   taskFirst = true;
            CTask* leaf = ped && ped->GetTaskManager() ? ped->GetTaskManager()->GetSimplestActiveTask() : nullptr;
            if (leaf)
            {
                AppendInteger(output, "leaf_type", leaf->GetTaskType(), taskFirst);
                AppendString(output, "leaf_name", leaf->GetTaskName(), taskFirst);
            }
            AppendKey(output, "ancestry", taskFirst);
            output.push_back('[');
            bool         taskArrayFirst = true;
            unsigned int depth = 0;
            CTask*       current = leaf;
            for (; current && depth < MAX_TASK_ANCESTRY; current = current->GetParent(), ++depth)
            {
                if (!taskArrayFirst)
                    output.push_back(',');
                taskArrayFirst = false;
                output.push_back('{');
                bool entryFirst = true;
                AppendInteger(output, "type", current->GetTaskType(), entryFirst);
                AppendString(output, "name", current->GetTaskName(), entryFirst);
                output.push_back('}');
            }
            output.push_back(']');
            AppendBoolean(output, "ancestry_truncated", current != nullptr, taskFirst);
            output.push_back('}');

            if (ped)
            {
                CVector position;
                CVector velocity;
                ped->GetPosition(position);
                ped->GetMoveSpeed(velocity);
                AppendKey(output, "state", first);
                output.push_back('{');
                bool stateFirst = true;
                AppendVectorArray(output, "position", position, stateFirst);
                AppendFloat(output, "heading", ped->GetCurrentRotation(), stateFirst);
                AppendVectorArray(output, "velocity", velocity, stateFirst);
                output.push_back('}');
            }

            if (packet)
            {
                AppendKey(output, "packet", first);
                output.push_back('{');
                bool packetFirst = true;
                AppendInteger(output, "local_sequence", packet->localSequence, packetFirst);
                if (packet->sampleKey != 0)
                {
                    char sampleKey[17]{};
                    snprintf(sampleKey, sizeof(sampleKey), "%016" PRIx64, packet->sampleKey);
                    AppendString(output, "sample_key", sampleKey, packetFirst);
                }
                AppendString(output, "lane", packet->lane, packetFirst);
                AppendString(output, "direction", packet->direction, packetFirst);
                AppendString(output, "reliability", packet->reliability, packetFirst);
                AppendString(output, "ordering", packet->ordering, packetFirst);
                AppendInteger(output, "sync_context", packet->syncContext, packetFirst);
                AppendInteger(output, "flags", packet->flags, packetFirst);
                AppendInteger(output, "flags2", packet->flags2, packetFirst);
                output.push_back('}');

                AppendKey(output, "sample", first);
                output.push_back('{');
                bool sampleFirst = true;
                if (packet->hasPosition)
                    AppendVectorArray(output, "position", packet->position, sampleFirst);
                if (packet->hasHeading)
                    AppendFloat(output, "heading", packet->heading, sampleFirst);
                if (packet->hasVelocity)
                    AppendVectorArray(output, "velocity", packet->velocity, sampleFirst);
                if (packet->locomotion)
                {
                    AppendKey(output, "locomotion", sampleFirst);
                    output.push_back('{');
                    bool locomotionFirst = true;
                    AppendInteger(output, "mode", packet->locomotion->data.uiMode, locomotionFirst);
                    AppendInteger(output, "stick_x", packet->locomotion->data.sLeftStickX, locomotionFirst);
                    AppendInteger(output, "stick_y", packet->locomotion->data.sLeftStickY, locomotionFirst);
                    output.push_back('}');
                }
                if (packet->animation)
                {
                    AppendKey(output, "animation", sampleFirst);
                    output.push_back('{');
                    bool animationFirst = true;
                    AppendInteger(output, "mode", packet->animation->data.uiMode, animationFirst);
                    AppendInteger(output, "group", packet->animation->data.usAnimGroup, animationFirst);
                    AppendInteger(output, "id", packet->animation->data.usAnimId, animationFirst);
                    AppendFloat(output, "progress", packet->animation->data.fProgress, animationFirst);
                    AppendFloat(output, "speed", packet->animation->data.fSpeed, animationFirst);
                    AppendFloat(output, "blend", packet->animation->data.fBlendAmount, animationFirst);
                    AppendFloat(output, "heading", packet->animation->data.fHeading, animationFirst);
                    output.push_back('}');
                }
                output.push_back('}');
            }
            if (groupDecision)
            {
                AppendKey(output, "group_decision", first);
                output.push_back('{');
                bool decisionFirst = true;
                if (groupDecision->nativeGroupId != 0xFFFFFFFFU)
                    AppendInteger(output, "native_group_id", groupDecision->nativeGroupId, decisionFirst);
                AppendInteger(output, "event_type", groupDecision->eventType, decisionFirst);
                AppendInteger(output, "event_source_type", groupDecision->eventSourceType, decisionFirst);
                AppendInteger(output, "task_type", groupDecision->taskType, decisionFirst);
                AppendBoolean(output, "threatened", groupDecision->threatened, decisionFirst);
                AppendBoolean(output, "friendly", groupDecision->friendly, decisionFirst);

                AppendKey(output, "representative", decisionFirst);
                output.push_back('{');
                bool representativeFirst = true;
                if (ped)
                    AppendInteger(output, "mta_element_id", ped->GetID().Value(), representativeFirst);
                AppendInteger(output, "model", groupDecision->representativeModel, representativeFirst);
                AppendInteger(output, "ped_type", groupDecision->representativePedType, representativeFirst);
                output.push_back('}');

                AppendKey(output, "source", decisionFirst);
                output.push_back('{');
                bool sourceFirst = true;
                AppendBoolean(output, "is_ped", groupDecision->sourceIsPed, sourceFirst);
                AppendBoolean(output, "is_player", groupDecision->sourceIsPlayer, sourceFirst);
                if (sourcePed)
                    AppendInteger(output, "mta_element_id", sourcePed->GetID().Value(), sourceFirst);
                if (groupDecision->sourceIsPed)
                {
                    AppendInteger(output, "model", groupDecision->sourceModel, sourceFirst);
                    AppendInteger(output, "ped_type", groupDecision->sourcePedType, sourceFirst);
                }
                output.push_back('}');
                output.push_back('}');
            }
            output.push_back('}');
        }

        std::mutex    m_mutex;
        FILE*         m_file{};
        SString       m_path;
        std::uint64_t m_fileBytes{};
        std::uint64_t m_eventSequence{};
        std::uint64_t m_rateWindowStartedAt{};
        std::uint64_t m_lastFlushAt{};
        std::uint32_t m_rateWindowEvents{};
        std::uint32_t m_eventsSinceFlush{};
        std::uint64_t m_droppedSinceLastRecord{};
    };

    CNativeAITelemetryWriter& GetWriter()
    {
        static CNativeAITelemetryWriter writer;
        return writer;
    }
}

bool CNativeAITelemetry::IsEnabled(ENativeAITelemetryCategory category) noexcept
{
    try
    {
        return GetEnablement().IsEnabled(category);
    }
    catch (...)
    {
        // Failure to initialize optional diagnostics means disabled.
        return false;
    }
}

std::uint64_t CNativeAITelemetry::NextPacketSequence() noexcept
{
    static std::atomic<std::uint64_t> sequence{};
    return ++sequence;
}

std::uint64_t CNativeAITelemetry::MakeSampleKey(const char* lane, const NetBitStreamInterface& bitStream, int startBit, int endBit) noexcept
{
    if (!lane || startBit < 0 || endBit < startBit || endBit > bitStream.GetNumberOfBitsUsed() || !bitStream.GetData())
        return 0;

    // Hash the exact local record bit range rather than decoded values. This
    // binds receive/apply records and bit-identical fast-animation relay
    // records. Spatial relays may be re-quantized by the server, so their
    // cross-process key is diagnostic evidence only, not a delivery ID.
    // Including the bit length avoids collisions caused only by zero padding.
    std::uint64_t hash = 14695981039346656037ULL;
    const auto    hashByte = [&hash](std::uint8_t value)
    {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(lane); *cursor; ++cursor)
        hashByte(*cursor);
    for (int bit = startBit; bit < endBit; ++bit)
    {
        const std::uint8_t value = (bitStream.GetData()[bit / 8] >> (7 - bit % 8)) & 1;
        hashByte(value);
    }
    for (unsigned int shift = 0; shift < 32; shift += 8)
        hashByte(static_cast<std::uint8_t>((endBit - startBit) >> shift));

    return hash;
}

void CNativeAITelemetry::RecordPedEvent(ENativeAITelemetryCategory category, const char* event, CClientPed* ped,
                                        const SNativeAITelemetryPacket* packet) noexcept
{
    if (!IsEnabled(category))
        return;
    try
    {
        GetWriter().Write(category, event, ped, packet);
    }
    catch (...)
    {
        // Construction of the optional writer must not affect gameplay.
    }
}

void CNativeAITelemetry::RecordGroupDecision(const SNativeAIGroupDecision& decision) noexcept
{
    if (!IsEnabled(ENativeAITelemetryCategory::GROUP_DECISION) || !decision.representative)
        return;

    try
    {
        const auto resolveClientPed = [](CPed* gamePed) -> CClientPed*
        {
            if (!gamePed)
                return nullptr;
            auto* entity = static_cast<CClientEntity*>(gamePed->GetStoredPointer());
            if (!entity || !IS_PED(entity))
                return nullptr;
            auto* clientPed = static_cast<CClientPed*>(entity);
            return clientPed->GetGamePlayer() == gamePed ? clientPed : nullptr;
        };

        CClientPed* representative = resolveClientPed(decision.representative);
        if (!representative)
            return;

        int trafficId = 0;
        if (representative->GetCustomDataInt(CStringName("neon:ambientPedTrafficId"), trafficId, false) && !representative->IsSyncing())
            return;

        GetWriter().Write(ENativeAITelemetryCategory::GROUP_DECISION, "group_response_selected", representative, nullptr, &decision,
                          resolveClientPed(decision.sourcePed));
    }
    catch (...)
    {
        // Group decisions are diagnostic-only and must never affect AI.
    }
}
