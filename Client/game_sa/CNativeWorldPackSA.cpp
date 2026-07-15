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

    CStreamingSA*                       g_streaming = nullptr;
    const SNativeWorldPackDescriptorSA* g_pack = nullptr;
    EState                              g_state = EState::Off;

    const SNativeWorldPackDescriptorSA& Pack()
    {
        assert(g_pack);
        return *g_pack;
    }

    const SNativeWorldPackDescriptorSA* SelectEnabledPack()
    {
        // Phase 1 intentionally has one reviewed descriptor. Keeping selection
        // here makes adding another immutable descriptor independent of the
        // registrar implementation; aggregate capacity planning comes later.
        const SNativeWorldPackDescriptorSA* available[] = {&GetNativeBullworthPackDescriptor()};
        const SNativeWorldPackDescriptorSA* selected = nullptr;
        for (const SNativeWorldPackDescriptorSA* candidate : available)
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
        const SString message("%s %s", Pack().logPrefix, detail);
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

    bool ValidateDescriptor(std::string& error)
    {
        const SNativeWorldPackDescriptorSA& pack = Pack();
        if (!pack.key || !pack.displayName || !pack.logPrefix || !pack.featureEnvironment || !pack.relativeDirectory || !pack.ideFileName ||
            !pack.imgFileName || !pack.colFileName || !pack.ideSha256 || !pack.imgSha256 || !pack.iplNames || !pack.iplCount || !pack.txdPoolProfiles ||
            !pack.txdPoolProfileCount)
        {
            error = "native world-pack descriptor has a missing required field";
            return false;
        }
        if (pack.modelFirst > pack.modelLast || pack.modelLast - pack.modelFirst + 1 != pack.modelCount ||
            pack.addedModelStores.atomic + pack.addedModelStores.damageAtomic + pack.addedModelStores.time != pack.modelCount)
        {
            error = "native world-pack descriptor model range or store deltas are inconsistent";
            return false;
        }
        if (pack.txdCount > pack.txdPoolCapacity || pack.stockColOccupied >= pack.colPoolCapacity ||
            pack.stockIplOccupied + pack.iplCount > pack.iplPoolCapacity || !pack.imgEntryCount || !pack.imgSectorCount || !pack.largestImgEntryBlocks ||
            pack.largestImgEntryBlocks > pack.imgSectorCount || strlen(pack.ideSha256) != 64 || strlen(pack.imgSha256) != 64)
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
            if (!ParseUnsigned(fields[0], id) || !ParseUnsigned(fields[5], flags) || id < Pack().modelFirst || id > Pack().modelLast ||
                !plan.modelIds.insert(id).second || fields[1].empty() || fields[2].empty())
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

        if (plan.modelIds.size() != Pack().modelCount || *plan.modelIds.begin() != Pack().modelFirst || *plan.modelIds.rbegin() != Pack().modelLast ||
            plan.modelNames.size() != Pack().modelCount || plan.txdNames.size() != Pack().txdCount || plan.atomic != Pack().addedModelStores.atomic ||
            plan.damage != Pack().addedModelStores.damageAtomic || plan.time != Pack().addedModelStores.time)
        {
            error = SString("%s counts or ID range differ from the native plan", Pack().ideFileName);
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
            error = SString("%s cannot be opened", Pack().imgFileName);
            return false;
        }

        LARGE_INTEGER fileSize{};
        SImgHeader    header{};
        DWORD         read = 0;
        const bool    validHeader = GetFileSizeEx(file, &fileSize) && ReadFile(file, &header, sizeof(header), &read, nullptr) && read == sizeof(header) &&
                                 memcmp(header.magic, "VER2", 4) == 0 && header.count == Pack().imgEntryCount &&
                                 fileSize.QuadPart == static_cast<LONGLONG>(Pack().imgSectorCount) * 2048;
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
        unsigned int                         colCount = 0;
        unsigned int                         maxEntrySize = 0;
        std::vector<std::pair<DWORD, DWORD>> ranges;
        for (const SImgEntry& entry : entries)
        {
            const std::string name = EntryName(entry);
            const size_t      dot = name.rfind('.');
            const uint64_t    endSector = static_cast<uint64_t>(entry.offset) + entry.size;
            if (name.empty() || dot == std::string::npos || !entry.size || entry.streamingSize != entry.size || entry.offset < 18 ||
                endSector > Pack().imgSectorCount || !names.insert(name).second)
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
            else if (extension == ".col" && name == Pack().colFileName)
                ++colCount;
            else if (extension == ".ipl")
                ipls.insert(name.substr(0, dot));
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

        std::set<std::string> expectedIpls;
        for (unsigned int index = 0; index < Pack().iplCount; ++index)
            expectedIpls.insert(Pack().iplNames[index]);
        if (dffs != ide.modelNames || txds != ide.txdNames || ipls != expectedIpls || colCount != 1 || maxEntrySize != Pack().largestImgEntryBlocks)
        {
            error = SString("%s names do not match %s and descriptor %s", Pack().imgFileName, Pack().ideFileName, Pack().key);
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

        CBaseModelInfoSAInterface** models = reinterpret_cast<CBaseModelInfoSAInterface**>(ARRAY_ModelInfo);
        for (unsigned int id = Pack().modelFirst; id <= Pack().modelLast; ++id)
        {
            if (models[id] || !StreamingInfoIsFree(id))
            {
                error = SString("a %s model ID or streaming slot is already occupied", Pack().displayName);
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
        const auto                          uppercaseKey = reinterpret_cast<unsigned int(__cdecl*)(const char*)>(GET_UPPERCASE_KEY);
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
        const SString idePath = SharedUtil::CalcMTASAPath(SString("%s\\%s", Pack().relativeDirectory, Pack().ideFileName));
        const SString imgPath = SharedUtil::CalcMTASAPath(SString("%s\\%s", Pack().relativeDirectory, Pack().imgFileName));
        SIdePlan      ide;
        std::string   error;
        Log("registrar=preflight ide=%s img=%s", idePath.c_str(), imgPath.c_str());
        if (!IsNativePathSafe(idePath) || !IsNativePathSafe(imgPath))
            error = "native loader paths must be ASCII and shorter than MAX_PATH";
        if (error.empty())
        {
            const SString ideHash = SharedUtil::GenerateSha256HexStringFromFile(idePath);
            const SString imgHash = SharedUtil::GenerateSha256HexStringFromFile(imgPath);
            if (_stricmp(ideHash.c_str(), Pack().ideSha256) != 0 || _stricmp(imgHash.c_str(), Pack().imgSha256) != 0)
                error = "runtime pack SHA-256 differs from the reviewed generated payload";
            else
                Log("registrar=integrity-ok ideSha256=%s imgSha256=%s", Pack().ideSha256, Pack().imgSha256);
        }
        if (!error.empty() || !ParseIde(idePath, ide, error) || !ValidateImg(imgPath, ide, error) || !PreflightRuntime(ide, error))
        {
            RestoreTxdFindCache(ide);
            g_state = EState::Refused;
            Log("registrar=refused reason=%s stock-world-remains-active", error.c_str());
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
            Log("registrar=refused reason=AddArchive-failed-before-pool-mutation stock-world-remains-active");
            return;
        }
        if (archiveId != Pack().expectedArchiveId)
        {
            g_streaming->RemoveArchive(archiveId);
            RestoreTxdFindCache(ide);
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
            g_state = EState::Refused;
            Log("registrar=refused reason=TXD-allocation-cursor-mismatch expected=%u actual=%d rollback=complete restoredFirstFree=%d", expectedFinalCursor,
                actualCursor, ide.txdOriginalFirstFree);
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
    const SNativeWorldPackDescriptorSA* selected = SelectEnabledPack();
    if (!selected)
        return;
    g_pack = selected;
    std::string descriptorError;
    if (!ValidateDescriptor(descriptorError))
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
    if (g_state != EState::Off)
    {
        Log("registrar=unchanged state=%d", static_cast<int>(g_state));
        return;
    }
    if (memcmp(reinterpret_cast<const void*>(LOAD_CD_DIRECTORY_CALL), LOAD_CD_DIRECTORY_CALL_BYTES, sizeof(LOAD_CD_DIRECTORY_CALL_BYTES)) != 0)
    {
        Log("registrar=refused reason=LoadCdDirectory-call-signature-mismatch");
        g_state = EState::Refused;
        return;
    }

    g_streaming = streaming;
    HookInstallCall(LOAD_CD_DIRECTORY_CALL, reinterpret_cast<DWORD>(&LoadCdDirectoryHook));
    g_state = EState::Hooked;
    Log("registrar=hooked call=0x%08X pack=%s runtimeFiles=%s,%s", LOAD_CD_DIRECTORY_CALL, Pack().relativeDirectory, Pack().ideFileName, Pack().imgFileName);
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
