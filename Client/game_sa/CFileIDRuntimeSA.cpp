/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CFileIDRuntimeSA.cpp
 *  PURPOSE:     Validated runtime view of GTA SA's FileID namespace
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CFileIDRuntimeSA.h"

#include <array>
#include <cstring>
#include <limits>

namespace
{
    constexpr DWORD EXPECTED_IMAGE_BASE = 0x00400000;
    constexpr DWORD STOCK_STREAMING_INFO_COUNT = 26316;

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

    DWORD ReadAnchor(EAnchor kind)
    {
        for (const SAnchor& anchor : ANCHORS)
        {
            if (anchor.kind == kind)
                return *reinterpret_cast<const DWORD*>(anchor.instructionAddress + anchor.operandOffset);
        }
        return 0;
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

    const std::uint32_t ordered[] = {layout.dff, layout.txd, layout.col,        layout.ipl,           layout.dat,  layout.ifp,
                                     layout.rrr, layout.scm, layout.loadedList, layout.requestedList, layout.total};
    for (size_t index = 1; index < std::size(ordered); ++index)
    {
        if (ordered[index - 1] >= ordered[index])
        {
            error = "FileID stock partitions are not strictly ordered";
            return false;
        }
    }

    auto* streamingInfoArray = reinterpret_cast<CStreamingInfo*>(streamingBegin);
    void* modelInfoArray = reinterpret_cast<void*>(ReadAnchor(EAnchor::ModelInfoBegin));
    if (!IsReadable(streamingInfoArray, static_cast<size_t>(layout.total) * sizeof(CStreamingInfo)) ||
        !IsReadable(modelInfoArray, static_cast<size_t>(layout.txd) * sizeof(void*)))
    {
        error = "FileID runtime arrays are not fully readable";
        return false;
    }

    m_layout = layout;
    m_streamingInfoArray = streamingInfoArray;
    m_modelInfoArray = modelInfoArray;
    SharedUtil::WriteDebugEvent(
        SString("[NativeFileID] state=captured layout=stock dff=%u txd=%u col=%u ipl=%u dat=%u ifp=%u rrr=%u scm=%u loaded=%u requested=%u total=%u "
                "streaming=%p models=%p nativeWrites=no",
                layout.dff, layout.txd, layout.col, layout.ipl, layout.dat, layout.ifp, layout.rrr, layout.scm, layout.loadedList, layout.requestedList,
                layout.total, m_streamingInfoArray, m_modelInfoArray));
    return true;
}
