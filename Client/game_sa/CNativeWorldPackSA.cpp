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
#include "CIplSA.h"
#include "CModelInfoSA.h"
#include "CNativeModelStoreSA.h"
#include "CNativeWorldCacheSA.h"
#include "CNativeWorldPayloadValidatorSA.h"
#include "CPoolSAInterface.h"
#include "CStreamingSA.h"
#include "CTextureDictonarySA.h"
#include "SharedUtil.File.h"
#include "SharedUtil.Hash.h"
#include "SharedUtil.Misc.h"

#include <cstdarg>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>

extern CGameSA* pGame;

namespace
{
    constexpr DWORD LOAD_CD_DIRECTORY_CALL = 0x5B8E1B;
    constexpr BYTE  LOAD_CD_DIRECTORY_CALL_BYTES[] = {0xE8, 0xA0, 0xF4, 0xFF, 0xFF};
    constexpr DWORD LOAD_CD_DIRECTORY = 0x5B82C0;
    constexpr DWORD LOAD_NAMED_CD_DIRECTORY = 0x5B6170;
    constexpr DWORD LOAD_OBJECT_TYPES = 0x5B8400;
    constexpr DWORD FIND_TXD_SLOT = 0x731850;
    constexpr DWORD ADD_TXD_SLOT = 0x731C80;
    constexpr DWORD FIND_IPL_SLOT = 0x404AC0;
    constexpr DWORD ENABLE_IPL_DYNAMIC_STREAMING = 0x404D30;
    constexpr DWORD TXD_FIND_CACHE = 0xC88014;
    constexpr DWORD GET_UPPERCASE_KEY = 0x53CF30;
    constexpr DWORD FATAL_EXIT_CODE = 0x4E425746;  // "NBWF"
    constexpr DWORD ATOMIC_MODEL_VTABLE = 0x85BBF0;
    constexpr DWORD DAMAGE_MODEL_VTABLE = 0x85BC30;
    constexpr DWORD TIME_MODEL_VTABLE = 0x85BCB0;
    constexpr DWORD FLIPPED_RECT_SENTINELS[] = {0x49742400, 0xC9742400, 0xC9742400, 0x49742400};
    constexpr float MIN_STATIC_WORLD_XY = -10000.0f;
    constexpr float MAX_STATIC_WORLD_XY = 9999.0f;
    constexpr float MAX_STATIC_WORLD_Z = 5000.0f;

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
#pragma pack(pop)
    static_assert(sizeof(SBinaryIplHeader) == 0x4C, "Unexpected binary IPL header size");
    static_assert(sizeof(SBinaryIplInstance) == 0x28, "Unexpected binary IPL instance size");

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

    const SNativeWorldPackDescriptorSA& Pack()
    {
        assert(g_pack);
        return *g_pack;
    }

    const SNativeWorldPackPolicySA* SelectEnabledPolicy()
    {
        // Activation path and native process policy stay compiled even though
        // the payload inventory now comes from a runtime manifest.
        const SNativeWorldPackPolicySA* available[] = {&GetNativeBullworthPackPolicy()};
        const SNativeWorldPackPolicySA* selected = nullptr;
        for (const SNativeWorldPackPolicySA* candidate : available)
        {
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

    bool IsLowerSha256(const std::string& value)
    {
        return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char character)
                                                 { return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'); });
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
        if (!HasExactKeys(root, {"format", "pack_id", "files"}))
        {
            error = "runtime manifest root schema differs from format 1";
            return false;
        }
        const SJsonValue* format = Member(root, "format", SJsonValue::EType::Unsigned);
        const SJsonValue* packId = Member(root, "pack_id", SJsonValue::EType::String);
        const SJsonValue* files = Member(root, "files", SJsonValue::EType::Object);
        if (!format || !packId || !files || format->number != 1 || packId->string != g_policy->key || !HasExactKeys(*files, {"ide", "img"}))
        {
            error = "runtime manifest format, pack ID, or nested schema is invalid";
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
            g_manifest.packId.c_str(),
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
                !ParseUnsigned(fields[5], flags) || flags > 0x00FFFFFF || !timedFieldsValid || id > g_policy->maximumModelId ||
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
        // the closed Bullworth profile before AddArchive or any pool mutation.
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

        CBaseModelInfoSAInterface** models = reinterpret_cast<CBaseModelInfoSAInterface**>(ARRAY_ModelInfo);
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

        CBaseModelInfoSAInterface** models = reinterpret_cast<CBaseModelInfoSAInterface**>(ARRAY_ModelInfo);
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
            ReleaseNativeWorldCacheLease();
            g_state = EState::Refused;
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
            ReleaseNativeWorldCacheLease();
            g_state = EState::Refused;
            Log("registrar=refused reason=AddArchive-failed-before-pool-mutation stock-world-remains-active");
            return;
        }
        if (archiveId != Pack().expectedArchiveId)
        {
            g_streaming->RemoveArchive(archiveId);
            RestoreTxdFindCache(ide);
            ReleaseNativeWorldCacheLease();
            g_state = EState::Refused;
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
                ReleaseNativeWorldCacheLease();
                g_state = EState::Refused;
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
            ReleaseNativeWorldCacheLease();
            g_state = EState::Refused;
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
        EnableOwnedIplDynamicStreaming(ide);
        CommitNativeWorldCacheLease();
        g_state = EState::Active;
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
}  // namespace

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
    HookInstallCall(LOAD_CD_DIRECTORY_CALL, reinterpret_cast<DWORD>(&LoadCdDirectoryHook));
    g_state = EState::Hooked;
    Log("registrar=hooked call=0x%08X pack=%s manifest=%s runtimeFiles=%s,%s", LOAD_CD_DIRECTORY_CALL, Pack().directoryPath, g_policy->runtimeManifestFileName,
        Pack().ideFileName, Pack().imgFileName);
}

unsigned int CNativeWorldPackManagerSA::GetRequiredStreamingBufferSizeBlocks()
{
    if (g_state != EState::Active)
        return 0;

    // GTA splits the allocation into two equal halves. The descriptor owns the
    // reviewed maximum entry; derive the process floor instead of duplicating
    // an independently editable rounded constant.
    return (Pack().largestImgEntryBlocks + 1) & ~1U;
}

void CNativeWorldPackManagerSA::LogStreamingBufferClamp(unsigned int requestedBlocks, unsigned int effectiveBlocks, unsigned int requiredBlocks)
{
    if (g_state == EState::Active)
        Log("streamingBuffer=request-clamped requestedBlocks=%u effectiveBlocks=%u requiredBlocks=%u", requestedBlocks, effectiveBlocks, requiredBlocks);
}
