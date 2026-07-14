/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/CPlayerMap.cpp
 *  PURPOSE:     Full screen player map renderer
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include <game/CRadar.h>

using SharedUtil::CalcMTASAPath;
using std::list;

enum
{
    MARKER_SQUARE_INDEX = 0,
    MARKER_UP_TRIANGLE_INDEX = 1,
    MARKER_DOWN_TRIANGLE_INDEX = 2,
    MARKER_FIRST_SPRITE_INDEX = 3,
    MARKER_LAST_SPRITE_INDEX = MARKER_FIRST_SPRITE_INDEX + RADAR_MARKER_LIMIT - 1,
};

constexpr std::array<std::uint32_t, 2> MAP_IMAGE_SIZES = {1024, 2048};

namespace
{
    constexpr unsigned int VANILLA_MAP_MIN_CELL = 14;
    constexpr unsigned int VANILLA_MAP_MAX_CELL = 25;
    constexpr unsigned int VANILLA_MAP_CELL_SPAN = 12;
    constexpr float        RADAR_MAP_WORLD_MIN = -10000.0f;
    constexpr float        RADAR_MAP_WORLD_MAX = 10000.0f;
    constexpr float        RADAR_MAP_TILE_SIZE = 500.0f;
    constexpr DWORD        PLAYER_MAP_OCEAN_COLOR = D3DCOLOR_XRGB(111, 137, 170);
    constexpr DWORD        PLAYER_MAP_TILE_BUILD_BUDGET_MS = 4;
    constexpr unsigned int PLAYER_MAP_TILE_BUILD_BATCH_LIMIT = 16;

    bool IsVanillaMapCell(unsigned int column, unsigned int row)
    {
        return column >= VANILLA_MAP_MIN_CELL && column <= VANILLA_MAP_MAX_CELL && row >= VANILLA_MAP_MIN_CELL && row <= VANILLA_MAP_MAX_CELL;
    }
}

CPlayerMap::CPlayerMap(CClientManager* pManager)
{
    m_failedToLoadTextures = false;

    // Setup our managers
    m_pManager = pManager;
    m_pRadarMarkerManager = pManager->GetRadarMarkerManager();
    m_pRadarAreaManager = m_pManager->GetRadarAreaManager();

    // Set the map bools
    m_bIsPlayerMapEnabled = false;
    m_bForcedState = false;
    m_bIsAttachedToLocal = false;
    m_bHideHelpText = false;

    // Set the movement bools
    m_bIsMovingNorth = false;
    m_bIsMovingSouth = false;
    m_bIsMovingEast = false;
    m_bIsMovingWest = false;
    m_bTextVisible = false;

    // Set the update time to the current time
    m_ulUpdateTime = GetTickCount32();

    // Get the window sizes and set the map variables to default zoom/movement
    m_uiHeight = g_pCore->GetGraphics()->GetViewportHeight();
    m_uiWidth = g_pCore->GetGraphics()->GetViewportWidth();
    m_fZoom = 1;
    m_iHorizontalMovement = 0;
    m_iVerticalMovement = 0;

    // Init texture vars
    m_mapImageTexture = nullptr;
    m_dynamicMapTexture = nullptr;
    m_playerMarkerTexture = nullptr;
    m_mapRegistryRevision = 0;
    m_dynamicMapTextureRevision = 0;
    m_dynamicMapBuildIndex = 0;
    m_registeredMapTileCount = 0;
    m_mapGridMinColumn = VANILLA_MAP_MIN_CELL;
    m_mapGridMaxColumn = VANILLA_MAP_MAX_CELL;
    m_mapGridMinRow = VANILLA_MAP_MIN_CELL;
    m_mapGridMaxRow = VANILLA_MAP_MAX_CELL;
    m_mapGridSpan = VANILLA_MAP_CELL_SPAN;
    m_dynamicMapNeedsReset = false;
    m_dynamicMapBuildPending = false;
    m_fWorldMinX = -3000.0f;
    m_fWorldMaxY = 3000.0f;
    m_fWorldSize = 6000.0f;

    // Create all map textures
    CreateAllTextures();

    // Create the text displays for the help text
    const SColorRGBA colorWhiteTransparent(255, 255, 255, 200);
    const SColorRGBA colorWhite(255, 255, 255, 255);
    struct
    {
        SColor  color;
        float   fPosY;
        float   fScale;
        SString strMessage;
    } messageList[] = {
        {colorWhiteTransparent, 0.92f, 1.5f, ""},
        {colorWhite, 0.95f, 1.0f, SString(_("Change mode: %s"), *GetBoundKeyName("radar_attach"))},

        {colorWhite, 0.05f, 1.0f,
         SString(_("Zoom: %s/%s     Movement: %s, %s, %s, %s     Opacity: %s/%s"), *GetBoundKeyName("radar_zoom_in"), *GetBoundKeyName("radar_zoom_out"),
                 *GetBoundKeyName("radar_move_north"), *GetBoundKeyName("radar_move_east"), *GetBoundKeyName("radar_move_south"),
                 *GetBoundKeyName("radar_move_west"), *GetBoundKeyName("radar_opacity_down"), *GetBoundKeyName("radar_opacity_up"))},
        {colorWhite, 0.07f, 1.0f, SString(_("Toggle map: %s     Toggle help text: %s"), *GetBoundKeyName("radar"), *GetBoundKeyName("radar_help"))},
    };

    for (uint i = 0; i < NUMELMS(messageList); i++)
    {
        auto pTextDisplay = m_pManager->GetDisplayManager()->CreateTextDisplay();
        pTextDisplay->SetCaption(messageList[i].strMessage);
        pTextDisplay->SetColor(messageList[i].color);
        pTextDisplay->SetPosition(CVector(0.50f, messageList[i].fPosY, 0));
        pTextDisplay->SetFormat(DT_CENTER | DT_VCENTER);
        pTextDisplay->SetScale(messageList[i].fScale);
        pTextDisplay->SetVisible(false);

        m_HelpTextList.push_back(pTextDisplay);
    }

    // Default to attached to player
    SetAttachedToLocalPlayer(true);

    RefreshMapDefinition();
    SetupMapVariables();
}

CPlayerMap::~CPlayerMap()
{
    // Delete our images
    ReleaseDynamicMapTexture();
    SAFE_RELEASE(m_mapImageTexture);
    SAFE_RELEASE(m_playerMarkerTexture);
    for (uint i = 0; i < m_markerTextureList.size(); i++)
        SAFE_RELEASE(m_markerTextureList[i]);
    m_markerTextureList.clear();
    m_HelpTextList.clear();
}

void CPlayerMap::CreateOrUpdateMapTexture()
{
    const std::uint32_t mapSize = MAP_IMAGE_SIZES[m_playerMapImageIndex];
    const SString       fileName("MTA\\cgui\\images\\map_%d.png", mapSize);

    auto* newTexture = g_pCore->GetGraphics()->GetRenderItemManager()->CreateTexture(CalcMTASAPath(fileName), nullptr, false, mapSize, mapSize, RFORMAT_DXT1);
    if (!newTexture)
        throw std::runtime_error("Failed to load map image");

    SAFE_RELEASE(m_mapImageTexture);
    m_mapImageTexture = newTexture;
}

void CPlayerMap::UpdateOrRevertMapTexture(std::size_t newImageIndex)
{
    const std::size_t oldImageIndex = m_playerMapImageIndex;
    try
    {
        m_playerMapImageIndex = newImageIndex;
        CreateOrUpdateMapTexture();
        m_dynamicMapNeedsReset = true;
    }
    catch (const std::exception& e)
    {
        m_playerMapImageIndex = oldImageIndex;
        g_pCore->GetConsole()->Printf("Problem updating map image: %s", e.what());
    }
}

void CPlayerMap::CreatePlayerBlipTexture()
{
    m_playerMarkerTexture = g_pCore->GetGraphics()->GetRenderItemManager()->CreateTexture(CalcMTASAPath("MTA\\cgui\\images\\radarset\\02.png"));
    if (!m_playerMarkerTexture)
        throw std::runtime_error("Failed to load player blip image");
}

void CPlayerMap::CreateAllTextures()
{
    try
    {
        // Create the map texture
        m_playerMapImageIndex = g_pCore->GetCVars()->GetValue<std::size_t>("mapimage");
        CreateOrUpdateMapTexture();

        // Create the player blip texture
        CreatePlayerBlipTexture();

        // Create the other marker textures
        CreateMarkerTextures();
    }
    catch (const std::exception& e)
    {
        m_failedToLoadTextures = true;
        g_pCore->GetConsole()->Printf("Problem initializing player map: %s", e.what());
    }
}

void CPlayerMap::RefreshMapDefinition()
{
    CRadar* radar = g_pGame->GetRadar();
    if (!radar)
        return;

    const std::uint32_t revision = radar->GetMapRevision();
    if (revision == m_mapRegistryRevision)
        return;

    const SRadarMapStats stats = radar->GetMapStats();
    m_mapRegistryRevision = stats.revision;
    m_registeredMapTileCount = stats.registeredTiles;

    unsigned int minColumn = VANILLA_MAP_MIN_CELL;
    unsigned int maxColumn = VANILLA_MAP_MAX_CELL;
    unsigned int minRow = VANILLA_MAP_MIN_CELL;
    unsigned int maxRow = VANILLA_MAP_MAX_CELL;
    if (stats.registeredTiles > 0)
    {
        minColumn = std::min(minColumn, stats.minColumn);
        maxColumn = std::max(maxColumn, stats.maxColumn);
        minRow = std::min(minRow, stats.minRow);
        maxRow = std::max(maxRow, stats.maxRow);
    }

    const unsigned int columnSpan = maxColumn - minColumn + 1;
    const unsigned int rowSpan = maxRow - minRow + 1;
    const unsigned int squareSpan = std::max(columnSpan, rowSpan);

    auto ExpandAxis = [squareSpan](unsigned int& minimum, unsigned int& maximum)
    {
        const unsigned int currentSpan = maximum - minimum + 1;
        int                targetMinimum = static_cast<int>(minimum) - static_cast<int>((squareSpan - currentSpan) / 2);
        targetMinimum = std::max(0, std::min(targetMinimum, static_cast<int>(CRadar::MAP_GRID_SIZE - squareSpan)));
        minimum = static_cast<unsigned int>(targetMinimum);
        maximum = minimum + squareSpan - 1;
    };
    ExpandAxis(minColumn, maxColumn);
    ExpandAxis(minRow, maxRow);

    const bool boundsChanged = minColumn != m_mapGridMinColumn || maxColumn != m_mapGridMaxColumn || minRow != m_mapGridMinRow || maxRow != m_mapGridMaxRow;
    m_mapGridMinColumn = minColumn;
    m_mapGridMaxColumn = maxColumn;
    m_mapGridMinRow = minRow;
    m_mapGridMaxRow = maxRow;
    m_mapGridSpan = squareSpan;
    m_fWorldMinX = RADAR_MAP_WORLD_MIN + static_cast<float>(minColumn) * RADAR_MAP_TILE_SIZE;
    m_fWorldMaxY = RADAR_MAP_WORLD_MAX - static_cast<float>(minRow) * RADAR_MAP_TILE_SIZE;
    m_fWorldSize = static_cast<float>(squareSpan) * RADAR_MAP_TILE_SIZE;

    m_dynamicMapNeedsReset = true;
    m_dynamicMapBuildPending = true;
    m_dynamicMapBuildIndex = 0;

    if (boundsChanged)
    {
        m_iHorizontalMovement = 0;
        m_iVerticalMovement = 0;
        SetupMapVariables();
    }
}

void CPlayerMap::ReleaseDynamicMapTexture()
{
    SAFE_RELEASE(m_dynamicMapTexture);
    m_dynamicMapTextureRevision = 0;
    m_dynamicMapBuildIndex = 0;
    m_dynamicMapNeedsReset = false;
    m_dynamicMapBuildPending = false;
}

bool CPlayerMap::ResetDynamicMapTexture()
{
    if (!m_mapImageTexture)
    {
        m_dynamicMapNeedsReset = true;
        return false;
    }

    const std::uint32_t textureSize = MAP_IMAGE_SIZES[m_playerMapImageIndex];
    if (m_dynamicMapTexture && (m_dynamicMapTexture->m_uiSizeX != textureSize || m_dynamicMapTexture->m_uiSizeY != textureSize))
        ReleaseDynamicMapTexture();

    CRenderItemManagerInterface* renderItemManager = g_pCore->GetGraphics()->GetRenderItemManager();
    if (!m_dynamicMapTexture)
        m_dynamicMapTexture = renderItemManager->CreateRenderTarget(textureSize, textureSize, false, false, 0, true);
    if (!m_dynamicMapTexture || !renderItemManager->SetRenderTarget(m_dynamicMapTexture, true))
    {
        m_dynamicMapNeedsReset = true;
        return false;
    }

    IDirect3DDevice9* device = g_pCore->GetGraphics()->GetDevice();
    if (device)
        device->Clear(0, nullptr, D3DCLEAR_TARGET, PLAYER_MAP_OCEAN_COLOR, 1.0f, 0);

    const float tileSize = static_cast<float>(textureSize) / static_cast<float>(m_mapGridSpan);
    const float vanillaLeft = static_cast<float>(VANILLA_MAP_MIN_CELL - m_mapGridMinColumn) * tileSize;
    const float vanillaTop = static_cast<float>(VANILLA_MAP_MIN_CELL - m_mapGridMinRow) * tileSize;
    const float vanillaSize = static_cast<float>(VANILLA_MAP_CELL_SPAN) * tileSize;
    g_pCore->GetGraphics()->DrawTexture(m_mapImageTexture, vanillaLeft, vanillaTop, vanillaSize / m_mapImageTexture->m_uiSizeX,
                                        vanillaSize / m_mapImageTexture->m_uiSizeY);

    renderItemManager->RestoreDefaultRenderTarget();
    m_dynamicMapTextureRevision = m_dynamicMapTexture->GetRevision();
    m_dynamicMapBuildIndex = 0;
    m_dynamicMapNeedsReset = false;
    m_dynamicMapBuildPending = true;
    return true;
}

void CPlayerMap::ContinueDynamicMapTextureBuild()
{
    if (m_dynamicMapTexture && m_dynamicMapTextureRevision != m_dynamicMapTexture->GetRevision())
        m_dynamicMapNeedsReset = true;
    if (m_dynamicMapNeedsReset && !ResetDynamicMapTexture())
        return;
    if (!m_dynamicMapTexture || !m_dynamicMapBuildPending)
        return;

    struct SAcquiredTile
    {
        unsigned int       index;
        IDirect3DTexture9* texture;
        bool               unloadAfterUse;
    };
    std::vector<unsigned int> candidateIndices;
    std::vector<SAcquiredTile> acquiredTiles;

    CRadar*      radar = g_pGame->GetRadar();
    const DWORD  startTime = GetTickCount32();
    unsigned int attemptedTiles = 0;
    while (m_dynamicMapBuildIndex < CRadar::MAP_GRID_SIZE * CRadar::MAP_GRID_SIZE)
    {
        const unsigned int index = m_dynamicMapBuildIndex++;
        const unsigned int column = index % CRadar::MAP_GRID_SIZE;
        const unsigned int row = index / CRadar::MAP_GRID_SIZE;
        if (!IsVanillaMapCell(column, row) && !radar->IsMapTileRegistered(column, row))
            continue;

        ++attemptedTiles;
        candidateIndices.push_back(index);

        if (attemptedTiles >= PLAYER_MAP_TILE_BUILD_BATCH_LIMIT || GetTickCount32() - startTime >= PLAYER_MAP_TILE_BUILD_BUDGET_MS)
            break;
    }

    if (!candidateIndices.empty())
    {
        std::vector<unsigned int> columns;
        std::vector<unsigned int> rows;
        columns.reserve(candidateIndices.size());
        rows.reserve(candidateIndices.size());
        for (const unsigned int index : candidateIndices)
        {
            columns.push_back(index % CRadar::MAP_GRID_SIZE);
            rows.push_back(index / CRadar::MAP_GRID_SIZE);
        }

        radar->PrepareMapTileTextures(columns.data(), rows.data(), columns.size());
        for (std::size_t i = 0; i < candidateIndices.size(); ++i)
        {
            bool unloadAfterUse = false;
            if (IDirect3DTexture9* texture = radar->AcquireMapTileTexture(columns[i], rows[i], unloadAfterUse))
                acquiredTiles.push_back({candidateIndices[i], texture, unloadAfterUse});
        }
    }

    if (m_dynamicMapBuildIndex >= CRadar::MAP_GRID_SIZE * CRadar::MAP_GRID_SIZE)
        m_dynamicMapBuildPending = false;
    if (acquiredTiles.empty())
        return;

    CRenderItemManagerInterface* renderItemManager = g_pCore->GetGraphics()->GetRenderItemManager();
    IDirect3DDevice9*            device = g_pCore->GetGraphics()->GetDevice();
    if (!device || !renderItemManager->SetRenderTarget(m_dynamicMapTexture, false))
    {
        m_dynamicMapBuildIndex = acquiredTiles.front().index;
        m_dynamicMapBuildPending = true;
        for (const SAcquiredTile& tile : acquiredTiles)
            radar->ReleaseMapTileTexture(tile.index % CRadar::MAP_GRID_SIZE, tile.index / CRadar::MAP_GRID_SIZE, tile.texture, tile.unloadAfterUse);
        return;
    }

    const float tileSize = static_cast<float>(m_dynamicMapTexture->m_uiSizeX) / static_cast<float>(m_mapGridSpan);
    for (const SAcquiredTile& tile : acquiredTiles)
    {
        const unsigned int column = tile.index % CRadar::MAP_GRID_SIZE;
        const unsigned int row = tile.index / CRadar::MAP_GRID_SIZE;
        const float        left = static_cast<float>(column - m_mapGridMinColumn) * tileSize;
        const float        top = static_cast<float>(row - m_mapGridMinRow) * tileSize;
        D3DSURFACE_DESC     description;
        if (SUCCEEDED(tile.texture->GetLevelDesc(0, &description)))
            g_pCore->GetGraphics()->DrawTextureRaw(tile.texture, description.Width, description.Height, left, top, tileSize, tileSize);
    }

    renderItemManager->RestoreDefaultRenderTarget();
    for (const SAcquiredTile& tile : acquiredTiles)
        radar->ReleaseMapTileTexture(tile.index % CRadar::MAP_GRID_SIZE, tile.index / CRadar::MAP_GRID_SIZE, tile.texture, tile.unloadAfterUse);
    m_dynamicMapTextureRevision = m_dynamicMapTexture->GetRevision();
}

void CPlayerMap::DrawMapBackground(const SColorARGB& color)
{
    if (m_dynamicMapTexture)
    {
        g_pCore->GetGraphics()->DrawTexture(m_dynamicMapTexture, static_cast<float>(m_iMapMinX), static_cast<float>(m_iMapMinY),
                                            m_fMapSize / m_dynamicMapTexture->m_uiSizeX, m_fMapSize / m_dynamicMapTexture->m_uiSizeY, 0.0f, 0.0f, 0.0f, color);
        return;
    }

    // Graceful low-memory fallback while preserving the dynamic world scale.
    g_pCore->GetGraphics()->DrawRectangle(static_cast<float>(m_iMapMinX), static_cast<float>(m_iMapMinY), m_fMapSize, m_fMapSize,
                                          SColorARGB(color.A, 111, 137, 170));
    const float tileSize = m_fMapSize / static_cast<float>(m_mapGridSpan);
    const float vanillaLeft = static_cast<float>(m_iMapMinX) + static_cast<float>(VANILLA_MAP_MIN_CELL - m_mapGridMinColumn) * tileSize;
    const float vanillaTop = static_cast<float>(m_iMapMinY) + static_cast<float>(VANILLA_MAP_MIN_CELL - m_mapGridMinRow) * tileSize;
    const float vanillaSize = static_cast<float>(VANILLA_MAP_CELL_SPAN) * tileSize;
    g_pCore->GetGraphics()->DrawTexture(m_mapImageTexture, vanillaLeft, vanillaTop, vanillaSize / m_mapImageTexture->m_uiSizeX,
                                        vanillaSize / m_mapImageTexture->m_uiSizeY, 0.0f, 0.0f, 0.0f, color);
}

void CPlayerMap::DoPulse()
{
    RefreshMapDefinition();
    const uint uiViewportWidth = g_pCore->GetGraphics()->GetViewportWidth();
    const uint uiViewportHeight = g_pCore->GetGraphics()->GetViewportHeight();
    if (uiViewportWidth > 0 && uiViewportHeight > 0 && (m_bPendingViewportRefresh || m_uiWidth != uiViewportWidth || m_uiHeight != uiViewportHeight))
    {
        m_uiWidth = uiViewportWidth;
        m_uiHeight = uiViewportHeight;
        SetupMapVariables();
        m_bPendingViewportRefresh = false;
    }

    // If our map image exists
    if (IsPlayerMapShowing())
    {
        // If we are following the local player blip
        if (m_bIsAttachedToLocal)
        {
            // Get the latest vars for the map
            SetupMapVariables();
        }

        // If the update time is more than 50ms behind
        if (GetTickCount32() >= m_ulUpdateTime + 50)
        {
            // Set the update time
            m_ulUpdateTime = GetTickCount32();

            // If we are set to moving then do a zoom/move level jump
            if (m_bIsMovingNorth)
            {
                MoveNorth();
            }
            else if (m_bIsMovingSouth)
            {
                MoveSouth();
            }
            else if (m_bIsMovingEast)
            {
                MoveEast();
            }
            else if (m_bIsMovingWest)
            {
                MoveWest();
            }
        }
    }
}

void CPlayerMap::MarkViewportRefreshPending()
{
    m_bPendingViewportRefresh = true;
}

void CPlayerMap::ClearMovementFlags()
{
    m_bIsMovingNorth = false;
    m_bIsMovingSouth = false;
    m_bIsMovingEast = false;
    m_bIsMovingWest = false;
}

//
// Precreate all the textures for the player map markers
//
void CPlayerMap::CreateMarkerTextures()
{
    m_markerTextureList.clear();
    SString strRadarSetDirectory = CalcMTASAPath("MTA\\cgui\\images\\radarset\\");

    // Load the 3 shapes
    const char* shapeFileNames[] = {"square.png", "up.png", "down.png"};
    for (uint i = 0; i < NUMELMS(shapeFileNames); i++)
    {
        CTextureItem* pTextureItem = g_pCore->GetGraphics()->GetRenderItemManager()->CreateTexture(PathJoin(strRadarSetDirectory, shapeFileNames[i]));
        m_markerTextureList.push_back(pTextureItem);
    }

    if (m_markerTextureList.size() != MARKER_FIRST_SPRITE_INDEX)
        throw std::runtime_error("Failed to load marker textures [1]");

    // Load the icons
    for (uint i = 0; i < RADAR_MARKER_LIMIT; i++)
    {
        CTextureItem* pTextureItem = g_pCore->GetGraphics()->GetRenderItemManager()->CreateTexture(PathJoin(strRadarSetDirectory, SString("%02u.png", i + 1)));
        m_markerTextureList.push_back(pTextureItem);
    }

    if (m_markerTextureList.size() != MARKER_LAST_SPRITE_INDEX + 1)
        throw std::runtime_error("Failed to load marker textures [2]");
}

//
// Get a texture for a marker, including scale and color
//
CTextureItem* CPlayerMap::GetMarkerTexture(CClientRadarMarker* pMarker, float fLocalZ, float* pfScale, SColor* pColor)
{
    float  fScale = pMarker->GetScale();
    ulong  ulSprite = pMarker->GetSprite();
    SColor color = pMarker->GetColor();

    // Make list index
    uint uiListIndex = 0;

    if (ulSprite)
    {
        // ulSprite >= 1 and <= 63
        // Remap to texture list index
        uiListIndex = ulSprite - 1 + MARKER_FIRST_SPRITE_INDEX;
        color = SColorARGB(255, 255, 255, 255);
        fScale = 1;
    }
    else
    {
        // ulSprite == 0 so draw a square or triangle depending on relative z position
        CVector vecMarker;
        pMarker->GetPosition(vecMarker);

        if (fLocalZ > vecMarker.fZ + 4.0f)
            uiListIndex = MARKER_DOWN_TRIANGLE_INDEX;  // We're higher than this marker, so draw the arrow pointing down
        else if (fLocalZ < vecMarker.fZ - 4.0f)
            uiListIndex = MARKER_UP_TRIANGLE_INDEX;  // We're lower than this entity, so draw the arrow pointing up
        else
            uiListIndex = MARKER_SQUARE_INDEX;  // We're at the same level so draw a square

        fScale /= 4;
    }

    *pfScale = fScale;
    *pColor = color;

    if (uiListIndex >= m_markerTextureList.size())
        return NULL;

    return m_markerTextureList[uiListIndex];
}

void CPlayerMap::DoRender()
{
    bool isMapShowing = IsPlayerMapShowing();
    if (isMapShowing)
    {
        g_pCore->GetGraphics()->RefreshViewportIfNeeded();
        if (!g_pCore->GetGraphics()->GetRenderItemManager()->IsUsingDefaultRenderTarget())
        {
            g_pCore->GetGraphics()->GetRenderItemManager()->RestoreDefaultRenderTarget();
        }
        g_pCore->GetGraphics()->ApplyMTARenderViewportIfNeeded();
        const uint uiViewportWidth = g_pCore->GetGraphics()->GetViewportWidth();
        const uint uiViewportHeight = g_pCore->GetGraphics()->GetViewportHeight();
        if (uiViewportWidth > 0 && uiViewportHeight > 0 && (m_bPendingViewportRefresh || m_uiWidth != uiViewportWidth || m_uiHeight != uiViewportHeight))
        {
            m_uiWidth = uiViewportWidth;
            m_uiHeight = uiViewportHeight;
            SetupMapVariables();
            m_bPendingViewportRefresh = false;
        }
    }

    // Render if showing and textures are all loaded
    if (isMapShowing && !m_failedToLoadTextures)
    {
        IDirect3DDevice9* pDevice = g_pCore->GetGraphics()->GetDevice();
        D3DVIEWPORT9      prevViewport = {};
        RECT              prevScissor = {};
        DWORD             prevScissorEnable = FALSE;
        bool              restoreViewport = false;
        bool              restoreScissor = false;

        if (pDevice && m_uiWidth > 0 && m_uiHeight > 0)
        {
            if (SUCCEEDED(pDevice->GetViewport(&prevViewport)))
                restoreViewport = true;
            if (SUCCEEDED(pDevice->GetScissorRect(&prevScissor)))
                restoreScissor = true;
            pDevice->GetRenderState(D3DRS_SCISSORTESTENABLE, &prevScissorEnable);

            D3DVIEWPORT9 viewport = {};
            viewport.X = 0;
            viewport.Y = 0;
            viewport.Width = m_uiWidth;
            viewport.Height = m_uiHeight;
            viewport.MinZ = 0.0f;
            viewport.MaxZ = 1.0f;
            pDevice->SetViewport(&viewport);

            RECT fullRect = {0, 0, static_cast<LONG>(m_uiWidth), static_cast<LONG>(m_uiHeight)};
            pDevice->SetScissorRect(&fullRect);
            pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
        }

        // Get the alpha value from the settings
        int mapAlpha;
        g_pCore->GetCVars()->Get("mapalpha", mapAlpha);
        const SColorARGB mapColor(mapAlpha, 255, 255, 255);

        // Update the image if the user changed it via a setting
        auto mapImageIndex = g_pCore->GetCVars()->GetValue<std::size_t>("mapimage");
        if (mapImageIndex != m_playerMapImageIndex)
        {
            UpdateOrRevertMapTexture(mapImageIndex);
        }

        ContinueDynamicMapTextureBuild();
        g_pCore->GetGraphics()->ApplyMTARenderViewportIfNeeded();
        DrawMapBackground(mapColor);

        // Grab the info for the local player blip
        CVector2D vecLocalPos;
        CVector   vecLocal;
        CVector   vecLocalRot;
        if (m_pManager->GetCamera()->IsInFixedMode())
        {
            m_pManager->GetCamera()->GetPosition(vecLocal);
            m_pManager->GetCamera()->GetRotationDegrees(vecLocalRot);
        }
        else
        {
            CClientPlayer* pLocalPlayer = m_pManager->GetPlayerManager()->GetLocalPlayer();
            if (!pLocalPlayer)
                return;
            pLocalPlayer->GetPosition(vecLocal);
            pLocalPlayer->GetRotationDegrees(vecLocalRot);
        }

        CalculateEntityOnScreenPosition(vecLocal, vecLocalPos);

        // Now loop our radar areas
        unsigned short                          usDimension = m_pRadarAreaManager->GetDimension();
        CClientRadarArea*                       pArea = NULL;
        list<CClientRadarArea*>::const_iterator areaIter = m_pRadarAreaManager->IterBegin();
        for (; areaIter != m_pRadarAreaManager->IterEnd(); ++areaIter)
        {
            pArea = *areaIter;

            if (pArea->GetDimension() == usDimension)
            {
                // Grab the area image and calculate the position to put it on the screen
                CVector2D vecPos;
                CalculateEntityOnScreenPosition(pArea, vecPos);

                // Get the area size and work out the ratio
                CVector2D vecSize;
                float     fX = (*areaIter)->GetSize().fX;
                float     fY = (*areaIter)->GetSize().fY;
                float     fRatio = m_fWorldSize / m_fMapSize;

                // Calculate the size of the area
                vecSize.fX = static_cast<float>(fX / fRatio);
                vecSize.fY = static_cast<float>(fY / fRatio);

                SColor color = pArea->GetColor();
                if (pArea->IsFlashing())
                {
                    color.A = static_cast<unsigned char>(color.A * pArea->GetAlphaFactor());
                }

                g_pCore->GetGraphics()->DrawRectangle(vecPos.fX, vecPos.fY, vecSize.fX, -vecSize.fY, color);
            }
        }

        // Now loop our radar markers
        usDimension = m_pRadarMarkerManager->GetDimension();
        list<CClientRadarMarker*>::const_iterator markerIter = m_pRadarMarkerManager->IterBegin();
        for (; markerIter != m_pRadarMarkerManager->IterEnd(); ++markerIter)
        {
            if ((*markerIter)->IsVisible() && (*markerIter)->GetDimension() == usDimension)
            {
                // Grab the marker image and calculate the position to put it on the screen
                float         fScale = 1;
                SColor        color;
                CTextureItem* pTexture = GetMarkerTexture(*markerIter, vecLocal.fZ, &fScale, &color);

                if (pTexture)
                {
                    CVector2D vecPos;
                    CalculateEntityOnScreenPosition(*markerIter, vecPos);
                    g_pCore->GetGraphics()->DrawTexture(pTexture, vecPos.fX, vecPos.fY, fScale, fScale, 0.0f, 0.5f, 0.5f, color);
                }
            }
        }

        g_pCore->GetGraphics()->DrawTexture(m_playerMarkerTexture, vecLocalPos.fX, vecLocalPos.fY, 1.0, 1.0, vecLocalRot.fZ, 0.5f, 0.5f);

        if (pDevice)
        {
            if (restoreViewport)
                pDevice->SetViewport(&prevViewport);
            if (restoreScissor)
                pDevice->SetScissorRect(&prevScissor);
            pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, prevScissorEnable);
        }
    }

    // Update visibility of help text
    bool bRequiredTextVisible = isMapShowing && !m_bHideHelpText;
    if (bRequiredTextVisible != m_bTextVisible)
    {
        m_bTextVisible = bRequiredTextVisible;
        for (uint i = 0; i < m_HelpTextList.size(); i++)
            m_HelpTextList[i]->SetVisible(m_bTextVisible);

        SetupMapVariables();
    }
}

void CPlayerMap::SetPlayerMapEnabled(bool show)
{
    bool alreadyEnabled = (m_bIsPlayerMapEnabled || m_bForcedState);
    bool definitiveShow = (show || m_bForcedState);
    if (alreadyEnabled != definitiveShow)
    {
        InternalSetPlayerMapEnabled(definitiveShow);
    }
    m_bIsPlayerMapEnabled = show;
}

void CPlayerMap::SetForcedState(bool state)
{
    bool currState = (m_bIsPlayerMapEnabled || m_bForcedState);
    bool definitiveState = (m_bIsPlayerMapEnabled || state);
    if (currState != definitiveState)
    {
        InternalSetPlayerMapEnabled(definitiveState);
    }
    m_bForcedState = state;
}

void CPlayerMap::InternalSetPlayerMapEnabled(bool enable)
{
    if (enable)
    {
        m_bChatVisible = g_pCore->IsChatVisible();
        m_bChatInputBlocked = g_pCore->IsChatInputBlocked();
        m_bDebugVisible = g_pCore->IsDebugVisible();

        g_pGame->GetHud()->Disable(true);
        g_pMultiplayer->HideRadar(true);
        g_pCore->SetChatVisible(false);
        g_pCore->SetDebugVisible(false);
    }
    else
    {
        g_pGame->GetHud()->Disable(false);
        g_pMultiplayer->HideRadar(false);
        g_pCore->SetChatVisible(m_bChatVisible, m_bChatInputBlocked);
        g_pCore->SetDebugVisible(m_bDebugVisible);
    }
}

bool CPlayerMap::CalculateEntityOnScreenPosition(CClientEntity* pEntity, CVector2D& vecLocalPos)
{
    // If the entity exists
    if (pEntity)
    {
        // Get the Entities ingame position
        CVector vecPosition;
        pEntity->GetPosition(vecPosition);

        // Adjust to the map variables and create the map ratio
        float fX = vecPosition.fX - m_fWorldMinX;
        float fY = m_fWorldMaxY - vecPosition.fY;
        float fRatio = m_fWorldSize / m_fMapSize;

        // Calculate the screen position for the marker
        vecLocalPos.fX = static_cast<float>(m_iMapMinX) + (fX / fRatio);
        vecLocalPos.fY = static_cast<float>(m_iMapMinY) + (fY / fRatio);

        // If the position is on the screen
        if (vecLocalPos.fX >= 0.0f && vecLocalPos.fX <= static_cast<float>(m_uiWidth) && vecLocalPos.fY >= 0.0f &&
            vecLocalPos.fY <= static_cast<float>(m_uiHeight))
        {
            // Then return true as it is on the screen
            return true;
        }
    }

    // Return false as it is not on the screen
    return false;
}

bool CPlayerMap::CalculateEntityOnScreenPosition(CVector vecPosition, CVector2D& vecLocalPos)
{
    // Adjust to the map variables and create the map ratio
    float fX = vecPosition.fX - m_fWorldMinX;
    float fY = m_fWorldMaxY - vecPosition.fY;
    float fRatio = m_fWorldSize / m_fMapSize;

    // Calculate the screen position for the marker
    vecLocalPos.fX = static_cast<float>(m_iMapMinX) + (fX / fRatio);
    vecLocalPos.fY = static_cast<float>(m_iMapMinY) + (fY / fRatio);

    // If the position is on the screen
    if (vecLocalPos.fX >= 0.0f && vecLocalPos.fX <= static_cast<float>(m_uiWidth) && vecLocalPos.fY >= 0.0f && vecLocalPos.fY <= static_cast<float>(m_uiHeight))
    {
        // Then return true as it is on the screen
        return true;
    }

    // Return false as it is not on the screen
    return false;
}

void CPlayerMap::SetupMapVariables()
{
    // Calculate the map size and the middle of the screen coords
    m_fMapSize = static_cast<float>(m_uiHeight * m_fZoom);
    int iMiddleX = static_cast<int>(m_uiWidth / 2);
    int iMiddleY = static_cast<int>(m_uiHeight / 2);

    // If we are attached to the local player and zoomed in at all
    if (m_bIsAttachedToLocal && m_fZoom > 1)
    {
        // Get the local player position
        CVector        vec;
        CClientPlayer* pLocalPlayer = m_pManager->GetPlayerManager()->GetLocalPlayer();
        if (pLocalPlayer)
            pLocalPlayer->GetPosition(vec);

        // Calculate the maps min and max vector positions putting the local player in the middle of the map
        m_iMapMinX = static_cast<int>(iMiddleX - ((vec.fX - m_fWorldMinX) * m_fMapSize) / m_fWorldSize);
        m_iMapMaxX = static_cast<int>(m_iMapMinX + m_fMapSize);
        m_iMapMinY = static_cast<int>(iMiddleY - ((m_fWorldMaxY - vec.fY) * m_fMapSize) / m_fWorldSize);
        m_iMapMaxY = static_cast<int>(m_iMapMinY + m_fMapSize);

        // If we are moving the map too far then stop centering the local player blip
        if (m_iMapMinX > 0)
        {
            m_iMapMinX = 0;
            m_iMapMaxX = static_cast<int>(m_iMapMinX + m_fMapSize);
        }
        else if (m_iMapMaxX <= static_cast<int>(m_uiWidth))
        {
            m_iMapMaxX = m_uiWidth;
            m_iMapMinX = static_cast<int>(m_iMapMaxX - m_fMapSize);
        }

        if (m_iMapMinY > 0)
        {
            m_iMapMinY = 0;
            m_iMapMaxY = static_cast<int>(m_iMapMinY + m_fMapSize);
        }
        else if (m_iMapMaxY <= static_cast<int>(m_uiHeight))
        {
            m_iMapMaxY = m_uiHeight;
            m_iMapMinY = static_cast<int>(m_iMapMaxY - m_fMapSize);
        }
    }
    // If we are in free roam mode or not zoomed in
    else
    {
        // Set the maps min and max vector positions relative to the movement selected
        m_iMapMinX = static_cast<int>(iMiddleX - (iMiddleY * m_fZoom) - ((m_iHorizontalMovement * m_fMapSize) / m_fWorldSize));
        m_iMapMaxX = static_cast<int>(m_iMapMinX + m_fMapSize);
        m_iMapMinY = static_cast<int>(iMiddleY - (iMiddleY * m_fZoom) + ((m_iVerticalMovement * m_fMapSize) / m_fWorldSize));
        m_iMapMaxY = static_cast<int>(m_iMapMinY + m_fMapSize);

        // If we are zoomed in
        if (m_fZoom > 1)
        {
            if (m_iMapMinX >= 0)
            {
                m_iMapMinX = 0;
                m_iMapMaxX = static_cast<int>(m_iMapMinX + m_fMapSize);
            }
            else if (m_iMapMaxX <= static_cast<int>(m_uiWidth))
            {
                m_iMapMaxX = m_uiWidth;
                m_iMapMinX = static_cast<int>(m_iMapMaxX - m_fMapSize);
            }

            if (m_iMapMinY >= 0)
            {
                m_iMapMinY = 0;
                m_iMapMaxY = static_cast<int>(m_iMapMinY + m_fMapSize);
            }
            else if (m_iMapMaxY <= static_cast<int>(m_uiHeight))
            {
                m_iMapMaxY = m_uiHeight;
                m_iMapMinY = static_cast<int>(m_iMapMaxY - m_fMapSize);
            }
        }
        // If we are not zoomed in
        else
        {
            // Set the movement margins to 0
            m_iHorizontalMovement = 0;
            m_iVerticalMovement = 0;
        }
    }

    // Show mode only when zoomed in
    if (!m_HelpTextList.empty())
    {
        m_HelpTextList[0]->SetVisible(m_fZoom > 1 && m_bTextVisible);
        m_HelpTextList[1]->SetVisible(m_fZoom > 1 && m_bTextVisible);
    }
}

void CPlayerMap::ZoomIn()
{
    if (m_fZoom <= 4)
    {
        m_fZoom = m_fZoom * 2;
        SetupMapVariables();
    }
}

void CPlayerMap::ZoomOut()
{
    if (m_fZoom >= 1)
    {
        m_fZoom = m_fZoom / 2;

        if (m_fZoom > 1)
        {
            m_iVerticalMovement = static_cast<int>(m_iVerticalMovement / 1.7f);
            m_iHorizontalMovement = static_cast<int>(m_iHorizontalMovement / 1.7f);
        }
        else
        {
            m_iVerticalMovement = 0;
            m_iHorizontalMovement = 0;
            // Stop the movement
            m_bIsMovingNorth = false;
            m_bIsMovingSouth = false;
            m_bIsMovingEast = false;
            m_bIsMovingWest = false;
        }

        SetupMapVariables();
    }
}

void CPlayerMap::MoveNorth()
{
    if (!m_bIsAttachedToLocal)
    {
        if (m_fZoom > 1)
        {
            if (m_iMapMinY >= 0)
            {
                m_iMapMinY = 0;
                m_iMapMaxY = static_cast<int>(m_iMapMinY + m_fMapSize);
            }
            else
            {
                m_iVerticalMovement = m_iVerticalMovement + 20;
                SetupMapVariables();
            }
        }
    }
}

void CPlayerMap::MoveSouth()
{
    if (!m_bIsAttachedToLocal)
    {
        if (m_fZoom > 1)
        {
            if (m_iMapMaxY <= static_cast<int>(m_uiHeight))
            {
                m_iMapMaxY = m_uiHeight;
                m_iMapMinY = static_cast<int>(m_iMapMaxY - m_fMapSize);
            }
            else
            {
                m_iVerticalMovement = m_iVerticalMovement - 20;
                SetupMapVariables();
            }
        }
    }
}

void CPlayerMap::MoveEast()
{
    if (!m_bIsAttachedToLocal)
    {
        if (m_fZoom > 1)
        {
            if (m_iMapMaxX <= static_cast<int>(m_uiWidth))
            {
                m_iMapMaxX = m_uiWidth;
                m_iMapMinX = static_cast<int>(m_iMapMaxX - m_fMapSize);
            }
            else
            {
                m_iHorizontalMovement = m_iHorizontalMovement + 20;
                SetupMapVariables();
            }
        }
    }
}

void CPlayerMap::MoveWest()
{
    if (!m_bIsAttachedToLocal)
    {
        if (m_fZoom > 1)
        {
            if (m_iMapMinX >= 0)
            {
                m_iMapMinX = 0;
                m_iMapMaxX = static_cast<int>(m_iMapMinX + m_fMapSize);
            }
            else
            {
                m_iHorizontalMovement = m_iHorizontalMovement - 20;
                SetupMapVariables();
            }
        }
    }
}

void CPlayerMap::SetAttachedToLocalPlayer(bool bIsAttachedToLocal)
{
    m_bIsAttachedToLocal = bIsAttachedToLocal;
    SetupMapVariables();

    if (m_bIsAttachedToLocal)
        m_HelpTextList[0]->SetCaption(_("Following Player"));
    else
        m_HelpTextList[0]->SetCaption(_("Free Movement"));
}

bool CPlayerMap::IsPlayerMapShowing()
{
    return ((m_bIsPlayerMapEnabled || m_bForcedState) && m_mapImageTexture && m_playerMarkerTexture &&
            (!g_pCore->GetConsole()->IsVisible() && !g_pCore->IsMenuVisible()));
}

bool CPlayerMap::GetBoundingBox(CVector& vecMin, CVector& vecMax)
{
    // If our map image exists (Values are not calculated unless map is showing)
    if (IsPlayerMapShowing())
    {
        vecMin.fX = static_cast<float>(m_iMapMinX);
        vecMin.fY = static_cast<float>(m_iMapMinY);

        vecMax.fX = static_cast<float>(m_iMapMaxX);
        vecMax.fY = static_cast<float>(m_iMapMaxY);

        return true;
    }
    else
    {
        return false;
    }
}

void CPlayerMap::ToggleHelpText()
{
    m_bHideHelpText = !m_bHideHelpText;
}

SString CPlayerMap::GetBoundKeyName(const SString& strCommand)
{
    CCommandBind* pCommandBind = g_pCore->GetKeyBinds()->GetBindFromCommand(strCommand, 0, 0, 0, false, 0);
    if (!pCommandBind)
        return strCommand;
    return pCommandBind->boundKey->szKey;
}
