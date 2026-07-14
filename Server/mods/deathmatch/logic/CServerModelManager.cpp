/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Server/mods/deathmatch/logic/CServerModelManager.cpp
 *  PURPOSE:     Server-authoritative custom model registry
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CServerModelManager.h"

#include "CObjectManager.h"
#include "CPlayer.h"
#include "CPlayerManager.h"
#include "CResource.h"
#include "CVehicleManager.h"
#include "packets/CLuaPacket.h"

CServerModelManager::CServerModelManager(CPlayerManager& playerManager) : m_playerManager(playerManager)
{
}

CServerModelManager::ModelId CServerModelManager::Allocate(CResource& owner, eServerModelType type, ModelId parent, const std::string& name)
{
    if (!IsValidNativeParent(type, parent) || m_nextModelId > LAST_MODEL_ID)
        return INVALID_MODEL_ID;

    // IDs deliberately remain unused after release. Reliable network packets can
    // arrive late, so process-lifetime monotonic IDs prevent a stale free/update
    // from ever targeting a newer model that happened to reuse the same number.
    const ModelId id = m_nextModelId;
    std::string   qualifiedName = owner.GetName().c_str();
    qualifiedName += ":";
    qualifiedName += name.empty() ? std::to_string(id) : name;
    if (!IsNameAvailable(qualifiedName))
        return INVALID_MODEL_ID;

    ++m_nextModelId;
    Definition definition{id, parent, type, &owner, qualifiedName, false};
    auto [it, inserted] = m_definitions.emplace(id, std::move(definition));
    if (!inserted)
        return INVALID_MODEL_ID;

    BroadcastAllocate(it->second);
    return id;
}

bool CServerModelManager::Free(CResource& requester, ModelId model)
{
    auto it = m_definitions.find(model);
    if (it == m_definitions.end() || it->second.owner != &requester)
        return false;

    return FreeDefinition(it);
}

void CServerModelManager::FreeAllOwnedBy(CResource& owner)
{
    for (auto it = m_definitions.begin(); it != m_definitions.end();)
    {
        if (it->second.owner != &owner)
        {
            ++it;
            continue;
        }

        auto current = it++;
        FreeDefinition(current);
    }
}

const CServerModelManager::Definition* CServerModelManager::Find(ModelId model) const
{
    const auto it = m_definitions.find(model);
    return it != m_definitions.end() ? &it->second : nullptr;
}

CServerModelManager::ModelId CServerModelManager::ResolveParent(ModelId model) const
{
    const Definition* definition = Find(model);
    return definition ? definition->parent : model;
}

bool CServerModelManager::IsModelOfType(ModelId model, eServerModelType type) const
{
    const Definition* definition = Find(model);
    if (definition)
        return !definition->retiring && definition->type == type;

    return IsValidNativeParent(type, model);
}

bool CServerModelManager::IsValidNativeParent(eServerModelType type, ModelId parent) const
{
    switch (type)
    {
        case eServerModelType::OBJECT:
            return CObjectManager::IsValidModel(parent);
        case eServerModelType::VEHICLE:
            // Do not call the registry-aware validator here: the MVP intentionally
            // flattens inheritance to a native GTA parent.
            return parent >= 400 && parent <= 611;
    }

    return false;
}

bool CServerModelManager::IsNameAvailable(const std::string& name) const
{
    return std::none_of(m_definitions.begin(), m_definitions.end(), [&name](const auto& pair) { return pair.second.name == name; });
}

bool CServerModelManager::FreeDefinition(Definitions::iterator definition)
{
    Definition& value = definition->second;
    if (value.retiring)
        return false;

    value.retiring = true;

    // This callback must synchronously remap every entity to value.parent. The
    // subsequent reliable FREE RPC is then ordered after all model-change RPCs.
    if (m_beforeFreeCallback)
        m_beforeFreeCallback(value);

    BroadcastFree(value.id);
    m_definitions.erase(definition);
    return true;
}

void CServerModelManager::BroadcastAllocate(const Definition& definition) const
{
    CBitStream bitStream;
    bitStream.pBitStream->Write(definition.id);
    bitStream.pBitStream->Write(definition.parent);
    bitStream.pBitStream->Write(static_cast<std::uint8_t>(definition.type));
    for (auto it = m_playerManager.IterBegin(); it != m_playerManager.IterEnd(); ++it)
    {
        CPlayer* player = *it;
        if (player->IsJoined() && player->CanBitStream(eBitStreamVersion::ServerModelRegistry))
            player->Send(CLuaPacket(ALLOCATE_SERVER_MODEL, *bitStream.pBitStream));
    }
}

void CServerModelManager::BroadcastFree(ModelId model) const
{
    CBitStream bitStream;
    bitStream.pBitStream->Write(model);
    for (auto it = m_playerManager.IterBegin(); it != m_playerManager.IterEnd(); ++it)
    {
        CPlayer* player = *it;
        if (player->IsJoined() && player->CanBitStream(eBitStreamVersion::ServerModelRegistry))
            player->Send(CLuaPacket(FREE_SERVER_MODEL, *bitStream.pBitStream));
    }
}
