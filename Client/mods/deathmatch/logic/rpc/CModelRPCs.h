/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/rpc/CModelRPCs.h
 *  PURPOSE:     Server model registry RPC calls
 *
 *****************************************************************************/

#pragma once

#include "CRPCFunctions.h"

class CModelRPCs : public CRPCFunctions
{
public:
    static void LoadFunctions();

    DECLARE_RPC(AllocateServerModel);
    DECLARE_RPC(FreeServerModel);
};
