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

    C3DMarkerSAInterface* g_p3DMarkerArray = reinterpret_cast<C3DMarkerSAInterface*>(ARRAY_3D_MARKERS);

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

    static C3DMarkerSAInterface       markerArray[MAX_3D_MARKERS]{};
    static SDirectionArrowSAInterface directionArrowArray[MAX_3D_MARKERS]{};

    for (C3DMarkerSAInterface& marker : markerArray)
        InitializeUnusedMarker(marker);

    // GTA has already run the constructors and marker initializers before the
    // game_sa wrapper is created. Preserve any native state instead of trying
    // to replay model loading or the executable's CRT array constructors.
    MemCpyFast(markerArray, reinterpret_cast<const void*>(ARRAY_3D_MARKERS), GTA_3D_MARKER_COUNT * sizeof(C3DMarkerSAInterface));
    MemCpyFast(directionArrowArray, reinterpret_cast<const void*>(0xC802E8), GTA_DIRECTION_ARROW_COUNT * sizeof(SDirectionArrowSAInterface));

    g_p3DMarkerArray = markerArray;

    // C3dMarkers::Shutdown and C3dMarkers::Update.
    PatchArrayAddress(0x722714, Field(markerArray, 0x60));
    PatchArrayAddress(0x722756, Field(markerArray + MAX_3D_MARKERS, 0x60));
    PatchArrayAddress(0x7227BE, Field(markerArray, 0x48));
    PatchArrayAddress(0x7227F9, Field(markerArray + MAX_3D_MARKERS, 0x48));

    // C3dMarkers::Init and C3dMarkers::Render3dMarkers.
    PatchArrayAddress(0x724E64, Field(markerArray, 0x50));
    PatchArrayAddress(0x724ED9, Field(markerArray + MAX_3D_MARKERS, 0x50));
    PatchArrayAddress(0x72506D, Field(markerArray, 0x30));
    PatchArrayAddress(0x7250FF, Field(markerArray + MAX_3D_MARKERS, 0x30));

    // C3dMarkers::PlaceMarker. The final replacement scan is unrolled in
    // groups of eight, which is why MAX_3D_MARKERS is constrained above.
    PatchArrayAddress(0x72518B, Field(markerArray, 0x54));
    PatchArrayAddress(0x7251A1, Field(markerArray + MAX_3D_MARKERS, 0x54));
    PatchArrayAddress(0x7251DE, markerArray);
    PatchArrayAddress(0x7251EB, Field(markerArray, 0x50));
    PatchArrayAddress(0x7251FE, Field(markerArray + MAX_3D_MARKERS, 0x50));
    PatchArrayAddress(0x72520E, markerArray);
    PatchArrayAddress(0x725234, Field(markerArray, 0x50));
    PatchArrayAddress(0x725480, Field(markerArray + MAX_3D_MARKERS, 0x50));

    // DirectionArrowInit, DirectionArrowFindFirstFreeSlot and
    // DirectionArrowSet. Checkpoint icons use this independent five-entry GTA
    // array, so it must grow with the checkpoint pool.
    PatchArrayAddress(0x721101, directionArrowArray);
    PatchArrayAddress(0x72110C, directionArrowArray + MAX_3D_MARKERS);
    PatchArrayAddress(0x721123, directionArrowArray);
    PatchArrayAddress(0x721132, directionArrowArray + MAX_3D_MARKERS);
    PatchArrayAddress(0x721143, directionArrowArray);
    PatchArrayAddress(0x721151, directionArrowArray + MAX_3D_MARKERS);
    PatchArrayAddress(0x72117B, Field(directionArrowArray, 0x4));
    PatchArrayAddress(0x721181, Field(directionArrowArray, 0xC));
    PatchArrayAddress(0x72118B, Field(directionArrowArray, 0x8));
    PatchArrayAddress(0x721195, Field(directionArrowArray, 0x10));
    PatchArrayAddress(0x72119F, Field(directionArrowArray, 0x14));
    PatchArrayAddress(0x7211A9, Field(directionArrowArray, 0x18));
    PatchArrayAddress(0x7211B3, Field(directionArrowArray, 0x1C));
    PatchArrayAddress(0x7211BD, Field(directionArrowArray, 0x20));
    PatchArrayAddress(0x7211C7, Field(directionArrowArray, 0x24));
    PatchArrayAddress(0x7211D1, Field(directionArrowArray, 0x28));
    PatchArrayAddress(0x7211D7, Field(directionArrowArray, 0x2C));
    PatchArrayAddress(0x7211DD, directionArrowArray);

    // DirectionArrowsDraw iterates from the color field at +0x14. The second
    // pair belongs to the global marker reset path at 0x7268F0.
    PatchArrayAddress(0x721218, Field(directionArrowArray, 0x14));
    PatchArrayAddress(0x7215F0, Field(directionArrowArray + MAX_3D_MARKERS, 0x14));
    PatchArrayAddress(0x72691E, directionArrowArray);
    PatchArrayAddress(0x726928, directionArrowArray + MAX_3D_MARKERS);

    // Do not patch the original array's CRT constructor/destructor calls at
    // 0x855321/0x856BDC, or globals beginning at 0xC803D8. They are not loop
    // references to the relocated runtime arrays.
    bPatched = true;
}

C3DMarkersSA::C3DMarkersSA()
{
    RelocateMarkerArrays();

    for (int i = 0; i < MAX_3D_MARKERS; i++)
    {
        Markers[i] = new C3DMarkerSA(&GetMarkerArray()[i]);
    }
}

C3DMarkersSA::~C3DMarkersSA()
{
    for (int i = 0; i < MAX_3D_MARKERS; i++)
    {
        delete Markers[i];
    }
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

    DWORD dwFunc = FUNC_PlaceMarker;
    DWORD dwReturn = 0;
    // clang-format off
    __asm
    {
        push    bZCheck     // zCheck  ##SA##
        push    0           // normalZ ##SA##
        push    0           // normalY ##SA##
        push    0           // normalX ##SA##
        push    0           // rotate rate
        push    fPulseFraction      // pulse
        push    0           // period
        push    a           // alpha
        push    b           // blue
        push    g           // green
        push    r           // red
        push    fSize       // size
        push    pVecPosCopy // position (copy to prevent PlaceMarker from corrupting the caller's vector)
        push    dwType      // type
        push    Identifier  // identifier
        call    dwFunc
        mov     dwReturn, eax
        add     esp, 0x3C
    }
    // clang-format on

    if (dwReturn)
    {
        const std::uintptr_t markerAddress = dwReturn;
        const std::uintptr_t arrayAddress = reinterpret_cast<std::uintptr_t>(GetMarkerArray());
        const std::uintptr_t arrayEndAddress = arrayAddress + sizeof(C3DMarkerSAInterface) * MAX_3D_MARKERS;
        if (markerAddress >= arrayAddress && markerAddress < arrayEndAddress && (markerAddress - arrayAddress) % sizeof(C3DMarkerSAInterface) == 0)
        {
            return Markers[(markerAddress - arrayAddress) / sizeof(C3DMarkerSAInterface)];
        }
    }

    return NULL;
}

C3DMarker* C3DMarkersSA::FindFreeMarker()
{
    for (int i = 0; i < MAX_3D_MARKERS; i++)
    {
        if (!Markers[i]->IsActive())
            return Markers[i];
    }
    return NULL;
}

C3DMarker* C3DMarkersSA::FindMarker(DWORD Identifier)
{
    for (int i = 0; i < MAX_3D_MARKERS; i++)
    {
        if (Markers[i]->GetIdentifier() == Identifier)
            return Markers[i];
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
}

unsigned int C3DMarkersSA::GetCount() const
{
    unsigned int count = 0;
    for (C3DMarkerSA* marker : Markers)
    {
        if (marker->GetInterface()->m_nType != UNUSED_MARKER_TYPE)
            ++count;
    }
    return count;
}
