/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/C3DMarkersSA.h
 *  PURPOSE:     Header file for 3D Marker entity manager class
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <game/C3DMarkers.h>
#include "C3DMarkerSA.h"

#define FUNC_PlaceMarker 0x725120
#define ARRAY_3D_MARKERS 0xC7DD58

// GTA's native replacement search processes eight entries per iteration, so
// this capacity must remain a multiple of eight unless that routine is replaced.
#define MAX_3D_MARKERS 4096

class C3DMarkersSA : public C3DMarkers
{
private:
    C3DMarkerSA* Markers[MAX_3D_MARKERS]{};

    static C3DMarkerSAInterface* GetMarkerArray();
    static void                  RelocateMarkerArrays();

public:
    C3DMarkersSA();
    ~C3DMarkersSA();

    C3DMarker*   CreateMarker(DWORD Identifier, T3DMarkerType dwType, CVector* vecPosition, float fSize, float fPulseFraction, BYTE r, BYTE g, BYTE b, BYTE a);
    C3DMarker*   FindFreeMarker();
    C3DMarker*   FindMarker(DWORD Identifier) override;
    void         ReinitMarkers();
    unsigned int GetCount() const override;
    unsigned int GetCapacity() const override { return MAX_3D_MARKERS; }
};
