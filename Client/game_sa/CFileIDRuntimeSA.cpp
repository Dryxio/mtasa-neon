/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CFileIDRuntimeSA.cpp
 *  PURPOSE:     Validated process-lifetime GTA SA FileID relocation
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CFileIDRuntimeSA.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <vector>

namespace
{
    constexpr DWORD EXPECTED_IMAGE_BASE = 0x00400000;
    constexpr DWORD STOCK_STREAMING_INFO_COUNT = 26316;
    constexpr DWORD STOCK_MODEL_INFO_COUNT = 20000;
    constexpr DWORD TARGET_MODEL_INFO_COUNT = 32000;
    constexpr DWORD TARGET_STREAMING_INFO_COUNT = 42341;

    constexpr SFileIDLayout STOCK_LAYOUT = {0, 20000, 25000, 25255, 25511, 25575, 25755, 26230, 26312, 26314, 26316};
    constexpr SFileIDLayout TARGET_LAYOUT = {0, 32000, 40000, 40512, 41536, 41600, 41780, 42255, 42337, 42339, 42341};

    static_assert(sizeof(SFileIDLayout) == 11 * sizeof(std::uint32_t));
    static_assert(TARGET_LAYOUT.txd - TARGET_LAYOUT.dff == 32000);
    static_assert(TARGET_LAYOUT.col - TARGET_LAYOUT.txd == 8000);
    static_assert(TARGET_LAYOUT.ipl - TARGET_LAYOUT.col == 512);
    static_assert(TARGET_LAYOUT.dat - TARGET_LAYOUT.ipl == 1024);
    static_assert(TARGET_LAYOUT.ifp - TARGET_LAYOUT.dat == 64);
    static_assert(TARGET_LAYOUT.rrr - TARGET_LAYOUT.ifp == 180);
    static_assert(TARGET_LAYOUT.scm - TARGET_LAYOUT.rrr == 475);
    static_assert(TARGET_LAYOUT.loadedList - TARGET_LAYOUT.scm == 82);
    static_assert(TARGET_LAYOUT.total - TARGET_LAYOUT.requestedList == 2);
    static_assert(TARGET_LAYOUT.total <= std::numeric_limits<std::uint16_t>::max());
    static_assert(TARGET_LAYOUT.txd <= static_cast<std::uint32_t>(std::numeric_limits<std::int16_t>::max()) + 1);

    enum class EAnchor
    {
        TxdBase,
        ColBase,
        IplBase,
        DatBase,
        IfpBase,
        RrrBase,
        ScmBase,
        StreamingBegin,
        StreamingEnd,
        ModelInfoBegin,
    };

    struct SAnchor
    {
        EAnchor              kind;
        const char*          name;
        DWORD                instructionAddress;
        BYTE                 operandOffset;
        DWORD                stockValue;
        BYTE                 instructionSize;
        std::array<BYTE, 10> expected;
    };

    constexpr SAnchor ANCHORS[] = {
#define NATIVE_FILE_ID_ANCHOR(kind, address, operand, stock, size, ...) {EAnchor::kind, #kind, address, operand, stock, size, {__VA_ARGS__}},
#include "CFileIDRuntimeSA.Manifest.inc"
#undef NATIVE_FILE_ID_ANCHOR
    };

    enum class ERelocationPatchKind : BYTE
    {
        ModelPointer,
        StreamingPointer,
        Value32,
        Movzx,
        Value16,
        RedirectNextOnCd,
        RedirectSave,
        RedirectLoad,
    };

    struct SRelocationPatch
    {
        ERelocationPatchKind kind;
        DWORD                address;
        DWORD                expected;
        DWORD                replacement;
        std::array<BYTE, 5>  expectedBytes;
    };

    constexpr SRelocationPatch RELOCATION_PATCHES[] = {
#define NATIVE_FILE_ID_POINTER(kind, address, expected, offset) {ERelocationPatchKind::kind##Pointer, address, expected, offset, {}},
#define NATIVE_FILE_ID_VALUE(address, expected, replacement)    {ERelocationPatchKind::Value32, address, expected, replacement, {}},
#define NATIVE_FILE_ID_MOVZX(address)                           {ERelocationPatchKind::Movzx, address, 0x0000BF0F, 0x0000B70F, {}},
#define NATIVE_FILE_ID_UINT16(address, expected, replacement)   {ERelocationPatchKind::Value16, address, expected, replacement, {}},
#define NATIVE_FILE_ID_REDIRECT(kind, address, ...)             {ERelocationPatchKind::Redirect##kind, address, 0, 0, {__VA_ARGS__}},
#include "CFileIDRelocationSA.Manifest.inc"
#undef NATIVE_FILE_ID_POINTER
#undef NATIVE_FILE_ID_VALUE
#undef NATIVE_FILE_ID_MOVZX
#undef NATIVE_FILE_ID_UINT16
#undef NATIVE_FILE_ID_REDIRECT
    };
    static_assert(std::size(RELOCATION_PATCHES) == 1276);

    struct SStockPartition
    {
        DWORD stockBase;
        DWORD targetBase;
        DWORD count;
    };

    constexpr SStockPartition STOCK_PARTITIONS[] = {
        {STOCK_LAYOUT.dff, TARGET_LAYOUT.dff, STOCK_LAYOUT.txd - STOCK_LAYOUT.dff},
        {STOCK_LAYOUT.txd, TARGET_LAYOUT.txd, STOCK_LAYOUT.col - STOCK_LAYOUT.txd},
        {STOCK_LAYOUT.col, TARGET_LAYOUT.col, STOCK_LAYOUT.ipl - STOCK_LAYOUT.col},
        {STOCK_LAYOUT.ipl, TARGET_LAYOUT.ipl, STOCK_LAYOUT.dat - STOCK_LAYOUT.ipl},
        {STOCK_LAYOUT.dat, TARGET_LAYOUT.dat, STOCK_LAYOUT.ifp - STOCK_LAYOUT.dat},
        {STOCK_LAYOUT.ifp, TARGET_LAYOUT.ifp, STOCK_LAYOUT.rrr - STOCK_LAYOUT.ifp},
        {STOCK_LAYOUT.rrr, TARGET_LAYOUT.rrr, STOCK_LAYOUT.scm - STOCK_LAYOUT.rrr},
        {STOCK_LAYOUT.scm, TARGET_LAYOUT.scm, STOCK_LAYOUT.loadedList - STOCK_LAYOUT.scm},
        {STOCK_LAYOUT.loadedList, TARGET_LAYOUT.loadedList, STOCK_LAYOUT.total - STOCK_LAYOUT.loadedList},
    };

    constexpr DWORD STOCK_SAVED_FILE_COUNT = 26316;
    static_assert(STOCK_SAVED_FILE_COUNT == STOCK_LAYOUT.total);

    CStreamingInfo* g_relocatedStreamingInfo{};
    void**          g_relocatedModelInfo{};

    bool IsReadable(const void* pointer, size_t size)
    {
        if (!pointer || !size)
            return false;

        uintptr_t current = reinterpret_cast<uintptr_t>(pointer);
        if (current > std::numeric_limits<uintptr_t>::max() - size)
            return false;
        const uintptr_t end = current + size;

        while (current < end)
        {
            MEMORY_BASIC_INFORMATION memory{};
            if (VirtualQuery(reinterpret_cast<const void*>(current), &memory, sizeof(memory)) != sizeof(memory) || memory.State != MEM_COMMIT ||
                memory.Protect & (PAGE_GUARD | PAGE_NOACCESS))
                return false;

            const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(memory.BaseAddress) + memory.RegionSize;
            if (regionEnd <= current)
                return false;
            current = regionEnd;
        }
        return true;
    }

    bool IsInImage(DWORD address, size_t size, DWORD imageSize)
    {
        if (address < EXPECTED_IMAGE_BASE || size > imageSize)
            return false;
        return address - EXPECTED_IMAGE_BASE <= imageSize - size;
    }

    bool ReadMemory(DWORD address, void* output, size_t size)
    {
        if (!IsReadable(reinterpret_cast<const void*>(address), size))
            return false;
        std::memcpy(output, reinterpret_cast<const void*>(address), size);
        return true;
    }

    bool WriteMemory(DWORD address, const void* bytes, size_t size)
    {
        DWORD oldProtection{};
        if (!VirtualProtect(reinterpret_cast<void*>(address), size, PAGE_EXECUTE_READWRITE, &oldProtection))
            return false;
        std::memcpy(reinterpret_cast<void*>(address), bytes, size);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), size);
        DWORD ignored{};
        return VirtualProtect(reinterpret_cast<void*>(address), size, oldProtection, &ignored) != FALSE;
    }

    DWORD ReadAnchor(EAnchor kind)
    {
        for (const SAnchor& anchor : ANCHORS)
        {
            if (anchor.kind == kind)
                return *reinterpret_cast<const DWORD*>(anchor.instructionAddress + anchor.operandOffset);
        }
        return 0;
    }

    size_t GetPatchSize(ERelocationPatchKind kind)
    {
        switch (kind)
        {
            case ERelocationPatchKind::ModelPointer:
            case ERelocationPatchKind::StreamingPointer:
            case ERelocationPatchKind::Value32:
                return sizeof(DWORD);
            case ERelocationPatchKind::Movzx:
            case ERelocationPatchKind::Value16:
                return sizeof(WORD);
            case ERelocationPatchKind::RedirectSave:
            case ERelocationPatchKind::RedirectLoad:
            case ERelocationPatchKind::RedirectNextOnCd:
                return 5;
        }
        return 0;
    }

    bool ValidateRelocationManifest(DWORD imageSize, std::string& error)
    {
        struct SRange
        {
            DWORD  begin;
            DWORD  end;
            size_t index;
        };

        std::vector<SRange> ranges;
        ranges.reserve(std::size(RELOCATION_PATCHES));
        for (size_t index = 0; index < std::size(RELOCATION_PATCHES); ++index)
        {
            const SRelocationPatch& patch = RELOCATION_PATCHES[index];
            const size_t            size = GetPatchSize(patch.kind);
            if (!size || !IsInImage(patch.address, size, imageSize))
            {
                error = SString("FileID relocation patch %u is outside the executable image", static_cast<unsigned int>(index));
                return false;
            }

            if (patch.kind == ERelocationPatchKind::ModelPointer && patch.replacement > TARGET_MODEL_INFO_COUNT * sizeof(void*))
            {
                error = SString("FileID model-pointer displacement is invalid at 0x%08X", patch.address);
                return false;
            }
            if (patch.kind == ERelocationPatchKind::StreamingPointer && patch.replacement > (TARGET_STREAMING_INFO_COUNT + 1) * sizeof(CStreamingInfo))
            {
                error = SString("FileID streaming-pointer displacement is invalid at 0x%08X", patch.address);
                return false;
            }

            std::array<BYTE, 5> current{};
            if (!ReadMemory(patch.address, current.data(), size))
            {
                error = SString("FileID relocation patch is unreadable at 0x%08X", patch.address);
                return false;
            }
            if (patch.kind == ERelocationPatchKind::RedirectNextOnCd || patch.kind == ERelocationPatchKind::RedirectSave ||
                patch.kind == ERelocationPatchKind::RedirectLoad)
            {
                if (!std::equal(current.begin(), current.begin() + size, patch.expectedBytes.begin()))
                {
                    error = SString("FileID relocation hook failed byte validation at 0x%08X", patch.address);
                    return false;
                }
            }
            else if (std::memcmp(current.data(), &patch.expected, size) != 0)
            {
                error = SString("FileID relocation operand failed validation at 0x%08X", patch.address);
                return false;
            }
            ranges.push_back({patch.address, patch.address + static_cast<DWORD>(size), index});
        }

        std::sort(ranges.begin(), ranges.end(), [](const SRange& left, const SRange& right) { return left.begin < right.begin; });
        for (size_t index = 1; index < ranges.size(); ++index)
        {
            if (ranges[index].begin < ranges[index - 1].end)
            {
                error = SString("FileID relocation patches %u and %u overlap", static_cast<unsigned int>(ranges[index - 1].index),
                                static_cast<unsigned int>(ranges[index].index));
                return false;
            }
        }
        return true;
    }

    using BufferTransfer = bool(__cdecl*)(void*, int);

    constexpr DWORD CONTINUE_NEXT_MODEL_ON_CD = 0x0040CD19;
    constexpr DWORD END_NEXT_MODEL_ON_CD = 0x0040CEF8;

    // GTA compares the sign-extended next IMG ID with -1. Once IDs above
    // 32767 are valid, the load must be unsigned, but 0xFFFF must still end
    // the chain instead of being used as streaming-table index 65535.
    void __declspec(naked) CompareNextModelOnCdUnsigned()
    {
        MTA_VERIFY_HOOK_LOCAL_SIZE;
        // clang-format off
        __asm
        {
            cmp esi, 0FFFFh
            je  endOfChain
            jmp CONTINUE_NEXT_MODEL_ON_CD

        endOfChain:
            jmp END_NEXT_MODEL_ON_CD
        }
        // clang-format on
    }

    bool __cdecl SaveStockStreamingFlags()
    {
        if (!g_relocatedStreamingInfo)
            return false;

        std::array<BYTE, STOCK_SAVED_FILE_COUNT> flags{};
        size_t                                   output = 0;
        for (const SStockPartition& partition : STOCK_PARTITIONS)
        {
            for (DWORD index = 0; index < partition.count; ++index)
            {
                const CStreamingInfo& info = g_relocatedStreamingInfo[partition.targetBase + index];
                flags[output++] = info.loadState == eModelLoadState::LOADSTATE_LOADED ? info.flg : 0xFF;
            }
        }
        return output == flags.size() && reinterpret_cast<BufferTransfer>(0x005D1270)(flags.data(), static_cast<int>(flags.size()));
    }

    bool __cdecl LoadStockStreamingFlags()
    {
        if (!g_relocatedStreamingInfo)
            return false;

        std::array<BYTE, STOCK_SAVED_FILE_COUNT> flags{};
        if (!reinterpret_cast<BufferTransfer>(0x005D1300)(flags.data(), static_cast<int>(flags.size())))
            return false;

        size_t input = 0;
        for (const SStockPartition& partition : STOCK_PARTITIONS)
        {
            for (DWORD index = 0; index < partition.count; ++index)
            {
                CStreamingInfo& info = g_relocatedStreamingInfo[partition.targetBase + index];
                const BYTE      savedFlags = flags[input++];
                if (info.loadState == eModelLoadState::LOADSTATE_LOADED && savedFlags != 0xFF)
                    info.flg |= savedFlags;
            }
        }
        return input == flags.size();
    }

    bool ResolveReplacement(const SRelocationPatch& patch, DWORD& replacement, std::string& error)
    {
        switch (patch.kind)
        {
            case ERelocationPatchKind::ModelPointer:
                replacement = reinterpret_cast<DWORD>(g_relocatedModelInfo) + patch.replacement;
                return true;
            case ERelocationPatchKind::StreamingPointer:
                replacement = reinterpret_cast<DWORD>(g_relocatedStreamingInfo) + patch.replacement;
                return true;
            case ERelocationPatchKind::Value32:
            case ERelocationPatchKind::Movzx:
            case ERelocationPatchKind::Value16:
                replacement = patch.replacement;
                return true;
            case ERelocationPatchKind::RedirectNextOnCd:
            case ERelocationPatchKind::RedirectSave:
            case ERelocationPatchKind::RedirectLoad:
            {
                DWORD target{};
                if (patch.kind == ERelocationPatchKind::RedirectNextOnCd)
                    target = reinterpret_cast<DWORD>(&CompareNextModelOnCdUnsigned);
                else if (patch.kind == ERelocationPatchKind::RedirectSave)
                    target = reinterpret_cast<DWORD>(&SaveStockStreamingFlags);
                else
                    target = reinterpret_cast<DWORD>(&LoadStockStreamingFlags);
                const int64_t displacement = static_cast<int64_t>(target) - static_cast<int64_t>(patch.address + 5);
                if (displacement < std::numeric_limits<std::int32_t>::min() || displacement > std::numeric_limits<std::int32_t>::max())
                {
                    error = SString("FileID relocation hook is out of relative-jump range at 0x%08X", patch.address);
                    return false;
                }
                replacement = static_cast<DWORD>(static_cast<std::int32_t>(displacement));
                return true;
            }
        }
        return false;
    }
}  // namespace

bool CFileIDRuntimeSA::CaptureStockLayout(eGameVersion gameVersion, std::string& error)
{
    if (gameVersion != VERSION_US_10)
    {
        error = "unsupported executable version for FileID runtime anchors";
        return false;
    }

    const HMODULE module = GetModuleHandle(nullptr);
    if (reinterpret_cast<uintptr_t>(module) != EXPECTED_IMAGE_BASE)
    {
        error = "unexpected executable image base for FileID runtime anchors";
        return false;
    }

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (!IsReadable(dos, sizeof(*dos)) || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x100000)
    {
        error = "invalid executable DOS header for FileID runtime anchors";
        return false;
    }

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const BYTE*>(module) + dos->e_lfanew);
    if (!IsReadable(nt, sizeof(*nt)) || nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
    {
        error = "invalid executable PE32 header for FileID runtime anchors";
        return false;
    }

    const DWORD imageSize = nt->OptionalHeader.SizeOfImage;
    for (const SAnchor& anchor : ANCHORS)
    {
        const uint64_t end = static_cast<uint64_t>(anchor.instructionAddress) + anchor.instructionSize;
        if (anchor.instructionAddress < EXPECTED_IMAGE_BASE || end > static_cast<uint64_t>(EXPECTED_IMAGE_BASE) + imageSize ||
            anchor.operandOffset + sizeof(DWORD) > anchor.instructionSize ||
            std::memcmp(reinterpret_cast<const void*>(anchor.instructionAddress), anchor.expected.data(), anchor.instructionSize) != 0)
        {
            error = SString("FileID runtime anchor %s failed read-only byte validation at 0x%08X", anchor.name, anchor.instructionAddress);
            return false;
        }

        if (*reinterpret_cast<const DWORD*>(anchor.instructionAddress + anchor.operandOffset) != anchor.stockValue)
        {
            error = SString("FileID runtime anchor %s has an unexpected stock operand", anchor.name);
            return false;
        }
    }

    SFileIDLayout layout{};
    layout.dff = 0;
    layout.txd = ReadAnchor(EAnchor::TxdBase);
    layout.col = ReadAnchor(EAnchor::ColBase);
    layout.ipl = ReadAnchor(EAnchor::IplBase);
    layout.dat = ReadAnchor(EAnchor::DatBase);
    layout.ifp = ReadAnchor(EAnchor::IfpBase);
    layout.rrr = ReadAnchor(EAnchor::RrrBase);
    layout.scm = ReadAnchor(EAnchor::ScmBase);

    const uintptr_t streamingBegin = ReadAnchor(EAnchor::StreamingBegin);
    const uintptr_t streamingEnd = ReadAnchor(EAnchor::StreamingEnd);
    if (streamingEnd <= streamingBegin || (streamingEnd - streamingBegin) % sizeof(CStreamingInfo) != 0)
    {
        error = "FileID streaming table endpoints do not form a whole CStreamingInfo array";
        return false;
    }

    layout.total = static_cast<std::uint32_t>((streamingEnd - streamingBegin) / sizeof(CStreamingInfo));
    if (layout.total != STOCK_STREAMING_INFO_COUNT || layout.total < 4 || layout.scm > layout.total - 4)
    {
        error = "FileID stock streaming table count or sentinel layout is invalid";
        return false;
    }
    layout.loadedList = layout.total - 4;
    layout.requestedList = layout.total - 2;

    if (std::memcmp(&layout, &STOCK_LAYOUT, sizeof(layout)) != 0)
    {
        error = "FileID stock partitions differ from the relocation contract";
        return false;
    }

    auto* streamingInfoArray = reinterpret_cast<CStreamingInfo*>(streamingBegin);
    void* modelInfoArray = reinterpret_cast<void*>(ReadAnchor(EAnchor::ModelInfoBegin));
    if (!IsReadable(streamingInfoArray, static_cast<size_t>(layout.total) * sizeof(CStreamingInfo)) ||
        !IsReadable(modelInfoArray, static_cast<size_t>(layout.txd) * sizeof(void*)))
    {
        error = "FileID runtime arrays are not fully readable";
        return false;
    }
    if (!ValidateRelocationManifest(imageSize, error))
        return false;

    m_layout = layout;
    m_streamingInfoArray = streamingInfoArray;
    m_modelInfoArray = modelInfoArray;
    m_imageSize = imageSize;
    m_relocationPrepared = true;
    SharedUtil::WriteDebugEvent(
        SString("[NativeFileID] state=prepared layout=stock dff=%u txd=%u col=%u ipl=%u dat=%u ifp=%u rrr=%u scm=%u loaded=%u requested=%u "
                "total=%u streaming=%p models=%p patchSites=%u nativeWrites=no",
                layout.dff, layout.txd, layout.col, layout.ipl, layout.dat, layout.ifp, layout.rrr, layout.scm, layout.loadedList, layout.requestedList,
                layout.total, m_streamingInfoArray, m_modelInfoArray, static_cast<unsigned int>(std::size(RELOCATION_PATCHES))));
    return true;
}

bool CFileIDRuntimeSA::InstallStockRelocation(std::string& error)
{
    if (m_relocationInstalled)
        return true;
    if (!m_relocationPrepared || m_installStarted)
    {
        error = m_installStarted ? "a previous FileID relocation commit did not complete" : "FileID relocation was not prepared";
        return false;
    }
    if (!ValidateRelocationManifest(m_imageSize, error))
        return false;

    g_relocatedModelInfo =
        static_cast<void**>(VirtualAlloc(nullptr, static_cast<size_t>(TARGET_MODEL_INFO_COUNT + 1) * sizeof(void*), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    g_relocatedStreamingInfo = static_cast<CStreamingInfo*>(
        VirtualAlloc(nullptr, static_cast<size_t>(TARGET_STREAMING_INFO_COUNT + 1) * sizeof(CStreamingInfo), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!g_relocatedModelInfo || !g_relocatedStreamingInfo)
    {
        if (g_relocatedModelInfo)
            VirtualFree(g_relocatedModelInfo, 0, MEM_RELEASE);
        if (g_relocatedStreamingInfo)
            VirtualFree(g_relocatedStreamingInfo, 0, MEM_RELEASE);
        g_relocatedModelInfo = nullptr;
        g_relocatedStreamingInfo = nullptr;
        error = "unable to allocate process-lifetime FileID tables";
        return false;
    }
    const auto releasePreparedTables = []()
    {
        VirtualFree(g_relocatedModelInfo, 0, MEM_RELEASE);
        VirtualFree(g_relocatedStreamingInfo, 0, MEM_RELEASE);
        g_relocatedModelInfo = nullptr;
        g_relocatedStreamingInfo = nullptr;
    };

    std::memcpy(g_relocatedModelInfo, m_modelInfoArray, static_cast<size_t>(STOCK_MODEL_INFO_COUNT) * sizeof(void*));
    for (const SStockPartition& partition : STOCK_PARTITIONS)
    {
        std::memcpy(g_relocatedStreamingInfo + partition.targetBase, m_streamingInfoArray + partition.stockBase,
                    static_cast<size_t>(partition.count) * sizeof(CStreamingInfo));
    }
    std::memcpy(g_relocatedStreamingInfo + TARGET_LAYOUT.loadedList, m_streamingInfoArray + STOCK_LAYOUT.loadedList,
                static_cast<size_t>(4) * sizeof(CStreamingInfo));

    struct SPreparedWrite
    {
        DWORD               address;
        std::array<BYTE, 5> bytes;
        size_t              size;
    };
    std::vector<SPreparedWrite> writes;
    writes.reserve(std::size(RELOCATION_PATCHES));
    for (const SRelocationPatch& patch : RELOCATION_PATCHES)
    {
        DWORD replacement{};
        if (!ResolveReplacement(patch, replacement, error))
        {
            releasePreparedTables();
            return false;
        }

        SPreparedWrite write{};
        write.address = patch.address;
        write.size = GetPatchSize(patch.kind);
        if (patch.kind == ERelocationPatchKind::RedirectNextOnCd || patch.kind == ERelocationPatchKind::RedirectSave ||
            patch.kind == ERelocationPatchKind::RedirectLoad)
        {
            write.bytes[0] = 0xE9;
            std::memcpy(write.bytes.data() + 1, &replacement, sizeof(replacement));
        }
        else
            std::memcpy(write.bytes.data(), &replacement, write.size);
        writes.push_back(write);
    }

    // Recheck every operand after allocation and preparation, directly before
    // the first native write. Startup is single-threaded, but this also catches
    // any earlier installer that changed a shared instruction unexpectedly.
    if (!ValidateRelocationManifest(m_imageSize, error))
    {
        releasePreparedTables();
        return false;
    }

    // From this point onward any failed native write is fatal to this process;
    // rolling back a partially observed table relocation would be less safe.
    m_installStarted = true;
    for (const SPreparedWrite& write : writes)
    {
        if (!WriteMemory(write.address, write.bytes.data(), write.size))
        {
            error = SString("fatal partial FileID relocation write failure at 0x%08X", write.address);
            return false;
        }
    }

    m_layout = TARGET_LAYOUT;
    m_streamingInfoArray = g_relocatedStreamingInfo;
    m_modelInfoArray = g_relocatedModelInfo;
    m_relocationInstalled = true;
    SharedUtil::WriteDebugEvent(
        SString("[NativeFileID] state=installed layout=stock-only dff=%u txd=%u col=%u ipl=%u dat=%u ifp=%u rrr=%u scm=%u loaded=%u "
                "requested=%u total=%u streaming=%p models=%p patchSites=%u nativeWrites=yes datExpansion=no pathsExpansion=no",
                m_layout.dff, m_layout.txd, m_layout.col, m_layout.ipl, m_layout.dat, m_layout.ifp, m_layout.rrr, m_layout.scm, m_layout.loadedList,
                m_layout.requestedList, m_layout.total, m_streamingInfoArray, m_modelInfoArray, static_cast<unsigned int>(std::size(RELOCATION_PATCHES))));
    return true;
}
