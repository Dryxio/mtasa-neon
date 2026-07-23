/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CNativeWorldPackSA.cpp
 *  PURPOSE:     Native GTA streaming registration for reviewed world packs
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CNativeWorldPackSA.h"
#include "CNativeBullworthPackSA.h"

#include "CGameSA.h"
#include "CFileIDRuntimeSA.h"
#include "CBuildingSA.h"
#include "CColModelSA.h"
#include "CIplSA.h"
#include "CModelInfoSA.h"
#include "CNativeModelStoreSA.h"
#include "CNativeWorldCacheSA.h"
#include "CNativeWorldPayloadValidatorSA.h"
#include "CPtrNodeSingleLinkPoolSA.h"
#include "CPoolSAInterface.h"
#include "CStreamingSA.h"
#include "CTextureDictonarySA.h"
#include "SharedUtil.File.h"
#include "SharedUtil.Hash.h"
#include "SharedUtil.Misc.h"
#include <core/CCoreInterface.h>

#include <cstdarg>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <sstream>

extern CGameSA*        pGame;
extern CCoreInterface* g_pCore;

namespace
{
    constexpr DWORD        LOAD_CD_DIRECTORY_CALL = 0x5B8E1B;
    constexpr BYTE         LOAD_CD_DIRECTORY_CALL_BYTES[] = {0xE8, 0xA0, 0xF4, 0xFF, 0xFF};
    constexpr DWORD        LOAD_CD_DIRECTORY = 0x5B82C0;
    constexpr DWORD        LOAD_NAMED_CD_DIRECTORY = 0x5B6170;
    constexpr DWORD        LOAD_OBJECT_TYPES = 0x5B8400;
    constexpr DWORD        FIND_TXD_SLOT = 0x731850;
    constexpr DWORD        ADD_TXD_SLOT = 0x731C80;
    constexpr DWORD        FIND_IPL_SLOT = 0x404AC0;
    constexpr DWORD        ENABLE_IPL_DYNAMIC_STREAMING = 0x404D30;
    constexpr DWORD        TXD_FIND_CACHE = 0xC88014;
    constexpr DWORD        GET_UPPERCASE_KEY = 0x53CF30;
    constexpr DWORD        ADD_IPL_SLOT = 0x405AC0;
    constexpr DWORD        LOAD_COL_BUFFER = 0x4106D0;
    constexpr DWORD        REMOVE_COL = 0x410730;
    constexpr DWORD        LOAD_IPL_BUFFER = 0x406080;
    constexpr DWORD        REMOVE_IPL = 0x404B20;
    constexpr DWORD        DELETE_COLLISION_MODEL = 0x4C4C40;
    constexpr DWORD        FATAL_EXIT_CODE = 0x4E425746;  // "NBWF"
    constexpr DWORD        ATOMIC_MODEL_VTABLE = 0x85BBF0;
    constexpr DWORD        DAMAGE_MODEL_VTABLE = 0x85BC30;
    constexpr DWORD        TIME_MODEL_VTABLE = 0x85BCB0;
    constexpr DWORD        FLIPPED_RECT_SENTINELS[] = {0x49742400, 0xC9742400, 0xC9742400, 0x49742400};
    constexpr float        MIN_STATIC_WORLD_XY = -10000.0f;
    constexpr float        MAX_STATIC_WORLD_XY = 9999.0f;
    constexpr float        MAX_STATIC_WORLD_Z = 5000.0f;
    constexpr unsigned int STATIC_WORLD_V3_FORMAT = 3;
    constexpr const char*  STATIC_WORLD_V3_POLICY = "static-world-v3";
    constexpr const char*  STATIC_WORLD_V3_AUDIT = "static-world-v3-transport-envelope-v1";
    constexpr const char*  STATIC_WORLD_V3_MANIFEST = "native-world.json";
    constexpr unsigned int STATIC_WORLD_V3_MAX_MANIFEST_BYTES = 64 * 1024;
    constexpr unsigned int STATIC_WORLD_V3_MAX_IDE_BYTES = 8 * 1024 * 1024;
    constexpr unsigned int STATIC_WORLD_V3_MAX_LOD_BYTES = 256 * 1024;
    constexpr uint64_t     STATIC_WORLD_V3_MAX_IMG_BYTES = 256ULL * 1024ULL * 1024ULL;
    constexpr unsigned int STATIC_WORLD_V3_MAX_IMAGES = 32;
    constexpr uint64_t     STATIC_WORLD_V3_MAX_TOTAL_BYTES = 8ULL * 1024ULL * 1024ULL * 1024ULL;
    constexpr unsigned int STATIC_WORLD_V3_MAX_MODELS = 4096;
    constexpr unsigned int STATIC_WORLD_V3_MAX_TXDS = 1024;
    constexpr unsigned int STATIC_WORLD_V3_MAX_SPATIAL_GROUPS = 64;
    constexpr unsigned int STATIC_WORLD_V3_MAX_PLACEMENTS = 20000;
    constexpr unsigned int STATIC_WORLD_V3_MAX_IPL_BYTES = 16 * 1024 * 1024;
    constexpr unsigned int STATIC_WORLD_V3_FIRST_CUSTOM_MODEL = 20000;
    constexpr unsigned int STATIC_WORLD_V3_LAST_MODEL = 31999;
    constexpr unsigned int STATIC_WORLD_V3_LAST_STOCK_MODEL = 19999;
    constexpr DWORD        STATIC_WORLD_V3_RW_LIBRARY_ID = 0x1803FFFF;
    constexpr const char*  STATIC_WORLD_V3_SET_POLICY = "static-world-v3-set";
    constexpr const char*  STATIC_WORLD_V3_SET_AUDIT = "static-world-v3-set-dry-run-v1";
    constexpr const char*  STATIC_WORLD_V3_SET_MANIFEST = "static-world-v3-set.json";
    constexpr unsigned int STATIC_WORLD_V3_SET_MAX_MANIFEST_BYTES = 16 * 1024;

#pragma pack(push, 1)
    struct SImgHeader
    {
        char  magic[4];
        DWORD count;
    };

    struct SImgEntry
    {
        DWORD offset;
        WORD  size;
        WORD  streamingSize;
        char  name[24];
    };

    struct SBinaryIplHeader
    {
        char  magic[4];
        DWORD counts[6];
        DWORD sections[12];
    };

    struct SBinaryIplInstance
    {
        float position[3];
        float quaternion[4];
        int   modelId;
        DWORD instanceType;
        int   lodIndex;
    };

    struct SStaticWorldV3LodHeader
    {
        char  magic[8];
        DWORD version;
        DWORD placementCount;
        DWORD groupCount;
        DWORD anchorCount;
        DWORD linkCount;
        DWORD reserved;
    };

    struct SStaticWorldV3LodLink
    {
        DWORD childOrdinal;
        DWORD anchorIndex;
    };
#pragma pack(pop)
    static_assert(sizeof(SBinaryIplHeader) == 0x4C, "Unexpected binary IPL header size");
    static_assert(sizeof(SBinaryIplInstance) == 0x28, "Unexpected binary IPL instance size");
    static_assert(sizeof(SStaticWorldV3LodHeader) == 0x20, "Unexpected static-world-v3 LOD header size");

    struct SBoundaryIplPayload
    {
        SBinaryIplHeader   header{};
        SBinaryIplInstance instance{};
    };
    static_assert(sizeof(SBoundaryIplPayload) == 0x74, "Unexpected boundary IPL payload size");

    struct SStaticWorldV3File
    {
        std::string  name;
        std::string  sha256;
        unsigned int bytes{};
    };

    struct SStaticWorldV3Manifest
    {
        std::string                     packId;
        std::string                     manifestSha256;
        unsigned int                    manifestBytes{};
        SStaticWorldV3File              ide;
        SStaticWorldV3File              lod;
        std::vector<SStaticWorldV3File> images;
    };

    struct SStaticWorldV3SetManifest
    {
        std::string                            setId;
        std::string                            manifestSha256;
        unsigned int                           manifestBytes{};
        std::array<SNativeWorldV3SetPackSA, 4> packs;
    };

    struct SStaticWorldV3Ide
    {
        std::set<unsigned int>              modelIds;
        std::set<std::string>               modelFiles;
        std::map<unsigned int, std::string> modelFilesById;
        std::set<std::string>               txdStems;
        std::string                         nameSpace;
        unsigned int                        firstModel{};
        unsigned int                        lastModel{};
        unsigned int                        ordinaryModels{};
        unsigned int                        damageModels{};
        unsigned int                        timedModels{};
    };

    struct SStaticWorldV3ImgEntry
    {
        std::filesystem::path archive;
        SImgEntry             entry{};
    };

    struct SStaticWorldV3Inventory
    {
        std::map<std::string, SStaticWorldV3ImgEntry> entries;
        std::set<std::string>                         colStems;
        std::set<std::string>                         iplStems;
        std::map<std::string, std::set<unsigned int>> colModelIds;
        std::map<std::string, std::set<unsigned int>> iplModelIds;
        std::vector<unsigned int>                     iplPlacementCounts;
        unsigned int                                  placements{};
        unsigned int                                  modelCount{};
        unsigned int                                  ordinaryModels{};
        unsigned int                                  damageModels{};
        unsigned int                                  timedModels{};
        unsigned int                                  txdCount{};
        unsigned int                                  imageCount{};
        std::string                                   nameSpace;
        std::set<unsigned int>                        modelIds;
        unsigned int                                  firstModel{};
        unsigned int                                  lastModel{};
    };

    struct SColDef
    {
        CRect rect;
        // CColStore::AddColSlot does not initialize these bytes. They are not
        // a name and must never be inspected as a C string.
        char           reserved[18];
        short          firstModel;
        short          lastModel;
        unsigned short refCount;
        bool           active;
        bool           required;
        bool           procedural;
        bool           interior;
    };
    static_assert(sizeof(SColDef) == 0x2C, "Unexpected CColStore slot size");
    static_assert(sizeof(CTextureDictonarySAInterface) == 12, "Unexpected CTxdStore slot size");

    struct SIdePlan
    {
        std::set<unsigned int>                    modelIds;
        std::set<std::string>                     modelNames;
        std::set<std::string>                     txdNames;
        std::map<unsigned int, std::string>       modelFileNames;
        std::map<unsigned int, std::string>       modelTxdNames;
        std::map<unsigned int, DWORD>             modelVtables;
        std::map<std::string, SImgEntry>          imgEntries;
        std::map<std::string, unsigned int>       txdSlots;
        std::vector<std::string>                  imgOrder;
        std::vector<bool>                         txdOriginallyOccupied;
        std::vector<unsigned char>                txdOriginalFlags;
        std::vector<CTextureDictonarySAInterface> txdOriginalObjects;
        std::vector<bool>                         colOriginallyOccupied;
        std::vector<unsigned char>                colOriginalFlags;
        std::vector<bool>                         iplOriginallyOccupied;
        std::vector<unsigned char>                iplOriginalFlags;
        std::vector<unsigned int>                 iplAllocationSlots;
        std::map<std::string, unsigned int>       iplSlots;
        unsigned int                              colSlot{};
        int                                       colOriginalFirstFree{};
        int                                       iplOriginalFirstFree{};
        const char*                               txdProfileName{};
        int                                       txdOriginalFirstFree{};
        int                                       txdOriginalFindCache{};
        bool                                      txdSnapshotValid{};
        unsigned int                              txdOccupied{};
        unsigned int                              txdFree{};
        int                                       txdHighestOccupied{-1};
        unsigned int                              txdHoles{};
        unsigned int                              txdPlanMin{};
        unsigned int                              txdPlanMax{};
        unsigned int                              txdPlanSpanHoles{};
        unsigned int                              atomic{};
        unsigned int                              damage{};
        unsigned int                              time{};
    };

    enum class EState
    {
        Off,
        Prepared,
        Hooked,
        Registering,
        Active,
        Refused,
    };

    CStreamingSA*                       g_streaming = nullptr;
    const SNativeWorldPackPolicySA*     g_policy = nullptr;
    SNativeWorldPackRuntimeDataSA       g_manifest;
    std::string                         g_activeDirectory;
    std::vector<const char*>            g_iplNamePointers;
    SNativeWorldPackDescriptorSA        g_runtimeDescriptor{};
    const SNativeWorldPackDescriptorSA* g_pack = nullptr;
    EState                              g_state = EState::Off;
    std::mutex                          g_transportPublisherMutex;
    bool                                g_authorizedRoute = false;
    SNativeWorldStartupSelection        g_authorizedSelection{};
    CNativeWorldCacheLeaseSA            g_authorizedLease;
    std::atomic_bool                    g_nativeModelSlotsReserved{false};
    std::atomic_uint                    g_reservedPackModelFirst{0};
    std::atomic_uint                    g_reservedPackModelLast{0};

    const SNativeWorldPackDescriptorSA& Pack()
    {
        assert(g_pack);
        return *g_pack;
    }

    void PublishNativeModelSlotReservation()
    {
        g_reservedPackModelFirst.store(Pack().modelFirst, std::memory_order_relaxed);
        g_reservedPackModelLast.store(Pack().modelLast, std::memory_order_relaxed);
        g_nativeModelSlotsReserved.store(true, std::memory_order_release);
    }

    void ReleaseNativeModelSlotReservation()
    {
        g_nativeModelSlotsReserved.store(false, std::memory_order_release);
    }

    const SNativeWorldPackPolicySA* SelectEnabledPolicy()
    {
        // Activation path and native process policy stay compiled even though
        // the payload inventory now comes from a runtime manifest.
        const SNativeWorldPackPolicySA* available[] = {&GetNativeBullworthPackPolicy()};
        const SNativeWorldPackPolicySA* selected = nullptr;
        for (const SNativeWorldPackPolicySA* candidate : available)
        {
            if (!candidate->featureEnvironment)
                continue;
            char        value[8]{};
            const DWORD valueLength = GetEnvironmentVariableA(candidate->featureEnvironment, value, sizeof(value));
            if (valueLength != 1 || value[0] != '1')
                continue;
            if (selected)
                return nullptr;
            selected = candidate;
        }
        return selected;
    }

    const SNativeWorldPackPolicySA* SelectAuthorizedPolicy(const SNativeWorldStartupSelection& selection)
    {
        // Transport format alone is not authority. Require the complete
        // compiled authorization tuple again in Game SA before any cache or
        // executable preflight can begin.
        if (!IsClosedNativeWorldStartupAuthorization(selection.wireVersion, selection.startupMode, selection.policy, selection.packFormat))
            return nullptr;
        if (selection.packFormat == NATIVE_WORLD_BULLWORTH_FORMAT)
            return &GetNativeBullworthPackPolicy();
        if (selection.packFormat == NATIVE_WORLD_STATIC_V1_FORMAT)
            return &GetNativeStaticWorldV1PackPolicy();
        return nullptr;
    }

    void Log(const char* format, ...)
    {
        char    detail[1900]{};
        va_list arguments;
        va_start(arguments, format);
        _vsnprintf_s(detail, sizeof(detail), _TRUNCATE, format, arguments);
        va_end(arguments);
        const char*   prefix = g_pack ? Pack().logPrefix : (g_policy ? g_policy->logPrefix : "[NativeWorld]");
        const SString message("%s %s", prefix, detail);
        OutputDebugStringA(message.c_str());
        OutputDebugStringA("\n");
        SharedUtil::WriteDebugEvent(message);
    }

    [[noreturn]] void Fatal(const char* reason)
    {
        // Native store and directory allocation has no complete inverse. A
        // post-commit mismatch must not continue with partially registered IDs.
        Log("registrar=fatal reason=%s exit=0x%08X", reason, FATAL_EXIT_CODE);
        TerminateProcess(GetCurrentProcess(), FATAL_EXIT_CODE);
        __assume(false);
    }

    void ReleaseRegistrationLease()
    {
        if (g_authorizedRoute)
            g_authorizedLease.Release();
        else
            ReleaseNativeWorldCacheLease();
    }

    void CommitRegistrationLease()
    {
        if (!g_authorizedRoute)
        {
            CommitNativeWorldCacheLease();
            return;
        }

        std::string error;
        if (!g_authorizedLease.Commit(g_policy->format, g_policy->key, g_authorizedSelection.contentId, g_authorizedSelection.ticketId, error))
            Fatal(error.c_str());
    }

    void MarkRegistrationRefused()
    {
        if (g_authorizedRoute)
            g_pCore->MarkNativeWorldStartupRefused();
    }

    std::string Trim(const std::string& value)
    {
        const size_t first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return {};
        const size_t last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    }

    std::vector<std::string> SplitCsv(const std::string& line)
    {
        std::vector<std::string> fields;
        std::stringstream        stream(line);
        std::string              field;
        while (std::getline(stream, field, ','))
            fields.emplace_back(Trim(field));
        return fields;
    }

    bool ParseUnsigned(const std::string& value, unsigned int& result)
    {
        if (value.empty())
            return false;
        uint64_t number = 0;
        for (unsigned char character : value)
        {
            if (character < '0' || character > '9')
                return false;
            number = number * 10 + character - '0';
            if (number > UINT_MAX)
                return false;
        }
        result = static_cast<unsigned int>(number);
        return true;
    }

    bool IsNativePathSafe(const SString& path)
    {
        if (path.empty() || path.length() >= MAX_PATH)
            return false;
        for (unsigned char character : path)
            if (character > 0x7F)
                return false;
        return true;
    }

    bool HasExactFileSize(const SString& path, unsigned int expected)
    {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        return GetFileAttributesExW(SharedUtil::FromUTF8(path).c_str(), GetFileExInfoStandard, &data) && data.nFileSizeHigh == 0 &&
               data.nFileSizeLow == expected;
    }

    bool IsSafeRegularFile(const SString& path)
    {
        const DWORD attributes = GetFileAttributesW(SharedUtil::FromUTF8(path).c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
    }

    struct SJsonValue
    {
        enum class EType
        {
            Object,
            Array,
            String,
            Unsigned,
        } type{};
        std::map<std::string, SJsonValue> object;
        std::vector<SJsonValue>           array;
        std::string                       string;
        unsigned int                      number{};
    };

    class CStrictJsonParser
    {
    public:
        explicit CStrictJsonParser(const std::string& text) : m_text(text) {}

        bool Parse(SJsonValue& value, std::string& error)
        {
            SkipWhitespace();
            if (!ParseValue(value, 0, error))
                return false;
            SkipWhitespace();
            if (m_offset != m_text.size())
            {
                error = "runtime manifest has trailing data";
                return false;
            }
            return true;
        }

    private:
        bool ParseValue(SJsonValue& value, unsigned int depth, std::string& error)
        {
            if (depth > 8 || m_offset >= m_text.size())
            {
                error = "runtime manifest nesting or value is invalid";
                return false;
            }
            if (m_text[m_offset] == '{')
                return ParseObject(value, depth, error);
            if (m_text[m_offset] == '[')
                return ParseArray(value, depth, error);
            if (m_text[m_offset] == '"')
            {
                value.type = SJsonValue::EType::String;
                return ParseString(value.string, error);
            }
            value.type = SJsonValue::EType::Unsigned;
            return ParseUnsignedValue(value.number, error);
        }

        bool ParseObject(SJsonValue& value, unsigned int depth, std::string& error)
        {
            value.type = SJsonValue::EType::Object;
            ++m_offset;
            SkipWhitespace();
            if (Consume('}'))
                return true;
            while (m_offset < m_text.size())
            {
                std::string key;
                if (!ParseString(key, error))
                    return false;
                SkipWhitespace();
                if (!Consume(':'))
                {
                    error = "runtime manifest object is missing a colon";
                    return false;
                }
                SkipWhitespace();
                SJsonValue child;
                if (!ParseValue(child, depth + 1, error))
                    return false;
                if (!value.object.emplace(key, std::move(child)).second)
                {
                    error = "runtime manifest contains a duplicate object key";
                    return false;
                }
                SkipWhitespace();
                if (Consume('}'))
                    return true;
                if (!Consume(','))
                {
                    error = "runtime manifest object is missing a comma";
                    return false;
                }
                SkipWhitespace();
            }
            error = "runtime manifest object is truncated";
            return false;
        }

        bool ParseArray(SJsonValue& value, unsigned int depth, std::string& error)
        {
            value.type = SJsonValue::EType::Array;
            ++m_offset;
            SkipWhitespace();
            if (Consume(']'))
                return true;
            while (m_offset < m_text.size())
            {
                SJsonValue child;
                if (!ParseValue(child, depth + 1, error))
                    return false;
                value.array.emplace_back(std::move(child));
                SkipWhitespace();
                if (Consume(']'))
                    return true;
                if (!Consume(','))
                {
                    error = "runtime manifest array is missing a comma";
                    return false;
                }
                SkipWhitespace();
            }
            error = "runtime manifest array is truncated";
            return false;
        }

        bool ParseString(std::string& result, std::string& error)
        {
            if (!Consume('"'))
            {
                error = "runtime manifest expected a string";
                return false;
            }
            while (m_offset < m_text.size())
            {
                const unsigned char character = m_text[m_offset++];
                if (character == '"')
                    return true;
                if (character < 0x20 || character > 0x7E)
                {
                    error = "runtime manifest strings must contain printable ASCII";
                    return false;
                }
                if (character == '\\')
                {
                    if (m_offset >= m_text.size())
                        break;
                    const char escaped = m_text[m_offset++];
                    if (escaped != '"' && escaped != '\\' && escaped != '/')
                    {
                        error = "runtime manifest uses an unsupported string escape";
                        return false;
                    }
                    result.push_back(escaped);
                }
                else
                    result.push_back(static_cast<char>(character));
                if (result.size() > 128)
                {
                    error = "runtime manifest string exceeds 128 bytes";
                    return false;
                }
            }
            error = "runtime manifest string is truncated";
            return false;
        }

        bool ParseUnsignedValue(unsigned int& result, std::string& error)
        {
            const size_t start = m_offset;
            if (m_offset >= m_text.size() || m_text[m_offset] < '0' || m_text[m_offset] > '9')
            {
                error = "runtime manifest permits only unsigned integer values";
                return false;
            }
            if (m_text[m_offset] == '0' && m_offset + 1 < m_text.size() && m_text[m_offset + 1] >= '0' && m_text[m_offset + 1] <= '9')
            {
                error = "runtime manifest integer has a leading zero";
                return false;
            }
            uint64_t number = 0;
            while (m_offset < m_text.size() && m_text[m_offset] >= '0' && m_text[m_offset] <= '9')
            {
                number = number * 10 + (m_text[m_offset++] - '0');
                if (number > std::numeric_limits<unsigned int>::max())
                {
                    error = "runtime manifest integer exceeds uint32";
                    return false;
                }
            }
            if (m_offset == start)
                return false;
            result = static_cast<unsigned int>(number);
            return true;
        }

        bool Consume(char expected)
        {
            if (m_offset >= m_text.size() || m_text[m_offset] != expected)
                return false;
            ++m_offset;
            return true;
        }

        void SkipWhitespace()
        {
            while (m_offset < m_text.size() && (m_text[m_offset] == ' ' || m_text[m_offset] == '\t' || m_text[m_offset] == '\r' || m_text[m_offset] == '\n'))
                ++m_offset;
        }

        const std::string& m_text;
        size_t             m_offset{};
    };

    bool HasExactKeys(const SJsonValue& value, std::initializer_list<const char*> keys)
    {
        if (value.type != SJsonValue::EType::Object || value.object.size() != keys.size())
            return false;
        for (const char* key : keys)
            if (value.object.find(key) == value.object.end())
                return false;
        return true;
    }

    const SJsonValue* Member(const SJsonValue& value, const char* key, SJsonValue::EType type)
    {
        const auto found = value.object.find(key);
        return found != value.object.end() && found->second.type == type ? &found->second : nullptr;
    }

    bool IsSafeLeafName(const std::string& name, size_t maximumLength)
    {
        if (name.empty() || name.size() > maximumLength || name == "." || name == "..")
            return false;
        return std::all_of(name.begin(), name.end(),
                           [](unsigned char character)
                           {
                               return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_' ||
                                      character == '-' || character == '.';
                           });
    }

    bool IsSafePackId(const std::string& value)
    {
        return !value.empty() && value.size() <= 15 &&
               std::all_of(
                   value.begin(), value.end(), [](unsigned char character)
                   { return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_' || character == '-'; });
    }

    bool IsLowerSha256(const std::string& value)
    {
        return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char character)
                                                 { return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'); });
    }

    bool ParseFinitePositiveFloat(const std::string& value);

    bool HasExactFileSize64(const SString& path, uint64_t expected)
    {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExW(SharedUtil::FromUTF8(path).c_str(), GetFileExInfoStandard, &data))
            return false;
        const uint64_t actual = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        return actual == expected;
    }

    bool LoadStaticWorldV3Manifest(const SString& path, SStaticWorldV3Manifest& manifest, std::string& error)
    {
        std::ifstream file(SharedUtil::FromUTF8(path), std::ios::binary);
        if (!file)
        {
            error = "static-world-v3 manifest cannot be opened";
            return false;
        }
        file.seekg(0, std::ios::end);
        const std::streamoff length = file.tellg();
        if (length <= 0 || length > STATIC_WORLD_V3_MAX_MANIFEST_BYTES)
        {
            error = "static-world-v3 manifest exceeds its 64 KiB policy";
            return false;
        }
        file.seekg(0, std::ios::beg);
        std::string bytes(static_cast<size_t>(length), '\0');
        if (!file.read(bytes.data(), length))
        {
            error = "static-world-v3 manifest read is truncated";
            return false;
        }

        SJsonValue root;
        if (!CStrictJsonParser(bytes).Parse(root, error) || !HasExactKeys(root, {"format", "policy", "pack_id", "files"}))
        {
            if (error.empty())
                error = "static-world-v3 manifest root schema is not exact";
            return false;
        }
        const SJsonValue* format = Member(root, "format", SJsonValue::EType::Unsigned);
        const SJsonValue* policy = Member(root, "policy", SJsonValue::EType::String);
        const SJsonValue* packId = Member(root, "pack_id", SJsonValue::EType::String);
        const SJsonValue* files = Member(root, "files", SJsonValue::EType::Object);
        if (!format || format->number != STATIC_WORLD_V3_FORMAT || !policy || policy->string != STATIC_WORLD_V3_POLICY || !packId ||
            !IsSafePackId(packId->string) || !files || !HasExactKeys(*files, {"ide", "lod", "images"}))
        {
            error = "static-world-v3 format, policy, pack ID, or file schema is invalid";
            return false;
        }

        const SJsonValue* ide = Member(*files, "ide", SJsonValue::EType::Object);
        const SJsonValue* lod = Member(*files, "lod", SJsonValue::EType::Object);
        const SJsonValue* images = Member(*files, "images", SJsonValue::EType::Array);
        if (!ide || !HasExactKeys(*ide, {"name", "bytes", "sha256"}) || !lod || !HasExactKeys(*lod, {"name", "bytes", "sha256"}) || !images ||
            images->array.empty() || images->array.size() > STATIC_WORLD_V3_MAX_IMAGES)
        {
            error = "static-world-v3 IDE or IMG descriptor count is invalid";
            return false;
        }
        const auto parseFile = [&error](const SJsonValue& value, const std::string& expectedName, unsigned int maximumBytes, SStaticWorldV3File& output)
        {
            if (!HasExactKeys(value, {"name", "bytes", "sha256"}))
                return false;
            const SJsonValue* name = Member(value, "name", SJsonValue::EType::String);
            const SJsonValue* hash = Member(value, "sha256", SJsonValue::EType::String);
            const SJsonValue* fileBytes = Member(value, "bytes", SJsonValue::EType::Unsigned);
            if (!name || name->string != expectedName || !IsSafeLeafName(name->string, 63) || !hash || !IsLowerSha256(hash->string) || !fileBytes ||
                !fileBytes->number || fileBytes->number > maximumBytes)
                return false;
            output = {name->string, hash->string, fileBytes->number};
            return true;
        };

        SStaticWorldV3Manifest parsed;
        if (!parseFile(*ide, "world.ide", STATIC_WORLD_V3_MAX_IDE_BYTES, parsed.ide) ||
            !parseFile(*lod, "world.lod", STATIC_WORLD_V3_MAX_LOD_BYTES, parsed.lod))
        {
            error = "static-world-v3 IDE or LOD descriptor is outside its closed policy";
            return false;
        }
        uint64_t totalBytes = static_cast<uint64_t>(parsed.ide.bytes) + parsed.lod.bytes;
        for (size_t index = 0; index < images->array.size(); ++index)
        {
            char expectedName[16]{};
            _snprintf_s(expectedName, sizeof(expectedName), _TRUNCATE, "w%03u.img", static_cast<unsigned int>(index));
            SStaticWorldV3File image;
            if (!parseFile(images->array[index], expectedName, static_cast<unsigned int>(STATIC_WORLD_V3_MAX_IMG_BYTES), image) || image.bytes % 2048 != 0 ||
                image.bytes > STATIC_WORLD_V3_MAX_TOTAL_BYTES - totalBytes)
            {
                error = "static-world-v3 IMG descriptors are non-contiguous, unaligned, or exceed the aggregate budget";
                return false;
            }
            totalBytes += image.bytes;
            parsed.images.emplace_back(std::move(image));
        }
        parsed.packId = packId->string;
        parsed.manifestSha256 = SharedUtil::GenerateSha256HexString(bytes).ToLower();
        parsed.manifestBytes = static_cast<unsigned int>(bytes.size());
        manifest = std::move(parsed);
        return true;
    }

    bool LoadStaticWorldV3SetManifest(const SString& path, SStaticWorldV3SetManifest& manifest, std::string& error)
    {
        std::ifstream file(SharedUtil::FromUTF8(path), std::ios::binary);
        if (!file)
        {
            error = "static-world-v3-set envelope cannot be opened";
            return false;
        }
        file.seekg(0, std::ios::end);
        const std::streamoff length = file.tellg();
        if (length <= 0 || length > STATIC_WORLD_V3_SET_MAX_MANIFEST_BYTES)
        {
            error = "static-world-v3-set envelope exceeds its 16 KiB policy";
            return false;
        }
        file.seekg(0, std::ios::beg);
        std::string bytes(static_cast<size_t>(length), '\0');
        if (!file.read(bytes.data(), length))
        {
            error = "static-world-v3-set envelope is truncated";
            return false;
        }

        SJsonValue root;
        if (!CStrictJsonParser(bytes).Parse(root, error) || !HasExactKeys(root, {"format", "policy", "set_id", "packs"}))
        {
            if (error.empty())
                error = "static-world-v3-set root schema is not exact";
            return false;
        }
        const SJsonValue*         format = Member(root, "format", SJsonValue::EType::Unsigned);
        const SJsonValue*         policy = Member(root, "policy", SJsonValue::EType::String);
        const SJsonValue*         setId = Member(root, "set_id", SJsonValue::EType::String);
        const SJsonValue*         packs = Member(root, "packs", SJsonValue::EType::Array);
        constexpr const char*     expectedPackIds[] = {"bullworth", "vice-city", "liberty-city", "carcer-city"};
        SStaticWorldV3SetManifest parsed;
        if (!format || format->number != 3 || !policy || policy->string != STATIC_WORLD_V3_SET_POLICY || !setId || !IsLowerSha256(setId->string) || !packs ||
            packs->array.size() != parsed.packs.size())
        {
            error = "static-world-v3-set format, policy, set ID, or pack count is invalid";
            return false;
        }
        for (size_t index = 0; index < parsed.packs.size(); ++index)
        {
            const SJsonValue& value = packs->array[index];
            const SJsonValue* packId = Member(value, "pack_id", SJsonValue::EType::String);
            const SJsonValue* contentId = Member(value, "content_id", SJsonValue::EType::String);
            if (!HasExactKeys(value, {"pack_id", "content_id"}) || !packId || packId->string != expectedPackIds[index] || !contentId ||
                !IsLowerSha256(contentId->string))
            {
                error = "static-world-v3-set pack identities are not exact or canonically ordered";
                return false;
            }
            parsed.packs[index] = {packId->string, contentId->string};
        }
        parsed.setId = setId->string;
        parsed.manifestSha256 = SharedUtil::GenerateSha256HexString(bytes).ToLower();
        parsed.manifestBytes = static_cast<unsigned int>(bytes.size());
        SNativeWorldV3SetRequestSA identity;
        identity.setId = parsed.setId;
        identity.packs = parsed.packs;
        if (GenerateNativeWorldV3SetId(identity) != parsed.setId)
        {
            error = "static-world-v3-set set ID differs from its ordered pack identities";
            return false;
        }
        manifest = std::move(parsed);
        return true;
    }

    bool IsStaticWorldV3GeneratedStem(const std::string& value, const std::string& nameSpace, char kind, size_t digits)
    {
        if (nameSpace.size() != 2 || value.size() != 3 + digits || value.compare(0, 2, nameSpace) != 0 || value[2] != kind)
            return false;
        return std::all_of(value.begin() + 3, value.end(),
                           [](unsigned char character) { return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'z'); });
    }

    std::string StaticWorldV3Base36(unsigned int value, size_t width)
    {
        static constexpr char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
        std::string           result(width, '0');
        for (size_t index = width; index-- > 0;)
        {
            result[index] = digits[value % 36];
            value /= 36;
        }
        return value == 0 ? result : std::string{};
    }

    unsigned int StaticWorldV3UppercaseKey(const std::string& value)
    {
        unsigned int key = 0xFFFFFFFF;
        for (unsigned char character : value)
        {
            if (character >= 'a' && character <= 'z')
                character -= 'a' - 'A';
            key ^= character;
            for (unsigned int bit = 0; bit < 8; ++bit)
                key = (key >> 1) ^ ((key & 1) ? 0xEDB88320 : 0);
        }
        return key;
    }

    bool ParseStaticWorldV3Ide(const SString& path, SStaticWorldV3Ide& plan, std::string& error)
    {
        std::ifstream file(SharedUtil::FromUTF8(path));
        if (!file)
        {
            error = "static-world-v3 IDE cannot be opened";
            return false;
        }
        enum class ESection
        {
            None,
            Objects,
            TimedObjects,
        } section = ESection::None;
        bool         sawObjects = false;
        bool         sawTimedObjects = false;
        unsigned int lineNumber = 0;
        std::string  line;
        while (std::getline(file, line))
        {
            ++lineNumber;
            if (line.size() > 512 || !std::all_of(line.begin(), line.end(), [](unsigned char character) { return character <= 0x7F; }))
            {
                error = "static-world-v3 IDE line exceeds the ASCII/length contract";
                return false;
            }
            line = Trim(line);
            if (line.empty() || line[0] == '#')
                continue;
            if (line == "objs" || line == "tobj")
            {
                const bool objects = line == "objs";
                if (section != ESection::None || (objects ? sawObjects : sawTimedObjects))
                {
                    error = "static-world-v3 IDE contains a duplicate or nested section";
                    return false;
                }
                section = objects ? ESection::Objects : ESection::TimedObjects;
                (objects ? sawObjects : sawTimedObjects) = true;
                continue;
            }
            if (line == "end")
            {
                if (section == ESection::None)
                {
                    error = "static-world-v3 IDE contains an unmatched end";
                    return false;
                }
                section = ESection::None;
                continue;
            }
            if (section == ESection::None)
            {
                error = "static-world-v3 IDE contains an unsupported section";
                return false;
            }

            const std::vector<std::string> fields = SplitCsv(line);
            if ((section == ESection::Objects && fields.size() != 6) || (section == ESection::TimedObjects && fields.size() != 8))
            {
                error = "static-world-v3 IDE contains a malformed model row";
                return false;
            }
            unsigned int id = 0, meshCount = 0, flags = 0, timeOn = 0, timeOff = 0;
            const bool   timedValid = section != ESection::TimedObjects || (ParseUnsigned(fields[6], timeOn) && ParseUnsigned(fields[7], timeOff) &&
                                                                          timeOn <= 23 && timeOff <= 23 && timeOn != timeOff);
            if (!ParseUnsigned(fields[0], id) || id < STATIC_WORLD_V3_FIRST_CUSTOM_MODEL || id > STATIC_WORLD_V3_LAST_MODEL ||
                !ParseUnsigned(fields[3], meshCount) || meshCount != 1 || !ParseFinitePositiveFloat(fields[4]) || !ParseUnsigned(fields[5], flags) ||
                flags > 0x007FFFFF || !timedValid || !plan.modelIds.insert(id).second)
            {
                error = "static-world-v3 IDE contains an invalid or duplicate model";
                return false;
            }
            if (plan.nameSpace.empty())
            {
                if (fields[1].size() != 7)
                {
                    error = "static-world-v3 model namespace is invalid";
                    return false;
                }
                plan.nameSpace = fields[1].substr(0, 2);
                if (plan.nameSpace[0] < 'a' || plan.nameSpace[0] > 'z' ||
                    !((plan.nameSpace[1] >= 'a' && plan.nameSpace[1] <= 'z') || (plan.nameSpace[1] >= '0' && plan.nameSpace[1] <= '9')))
                {
                    error = "static-world-v3 model namespace is outside the short-name policy";
                    return false;
                }
            }
            if (!IsStaticWorldV3GeneratedStem(fields[1], plan.nameSpace, 'm', 4) || !IsStaticWorldV3GeneratedStem(fields[2], plan.nameSpace, 't', 3) ||
                !plan.modelFiles.insert(fields[1] + ".dff").second || !plan.modelFilesById.emplace(id, fields[1] + ".dff").second)
            {
                error = "static-world-v3 IDE model or TXD name is not canonical";
                return false;
            }
            if (section == ESection::TimedObjects)
                ++plan.timedModels;
            else if (flags & 0x1000)
                ++plan.damageModels;
            else
                ++plan.ordinaryModels;
            plan.txdStems.insert(fields[2]);
        }

        if (section != ESection::None || !sawObjects || !sawTimedObjects || plan.modelIds.empty() || plan.modelIds.size() > STATIC_WORLD_V3_MAX_MODELS ||
            plan.txdStems.empty() || plan.txdStems.size() > STATIC_WORLD_V3_MAX_TXDS ||
            *plan.modelIds.rbegin() - *plan.modelIds.begin() + 1 != plan.modelIds.size())
        {
            error = "static-world-v3 IDE counts, sections, or contiguous ID range are invalid";
            return false;
        }
        plan.firstModel = *plan.modelIds.begin();
        plan.lastModel = *plan.modelIds.rbegin();
        for (size_t index = 0; index < plan.modelFiles.size(); ++index)
        {
            const std::string expected = plan.nameSpace + "m" + StaticWorldV3Base36(static_cast<unsigned int>(index), 4) + ".dff";
            const auto        found = plan.modelFilesById.find(plan.firstModel + static_cast<unsigned int>(index));
            if (found == plan.modelFilesById.end() || found->second != expected)
            {
                error = "static-world-v3 model ID/name remap is not a contiguous deterministic sequence";
                return false;
            }
        }
        for (size_t index = 0; index < plan.txdStems.size(); ++index)
        {
            const std::string expected = plan.nameSpace + "t" + StaticWorldV3Base36(static_cast<unsigned int>(index), 3);
            if (plan.txdStems.find(expected) == plan.txdStems.end())
            {
                error = "static-world-v3 TXD names are not a contiguous deterministic sequence";
                return false;
            }
        }
        std::set<unsigned int> modelKeys;
        std::set<unsigned int> txdKeys;
        for (const std::string& file : plan.modelFiles)
        {
            const std::string stem = file.substr(0, file.size() - 4);
            if (!modelKeys.insert(StaticWorldV3UppercaseKey(stem)).second)
            {
                error = "static-world-v3 generated model names collide in GTA uppercase key space";
                return false;
            }
        }
        for (const std::string& stem : plan.txdStems)
        {
            if (!txdKeys.insert(StaticWorldV3UppercaseKey(stem)).second)
            {
                error = "static-world-v3 generated TXD names collide in GTA uppercase key space";
                return false;
            }
        }
        return true;
    }

    bool LoadRuntimeManifest(const SString& path, std::string& error)
    {
        std::ifstream file(SharedUtil::FromUTF8(path), std::ios::binary);
        if (!file)
        {
            error = "native-world.json cannot be opened";
            return false;
        }
        file.seekg(0, std::ios::end);
        const std::streamoff length = file.tellg();
        if (length <= 0 || length > g_policy->maximumManifestBytes)
        {
            error = "runtime manifest byte length exceeds trusted policy";
            return false;
        }
        file.seekg(0, std::ios::beg);
        std::string bytes(static_cast<size_t>(length), '\0');
        if (!file.read(bytes.data(), length))
        {
            error = "runtime manifest read is truncated";
            return false;
        }

        SJsonValue root;
        if (!CStrictJsonParser(bytes).Parse(root, error))
            return false;
        const bool legacyManifest = g_policy->format == 1;
        if ((legacyManifest && !HasExactKeys(root, {"format", "pack_id", "files"})) ||
            (!legacyManifest && !HasExactKeys(root, {"format", "policy", "pack_id", "files"})))
        {
            error = "runtime manifest root schema differs from its compiled format policy";
            return false;
        }
        const SJsonValue* format = Member(root, "format", SJsonValue::EType::Unsigned);
        const SJsonValue* policy = legacyManifest ? nullptr : Member(root, "policy", SJsonValue::EType::String);
        const SJsonValue* packId = Member(root, "pack_id", SJsonValue::EType::String);
        const SJsonValue* files = Member(root, "files", SJsonValue::EType::Object);
        const bool        packMatches =
            legacyManifest ? packId && packId->string == g_policy->key : packId && policy && policy->string == g_policy->key && IsSafePackId(packId->string);
        if (!format || !files || format->number != g_policy->format || !packMatches || !HasExactKeys(*files, {"ide", "img"}))
        {
            error = "runtime manifest format, policy, pack ID, or nested schema is invalid";
            return false;
        }

        const SJsonValue* ide = Member(*files, "ide", SJsonValue::EType::Object);
        const SJsonValue* img = Member(*files, "img", SJsonValue::EType::Object);
        if (!ide || !img || !HasExactKeys(*ide, {"name", "bytes", "sha256"}) || !HasExactKeys(*img, {"name", "bytes", "sha256"}))
        {
            error = "runtime manifest file or model-store schema is invalid";
            return false;
        }

        const auto        stringMember = [](const SJsonValue& object, const char* key) { return Member(object, key, SJsonValue::EType::String); };
        const auto        numberMember = [](const SJsonValue& object, const char* key) { return Member(object, key, SJsonValue::EType::Unsigned); };
        const SJsonValue* ideName = stringMember(*ide, "name");
        const SJsonValue* imgName = stringMember(*img, "name");
        const SJsonValue* ideHash = stringMember(*ide, "sha256");
        const SJsonValue* imgHash = stringMember(*img, "sha256");
        const SJsonValue* ideBytes = numberMember(*ide, "bytes");
        const SJsonValue* imgBytes = numberMember(*img, "bytes");
        if (!ideName || !imgName || !ideHash || !imgHash || !ideBytes || !imgBytes || !IsSafeLeafName(ideName->string, 63) ||
            !IsSafeLeafName(imgName->string, 63) || !IsLowerSha256(ideHash->string) || !IsLowerSha256(imgHash->string) || !ideBytes->number ||
            ideBytes->number > g_policy->maximumIdeBytes || !imgBytes->number || imgBytes->number % 2048 != 0 ||
            imgBytes->number / 2048 > g_policy->maximumImgSectors)
        {
            error = "runtime manifest contains an invalid filename, hash, or integer field";
            return false;
        }

        SNativeWorldPackRuntimeDataSA manifest;
        manifest.format = format->number;
        manifest.policyKey = g_policy->key;
        manifest.packId = packId->string;
        // Runtime manifests require lowercase SHA-256 text. Normalize the
        // locally generated manifest digest to the same canonical spelling.
        manifest.manifestSha256 = SharedUtil::GenerateSha256HexString(bytes).ToLower();
        manifest.manifestBytes = static_cast<unsigned int>(bytes.size());
        manifest.ideFileName = ideName->string;
        manifest.imgFileName = imgName->string;
        manifest.ideSha256 = ideHash->string;
        manifest.imgSha256 = imgHash->string;
        manifest.ideBytes = ideBytes->number;
        manifest.imgBytes = imgBytes->number;

        // Build the merged view only after the complete schema is accepted.
        g_manifest = std::move(manifest);
        g_iplNamePointers.clear();
        for (const std::string& name : g_manifest.iplNames)
            g_iplNamePointers.push_back(name.c_str());
        g_runtimeDescriptor = {
            g_policy->key,
            g_policy->displayName,
            g_policy->logPrefix,
            g_policy->featureEnvironment,
            g_activeDirectory.c_str(),
            g_manifest.ideFileName.c_str(),
            g_manifest.imgFileName.c_str(),
            nullptr,
            g_manifest.ideSha256.c_str(),
            g_manifest.imgSha256.c_str(),
            0,
            0,
            0,
            0,
            g_policy->txdPoolCapacity,
            g_policy->stockColOccupied,
            g_policy->colPoolCapacity,
            g_policy->stockIplOccupied,
            g_policy->iplPoolCapacity,
            nullptr,
            0,
            0,
            0,
            0,
            g_policy->expectedArchiveId,
            g_policy->stockModelStores,
            {},
            g_policy->txdPoolProfiles,
            g_policy->txdPoolProfileCount,
        };
        g_pack = &g_runtimeDescriptor;
        return true;
    }

    bool ValidateDescriptor(std::string& error)
    {
        const SNativeWorldPackDescriptorSA& pack = Pack();
        if (!pack.key || !pack.displayName || !pack.logPrefix || !pack.featureEnvironment || !pack.directoryPath || !pack.ideFileName || !pack.imgFileName ||
            !pack.colFileName || !pack.ideSha256 || !pack.imgSha256 || !pack.iplNames || !pack.iplCount || !pack.txdPoolProfiles || !pack.txdPoolProfileCount)
        {
            error = "native world-pack descriptor has a missing required field";
            return false;
        }
        if (strcmp(pack.key, g_policy->key) != 0 || pack.modelFirst > pack.modelLast || pack.modelLast > g_policy->maximumModelId ||
            pack.modelLast - pack.modelFirst + 1 != pack.modelCount ||
            pack.addedModelStores.atomic + pack.addedModelStores.damageAtomic + pack.addedModelStores.time != pack.modelCount)
        {
            error = "native world-pack descriptor model range or store deltas are inconsistent";
            return false;
        }
        if (pack.stockModelStores.atomic > g_policy->modelStoreCapacities.atomic ||
            pack.addedModelStores.atomic > g_policy->modelStoreCapacities.atomic - pack.stockModelStores.atomic ||
            pack.stockModelStores.damageAtomic > g_policy->modelStoreCapacities.damageAtomic ||
            pack.addedModelStores.damageAtomic > g_policy->modelStoreCapacities.damageAtomic - pack.stockModelStores.damageAtomic ||
            pack.stockModelStores.time > g_policy->modelStoreCapacities.time ||
            pack.addedModelStores.time > g_policy->modelStoreCapacities.time - pack.stockModelStores.time)
        {
            error = "native world-pack model-store additions exceed trusted capacities";
            return false;
        }
        if (!pack.modelCount || pack.modelCount > g_policy->maximumModelCount || !pack.txdCount || pack.txdCount > g_policy->maximumTxdCount ||
            pack.txdCount > pack.txdPoolCapacity || pack.stockColOccupied >= pack.colPoolCapacity ||
            pack.stockIplOccupied + pack.iplCount > pack.iplPoolCapacity || !pack.imgEntryCount || !pack.imgSectorCount || !pack.largestImgEntryBlocks ||
            pack.imgEntryCount > g_policy->maximumImgEntries || pack.imgSectorCount > g_policy->maximumImgSectors ||
            pack.largestImgEntryBlocks > g_policy->maximumImgEntryBlocks || pack.largestImgEntryBlocks > pack.imgSectorCount ||
            pack.imgEntryCount != pack.modelCount + pack.txdCount + pack.iplCount + 1 || strlen(pack.ideSha256) != 64 || strlen(pack.imgSha256) != 64)
        {
            error = "native world-pack descriptor pool, archive, or hash contract is inconsistent";
            return false;
        }
        std::set<std::string> uniqueIpls;
        for (unsigned int index = 0; index < pack.iplCount; ++index)
            if (!pack.iplNames[index] || !*pack.iplNames[index] || !uniqueIpls.insert(pack.iplNames[index]).second)
            {
                error = "native world-pack descriptor has an empty or duplicate IPL name";
                return false;
            }
        return true;
    }

    bool IsSafeIdeStem(const std::string& value)
    {
        return !value.empty() && value.size() <= 19 &&
               std::all_of(value.begin(), value.end(),
                           [](unsigned char character)
                           {
                               return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                                      (character >= '0' && character <= '9') || character == '_' || character == '-';
                           });
    }

    bool ParseFinitePositiveFloat(const std::string& value)
    {
        char*       end = nullptr;
        const float number = strtof(value.c_str(), &end);
        return end && end != value.c_str() && *end == '\0' && std::isfinite(number) && number > 0.0f && number <= 100000.0f;
    }

    bool ParseIde(const SString& path, SIdePlan& plan, std::string& error)
    {
        std::ifstream file(SharedUtil::FromUTF8(path));
        if (!file)
        {
            error = SString("%s cannot be opened", Pack().ideFileName);
            return false;
        }

        enum class ESection
        {
            None,
            Objects,
            TimedObjects,
        } section = ESection::None;
        bool         sawObjects = false;
        bool         sawTimedObjects = false;
        unsigned int lineNumber = 0;

        std::string line;
        while (std::getline(file, line))
        {
            ++lineNumber;
            if (line.size() > 512 || !std::all_of(line.begin(), line.end(), [](unsigned char character) { return character <= 0x7F; }))
            {
                error = SString("%s line %u exceeds the ASCII/length contract", Pack().ideFileName, lineNumber);
                return false;
            }
            line = Trim(line);
            if (line.empty() || line[0] == '#')
                continue;
            if (line == "objs")
            {
                if (section != ESection::None || sawObjects)
                {
                    error = SString("%s has a duplicate or nested objs section", Pack().ideFileName);
                    return false;
                }
                sawObjects = true;
                section = ESection::Objects;
                continue;
            }
            if (line == "tobj")
            {
                if (section != ESection::None || sawTimedObjects)
                {
                    error = SString("%s has a duplicate or nested tobj section", Pack().ideFileName);
                    return false;
                }
                sawTimedObjects = true;
                section = ESection::TimedObjects;
                continue;
            }
            if (line == "end")
            {
                if (section == ESection::None)
                {
                    error = SString("%s has an unmatched end", Pack().ideFileName);
                    return false;
                }
                section = ESection::None;
                continue;
            }
            if (section == ESection::None)
            {
                error = SString("%s contains an unsupported section", Pack().ideFileName);
                return false;
            }

            const std::vector<std::string> fields = SplitCsv(line);
            if ((section == ESection::Objects && fields.size() != 6) || (section == ESection::TimedObjects && fields.size() != 8))
            {
                error = SString("%s contains a malformed row", Pack().ideFileName);
                return false;
            }

            unsigned int id = 0;
            unsigned int flags = 0;
            unsigned int meshCount = 0;
            unsigned int timeOn = 0;
            unsigned int timeOff = 0;
            const bool   timedFieldsValid = section != ESection::TimedObjects || (ParseUnsigned(fields[6], timeOn) && ParseUnsigned(fields[7], timeOff) &&
                                                                                timeOn <= 23 && timeOff <= 23 && timeOn != timeOff);
            if (!ParseUnsigned(fields[0], id) || !ParseUnsigned(fields[3], meshCount) || meshCount != 1 || !ParseFinitePositiveFloat(fields[4]) ||
                !ParseUnsigned(fields[5], flags) || flags > 0x007FFFFF || !timedFieldsValid || id > g_policy->maximumModelId ||
                !plan.modelIds.insert(id).second || !IsSafeIdeStem(fields[1]) || !IsSafeIdeStem(fields[2]))
            {
                error = SString("%s contains an invalid or duplicate model", Pack().ideFileName);
                return false;
            }
            plan.modelNames.insert(fields[1] + ".dff");
            plan.modelFileNames[id] = fields[1] + ".dff";
            plan.txdNames.insert(fields[2]);
            plan.modelTxdNames[id] = fields[2];
            if (section == ESection::TimedObjects)
            {
                ++plan.time;
                plan.modelVtables[id] = TIME_MODEL_VTABLE;
            }
            else if (flags & 0x1000)
            {
                ++plan.damage;
                plan.modelVtables[id] = DAMAGE_MODEL_VTABLE;
            }
            else
            {
                ++plan.atomic;
                plan.modelVtables[id] = ATOMIC_MODEL_VTABLE;
            }
        }

        if (section != ESection::None || (!sawObjects && !sawTimedObjects) || plan.modelIds.empty() || plan.modelIds.size() > g_policy->maximumModelCount ||
            plan.txdNames.empty() || plan.txdNames.size() > g_policy->maximumTxdCount ||
            *plan.modelIds.rbegin() - *plan.modelIds.begin() + 1 != plan.modelIds.size() || plan.modelNames.size() != plan.modelIds.size())
        {
            error = SString("%s derived counts or contiguous ID range exceed trusted policy", Pack().ideFileName);
            return false;
        }
        g_runtimeDescriptor.modelFirst = *plan.modelIds.begin();
        g_runtimeDescriptor.modelLast = *plan.modelIds.rbegin();
        g_runtimeDescriptor.modelCount = static_cast<unsigned int>(plan.modelIds.size());
        g_runtimeDescriptor.txdCount = static_cast<unsigned int>(plan.txdNames.size());
        g_runtimeDescriptor.addedModelStores = {plan.atomic, plan.damage, plan.time};
        return true;
    }

    std::string EntryName(const SImgEntry& entry)
    {
        const void* terminator = memchr(entry.name, '\0', sizeof(entry.name));
        if (!terminator)
            return {};
        return std::string(entry.name, static_cast<const char*>(terminator));
    }

    bool ReadStaticWorldV3Entry(const SStaticWorldV3ImgEntry& location, unsigned int maximumBytes, std::vector<BYTE>& data, std::string& error)
    {
        const uint64_t bytes = static_cast<uint64_t>(location.entry.size) * 2048;
        if (!bytes || bytes > maximumBytes)
        {
            error = "static-world-v3 member exceeds its audit buffer";
            return false;
        }
        std::ifstream file(location.archive, std::ios::binary);
        if (!file)
        {
            error = "static-world-v3 IMG cannot be reopened for member audit";
            return false;
        }
        file.seekg(static_cast<std::streamoff>(location.entry.offset) * 2048, std::ios::beg);
        data.resize(static_cast<size_t>(bytes));
        if (!file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size())))
        {
            error = "static-world-v3 IMG member is truncated";
            return false;
        }
        return true;
    }

    bool ValidateStaticWorldV3Archive(const std::filesystem::path& path, uint64_t expectedBytes, const SStaticWorldV3Ide& ide,
                                      SStaticWorldV3Inventory& inventory, std::string& error)
    {
        const SString nativePath = path.string().c_str();
        if (!IsNativePathSafe(nativePath) || !IsSafeRegularFile(nativePath) || !HasExactFileSize64(nativePath, expectedBytes))
        {
            error = "static-world-v3 IMG path or byte length differs from its manifest";
            return false;
        }

        const WString widePath = SharedUtil::FromUTF8(nativePath);
        HANDLE        file = CreateFileW(widePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            error = "static-world-v3 IMG cannot be opened";
            return false;
        }
        SImgHeader header{};
        DWORD      read = 0;
        if (!ReadFile(file, &header, sizeof(header), &read, nullptr) || read != sizeof(header) || memcmp(header.magic, "VER2", 4) != 0 || !header.count ||
            header.count > STATIC_WORLD_V3_MAX_MODELS + STATIC_WORLD_V3_MAX_TXDS + STATIC_WORLD_V3_MAX_SPATIAL_GROUPS * 2)
        {
            CloseHandle(file);
            error = "static-world-v3 IMG header or directory count is invalid";
            return false;
        }
        std::vector<SImgEntry> entries(header.count);
        const DWORD            directoryBytes = static_cast<DWORD>(entries.size() * sizeof(SImgEntry));
        if (!ReadFile(file, entries.data(), directoryBytes, &read, nullptr) || read != directoryBytes)
        {
            CloseHandle(file);
            error = "static-world-v3 IMG directory is truncated";
            return false;
        }

        const uint64_t                       sectorCount = expectedBytes / 2048;
        const uint64_t                       directoryEndSector = (sizeof(SImgHeader) + static_cast<uint64_t>(directoryBytes) + 2047) / 2048;
        std::vector<std::pair<DWORD, DWORD>> ranges;
        for (const SImgEntry& entry : entries)
        {
            const std::string name = EntryName(entry);
            const size_t      dot = name.rfind('.');
            const uint64_t    endSector = static_cast<uint64_t>(entry.offset) + entry.size;
            if (!IsSafeLeafName(name, 23) || dot == std::string::npos || dot == 0 || dot != name.find('.') || !entry.size ||
                entry.streamingSize != entry.size || entry.offset < directoryEndSector || endSector > sectorCount)
            {
                CloseHandle(file);
                error = "static-world-v3 IMG contains an unsafe or out-of-bounds directory entry";
                return false;
            }
            if (!inventory.entries.emplace(name, SStaticWorldV3ImgEntry{path, entry}).second)
            {
                CloseHandle(file);
                error = "static-world-v3 IMG set contains a duplicate cross-archive member";
                return false;
            }
            ranges.emplace_back(entry.offset, entry.offset + entry.size);

            BYTE          prefix[12]{};
            LARGE_INTEGER offset{};
            offset.QuadPart = static_cast<LONGLONG>(entry.offset) * 2048;
            if (!SetFilePointerEx(file, offset, nullptr, FILE_BEGIN) || !ReadFile(file, prefix, sizeof(prefix), &read, nullptr) || read != sizeof(prefix))
            {
                CloseHandle(file);
                error = "static-world-v3 IMG member prefix is truncated";
                return false;
            }
            const std::string extension = name.substr(dot);
            const std::string stem = name.substr(0, dot);
            if (extension == ".dff" || extension == ".txd")
            {
                const DWORD expectedRoot = extension == ".dff" ? 0x10 : 0x16;
                DWORD       root = 0;
                DWORD       payloadBytes = 0;
                DWORD       libraryId = 0;
                memcpy(&root, prefix, sizeof(root));
                memcpy(&payloadBytes, prefix + 4, sizeof(payloadBytes));
                memcpy(&libraryId, prefix + 8, sizeof(libraryId));
                if (root != expectedRoot || libraryId != STATIC_WORLD_V3_RW_LIBRARY_ID || payloadBytes > static_cast<uint64_t>(entry.size) * 2048 - 12)
                {
                    CloseHandle(file);
                    error = "static-world-v3 DFF/TXD root is not canonical RenderWare 3.6";
                    return false;
                }
            }
            else if (extension == ".col")
            {
                // GTA's native COL1 records remain canonical COLL records.
                // The offline admission pass validates their sequential layout
                // just as strictly as COL3 instead of performing a lossy
                // geometry conversion solely to normalize the magic.
                const bool coll = memcmp(prefix, "COLL", 4) == 0;
                const bool col3 = memcmp(prefix, "COL3", 4) == 0;
                if ((!coll && !col3) || !IsStaticWorldV3GeneratedStem(stem, ide.nameSpace, 'c', 2) || !inventory.colStems.insert(stem).second)
                {
                    CloseHandle(file);
                    error = "static-world-v3 COL member is not canonical";
                    return false;
                }
            }
            else if (extension == ".ipl")
            {
                if (memcmp(prefix, "bnry", 4) != 0 || !IsStaticWorldV3GeneratedStem(stem, ide.nameSpace, 'i', 2) || !inventory.iplStems.insert(stem).second)
                {
                    CloseHandle(file);
                    error = "static-world-v3 IPL member is not canonical";
                    return false;
                }
            }
            else
            {
                CloseHandle(file);
                error = "static-world-v3 IMG contains an unsupported member type";
                return false;
            }
        }
        CloseHandle(file);
        std::sort(ranges.begin(), ranges.end());
        for (size_t index = 1; index < ranges.size(); ++index)
        {
            if (ranges[index].first < ranges[index - 1].second)
            {
                error = "static-world-v3 IMG contains overlapping members";
                return false;
            }
        }
        return true;
    }

    bool ValidateStaticWorldV3Cols(const SStaticWorldV3Ide& ide, SStaticWorldV3Inventory& inventory, std::string& error)
    {
        std::set<unsigned int> ids;
        std::set<std::string>  names;
        for (const std::string& stem : inventory.colStems)
        {
            const auto found = inventory.entries.find(stem + ".col");
            if (found == inventory.entries.end())
            {
                error = "static-world-v3 COL inventory binding is incomplete";
                return false;
            }
            std::vector<BYTE> data;
            if (!ReadStaticWorldV3Entry(found->second, 16 * 1024 * 1024, data, error))
                return false;
            size_t       offset = 0;
            unsigned int recordCount = 0;
            while (offset < data.size() && data[offset] != 0)
            {
                const bool coll = data.size() - offset >= 4 && memcmp(data.data() + offset, "COLL", 4) == 0;
                const bool col3 = data.size() - offset >= 4 && memcmp(data.data() + offset, "COL3", 4) == 0;
                if (data.size() - offset < 32 || (!coll && !col3))
                {
                    error = "static-world-v3 COL archive contains a non-canonical record";
                    return false;
                }
                unsigned int payloadBytes = 0;
                memcpy(&payloadBytes, data.data() + offset + 4, sizeof(payloadBytes));
                const uint64_t recordEnd = static_cast<uint64_t>(offset) + 8 + payloadBytes;
                if (payloadBytes < 24 || recordEnd > data.size())
                {
                    error = "static-world-v3 COL record boundary is invalid";
                    return false;
                }
                const char* terminator = static_cast<const char*>(memchr(data.data() + offset + 8, '\0', 22));
                if (!terminator)
                {
                    error = "static-world-v3 COL model name is unterminated";
                    return false;
                }
                const std::string name(reinterpret_cast<const char*>(data.data() + offset + 8), terminator);
                unsigned short    id = 0;
                memcpy(&id, data.data() + offset + 30, sizeof(id));
                const auto expectedName = ide.modelFilesById.find(id);
                if (!IsStaticWorldV3GeneratedStem(name, ide.nameSpace, 'm', 4) || expectedName == ide.modelFilesById.end() ||
                    expectedName->second != name + ".dff" || !ids.insert(id).second || !names.insert(name + ".dff").second)
                {
                    error = "static-world-v3 COL record name or model ID differs from the IDE";
                    return false;
                }
                inventory.colModelIds[stem.substr(3)].insert(id);
                offset = static_cast<size_t>(recordEnd);
                ++recordCount;
            }
            if (!recordCount || std::any_of(data.begin() + offset, data.end(), [](BYTE value) { return value != 0; }))
            {
                error = "static-world-v3 COL member is empty or has non-zero archive padding";
                return false;
            }
        }
        // Some source LODs intentionally have no collision. Every supplied
        // record must still map one-to-one to its IDE row; absent records stay
        // absent instead of gaining synthetic geometry.
        return true;
    }

    bool ValidateStaticWorldV3Ipls(const SStaticWorldV3Ide& ide, SStaticWorldV3Inventory& inventory, std::string& error)
    {
        unsigned int                        totalInstances = 0;
        std::map<unsigned int, std::string> generatedModelOwner;
        for (const std::string& stem : inventory.iplStems)
        {
            const auto found = inventory.entries.find(stem + ".ipl");
            if (found == inventory.entries.end())
            {
                error = "static-world-v3 IPL inventory binding is incomplete";
                return false;
            }
            std::vector<BYTE> data;
            if (!ReadStaticWorldV3Entry(found->second, STATIC_WORLD_V3_MAX_IPL_BYTES, data, error))
                return false;
            const auto*    header = reinterpret_cast<const SBinaryIplHeader*>(data.data());
            const DWORD    count = header->counts[0];
            const DWORD    instanceOffset = header->sections[0];
            const uint64_t instancesEnd = static_cast<uint64_t>(instanceOffset) + static_cast<uint64_t>(count) * sizeof(SBinaryIplInstance);
            bool           unsupportedSection = false;
            for (unsigned int section = 1; section < 6; ++section)
                unsupportedSection = unsupportedSection || header->counts[section] != 0;
            for (unsigned int section = 1; section < 12; ++section)
                unsupportedSection = unsupportedSection || header->sections[section] != 0;
            if (memcmp(header->magic, "bnry", 4) != 0 || !count || count > STATIC_WORLD_V3_MAX_PLACEMENTS - totalInstances ||
                instanceOffset != sizeof(SBinaryIplHeader) || instancesEnd > data.size() || unsupportedSection)
            {
                error = "static-world-v3 binary IPL header or aggregate count is invalid";
                return false;
            }
            const auto* instances = reinterpret_cast<const SBinaryIplInstance*>(data.data() + instanceOffset);
            for (DWORD index = 0; index < count; ++index)
            {
                const SBinaryIplInstance& instance = instances[index];
                bool                      finite = true;
                for (float coordinate : instance.position)
                    finite = finite && std::isfinite(coordinate);
                float quaternionLength = 0.0f;
                for (float component : instance.quaternion)
                {
                    finite = finite && std::isfinite(component);
                    quaternionLength += component * component;
                }
                const bool generated = instance.modelId >= 0 && ide.modelIds.find(static_cast<unsigned int>(instance.modelId)) != ide.modelIds.end();
                const bool stock = instance.modelId >= 0 && static_cast<unsigned int>(instance.modelId) <= STATIC_WORLD_V3_LAST_STOCK_MODEL;
                if (!finite || instance.position[0] < MIN_STATIC_WORLD_XY || instance.position[0] > MAX_STATIC_WORLD_XY ||
                    instance.position[1] < MIN_STATIC_WORLD_XY || instance.position[1] > MAX_STATIC_WORLD_XY ||
                    fabsf(instance.position[2]) > MAX_STATIC_WORLD_Z || quaternionLength < 0.25f || quaternionLength > 2.25f || (!generated && !stock) ||
                    instance.instanceType != 0 || instance.lodIndex != -1)
                {
                    error = "static-world-v3 binary IPL contains an invalid instance";
                    return false;
                }
                if (generated)
                {
                    const unsigned int modelId = static_cast<unsigned int>(instance.modelId);
                    const std::string  ordinal = stem.substr(3);
                    const auto [owner, inserted] = generatedModelOwner.emplace(modelId, ordinal);
                    if (!inserted && owner->second != ordinal)
                    {
                        error = "static-world-v3 generated model is shared by multiple spatial IPLs";
                        return false;
                    }
                    inventory.iplModelIds[ordinal].insert(modelId);
                }
            }
            const auto paddingBegin = data.begin() + static_cast<std::vector<BYTE>::difference_type>(instancesEnd);
            if (std::any_of(paddingBegin, data.end(), [](BYTE value) { return value != 0; }))
            {
                error = "static-world-v3 binary IPL has non-zero archive padding";
                return false;
            }
            totalInstances += count;
            inventory.iplPlacementCounts.push_back(count);
        }
        inventory.placements = totalInstances;
        return true;
    }

    bool ValidateStaticWorldV3LodMap(const std::filesystem::path& path, const SStaticWorldV3Inventory& inventory, std::string& error)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            error = "static-world-v3 LOD map cannot be opened";
            return false;
        }
        file.seekg(0, std::ios::end);
        const std::streamoff length = file.tellg();
        if (length < static_cast<std::streamoff>(sizeof(SStaticWorldV3LodHeader)) || length > STATIC_WORLD_V3_MAX_LOD_BYTES)
        {
            error = "static-world-v3 LOD map length is outside its closed policy";
            return false;
        }
        file.seekg(0, std::ios::beg);
        std::vector<BYTE> bytes(static_cast<size_t>(length));
        if (!file.read(reinterpret_cast<char*>(bytes.data()), length))
        {
            error = "static-world-v3 LOD map is truncated";
            return false;
        }
        const auto*    header = reinterpret_cast<const SStaticWorldV3LodHeader*>(bytes.data());
        const uint64_t expectedBytes = sizeof(*header) + static_cast<uint64_t>(header->groupCount + header->anchorCount) * sizeof(DWORD) +
                                       static_cast<uint64_t>(header->linkCount) * sizeof(SStaticWorldV3LodLink);
        if (memcmp(header->magic, "NWL3LOD\0", 8) != 0 || header->version != 1 || header->reserved != 0 ||
            header->groupCount != inventory.iplPlacementCounts.size() || !header->groupCount || header->groupCount > STATIC_WORLD_V3_MAX_SPATIAL_GROUPS ||
            header->placementCount != inventory.placements || !header->placementCount || header->placementCount > STATIC_WORLD_V3_MAX_PLACEMENTS ||
            header->anchorCount > 4096 || header->linkCount > 4096 || header->anchorCount + header->linkCount > 4096 ||
            header->anchorCount != header->linkCount || expectedBytes != bytes.size())
        {
            error = "static-world-v3 LOD header, length, or scratch budget is invalid";
            return false;
        }

        const DWORD* groupCounts = reinterpret_cast<const DWORD*>(bytes.data() + sizeof(*header));
        for (size_t index = 0; index < inventory.iplPlacementCounts.size(); ++index)
        {
            if (!groupCounts[index] || groupCounts[index] != inventory.iplPlacementCounts[index])
            {
                error = "static-world-v3 LOD group counts differ from the canonical IPL sequence";
                return false;
            }
        }
        const DWORD*    anchors = groupCounts + header->groupCount;
        const auto*     links = reinterpret_cast<const SStaticWorldV3LodLink*>(anchors + header->anchorCount);
        std::set<DWORD> anchorOrdinals;
        std::set<DWORD> childOrdinals;
        std::set<DWORD> anchorIndices;
        DWORD           previousAnchor = 0;
        DWORD           previousChild = 0;
        for (DWORD index = 0; index < header->anchorCount; ++index)
        {
            if (anchors[index] >= header->placementCount || (index && anchors[index] <= previousAnchor) || !anchorOrdinals.insert(anchors[index]).second)
            {
                error = "static-world-v3 LOD anchors are not strictly ordered unique ordinals";
                return false;
            }
            previousAnchor = anchors[index];
        }
        for (DWORD index = 0; index < header->linkCount; ++index)
        {
            if (links[index].childOrdinal >= header->placementCount || links[index].anchorIndex >= header->anchorCount ||
                (index && links[index].childOrdinal <= previousChild) || !childOrdinals.insert(links[index].childOrdinal).second ||
                !anchorIndices.insert(links[index].anchorIndex).second)
            {
                error = "static-world-v3 LOD links are not a strict one-to-one mapping";
                return false;
            }
            previousChild = links[index].childOrdinal;
        }
        for (DWORD index = 0; index < header->anchorCount; ++index)
        {
            if (anchorIndices.find(index) == anchorIndices.end() || childOrdinals.find(anchors[index]) != childOrdinals.end())
            {
                error = "static-world-v3 LOD children and anchors overlap or omit a reference";
                return false;
            }
        }
        return true;
    }

    bool ValidateStaticWorldV3Inventory(const SStaticWorldV3Ide& ide, SStaticWorldV3Inventory& inventory, std::string& error)
    {
        std::set<std::string> dffs;
        std::set<std::string> txds;
        for (const auto& [name, ignored] : inventory.entries)
        {
            const size_t      dot = name.rfind('.');
            const std::string extension = name.substr(dot);
            if (extension == ".dff")
                dffs.insert(name);
            else if (extension == ".txd")
                txds.insert(name.substr(0, dot));
        }
        if (dffs != ide.modelFiles || txds != ide.txdStems || inventory.colStems.empty() || inventory.colStems.size() != inventory.iplStems.size() ||
            inventory.colStems.size() > STATIC_WORLD_V3_MAX_SPATIAL_GROUPS)
        {
            error = "static-world-v3 cross-IMG inventory differs from its IDE or spatial budgets";
            return false;
        }
        for (size_t index = 0; index < inventory.colStems.size(); ++index)
        {
            const std::string suffix = StaticWorldV3Base36(static_cast<unsigned int>(index), 2);
            if (inventory.colStems.find(ide.nameSpace + "c" + suffix) == inventory.colStems.end() ||
                inventory.iplStems.find(ide.nameSpace + "i" + suffix) == inventory.iplStems.end())
            {
                error = "static-world-v3 COL/IPL spatial ordinals are not contiguous and paired";
                return false;
            }
        }
        if (!ValidateStaticWorldV3Cols(ide, inventory, error) || !ValidateStaticWorldV3Ipls(ide, inventory, error))
            return false;
        for (const auto& [ordinal, colIds] : inventory.colModelIds)
        {
            const auto iplIds = inventory.iplModelIds.find(ordinal);
            if (iplIds == inventory.iplModelIds.end() || !std::includes(iplIds->second.begin(), iplIds->second.end(), colIds.begin(), colIds.end()))
            {
                error = "static-world-v3 COL model is not placed by its paired spatial IPL";
                return false;
            }
        }
        return true;
    }

    bool ValidateImg(const SString& path, SIdePlan& ide, std::string& error)
    {
        const WString widePath = SharedUtil::FromUTF8(path);
        HANDLE        file = CreateFileW(widePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            error = SString("%s cannot be opened", Pack().imgFileName);
            return false;
        }

        LARGE_INTEGER fileSize{};
        SImgHeader    header{};
        DWORD         read = 0;
        const bool    validHeader = GetFileSizeEx(file, &fileSize) && ReadFile(file, &header, sizeof(header), &read, nullptr) && read == sizeof(header) &&
                                 memcmp(header.magic, "VER2", 4) == 0 && header.count > 0 && header.count <= g_policy->maximumImgEntries &&
                                 fileSize.QuadPart == g_manifest.imgBytes && fileSize.QuadPart % 2048 == 0;
        if (!validHeader)
        {
            CloseHandle(file);
            error = SString("%s header, count, or byte length differs from the native plan", Pack().imgFileName);
            return false;
        }

        std::vector<SImgEntry> entries(header.count);
        const DWORD            directoryBytes = static_cast<DWORD>(entries.size() * sizeof(SImgEntry));
        if (!ReadFile(file, entries.data(), directoryBytes, &read, nullptr) || read != directoryBytes)
        {
            CloseHandle(file);
            error = SString("%s directory is truncated", Pack().imgFileName);
            return false;
        }
        CloseHandle(file);

        std::set<std::string>                names;
        std::set<std::string>                dffs;
        std::set<std::string>                txds;
        std::set<std::string>                ipls;
        std::vector<std::string>             orderedIpls;
        std::string                          colName;
        unsigned int                         colCount = 0;
        unsigned int                         maxEntrySize = 0;
        const uint64_t                       directoryEndSector = (sizeof(SImgHeader) + static_cast<uint64_t>(header.count) * sizeof(SImgEntry) + 2047) / 2048;
        std::vector<std::pair<DWORD, DWORD>> ranges;
        for (const SImgEntry& entry : entries)
        {
            const std::string name = EntryName(entry);
            const size_t      dot = name.rfind('.');
            const size_t      firstDot = name.find('.');
            const uint64_t    endSector = static_cast<uint64_t>(entry.offset) + entry.size;
            if (!IsSafeLeafName(name, 23) || dot == std::string::npos || dot == 0 || firstDot != dot || !IsSafeLeafName(name.substr(0, dot), 19) ||
                !entry.size || entry.size > g_policy->maximumImgEntryBlocks || entry.streamingSize != entry.size || entry.offset < directoryEndSector ||
                endSector > static_cast<uint64_t>(fileSize.QuadPart / 2048) || !names.insert(name).second)
            {
                error = SString("%s contains an invalid, duplicate, or out-of-bounds entry", Pack().imgFileName);
                return false;
            }
            ranges.emplace_back(entry.offset, entry.offset + entry.size);
            ide.imgEntries[name] = entry;
            ide.imgOrder.emplace_back(name);
            maxEntrySize = std::max(maxEntrySize, static_cast<unsigned int>(entry.size));
            const std::string extension = name.substr(dot);
            if (extension == ".dff")
                dffs.insert(name);
            else if (extension == ".txd")
                txds.insert(name.substr(0, dot));
            else if (extension == ".col")
            {
                ++colCount;
                colName = name;
            }
            else if (extension == ".ipl")
            {
                const std::string iplName = name.substr(0, dot);
                if (!IsSafeLeafName(iplName, 15) || iplName.find('.') != std::string::npos || !ipls.insert(iplName).second)
                {
                    error = SString("%s contains an unsafe or duplicate IPL name", Pack().imgFileName);
                    return false;
                }
                orderedIpls.push_back(iplName);
            }
            else
            {
                error = SString("%s contains an unexpected entry type", Pack().imgFileName);
                return false;
            }
        }
        std::sort(ranges.begin(), ranges.end());
        for (size_t i = 1; i < ranges.size(); ++i)
        {
            if (ranges[i].first < ranges[i - 1].second)
            {
                error = SString("%s contains overlapping entries", Pack().imgFileName);
                return false;
            }
        }

        if (dffs != ide.modelNames || txds != ide.txdNames || colCount != 1 || orderedIpls.empty() ||
            orderedIpls.size() > g_policy->iplPoolCapacity - g_policy->stockIplOccupied)
        {
            error = SString("%s inventory does not match %s or trusted pool budgets", Pack().imgFileName, Pack().ideFileName);
            return false;
        }
        g_manifest.colFileName = colName;
        g_manifest.iplNames = std::move(orderedIpls);
        g_iplNamePointers.clear();
        for (const std::string& name : g_manifest.iplNames)
            g_iplNamePointers.push_back(name.c_str());
        g_runtimeDescriptor.colFileName = g_manifest.colFileName.c_str();
        g_runtimeDescriptor.iplNames = g_iplNamePointers.data();
        g_runtimeDescriptor.iplCount = static_cast<unsigned int>(g_iplNamePointers.size());
        g_runtimeDescriptor.imgEntryCount = header.count;
        g_runtimeDescriptor.imgSectorCount = static_cast<unsigned int>(fileSize.QuadPart / 2048);
        g_runtimeDescriptor.largestImgEntryBlocks = maxEntrySize;
        return true;
    }

    bool ValidateBinaryIpls(const SString& path, const SIdePlan& ide, std::string& error)
    {
        const WString widePath = SharedUtil::FromUTF8(path);
        HANDLE        file = CreateFileW(widePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            error = SString("%s cannot be reopened for binary IPL validation", Pack().imgFileName);
            return false;
        }
        unsigned int totalInstances = 0;
        for (unsigned int index = 0; index < Pack().iplCount; ++index)
        {
            const std::string name = SString("%s.ipl", Pack().iplNames[index]);
            const SImgEntry&  entry = ide.imgEntries.at(name);
            std::vector<BYTE> data(static_cast<size_t>(entry.size) * 2048);
            LARGE_INTEGER     offset{};
            offset.QuadPart = static_cast<LONGLONG>(entry.offset) * 2048;
            DWORD read = 0;
            if (!SetFilePointerEx(file, offset, nullptr, FILE_BEGIN) || !ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &read, nullptr) ||
                read != data.size())
            {
                CloseHandle(file);
                error = SString("%s binary IPL entry is truncated", name.c_str());
                return false;
            }
            const auto*    header = reinterpret_cast<const SBinaryIplHeader*>(data.data());
            const DWORD    count = header->counts[0];
            const DWORD    instanceOffset = header->sections[0];
            const uint64_t instancesEnd = static_cast<uint64_t>(instanceOffset) + static_cast<uint64_t>(count) * sizeof(SBinaryIplInstance);
            bool           unsupportedSection = false;
            for (unsigned int section = 1; section < 6; ++section)
                unsupportedSection = unsupportedSection || header->counts[section] != 0;
            if (data.size() < sizeof(SBinaryIplHeader) || memcmp(header->magic, "bnry", 4) != 0 || !count ||
                count > g_policy->maximumIplInstances - totalInstances || instanceOffset < sizeof(SBinaryIplHeader) || instancesEnd > data.size() ||
                unsupportedSection)
            {
                CloseHandle(file);
                error = SString("%s has an invalid or unsupported binary IPL header", name.c_str());
                return false;
            }
            const auto* instances = reinterpret_cast<const SBinaryIplInstance*>(data.data() + instanceOffset);
            for (DWORD instanceIndex = 0; instanceIndex < count; ++instanceIndex)
            {
                const SBinaryIplInstance& instance = instances[instanceIndex];
                bool                      finite = true;
                for (float coordinate : instance.position)
                    finite = finite && std::isfinite(coordinate);
                float quaternionLength = 0.0f;
                for (float component : instance.quaternion)
                {
                    finite = finite && std::isfinite(component);
                    quaternionLength += component * component;
                }
                if (!finite || instance.position[0] < MIN_STATIC_WORLD_XY || instance.position[0] > MAX_STATIC_WORLD_XY ||
                    instance.position[1] < MIN_STATIC_WORLD_XY || instance.position[1] > MAX_STATIC_WORLD_XY ||
                    fabsf(instance.position[2]) > MAX_STATIC_WORLD_Z || quaternionLength < 0.25f || quaternionLength > 2.25f ||
                    ide.modelIds.find(instance.modelId) == ide.modelIds.end() || instance.instanceType != 0 || instance.lodIndex != -1)
                {
                    CloseHandle(file);
                    error = SString("%s contains an invalid binary IPL instance", name.c_str());
                    return false;
                }
            }
            totalInstances += count;
        }
        CloseHandle(file);
        return true;
    }

    bool ValidatePayloads(const SString& path, const SIdePlan& ide, std::string& error)
    {
        SNativeWorldPayloadPlanSA plan;
        for (const auto& [name, entry] : ide.imgEntries)
            plan.imgEntries.emplace(name, SNativeWorldPayloadImgEntrySA{entry.offset, entry.size});
        for (const auto& [id, fileName] : ide.modelFileNames)
            plan.modelNames.emplace(id, fileName.substr(0, fileName.size() - 4));
        plan.txdNames = ide.txdNames;
        plan.colFileName = Pack().colFileName;

        SNativeWorldPayloadSummarySA summary;
        const WString                widePath = SharedUtil::FromUTF8(path);
        if (!CNativeWorldPayloadValidatorSA::Validate(widePath.c_str(), plan, g_policy->payloadBudget, summary, error))
            return false;

        // Keep this stable and compact: it proves the untrusted bytes passed
        // the selected closed static-world policy before native mutation.
        Log("payloadAudit=ok dff=%u txd=%u rwChunks=%u rwDepth=%u rwBytes=%llu geometry=%llu/%llu/%llu plugins=%llu/%llu/%llu/%llu "
            "nativeTextures=%u textureBytes=%llu/%llu colRecords=%u coll=%u col3=%u colBytes=%llu maxColRecord=%u maxColVertices=%u "
            "maxColFaces=%u maxColFaceGroups=%u",
            summary.dffCount, summary.txdCount, summary.renderWareChunkCount, summary.maximumRenderWareDepth, summary.renderWareBytes, summary.geometryVertices,
            summary.geometryTriangles, summary.geometryMaterials, summary.effects2d, summary.breakableVertices, summary.breakableTriangles,
            summary.breakableMaterials, summary.nativeTextureCount, summary.nativeTextureGpuBytes, summary.nativeTextureDecodedBytes, summary.colRecordCount,
            summary.collRecordCount, summary.col3RecordCount, summary.colBytes, summary.maximumColRecordBytes, summary.maximumColVertices,
            summary.maximumColFaces, summary.maximumColFaceGroups);
        return true;
    }

    template <class T>
    bool BuildPoolAllocationPlan(CPoolSAInterface<T>* pool, int capacity, unsigned int expectedOccupied, unsigned int additionCount, const char* name,
                                 std::vector<bool>& originallyOccupied, std::vector<unsigned char>& originalFlags, int& originalFirstFree,
                                 std::vector<unsigned int>& plannedSlots, std::string& error)
    {
        if (!pool || !pool->m_pObjects || !pool->m_byteMap || pool->m_nSize != capacity || pool->m_nFirstFree < 0 || pool->m_nFirstFree >= capacity ||
            !pool->IsContains(pool->m_nFirstFree))
        {
            error = SString("%s pool pointer, capacity, or cursor is invalid", name);
            return false;
        }

        originalFirstFree = pool->m_nFirstFree;
        originallyOccupied.resize(capacity);
        originalFlags.resize(capacity);
        unsigned int occupied = 0;
        unsigned int holes = 0;
        int          highest = -1;
        for (int slot = 0; slot < capacity; ++slot)
        {
            const bool contains = pool->IsContains(slot);
            originallyOccupied[slot] = contains;
            originalFlags[slot] = reinterpret_cast<const unsigned char*>(pool->m_byteMap)[slot];
            if (contains)
            {
                ++occupied;
                highest = slot;
            }
        }
        for (int slot = 0; slot <= highest; ++slot)
            if (!originallyOccupied[slot])
                ++holes;
        bool exactStockLayout = occupied == expectedOccupied && highest == static_cast<int>(expectedOccupied) - 1 && holes == 0 && originalFirstFree == highest;
        for (int slot = 0; slot < capacity && exactStockLayout; ++slot)
            exactStockLayout = originallyOccupied[slot] == (slot < static_cast<int>(expectedOccupied));
        if (!exactStockLayout || capacity - occupied < additionCount)
        {
            error = SString("%s pool differs from exact contiguous stock layout occupied=%u expected=%u firstFree=%d highest=%d holes=%u", name, occupied,
                            expectedOccupied, originalFirstFree, highest, holes);
            return false;
        }

        std::vector<bool> simulated = originallyOccupied;
        int               cursor = originalFirstFree;
        for (unsigned int addition = 0; addition < additionCount; ++addition)
        {
            ++cursor;
            if (cursor >= capacity)
                cursor = 0;
            int selected = -1;
            for (int offset = 0; offset < capacity; ++offset)
            {
                int slot = cursor + offset;
                if (slot >= capacity)
                    slot -= capacity;
                if (!simulated[slot])
                {
                    selected = slot;
                    break;
                }
            }
            if (selected < 0)
            {
                error = SString("%s pool allocation simulation exhausted", name);
                return false;
            }
            simulated[selected] = true;
            cursor = selected;
            plannedSlots.push_back(static_cast<unsigned int>(selected));
        }

        std::ostringstream slots;
        for (size_t index = 0; index < plannedSlots.size(); ++index)
        {
            if (index)
                slots << ',';
            slots << plannedSlots[index];
        }
        Log("%sPool capacity=%d occupied=%u free=%u firstFree=%d highest=%d holes=%u planned=%s", name, capacity, occupied, capacity - occupied,
            originalFirstFree, highest, holes, slots.str().c_str());
        return true;
    }

    template <class T>
    bool ValidatePoolAllocationPostcondition(CPoolSAInterface<T>* pool, int capacity, const std::vector<bool>& originallyOccupied,
                                             const std::vector<unsigned char>& originalFlags, const std::vector<unsigned int>& plannedSlots, const char* name,
                                             std::string& error)
    {
        if (!pool || !pool->m_pObjects || !pool->m_byteMap || pool->m_nSize != capacity || originallyOccupied.size() != capacity ||
            originalFlags.size() != capacity || plannedSlots.empty())
        {
            error = SString("%s pool pointer, capacity, or snapshot is invalid after directory commit", name);
            return false;
        }

        std::set<unsigned int> planned(plannedSlots.begin(), plannedSlots.end());
        for (int slot = 0; slot < capacity; ++slot)
        {
            const auto    plannedSlot = planned.find(static_cast<unsigned int>(slot)) != planned.end();
            const auto    actualFlag = reinterpret_cast<const unsigned char*>(pool->m_byteMap)[slot];
            unsigned char expectedFlag = originalFlags[slot];
            if (plannedSlot)
            {
                if (originallyOccupied[slot] || !(originalFlags[slot] & 0x80))
                {
                    error = SString("%s planned slot was not free in the preflight snapshot slot=%d", name, slot);
                    return false;
                }
                // Native CPool::New clears bEmpty and advances the seven-bit
                // generation. Checking the whole flag proves the exact slots
                // were allocated, not merely that occupancy increased.
                expectedFlag = static_cast<unsigned char>(((originalFlags[slot] & 0x7F) + 1) & 0x7F);
            }
            if (actualFlag != expectedFlag || pool->IsContains(slot) != (originallyOccupied[slot] || plannedSlot))
            {
                error =
                    SString("%s pool allocation postcondition mismatch slot=%d expectedFlag=0x%02X actualFlag=0x%02X", name, slot, expectedFlag, actualFlag);
                return false;
            }
        }
        if (pool->m_nFirstFree != static_cast<int>(plannedSlots.back()))
        {
            error = SString("%s pool cursor postcondition mismatch expected=%u actual=%d", name, plannedSlots.back(), pool->m_nFirstFree);
            return false;
        }
        return true;
    }

    template <size_t Size>
    bool FixedNameEquals(const char (&actual)[Size], const char* expected)
    {
        const size_t length = strlen(expected);
        return length < Size && memcmp(actual, expected, length) == 0 && actual[length] == '\0';
    }

    bool StreamingInfoIsFree(unsigned int id)
    {
        const CStreamingInfo* info = g_streaming->GetStreamingInfo(id);
        return info && info->prevId == 0xFFFF && info->nextId == 0xFFFF && info->nextInImg == 0xFFFF && info->flg == 0 && info->archiveId == 0 &&
               info->offsetInBlocks == 0 && info->sizeInBlocks == 0 && info->loadState == eModelLoadState::LOADSTATE_NOT_LOADED;
    }

    template <class T>
    struct SBoundaryPoolSlotSnapshot
    {
        CPoolSAInterface<T>*        pool{};
        int                         slot{-1};
        int                         firstFree{-1};
        BYTE                        flag{};
        std::array<BYTE, sizeof(T)> object{};
    };

    template <class T>
    bool SnapshotFreePoolSlot(CPoolSAInterface<T>* pool, int slot, SBoundaryPoolSlotSnapshot<T>& snapshot, const char* name, std::string& error)
    {
        if (!pool || !pool->m_pObjects || !pool->m_byteMap || slot < 0 || slot >= pool->m_nSize || pool->IsContains(slot))
        {
            error = SString("boundary harness %s slot %d is not a valid free pool slot", name, slot);
            return false;
        }
        snapshot.pool = pool;
        snapshot.slot = slot;
        snapshot.firstFree = pool->m_nFirstFree;
        snapshot.flag = reinterpret_cast<const BYTE*>(pool->m_byteMap)[slot];
        std::memcpy(snapshot.object.data(), pool->GetObject(slot), sizeof(T));
        return true;
    }

    template <class T>
    int PredictNextPoolSlot(CPoolSAInterface<T>* pool)
    {
        if (!pool || !pool->m_pObjects || !pool->m_byteMap || pool->m_nSize <= 0)
            return -1;
        int cursor = pool->m_nFirstFree + 1;
        if (cursor < 0 || cursor >= pool->m_nSize)
            cursor = 0;
        for (int offset = 0; offset < pool->m_nSize; ++offset)
        {
            int slot = cursor + offset;
            if (slot >= pool->m_nSize)
                slot -= pool->m_nSize;
            if (!pool->IsContains(slot))
                return slot;
        }
        return -1;
    }

    template <class T>
    void RestorePoolSlot(const SBoundaryPoolSlotSnapshot<T>& snapshot)
    {
        std::memcpy(snapshot.pool->GetObject(snapshot.slot), snapshot.object.data(), sizeof(T));
        reinterpret_cast<BYTE*>(snapshot.pool->m_byteMap)[snapshot.slot] = snapshot.flag;
        snapshot.pool->m_nFirstFree = snapshot.firstFree;
    }

    template <class T>
    bool PoolSlotMatchesSnapshot(const SBoundaryPoolSlotSnapshot<T>& snapshot)
    {
        return snapshot.pool && std::memcmp(snapshot.pool->GetObject(snapshot.slot), snapshot.object.data(), sizeof(T)) == 0 &&
               reinterpret_cast<const BYTE*>(snapshot.pool->m_byteMap)[snapshot.slot] == snapshot.flag && snapshot.pool->m_nFirstFree == snapshot.firstFree;
    }

    template <class T>
    bool AllocateNativeStoreSlotAt(CPoolSAInterface<T>* pool, int target, int(__cdecl* allocate)(const char*), const char* name,
                                   SBoundaryPoolSlotSnapshot<T>& snapshot, std::string& error)
    {
        if (!SnapshotFreePoolSlot(pool, target, snapshot, name, error))
            return false;
        pool->m_nFirstFree = target - 1;
        const int allocated = allocate(name);
        pool->m_nFirstFree = snapshot.firstFree;
        if (allocated != target || !pool->IsContains(target))
        {
            RestorePoolSlot(snapshot);
            error = SString("boundary harness %s allocator returned %d instead of %d", name, allocated, target);
            return false;
        }
        return true;
    }

    bool ReadImgMember(const SString& path, const SImgEntry& entry, std::vector<BYTE>& data, std::string& error)
    {
        const WString widePath = SharedUtil::FromUTF8(path);
        HANDLE        file = CreateFileW(widePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            error = "boundary harness cannot reopen the native IMG";
            return false;
        }
        data.resize(static_cast<size_t>(entry.size) * 2048);
        LARGE_INTEGER offset{};
        offset.QuadPart = static_cast<LONGLONG>(entry.offset) * 2048;
        DWORD      read = 0;
        const bool ok = SetFilePointerEx(file, offset, nullptr, FILE_BEGIN) && ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &read, nullptr) &&
                        read == data.size();
        CloseHandle(file);
        if (!ok)
            error = "boundary harness native IMG member is truncated";
        return ok;
    }

    bool SelectBoundaryColRecord(const SString& imgPath, const SIdePlan& ide, std::vector<BYTE>& record, unsigned int& modelId,
                                 CBaseModelInfoSAInterface*& model, std::string& error)
    {
        std::vector<BYTE> member;
        if (!ReadImgMember(imgPath, ide.imgEntries.at(Pack().colFileName), member, error))
            return false;

        CBaseModelInfoSAInterface** models = reinterpret_cast<CBaseModelInfoSAInterface**>(CModelInfoSAInterface::ms_modelInfoPtrs);
        for (size_t offset = 0; offset + 32 <= member.size();)
        {
            DWORD payloadBytes = 0;
            std::memcpy(&payloadBytes, member.data() + offset + 4, sizeof(payloadBytes));
            const uint64_t recordBytes = static_cast<uint64_t>(payloadBytes) + 8;
            if ((std::memcmp(member.data() + offset, "COLL", 4) != 0 && std::memcmp(member.data() + offset, "COL3", 4) != 0) || recordBytes < 32 ||
                recordBytes > member.size() - offset)
                break;

            std::uint16_t candidateId = 0;
            std::memcpy(&candidateId, member.data() + offset + 30, sizeof(candidateId));
            const auto                 vtable = ide.modelVtables.find(candidateId);
            CBaseModelInfoSAInterface* candidate = candidateId <= g_policy->maximumModelId ? models[candidateId] : nullptr;
            if (vtable != ide.modelVtables.end() && vtable->second == ATOMIC_MODEL_VTABLE && candidate &&
                reinterpret_cast<DWORD>(candidate->VFTBL) == ATOMIC_MODEL_VTABLE && candidate->usDynamicIndex == 0xFFFF &&
                candidate->eSpecialModelType == eModelSpecialType::NONE)
            {
                record.assign(member.begin() + offset, member.begin() + offset + static_cast<size_t>(recordBytes));
                modelId = candidateId;
                model = candidate;
                return true;
            }
            offset += static_cast<size_t>(recordBytes);
        }
        error = "boundary harness found no disposable atomic static COL record";
        return false;
    }

    bool BoundaryHarnessEnabled()
    {
        char        value[8]{};
        const DWORD length = GetEnvironmentVariableA("MTA_NATIVE_WORLD_STORE_BOUNDARY_TEST", value, sizeof(value));
        return length == 1 && value[0] == '1';
    }

    bool RunNativeStoreBoundaryHarness(const SString& imgPath, const SIdePlan& ide, std::string& error)
    {
        if (!BoundaryHarnessEnabled())
            return true;

        auto*     colPool = *reinterpret_cast<CPoolSAInterface<SColDef>**>(0x965560);
        auto*     iplPool = *reinterpret_cast<CPoolSAInterface<CIplSAInterface>**>(0x8E3FB0);
        auto*     colModelPool = *reinterpret_cast<CPoolSAInterface<CColModelSAInterface>**>(0xB744A4);
        auto*     buildingPool = *reinterpret_cast<CPoolSAInterface<CBuildingSAInterface>**>(0xB74498);
        auto*     ptrNodePool = CPtrNodeSingleLinkPoolSA::GetPoolInstance();
        const int colCapacity = colPool ? colPool->m_nSize : -1;
        const int iplCapacity = iplPool ? iplPool->m_nSize : -1;
        const int colModelCapacity = colModelPool ? colModelPool->m_nSize : -1;
        const int buildingCapacity = buildingPool ? buildingPool->m_nSize : -1;
        const int ptrNodeCapacity = ptrNodePool ? static_cast<int>(ptrNodePool->GetCapacity()) : -1;
        Log("boundaryHarness=preflight col=%d/%u ipl=%d/%u colModel=%d/30000 building=%d/32000 ptrNode=%d/%d", colCapacity, Pack().colPoolCapacity, iplCapacity,
            Pack().iplPoolCapacity, colModelCapacity, buildingCapacity, ptrNodeCapacity, MAX_POINTER_SINGLE_LINKS);
        if (colCapacity != static_cast<int>(Pack().colPoolCapacity) || iplCapacity != static_cast<int>(Pack().iplPoolCapacity) || colModelCapacity != 30000 ||
            buildingCapacity != 32000 || ptrNodeCapacity != MAX_POINTER_SINGLE_LINKS)
        {
            error = SString("boundary harness pool capacities differ col=%d/%u ipl=%d/%u colModel=%d/30000 building=%d/32000 ptrNode=%d/%d", colCapacity,
                            Pack().colPoolCapacity, iplCapacity, Pack().iplPoolCapacity, colModelCapacity, buildingCapacity, ptrNodeCapacity,
                            MAX_POINTER_SINGLE_LINKS);
            return false;
        }
        if (*reinterpret_cast<const DWORD*>(0xBC40A0) != 0)
        {
            error = "boundary harness requires the collision accelerator to be ended";
            return false;
        }
        const std::array<BYTE, 44> colAccelBefore = *reinterpret_cast<const std::array<BYTE, 44>*>(0xBC4090);

        std::vector<BYTE>          colRecord;
        unsigned int               modelId = 0;
        CBaseModelInfoSAInterface* model = nullptr;
        if (!SelectBoundaryColRecord(imgPath, ide, colRecord, modelId, model, error))
            return false;
        if (!CFileIDRuntimeSA::BeginStoreExtensionTestSnapshot(error))
            return false;

        CColModelSAInterface* originalColModel = model->pColModel;
        const unsigned short  originalModelFlags = model->usFlags;
        const auto            addColSlot = reinterpret_cast<int(__cdecl*)(const char*)>(0x411140);
        const auto            addIplSlot = reinterpret_cast<int(__cdecl*)(const char*)>(ADD_IPL_SLOT);
        const auto            loadCol = reinterpret_cast<bool(__cdecl*)(int, BYTE*, int)>(LOAD_COL_BUFFER);
        const auto            removeCol = reinterpret_cast<void(__cdecl*)(int)>(REMOVE_COL);
        const auto            loadIpl = reinterpret_cast<bool(__cdecl*)(int, char*, int)>(LOAD_IPL_BUFFER);
        const auto            removeIpl = reinterpret_cast<void(__cdecl*)(int)>(REMOVE_IPL);
        const auto            deleteCollisionModel = reinterpret_cast<void(__thiscall*)(CBaseModelInfoSAInterface*)>(DELETE_COLLISION_MODEL);
        constexpr int         colTargets[] = {255, 256, 511};
        constexpr int         iplTargets[] = {255, 256, 1023};

        for (size_t boundary = 0; boundary < std::size(colTargets); ++boundary)
        {
            const int          colTarget = colTargets[boundary];
            const int          iplTarget = iplTargets[boundary];
            const unsigned int colFileId = pGame->GetBaseIDforCOL() + colTarget;
            const unsigned int iplFileId = pGame->GetBaseIDforIPL() + iplTarget;
            if (!StreamingInfoIsFree(colFileId) || !StreamingInfoIsFree(iplFileId))
            {
                error = SString("boundary harness streaming slot is occupied COL=%d IPL=%d", colTarget, iplTarget);
                return false;
            }
            const CStreamingInfo colStreamingBefore = *g_streaming->GetStreamingInfo(colFileId);
            const CStreamingInfo iplStreamingBefore = *g_streaming->GetStreamingInfo(iplFileId);

            SBoundaryPoolSlotSnapshot<SColDef>         colSlot;
            SBoundaryPoolSlotSnapshot<CIplSAInterface> iplSlot;
            SBoundaryPoolSlotSnapshot<SColDef>         col255Canary;
            const SString                              colName = SString("nwcol%d", colTarget);
            const SString                              iplName = SString("nwipl%d", iplTarget);
            if (colTarget != 255 && !SnapshotFreePoolSlot(colPool, 255, col255Canary, "COL-255-canary", error))
                return false;
            if (!AllocateNativeStoreSlotAt(colPool, colTarget, addColSlot, colName.c_str(), colSlot, error) ||
                !AllocateNativeStoreSlotAt(iplPool, iplTarget, addIplSlot, iplName.c_str(), iplSlot, error))
                return false;

            const int                                       colModelAllocation = PredictNextPoolSlot(colModelPool);
            SBoundaryPoolSlotSnapshot<CColModelSAInterface> colModelSlot;
            if (!SnapshotFreePoolSlot(colModelPool, colModelAllocation, colModelSlot, "ColModel", error))
                return false;

            if (!loadCol(colTarget, colRecord.data(), static_cast<int>(colRecord.size())) || model->pColModel == originalColModel ||
                model->pColModel != colModelPool->GetObject(colModelAllocation) || !model->pColModel->m_data ||
                CFileIDRuntimeSA::GetColModelSlot(model->pColModel) != colTarget || reinterpret_cast<const BYTE*>(model->pColModel)[0x28] != 0xFF)
            {
                error = SString("boundary harness first COL load failed at slot %d", colTarget);
                return false;
            }
            CColModelSAInterface* testColModel = model->pColModel;
            removeCol(colTarget);
            if (testColModel->m_data)
            {
                error = SString("boundary harness COL remove failed at slot %d", colTarget);
                return false;
            }
            if (!loadCol(colTarget, colRecord.data(), static_cast<int>(colRecord.size())) || model->pColModel != testColModel || !testColModel->m_data ||
                CFileIDRuntimeSA::GetColModelSlot(testColModel) != colTarget)
            {
                error = SString("boundary harness regular COL reload failed at slot %d", colTarget);
                return false;
            }

            SBoundaryIplPayload payload{};
            std::memcpy(payload.header.magic, "bnry", 4);
            payload.header.counts[0] = 1;
            payload.header.sections[0] = sizeof(SBinaryIplHeader);
            payload.header.sections[1] = sizeof(SBinaryIplInstance);
            payload.instance.position[0] = 0.0f;
            payload.instance.position[1] = 0.0f;
            payload.instance.position[2] = 0.0f;
            payload.instance.quaternion[3] = 1.0f;
            payload.instance.modelId = static_cast<int>(modelId);
            payload.instance.instanceType = 0;
            payload.instance.lodIndex = -1;
            std::memset(&iplPool->GetObject(iplTarget)->rect, 0, sizeof(CRect));

            const int                                       buildingAllocation = PredictNextPoolSlot(buildingPool);
            SBoundaryPoolSlotSnapshot<CBuildingSAInterface> buildingSlot;
            CPtrNodeSingleLinkPoolSA::pool_t::TestSnapshot  ptrNodeSnapshot;
            const size_t                                    ptrNodeUsedBefore = ptrNodePool->GetUsedSize();
            ptrNodePool->CaptureTestSnapshot(ptrNodeSnapshot);
            const CRect targetColRectBeforeIpl = colPool->GetObject(colTarget)->rect;
            if (!SnapshotFreePoolSlot(buildingPool, buildingAllocation, buildingSlot, "building", error) ||
                !loadIpl(iplTarget, reinterpret_cast<char*>(&payload), sizeof(payload)))
            {
                error = SString("boundary harness IPL load failed at slot %d", iplTarget);
                return false;
            }
            if (std::memcmp(&colPool->GetObject(colTarget)->rect, &targetColRectBeforeIpl, sizeof(CRect)) == 0)
            {
                error = SString("boundary harness IPL did not restrict target COL slot %d", colTarget);
                return false;
            }
            CBuildingSAInterface* testBuilding = buildingPool->GetObject(buildingAllocation);
            if (!buildingPool->IsContains(buildingAllocation) || CFileIDRuntimeSA::GetEntityIplIndex(testBuilding) != iplTarget ||
                reinterpret_cast<const BYTE*>(testBuilding)[0x2E] != 0xFF)
            {
                error = SString("boundary harness IPL entity did not retain slot %d", iplTarget);
                return false;
            }

            removeIpl(iplTarget);
            if (buildingPool->IsContains(buildingAllocation) || CFileIDRuntimeSA::GetEntityIplIndex(testBuilding) == iplTarget)
            {
                error = SString("boundary harness IPL remove aliased or retained slot %d", iplTarget);
                return false;
            }
            if (ptrNodePool->GetUsedSize() != ptrNodeUsedBefore)
            {
                error = SString("boundary harness IPL pointer-node usage did not roll back at slot %d before=%u after=%u", iplTarget,
                                static_cast<unsigned int>(ptrNodeUsedBefore), static_cast<unsigned int>(ptrNodePool->GetUsedSize()));
                return false;
            }
            RestorePoolSlot(buildingSlot);
            if (!ptrNodePool->RestoreTestSnapshot(ptrNodeSnapshot) || ptrNodePool->GetUsedSize() != ptrNodeUsedBefore)
            {
                error = SString("boundary harness IPL pointer-node snapshot did not restore at slot %d", iplTarget);
                return false;
            }

            removeCol(colTarget);
            if (testColModel->m_data)
            {
                error = SString("boundary harness final COL remove failed at slot %d", colTarget);
                return false;
            }
            deleteCollisionModel(model);
            if (model->pColModel || CFileIDRuntimeSA::GetColModelSlot(testColModel) == colTarget)
            {
                error = SString("boundary harness COL side storage survived deletion at slot %d", colTarget);
                return false;
            }
            RestorePoolSlot(colModelSlot);
            model->pColModel = originalColModel;
            model->usFlags = originalModelFlags;
            RestorePoolSlot(iplSlot);
            RestorePoolSlot(colSlot);

            if (col255Canary.pool && !PoolSlotMatchesSnapshot(col255Canary))
            {
                error = SString("boundary harness high COL slot %d aliased COL slot 255", colTarget);
                return false;
            }

            if (std::memcmp(g_streaming->GetStreamingInfo(colFileId), &colStreamingBefore, sizeof(colStreamingBefore)) != 0 ||
                std::memcmp(g_streaming->GetStreamingInfo(iplFileId), &iplStreamingBefore, sizeof(iplStreamingBefore)) != 0 ||
                std::memcmp(reinterpret_cast<const void*>(0xBC4090), colAccelBefore.data(), colAccelBefore.size()) != 0)
            {
                error = SString("boundary harness rollback changed native state at COL=%d IPL=%d", colTarget, iplTarget);
                return false;
            }
            Log("boundaryHarness=pair-ok col=%d ipl=%d set=get=remove ownedPoolRollback=exact streamingRollback=exact", colTarget, iplTarget);
        }
        if (!CFileIDRuntimeSA::RestoreStoreExtensionTestSnapshot(error))
            return false;
        Log("boundaryHarness=passed col=255,256,511 ipl=255,256,1023 fullWidth=yes sideTableRollback=exact "
            "carGenerators=static-owned-skip rngDraws=3 intentional=yes");
        return true;
    }

    bool BuildTxdAllocationPlan(CPoolSAInterface<CTextureDictonarySAInterface>* pool, SIdePlan& ide, std::string& error)
    {
        const char*                    executableIdentity = CNativeModelStoreSA::GetExecutableIdentityName();
        const SNativeTxdPoolProfileSA* profile = nullptr;
        for (unsigned int index = 0; index < Pack().txdPoolProfileCount; ++index)
        {
            const SNativeTxdPoolProfileSA& candidate = Pack().txdPoolProfiles[index];
            if (executableIdentity && strcmp(executableIdentity, candidate.executableIdentity) == 0)
            {
                profile = &candidate;
                break;
            }
        }
        if (!profile)
        {
            error = "no TXD pool profile matches the executable identity";
            return false;
        }
        ide.txdProfileName = profile->name;

        if (!pool || !pool->m_pObjects || !pool->m_byteMap || pool->m_nSize != static_cast<int>(Pack().txdPoolCapacity) ||
            pool->m_nFirstFree != profile->firstFree)
        {
            error = SString("TXD pool pointer, capacity, or cursor differs from profile=%s", profile->name);
            return false;
        }

        const uint64_t txdPartitionEnd = static_cast<uint64_t>(pGame->GetBaseIDforTXD()) + pool->m_nSize;
        if (txdPartitionEnd != pGame->GetBaseIDforCOL())
        {
            error = "TXD pool capacity does not exactly fill its streaming ID partition";
            return false;
        }

        ide.txdOriginalFirstFree = pool->m_nFirstFree;
        ide.txdOriginalFindCache = *reinterpret_cast<const int*>(TXD_FIND_CACHE);
        ide.txdSnapshotValid = true;
        ide.txdOriginallyOccupied.resize(pool->m_nSize);
        ide.txdOriginalFlags.resize(pool->m_nSize);
        ide.txdOriginalObjects.resize(pool->m_nSize);
        for (int slot = 0; slot < pool->m_nSize; ++slot)
        {
            const bool occupied = pool->IsContains(slot);
            ide.txdOriginallyOccupied[slot] = occupied;
            ide.txdOriginalFlags[slot] = reinterpret_cast<const unsigned char*>(pool->m_byteMap)[slot];
            ide.txdOriginalObjects[slot] = *pool->GetObject(slot);
            if (occupied)
            {
                ++ide.txdOccupied;
                ide.txdHighestOccupied = slot;
            }
        }
        ide.txdFree = static_cast<unsigned int>(pool->m_nSize) - ide.txdOccupied;
        for (int slot = 0; slot <= ide.txdHighestOccupied; ++slot)
            if (!ide.txdOriginallyOccupied[slot])
                ++ide.txdHoles;
        if (ide.txdOccupied != profile->occupied || ide.txdHighestOccupied != static_cast<int>(profile->occupied) - 1 || ide.txdHoles != 0)
        {
            error = SString("TXD pool occupancy differs from profile=%s", profile->name);
            return false;
        }
        for (int slot = 0; slot < pool->m_nSize; ++slot)
            if (ide.txdOriginallyOccupied[slot] != (slot < static_cast<int>(profile->occupied)))
            {
                error = SString("TXD pool is not contiguous for profile=%s at slot=%d", profile->name, slot);
                return false;
            }

        if (profile->fingerprintSlot >= 0)
        {
            const unsigned int                  slot = static_cast<unsigned int>(profile->fingerprintSlot);
            const BYTE                          poolFlag = reinterpret_cast<const BYTE*>(pool->m_byteMap)[slot];
            const CTextureDictonarySAInterface* definition = pool->GetObject(slot);
            const CStreamingInfo*               streaming = g_streaming->GetStreamingInfo(pGame->GetBaseIDforTXD() + slot);
            Log("txdProfileFingerprint profile=%s slot=%u poolFlag=0x%02X dictionary=%p usages=%u parent=%u hash=0x%08X streamPrev=%u streamNext=%u "
                "streamNextImg=%u streamFlags=0x%02X archive=%u offset=%u size=%u loadState=%u",
                profile->name, slot, poolFlag, definition->rwTexDictonary, definition->usUsagesCount, definition->usParentIndex, definition->hash,
                streaming->prevId, streaming->nextId, streaming->nextInImg, streaming->flg, streaming->archiveId, streaming->offsetInBlocks,
                streaming->sizeInBlocks, static_cast<unsigned int>(streaming->loadState));
            const SNativeTxdSlotFingerprintSA& expected = profile->fingerprint;
            if (!expected.configured)
            {
                error = SString("TXD profile=%s requires a reviewed slot fingerprint", profile->name);
                return false;
            }
            if (poolFlag != expected.poolFlag || reinterpret_cast<DWORD>(definition->rwTexDictonary) != expected.dictionary ||
                definition->usUsagesCount != expected.usages || definition->usParentIndex != expected.parent || definition->hash != expected.hash ||
                streaming->prevId != expected.prev || streaming->nextId != expected.next || streaming->nextInImg != expected.nextInImg ||
                streaming->flg != expected.streamingFlags || streaming->archiveId != expected.archive || streaming->offsetInBlocks != expected.offset ||
                streaming->sizeInBlocks != expected.size || static_cast<DWORD>(streaming->loadState) != expected.loadState)
            {
                error = SString("TXD profile=%s slot fingerprint mismatch", profile->name);
                return false;
            }
        }
        if (ide.txdFree < Pack().txdCount)
        {
            error = SString("TXD pool does not have %u free slots", Pack().txdCount);
            return false;
        }

        std::vector<bool> simulatedOccupied = ide.txdOriginallyOccupied;
        int               cursor = ide.txdOriginalFirstFree;
        for (const std::string& name : ide.txdNames)
        {
            ++cursor;
            if (cursor < 0 || cursor >= pool->m_nSize)
                cursor = 0;

            int selected = -1;
            for (int offset = 0; offset < pool->m_nSize; ++offset)
            {
                int slot = cursor + offset;
                if (slot >= pool->m_nSize)
                    slot -= pool->m_nSize;
                if (!simulatedOccupied[slot])
                {
                    selected = slot;
                    break;
                }
            }
            if (selected < 0)
            {
                error = "TXD allocation simulation exhausted the pool";
                return false;
            }

            cursor = selected;
            simulatedOccupied[selected] = true;
            ide.txdSlots[name] = static_cast<unsigned int>(selected);
        }

        const auto [minimum, maximum] =
            std::minmax_element(ide.txdSlots.begin(), ide.txdSlots.end(), [](const auto& left, const auto& right) { return left.second < right.second; });
        ide.txdPlanMin = minimum->second;
        ide.txdPlanMax = maximum->second;
        ide.txdPlanSpanHoles = ide.txdPlanMax - ide.txdPlanMin + 1 - Pack().txdCount;

        for (const auto& [name, slot] : ide.txdSlots)
        {
            if (!StreamingInfoIsFree(pGame->GetBaseIDforTXD() + slot))
            {
                error = SString("planned %s TXD streaming slot is occupied name=%s slot=%u", Pack().displayName, name.c_str(), slot);
                return false;
            }
        }

        Log("txdPool profile=%s capacity=%d occupied=%u free=%u firstFree=%d highest=%d holes=%u planned=%u plannedRange=%u..%u plannedSpanHoles=%u",
            ide.txdProfileName, pool->m_nSize, ide.txdOccupied, ide.txdFree, ide.txdOriginalFirstFree, ide.txdHighestOccupied, ide.txdHoles, Pack().txdCount,
            ide.txdPlanMin, ide.txdPlanMax, ide.txdPlanSpanHoles);
        return true;
    }

    void RestoreTxdFindCache(const SIdePlan& ide)
    {
        if (ide.txdSnapshotValid)
            *reinterpret_cast<int*>(TXD_FIND_CACHE) = ide.txdOriginalFindCache;
    }

    bool PreflightRuntime(SIdePlan& ide, std::string& error)
    {
        unsigned int atomicCapacity = 0, damageCapacity = 0, timeCapacity = 0;
        CNativeModelStoreSA::GetCapacities(atomicCapacity, damageCapacity, timeCapacity);
        if (atomicCapacity != g_policy->modelStoreCapacities.atomic || damageCapacity != g_policy->modelStoreCapacities.damageAtomic ||
            timeCapacity != g_policy->modelStoreCapacities.time)
        {
            error = "native model-store foundation differs from the compiled pack policy";
            return false;
        }

        unsigned int atomic = 0, damage = 0, time = 0;
        if (!CNativeModelStoreSA::GetUsage(atomic, damage, time) || atomic != Pack().stockModelStores.atomic ||
            damage != Pack().stockModelStores.damageAtomic || time != Pack().stockModelStores.time)
        {
            error = "native model stores are not at exact stock occupancy";
            return false;
        }
        if (atomic + Pack().addedModelStores.atomic > g_policy->modelStoreCapacities.atomic ||
            damage + Pack().addedModelStores.damageAtomic > g_policy->modelStoreCapacities.damageAtomic ||
            time + Pack().addedModelStores.time > g_policy->modelStoreCapacities.time)
        {
            error = "native model-store headroom is below the derived IDE additions";
            return false;
        }

        CBaseModelInfoSAInterface** models = reinterpret_cast<CBaseModelInfoSAInterface**>(CModelInfoSAInterface::ms_modelInfoPtrs);
        for (unsigned int id = Pack().modelFirst; id <= Pack().modelLast; ++id)
        {
            if (models[id] || !StreamingInfoIsFree(id))
            {
                error = SString("a %s model ID or streaming slot is already occupied", Pack().displayName);
                return false;
            }
        }

        // GTA stores only CKeyGen::GetUppercaseKey(modelName) in model infos.
        // Compare those exact native keys before LoadObjectTypes can construct
        // any custom entries; filename/string equality alone misses hash and
        // case-fold collisions.
        const auto                          uppercaseKey = reinterpret_cast<unsigned int(__cdecl*)(const char*)>(GET_UPPERCASE_KEY);
        std::map<unsigned int, std::string> modelKeys;
        for (const auto& [id, fileName] : ide.modelFileNames)
        {
            const std::string  modelStem = fileName.substr(0, fileName.size() - 4);
            const unsigned int key = uppercaseKey(modelStem.c_str());
            const auto [existing, inserted] = modelKeys.emplace(key, modelStem);
            if (!inserted)
            {
                error = SString("%s DFF native-key collision key=0x%08X names=%s,%s", Pack().displayName, key, existing->second.c_str(), modelStem.c_str());
                return false;
            }
        }
        for (unsigned int id = 0; id <= g_policy->maximumModelId; ++id)
        {
            const CBaseModelInfoSAInterface* model = models[id];
            if (!model)
                continue;
            const auto collision = modelKeys.find(model->ulHashKey);
            if (collision != modelKeys.end())
            {
                error = SString("%s DFF native-key collides with occupied stock model id=%u key=0x%08X name=%s", Pack().displayName, id, model->ulHashKey,
                                collision->second.c_str());
                return false;
            }
        }

        auto*                     txdPool = *reinterpret_cast<CPoolSAInterface<CTextureDictonarySAInterface>**>(0xC8800C);
        auto*                     colPool = *reinterpret_cast<CPoolSAInterface<SColDef>**>(0x965560);
        auto*                     iplPool = *reinterpret_cast<CPoolSAInterface<CIplSAInterface>**>(0x8E3FB0);
        std::vector<unsigned int> colSlots;
        std::vector<unsigned int> iplSlots;
        if (!BuildPoolAllocationPlan(colPool, Pack().colPoolCapacity, Pack().stockColOccupied, 1, "col", ide.colOriginallyOccupied, ide.colOriginalFlags,
                                     ide.colOriginalFirstFree, colSlots, error) ||
            !BuildPoolAllocationPlan(iplPool, Pack().iplPoolCapacity, Pack().stockIplOccupied, Pack().iplCount, "ipl", ide.iplOriginallyOccupied,
                                     ide.iplOriginalFlags, ide.iplOriginalFirstFree, iplSlots, error) ||
            !BuildTxdAllocationPlan(txdPool, ide, error))
            return false;
        if (pGame->GetBaseIDforCOL() + Pack().colPoolCapacity != pGame->GetBaseIDforIPL() ||
            pGame->GetBaseIDforIPL() + Pack().iplPoolCapacity > pGame->GetCountOfAllFileIDs())
        {
            error = "COL/IPL pool capacities do not fit their streaming ID partitions";
            return false;
        }
        ide.colSlot = colSlots.front();
        ide.iplAllocationSlots = iplSlots;
        unsigned int nextIplSlot = 0;
        for (const std::string& entryName : ide.imgOrder)
        {
            const size_t dot = entryName.rfind('.');
            if (dot != std::string::npos && entryName.substr(dot) == ".ipl")
            {
                if (nextIplSlot >= iplSlots.size())
                {
                    error = "the IMG directory contains more IPL allocations than planned";
                    return false;
                }
                ide.iplSlots[entryName.substr(0, dot)] = iplSlots[nextIplSlot++];
            }
        }
        if (nextIplSlot != Pack().iplCount || ide.iplSlots.size() != Pack().iplCount)
        {
            error = "the IPL allocation plan does not match IMG directory order";
            return false;
        }

        const auto                          findTxd = reinterpret_cast<int(__cdecl*)(const char*)>(FIND_TXD_SLOT);
        std::map<unsigned int, std::string> txdKeys;
        for (const std::string& name : ide.txdNames)
        {
            const unsigned int key = uppercaseKey(name.c_str());
            const auto [existing, inserted] = txdKeys.emplace(key, name);
            if (!inserted)
            {
                error = SString("%s TXD native-key collision key=0x%08X names=%s,%s", Pack().displayName, key, existing->second.c_str(), name.c_str());
                return false;
            }
            if (findTxd(name.c_str()) != -1)
            {
                error = SString("a %s TXD name already exists", Pack().displayName);
                return false;
            }
        }
        const auto findIpl = reinterpret_cast<int(__cdecl*)(const char*)>(FIND_IPL_SLOT);
        for (unsigned int index = 0; index < Pack().iplCount; ++index)
        {
            const char* name = Pack().iplNames[index];
            if (findIpl(name) != -1)
            {
                error = SString("a %s IPL name already exists", Pack().displayName);
                return false;
            }
        }
        if (!StreamingInfoIsFree(pGame->GetBaseIDforCOL() + ide.colSlot))
        {
            error = SString("the planned %s COL streaming slot is occupied", Pack().displayName);
            return false;
        }
        for (unsigned int i = 0; i < Pack().iplCount; ++i)
            if (!StreamingInfoIsFree(pGame->GetBaseIDforIPL() + ide.iplSlots.at(Pack().iplNames[i])))
            {
                error = SString("a planned %s IPL streaming slot is occupied", Pack().displayName);
                return false;
            }

        if (g_streaming->GetUnusedArchive() == INVALID_ARCHIVE_ID || g_streaming->GetUnusedStreamHandle() == INVALID_STREAM_ID)
        {
            error = "no archive or stream handle is available";
            return false;
        }
        return true;
    }

    void ValidateIdePostconditions(const SIdePlan& ide)
    {
        unsigned int atomic = 0, damage = 0, time = 0;
        if (!CNativeModelStoreSA::GetUsage(atomic, damage, time) || atomic != Pack().stockModelStores.atomic + Pack().addedModelStores.atomic ||
            damage != Pack().stockModelStores.damageAtomic + Pack().addedModelStores.damageAtomic ||
            time != Pack().stockModelStores.time + Pack().addedModelStores.time)
            Fatal("model-store occupancy mismatch after IDE commit");

        CBaseModelInfoSAInterface** models = reinterpret_cast<CBaseModelInfoSAInterface**>(CModelInfoSAInterface::ms_modelInfoPtrs);
        const auto                  findTxd = reinterpret_cast<int(__cdecl*)(const char*)>(FIND_TXD_SLOT);
        for (const std::string& name : ide.txdNames)
            if (findTxd(name.c_str()) != static_cast<int>(ide.txdSlots.at(name)))
                Fatal("TXD slot postcondition mismatch after IDE commit");
        for (unsigned int id = Pack().modelFirst; id <= Pack().modelLast; ++id)
        {
            CBaseModelInfoSAInterface* model = models[id];
            const int                  expectedTxd = findTxd(ide.modelTxdNames.at(id).c_str());
            if (!model || reinterpret_cast<DWORD>(model->VFTBL) != ide.modelVtables.at(id) || model->usTextureDictionary != expectedTxd)
                Fatal("IDE model type or TXD binding postcondition mismatch");
        }
    }

    void LogColPostconditionDiagnostics(unsigned char archiveId, unsigned int expectedSlot)
    {
        auto*        pool = *reinterpret_cast<CPoolSAInterface<SColDef>**>(0x965560);
        unsigned int occupied = 0;
        unsigned int holes = 0;
        int          highest = -1;
        if (!pool || !pool->m_pObjects || !pool->m_byteMap)
        {
            Log("colPost pool=invalid");
            return;
        }
        for (int slot = 0; slot < pool->m_nSize; ++slot)
            if (pool->IsContains(slot))
            {
                ++occupied;
                highest = slot;
            }
        for (int slot = 0; slot <= highest; ++slot)
            if (!pool->IsContains(slot))
                ++holes;
        Log("colPost capacity=%d occupied=%u free=%u firstFree=%d highest=%d holes=%u expectedSlot=%u expectedArchive=%u", pool->m_nSize, occupied,
            static_cast<unsigned int>(pool->m_nSize) - occupied, pool->m_nFirstFree, highest, holes, expectedSlot, archiveId);

        for (int slot = 0; slot < pool->m_nSize; ++slot)
        {
            const CStreamingInfo* streaming = g_streaming->GetStreamingInfo(pGame->GetBaseIDforCOL() + slot);
            if (slot != static_cast<int>(expectedSlot) && streaming->archiveId != archiveId)
                continue;

            const SColDef* definition = pool->GetObject(slot);
            Log("colPost slot=%d occupied=%u poolFlag=0x%02X rectBits=%08X,%08X,%08X,%08X firstModel=%d lastModel=%d refs=%u state=%u%u%u%u "
                "streamPrev=%u streamNext=%u streamNextImg=%u streamFlags=0x%02X archive=%u offset=%u size=%u loadState=%u",
                slot, pool->IsContains(slot) ? 1 : 0, reinterpret_cast<const BYTE*>(pool->m_byteMap)[slot],
                reinterpret_cast<const DWORD*>(&definition->rect)[0], reinterpret_cast<const DWORD*>(&definition->rect)[1],
                reinterpret_cast<const DWORD*>(&definition->rect)[2], reinterpret_cast<const DWORD*>(&definition->rect)[3], definition->firstModel,
                definition->lastModel, definition->refCount, definition->active ? 1 : 0, definition->required ? 1 : 0, definition->procedural ? 1 : 0,
                definition->interior ? 1 : 0, streaming->prevId, streaming->nextId, streaming->nextInImg, streaming->flg, streaming->archiveId,
                streaming->offsetInBlocks, streaming->sizeInBlocks, static_cast<unsigned int>(streaming->loadState));
        }
    }

    void ValidatePostconditions(const SIdePlan& ide, unsigned char archiveId)
    {
        ValidateIdePostconditions(ide);

        auto* colPool = *reinterpret_cast<CPoolSAInterface<SColDef>**>(0x965560);
        auto* iplPool = *reinterpret_cast<CPoolSAInterface<CIplSAInterface>**>(0x8E3FB0);
        LogColPostconditionDiagnostics(archiveId, ide.colSlot);
        const std::vector<unsigned int> colSlots = {ide.colSlot};
        std::string                     poolError;
        if (!ValidatePoolAllocationPostcondition(colPool, Pack().colPoolCapacity, ide.colOriginallyOccupied, ide.colOriginalFlags, colSlots, "COL", poolError))
            Fatal(poolError.c_str());
        if (!ValidatePoolAllocationPostcondition(iplPool, Pack().iplPoolCapacity, ide.iplOriginallyOccupied, ide.iplOriginalFlags, ide.iplAllocationSlots,
                                                 "IPL", poolError))
            Fatal(poolError.c_str());

        const SColDef* col = colPool->GetObject(ide.colSlot);
        if (memcmp(&col->rect, FLIPPED_RECT_SENTINELS, sizeof(FLIPPED_RECT_SENTINELS)) != 0 || col->firstModel != 0x7FFF ||
            col->lastModel != static_cast<short>(0x8000) || col->refCount != 0 || col->active || col->required || col->procedural || col->interior)
            Fatal("COL slot structural postcondition mismatch");
        for (unsigned int i = 0; i < Pack().iplCount; ++i)
        {
            const char*        name = Pack().iplNames[i];
            const unsigned int slot = ide.iplSlots.at(name);
            const auto*        ipl = iplPool->GetObject(slot);
            if (!iplPool->IsContains(slot) || !FixedNameEquals(ipl->name, name) ||
                memcmp(&ipl->rect, FLIPPED_RECT_SENTINELS, sizeof(FLIPPED_RECT_SENTINELS)) != 0 || ipl->minBuildId != 0x7FFF ||
                ipl->maxBuildId != static_cast<short>(0x8000) || ipl->minBummyId != 0x7FFF || ipl->maxDummyId != static_cast<short>(0x8000) ||
                ipl->relatedIpl != -1 || ipl->interior != 0 || ipl->unk2 != 0 || ipl->bLoadReq != 0 || !ipl->bDisabledStreaming || ipl->unk3 != 0 ||
                ipl->unk4 != 0)
                Fatal("IPL initial slot postcondition mismatch");
        }

        const auto validateStreamingEntry = [&ide, archiveId](unsigned int id, const std::string& name)
        {
            const CStreamingInfo* info = g_streaming->GetStreamingInfo(id);
            const SImgEntry&      entry = ide.imgEntries.at(name);
            if (!info || info->archiveId != archiveId || info->offsetInBlocks != entry.offset || info->sizeInBlocks != entry.size)
                Fatal("streaming directory offset or size postcondition mismatch");
        };
        for (unsigned int id = Pack().modelFirst; id <= Pack().modelLast; ++id)
            validateStreamingEntry(id, ide.modelFileNames.at(id));
        for (const std::string& name : ide.txdNames)
            validateStreamingEntry(pGame->GetBaseIDforTXD() + ide.txdSlots.at(name), name + ".txd");
        validateStreamingEntry(pGame->GetBaseIDforCOL() + ide.colSlot, Pack().colFileName);
        for (unsigned int i = 0; i < Pack().iplCount; ++i)
            validateStreamingEntry(pGame->GetBaseIDforIPL() + ide.iplSlots.at(Pack().iplNames[i]), SString("%s.ipl", Pack().iplNames[i]));

        std::map<std::string, unsigned int> streamingIds;
        for (unsigned int id = Pack().modelFirst; id <= Pack().modelLast; ++id)
            streamingIds[ide.modelFileNames.at(id)] = id;
        for (const std::string& name : ide.txdNames)
            streamingIds[name + ".txd"] = pGame->GetBaseIDforTXD() + ide.txdSlots.at(name);
        streamingIds[Pack().colFileName] = pGame->GetBaseIDforCOL() + ide.colSlot;
        for (unsigned int i = 0; i < Pack().iplCount; ++i)
            streamingIds[SString("%s.ipl", Pack().iplNames[i])] = pGame->GetBaseIDforIPL() + ide.iplSlots.at(Pack().iplNames[i]);

        for (size_t i = 0; i < ide.imgOrder.size(); ++i)
        {
            const CStreamingInfo* info = g_streaming->GetStreamingInfo(streamingIds.at(ide.imgOrder[i]));
            const unsigned short  expectedNext = i + 1 < ide.imgOrder.size() ? static_cast<unsigned short>(streamingIds.at(ide.imgOrder[i + 1])) : 0xFFFF;
            if (info->nextInImg != expectedNext)
                Fatal("streaming directory nextInImg chain postcondition mismatch");
        }
        if (*reinterpret_cast<const unsigned int*>(0x8E4CA8) < Pack().largestImgEntryBlocks)
            Fatal("native maximum streaming entry size was not raised");
    }

    void EnableOwnedIplDynamicStreaming(const SIdePlan& ide)
    {
        auto*      iplPool = *reinterpret_cast<CPoolSAInterface<CIplSAInterface>**>(0x8E3FB0);
        const auto enableDynamicStreaming = reinterpret_cast<void(__cdecl*)(int, bool)>(ENABLE_IPL_DYNAMIC_STREAMING);
        for (unsigned int index = 0; index < Pack().iplCount; ++index)
        {
            const char*        name = Pack().iplNames[index];
            const unsigned int slot = ide.iplSlots.at(name);
            enableDynamicStreaming(slot, true);
            const auto* ipl = iplPool->GetObject(slot);
            if (ipl->bDisabledStreaming || memcmp(&ipl->rect, FLIPPED_RECT_SENTINELS, sizeof(FLIPPED_RECT_SENTINELS)) != 0 || ipl->unk2 != 0 ||
                ipl->bLoadReq != 0)
                Fatal("IPL dynamic-streaming enable postcondition mismatch");
        }

        // LoadAllRemainingIpls runs later in the stock level-loading sequence.
        // Leaving each rectangle flipped here makes that native pass calculate
        // the real bounds, add the slot to the IPL quadtree, and unload it so
        // normal position-driven streaming owns the subsequent lifecycle.
        std::ostringstream slots;
        for (unsigned int index = 0; index < Pack().iplCount; ++index)
        {
            if (index)
                slots << ',';
            slots << ide.iplSlots.at(Pack().iplNames[index]);
        }
        Log("iplBootstrap dynamicStreaming=enabled slots=%s boundingBoxes=pending-native-pass", slots.str().c_str());
    }

    void RegisterPack()
    {
        const SString idePath = SString("%s\\%s", Pack().directoryPath, Pack().ideFileName);
        const SString imgPath = SString("%s\\%s", Pack().directoryPath, Pack().imgFileName);
        SIdePlan      ide;
        std::string   error;
        Log("registrar=preflight ide=%s img=%s", idePath.c_str(), imgPath.c_str());
        if (CFileIDRuntimeSA::HasStoreExtensionOverflow())
            error = "COL/IPL full-width side storage overflowed before registration";
        if (!IsNativePathSafe(idePath) || !IsNativePathSafe(imgPath) || !IsSafeRegularFile(idePath) || !IsSafeRegularFile(imgPath))
            error = "native loader paths must be regular non-reparse ASCII files shorter than MAX_PATH";
        if (error.empty())
        {
            const SString ideHash = SharedUtil::GenerateSha256HexStringFromFile(idePath);
            const SString imgHash = SharedUtil::GenerateSha256HexStringFromFile(imgPath);
            if (!HasExactFileSize(idePath, g_manifest.ideBytes) || !HasExactFileSize(imgPath, g_manifest.imgBytes) ||
                _stricmp(ideHash.c_str(), Pack().ideSha256) != 0 || _stricmp(imgHash.c_str(), Pack().imgSha256) != 0)
                error = "runtime pack byte length or SHA-256 differs from its manifest";
            else
                Log("registrar=integrity-ok ideSha256=%s imgSha256=%s", Pack().ideSha256, Pack().imgSha256);
        }
        if (!error.empty() || !ParseIde(idePath, ide, error) || !ValidateImg(imgPath, ide, error) || !ValidateBinaryIpls(imgPath, ide, error) ||
            !ValidatePayloads(imgPath, ide, error) || !ValidateDescriptor(error) || !PreflightRuntime(ide, error))
        {
            RestoreTxdFindCache(ide);
            ReleaseRegistrationLease();
            ReleaseNativeModelSlotReservation();
            g_state = EState::Refused;
            MarkRegistrationRefused();
            Log("registrar=refused reason=%s stock-world-remains-active", error.c_str());
            return;
        }

        // AddArchive is the only reversible native mutation that precedes pool
        // allocation. Keep it first so an open failure leaves every pool stock.
        // Cache lease handles continuously deny mutation and deletion while
        // GTA opens the archive. CStreamingSA then owns its read handle, while
        // the cache lease remains process-lifetime after activation so later
        // path-based streaming cannot be redirected.
        const WString       wideImgPath = SharedUtil::FromUTF8(imgPath);
        const unsigned char archiveId = g_streaming->AddArchive(wideImgPath.c_str());
        if (archiveId == INVALID_ARCHIVE_ID)
        {
            RestoreTxdFindCache(ide);
            ReleaseRegistrationLease();
            ReleaseNativeModelSlotReservation();
            g_state = EState::Refused;
            MarkRegistrationRefused();
            Log("registrar=refused reason=AddArchive-failed-before-pool-mutation stock-world-remains-active");
            return;
        }
        if (archiveId != Pack().expectedArchiveId)
        {
            g_streaming->RemoveArchive(archiveId);
            RestoreTxdFindCache(ide);
            ReleaseRegistrationLease();
            ReleaseNativeModelSlotReservation();
            g_state = EState::Refused;
            MarkRegistrationRefused();
            Log("registrar=refused reason=unexpected-archive-id expected=%u actual=%u rollback=complete", Pack().expectedArchiveId, archiveId);
            return;
        }

        g_state = EState::Registering;
        auto*                     txdPool = *reinterpret_cast<CPoolSAInterface<CTextureDictonarySAInterface>**>(0xC8800C);
        const auto                addTxd = reinterpret_cast<int(__cdecl*)(const char*)>(ADD_TXD_SLOT);
        const auto                findTxd = reinterpret_cast<int(__cdecl*)(const char*)>(FIND_TXD_SLOT);
        std::vector<unsigned int> ownedTxdSlots;
        const auto                rollbackTxdAllocations = [&]()
        {
            for (auto slot = ownedTxdSlots.rbegin(); slot != ownedTxdSlots.rend(); ++slot)
            {
                if (txdPool->IsContains(*slot))
                    txdPool->Release(*slot);
                *txdPool->GetObject(*slot) = ide.txdOriginalObjects[*slot];
                reinterpret_cast<unsigned char*>(txdPool->m_byteMap)[*slot] = ide.txdOriginalFlags[*slot];
            }
            txdPool->m_nFirstFree = ide.txdOriginalFirstFree;
            RestoreTxdFindCache(ide);
            g_streaming->RemoveArchive(archiveId);
        };
        for (const std::string& name : ide.txdNames)
        {
            const unsigned int expected = ide.txdSlots.at(name);
            const int          allocated = addTxd(name.c_str());
            if (allocated >= 0 && allocated < txdPool->m_nSize && !ide.txdOriginallyOccupied[allocated] &&
                std::find(ownedTxdSlots.begin(), ownedTxdSlots.end(), allocated) == ownedTxdSlots.end())
                ownedTxdSlots.push_back(static_cast<unsigned int>(allocated));

            if (allocated != static_cast<int>(expected) || findTxd(name.c_str()) != static_cast<int>(expected))
            {
                // These slots have no streaming entries or dictionaries yet.
                // Releasing in reverse and restoring the saved cursor makes
                // this failed pre-IDE allocation indistinguishable from no run.
                rollbackTxdAllocations();
                ReleaseRegistrationLease();
                ReleaseNativeModelSlotReservation();
                g_state = EState::Refused;
                MarkRegistrationRefused();
                Log("registrar=refused reason=TXD-allocation-plan-mismatch name=%s expected=%u actual=%d rollback=complete restoredFirstFree=%d", name.c_str(),
                    expected, allocated, ide.txdOriginalFirstFree);
                return;
            }
        }
        const unsigned int expectedFinalCursor = ide.txdSlots.at(*ide.txdNames.rbegin());
        if (txdPool->m_nFirstFree != static_cast<int>(expectedFinalCursor))
        {
            const int actualCursor = txdPool->m_nFirstFree;
            rollbackTxdAllocations();
            ReleaseRegistrationLease();
            ReleaseNativeModelSlotReservation();
            g_state = EState::Refused;
            MarkRegistrationRefused();
            Log("registrar=refused reason=TXD-allocation-cursor-mismatch expected=%u actual=%d rollback=complete restoredFirstFree=%d", expectedFinalCursor,
                actualCursor, ide.txdOriginalFirstFree);
            return;
        }

        // LoadObjectTypes reopens the IDE rather than consuming validator
        // bytes. The pending cache lease now holds that exact path immutable
        // across this irreversible commit point.
        reinterpret_cast<void(__cdecl*)(const char*)>(LOAD_OBJECT_TYPES)(idePath.c_str());
        ValidateIdePostconditions(ide);
        reinterpret_cast<void(__cdecl*)(const char*, int32_t)>(LOAD_NAMED_CD_DIRECTORY)(imgPath.c_str(), archiveId);

        ValidatePostconditions(ide, archiveId);
        if (!RunNativeStoreBoundaryHarness(imgPath, ide, error))
            Fatal(error.c_str());
        EnableOwnedIplDynamicStreaming(ide);
        CommitRegistrationLease();
        g_state = EState::Active;
        if (g_authorizedRoute)
            g_pCore->MarkNativeWorldStartupActive();
        std::ostringstream iplSlots;
        for (unsigned int index = 0; index < Pack().iplCount; ++index)
        {
            if (index)
                iplSlots << ',';
            iplSlots << ide.iplSlots.at(Pack().iplNames[index]);
        }
        Log("registrar=active archive=%u models=%u txds=%u txdSlots=%u..%u txdSpanHoles=%u colSlot=%u iplSlots=%s entries=%u lodLinks=none", archiveId,
            Pack().modelCount, Pack().txdCount, ide.txdPlanMin, ide.txdPlanMax, ide.txdPlanSpanHoles, ide.colSlot, iplSlots.str().c_str(),
            Pack().imgEntryCount);
    }

    void __cdecl LoadCdDirectoryHook()
    {
        reinterpret_cast<void(__cdecl*)()>(LOAD_CD_DIRECTORY)();
        if (g_state == EState::Hooked)
            RegisterPack();
    }

    bool AuditStaticWorldV3Directory(const std::filesystem::path& directory, const SStaticWorldV3Manifest& manifest, const std::function<bool()>& isCancelled,
                                     SStaticWorldV3Inventory& inventory, std::string& error)
    {
        const std::filesystem::path idePath = directory / manifest.ide.name;
        const SString               nativeIdePath = idePath.string().c_str();
        if (!IsNativePathSafe(nativeIdePath) || !IsSafeRegularFile(nativeIdePath) || !HasExactFileSize64(nativeIdePath, manifest.ide.bytes) ||
            SharedUtil::GenerateSha256HexStringFromFile(nativeIdePath).ToLower() != manifest.ide.sha256.c_str())
        {
            error = "static-world-v3 IDE identity differs from its manifest";
            return false;
        }
        if (isCancelled())
        {
            error = "static-world-v3 publication was cancelled";
            return false;
        }
        const std::filesystem::path lodPath = directory / manifest.lod.name;
        const SString               nativeLodPath = lodPath.string().c_str();
        if (!IsNativePathSafe(nativeLodPath) || !IsSafeRegularFile(nativeLodPath) || !HasExactFileSize64(nativeLodPath, manifest.lod.bytes) ||
            SharedUtil::GenerateSha256HexStringFromFile(nativeLodPath).ToLower() != manifest.lod.sha256.c_str())
        {
            error = "static-world-v3 LOD identity differs from its manifest";
            return false;
        }

        SStaticWorldV3Ide ide;
        if (!ParseStaticWorldV3Ide(nativeIdePath, ide, error))
            return false;
        inventory.modelCount = static_cast<unsigned int>(ide.modelIds.size());
        inventory.ordinaryModels = ide.ordinaryModels;
        inventory.damageModels = ide.damageModels;
        inventory.timedModels = ide.timedModels;
        inventory.txdCount = static_cast<unsigned int>(ide.txdStems.size());
        inventory.imageCount = static_cast<unsigned int>(manifest.images.size());
        inventory.nameSpace = ide.nameSpace;
        inventory.modelIds = ide.modelIds;
        inventory.firstModel = ide.firstModel;
        inventory.lastModel = ide.lastModel;
        for (const SStaticWorldV3File& image : manifest.images)
        {
            const std::filesystem::path imagePath = directory / image.name;
            const SString               nativeImagePath = imagePath.string().c_str();
            if (!IsNativePathSafe(nativeImagePath) || !IsSafeRegularFile(nativeImagePath) || !HasExactFileSize64(nativeImagePath, image.bytes) ||
                SharedUtil::GenerateSha256HexStringFromFile(nativeImagePath).ToLower() != image.sha256.c_str() ||
                !ValidateStaticWorldV3Archive(imagePath, image.bytes, ide, inventory, error))
            {
                if (error.empty())
                    error = "static-world-v3 IMG identity differs from its manifest";
                return false;
            }
            if (isCancelled())
            {
                error = "static-world-v3 publication was cancelled";
                return false;
            }
        }
        return ValidateStaticWorldV3Inventory(ide, inventory, error) && ValidateStaticWorldV3LodMap(lodPath, inventory, error);
    }

    bool BuildStaticWorldV3CachedRequest(const SNativeWorldV3SetPackSA& identity, SNativeWorldCacheRequestSA& request, SStaticWorldV3Manifest& manifest,
                                         std::string& directory, std::string& error)
    {
        directory = SString("%s\\native-world-cache\\v3\\%s\\%s", SharedUtil::GetMTADataPath().c_str(), STATIC_WORLD_V3_POLICY, identity.contentId.c_str());
        const SString manifestPath = SString("%s\\%s", directory.c_str(), STATIC_WORLD_V3_MANIFEST);
        if (!IsNativePathSafe(manifestPath) || !IsSafeRegularFile(manifestPath) || !LoadStaticWorldV3Manifest(manifestPath, manifest, error) ||
            manifest.packId != identity.packId)
        {
            if (error.empty())
                error = "static-world-v3-set child manifest is missing, unsafe, or has the wrong pack ID";
            return false;
        }
        request.format = 3;
        request.policyKey = STATIC_WORLD_V3_POLICY;
        request.packId = manifest.packId;
        request.manifestFileName = STATIC_WORLD_V3_MANIFEST;
        request.sourceManifestSha256 = manifest.manifestSha256;
        request.sourceManifestBytes = manifest.manifestBytes;
        request.maximumManifestBytes = STATIC_WORLD_V3_MAX_MANIFEST_BYTES;
        request.ide = {manifest.ide.name, manifest.ide.sha256, manifest.ide.bytes};
        request.lod = {manifest.lod.name, manifest.lod.sha256, manifest.lod.bytes};
        for (const SStaticWorldV3File& image : manifest.images)
            request.images.push_back({image.name, image.sha256, image.bytes});
        request.contentId = GenerateNativeWorldContentId(request);
        if (request.contentId != identity.contentId)
        {
            error = "static-world-v3-set child content ID does not recompute from its locked manifest";
            return false;
        }
        return true;
    }

    bool ValidateStaticWorldV3Aggregate(const std::array<SNativeWorldV3SetPackSA, 4>& packs, const std::array<SStaticWorldV3Inventory, 4>& inventories,
                                        std::string& error)
    {
        constexpr const char*               expectedNamespaces[] = {"bw", "vc", "lc", "cc"};
        constexpr unsigned int              expectedFirstModels[] = {20000, 21054, 24856, 28344};
        constexpr unsigned int              expectedLastModels[] = {21053, 24855, 28343, 31836};
        std::set<std::string>               globalMembers;
        std::set<unsigned int>              globalModelIds;
        std::map<unsigned int, std::string> globalModelKeys;
        std::map<unsigned int, std::string> globalTxdKeys;
        unsigned int                        ordinary = 13984;
        unsigned int                        damage = 69;
        unsigned int                        timed = 160;
        unsigned int                        txds = 3608;
        unsigned int                        colSlots = 252;
        unsigned int                        iplSlots = 191;
        unsigned int                        archives = 6;
        unsigned int                        handles = 10;
        unsigned int                        totalModels = 0;
        unsigned int                        totalPlacements = 0;
        unsigned int                        maximumCityPlacements = 0;
        unsigned int                        maximumCityColModels = 0;
        for (size_t index = 0; index < inventories.size(); ++index)
        {
            const SStaticWorldV3Inventory& inventory = inventories[index];
            if (inventory.nameSpace != expectedNamespaces[index] || inventory.firstModel != expectedFirstModels[index] ||
                inventory.lastModel != expectedLastModels[index])
            {
                error = SString("static-world-v3-set pack %s namespace/range differs actual=%s:%u..%u expected=%s:%u..%u", packs[index].packId.c_str(),
                                inventory.nameSpace.c_str(), inventory.firstModel, inventory.lastModel, expectedNamespaces[index], expectedFirstModels[index],
                                expectedLastModels[index]);
                return false;
            }
            for (unsigned int modelId : inventory.modelIds)
            {
                if (!globalModelIds.emplace(modelId).second)
                {
                    error = SString("static-world-v3-set cross-pack model ID collision: %u", modelId);
                    return false;
                }
            }
            for (const auto& [name, ignored] : inventory.entries)
            {
                if (!globalMembers.emplace(name).second)
                {
                    error = SString("static-world-v3-set cross-pack member collision: %s", name.c_str());
                    return false;
                }
                const size_t dot = name.rfind('.');
                if (dot != std::string::npos && (name.compare(dot, 4, ".dff") == 0 || name.compare(dot, 4, ".txd") == 0))
                {
                    const unsigned int key = StaticWorldV3UppercaseKey(name.substr(0, dot));
                    auto&              keys = name.compare(dot, 4, ".dff") == 0 ? globalModelKeys : globalTxdKeys;
                    const auto [owner, inserted] = keys.emplace(key, name);
                    if (!inserted && owner->second != name)
                    {
                        error = SString("static-world-v3-set GTA uppercase-key collision: %s / %s", owner->second.c_str(), name.c_str());
                        return false;
                    }
                }
            }
            ordinary += inventory.ordinaryModels;
            damage += inventory.damageModels;
            timed += inventory.timedModels;
            txds += inventory.txdCount;
            colSlots += static_cast<unsigned int>(inventory.colStems.size());
            iplSlots += static_cast<unsigned int>(inventory.iplStems.size());
            archives += inventory.imageCount;
            handles += inventory.imageCount;
            totalModels += inventory.modelCount;
            totalPlacements += inventory.placements;
            maximumCityPlacements = std::max(maximumCityPlacements, inventory.placements);
            unsigned int cityColModels = 0;
            for (const auto& [ordinal, ids] : inventory.colModelIds)
                cityColModels += static_cast<unsigned int>(ids.size());
            maximumCityColModels = std::max(maximumCityColModels, cityColModels);
        }
        if (ordinary > 32000 || damage > 512 || timed > 1024 || txds > 8000 || colSlots > 512 || iplSlots > 1024 || archives > 245 || handles > 255 ||
            totalModels > 12000 || 9980 + maximumCityColModels > 30000 || 9166 + maximumCityPlacements > 32000)
        {
            error = SString(
                "static-world-v3-set capacity proof failed atomic=%u/32000 damage=%u/512 timed=%u/1024 txd=%u/8000 col=%u/512 ipl=%u/1024 "
                "archives=%u/245 handles=%u/255 models=%u/12000 colModel=%u/30000 buildings=%u/32000",
                ordinary, damage, timed, txds, colSlots, iplSlots, archives, handles, totalModels, 9980 + maximumCityColModels, 9166 + maximumCityPlacements);
            return false;
        }
        SharedUtil::WriteDebugEvent(
            SString("[NativeWorldAggregatePlanner] state=proved packs=4 models=%u placements=%u atomic=%u/32000 damage=%u/512 timed=%u/1024 txd=%u/8000 "
                    "col=%u/512 ipl=%u/1024 archives=%u/245 handles=%u/255 maxCityBuildings=%u/32000 maxCityColModels=%u/30000 nativeWrites=0",
                    totalModels, totalPlacements, ordinary, damage, timed, txds, colSlots, iplSlots, archives, handles, 9166 + maximumCityPlacements,
                    9980 + maximumCityColModels));
        return true;
    }

    bool AcquireStaticWorldV3SetChildren(const SStaticWorldV3SetManifest& setManifest, const std::string& ticketId, const std::function<bool()>& isCancelled,
                                         std::array<CNativeWorldCacheLeaseSA, 4>& leases, std::array<SStaticWorldV3Inventory, 4>& inventories,
                                         std::string& error)
    {
        for (size_t index = 0; index < setManifest.packs.size(); ++index)
        {
            SNativeWorldCacheRequestSA request;
            SStaticWorldV3Manifest     manifest;
            std::string                directory;
            if (!BuildStaticWorldV3CachedRequest(setManifest.packs[index], request, manifest, directory, error))
                return false;
            request.cancellation = nullptr;
            const auto audit = [&manifest, &inventories, index, &isCancelled](const std::string& lockedDirectory, std::string& auditError)
            { return AuditStaticWorldV3Directory(lockedDirectory, manifest, isCancelled, inventories[index], auditError); };
            std::string leasedDirectory;
            if (!AcquireExistingNativeWorldCacheLease(request, ticketId, audit, leases[index], leasedDirectory, error))
                return false;
        }
        return ValidateStaticWorldV3Aggregate(setManifest.packs, inventories, error);
    }

    SNativeWorldTransportPublishResult PublishStaticWorldV3SetOffer(const SNativeWorldTransportOffer& offer, const std::function<bool()>& isCancelled)
    {
        SNativeWorldTransportPublishResult result;
        result.auditProfile = STATIC_WORLD_V3_SET_AUDIT;
        if (!offer.startupAuthorization || offer.files.size() != 1 || offer.manifestRelativePath.empty() || offer.startupAuthorization->packFormat != 3 ||
            offer.startupAuthorization->wireVersion != 3 || offer.startupAuthorization->startupMode != 1 || offer.startupAuthorization->policy != 3)
        {
            result.error = "static-world-v3-set requires its exact format-3 startup authorization tuple";
            return result;
        }
        const SNativeWorldTransportFile& manifestOffer = offer.files.front();
        const std::filesystem::path      absolute = std::filesystem::path(manifestOffer.absolutePath).lexically_normal();
        if (manifestOffer.relativePath != offer.manifestRelativePath || absolute.filename().generic_string() != STATIC_WORLD_V3_SET_MANIFEST ||
            manifestOffer.declaredBytes > STATIC_WORLD_V3_SET_MAX_MANIFEST_BYTES)
        {
            result.error = "static-world-v3-set transport envelope path or byte declaration is invalid";
            return result;
        }
        SStaticWorldV3SetManifest manifest;
        const SString             manifestPath = absolute.string().c_str();
        if (!IsNativePathSafe(manifestPath) || !IsSafeRegularFile(manifestPath) || !LoadStaticWorldV3SetManifest(manifestPath, manifest, result.error) ||
            manifestOffer.declaredBytes != manifest.manifestBytes)
            return result;

        SNativeWorldV3SetRequestSA request;
        request.sourceAbsoluteDirectory = absolute.parent_path().string();
        request.manifestFileName = STATIC_WORLD_V3_SET_MANIFEST;
        request.sourceManifestSha256 = manifest.manifestSha256;
        request.sourceManifestBytes = manifest.manifestBytes;
        request.setId = manifest.setId;
        request.packs = manifest.packs;
        request.cancellation = offer.cancelled;
        result.contentId = manifest.setId;
        std::ostringstream offerIdentity;
        offerIdentity << "mta-native-world-transport-offer-v3-set\nresource=" << offer.resourceName << "\nformat=3\nmanifest=" << offer.manifestRelativePath
                      << "\nmanifest.bytes=" << manifest.manifestBytes << "\nmanifest.sha256=" << manifest.manifestSha256 << "\nset_id=" << manifest.setId
                      << '\n';
        result.offerId = SharedUtil::GenerateSha256HexString(offerIdentity.str()).ToLower();
        const auto audit = [&manifest, &isCancelled](const std::string&, std::string& auditError)
        {
            std::array<CNativeWorldCacheLeaseSA, 4> leases;
            std::array<SStaticWorldV3Inventory, 4>  inventories;
            const std::string                       syntheticTicket = manifest.setId.substr(0, 32);
            return AcquireStaticWorldV3SetChildren(manifest, syntheticTicket, isCancelled, leases, inventories, auditError);
        };
        result.success = PublishNativeWorldV3Set(request, audit, result.publishedDirectory, result.cacheHit, result.error);
        return result;
    }

    SNativeWorldTransportPublishResult PublishStaticWorldV3TransportOffer(const SNativeWorldTransportOffer& offer, const std::function<bool()>& isCancelled)
    {
        SNativeWorldTransportPublishResult result;
        result.auditProfile = STATIC_WORLD_V3_AUDIT;
        if (offer.resourceName.empty() || offer.resourceName.size() > 255 || offer.manifestRelativePath.empty())
        {
            result.error = "static-world-v3 transport offer identity is invalid";
            return result;
        }

        std::map<std::string, const SNativeWorldTransportFile*> offeredFiles;
        for (const SNativeWorldTransportFile& file : offer.files)
        {
            const std::filesystem::path relative(file.relativePath);
            if (file.relativePath.empty() || file.relativePath.size() > 255 || relative.has_root_path() ||
                relative.lexically_normal().generic_string() != file.relativePath || !file.declaredBytes ||
                !offeredFiles.emplace(file.relativePath, &file).second)
            {
                result.error = "static-world-v3 transport file identity is non-canonical or duplicated";
                return result;
            }
        }
        const auto manifestOffer = offeredFiles.find(offer.manifestRelativePath);
        if (manifestOffer == offeredFiles.end())
        {
            result.error = "static-world-v3 transport manifest is absent";
            return result;
        }

        const std::filesystem::path manifestAbsolute = std::filesystem::path(manifestOffer->second->absolutePath).lexically_normal();
        const std::filesystem::path sourceDirectory = manifestAbsolute.parent_path();
        if (manifestAbsolute.filename().generic_string() != STATIC_WORLD_V3_MANIFEST || sourceDirectory.empty())
        {
            result.error = "static-world-v3 transport manifest filename differs from the closed policy";
            return result;
        }
        const SString          manifestPath = manifestAbsolute.string().c_str();
        SStaticWorldV3Manifest manifest;
        if (!IsNativePathSafe(manifestPath) || !IsSafeRegularFile(manifestPath) || !LoadStaticWorldV3Manifest(manifestPath, manifest, result.error) ||
            manifestOffer->second->declaredBytes != manifest.manifestBytes)
        {
            if (result.error.empty())
                result.error = "static-world-v3 transport manifest identity is invalid";
            return result;
        }

        const std::string relativeDirectory = std::filesystem::path(offer.manifestRelativePath).parent_path().generic_string();
        const auto        composeRelative = [&relativeDirectory](const std::string& leaf)
        { return relativeDirectory.empty() ? leaf : relativeDirectory + "/" + leaf; };
        if (offeredFiles.size() != manifest.images.size() + 3)
        {
            result.error = "static-world-v3 transport offer does not contain the exact manifest, IDE, LOD, and ordered IMG set";
            return result;
        }
        const auto bindFile = [&](const SStaticWorldV3File& identity)
        {
            const auto found = offeredFiles.find(composeRelative(identity.name));
            if (found == offeredFiles.end() || found->second->declaredBytes != identity.bytes ||
                std::filesystem::path(found->second->absolutePath).lexically_normal().parent_path() != sourceDirectory ||
                std::filesystem::path(found->second->absolutePath).lexically_normal().filename().generic_string() != identity.name)
                return false;
            return true;
        };
        if (!bindFile(manifest.ide) || !bindFile(manifest.lod))
        {
            result.error = "static-world-v3 IDE or LOD does not bind its exact transport file";
            return result;
        }
        for (const SStaticWorldV3File& image : manifest.images)
        {
            if (!bindFile(image))
            {
                result.error = "static-world-v3 IMG does not bind its exact ordered transport file";
                return result;
            }
        }
        if (isCancelled())
        {
            result.error = "static-world-v3 publication was cancelled";
            return result;
        }

        SStaticWorldV3Inventory inventory;
        if (!AuditStaticWorldV3Directory(sourceDirectory, manifest, isCancelled, inventory, result.error))
            return result;

        std::ostringstream offerIdentity;
        offerIdentity << "mta-native-world-transport-offer-v3\nresource=" << offer.resourceName << "\nformat=3\nmanifest=" << offer.manifestRelativePath
                      << "\nmanifest.bytes=" << manifest.manifestBytes << "\nmanifest.sha256=" << manifest.manifestSha256 << "\nide.name=" << manifest.ide.name
                      << "\nide.bytes=" << manifest.ide.bytes << "\nide.sha256=" << manifest.ide.sha256 << "\nlod.name=" << manifest.lod.name
                      << "\nlod.bytes=" << manifest.lod.bytes << "\nlod.sha256=" << manifest.lod.sha256 << '\n';
        for (const SStaticWorldV3File& image : manifest.images)
            offerIdentity << "img.name=" << image.name << "\nimg.bytes=" << image.bytes << "\nimg.sha256=" << image.sha256 << '\n';
        result.offerId = SharedUtil::GenerateSha256HexString(offerIdentity.str()).ToLower();

        SNativeWorldCacheRequestSA request;
        request.format = STATIC_WORLD_V3_FORMAT;
        request.sourceAbsoluteDirectory = sourceDirectory.string();
        request.policyKey = STATIC_WORLD_V3_POLICY;
        request.packId = manifest.packId;
        request.manifestFileName = STATIC_WORLD_V3_MANIFEST;
        request.sourceManifestSha256 = manifest.manifestSha256;
        request.sourceManifestBytes = manifest.manifestBytes;
        request.maximumManifestBytes = STATIC_WORLD_V3_MAX_MANIFEST_BYTES;
        request.ide = {manifest.ide.name, manifest.ide.sha256, manifest.ide.bytes};
        request.lod = {manifest.lod.name, manifest.lod.sha256, manifest.lod.bytes};
        for (const SStaticWorldV3File& image : manifest.images)
            request.images.push_back({image.name, image.sha256, image.bytes});
        request.cancellation = offer.cancelled;
        request.contentId = GenerateNativeWorldContentId(request);
        result.contentId = request.contentId;

        const auto auditQuarantine = [&request, &isCancelled](const std::string& directory, std::string& auditError)
        {
            const std::filesystem::path manifestPath = std::filesystem::path(directory) / STATIC_WORLD_V3_MANIFEST;
            const SString               nativeManifestPath = manifestPath.string().c_str();
            SStaticWorldV3Manifest      lockedManifest;
            if (isCancelled())
            {
                auditError = "static-world-v3 publication was cancelled";
                return false;
            }
            if (!IsNativePathSafe(nativeManifestPath) || !IsSafeRegularFile(nativeManifestPath) ||
                !LoadStaticWorldV3Manifest(nativeManifestPath, lockedManifest, auditError))
            {
                if (auditError.empty())
                    auditError = "static-world-v3 quarantine manifest is unsafe";
                return false;
            }
            SNativeWorldCacheRequestSA lockedIdentity = request;
            lockedIdentity.packId = lockedManifest.packId;
            lockedIdentity.sourceManifestSha256 = lockedManifest.manifestSha256;
            lockedIdentity.sourceManifestBytes = lockedManifest.manifestBytes;
            lockedIdentity.ide = {lockedManifest.ide.name, lockedManifest.ide.sha256, lockedManifest.ide.bytes};
            lockedIdentity.lod = {lockedManifest.lod.name, lockedManifest.lod.sha256, lockedManifest.lod.bytes};
            lockedIdentity.images.clear();
            for (const SStaticWorldV3File& image : lockedManifest.images)
                lockedIdentity.images.push_back({image.name, image.sha256, image.bytes});
            if (GenerateNativeWorldContentId(lockedIdentity) != request.contentId)
            {
                auditError = "static-world-v3 quarantine semantic content ID differs from the offer";
                return false;
            }
            SStaticWorldV3Inventory lockedInventory;
            return AuditStaticWorldV3Directory(directory, lockedManifest, isCancelled, lockedInventory, auditError);
        };
        result.success = PublishNativeWorldCache(request, auditQuarantine, result.publishedDirectory, result.cacheHit, result.error);
        return result;
    }
}  // namespace

void CNativeWorldPackManagerSA::HandleStartupSelection(eGameVersion gameVersion, const SNativeWorldStartupSelection& selection)
{
    std::lock_guard<std::mutex> lock(g_transportPublisherMutex);
    const auto                  isCancelled = [&selection]() { return g_pCore->IsNativeWorldStartupSelectionCancelled(selection.ticketId); };
    const auto                  resetAuditState = []()
    {
        g_policy = nullptr;
        g_manifest = {};
        g_activeDirectory.clear();
        g_iplNamePointers.clear();
        g_runtimeDescriptor = {};
        g_pack = nullptr;
    };
    const auto finish = [&selection](bool claim, const char* reason)
    {
        SNativeWorldAuthorizationRecordResult result = g_pCore->FinishNativeWorldStartupSelection(selection.ticketId, claim, claim ? "" : reason);
        if (!result.success)
            SharedUtil::WriteDebugEvent(SString("[NativeWorldAuthorization] state=finish-failed ticket=%s detail=%s activation=no lease=no",
                                                selection.ticketId.substr(0, 8).c_str(), result.error.c_str()));
        else if (claim && result.claimed)
            SharedUtil::WriteDebugEvent(
                SString("[NativeWorldAuthorization] state=claimed ticket=%s activation=no lease=pending", selection.ticketId.substr(0, 8).c_str()));
        else
            SharedUtil::WriteDebugEvent(SString("[NativeWorldAuthorization] %s", result.diagnostic.c_str()));
        return result.success && (!claim || result.claimed);
    };
    const auto refuse = [&](const char* reason, const std::string& detail)
    {
        finish(false, reason);
        SharedUtil::WriteDebugEvent(SString("[NativeWorldAuthorization] state=refused reason=%s detail=%s activation=no lease=no", reason, detail.c_str()));
        resetAuditState();
    };

    if (selection.packFormat == NATIVE_WORLD_STATIC_V3_SET_FORMAT)
    {
        if (!selection.ready ||
            !IsClosedNativeWorldStartupAuthorization(selection.wireVersion, selection.startupMode, selection.policy, selection.packFormat) ||
            selection.policy != NATIVE_WORLD_STATIC_V3_SET_POLICY || g_state != EState::Off)
        {
            refuse("selection-invalid", "static-world-v3-set startup selection is not a closed idle transaction");
            return;
        }
        const std::string setDirectory =
            SString("%s\\native-world-cache\\v3\\%s\\%s", SharedUtil::GetMTADataPath().c_str(), STATIC_WORLD_V3_SET_POLICY, selection.contentId.c_str());
        const SString             setManifestPath = SString("%s\\%s", setDirectory.c_str(), STATIC_WORLD_V3_SET_MANIFEST);
        SStaticWorldV3SetManifest setManifest;
        std::string               error;
        if (!IsNativePathSafe(setManifestPath) || !IsSafeRegularFile(setManifestPath) || !LoadStaticWorldV3SetManifest(setManifestPath, setManifest, error) ||
            setManifest.setId != selection.contentId)
        {
            if (error.empty())
                error = "authorized static-world-v3-set envelope is unavailable or has the wrong set ID";
            refuse("cache-invalid", error);
            return;
        }
        SNativeWorldV3SetRequestSA request;
        request.manifestFileName = STATIC_WORLD_V3_SET_MANIFEST;
        request.sourceManifestSha256 = setManifest.manifestSha256;
        request.sourceManifestBytes = setManifest.manifestBytes;
        request.setId = setManifest.setId;
        request.packs = setManifest.packs;
        const auto auditSet = [&setManifest, &isCancelled](const std::string& lockedDirectory, std::string& auditError)
        {
            const SString             lockedManifestPath = SString("%s\\%s", lockedDirectory.c_str(), STATIC_WORLD_V3_SET_MANIFEST);
            SStaticWorldV3SetManifest lockedManifest;
            bool                      samePacks = true;
            if (!isCancelled() && LoadStaticWorldV3SetManifest(lockedManifestPath, lockedManifest, auditError))
            {
                for (size_t index = 0; index < lockedManifest.packs.size(); ++index)
                    samePacks = samePacks && lockedManifest.packs[index].packId == setManifest.packs[index].packId &&
                                lockedManifest.packs[index].contentId == setManifest.packs[index].contentId;
            }
            else
                samePacks = false;
            if (!samePacks || lockedManifest.setId != setManifest.setId)
            {
                if (auditError.empty())
                    auditError = isCancelled() ? "static-world-v3-set dry-run was cancelled" : "locked set envelope identity changed";
                return false;
            }
            std::array<CNativeWorldCacheLeaseSA, 4> childLeases;
            std::array<SStaticWorldV3Inventory, 4>  childInventories;
            return AcquireStaticWorldV3SetChildren(lockedManifest, setManifest.setId.substr(0, 32), isCancelled, childLeases, childInventories, auditError);
        };
        CNativeWorldCacheLeaseSA setLease;
        std::string              leasedDirectory;
        if (!AcquireExistingNativeWorldV3SetLease(request, selection.ticketId, auditSet, setLease, leasedDirectory, error))
        {
            refuse(isCancelled() ? "startup-cancelled" : "aggregate-dry-run-failed", error);
            return;
        }
        SharedUtil::WriteDebugEvent(
            SString("[NativeWorldAuthorization] state=aggregate-cache-audited format=3 policy=%s setId=%s ticket=%s activation=no "
                    "leasesLockedDuringPlan=5 childLeases=released:4 setLease=pending nativeWrites=0 allocations=0 hooks=0 archives=0",
                    STATIC_WORLD_V3_SET_POLICY, setManifest.setId.c_str(), selection.ticketId.substr(0, 8).c_str()));
        if (!CNativeModelStoreSA::ValidateExecutableAndPatchManifestReadOnly(gameVersion, error) || isCancelled() || !setLease.RevalidateClosedObject(error))
        {
            setLease.Release();
            refuse(isCancelled() ? "startup-cancelled" : "executable-invalid", error);
            return;
        }
        setLease.Release();
        if (!finish(false, "aggregate-dry-run"))
        {
            resetAuditState();
            return;
        }
        SharedUtil::WriteDebugEvent(
            SString("[NativeWorldAuthorization] state=aggregate-dry-run-complete setId=%s ticket=%s activation=no leases=released:5 "
                    "nativeWrites=0 allocations=0 hooks=0 archives=0 stock-behavior=preserved",
                    setManifest.setId.c_str(), selection.ticketId.substr(0, 8).c_str()));
        resetAuditState();
        return;
    }

    const SNativeWorldPackPolicySA* selectedPolicy = SelectAuthorizedPolicy(selection);
    if (!selection.ready || !selectedPolicy || g_state != EState::Off)
    {
        refuse("selection-invalid", "startup selection is not a closed idle native-world transaction");
        return;
    }

    const SNativeWorldPackPolicySA& policy = *selectedPolicy;
    g_policy = &policy;
    g_activeDirectory =
        SString("%s\\native-world-cache\\v%u\\%s\\%s", SharedUtil::GetMTADataPath().c_str(), policy.format, policy.key, selection.contentId.c_str());
    const SString manifestPath = SString("%s\\%s", g_activeDirectory.c_str(), policy.runtimeManifestFileName);
    std::string   error;
    if (!IsNativePathSafe(manifestPath) || !IsSafeRegularFile(manifestPath) || !LoadRuntimeManifest(manifestPath, error))
    {
        if (error.empty())
            error = "selected canonical cache manifest is unavailable or unsafe";
        refuse("cache-invalid", error);
        return;
    }

    SNativeWorldCacheRequestSA request;
    request.format = g_manifest.format;
    request.policyKey = policy.key;
    request.packId = g_manifest.packId;
    request.manifestFileName = policy.runtimeManifestFileName;
    request.sourceManifestSha256 = g_manifest.manifestSha256;
    request.sourceManifestBytes = g_manifest.manifestBytes;
    request.maximumManifestBytes = policy.maximumManifestBytes;
    request.ide = {g_manifest.ideFileName, g_manifest.ideSha256, g_manifest.ideBytes};
    request.img = {g_manifest.imgFileName, g_manifest.imgSha256, g_manifest.imgBytes};
    request.contentId = GenerateNativeWorldContentId(request);
    if (request.contentId != selection.contentId)
    {
        refuse("cache-invalid", "selected cache manifest does not recompute to the authorized content ID");
        return;
    }

    const auto auditExisting = [&request, &policy, &isCancelled](const std::string& directory, std::string& auditError)
    {
        if (isCancelled())
        {
            auditError = "startup cache audit was cancelled";
            return false;
        }
        g_activeDirectory = directory;
        const SString lockedManifestPath = SString("%s\\%s", directory.c_str(), policy.runtimeManifestFileName);
        if (!IsNativePathSafe(lockedManifestPath) || !IsSafeRegularFile(lockedManifestPath) || !LoadRuntimeManifest(lockedManifestPath, auditError) ||
            isCancelled())
        {
            if (isCancelled())
                auditError = "startup cache audit was cancelled";
            else if (auditError.empty())
                auditError = "locked canonical manifest is unsafe";
            return false;
        }
        SNativeWorldCacheRequestSA lockedIdentity = request;
        lockedIdentity.format = g_manifest.format;
        lockedIdentity.packId = g_manifest.packId;
        lockedIdentity.sourceManifestSha256 = g_manifest.manifestSha256;
        lockedIdentity.sourceManifestBytes = g_manifest.manifestBytes;
        lockedIdentity.ide = {g_manifest.ideFileName, g_manifest.ideSha256, g_manifest.ideBytes};
        lockedIdentity.img = {g_manifest.imgFileName, g_manifest.imgSha256, g_manifest.imgBytes};
        if (GenerateNativeWorldContentId(lockedIdentity) != request.contentId)
        {
            auditError = "locked canonical manifest differs from the authorized semantic content ID";
            return false;
        }
        if (isCancelled())
        {
            auditError = "startup cache audit was cancelled";
            return false;
        }
        const SString idePath = SString("%s\\%s", directory.c_str(), g_manifest.ideFileName.c_str());
        const SString imgPath = SString("%s\\%s", directory.c_str(), g_manifest.imgFileName.c_str());
        if (!IsNativePathSafe(idePath) || !IsNativePathSafe(imgPath) || !IsSafeRegularFile(idePath) || !IsSafeRegularFile(imgPath) ||
            !HasExactFileSize(idePath, g_manifest.ideBytes) || !HasExactFileSize(imgPath, g_manifest.imgBytes) ||
            SharedUtil::GenerateSha256HexStringFromFile(idePath).ToLower() != g_manifest.ideSha256.c_str() ||
            SharedUtil::GenerateSha256HexStringFromFile(imgPath).ToLower() != g_manifest.imgSha256.c_str())
        {
            auditError = "locked cache payload identity differs from its manifest";
            return false;
        }
        SIdePlan   ide;
        const auto continueAudit = [&]()
        {
            if (!isCancelled())
                return true;
            auditError = "startup cache audit was cancelled";
            return false;
        };
        if (!ParseIde(idePath, ide, auditError) || !continueAudit() || !ValidateImg(imgPath, ide, auditError) || !continueAudit() ||
            !ValidateBinaryIpls(imgPath, ide, auditError) || !continueAudit() || !ValidatePayloads(imgPath, ide, auditError) || !continueAudit() ||
            !ValidateDescriptor(auditError) || !continueAudit())
            return false;
        return true;
    };

    CNativeWorldCacheLeaseSA lease;
    std::string              leasedDirectory;
    if (!AcquireExistingNativeWorldCacheLease(request, selection.ticketId, auditExisting, lease, leasedDirectory, error))
    {
        refuse(isCancelled() ? "startup-cancelled" : "cache-invalid", error);
        return;
    }
    SharedUtil::WriteDebugEvent(
        SString("[NativeWorldAuthorization] state=cache-audited format=%u policy=%s packId=%s contentId=%s ticket=%s activation=no lease=pending",
                policy.format, policy.key, g_manifest.packId.c_str(), selection.contentId.c_str(), selection.ticketId.substr(0, 8).c_str()));

    if (!CNativeModelStoreSA::ValidateExecutableAndPatchManifestReadOnly(gameVersion, error) ||
        memcmp(reinterpret_cast<const void*>(LOAD_CD_DIRECTORY_CALL), LOAD_CD_DIRECTORY_CALL_BYTES, sizeof(LOAD_CD_DIRECTORY_CALL_BYTES)) != 0)
    {
        if (error.empty())
            error = "LoadCdDirectory call signature differs from the compiled manifest";
        lease.Release();
        refuse("executable-invalid", error);
        return;
    }
    SharedUtil::WriteDebugEvent(
        SString("[NativeWorldAuthorization] state=executable-valid ticket=%s nativeWrites=0 allocations=0 hooks=0 archives=0 "
                "poolMutations=0 activation=no lease=pending",
                selection.ticketId.substr(0, 8).c_str()));

    if (isCancelled())
    {
        lease.Release();
        refuse("startup-cancelled", "startup selection was cancelled immediately before claim");
        return;
    }
    if (!lease.RevalidateClosedObject(error))
    {
        lease.Release();
        refuse("cache-raced", error);
        return;
    }
    const bool claimed = finish(true, "");
    if (!claimed)
    {
        lease.Release();
        SharedUtil::WriteDebugEvent(
            SString("[NativeWorldAuthorization] state=claim-failed ticket=%s activation=no lease=released", selection.ticketId.substr(0, 8).c_str()));
        resetAuditState();
        return;
    }

    if (!CNativeModelStoreSA::InstallForAuthorizedStartup(gameVersion, error))
    {
        lease.Release();
        g_pCore->TerminateNativeWorldStartup(error);
        return;
    }

    g_authorizedRoute = true;
    g_authorizedSelection = selection;
    g_authorizedLease = std::move(lease);
    PublishNativeModelSlotReservation();
    g_state = EState::Prepared;
    SharedUtil::WriteDebugEvent(
        SString("[NativeWorldAuthorization] state=checkpoint-c-prepared ticket=%s nativeWrites=yes hooks=0 archives=0 poolMutations=0 "
                "activation=prepared lease=pending",
                selection.ticketId.substr(0, 8).c_str()));
}

void CNativeWorldPackManagerSA::AttachAuthorizedStreaming(CStreamingSA* streaming)
{
    if (!g_authorizedRoute)
        return;
    if (g_state != EState::Prepared || !streaming)
    {
        g_authorizedLease.Release();
        g_pCore->TerminateNativeWorldStartup("authorized native-world streaming foundation is unavailable");
        return;
    }
    g_streaming = streaming;
}

bool CNativeWorldPackManagerSA::VerifyAuthorizedStartupBeforeStartGame()
{
    if (!g_authorizedRoute)
        return true;

    std::string error;
    if (CFileIDRuntimeSA::HasStoreExtensionOverflow())
    {
        g_authorizedLease.Release();
        g_pCore->TerminateNativeWorldStartup("COL/IPL full-width side storage overflowed before StartGame");
        return false;
    }
    if (!g_pCore->ValidateNativeWorldStartupSession(error))
    {
        g_authorizedLease.Release();
        g_pCore->TerminateNativeWorldStartup(error);
        return false;
    }
    if (g_state == EState::Active || g_state == EState::Refused || g_state == EState::Hooked)
        return true;
    if (g_state != EState::Prepared || !CNativeModelStoreSA::IsInstalled() || !g_streaming || !g_authorizedLease.IsValid())
    {
        g_authorizedLease.Release();
        g_pCore->TerminateNativeWorldStartup("authorized native-world preparation is incomplete before StartGame");
        return false;
    }
    if (!g_authorizedLease.RevalidateClosedObject(error) ||
        memcmp(reinterpret_cast<const void*>(LOAD_CD_DIRECTORY_CALL), LOAD_CD_DIRECTORY_CALL_BYTES, sizeof(LOAD_CD_DIRECTORY_CALL_BYTES)) != 0)
    {
        if (error.empty())
            error = "LoadCdDirectory call signature changed before authorized hook installation";
        g_authorizedLease.Release();
        g_pCore->TerminateNativeWorldStartup(error);
        return false;
    }

    HookInstallCall(LOAD_CD_DIRECTORY_CALL, reinterpret_cast<DWORD>(&LoadCdDirectoryHook));
    g_state = EState::Hooked;
    SharedUtil::WriteDebugEvent(SString("[NativeWorldAuthorization] state=hooked ticket=%s call=0x%08X activation=committing lease=pending",
                                        g_authorizedSelection.ticketId.substr(0, 8).c_str(), LOAD_CD_DIRECTORY_CALL));
    return true;
}

void CNativeWorldPackManagerSA::CancelAuthorizedActivation()
{
    if (!g_authorizedRoute || g_state == EState::Active)
        return;
    g_authorizedLease.Release();
    ReleaseNativeModelSlotReservation();
    if (g_state != EState::Refused)
        g_state = EState::Refused;
}

void CNativeWorldPackManagerSA::InstallFromEnvironment(CStreamingSA* streaming)
{
    const SNativeWorldPackPolicySA* selected = SelectEnabledPolicy();
    if (!selected)
        return;
    if (g_state != EState::Off)
    {
        Log("registrar=unchanged state=%d", static_cast<int>(g_state));
        return;
    }
    g_policy = selected;
    const SString manifestPath = SharedUtil::CalcMTASAPath(SString("%s\\%s", g_policy->relativeDirectory, g_policy->runtimeManifestFileName));
    std::string   descriptorError;
    if (!IsNativePathSafe(manifestPath) || !IsSafeRegularFile(manifestPath))
        descriptorError = "runtime manifest must be a regular non-reparse ASCII file shorter than MAX_PATH";
    if (!descriptorError.empty() || !LoadRuntimeManifest(manifestPath, descriptorError))
    {
        Log("registrar=refused reason=%s", descriptorError.c_str());
        g_state = EState::Refused;
        return;
    }

    if (!CNativeModelStoreSA::IsInstalled() || !streaming)
    {
        Log("registrar=refused reason=native-model-store-foundation-inactive");
        g_state = EState::Refused;
        return;
    }
    if (memcmp(reinterpret_cast<const void*>(LOAD_CD_DIRECTORY_CALL), LOAD_CD_DIRECTORY_CALL_BYTES, sizeof(LOAD_CD_DIRECTORY_CALL_BYTES)) != 0)
    {
        Log("registrar=refused reason=LoadCdDirectory-call-signature-mismatch");
        g_state = EState::Refused;
        return;
    }

    SNativeWorldCacheRequestSA cacheRequest;
    cacheRequest.format = g_manifest.format;
    cacheRequest.sourceRelativeDirectory = g_policy->relativeDirectory;
    cacheRequest.policyKey = g_policy->key;
    cacheRequest.packId = g_manifest.packId;
    cacheRequest.manifestFileName = g_policy->runtimeManifestFileName;
    cacheRequest.sourceManifestSha256 = g_manifest.manifestSha256;
    cacheRequest.sourceManifestBytes = g_manifest.manifestBytes;
    cacheRequest.maximumManifestBytes = g_policy->maximumManifestBytes;
    cacheRequest.ide = {g_manifest.ideFileName, g_manifest.ideSha256, g_manifest.ideBytes};
    cacheRequest.img = {g_manifest.imgFileName, g_manifest.imgSha256, g_manifest.imgBytes};
    cacheRequest.contentId = GenerateNativeWorldContentId(cacheRequest);
    bool cacheHit = false;
    if (!PrepareAndLockNativeWorldCache(cacheRequest, g_activeDirectory, cacheHit, descriptorError))
    {
        Log("registrar=refused reason=native-world-cache-failed detail=%s", descriptorError.c_str());
        g_state = EState::Refused;
        return;
    }

    // Reparse the canonical published manifest while its file is locked. Its
    // semantic identity must equal the parsed seed even when seed JSON layout
    // differed; no seed-directory path survives beyond this point.
    const std::string expectedContentId = cacheRequest.contentId;
    const SString     cachedManifestPath = SString("%s\\%s", g_activeDirectory.c_str(), g_policy->runtimeManifestFileName);
    if (!LoadRuntimeManifest(cachedManifestPath, descriptorError))
    {
        ReleaseNativeWorldCacheLease();
        Log("registrar=refused reason=cached-manifest-invalid detail=%s", descriptorError.c_str());
        g_state = EState::Refused;
        return;
    }
    SNativeWorldCacheRequestSA cachedIdentity = cacheRequest;
    cachedIdentity.format = g_manifest.format;
    cachedIdentity.packId = g_manifest.packId;
    cachedIdentity.ide = {g_manifest.ideFileName, g_manifest.ideSha256, g_manifest.ideBytes};
    cachedIdentity.img = {g_manifest.imgFileName, g_manifest.imgSha256, g_manifest.imgBytes};
    if (GenerateNativeWorldContentId(cachedIdentity) != expectedContentId)
    {
        ReleaseNativeWorldCacheLease();
        Log("registrar=refused reason=cached-manifest-content-identity-mismatch expected=%s", expectedContentId.c_str());
        g_state = EState::Refused;
        return;
    }
    Log("cache=ready disposition=%s contentId=%s manifestSha256=%s directory=%s lease=pending", cacheHit ? "hit" : "published", expectedContentId.c_str(),
        g_manifest.manifestSha256.c_str(), g_activeDirectory.c_str());

    g_streaming = streaming;
    PublishNativeModelSlotReservation();
    HookInstallCall(LOAD_CD_DIRECTORY_CALL, reinterpret_cast<DWORD>(&LoadCdDirectoryHook));
    g_state = EState::Hooked;
    Log("registrar=hooked call=0x%08X pack=%s manifest=%s runtimeFiles=%s,%s", LOAD_CD_DIRECTORY_CALL, Pack().directoryPath, g_policy->runtimeManifestFileName,
        Pack().ideFileName, Pack().imgFileName);
}

SNativeWorldTransportPublishResult CNativeWorldPackManagerSA::PublishTransportOffer(const SNativeWorldTransportOffer& offer)
{
    std::lock_guard<std::mutex>        lock(g_transportPublisherMutex);
    SNativeWorldTransportPublishResult result;
    const auto                         isCancelled = [&offer]() { return offer.cancelled && offer.cancelled->load(std::memory_order_acquire); };
    if (isCancelled())
    {
        result.error = "transport publication was cancelled";
        return result;
    }
    if (g_state != EState::Off)
    {
        result.existingActivationActive = g_state == EState::Active;
        result.error = "native registrar state is not idle; transport publication cannot share its mutable descriptor";
        return result;
    }
    // V3 proves the generic multi-IMG transport and byte admission boundary
    // before the later multi-city registrar exists. Keeping this path free of
    // g_policy/g_pack state makes an accepted cache object incapable of
    // becoming native GTA input through the format-1/2 startup route.
    if (offer.format == STATIC_WORLD_V3_FORMAT)
        return offer.startupAuthorization && offer.startupAuthorization->policy == NATIVE_WORLD_STATIC_V3_SET_POLICY
                   ? PublishStaticWorldV3SetOffer(offer, isCancelled)
                   : PublishStaticWorldV3TransportOffer(offer, isCancelled);

    const auto resetTransportAuditState = [&]()
    {
        g_policy = nullptr;
        g_manifest = {};
        g_activeDirectory.clear();
        g_iplNamePointers.clear();
        g_runtimeDescriptor = {};
        g_pack = nullptr;
    };

    const SNativeWorldPackPolicySA* selectedPolicy = FindNativeWorldPackPolicy(offer.format);
    if (!selectedPolicy)
    {
        result.error = "transport offer format has no compiled static-world policy";
        return result;
    }
    const SNativeWorldPackPolicySA& policy = *selectedPolicy;
    g_policy = &policy;
    if (offer.resourceName.empty() || offer.resourceName.size() > 255 || offer.manifestRelativePath.empty())
        result.error = "transport offer identity is invalid";

    std::map<std::string, const SNativeWorldTransportFile*> offeredFiles;
    for (const SNativeWorldTransportFile& file : offer.files)
    {
        const std::filesystem::path relative(file.relativePath);
        if (!result.error.empty())
            break;
        if (file.relativePath.empty() || file.relativePath.size() > 255 || relative.has_root_path() ||
            relative.lexically_normal().generic_string() != file.relativePath || !file.declaredBytes || !offeredFiles.emplace(file.relativePath, &file).second)
        {
            result.error = "transport file identity is non-canonical or duplicated";
            break;
        }
    }

    const auto manifestOffer = offeredFiles.find(offer.manifestRelativePath);
    if (result.error.empty() && manifestOffer == offeredFiles.end())
        result.error = "transport manifest is absent from its exact three-file offer";

    std::filesystem::path manifestAbsolute;
    std::filesystem::path sourceDirectory;
    if (result.error.empty())
    {
        manifestAbsolute = std::filesystem::path(manifestOffer->second->absolutePath).lexically_normal();
        sourceDirectory = manifestAbsolute.parent_path();
        if (manifestAbsolute.filename().generic_string() != policy.runtimeManifestFileName || sourceDirectory.empty())
            result.error = "transport manifest filename differs from the closed policy";
    }

    if (result.error.empty())
    {
        g_activeDirectory = sourceDirectory.string();
        const SString manifestPath = manifestAbsolute.string().c_str();
        if (!IsNativePathSafe(manifestPath) || !IsSafeRegularFile(manifestPath) || !LoadRuntimeManifest(manifestPath, result.error))
        {
            if (result.error.empty())
                result.error = "transport manifest is not a safe regular file";
        }
    }

    const SNativeWorldTransportFile* ideOffer = nullptr;
    const SNativeWorldTransportFile* imgOffer = nullptr;
    if (result.error.empty())
    {
        const std::string prefix = std::filesystem::path(offer.manifestRelativePath).parent_path().generic_string();
        const auto        composeRelative = [&prefix](const std::string& leaf) { return prefix.empty() ? leaf : prefix + "/" + leaf; };
        const auto        ideFound = offeredFiles.find(composeRelative(g_manifest.ideFileName));
        const auto        imgFound = offeredFiles.find(composeRelative(g_manifest.imgFileName));
        if (ideFound == offeredFiles.end() || imgFound == offeredFiles.end() || ideFound == imgFound || ideFound == manifestOffer || imgFound == manifestOffer)
            result.error = "manifest payload names do not bind the exact three declared transport files";
        else
        {
            ideOffer = ideFound->second;
            imgOffer = imgFound->second;
            if (manifestOffer->second->declaredBytes != g_manifest.manifestBytes || ideOffer->declaredBytes != g_manifest.ideBytes ||
                imgOffer->declaredBytes != g_manifest.imgBytes ||
                std::filesystem::path(ideOffer->absolutePath).lexically_normal().parent_path() != sourceDirectory ||
                std::filesystem::path(imgOffer->absolutePath).lexically_normal().parent_path() != sourceDirectory)
            {
                result.error = "transport byte lengths or source directories differ from the parsed manifest";
            }
        }
    }

    SIdePlan    ide;
    std::string idePathString;
    std::string imgPathString;
    if (result.error.empty())
    {
        idePathString = std::filesystem::path(ideOffer->absolutePath).lexically_normal().string();
        imgPathString = std::filesystem::path(imgOffer->absolutePath).lexically_normal().string();
        const SString idePath = idePathString.c_str();
        const SString imgPath = imgPathString.c_str();
        const SString ideHash = SharedUtil::GenerateSha256HexStringFromFile(idePath).ToLower();
        const SString imgHash = SharedUtil::GenerateSha256HexStringFromFile(imgPath).ToLower();
        if (!IsNativePathSafe(idePath) || !IsNativePathSafe(imgPath) || !IsSafeRegularFile(idePath) || !IsSafeRegularFile(imgPath) ||
            !HasExactFileSize(idePath, g_manifest.ideBytes) || !HasExactFileSize(imgPath, g_manifest.imgBytes) || ideHash != g_manifest.ideSha256.c_str() ||
            imgHash != g_manifest.imgSha256.c_str())
        {
            result.error = "transport payload size, hash, or regular-file identity differs from its manifest";
        }
        else if (!ParseIde(idePath, ide, result.error) || isCancelled() || !ValidateImg(imgPath, ide, result.error) || isCancelled() ||
                 !ValidateBinaryIpls(imgPath, ide, result.error) || isCancelled() || !ValidatePayloads(imgPath, ide, result.error) || isCancelled() ||
                 !ValidateDescriptor(result.error))
        {
            // The closed audit functions provide the precise refusal reason.
            if (result.error.empty() && isCancelled())
                result.error = "transport publication was cancelled";
        }
    }

    if (result.error.empty())
    {
        std::ostringstream offerIdentity;
        offerIdentity << "mta-native-world-transport-offer-v1\nresource=" << offer.resourceName << "\nformat=" << static_cast<unsigned int>(offer.format)
                      << "\nmanifest=" << offer.manifestRelativePath << '\n';
        for (const auto& [relativePath, file] : offeredFiles)
            offerIdentity << "file=" << relativePath << "\nbytes=" << file->declaredBytes << '\n';
        offerIdentity << "manifest.sha256=" << g_manifest.manifestSha256 << "\nide.sha256=" << g_manifest.ideSha256 << "\nimg.sha256=" << g_manifest.imgSha256
                      << '\n';
        result.offerId = SharedUtil::GenerateSha256HexString(offerIdentity.str()).ToLower();

        SNativeWorldCacheRequestSA request;
        request.format = g_manifest.format;
        request.sourceAbsoluteDirectory = sourceDirectory.string();
        request.policyKey = policy.key;
        request.packId = g_manifest.packId;
        request.manifestFileName = policy.runtimeManifestFileName;
        request.sourceManifestSha256 = g_manifest.manifestSha256;
        request.sourceManifestBytes = g_manifest.manifestBytes;
        request.maximumManifestBytes = policy.maximumManifestBytes;
        request.ide = {g_manifest.ideFileName, g_manifest.ideSha256, g_manifest.ideBytes};
        request.img = {g_manifest.imgFileName, g_manifest.imgSha256, g_manifest.imgBytes};
        request.cancellation = offer.cancelled;
        request.contentId = GenerateNativeWorldContentId(request);
        result.contentId = request.contentId;
        result.auditProfile = policy.auditProfile;

        const auto auditQuarantine = [&request, &policy, &isCancelled](const std::string& directory, std::string& auditError)
        {
            if (isCancelled())
            {
                auditError = "transport publication was cancelled";
                return false;
            }
            g_activeDirectory = directory;
            const SString manifestPath = SString("%s\\%s", directory.c_str(), policy.runtimeManifestFileName);
            if (!IsNativePathSafe(manifestPath) || !IsSafeRegularFile(manifestPath) || !LoadRuntimeManifest(manifestPath, auditError))
            {
                if (auditError.empty())
                    auditError = "canonical quarantine manifest is not a safe regular file";
                return false;
            }

            SNativeWorldCacheRequestSA auditedIdentity = request;
            auditedIdentity.format = g_manifest.format;
            auditedIdentity.packId = g_manifest.packId;
            auditedIdentity.ide = {g_manifest.ideFileName, g_manifest.ideSha256, g_manifest.ideBytes};
            auditedIdentity.img = {g_manifest.imgFileName, g_manifest.imgSha256, g_manifest.imgBytes};
            if (GenerateNativeWorldContentId(auditedIdentity) != request.contentId)
            {
                auditError = "canonical quarantine content identity differs from the transport offer";
                return false;
            }

            const SString idePath = SString("%s\\%s", directory.c_str(), g_manifest.ideFileName.c_str());
            const SString imgPath = SString("%s\\%s", directory.c_str(), g_manifest.imgFileName.c_str());
            if (!IsNativePathSafe(idePath) || !IsNativePathSafe(imgPath) || !IsSafeRegularFile(idePath) || !IsSafeRegularFile(imgPath) ||
                !HasExactFileSize(idePath, g_manifest.ideBytes) || !HasExactFileSize(imgPath, g_manifest.imgBytes) ||
                SharedUtil::GenerateSha256HexStringFromFile(idePath).ToLower() != g_manifest.ideSha256.c_str() ||
                SharedUtil::GenerateSha256HexStringFromFile(imgPath).ToLower() != g_manifest.imgSha256.c_str())
            {
                auditError = "canonical quarantine payload identity differs from its manifest";
                return false;
            }

            SIdePlan   ide;
            const bool valid = ParseIde(idePath, ide, auditError) && !isCancelled() && ValidateImg(imgPath, ide, auditError) && !isCancelled() &&
                               ValidateBinaryIpls(imgPath, ide, auditError) && !isCancelled() && ValidatePayloads(imgPath, ide, auditError) && !isCancelled() &&
                               ValidateDescriptor(auditError);
            if (!valid && auditError.empty() && isCancelled())
                auditError = "transport publication was cancelled";
            return valid;
        };
        result.success = PublishNativeWorldCache(request, auditQuarantine, result.publishedDirectory, result.cacheHit, result.error);
    }

    resetTransportAuditState();
    return result;
}

bool CNativeWorldPackManagerSA::IsModelIdReserved(unsigned int modelId)
{
    if (!g_nativeModelSlotsReserved.load(std::memory_order_acquire))
        return false;

    const bool activePackSlot =
        modelId >= g_reservedPackModelFirst.load(std::memory_order_relaxed) && modelId <= g_reservedPackModelLast.load(std::memory_order_relaxed);
    const bool aggregateArenaSlot = modelId >= NATIVE_WORLD_MODEL_ARENA_FIRST && modelId <= NATIVE_WORLD_MODEL_ARENA_LAST;
    return activePackSlot || aggregateArenaSlot;
}

unsigned int CNativeWorldPackManagerSA::GetRequiredStreamingBufferSizeBlocks()
{
    if (g_state != EState::Active)
        return 0;

    // SetStreamingBufferSize interprets this value as the total allocation and
    // then splits it into two equal channel buffers. Each half must hold the
    // reviewed largest entry, so returning only one rounded entry here would
    // silently halve the usable per-channel capacity.
    const uint64_t perChannelBlocks = (static_cast<uint64_t>(Pack().largestImgEntryBlocks) + 1) & ~uint64_t{1};
    const uint64_t totalBlocks = perChannelBlocks * 2;
    if (totalBlocks > std::numeric_limits<unsigned int>::max())
        Fatal("reviewed streaming-buffer floor exceeds the native block-count width");
    return static_cast<unsigned int>(totalBlocks);
}

void CNativeWorldPackManagerSA::LogStreamingBufferClamp(unsigned int requestedBlocks, unsigned int effectiveBlocks, unsigned int requiredBlocks)
{
    if (g_state == EState::Active)
        Log("streamingBuffer=request-clamped requestedBlocks=%u effectiveBlocks=%u requiredBlocks=%u", requestedBlocks, effectiveBlocks, requiredBlocks);
}
