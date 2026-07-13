/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CRadarSA.cpp
 *  PURPOSE:     Game radar
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include <CRect.h>
#include <CVector2D.h>
#include <game/CRenderWare.h>
#include <game/RenderWare.h>
#include "CGameSA.h"
#include "CRadarSA.h"

#include <array>
#include <cmath>
#include <cstring>

extern CGameSA* pGame;

CMarkerSA* Markers[MAX_MARKERS];

namespace
{
    constexpr unsigned int RADAR_MAP_SIZE = 40;
    constexpr int          RADAR_MAP_GTA_OFFSET = 14;
    constexpr int          RADAR_MAP_MIN_GTA_TILE = -RADAR_MAP_GTA_OFFSET;
    constexpr int          RADAR_MAP_MAX_GTA_TILE = RADAR_MAP_SIZE - RADAR_MAP_GTA_OFFSET - 1;
    constexpr int          RADAR_MAP_STREAM_RADIUS = 1;
    constexpr int          RADAR_MAP_RETAIN_RADIUS = 2;

    constexpr DWORD FUNC_GetTextureCorners = 0x584D90;
    constexpr DWORD FUNC_ClipRadarPoly = 0x585040;
    constexpr DWORD FUNC_TransformRadarPointToScreenSpace = 0x583480;
    constexpr DWORD FUNC_DrawRadarSection = 0x586110;
    constexpr DWORD FUNC_StreamRadarSectionsXY = 0x584C50;
    constexpr DWORD FUNC_StreamRadarSectionsVector = 0x5858D0;
    constexpr DWORD FUNC_CSprite2d_SetVertices = 0x727890;
    constexpr DWORD FUNC_RwIm2DRenderPrimitive = 0x734E90;

    constexpr DWORD VAR_RwEngineInstance = 0xC97B24;
    constexpr DWORD VAR_CSprite2dVertices = 0xC80468;
    constexpr DWORD VAR_RadarCachedCos = 0xBA8308;
    constexpr DWORD VAR_RadarCachedSin = 0xBA830C;
    constexpr DWORD VAR_RadarRange = 0xBA8314;
    constexpr DWORD VAR_RadarOrigin = 0xBAA248;

    struct SRadarColor
    {
        unsigned char red;
        unsigned char green;
        unsigned char blue;
        unsigned char alpha;
    };

    using GetTextureCorners_t = void(__cdecl*)(int, int, CVector2D*);
    using ClipRadarPoly_t = int(__cdecl*)(CVector2D*, const CVector2D*);
    // GTA writes the result through a hidden output pointer but leaves the
    // input pointer in EAX. Calling it as a normal C++ struct-return function
    // would therefore copy radar-space coordinates back as screen positions.
    using TransformRadarPointToScreenSpace_t = void(__cdecl*)(CVector2D*, const CVector2D*);
    using DrawRadarSection_t = void(__cdecl*)(int, int);
    using StreamRadarSectionsXY_t = void(__cdecl*)(int, int);
    using StreamRadarSectionsVector_t = void(__cdecl*)(const CVector&);
    using SpriteSetVertices_t = void(__cdecl*)(int, const CVector2D*, const CVector2D*, const SRadarColor&);
    using RwIm2DRenderPrimitive_t = int(__cdecl*)(int, void*, int);
    using RwRenderStateSet_t = BOOL(__cdecl*)(DWORD, void*);

    GetTextureCorners_t                GetTextureCorners = reinterpret_cast<GetTextureCorners_t>(FUNC_GetTextureCorners);
    ClipRadarPoly_t                    ClipRadarPoly = reinterpret_cast<ClipRadarPoly_t>(FUNC_ClipRadarPoly);
    TransformRadarPointToScreenSpace_t TransformRadarPointToScreenSpace =
        reinterpret_cast<TransformRadarPointToScreenSpace_t>(FUNC_TransformRadarPointToScreenSpace);
    DrawRadarSection_t          DrawRadarSection = reinterpret_cast<DrawRadarSection_t>(FUNC_DrawRadarSection);
    StreamRadarSectionsXY_t     StreamRadarSectionsXY = reinterpret_cast<StreamRadarSectionsXY_t>(FUNC_StreamRadarSectionsXY);
    StreamRadarSectionsVector_t StreamRadarSectionsVector = reinterpret_cast<StreamRadarSectionsVector_t>(FUNC_StreamRadarSectionsVector);
    SpriteSetVertices_t         SpriteSetVertices = reinterpret_cast<SpriteSetVertices_t>(FUNC_CSprite2d_SetVertices);
    RwIm2DRenderPrimitive_t     RwIm2DRenderPrimitive = reinterpret_cast<RwIm2DRenderPrimitive_t>(FUNC_RwIm2DRenderPrimitive);

    CRadarSA* g_pExtendedRadar = nullptr;

    bool IsGtaTile(int x, int y)
    {
        return x >= 0 && x < 12 && y >= 0 && y < 12;
    }

    bool IsExtendedWorldTile(int x, int y)
    {
        return x >= RADAR_MAP_MIN_GTA_TILE && x <= RADAR_MAP_MAX_GTA_TILE && y >= RADAR_MAP_MIN_GTA_TILE && y <= RADAR_MAP_MAX_GTA_TILE;
    }

    bool IsRegisterableTile(unsigned int column, unsigned int row)
    {
        if (column >= RADAR_MAP_SIZE || row >= RADAR_MAP_SIZE)
            return false;

        return !IsGtaTile(static_cast<int>(column) - RADAR_MAP_GTA_OFFSET, static_cast<int>(row) - RADAR_MAP_GTA_OFFSET);
    }

    std::size_t GetTileIndex(unsigned int column, unsigned int row)
    {
        return static_cast<std::size_t>(row) * RADAR_MAP_SIZE + column;
    }

    bool VerifyCall(DWORD address, const std::array<unsigned char, 5>& expectedBytes)
    {
        return std::memcmp(reinterpret_cast<const void*>(address), expectedBytes.data(), expectedBytes.size()) == 0;
    }

    RwRenderStateSet_t GetRenderStateSetter()
    {
        const DWORD engine = *reinterpret_cast<const DWORD*>(VAR_RwEngineInstance);
        if (!engine)
            return nullptr;
        return reinterpret_cast<RwRenderStateSet_t>(*reinterpret_cast<const DWORD*>(engine + 0x20));
    }

    void __cdecl DrawExtendedRadarSection(int x, int y);
    void __cdecl StreamExtendedRadarSections(int x, int y);
    void __cdecl StreamExtendedRadarSections(const CVector& position);
}

struct CRadarSA::SExtendedRadar
{
    struct STile
    {
        const void*          owner = nullptr;
        const void*          source = nullptr;
        SString              data;
        bool                 filteringEnabled = true;
        bool                 loaded = false;
        bool                 loadFailed = false;
        SReplacementTextures textures;
    };

    std::array<STile, RADAR_MAP_SIZE * RADAR_MAP_SIZE> tiles;
    bool                                               hooksInstalled = false;

    ~SExtendedRadar()
    {
        for (STile& tile : tiles)
            Unload(tile);
    }

    void Unload(STile& tile)
    {
        if (!tile.textures.textures.empty())
            pGame->GetRenderWare()->ModelInfoTXDRemoveTextures(&tile.textures);
        tile.textures = SReplacementTextures();
        tile.loaded = false;
    }

    bool Load(STile& tile)
    {
        if (!tile.owner || tile.loaded || tile.loadFailed)
            return tile.loaded;

        if (!pGame->GetRenderWare()->ModelInfoTXDLoadTextures(&tile.textures, SString(), tile.data, tile.filteringEnabled) || tile.textures.textures.empty())
        {
            tile.loadFailed = true;
            tile.textures = SReplacementTextures();
            return false;
        }

        tile.loaded = true;
        return true;
    }

    void Clear(STile& tile)
    {
        Unload(tile);
        tile.owner = nullptr;
        tile.source = nullptr;
        SString().swap(tile.data);
        tile.filteringEnabled = true;
        tile.loadFailed = false;
    }

    void UpdateStreaming(int centerX, int centerY)
    {
        for (unsigned int row = 0; row < RADAR_MAP_SIZE; ++row)
        {
            for (unsigned int column = 0; column < RADAR_MAP_SIZE; ++column)
            {
                STile& tile = tiles[GetTileIndex(column, row)];
                if (!tile.owner)
                    continue;

                const int tileX = static_cast<int>(column) - RADAR_MAP_GTA_OFFSET;
                const int tileY = static_cast<int>(row) - RADAR_MAP_GTA_OFFSET;
                const int distance = std::max(std::abs(tileX - centerX), std::abs(tileY - centerY));

                if (distance <= RADAR_MAP_STREAM_RADIUS)
                    Load(tile);
                else if (distance > RADAR_MAP_RETAIN_RADIUS)
                    Unload(tile);
            }
        }
    }

    SRadarMapStats GetStats() const
    {
        SRadarMapStats stats;
        stats.hooksInstalled = hooksInstalled;
        for (const STile& tile : tiles)
        {
            if (!tile.owner)
                continue;
            ++stats.registeredTiles;
            stats.sourceBytes += tile.data.size();
            if (tile.loaded)
                ++stats.loadedTiles;
            if (tile.loadFailed)
                ++stats.failedTiles;
        }
        return stats;
    }
};

namespace
{
    bool InstallExtendedRadarHooks()
    {
        if (pGame->GetGameVersion() != VERSION_US_10)
        {
            OutputReleaseLine("[Radar] Extended radar tiles disabled: unsupported GTA executable");
            return false;
        }

        struct SCallPatch
        {
            DWORD                        address;
            std::array<unsigned char, 5> expectedBytes;
            DWORD                        replacement;
        };

        const std::array<SCallPatch, 11> patches{{
            {0x586976, {0xE8, 0x95, 0xF7, 0xFF, 0xFF}, reinterpret_cast<DWORD>(&DrawExtendedRadarSection)},
            {0x58697D, {0xE8, 0x8E, 0xF7, 0xFF, 0xFF}, reinterpret_cast<DWORD>(&DrawExtendedRadarSection)},
            {0x586987, {0xE8, 0x84, 0xF7, 0xFF, 0xFF}, reinterpret_cast<DWORD>(&DrawExtendedRadarSection)},
            {0x586991, {0xE8, 0x7A, 0xF7, 0xFF, 0xFF}, reinterpret_cast<DWORD>(&DrawExtendedRadarSection)},
            {0x586998, {0xE8, 0x73, 0xF7, 0xFF, 0xFF}, reinterpret_cast<DWORD>(&DrawExtendedRadarSection)},
            {0x5869A2, {0xE8, 0x69, 0xF7, 0xFF, 0xFF}, reinterpret_cast<DWORD>(&DrawExtendedRadarSection)},
            {0x5869AA, {0xE8, 0x61, 0xF7, 0xFF, 0xFF}, reinterpret_cast<DWORD>(&DrawExtendedRadarSection)},
            {0x5869B1, {0xE8, 0x5A, 0xF7, 0xFF, 0xFF}, reinterpret_cast<DWORD>(&DrawExtendedRadarSection)},
            {0x5869B8, {0xE8, 0x53, 0xF7, 0xFF, 0xFF}, reinterpret_cast<DWORD>(&DrawExtendedRadarSection)},
            {0x5868E8, {0xE8, 0x63, 0xE3, 0xFF, 0xFF}, reinterpret_cast<DWORD>(static_cast<void(__cdecl*)(int, int)>(&StreamExtendedRadarSections))},
            {0x40EC92, {0xE8, 0x39, 0x6C, 0x17, 0x00}, reinterpret_cast<DWORD>(static_cast<void(__cdecl*)(const CVector&)>(&StreamExtendedRadarSections))},
        }};

        for (const SCallPatch& patch : patches)
        {
            if (!VerifyCall(patch.address, patch.expectedBytes))
            {
                OutputReleaseLine(SString("[Radar] Extended radar tiles disabled: hook validation failed at 0x%08X", patch.address));
                return false;
            }
        }

        for (const SCallPatch& patch : patches)
            HookInstallCall(patch.address, patch.replacement);

        OutputReleaseLine("[Radar] Extended 40x40 radar tile grid enabled");
        return true;
    }

    void DrawCustomRadarSection(int x, int y, RwTexture* texture)
    {
        CVector2D corners[4];
        CVector2D rotated[4];
        CVector2D clipped[8];
        CVector2D textureCoordinates[8];
        CVector2D screenVertices[8];

        GetTextureCorners(x, y, corners);

        const float      cachedCos = *reinterpret_cast<const float*>(VAR_RadarCachedCos);
        const float      cachedSin = *reinterpret_cast<const float*>(VAR_RadarCachedSin);
        const float      radarRange = *reinterpret_cast<const float*>(VAR_RadarRange);
        const CVector2D& radarOrigin = *reinterpret_cast<const CVector2D*>(VAR_RadarOrigin);
        if (radarRange == 0.0f)
            return;

        for (unsigned int i = 0; i < 4; ++i)
        {
            const float relativeX = (corners[i].fX - radarOrigin.fX) / radarRange;
            const float relativeY = (corners[i].fY - radarOrigin.fY) / radarRange;
            rotated[i].fX = cachedCos * relativeX - cachedSin * relativeY;
            rotated[i].fY = cachedSin * relativeX + cachedCos * relativeY;
        }

        const int vertexCount = ClipRadarPoly(clipped, rotated);
        for (int i = 0; i < vertexCount; ++i)
        {
            const float worldX = radarOrigin.fX + (cachedCos * clipped[i].fX + cachedSin * clipped[i].fY) * radarRange;
            const float worldY = radarOrigin.fY + (-cachedSin * clipped[i].fX + cachedCos * clipped[i].fY) * radarRange;
            textureCoordinates[i].fX = (worldX - (500.0f * x - 3000.0f)) / 500.0f;
            textureCoordinates[i].fY = -(worldY - (500.0f * (12 - y) - 3000.0f)) / 500.0f;
            TransformRadarPointToScreenSpace(&screenVertices[i], &clipped[i]);
        }

        RwRenderStateSet_t renderStateSet = GetRenderStateSetter();
        if (!renderStateSet)
            return;

        if (texture)
        {
            renderStateSet(1 /* rwRENDERSTATETEXTURERASTER */, texture->raster);
            SpriteSetVertices(vertexCount, screenVertices, textureCoordinates, {255, 255, 255, 255});
        }
        else
        {
            renderStateSet(1 /* rwRENDERSTATETEXTURERASTER */, nullptr);
            SpriteSetVertices(vertexCount, screenVertices, textureCoordinates, {111, 137, 170, 255});
        }

        if (vertexCount > 2)
            RwIm2DRenderPrimitive(PRIMITIVE_TRIANGLE_FAN, reinterpret_cast<void*>(VAR_CSprite2dVertices), vertexCount);
    }

    void __cdecl DrawExtendedRadarSection(int x, int y)
    {
        if (IsGtaTile(x, y) || !IsExtendedWorldTile(x, y) || !g_pExtendedRadar)
        {
            DrawRadarSection(x, y);
            return;
        }
        g_pExtendedRadar->DrawMapSection(x, y);
    }

    void __cdecl StreamExtendedRadarSections(int x, int y)
    {
        StreamRadarSectionsXY(x, y);
        if (g_pExtendedRadar)
            g_pExtendedRadar->UpdateMapStreaming(x, y);
    }

    void __cdecl StreamExtendedRadarSections(const CVector& position)
    {
        StreamRadarSectionsVector(position);
        if (!g_pExtendedRadar)
            return;

        const int x = static_cast<int>(std::floor((position.fX + 3000.0f) / 500.0f));
        const int y = static_cast<int>(std::ceil(11.0f - (position.fY + 3000.0f) / 500.0f));
        g_pExtendedRadar->UpdateMapStreaming(x, y);
    }
}

CRadarSA::CRadarSA() : m_ExtendedRadar(std::make_unique<SExtendedRadar>())
{
    for (int i = 0; i < MAX_MARKERS; i++)
        Markers[i] = new CMarkerSA((CMarkerSAInterface*)(ARRAY_CMarker + i * sizeof(CMarkerSAInterface)));

    g_pExtendedRadar = this;
    m_ExtendedRadar->hooksInstalled = InstallExtendedRadarHooks();
}

CRadarSA::~CRadarSA()
{
    g_pExtendedRadar = nullptr;
    for (int i = 0; i < MAX_MARKERS; i++)
    {
        if (Markers[i])
            delete Markers[i];
    }
}

bool CRadarSA::SetMapTile(unsigned int column, unsigned int row, const void* owner, const void* source, const char* data, std::size_t size,
                          bool filteringEnabled)
{
    if (!m_ExtendedRadar->hooksInstalled || !IsRegisterableTile(column, row) || !owner || !source || !data || size == 0)
        return false;

    SExtendedRadar::STile& tile = m_ExtendedRadar->tiles[GetTileIndex(column, row)];
    if (tile.owner && tile.owner != owner)
        return false;

    m_ExtendedRadar->Clear(tile);
    tile.owner = owner;
    tile.source = source;
    tile.data.assign(data, size);
    tile.filteringEnabled = filteringEnabled;
    return true;
}

bool CRadarSA::ResetMapTile(unsigned int column, unsigned int row, const void* owner)
{
    if (!IsRegisterableTile(column, row) || !owner)
        return false;

    SExtendedRadar::STile& tile = m_ExtendedRadar->tiles[GetTileIndex(column, row)];
    if (tile.owner != owner)
        return false;

    m_ExtendedRadar->Clear(tile);
    return true;
}

void CRadarSA::RemoveMapTilesForSource(const void* source)
{
    if (!source)
        return;

    for (SExtendedRadar::STile& tile : m_ExtendedRadar->tiles)
    {
        if (tile.source == source)
            m_ExtendedRadar->Clear(tile);
    }
}

SRadarMapStats CRadarSA::GetMapStats() const
{
    return m_ExtendedRadar->GetStats();
}

void CRadarSA::DrawMapSection(int x, int y)
{
    const unsigned int     column = static_cast<unsigned int>(x + RADAR_MAP_GTA_OFFSET);
    const unsigned int     row = static_cast<unsigned int>(y + RADAR_MAP_GTA_OFFSET);
    SExtendedRadar::STile& tile = m_ExtendedRadar->tiles[GetTileIndex(column, row)];
    if (tile.owner)
    {
        m_ExtendedRadar->Load(tile);
        if (tile.loaded && !tile.textures.textures.empty())
        {
            DrawCustomRadarSection(x, y, tile.textures.textures.front());
            return;
        }
    }

    // GTA's original function already accepts out-of-range section indices and
    // draws its native ocean fallback. Reusing it for an absent server tile
    // keeps the exact same clipping and rounding as adjacent vanilla sections
    // while the radar rotates, preventing seams and transparent wedges.
    DrawRadarSection(x, y);
}

void CRadarSA::UpdateMapStreaming(int centerX, int centerY)
{
    m_ExtendedRadar->UpdateStreaming(centerX, centerY);
}

CMarker* CRadarSA::CreateMarker(CVector* vecPosition)
{
    CMarkerSA* marker;
    marker = (CMarkerSA*)GetFreeMarker();
    if (marker)
    {
        marker->Init();
        marker->SetPosition(vecPosition);
    }

    return marker;
}

CMarker* CRadarSA::GetFreeMarker()
{
    int Index;
    Index = 0;
    while ((Index < MAX_MARKERS) && (Markers[Index]->GetInterface()->bTrackingBlip))
    {
        Index++;
    }
    if (Index >= MAX_MARKERS)
        return NULL;
    else
        return Markers[Index];
}

void CRadarSA::DrawAreaOnRadar(float fX1, float fY1, float fX2, float fY2, const SharedUtil::SColor color)
{
    // Convert color to required abgr at the last moment
    unsigned long abgr = color.A << 24 | color.B << 16 | color.G << 8 | color.R;
    CRect         myRect(fX1, fY2, fX2, fY1);
    DWORD         dwFunc = FUNC_DrawAreaOnRadar;
    // clang-format off
    __asm
    {
        push    eax

        push    1           //bool
        lea     eax, abgr
        push    eax
        lea     eax, myRect
        push    eax
        call    dwFunc
        add     esp, 12

        pop     eax
    }
    // clang-format on
}
