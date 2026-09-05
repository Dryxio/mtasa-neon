/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        core/CRenderItemManager.TextureReplace.cpp
 *
 *****************************************************************************/

#include "StdInc.h"
#include <game/CGame.h>
#include <game/CRenderWare.h>

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::GetVisibleTextureNames
//
// Get the names of all streamed in world textures, filtered by name and/or model
//
////////////////////////////////////////////////////////////////
void CRenderItemManager::GetVisibleTextureNames(std::vector<SString>& outNameList, const SString& strTextureNameMatch, ushort usModelID)
{
    // If modelid supplied, get the model texture names into a map
    std::set<SString> modelTextureNameMap;
    if (usModelID)
    {
        std::vector<SString> modelTextureNameList;
        m_pRenderWare->GetModelTextureNames(modelTextureNameList, usModelID);
        for (std::vector<SString>::const_iterator iter = modelTextureNameList.begin(); iter != modelTextureNameList.end(); ++iter)
            modelTextureNameMap.insert((*iter).ToLower());
    }

    SString strTextureNameMatchLower = strTextureNameMatch.ToLower();

    std::set<SString> resultMap;

    // For each texture that was used in the previous frame
    for (CFastHashSet<CD3DDUMMY*>::const_iterator iter = m_PrevFrameTextureUsage.begin(); iter != m_PrevFrameTextureUsage.end(); ++iter)
    {
        // Get the texture name
        const char* szTextureName = m_pRenderWare->GetTextureName(*iter);

        // Filter by wildcard match
        if (!WildcardMatchI(strTextureNameMatchLower, szTextureName))
            continue;

        // Filter by model
        if (usModelID)
            if (!MapContains(modelTextureNameMap, SStringX(szTextureName).ToLower()))
                continue;

        resultMap.insert(szTextureName);
    }

    outNameList.reserve(resultMap.size());
    for (std::set<SString>::const_iterator iter = resultMap.begin(); iter != resultMap.end(); ++iter)
    {
        outNameList.push_back(*iter);
    }
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::RemoveClientEntityRefs
//
// Make sure all replacements are cleared for this entity
//
////////////////////////////////////////////////////////////////
void CRenderItemManager::RemoveClientEntityRefs(CClientEntityBase* pClientEntity)
{
    m_pRenderWare->RemoveClientEntityRefs(pClientEntity);
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::GetAppliedShaderForD3DData
//
// Find which shader item is being used to render this D3DData
//
////////////////////////////////////////////////////////////////
SShaderItemLayers* CRenderItemManager::GetAppliedShaderForD3DData(CD3DDUMMY* pD3DData)
{
    // Save texture usage for later
    MapInsert(m_FrameTextureUsage, pD3DData);

    if (!m_pSceneViewShaderContext)
        return m_pRenderWare->GetAppliedShaderForD3DData(pD3DData);

    // The isolated SceneView namespace is texture-based. A wildcard must not turn untextured immediate-mode
    // geometry (sky, shadows and effects) into textured geometry by sampling whatever RenderWare happened to
    // leave in stage 0. With animated UV shaders that stale binding appears as a moving copy of another part
    // of the scene across otherwise untextured polygons.
    if (!pD3DData)
        return nullptr;

    // A SceneView context is an isolated replacement namespace: global world-texture assignments are not
    // consulted at all. This avoids mutating or cloning the global match-channel graph around a native GTA
    // render and guarantees that another view or the primary camera cannot observe these assignments.
    *m_pSceneViewShaderLayers = SShaderItemLayers();
    const char*        szTextureName = m_pRenderWare->GetTextureName(pD3DData);
    const int          iEntityType = m_pRenderWare->GetRenderingEntityType();
    CClientEntityBase* pRenderingEntity = m_pRenderWare->GetRenderingClientEntity();

    const auto matches = [&](const SSceneViewShaderAssignment& assignment)
    {
        CShaderItem* pShader = assignment.pShaderItem;
        return pShader && (pShader->m_iTypeMask & iEntityType) && WildcardMatchI(assignment.strTextureNameMatch, szTextureName);
    };

    // engineApplyShaderToWorldTexture parity: a targeted (non-global) assignment with appendLayers=false
    // takes exclusive priority over global (no-target) assignments for that one element's own textures.
    bool bExclusiveEntityMatch = false;
    if (pRenderingEntity)
    {
        for (const SSceneViewShaderAssignment& assignment : *m_pSceneViewShaderContext)
        {
            if (assignment.pTargetEntity == pRenderingEntity && !assignment.bAppendLayers && matches(assignment))
            {
                bExclusiveEntityMatch = true;
                break;
            }
        }
    }

    std::vector<CShaderItem*> matchedLayers;
    CShaderItem*              pBestBase = nullptr;
    for (const SSceneViewShaderAssignment& assignment : *m_pSceneViewShaderContext)
    {
        if (!matches(assignment))
            continue;
        if (assignment.pTargetEntity && assignment.pTargetEntity != pRenderingEntity)
            continue;  // targets a different element
        if (!assignment.pTargetEntity && bExclusiveEntityMatch)
            continue;  // suppressed by this element's own exclusive assignment

        CShaderItem* pShader = assignment.pShaderItem;
        if (pShader->m_bLayered)
            matchedLayers.push_back(pShader);
        else if (!pBestBase || pShader->m_fPriority > pBestBase->m_fPriority ||
                 (pShader->m_fPriority == pBestBase->m_fPriority && pShader->m_uiCreateTime > pBestBase->m_uiCreateTime))
            pBestBase = pShader;
    }

    std::sort(
        matchedLayers.begin(), matchedLayers.end(), [](const CShaderItem* pLeft, const CShaderItem* pRight)
        { return pLeft->m_fPriority < pRight->m_fPriority || (pLeft->m_fPriority == pRight->m_fPriority && pLeft->m_uiCreateTime < pRight->m_uiCreateTime); });

    m_pSceneViewShaderLayers->pBase = pBestBase;
    m_pSceneViewShaderLayers->layerList = std::move(matchedLayers);
    m_pSceneViewShaderLayers->bUsesVertexShader = pBestBase && pBestBase->GetUsesVertexShader();
    for (CShaderItem* pLayer : m_pSceneViewShaderLayers->layerList)
        m_pSceneViewShaderLayers->bUsesVertexShader |= pLayer->GetUsesVertexShader();

    if (!m_pSceneViewShaderLayers->pBase && m_pSceneViewShaderLayers->layerList.empty())
        return nullptr;

    m_pRenderWare->OnShaderReplacementResolved(m_pSceneViewShaderLayers->bUsesVertexShader);
    return m_pSceneViewShaderLayers;
}

void CRenderItemManager::SetSceneViewShaderContext(const std::vector<SSceneViewShaderAssignment>* pAssignments)
{
    m_pSceneViewShaderContext = pAssignments;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::ApplyShaderItemToWorldTexture
//
// Add an association between the shader item and a world texture match
//
////////////////////////////////////////////////////////////////
bool CRenderItemManager::ApplyShaderItemToWorldTexture(CShaderItem* pShaderItem, const SString& strTextureNameMatch, CClientEntityBase* pClientEntity,
                                                       bool bAppendLayers)
{
    assert(pShaderItem);
    m_pRenderWare->AppendAdditiveMatch((CSHADERDUMMY*)pShaderItem, pClientEntity, strTextureNameMatch, pShaderItem->m_fPriority, pShaderItem->m_bLayered,
                                       pShaderItem->m_iTypeMask, pShaderItem->m_uiCreateTime, pShaderItem->GetUsesVertexShader(), bAppendLayers);
    return true;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::RemoveShaderItemFromWorldTexture
//
// Remove an association between the shader item and the world texture
//
////////////////////////////////////////////////////////////////
bool CRenderItemManager::RemoveShaderItemFromWorldTexture(CShaderItem* pShaderItem, const SString& strTextureNameMatch, CClientEntityBase* pClientEntity)
{
    assert(pShaderItem);
    m_pRenderWare->AppendSubtractiveMatch((CSHADERDUMMY*)pShaderItem, pClientEntity, strTextureNameMatch);
    return true;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::RemoveShaderItemFromWatchLists
//
// Ensure the shader item is not being referred to by the texture replacement system
//
////////////////////////////////////////////////////////////////
void CRenderItemManager::RemoveShaderItemFromWatchLists(CShaderItem* pShaderItem)
{
    m_pRenderWare->RemoveShaderRefs((CSHADERDUMMY*)pShaderItem);
}
