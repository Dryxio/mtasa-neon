/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        sdk/game/CRadar.h
 *  PURPOSE:     Game radar interface
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include "Common.h"
#include <cstddef>
#include <cstdint>

class CMarker;
class CVector;
struct IDirect3DTexture9;

struct SRadarMapStats
{
    bool          hooksInstalled = false;
    std::uint32_t registeredTiles = 0;
    std::uint32_t loadedTiles = 0;
    std::uint32_t failedTiles = 0;
    std::uint64_t sourceBytes = 0;
    std::uint32_t revision = 0;
    unsigned int  minColumn = 0;
    unsigned int  maxColumn = 0;
    unsigned int  minRow = 0;
    unsigned int  maxRow = 0;
};

class CRadar
{
public:
    static constexpr unsigned int MAP_GRID_SIZE = 40;

    virtual CMarker*           CreateMarker(CVector* vecPosition) = 0;
    virtual CMarker*           GetFreeMarker() = 0;
    virtual void               DrawAreaOnRadar(float fX1, float fY1, float fX2, float fY2, const SharedUtil::SColor color) = 0;
    virtual bool               SetMapTile(unsigned int column, unsigned int row, const void* owner, const void* source, const char* data, std::size_t size,
                                          bool filteringEnabled) = 0;
    virtual bool               ResetMapTile(unsigned int column, unsigned int row, const void* owner) = 0;
    virtual void               RemoveMapTilesForSource(const void* source) = 0;
    virtual SRadarMapStats     GetMapStats() const = 0;
    virtual std::uint32_t      GetMapRevision() const = 0;
    virtual bool               IsMapTileRegistered(unsigned int column, unsigned int row) const = 0;
    virtual void               PrepareMapTileTextures(const unsigned int* columns, const unsigned int* rows, std::size_t count) = 0;
    virtual IDirect3DTexture9* AcquireMapTileTexture(unsigned int column, unsigned int row, bool& unloadAfterUse) = 0;
    virtual void ReleaseMapTileTexture(unsigned int column, unsigned int row, IDirect3DTexture9* texture, bool unloadAfterUse) = 0;
};
