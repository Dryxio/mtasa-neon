/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CRadarSA.h
 *  PURPOSE:     Header file for game radar class
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <game/CRadar.h>
#include "CMarkerSA.h"
#include <memory>

#define ARRAY_CMarker 0xBA86F0
#define MAX_MARKERS   175

#define FUNC_DrawAreaOnRadar 0x5853d0
#define FUNC_SetCoordBlip    0x583820

class CRadarSA : public CRadar
{
public:
    CRadarSA();
    ~CRadarSA();
    CMarker*           CreateMarker(CVector* vecPosition);
    CMarker*           GetFreeMarker();
    void               DrawAreaOnRadar(float fX1, float fY1, float fX2, float fY2, const SharedUtil::SColor color);
    bool               SetMapTile(unsigned int column, unsigned int row, const void* owner, const void* source, const char* data, std::size_t size,
                                  bool filteringEnabled) override;
    bool               ResetMapTile(unsigned int column, unsigned int row, const void* owner) override;
    void               RemoveMapTilesForSource(const void* source) override;
    SRadarMapStats     GetMapStats() const override;
    std::uint32_t      GetMapRevision() const override;
    bool               IsMapTileRegistered(unsigned int column, unsigned int row) const override;
    void               PrepareMapTileTextures(const unsigned int* columns, const unsigned int* rows, std::size_t count) override;
    IDirect3DTexture9* AcquireMapTileTexture(unsigned int column, unsigned int row, bool& unloadAfterUse) override;
    void ReleaseMapTileTexture(unsigned int column, unsigned int row, IDirect3DTexture9* texture, bool unloadAfterUse) override;

    // Internal entry points used by the validated GTA call-site hooks.
    void DrawMapSection(int x, int y);
    void UpdateMapStreaming(int centerX, int centerY);

private:
    struct SExtendedRadar;
    std::unique_ptr<SExtendedRadar> m_ExtendedRadar;
};
