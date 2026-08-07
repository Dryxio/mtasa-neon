/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CCheckpointsSA.h
 *  PURPOSE:     Header file for checkpoint entity manager class
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <game/CCheckpoints.h>
#include <array>
#include "CCheckpointSA.h"

class CCheckpointSAInterface;
class CVector;

// 00722c40      public: static class CCheckpoint * __cdecl CCheckpoints::PlaceMarker(unsigned int,unsigned short,class CVector &,class CVector &,float,unsigned
// char,unsigned char,unsigned char,unsigned char,unsigned short,float,short)
#define FUNC_CCheckpoints__PlaceMarker 0x722c40

#define MAX_CHECKPOINTS   4096
#define ARRAY_CHECKPOINTS 0xC7F158

class CCheckpointsSA : public CCheckpoints
{
private:
    std::array<CCheckpointSA, MAX_CHECKPOINTS> Checkpoints{};

    static CCheckpointSAInterface* GetCheckpointArray();
    static void                    RelocateCheckpointArray();

public:
    CCheckpointsSA();
    ~CCheckpointsSA() = default;

    static CCheckpointSA* FromInterface(CCheckpointSAInterface* checkpointInterface);

    CCheckpoint* CreateCheckpoint(DWORD Identifier, WORD wType, CVector* vecPosition, CVector* vecPointDir, float fSize, float fPulseFraction,
                                  const SharedUtil::SColor color);
    CCheckpoint* FindFreeMarker();
    CCheckpoint* FindMarker(DWORD identifier);
    void         BeginFrame() override;
    unsigned int GetCount() const override;
    unsigned int GetCapacity() const override { return MAX_CHECKPOINTS; }
    unsigned int GetProcessLimit() const override;
    unsigned int GetRequired3DMarkerSlots() const override;
};
