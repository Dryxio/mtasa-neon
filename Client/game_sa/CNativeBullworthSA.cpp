/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CNativeBullworthSA.cpp
 *  PURPOSE:     Opt-in registration of the native Bullworth streaming pack
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CNativeBullworthSA.h"

#include "CGameSA.h"
#include "CIplSA.h"
#include "CModelInfoSA.h"
#include "CNativeModelStoreSA.h"
#include "CPoolSAInterface.h"
#include "CStreamingSA.h"
#include "CTextureDictonarySA.h"
#include "SharedUtil.File.h"
#include "SharedUtil.Hash.h"
#include "SharedUtil.Misc.h"

#include <cstdarg>
#include <fstream>
#include <sstream>

extern CGameSA* pGame;

namespace
{
    // The largest native archive entry is bw.col at 4007 sectors. GTA splits
    // its streaming allocation into two equal halves, so the total must be
    // rounded to the next even sector count.
    constexpr unsigned int REQUIRED_STREAMING_BUFFER_BLOCKS = 4008;

    constexpr const char* FEATURE_ENVIRONMENT = "MTA_NATIVE_BW_MODEL_STORES";
    constexpr const char* PACK_DIRECTORY = "MTA\\data\\extended-world\\bullworth";
    constexpr DWORD       LOAD_CD_DIRECTORY_CALL = 0x5B8E1B;
    constexpr BYTE        LOAD_CD_DIRECTORY_CALL_BYTES[] = {0xE8, 0xA0, 0xF4, 0xFF, 0xFF};
    constexpr DWORD       LOAD_CD_DIRECTORY = 0x5B82C0;
    constexpr DWORD       LOAD_NAMED_CD_DIRECTORY = 0x5B6170;
    constexpr DWORD       LOAD_OBJECT_TYPES = 0x5B8400;
    constexpr DWORD       FIND_TXD_SLOT = 0x731850;
    constexpr DWORD       ADD_TXD_SLOT = 0x731C80;
    constexpr DWORD       FIND_IPL_SLOT = 0x404AC0;
    constexpr DWORD       ENABLE_IPL_DYNAMIC_STREAMING = 0x404D30;
    constexpr DWORD       TXD_FIND_CACHE = 0xC88014;
    constexpr DWORD       GET_UPPERCASE_KEY = 0x53CF30;
    constexpr DWORD       FATAL_EXIT_CODE = 0x4E425746;  // "NBWF"
    constexpr const char* IDE_SHA256 = "0bdf5aeb17eaefe6e2f42e47d38f82d65526c580f3eecc223b7b65f8b905eeb4";
    constexpr const char* IMG_SHA256 = "bc7f3ad5ce47bbd8a9018c9743142582cd458875d2100f31c0d96aac7f4bbfc0";

    constexpr unsigned int MODEL_FIRST = 18631;
    constexpr unsigned int MODEL_LAST = 19582;
    constexpr unsigned int MODEL_COUNT = 952;
    constexpr unsigned int TXD_COUNT = 166;
    constexpr unsigned int TXD_POOL_CAPACITY = 5000;
    constexpr unsigned int COL_STOCK_OCCUPIED = 252;
    constexpr unsigned int IPL_STOCK_OCCUPIED = 191;
    constexpr unsigned int IPL_COUNT = 7;
    constexpr unsigned int IMG_ENTRY_COUNT = 1126;
    constexpr unsigned int IMG_SECTOR_COUNT = 82786;
    constexpr unsigned int EXPECTED_ATOMIC = 13984;
    constexpr unsigned int EXPECTED_DAMAGE = 69;
    constexpr unsigned int EXPECTED_TIME = 160;
    constexpr unsigned int ADDED_ATOMIC = 870;
    constexpr unsigned int ADDED_DAMAGE = 67;
    constexpr unsigned int ADDED_TIME = 15;
    constexpr DWORD        ATOMIC_MODEL_VTABLE = 0x85BBF0;
    constexpr DWORD        DAMAGE_MODEL_VTABLE = 0x85BC30;
    constexpr DWORD        TIME_MODEL_VTABLE = 0x85BCB0;
    constexpr DWORD        FLIPPED_RECT_SENTINELS[] = {0x49742400, 0xC9742400, 0xC9742400, 0x49742400};

    constexpr const char* IPL_NAMES[IPL_COUNT] = {
        "bw_tbusines", "bw_tcarni", "bw_tglobal", "bw_tindust", "bw_tjyard", "bw_trich", "bw_tschool",
    };

    struct STxdSlotFingerprint
    {
        bool  configured;
        BYTE  poolFlag;
        DWORD dictionary;
        WORD  usages;
        WORD  parent;
        DWORD hash;
        WORD  prev;
        WORD  next;
        WORD  nextInImg;
        BYTE  streamingFlags;
        BYTE  archive;
        DWORD offset;
        DWORD size;
        DWORD loadState;
    };

    struct STxdPoolProfile
    {
        const char*         executableIdentity;
        const char*         name;
        unsigned int        occupied;
        int                 firstFree;
        int                 fingerprintSlot;
        STxdSlotFingerprint fingerprint;
    };

    constexpr STxdPoolProfile TXD_POOL_PROFILES[] = {
        {"hoodlum-raw", "standalone-3607", 3607, 3606, -1, {true}},
        {"mta-programdata",
         "mta-runtime-3608",
         3608,
         3607,
         3607,
         {true, 0x01, 0x00000000, 0, 0xFFFF, 0xEA5A8E45, 0xFFFF, 0xFFFF, 0xFFFF, 0x00, 4, 13153, 5, 0}},
    };

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
#pragma pack(pop)

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

    CStreamingSA* g_streaming = nullptr;
    EState        g_state = EState::Off;

    void Log(const char* format, ...)
    {
        char    message[2048]{};
        va_list arguments;
        va_start(arguments, format);
        _vsnprintf_s(message, sizeof(message), _TRUNCATE, format, arguments);
        va_end(arguments);
        OutputDebugStringA(message);
        OutputDebugStringA("\n");
        SharedUtil::WriteDebugEvent(message);
    }

    [[noreturn]] void Fatal(const char* reason)
    {
        // Native store and directory allocation has no complete inverse. A
        // post-commit mismatch must not continue with partially registered IDs.
        Log("[NativeBW] registrar=fatal reason=%s exit=0x%08X", reason, FATAL_EXIT_CODE);
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
        char*               end = nullptr;
        const unsigned long number = strtoul(value.c_str(), &end, 10);
        if (!end || end == value.c_str() || *end != '\0' || number > UINT_MAX)
            return false;
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

    bool ParseIde(const SString& path, SIdePlan& plan, std::string& error)
    {
        std::ifstream file(SharedUtil::FromUTF8(path));
        if (!file)
        {
            error = "bw.ide cannot be opened";
            return false;
        }

        enum class ESection
        {
            None,
            Objects,
            TimedObjects,
        } section = ESection::None;

        std::string line;
        while (std::getline(file, line))
        {
            line = Trim(line);
            if (line.empty() || line[0] == '#')
                continue;
            if (line == "objs")
            {
                section = ESection::Objects;
                continue;
            }
            if (line == "tobj")
            {
                section = ESection::TimedObjects;
                continue;
            }
            if (line == "end")
            {
                section = ESection::None;
                continue;
            }
            if (section == ESection::None)
            {
                error = "bw.ide contains an unsupported section";
                return false;
            }

            const std::vector<std::string> fields = SplitCsv(line);
            if ((section == ESection::Objects && fields.size() != 6) || (section == ESection::TimedObjects && fields.size() != 8))
            {
                error = "bw.ide contains a malformed row";
                return false;
            }

            unsigned int id = 0;
            unsigned int flags = 0;
            if (!ParseUnsigned(fields[0], id) || !ParseUnsigned(fields[5], flags) || id < MODEL_FIRST || id > MODEL_LAST || !plan.modelIds.insert(id).second ||
                fields[1].empty() || fields[2].empty())
            {
                error = "bw.ide contains an invalid or duplicate model";
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

        if (plan.modelIds.size() != MODEL_COUNT || *plan.modelIds.begin() != MODEL_FIRST || *plan.modelIds.rbegin() != MODEL_LAST ||
            plan.modelNames.size() != MODEL_COUNT || plan.txdNames.size() != TXD_COUNT || plan.atomic != ADDED_ATOMIC || plan.damage != ADDED_DAMAGE ||
            plan.time != ADDED_TIME)
        {
            error = "bw.ide counts or ID range differ from the native plan";
            return false;
        }
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
            error = "bw.img cannot be opened";
            return false;
        }

        LARGE_INTEGER fileSize{};
        SImgHeader    header{};
        DWORD         read = 0;
        const bool    validHeader = GetFileSizeEx(file, &fileSize) && ReadFile(file, &header, sizeof(header), &read, nullptr) && read == sizeof(header) &&
                                 memcmp(header.magic, "VER2", 4) == 0 && header.count == IMG_ENTRY_COUNT &&
                                 fileSize.QuadPart == static_cast<LONGLONG>(IMG_SECTOR_COUNT) * 2048;
        if (!validHeader)
        {
            CloseHandle(file);
            error = "bw.img header, count, or byte length differs from the native plan";
            return false;
        }

        std::vector<SImgEntry> entries(header.count);
        const DWORD            directoryBytes = static_cast<DWORD>(entries.size() * sizeof(SImgEntry));
        if (!ReadFile(file, entries.data(), directoryBytes, &read, nullptr) || read != directoryBytes)
        {
            CloseHandle(file);
            error = "bw.img directory is truncated";
            return false;
        }
        CloseHandle(file);

        std::set<std::string>                names;
        std::set<std::string>                dffs;
        std::set<std::string>                txds;
        std::set<std::string>                ipls;
        unsigned int                         colCount = 0;
        unsigned int                         maxEntrySize = 0;
        std::vector<std::pair<DWORD, DWORD>> ranges;
        for (const SImgEntry& entry : entries)
        {
            const std::string name = EntryName(entry);
            const size_t      dot = name.rfind('.');
            const uint64_t    endSector = static_cast<uint64_t>(entry.offset) + entry.size;
            if (name.empty() || dot == std::string::npos || !entry.size || entry.streamingSize != entry.size || entry.offset < 18 ||
                endSector > IMG_SECTOR_COUNT || !names.insert(name).second)
            {
                error = "bw.img contains an invalid, duplicate, or out-of-bounds entry";
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
            else if (extension == ".col" && name == "bw.col")
                ++colCount;
            else if (extension == ".ipl")
                ipls.insert(name.substr(0, dot));
            else
            {
                error = "bw.img contains an unexpected entry type";
                return false;
            }
        }
        std::sort(ranges.begin(), ranges.end());
        for (size_t i = 1; i < ranges.size(); ++i)
        {
            if (ranges[i].first < ranges[i - 1].second)
            {
                error = "bw.img contains overlapping entries";
                return false;
            }
        }

        std::set<std::string> expectedIpls;
        for (const char* name : IPL_NAMES)
            expectedIpls.insert(name);
        if (dffs != ide.modelNames || txds != ide.txdNames || ipls != expectedIpls || colCount != 1 || maxEntrySize != 4007)
        {
            error = "bw.img names do not match bw.ide and the seven-district plan";
            return false;
        }
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
        Log("[NativeBW] %sPool capacity=%d occupied=%u free=%u firstFree=%d highest=%d holes=%u planned=%s", name, capacity, occupied, capacity - occupied,
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
        const char*            executableIdentity = CNativeModelStoreSA::GetExecutableIdentityName();
        const STxdPoolProfile* profile = nullptr;
        for (const STxdPoolProfile& candidate : TXD_POOL_PROFILES)
            if (executableIdentity && strcmp(executableIdentity, candidate.executableIdentity) == 0)
            {
                profile = &candidate;
                break;
            }
        if (!profile)
        {
            error = "no TXD pool profile matches the executable identity";
            return false;
        }
        ide.txdProfileName = profile->name;

        if (!pool || !pool->m_pObjects || !pool->m_byteMap || pool->m_nSize != TXD_POOL_CAPACITY || pool->m_nFirstFree != profile->firstFree)
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
            Log("[NativeBW] txdProfileFingerprint profile=%s slot=%u poolFlag=0x%02X dictionary=%p usages=%u parent=%u hash=0x%08X streamPrev=%u streamNext=%u "
                "streamNextImg=%u streamFlags=0x%02X archive=%u offset=%u size=%u loadState=%u",
                profile->name, slot, poolFlag, definition->rwTexDictonary, definition->usUsagesCount, definition->usParentIndex, definition->hash,
                streaming->prevId, streaming->nextId, streaming->nextInImg, streaming->flg, streaming->archiveId, streaming->offsetInBlocks,
                streaming->sizeInBlocks, static_cast<unsigned int>(streaming->loadState));
            const STxdSlotFingerprint& expected = profile->fingerprint;
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
        if (ide.txdFree < TXD_COUNT)
        {
            error = "TXD pool does not have 166 free slots";
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
        ide.txdPlanSpanHoles = ide.txdPlanMax - ide.txdPlanMin + 1 - TXD_COUNT;

        for (const auto& [name, slot] : ide.txdSlots)
        {
            if (!StreamingInfoIsFree(pGame->GetBaseIDforTXD() + slot))
            {
                error = SString("planned Bullworth TXD streaming slot is occupied name=%s slot=%u", name.c_str(), slot);
                return false;
            }
        }

        Log("[NativeBW] txdPool profile=%s capacity=%d occupied=%u free=%u firstFree=%d highest=%d holes=%u planned=%u plannedRange=%u..%u plannedSpanHoles=%u",
            ide.txdProfileName, pool->m_nSize, ide.txdOccupied, ide.txdFree, ide.txdOriginalFirstFree, ide.txdHighestOccupied, ide.txdHoles, TXD_COUNT,
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
        if (!CNativeModelStoreSA::GetUsage(atomic, damage, time) || atomic != EXPECTED_ATOMIC || damage != EXPECTED_DAMAGE || time != EXPECTED_TIME)
        {
            error = "native model stores are not at exact stock occupancy";
            return false;
        }

        CBaseModelInfoSAInterface** models = reinterpret_cast<CBaseModelInfoSAInterface**>(ARRAY_ModelInfo);
        for (unsigned int id = MODEL_FIRST; id <= MODEL_LAST; ++id)
        {
            if (models[id] || !StreamingInfoIsFree(id))
            {
                error = "a Bullworth model ID or streaming slot is already occupied";
                return false;
            }
        }

        auto*                     txdPool = *reinterpret_cast<CPoolSAInterface<CTextureDictonarySAInterface>**>(0xC8800C);
        auto*                     colPool = *reinterpret_cast<CPoolSAInterface<SColDef>**>(0x965560);
        auto*                     iplPool = *reinterpret_cast<CPoolSAInterface<CIplSAInterface>**>(0x8E3FB0);
        std::vector<unsigned int> colSlots;
        std::vector<unsigned int> iplSlots;
        if (!BuildPoolAllocationPlan(colPool, 255, COL_STOCK_OCCUPIED, 1, "col", ide.colOriginallyOccupied, ide.colOriginalFlags, ide.colOriginalFirstFree,
                                     colSlots, error) ||
            !BuildPoolAllocationPlan(iplPool, 256, IPL_STOCK_OCCUPIED, IPL_COUNT, "ipl", ide.iplOriginallyOccupied, ide.iplOriginalFlags,
                                     ide.iplOriginalFirstFree, iplSlots, error) ||
            !BuildTxdAllocationPlan(txdPool, ide, error))
            return false;
        if (pGame->GetBaseIDforCOL() + 255 != pGame->GetBaseIDforIPL() || pGame->GetBaseIDforIPL() + 256 > pGame->GetCountOfAllFileIDs())
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
        if (nextIplSlot != IPL_COUNT || ide.iplSlots.size() != IPL_COUNT)
        {
            error = "the IPL allocation plan does not match IMG directory order";
            return false;
        }

        const auto                          findTxd = reinterpret_cast<int(__cdecl*)(const char*)>(FIND_TXD_SLOT);
        const auto                          uppercaseKey = reinterpret_cast<unsigned int(__cdecl*)(const char*)>(GET_UPPERCASE_KEY);
        std::map<unsigned int, std::string> txdKeys;
        for (const std::string& name : ide.txdNames)
        {
            const unsigned int key = uppercaseKey(name.c_str());
            const auto [existing, inserted] = txdKeys.emplace(key, name);
            if (!inserted)
            {
                error = SString("Bullworth TXD native-key collision key=0x%08X names=%s,%s", key, existing->second.c_str(), name.c_str());
                return false;
            }
            if (findTxd(name.c_str()) != -1)
            {
                error = "a Bullworth TXD name already exists";
                return false;
            }
        }
        const auto findIpl = reinterpret_cast<int(__cdecl*)(const char*)>(FIND_IPL_SLOT);
        for (const char* name : IPL_NAMES)
        {
            if (findIpl(name) != -1)
            {
                error = "a Bullworth IPL name already exists";
                return false;
            }
        }
        if (!StreamingInfoIsFree(pGame->GetBaseIDforCOL() + ide.colSlot))
        {
            error = "the planned Bullworth COL streaming slot is occupied";
            return false;
        }
        for (unsigned int i = 0; i < IPL_COUNT; ++i)
            if (!StreamingInfoIsFree(pGame->GetBaseIDforIPL() + ide.iplSlots.at(IPL_NAMES[i])))
            {
                error = "a planned Bullworth IPL streaming slot is occupied";
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
        if (!CNativeModelStoreSA::GetUsage(atomic, damage, time) || atomic != EXPECTED_ATOMIC + ADDED_ATOMIC || damage != EXPECTED_DAMAGE + ADDED_DAMAGE ||
            time != EXPECTED_TIME + ADDED_TIME)
            Fatal("model-store occupancy mismatch after IDE commit");

        CBaseModelInfoSAInterface** models = reinterpret_cast<CBaseModelInfoSAInterface**>(ARRAY_ModelInfo);
        const auto                  findTxd = reinterpret_cast<int(__cdecl*)(const char*)>(FIND_TXD_SLOT);
        for (const std::string& name : ide.txdNames)
            if (findTxd(name.c_str()) != static_cast<int>(ide.txdSlots.at(name)))
                Fatal("TXD slot postcondition mismatch after IDE commit");
        for (unsigned int id = MODEL_FIRST; id <= MODEL_LAST; ++id)
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
            Log("[NativeBW] colPost pool=invalid");
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
        Log("[NativeBW] colPost capacity=%d occupied=%u free=%u firstFree=%d highest=%d holes=%u expectedSlot=%u expectedArchive=%u", pool->m_nSize, occupied,
            static_cast<unsigned int>(pool->m_nSize) - occupied, pool->m_nFirstFree, highest, holes, expectedSlot, archiveId);

        for (int slot = 0; slot < pool->m_nSize; ++slot)
        {
            const CStreamingInfo* streaming = g_streaming->GetStreamingInfo(pGame->GetBaseIDforCOL() + slot);
            if (slot != static_cast<int>(expectedSlot) && streaming->archiveId != archiveId)
                continue;

            const SColDef* definition = pool->GetObject(slot);
            Log("[NativeBW] colPost slot=%d occupied=%u poolFlag=0x%02X rectBits=%08X,%08X,%08X,%08X firstModel=%d lastModel=%d refs=%u state=%u%u%u%u "
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
        if (!ValidatePoolAllocationPostcondition(colPool, 255, ide.colOriginallyOccupied, ide.colOriginalFlags, colSlots, "COL", poolError))
            Fatal(poolError.c_str());
        if (!ValidatePoolAllocationPostcondition(iplPool, 256, ide.iplOriginallyOccupied, ide.iplOriginalFlags, ide.iplAllocationSlots, "IPL", poolError))
            Fatal(poolError.c_str());

        const SColDef* col = colPool->GetObject(ide.colSlot);
        if (memcmp(&col->rect, FLIPPED_RECT_SENTINELS, sizeof(FLIPPED_RECT_SENTINELS)) != 0 || col->firstModel != 0x7FFF ||
            col->lastModel != static_cast<short>(0x8000) || col->refCount != 0 || col->active || col->required || col->procedural || col->interior)
            Fatal("COL slot structural postcondition mismatch");
        for (unsigned int i = 0; i < IPL_COUNT; ++i)
        {
            const unsigned int slot = ide.iplSlots.at(IPL_NAMES[i]);
            const auto*        ipl = iplPool->GetObject(slot);
            if (!iplPool->IsContains(slot) || !FixedNameEquals(ipl->name, IPL_NAMES[i]) ||
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
        for (unsigned int id = MODEL_FIRST; id <= MODEL_LAST; ++id)
            validateStreamingEntry(id, ide.modelFileNames.at(id));
        for (const std::string& name : ide.txdNames)
            validateStreamingEntry(pGame->GetBaseIDforTXD() + ide.txdSlots.at(name), name + ".txd");
        validateStreamingEntry(pGame->GetBaseIDforCOL() + ide.colSlot, "bw.col");
        for (unsigned int i = 0; i < IPL_COUNT; ++i)
            validateStreamingEntry(pGame->GetBaseIDforIPL() + ide.iplSlots.at(IPL_NAMES[i]), SString("%s.ipl", IPL_NAMES[i]));

        std::map<std::string, unsigned int> streamingIds;
        for (unsigned int id = MODEL_FIRST; id <= MODEL_LAST; ++id)
            streamingIds[ide.modelFileNames.at(id)] = id;
        for (const std::string& name : ide.txdNames)
            streamingIds[name + ".txd"] = pGame->GetBaseIDforTXD() + ide.txdSlots.at(name);
        streamingIds["bw.col"] = pGame->GetBaseIDforCOL() + ide.colSlot;
        for (unsigned int i = 0; i < IPL_COUNT; ++i)
            streamingIds[SString("%s.ipl", IPL_NAMES[i])] = pGame->GetBaseIDforIPL() + ide.iplSlots.at(IPL_NAMES[i]);

        for (size_t i = 0; i < ide.imgOrder.size(); ++i)
        {
            const CStreamingInfo* info = g_streaming->GetStreamingInfo(streamingIds.at(ide.imgOrder[i]));
            const unsigned short  expectedNext = i + 1 < ide.imgOrder.size() ? static_cast<unsigned short>(streamingIds.at(ide.imgOrder[i + 1])) : 0xFFFF;
            if (info->nextInImg != expectedNext)
                Fatal("streaming directory nextInImg chain postcondition mismatch");
        }
        if (*reinterpret_cast<const unsigned int*>(0x8E4CA8) < 4007)
            Fatal("native maximum streaming entry size was not raised");
    }

    void EnableOwnedIplDynamicStreaming(const SIdePlan& ide)
    {
        auto*      iplPool = *reinterpret_cast<CPoolSAInterface<CIplSAInterface>**>(0x8E3FB0);
        const auto enableDynamicStreaming = reinterpret_cast<void(__cdecl*)(int, bool)>(ENABLE_IPL_DYNAMIC_STREAMING);
        for (const char* name : IPL_NAMES)
        {
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
        Log("[NativeBW] iplBootstrap dynamicStreaming=enabled slots=%u,%u,%u,%u,%u,%u,%u boundingBoxes=pending-native-pass", ide.iplSlots.at(IPL_NAMES[0]),
            ide.iplSlots.at(IPL_NAMES[1]), ide.iplSlots.at(IPL_NAMES[2]), ide.iplSlots.at(IPL_NAMES[3]), ide.iplSlots.at(IPL_NAMES[4]),
            ide.iplSlots.at(IPL_NAMES[5]), ide.iplSlots.at(IPL_NAMES[6]));
    }

    void RegisterPack()
    {
        const SString idePath = SharedUtil::CalcMTASAPath(SString("%s\\bw.ide", PACK_DIRECTORY));
        const SString imgPath = SharedUtil::CalcMTASAPath(SString("%s\\bw.img", PACK_DIRECTORY));
        SIdePlan      ide;
        std::string   error;
        Log("[NativeBW] registrar=preflight ide=%s img=%s", idePath.c_str(), imgPath.c_str());
        if (!IsNativePathSafe(idePath) || !IsNativePathSafe(imgPath))
            error = "native loader paths must be ASCII and shorter than MAX_PATH";
        if (error.empty())
        {
            const SString ideHash = SharedUtil::GenerateSha256HexStringFromFile(idePath);
            const SString imgHash = SharedUtil::GenerateSha256HexStringFromFile(imgPath);
            if (_stricmp(ideHash.c_str(), IDE_SHA256) != 0 || _stricmp(imgHash.c_str(), IMG_SHA256) != 0)
                error = "runtime pack SHA-256 differs from the reviewed generated payload";
            else
                Log("[NativeBW] registrar=integrity-ok ideSha256=%s imgSha256=%s", IDE_SHA256, IMG_SHA256);
        }
        if (!error.empty() || !ParseIde(idePath, ide, error) || !ValidateImg(imgPath, ide, error) || !PreflightRuntime(ide, error))
        {
            RestoreTxdFindCache(ide);
            g_state = EState::Refused;
            Log("[NativeBW] registrar=refused reason=%s stock-world-remains-active", error.c_str());
            return;
        }

        // AddArchive is the only reversible native mutation that precedes pool
        // allocation. Keep it first so an open failure leaves every pool stock.
        const WString       wideImgPath = SharedUtil::FromUTF8(imgPath);
        const unsigned char archiveId = g_streaming->AddArchive(wideImgPath.c_str());
        if (archiveId == INVALID_ARCHIVE_ID)
        {
            RestoreTxdFindCache(ide);
            g_state = EState::Refused;
            Log("[NativeBW] registrar=refused reason=AddArchive-failed-before-pool-mutation stock-world-remains-active");
            return;
        }
        if (archiveId != 6)
        {
            g_streaming->RemoveArchive(archiveId);
            RestoreTxdFindCache(ide);
            g_state = EState::Refused;
            Log("[NativeBW] registrar=refused reason=unexpected-archive-id expected=6 actual=%u rollback=complete", archiveId);
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
                g_state = EState::Refused;
                Log("[NativeBW] registrar=refused reason=TXD-allocation-plan-mismatch name=%s expected=%u actual=%d rollback=complete restoredFirstFree=%d",
                    name.c_str(), expected, allocated, ide.txdOriginalFirstFree);
                return;
            }
        }
        const unsigned int expectedFinalCursor = ide.txdSlots.at(*ide.txdNames.rbegin());
        if (txdPool->m_nFirstFree != static_cast<int>(expectedFinalCursor))
        {
            const int actualCursor = txdPool->m_nFirstFree;
            rollbackTxdAllocations();
            g_state = EState::Refused;
            Log("[NativeBW] registrar=refused reason=TXD-allocation-cursor-mismatch expected=%u actual=%d rollback=complete restoredFirstFree=%d",
                expectedFinalCursor, actualCursor, ide.txdOriginalFirstFree);
            return;
        }

        // LoadObjectTypes is the irreversible commit point: it constructs model
        // store entries and binds them to the preallocated deterministic TXDs.
        reinterpret_cast<void(__cdecl*)(const char*)>(LOAD_OBJECT_TYPES)(idePath.c_str());
        ValidateIdePostconditions(ide);
        reinterpret_cast<void(__cdecl*)(const char*, int32_t)>(LOAD_NAMED_CD_DIRECTORY)(imgPath.c_str(), archiveId);

        ValidatePostconditions(ide, archiveId);
        EnableOwnedIplDynamicStreaming(ide);
        g_state = EState::Active;
        std::ostringstream iplSlots;
        for (unsigned int index = 0; index < IPL_COUNT; ++index)
        {
            if (index)
                iplSlots << ',';
            iplSlots << ide.iplSlots.at(IPL_NAMES[index]);
        }
        Log("[NativeBW] registrar=active archive=%u models=%u txds=%u txdSlots=%u..%u txdSpanHoles=%u colSlot=%u iplSlots=%s entries=%u lodLinks=none",
            archiveId, MODEL_COUNT, TXD_COUNT, ide.txdPlanMin, ide.txdPlanMax, ide.txdPlanSpanHoles, ide.colSlot, iplSlots.str().c_str(), IMG_ENTRY_COUNT);
    }

    void __cdecl LoadCdDirectoryHook()
    {
        reinterpret_cast<void(__cdecl*)()>(LOAD_CD_DIRECTORY)();
        if (g_state == EState::Hooked)
            RegisterPack();
    }
}  // namespace

void CNativeBullworthSA::InstallFromEnvironment(CStreamingSA* streaming)
{
    char  value[8]{};
    DWORD valueLength = GetEnvironmentVariableA(FEATURE_ENVIRONMENT, value, sizeof(value));
    if (valueLength != 1 || value[0] != '1')
        return;
    if (!CNativeModelStoreSA::IsInstalled() || !streaming)
    {
        Log("[NativeBW] registrar=refused reason=native-model-store-foundation-inactive");
        g_state = EState::Refused;
        return;
    }
    if (g_state != EState::Off)
    {
        Log("[NativeBW] registrar=unchanged state=%d", static_cast<int>(g_state));
        return;
    }
    if (memcmp(reinterpret_cast<const void*>(LOAD_CD_DIRECTORY_CALL), LOAD_CD_DIRECTORY_CALL_BYTES, sizeof(LOAD_CD_DIRECTORY_CALL_BYTES)) != 0)
    {
        Log("[NativeBW] registrar=refused reason=LoadCdDirectory-call-signature-mismatch");
        g_state = EState::Refused;
        return;
    }

    g_streaming = streaming;
    HookInstallCall(LOAD_CD_DIRECTORY_CALL, reinterpret_cast<DWORD>(&LoadCdDirectoryHook));
    g_state = EState::Hooked;
    Log("[NativeBW] registrar=hooked call=0x%08X pack=%s runtimeFiles=bw.ide,bw.img", LOAD_CD_DIRECTORY_CALL, PACK_DIRECTORY);
}

unsigned int CNativeBullworthSA::GetRequiredStreamingBufferSizeBlocks()
{
    return g_state == EState::Active ? REQUIRED_STREAMING_BUFFER_BLOCKS : 0;
}
