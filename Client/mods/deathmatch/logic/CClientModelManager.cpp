/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *               (Shared logic for modifications)
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/CClientModelManager.cpp
 *  PURPOSE:     Model manager class
 *
 *****************************************************************************/

#include "StdInc.h"
CClientModelManager::CClientModelManager() : m_Models(std::make_unique<std::shared_ptr<CClientModel>[]>(g_pGame->GetBaseIDforCOL()))
{
    const unsigned int uiMaxModelID = g_pGame->GetBaseIDforCOL();
    for (unsigned int i = 0; i < uiMaxModelID; i++)
    {
        m_Models[i] = nullptr;
    }
}

CClientModelManager::~CClientModelManager(void)
{
    RemoveAll();
}

void CClientModelManager::RemoveAll(void)
{
    ClearServerModels();

    const unsigned int uiMaxModelID = g_pGame->GetBaseIDforCOL();
    for (unsigned int i = 0; i < uiMaxModelID; i++)
    {
        m_Models[i] = nullptr;
    }
    m_modelCount = 0;
}

void CClientModelManager::Add(const std::shared_ptr<CClientModel>& pModel)
{
    if (m_Models[pModel->GetModelID()] != nullptr)
    {
        dassert(m_Models[pModel->GetModelID()].get() == pModel.get());
        return;
    }
    m_Models[pModel->GetModelID()] = pModel;
    m_modelCount++;
}

bool CClientModelManager::Remove(const std::shared_ptr<CClientModel>& pModel)
{
    int modelId = pModel->GetModelID();

    // Server registry slots are process-owned. A client resource must not be able
    // to invalidate the mapping used by network entities through engineFreeModel.
    if (m_ServerModelByRuntime.find(static_cast<std::uint16_t>(modelId)) != m_ServerModelByRuntime.end())
        return false;

    if (m_Models[modelId] != nullptr)
    {
        CResource* parentResource = m_Models[modelId]->GetParentResource();

        if (parentResource)
            parentResource->GetResourceModelStreamer()->FullyReleaseModel(static_cast<std::uint16_t>(modelId));

        m_Models[modelId]->RestoreEntitiesUsingThisModel();
        m_Models[modelId] = nullptr;
        m_modelCount--;
        return true;
    }

    return false;
}

int CClientModelManager::GetFirstFreeModelID(void)
{
    // Dynamic DFF models must stay below the TXD file-ID range. Allocating an
    // object in a TXD/COL slot corrupts the corresponding streaming entry.
    const unsigned int uiMaxModelID = g_pGame->GetBaseIDforTXD();
    for (unsigned int i = 0; i < uiMaxModelID; i++)
    {
        CModelInfo* pModelInfo = g_pGame->GetModelInfo(i, true);
        if (!pModelInfo->IsValid())
        {
            return i;
        }
    }
    return INVALID_MODEL_ID;
}

int CClientModelManager::GetFreeTxdModelID()
{
    std::uint32_t usTxdId = g_pGame->GetPools()->GetTxdPool().GetFreeTextureDictonarySlot();

    if (usTxdId == -1)
        return INVALID_MODEL_ID;

    return MAX_MODEL_DFF_ID + usTxdId;
}

std::shared_ptr<CClientModel> CClientModelManager::FindModelByID(int iModelID)
{
    int32_t iMaxModelId = g_pGame->GetBaseIDforCOL();

    if (iModelID < iMaxModelId)
        return m_Models[iModelID];

    return nullptr;
}

std::shared_ptr<CClientModel> CClientModelManager::Request(CClientManager* pManager, int iModelID, eClientModelType eType)
{
    std::shared_ptr<CClientModel> pModel = FindModelByID(iModelID);
    if (pModel == nullptr)
    {
        pModel = std::make_shared<CClientModel>(pManager, iModelID, eType);
    }

    pModel->m_eModelType = eType;
    return pModel;
}

std::vector<std::shared_ptr<CClientModel>> CClientModelManager::GetModelsByType(const eClientModelType type, const unsigned int minModelID)
{
    std::vector<std::shared_ptr<CClientModel>> found;
    found.reserve(m_modelCount);

    const unsigned int uiMaxModelID = g_pGame->GetBaseIDforCOL();
    for (unsigned int i = minModelID; i < uiMaxModelID; i++)
    {
        const std::shared_ptr<CClientModel>& model = m_Models[i];
        if (model && model->GetModelType() == type)
        {
            found.push_back(model);
        }
    }
    return found;
}

void CClientModelManager::DeallocateModelsAllocatedByResource(CResource* pResource)
{
    const unsigned int uiMaxModelID = g_pGame->GetBaseIDforCOL();
    for (unsigned int i = 0; i < uiMaxModelID; i++)
    {
        if (m_Models[i] != nullptr && m_Models[i]->GetParentResource() == pResource)
            Remove(m_Models[i]);
    }
}

bool CClientModelManager::AllocateServerModel(const SServerModelDefinition& definition)
{
    if (definition.logicalModelId < SERVER_MODEL_ID_MIN || definition.logicalModelId == 0xFFFF || definition.parentModelId >= SERVER_MODEL_ID_MIN)
    {
        return false;
    }

    eClientModelType clientType;
    switch (definition.type)
    {
        case eServerModelType::OBJECT:
            if (!g_pClientGame->GetObjectManager()->IsValidModel(definition.parentModelId))
                return false;

            // Preserve the parent's concrete model-info layout. Treating every
            // object as an atomic corrupts TIME/CLUMP-specific metadata.
            if (CModelInfo* parentInfo = g_pGame->GetModelInfo(definition.parentModelId, true))
            {
                switch (parentInfo->GetModelType())
                {
                    case eModelInfoType::TIME:
                        clientType = eClientModelType::TIMED_OBJECT;
                        break;
                    case eModelInfoType::CLUMP:
                        clientType = eClientModelType::CLUMP;
                        break;
                    case eModelInfoType::ATOMIC:
                        clientType = parentInfo->IsDamageableAtomic() ? eClientModelType::OBJECT_DAMAGEABLE : eClientModelType::OBJECT;
                        break;
                    default:
                        clientType = eClientModelType::OBJECT;
                        break;
                }
            }
            else
                return false;
            break;
        case eServerModelType::VEHICLE:
            clientType = eClientModelType::VEHICLE;
            if (!g_pClientGame->GetVehicleManager()->IsValidModel(definition.parentModelId))
                return false;
            break;
        default:
            return false;
    }

    auto existing = m_ServerModels.find(definition.logicalModelId);
    if (existing != m_ServerModels.end())
    {
        const auto& current = existing->second.definition;
        if (existing->second.model && current.parentModelId == definition.parentModelId && current.type == definition.type)
            return true;

        FreeServerModel(definition.logicalModelId);
    }

    SServerModelEntry entry;
    entry.definition = definition;

    const int runtimeModelId = GetFirstFreeModelID();
    if (runtimeModelId == INVALID_MODEL_ID)
    {
        // Retain the definition so entity decoding can safely fall back to the
        // vanilla parent if this client has exhausted its local model slots.
        m_ServerModels.emplace(definition.logicalModelId, std::move(entry));
        return false;
    }

    auto model = Request(g_pClientGame->GetManager(), runtimeModelId, clientType);
    Add(model);
    if (!model->Allocate(definition.parentModelId))
    {
        Remove(model);
        m_ServerModels.emplace(definition.logicalModelId, std::move(entry));
        return false;
    }

    entry.runtimeModelId = runtimeModelId;
    entry.model = model;
    m_ServerModelByRuntime.emplace(static_cast<std::uint16_t>(runtimeModelId), definition.logicalModelId);
    m_ServerModels.emplace(definition.logicalModelId, std::move(entry));
    return true;
}

bool CClientModelManager::FreeServerModel(std::uint16_t logicalModelId)
{
    auto entry = m_ServerModels.find(logicalModelId);
    if (entry == m_ServerModels.end())
        return false;

    std::shared_ptr<CClientModel> model = entry->second.model;
    if (entry->second.runtimeModelId != INVALID_MODEL_ID)
        m_ServerModelByRuntime.erase(static_cast<std::uint16_t>(entry->second.runtimeModelId));

    m_ServerModels.erase(entry);
    if (model)
        Remove(model);

    return true;
}

void CClientModelManager::ClearServerModels()
{
    std::vector<std::uint16_t> logicalModelIds;
    logicalModelIds.reserve(m_ServerModels.size());
    for (const auto& entry : m_ServerModels)
        logicalModelIds.push_back(entry.first);

    for (const std::uint16_t logicalModelId : logicalModelIds)
        FreeServerModel(logicalModelId);
}

std::uint16_t CClientModelManager::ResolveServerModelID(std::uint16_t logicalModelId) const
{
    const auto entry = m_ServerModels.find(logicalModelId);
    if (entry == m_ServerModels.end())
        return logicalModelId;

    if (entry->second.runtimeModelId != INVALID_MODEL_ID)
        return static_cast<std::uint16_t>(entry->second.runtimeModelId);

    return entry->second.definition.parentModelId;
}

std::uint16_t CClientModelManager::GetServerModelID(std::uint16_t runtimeModelId) const
{
    const auto logicalModelId = m_ServerModelByRuntime.find(runtimeModelId);
    if (logicalModelId == m_ServerModelByRuntime.end())
        return 0xFFFF;

    return logicalModelId->second;
}

int CClientModelManager::GetServerModelRuntimeID(std::uint16_t logicalModelId) const
{
    const auto entry = m_ServerModels.find(logicalModelId);
    if (entry == m_ServerModels.end())
        return INVALID_MODEL_ID;

    return entry->second.runtimeModelId;
}

const SServerModelDefinition* CClientModelManager::FindServerModelDefinition(std::uint16_t logicalModelId) const
{
    const auto entry = m_ServerModels.find(logicalModelId);
    if (entry == m_ServerModels.end())
        return nullptr;

    return &entry->second.definition;
}
