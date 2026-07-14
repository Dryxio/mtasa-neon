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
    SString                name;
    if (!bitStream.Read(definition.logicalModelId) || !bitStream.Read(definition.parentModelId) || !bitStream.Read(rawType))
        return;

    if (rawType > static_cast<std::uint8_t>(eServerModelType::PED))
        return;

    definition.type = static_cast<eServerModelType>(rawType);
    // V1 allocations ended after type. V2 appends a qualified name, so the
    // current client remains able to join a server using the original schema.
    if (bitStream.GetNumberOfUnreadBits() > 0)
    {
        if (!bitStream.ReadString(name))
            return;
        definition.name = name.c_str();
    }
    m_pManager->GetModelManager()->AllocateServerModel(definition);
}

void CModelRPCs::FreeServerModel(NetBitStreamInterface& bitStream)
{
    std::uint16_t logicalModelId = 0;
    if (bitStream.Read(logicalModelId))
        m_pManager->GetModelManager()->FreeServerModel(logicalModelId);
}
