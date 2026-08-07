/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/C3DMarkersSA.cpp
 *  PURPOSE:     3D Marker entity manager
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "C3DMarkersSA.h"

namespace
{
    constexpr std::size_t GTA_3D_MARKER_COUNT = 32;
    constexpr std::size_t GTA_DIRECTION_ARROW_COUNT = 5;
    constexpr std::size_t PROCESS_LIMIT_GROWTH = 32;
    constexpr WORD        UNUSED_MARKER_TYPE = 257;

    struct SDirectionArrowSAInterface
    {
        bool    m_bIsUsed;
        CVector m_vecPosition;
        float   m_fSize;
        CVector m_vecDirection;
        DWORD   m_nRed;
        DWORD   m_nGreen;
        DWORD   m_nBlue;
        DWORD   m_nAlpha;
    };

    static_assert(sizeof(SDirectionArrowSAInterface) == 0x30, "Invalid GTA direction-arrow size");
    static_assert(offsetof(SDirectionArrowSAInterface, m_vecPosition) == 0x4, "Invalid GTA direction-arrow position offset");
    static_assert(offsetof(SDirectionArrowSAInterface, m_nAlpha) == 0x2C, "Invalid GTA direction-arrow color layout");
    static_assert(MAX_3D_MARKERS % 8 == 0, "GTA's unrolled marker search requires a multiple-of-eight capacity");

    C3DMarkerSAInterface       g_markerArray[MAX_3D_MARKERS]{};
    SDirectionArrowSAInterface g_directionArrowArray[MAX_3D_MARKERS]{};
    C3DMarkerSAInterface*      g_p3DMarkerArray = reinterpret_cast<C3DMarkerSAInterface*>(ARRAY_3D_MARKERS);
    std::size_t                g_markerProcessLimit{};
    std::size_t                g_markerSearchLimit{};
    std::size_t                g_directionArrowProcessLimit{};
    std::size_t                g_directionArrowNextSlot{};

    void PatchArrayAddress(std::uintptr_t operandAddress, const void* value)
    {
        MemPut<DWORD>(operandAddress, reinterpret_cast<DWORD>(value));
    }

    BYTE* Field(void* base, std::size_t offset)
    {
        return reinterpret_cast<BYTE*>(base) + offset;
    }

    void InitializeUnusedMarker(C3DMarkerSAInterface& marker)
    {
        marker.m_nType = UNUSED_MARKER_TYPE;
        marker.rwColour = 0xFFFFFFFF;
        marker.m_nPulsePeriod = 1024;
        marker.m_nRotateRate = 5;
        marker.m_fPulseFraction = 0.25f;
        marker.m_fStdSize = 1.0f;
        marker.m_fSize = 1.0f;
        marker.m_fBrightness = 1.0f;
        marker.m_LastMapReadX = 30000;
        marker.m_LastMapReadY = 30000;
        marker.m_roofHeight = 65535.0f;
    }

    std::size_t RoundMarkerLimit(std::size_t limit)
    {
        constexpr std::size_t groupSize = 8;
        return std::min<std::size_t>(MAX_3D_MARKERS, (limit + groupSize - 1) & ~(groupSize - 1));
    }

    void SetMarkerProcessLimit(std::size_t requestedLimit)
    {
        const std::size_t limit = RoundMarkerLimit(std::max(requestedLimit, GTA_3D_MARKER_COUNT));
        if (limit == g_markerProcessLimit)
            return;

        // Only per-frame update/render follows the live working set. Init and
        // shutdown retain the full allocation so reinitialization always
        // releases every native object, including entries outside this bound.
        PatchArrayAddress(0x7227F9, Field(g_markerArray + limit, 0x48));
        PatchArrayAddress(0x7250FF, Field(g_markerArray + limit, 0x30));
        g_markerProcessLimit = limit;
    }

    void SetMarkerSearchLimit(std::size_t requestedLimit)
    {
        const std::size_t limit = RoundMarkerLimit(std::max(requestedLimit, GTA_3D_MARKER_COUNT));
        if (limit == g_markerSearchLimit)
            return;

        // PlaceMarker has three independent searches, including a replacement
        // loop unrolled by eight. Keep their shared bound separate from the
        // per-frame update/render high-water mark.
        PatchArrayAddress(0x7251A1, Field(g_markerArray + limit, 0x54));
        PatchArrayAddress(0x7251FE, Field(g_markerArray + limit, 0x50));
        PatchArrayAddress(0x725480, Field(g_markerArray + limit, 0x50));
        g_markerSearchLimit = limit;
    }

    void RefreshMarkerProcessLimit()
    {
        std::size_t       requiredLimit = GTA_3D_MARKER_COUNT;
        const std::size_t previousLimit = std::max(g_markerProcessLimit, g_markerSearchLimit);
        for (std::size_t index = previousLimit; index > GTA_3D_MARKER_COUNT; --index)
        {
            const C3DMarkerSAInterface& marker = g_markerArray[index - 1];
            if (marker.m_nType != UNUSED_MARKER_TYPE || marker.m_bIsUsed || marker.m_bIsActive || marker.m_pRwObject)
            {
                requiredLimit = index;
                break;
            }
        }
        SetMarkerProcessLimit(requiredLimit);
        SetMarkerSearchLimit(requiredLimit);
    }

    void SetDirectionArrowProcessLimit(std::size_t requestedLimit)
    {
        const std::size_t limit = std::clamp<std::size_t>(requestedLimit, GTA_DIRECTION_ARROW_COUNT, MAX_3D_MARKERS);
        if (limit == g_directionArrowProcessLimit)
            return;

        PatchArrayAddress(0x7215F0, Field(g_directionArrowArray + limit, 0x14));
        g_directionArrowProcessLimit = limit;
    }

    void BeginDirectionArrowFrame(std::size_t expectedArrowCount)
    {
        std::size_t usedCount = 0;
        std::size_t highestUsedSlot = 0;
        for (std::size_t index = 0; index < g_directionArrowProcessLimit; ++index)
        {
            if (!g_directionArrowArray[index].m_bIsUsed)
                continue;

            ++usedCount;
            highestUsedSlot = index + 1;
        }

        // DirectionArrowsDraw clears every submitted entry. The next frame can
        // therefore return to the five-slot stock bound, while arrows already
        // submitted by other native code remain inside the new working set.
        g_directionArrowNextSlot = 0;
        SetDirectionArrowProcessLimit(std::max(highestUsedSlot, usedCount + expectedArrowCount));
    }

    int32_t __cdecl DirectionArrowFindFirstFreeSlotHook()
    {
        // Draw clears used flags but intentionally does not call back into the
        // allocator. Detect that boundary as well as the explicit BeginFrame
        // reset so callers between those two points reuse slot zero.
        if (g_directionArrowNextSlot > 0 && !g_directionArrowArray[0].m_bIsUsed)
            g_directionArrowNextSlot = 0;

        while (g_directionArrowNextSlot < g_directionArrowProcessLimit && g_directionArrowArray[g_directionArrowNextSlot].m_bIsUsed)
        {
            ++g_directionArrowNextSlot;
        }

        if (g_directionArrowNextSlot == g_directionArrowProcessLimit && g_directionArrowProcessLimit < MAX_3D_MARKERS)
        {
            SetDirectionArrowProcessLimit(std::min<std::size_t>(MAX_3D_MARKERS, g_directionArrowProcessLimit + PROCESS_LIMIT_GROWTH));
        }

        return g_directionArrowNextSlot < g_directionArrowProcessLimit ? static_cast<int32_t>(g_directionArrowNextSlot) : -1;
    }

    void __cdecl DirectionArrowSetHook(float x, float y, float z, float size, DWORD red, DWORD green, DWORD blue, DWORD alpha, float directionX,
                                       float directionY, float directionZ)
    {
        const int32_t index = DirectionArrowFindFirstFreeSlotHook();
        if (index < 0)
            return;

        SDirectionArrowSAInterface& arrow = g_directionArrowArray[index];
        arrow.m_vecPosition = CVector{x, y, z + 3.0f};
        arrow.m_fSize = size;
        arrow.m_vecDirection = CVector{directionX, directionY, directionZ};
        arrow.m_nRed = red;
        arrow.m_nGreen = green;
        arrow.m_nBlue = blue;
        arrow.m_nAlpha = alpha;
        arrow.m_bIsUsed = true;
        g_directionArrowNextSlot = static_cast<std::size_t>(index) + 1;
    }

}  // namespace

C3DMarkerSAInterface* C3DMarkersSA::GetMarkerArray()
{
    return g_p3DMarkerArray;
}

void C3DMarkersSA::RelocateMarkerArrays()
{
    static bool bPatched = false;
    if (bPatched)
        return;

    for (C3DMarkerSAInterface& marker : g_markerArray)
        InitializeUnusedMarker(marker);

    // GTA has already run the constructors and marker initializers before the
    // game_sa wrapper is created. Preserve any native state instead of trying
    // to replay model loading or the executable's CRT array constructors.
    MemCpyFast(g_markerArray, reinterpret_cast<const void*>(ARRAY_3D_MARKERS), GTA_3D_MARKER_COUNT * sizeof(C3DMarkerSAInterface));
    MemCpyFast(g_directionArrowArray, reinterpret_cast<const void*>(0xC802E8), GTA_DIRECTION_ARROW_COUNT * sizeof(SDirectionArrowSAInterface));

    g_p3DMarkerArray = g_markerArray;

    // C3dMarkers::Shutdown and C3dMarkers::Update.
    PatchArrayAddress(0x722714, Field(g_markerArray, 0x60));
    PatchArrayAddress(0x722756, Field(g_markerArray + MAX_3D_MARKERS, 0x60));
    PatchArrayAddress(0x7227BE, Field(g_markerArray, 0x48));

    // C3dMarkers::Init and C3dMarkers::Render3dMarkers.
    PatchArrayAddress(0x724E64, Field(g_markerArray, 0x50));
    PatchArrayAddress(0x724ED9, Field(g_markerArray + MAX_3D_MARKERS, 0x50));
    PatchArrayAddress(0x72506D, Field(g_markerArray, 0x30));

    // C3dMarkers::PlaceMarker. The final replacement scan is unrolled in
    // groups of eight, which is why MAX_3D_MARKERS is constrained above.
    PatchArrayAddress(0x72518B, Field(g_markerArray, 0x54));
    PatchArrayAddress(0x7251DE, g_markerArray);
    PatchArrayAddress(0x7251EB, Field(g_markerArray, 0x50));
    PatchArrayAddress(0x72520E, g_markerArray);
    PatchArrayAddress(0x725234, Field(g_markerArray, 0x50));

    // DirectionArrowInit, DirectionArrowFindFirstFreeSlot and
    // DirectionArrowSet. Checkpoint icons use this independent five-entry GTA
    // array, so it must grow with the checkpoint pool.
    PatchArrayAddress(0x721101, g_directionArrowArray);
    PatchArrayAddress(0x72110C, g_directionArrowArray + MAX_3D_MARKERS);
    PatchArrayAddress(0x721123, g_directionArrowArray);
    PatchArrayAddress(0x721143, g_directionArrowArray);
    PatchArrayAddress(0x72117B, Field(g_directionArrowArray, 0x4));
    PatchArrayAddress(0x721181, Field(g_directionArrowArray, 0xC));
    PatchArrayAddress(0x72118B, Field(g_directionArrowArray, 0x8));
    PatchArrayAddress(0x721195, Field(g_directionArrowArray, 0x10));
    PatchArrayAddress(0x72119F, Field(g_directionArrowArray, 0x14));
    PatchArrayAddress(0x7211A9, Field(g_directionArrowArray, 0x18));
    PatchArrayAddress(0x7211B3, Field(g_directionArrowArray, 0x1C));
    PatchArrayAddress(0x7211BD, Field(g_directionArrowArray, 0x20));
    PatchArrayAddress(0x7211C7, Field(g_directionArrowArray, 0x24));
    PatchArrayAddress(0x7211D1, Field(g_directionArrowArray, 0x28));
    PatchArrayAddress(0x7211D7, Field(g_directionArrowArray, 0x2C));
    PatchArrayAddress(0x7211DD, g_directionArrowArray);

    // DirectionArrowsDraw iterates from the color field at +0x14. The second
    // pair belongs to the global marker reset path at 0x7268F0.
    PatchArrayAddress(0x721218, Field(g_directionArrowArray, 0x14));
    PatchArrayAddress(0x72691E, g_directionArrowArray);
    PatchArrayAddress(0x726928, g_directionArrowArray + MAX_3D_MARKERS);

    SetMarkerProcessLimit(GTA_3D_MARKER_COUNT);
    SetMarkerSearchLimit(GTA_3D_MARKER_COUNT);
    SetDirectionArrowProcessLimit(GTA_DIRECTION_ARROW_COUNT);

    // The stock setter searches from slot zero for every arrow. Replacing both
    // entry points with one append cursor turns N checkpoint arrows from an
    // O(N^2) submission path into O(N), while preserving the native ABI.
    HookInstall(0x721120, &DirectionArrowFindFirstFreeSlotHook, 7);
    HookInstall(0x721140, &DirectionArrowSetHook, 7);

    // Do not patch the original array's CRT constructor/destructor calls at
    // 0x855321/0x856BDC, or globals beginning at 0xC803D8. They are not loop
    // references to the relocated runtime arrays.
    bPatched = true;
}

C3DMarkersSA::C3DMarkersSA()
{
    RelocateMarkerArrays();

    for (int i = 0; i < MAX_3D_MARKERS; i++)
        Markers[i].SetInterface(&GetMarkerArray()[i]);
}

void C3DMarkersSA::SetExpectedDirectionArrowCount(std::size_t count)
{
    BeginDirectionArrowFrame(count);
}

C3DMarker* C3DMarkersSA::CreateMarker(DWORD Identifier, T3DMarkerType dwType, CVector* vecPosition, float fSize, float fPulseFraction, BYTE r, BYTE g, BYTE b,
                                      BYTE a)
{
    /*
    static C3dMarker *PlaceMarker(unsigned int nIdentifier, unsigned short nType,
    CVector &vecPosition, float fSize, unsigned char r, unsigned char g, unsigned char b, unsigned char a,
    unsigned short nPeriod, float fPulseFrac, short nRotRate, float normalX = 0.0f,
    float normalY = 0.0f, float normalZ = 0.0f, bool zCheck = FALSE);
    */
    WORD wType = (WORD)dwType;
    dwType = (T3DMarkerType)wType;
    bool bZCheck = true;

    // Pass a copy of the position to PlaceMarker, not the original pointer.
    CVector  vecPositionCopy = *vecPosition;
    CVector* pVecPosCopy = &vecPositionCopy;

    using PlaceMarker = C3DMarkerSAInterface*(__cdecl*)(DWORD, WORD, CVector*, float, BYTE, BYTE, BYTE, BYTE, WORD, float, short, float, float, float, bool);
    const auto placeMarker = reinterpret_cast<PlaceMarker>(FUNC_PlaceMarker);
    auto*      markerInterface = placeMarker(Identifier, wType, pVecPosCopy, fSize, r, g, b, a, 0, fPulseFraction, 0, 0.0f, 0.0f, 0.0f, bZCheck);

    // A genuinely full search window is uncommon. Grow and retry only after
    // the native allocator proves it needs another chunk, avoiding a second
    // linear scan before every normal PlaceMarker call.
    if (!markerInterface && g_markerSearchLimit < MAX_3D_MARKERS)
    {
        const std::size_t expandedLimit = std::min<std::size_t>(MAX_3D_MARKERS, g_markerSearchLimit + PROCESS_LIMIT_GROWTH);
        SetMarkerSearchLimit(expandedLimit);
        SetMarkerProcessLimit(expandedLimit);
        markerInterface = placeMarker(Identifier, wType, pVecPosCopy, fSize, r, g, b, a, 0, fPulseFraction, 0, 0.0f, 0.0f, 0.0f, bZCheck);
    }

    if (markerInterface)
    {
        const std::uintptr_t markerAddress = reinterpret_cast<std::uintptr_t>(markerInterface);
        const std::uintptr_t arrayAddress = reinterpret_cast<std::uintptr_t>(GetMarkerArray());
        const std::uintptr_t arrayEndAddress = arrayAddress + sizeof(C3DMarkerSAInterface) * MAX_3D_MARKERS;
        if (markerAddress >= arrayAddress && markerAddress < arrayEndAddress && (markerAddress - arrayAddress) % sizeof(C3DMarkerSAInterface) == 0)
        {
            const std::size_t markerIndex = (markerAddress - arrayAddress) / sizeof(C3DMarkerSAInterface);
            SetMarkerProcessLimit(markerIndex + 1);
            return &Markers[markerIndex];
        }
    }

    return NULL;
}

C3DMarker* C3DMarkersSA::FindFreeMarker()
{
    for (std::size_t i = 0; i < g_markerProcessLimit; i++)
    {
        if (!Markers[i].IsActive())
            return &Markers[i];
    }

    if (g_markerProcessLimit < MAX_3D_MARKERS)
    {
        const std::size_t firstNewSlot = g_markerProcessLimit;
        SetMarkerProcessLimit(std::min<std::size_t>(MAX_3D_MARKERS, g_markerProcessLimit + PROCESS_LIMIT_GROWTH));
        SetMarkerSearchLimit(g_markerProcessLimit);
        return &Markers[firstNewSlot];
    }
    return NULL;
}

void C3DMarkersSA::RenderScriptImportantArea(DWORD identifier, const CVector& center, float radiusX, float radiusY)
{
    // SCM LOCATE commands submit this primitive every script frame. Calling
    // the verified GTA routine preserves its three concentric cylinders,
    // pulse timing, additive alpha, and per-layer ground-height correction.
    using HighlightImportantArea = void(__cdecl*)(DWORD, float, float, float, float, float);
    reinterpret_cast<HighlightImportantArea>(FUNC_HighlightImportantArea)(identifier, center.fX - radiusX, center.fY - radiusY, center.fX + radiusX,
                                                                          center.fY + radiusY, center.fZ);
}

C3DMarker* C3DMarkersSA::FindMarker(DWORD Identifier)
{
    for (std::size_t i = 0; i < g_markerSearchLimit; i++)
    {
        if (Markers[i].GetIdentifier() == Identifier)
            return &Markers[i];
    }
    return NULL;
}

void C3DMarkersSA::ReinitMarkers()
{
    using Function_ShutdownMarkers = void(__cdecl*)();
    auto shutdownMarkers = reinterpret_cast<Function_ShutdownMarkers>(0x722710);

    using Function_InitMarkers = void(__cdecl*)();
    auto initMarkers = reinterpret_cast<Function_InitMarkers>(0x724E40);

    shutdownMarkers();
    initMarkers();
    g_directionArrowNextSlot = 0;
    for (SDirectionArrowSAInterface& arrow : g_directionArrowArray)
        arrow.m_bIsUsed = false;
    SetMarkerProcessLimit(GTA_3D_MARKER_COUNT);
    SetMarkerSearchLimit(GTA_3D_MARKER_COUNT);
    SetDirectionArrowProcessLimit(GTA_DIRECTION_ARROW_COUNT);
}

void C3DMarkersSA::BeginFrame()
{
    RefreshMarkerProcessLimit();
}

void C3DMarkersSA::ReservePlaceMarkerSlots(unsigned int additionalSlots)
{
    std::size_t occupiedSlots = 0;
    for (std::size_t index = 0; index < g_markerSearchLimit; ++index)
    {
        if (g_markerArray[index].m_nType != UNUSED_MARKER_TYPE)
            ++occupiedSlots;
    }

    // Torus/tube markers cannot use GTA's generic replacement path. Budget
    // for every current occupant plus every upcoming checkpoint emission so
    // identifier churn cannot make a valid checkpoint silently disappear.
    // Empty reservation slots are discarded by BeginFrame, so this does not
    // accumulate from one frame to the next.
    const std::size_t requiredLimit = std::min<std::size_t>(MAX_3D_MARKERS, std::max<std::size_t>(GTA_3D_MARKER_COUNT, occupiedSlots + additionalSlots));
    const std::size_t reservedLimit = std::max(g_markerProcessLimit, requiredLimit);
    SetMarkerSearchLimit(reservedLimit);
    SetMarkerProcessLimit(reservedLimit);
}

unsigned int C3DMarkersSA::GetCount() const
{
    unsigned int count = 0;
    for (std::size_t index = 0; index < g_markerProcessLimit; ++index)
    {
        if (Markers[index].GetInterface()->m_nType != UNUSED_MARKER_TYPE)
            ++count;
    }
    return count;
}

unsigned int C3DMarkersSA::GetProcessLimit() const
{
    return static_cast<unsigned int>(g_markerProcessLimit);
}

unsigned int C3DMarkersSA::GetDirectionArrowProcessLimit() const
{
    return static_cast<unsigned int>(g_directionArrowProcessLimit);
}
