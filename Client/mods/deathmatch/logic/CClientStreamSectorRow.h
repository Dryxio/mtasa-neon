/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *               (Shared logic for modifications)
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/shared_logic/CClientStreamSectorRow.h
 *  PURPOSE:     Stream sector row class header
 *
 *****************************************************************************/

#pragma once

#include "CClientCommon.h"
#include <cmath>
#include <list>

// This is only the eagerly allocated central grid. The streamer creates extra
// rows and sectors outside it, so it must never be used as a world boundary.
constexpr float STREAMER_MAIN_GRID_HALF_SIZE = 6000.0f;

inline float AlignStreamSectorCoordinate(float coordinate, float sectorSize)
{
    return std::floor(coordinate / sectorSize) * sectorSize;
}

class CClientStreamSector;
class CClientStreamer;

class CClientStreamSectorRow
{
    friend class CClientStreamer;

public:
    CClientStreamSectorRow(float fBottom, float fTop, float fSectorSize, float fRowSize);
    ~CClientStreamSectorRow();

    std::list<CClientStreamSector*>::iterator Begin() { return m_Sectors.begin(); }
    std::list<CClientStreamSector*>::iterator End() { return m_Sectors.end(); }
    CClientStreamSector*                      Front() { return m_Sectors.front(); }
    void                                      Add(CClientStreamSector* pSector);
    void                                      Remove(CClientStreamSector* pSector);
    unsigned int                              CountSectors() { return m_Sectors.size(); }

    bool DoesContain(CVector& vecPosition);
    bool DoesContain(float fY);

    CClientStreamSector* FindOrCreateSector(CVector& vecPosition, CClientStreamSector* pSurrounding = NULL);
    CClientStreamSector* FindSector(float fX);

    void ConnectSector(CClientStreamSector* pSector);

    void GetPosition(float& fTop, float& fBottom)
    {
        fTop = m_fTop;
        fBottom = m_fBottom;
    }

private:
    bool IsExtra() { return m_bExtra; }
    void SetExtra(bool bExtra) { m_bExtra = bExtra; }

    const float                     m_fSectorSize;
    const float                     m_fRowSize;
    float                           m_fBottom, m_fTop;
    std::list<CClientStreamSector*> m_Sectors;
    bool                            m_bExtra;
    CClientStreamSectorRow *        m_pTop, *m_pBottom;
};
