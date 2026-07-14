/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/rpc/CModelRPCs.cpp
 *  PURPOSE:     Server model registry RPC calls
 *
 *****************************************************************************/

#include <StdInc.h>
#include <CServerModelDefinition.h>
#include "CModelRPCs.h"

void CModelRPCs::LoadFunctions()
{
    AddHandler(ALLOCATE_SERVER_MODEL, AllocateServerModel, "AllocateServerModel");
    AddHandler(FREE_SERVER_MODEL, FreeServerModel, "FreeServerModel");
}

void CModelRPCs::AllocateServerModel(NetBitStreamInterface& bitStream)
{
    SServerModelDefinition definition;
    std::uint8_t           rawType = 0;
    if (!bitStream.Read(definition.logicalModelId) || !bitStream.Read(definition.parentModelId) || !bitStream.Read(rawType))
        return;

    if (rawType > static_cast<std::uint8_t>(eServerModelType::VEHICLE))
        return;

    definition.type = static_cast<eServerModelType>(rawType);
    m_pManager->GetModelManager()->AllocateServerModel(definition);
}

void CModelRPCs::FreeServerModel(NetBitStreamInterface& bitStream)
{
    std::uint16_t logicalModelId = 0;
    if (bitStream.Read(logicalModelId))
        m_pManager->GetModelManager()->FreeServerModel(logicalModelId);
}
