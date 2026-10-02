/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *               (Shared logic for modifications)
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/shared_logic/CClientRenderElementManager.cpp
 *  PURPOSE:
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CClientVectorGraphic.h"

////////////////////////////////////////////////////////////////
//
// CClientRenderElementManager::CClientRenderElementManager
//
//
////////////////////////////////////////////////////////////////
CClientRenderElementManager::CClientRenderElementManager(CClientManager* pClientManager)
{
    m_pClientManager = pClientManager;
    m_pRenderItemManager = g_pCore->GetGraphics()->GetRenderItemManager();
    m_uiStatsDxFontCount = 0;
    m_uiStatsGuiFontCount = 0;
    m_uiStatsTextureCount = 0;
    m_uiStatsShaderCount = 0;
    m_uiStatsRenderTargetCount = 0;
    m_uiStatsDepthStencilTargetCount = 0;
    m_uiStatsMrtSetCount = 0;
    m_uiStatsSceneViewCount = 0;
    m_uiStatsCubemapRenderTargetCount = 0;
    m_uiStatsScreenSourceCount = 0;
    m_uiStatsWebBrowserCount = 0;
    m_uiStatsVectorGraphicCount = 0;
}

////////////////////////////////////////////////////////////////
//
// CClientRenderElementManager::~CClientRenderElementManager
//
//
////////////////////////////////////////////////////////////////
CClientRenderElementManager::~CClientRenderElementManager()
{
    // Remove any existing
    while (m_ItemElementMap.size())
        Remove(m_ItemElementMap.begin()->second);
}

////////////////////////////////////////////////////////////////
//
// CClientRenderElementManager::CreateDxFont
//
//
//
////////////////////////////////////////////////////////////////
CClientDxFont* CClientRenderElementManager::CreateDxFont(const SString& strFullFilePath, uint uiSize, bool bBold, const DWORD ulQuality)
{
    // Create the item
    CDxFontItem* pDxFontItem = m_pRenderItemManager->CreateDxFont(strFullFilePath, uiSize, bBold, ulQuality);

    // Check create worked
    if (!pDxFontItem)
        return NULL;

    // Create the element
    CClientDxFont* pDxFontElement = new CClientDxFont(m_pClientManager, INVALID_ELEMENT_ID, pDxFontItem);

    // Add to this manager's list
    MapSet(m_ItemElementMap, pDxFontItem, pDxFontElement);

    // Update stats
    m_uiStatsDxFontCount++;

    return pDxFontElement;
}

////////////////////////////////////////////////////////////////
//
// CClientRenderElementManager::CreateGuiFont
//
//
//
////////////////////////////////////////////////////////////////
CClientGuiFont* CClientRenderElementManager::CreateGuiFont(const SString& strFullFilePath, const SString& strUniqueName, uint uiSize)
{
    // Create the item
    CGuiFontItem* pGuiFontItem = m_pRenderItemManager->CreateGuiFont(strFullFilePath, strUniqueName, uiSize);

    // Check create worked
    if (!pGuiFontItem)
        return NULL;

    // Create the element
    CClientGuiFont* pGuiFontElement = new CClientGuiFont(m_pClientManager, INVALID_ELEMENT_ID, pGuiFontItem);

    // Add to this manager's list
    MapSet(m_ItemElementMap, pGuiFontItem, pGuiFontElement);

    // Update stats
    m_uiStatsGuiFontCount++;

    return pGuiFontElement;
}

////////////////////////////////////////////////////////////////
//
// CClientRenderElementManager::CreateTexture
//
//
//
////////////////////////////////////////////////////////////////
CClientTexture* CClientRenderElementManager::CreateTexture(const SString& strFullFilePath, const CPixels* pPixels, bool bMipMaps, uint uiSizeX, uint uiSizeY,
                                                           ERenderFormat format, ETextureAddress textureAddress, ETextureType textureType, uint uiVolumeDepth)
{
    // Create the item
    CTextureItem* pTextureItem =
        m_pRenderItemManager->CreateTexture(strFullFilePath, pPixels, bMipMaps, uiSizeX, uiSizeY, format, textureAddress, textureType, uiVolumeDepth);

    // Check create worked
    if (!pTextureItem)
        return NULL;

    // Create the element
    CClientTexture* pTextureElement = new CClientTexture(m_pClientManager, INVALID_ELEMENT_ID, pTextureItem);

    // Add to this manager's list
    MapSet(m_ItemElementMap, pTextureItem, pTextureElement);

    // Update stats
    m_uiStatsTextureCount++;

    return pTextureElement;
}

////////////////////////////////////////////////////////////////
//
// CClientRenderElementManager::CreateShader
//
//
//
////////////////////////////////////////////////////////////////
CClientShader* CClientRenderElementManager::CreateShader(const SString& strFile, const SString& strRootPath, bool bIsRawData, SString& strOutStatus,
                                                         float fPriority, float fMaxDistance, bool bLayered, bool bDebug, int iTypeMask,
                                                         const EffectMacroList& macros)
{
    // Create the item
    CShaderItem* pShaderItem =
        m_pRenderItemManager->CreateShader(strFile, strRootPath, bIsRawData, strOutStatus, fPriority, fMaxDistance, bLayered, bDebug, iTypeMask, macros);

    // Check create worked
    if (!pShaderItem)
        return NULL;

    // Create the element
    CClientShader* pShaderElement = new CClientShader(m_pClientManager, INVALID_ELEMENT_ID, pShaderItem);

    // Add to this manager's list
    MapSet(m_ItemElementMap, pShaderItem, pShaderElement);

    // Update stats
    m_uiStatsShaderCount++;

    return pShaderElement;
}

////////////////////////////////////////////////////////////////
//
// CClientRenderElementManager::CreateRenderTarget
//
//
//
////////////////////////////////////////////////////////////////
CClientRenderTarget* CClientRenderElementManager::CreateRenderTarget(uint uiSizeX, uint uiSizeY, bool bHasSurfaceFormat, bool bWithAlphaChannel,
                                                                     _D3DFORMAT surfaceFormat)
{
    // Create the item
    CRenderTargetItem* pRenderTargetItem = m_pRenderItemManager->CreateRenderTarget(uiSizeX, uiSizeY, bHasSurfaceFormat, bWithAlphaChannel, surfaceFormat);

    // Check create worked
    if (!pRenderTargetItem)
        return NULL;

    // Create the element
    CClientRenderTarget* pRenderTargetElement = new CClientRenderTarget(m_pClientManager, INVALID_ELEMENT_ID, pRenderTargetItem);

    // Add to this manager's list
    MapSet(m_ItemElementMap, pRenderTargetItem, pRenderTargetElement);

    // Update stats
    m_uiStatsRenderTargetCount++;

    return pRenderTargetElement;
}

////////////////////////////////////////////////////////////////
//
// CClientRenderElementManager::CreateDepthStencilTarget
//
//
//
////////////////////////////////////////////////////////////////
CClientDepthStencilTarget* CClientRenderElementManager::CreateDepthStencilTarget(uint uiSizeX, uint uiSizeY, _D3DFORMAT surfaceFormat, bool bSampleable)
{
    // Create the item
    CDepthStencilTargetItem* pDepthStencilTargetItem = m_pRenderItemManager->CreateDepthStencilTarget(uiSizeX, uiSizeY, surfaceFormat, bSampleable);

    // Check create worked
    if (!pDepthStencilTargetItem)
        return NULL;

    // Create the element
    CClientDepthStencilTarget* pDepthStencilTargetElement = new CClientDepthStencilTarget(m_pClientManager, INVALID_ELEMENT_ID, pDepthStencilTargetItem);

    // Add to this manager's list
    MapSet(m_ItemElementMap, pDepthStencilTargetItem, pDepthStencilTargetElement);

    // Update stats
    m_uiStatsDepthStencilTargetCount++;

    return pDepthStencilTargetElement;
}

////////////////////////////////////////////////////////////////
//
// CClientRenderElementManager::CreateMrtSet
//
//
//
////////////////////////////////////////////////////////////////
CClientMrtSet* CClientRenderElementManager::CreateMrtSet(CClientRenderTarget* const targets[MAX_MRT_RENDER_TARGETS], uint uiNumTargets,
                                                         CClientDepthStencilTarget* pDepthStencilTarget)
{
    CRenderTargetItem* itemTargets[MAX_MRT_RENDER_TARGETS] = {nullptr, nullptr, nullptr, nullptr};
    for (uint i = 0; i < uiNumTargets && i < MAX_MRT_RENDER_TARGETS; i++)
        itemTargets[i] = targets[i] ? targets[i]->GetRenderTargetItem() : nullptr;

    CDepthStencilTargetItem* pDepthStencilTargetItem = pDepthStencilTarget ? pDepthStencilTarget->GetDepthStencilTargetItem() : nullptr;

    // Create the item
    CMrtSetItem* pMrtSetItem = m_pRenderItemManager->CreateMrtSet(itemTargets, uiNumTargets, pDepthStencilTargetItem);

    // Check create worked
    if (!pMrtSetItem)
        return NULL;

    // Create the element
    CClientMrtSet* pMrtSetElement = new CClientMrtSet(m_pClientManager, INVALID_ELEMENT_ID, pMrtSetItem);

    // Add to this manager's list
    MapSet(m_ItemElementMap, pMrtSetItem, pMrtSetElement);

    // Update stats
    m_uiStatsMrtSetCount++;

    return pMrtSetElement;
}

CClientSceneView* CClientRenderElementManager::CreateSceneView(uint uiSizeX, uint uiSizeY, _D3DFORMAT colorFormat, _D3DFORMAT depthFormat,
                                                               bool bSampleableDepth)
{
    // No fixed count cap - creation is still bounded by CanCreateRenderItem's existing memory accounting
    // (via CreateRenderTarget/CreateDepthStencilTarget below), and per-frame render cost is bounded only by
    // how many views a script actually requests a render for, not by how many exist.
    CRenderTargetItem* pRenderTargetItem = m_pRenderItemManager->CreateRenderTarget(uiSizeX, uiSizeY, true, true, colorFormat);
    if (!pRenderTargetItem)
        return nullptr;

    // bSampleableDepth is what makes a SceneView usable as a shadow-map camera: the depth buffer a shadow
    // pass writes is also what a later pass needs to sample when comparing depths. Ordinary SceneViews
    // leave this false, unchanged from before this parameter existed.
    CDepthStencilTargetItem* pDepthStencilTargetItem = m_pRenderItemManager->CreateDepthStencilTarget(uiSizeX, uiSizeY, depthFormat, bSampleableDepth);
    if (!pDepthStencilTargetItem)
    {
        SAFE_RELEASE(pRenderTargetItem);
        return nullptr;
    }

    CClientSceneView* pSceneView = new CClientSceneView(m_pClientManager, INVALID_ELEMENT_ID, pRenderTargetItem, pDepthStencilTargetItem);
    MapSet(m_ItemElementMap, pRenderTargetItem, pSceneView);
    m_SceneViews.insert(pSceneView);
    ++m_uiStatsSceneViewCount;
    return pSceneView;
}

bool CClientRenderElementManager::SetSceneViewOutputShader(CClientSceneView* pSceneView, CShaderItem* pShaderItem, const SString& strInputName)
{
    if (!m_pRenderItemManager->IsSceneViewOutputShaderValid(pShaderItem, strInputName))
        return false;

    CRenderTargetItem* pSource = pSceneView->GetRenderTargetItem();
    CRenderTargetItem* pIntermediate = m_pRenderItemManager->CreateRenderTarget(pSource->m_uiSizeX, pSource->m_uiSizeY, true, true, pSource->m_eSurfaceFormat);
    if (!pIntermediate)
        return false;

    // The SceneView takes ownership of the newly-created private target. It is deliberately not mapped to
    // a Lua element, so scripts cannot bind it and create a feedback hazard behind the scheduler's back.
    pSceneView->SetOutputShader(pShaderItem, pIntermediate, strInputName);
    return true;
}

bool CClientRenderElementManager::RenderRequestedSceneView()
{
    // Consume requests before entering each native world pass. Lua cannot run from this loop, and the
    // multiplayer recursion guard rejects any accidental nested render. No fixed per-frame cap - every view
    // with a pending request renders every frame; cost scales directly with how many a script actually asks for.
    bool       bAnyRendered = false;
    const uint uiFrame = ++m_uiSceneViewSchedulerFrame;
    const uint uiTickCount = GetTickCount32();
    for (CClientSceneView* pSceneView : m_SceneViews)
    {
        if (!pSceneView->ShouldRender(uiFrame, uiTickCount))
            continue;

        const bool bBegan = m_pRenderItemManager->BeginSceneViewRender(pSceneView->GetRenderTargetItem(), pSceneView->GetDepthStencilTargetItem(),
                                                                       pSceneView->GetCameraMatrix(), pSceneView->GetFOV(), false);
        bool       bRendered = false;
        pSceneView->SetLastRenderError(bBegan ? "secondary scene render failed" : "could not begin scene-view render pass");
        if (bBegan)
        {
            // A non-null context, including an empty one, intentionally suppresses global world shader
            // assignments for this view. Scope restoration is unconditional so the primary camera resumes
            // using the ordinary global matcher even when the native secondary render fails.
            struct CScopedSceneViewShaderContext
            {
                CScopedSceneViewShaderContext(CRenderItemManagerInterface* pManager, const std::vector<SSceneViewShaderAssignment>& assignments)
                    : m_pManager(pManager)
                {
                    m_pManager->SetSceneViewShaderContext(&assignments);
                }
                ~CScopedSceneViewShaderContext() { m_pManager->SetSceneViewShaderContext(nullptr); }
                CRenderItemManagerInterface* m_pManager;
            } shaderContext(m_pRenderItemManager, pSceneView->GetShaderAssignments());

            g_pMultiplayer->SetSceneViewProjection(pSceneView->IsOrthographic(), pSceneView->GetOrthographicWidth(), pSceneView->GetOrthographicHeight(),
                                                   pSceneView->GetOrthographicNearClip(), pSceneView->GetOrthographicFarClip());
            bRendered = g_pMultiplayer->RenderSecondaryScene();
            if (!bRendered)
                pSceneView->SetLastRenderError(g_pMultiplayer->GetLastSecondarySceneRenderError());
            m_pRenderItemManager->EndRenderPass();
            if (bRendered && pSceneView->GetOutputShaderItem())
            {
                bRendered = m_pRenderItemManager->ApplySceneViewOutputShader(pSceneView->GetRenderTargetItem(), pSceneView->GetPostProcessTargetItem(),
                                                                             pSceneView->GetOutputShaderItem(), pSceneView->GetOutputShaderInputName());
                pSceneView->SetLastRenderError(bRendered ? SString() : m_pRenderItemManager->GetLastSceneViewOutputError());
            }
            else if (bRendered)
                pSceneView->SetLastRenderError("");
        }
        pSceneView->OnRenderCompleted(bRendered, uiFrame, uiTickCount);
        bAnyRendered |= bRendered;
    }
    return bAnyRendered;
}

////////////////////////////////////////////////////////////////
//
// CClientRenderElementManager::CreateCubemapRenderTarget
//
////////////////////////////////////////////////////////////////
CClientCubemapRenderTarget* CClientRenderElementManager::CreateCubemapRenderTarget(uint uiEdgeSize, _D3DFORMAT surfaceFormat)
{
    CCubemapRenderTargetItem* pCubemapItem = m_pRenderItemManager->CreateCubemapRenderTarget(uiEdgeSize, surfaceFormat);
    if (!pCubemapItem)
        return nullptr;

    CClientCubemapRenderTarget* pCubemap = new CClientCubemapRenderTarget(m_pClientManager, INVALID_ELEMENT_ID, pCubemapItem);
    MapSet(m_ItemElementMap, pCubemapItem, pCubemap);
    m_Cubemaps.insert(pCubemap);
    ++m_uiStatsCubemapRenderTargetCount;
    return pCubemap;
}

////////////////////////////////////////////////////////////////
//
// CClientRenderElementManager::RenderRequestedCubemaps
//
// Face indices and directions match D3DCUBEMAP_FACES (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z). Up vectors
// avoid the degenerate parallel-to-front case the same way SceneView cameras already have to for a
// straight-up/down look direction - (0,0,1) works for every horizontal face, but the vertical (+Z/-Z)
// faces need (0,1,0) instead since their own front direction IS (0,0,1)/(0,0,-1).
//
////////////////////////////////////////////////////////////////
bool CClientRenderElementManager::RenderRequestedCubemaps()
{
    static const struct
    {
        CVector front;
        CVector up;
    } faceDirections[6] = {
        {CVector(1, 0, 0), CVector(0, 0, 1)},  {CVector(-1, 0, 0), CVector(0, 0, 1)}, {CVector(0, 1, 0), CVector(0, 0, 1)},
        {CVector(0, -1, 0), CVector(0, 0, 1)}, {CVector(0, 0, 1), CVector(0, 1, 0)},  {CVector(0, 0, -1), CVector(0, 1, 0)},
    };

    // No fixed per-frame cap - every requested face renders every frame; cost scales directly with how many
    // a script actually requests. GetRequestedFaces/ClearRenderedFaces are split (rather than one
    // consume-and-clear call) so a face that fails to begin its render pass - eg. device not ready - stays
    // queued for the next frame instead of being silently dropped.
    bool bAnyRendered = false;
    for (CClientCubemapRenderTarget* pCubemap : m_Cubemaps)
    {
        const uint8 uiRequestedMask = pCubemap->GetRequestedFaces();
        if (!uiRequestedMask)
            continue;
        if (!pCubemap->IsPositionConfigured())
        {
            pCubemap->SetLastRenderError("cubemap camera position has not been configured");
            pCubemap->OnRenderCompleted(false);
            continue;
        }

        uint8 uiRenderedMask = 0;
        bool  bAllRequestedRendered = true;
        for (uint uiFace = 0; uiFace < 6; ++uiFace)
        {
            if (!(uiRequestedMask & (1 << uiFace)))
                continue;

            CMatrix matrix;
            matrix.vPos = pCubemap->GetCameraPosition();
            matrix.vFront = faceDirections[uiFace].front;
            matrix.vUp = faceDirections[uiFace].up;
            matrix.OrthoNormalize(CMatrix::AXIS_FRONT, CMatrix::AXIS_UP);

            g_pMultiplayer->SetSceneViewSquarePerspective(true);
            g_pMultiplayer->SetSceneViewProjection(false, 0.0f, 0.0f, pCubemap->GetNearClip(), pCubemap->GetFarClip());

            const bool bBegan = m_pRenderItemManager->BeginCubemapFaceRender(pCubemap->GetCubemapRenderTargetItem(), uiFace, matrix, true);
            bool       bRendered = false;
            if (bBegan)
            {
                // No per-cubemap world-shader assignment set exists yet (unlike SceneView) - always render
                // with the unmodified GTA material path rather than risk inheriting a stale context left
                // over from a preceding SceneView render this same frame.
                m_pRenderItemManager->SetSceneViewShaderContext(nullptr);
                bRendered = g_pMultiplayer->RenderSecondaryScene();
                if (!bRendered)
                    pCubemap->SetLastRenderError(g_pMultiplayer->GetLastSecondarySceneRenderError());
                m_pRenderItemManager->EndRenderPass();
                uiRenderedMask |= (1 << uiFace);
            }
            else
                pCubemap->SetLastRenderError("could not begin cubemap face render pass");

            g_pMultiplayer->SetSceneViewSquarePerspective(false);

            bAllRequestedRendered &= bRendered;
            bAnyRendered |= bRendered;
        }

        pCubemap->ClearRenderedFaces(uiRenderedMask);
        pCubemap->OnRenderCompleted(bAllRequestedRendered);
    }
    return bAnyRendered;
}

////////////////////////////////////////////////////////////////
//
// CClientRenderElementManager::CreateScreenSource
//
//
//
////////////////////////////////////////////////////////////////
CClientScreenSource* CClientRenderElementManager::CreateScreenSource(uint uiSizeX, uint uiSizeY)
{
    // Create the item
    CScreenSourceItem* pScreenSourceItem = m_pRenderItemManager->CreateScreenSource(uiSizeX, uiSizeY);

    // Check create worked
    if (!pScreenSourceItem)
        return NULL;

    // Create the element
    CClientScreenSource* pScreenSourceElement = new CClientScreenSource(m_pClientManager, INVALID_ELEMENT_ID, pScreenSourceItem);

    // Add to this manager's list
    MapSet(m_ItemElementMap, pScreenSourceItem, pScreenSourceElement);

    // Update stats
    m_uiStatsScreenSourceCount++;

    return pScreenSourceElement;
}

////////////////////////////////////////////////////////////////
//
// CClientRenderElementManager::CreateWebBrowser
//
//
//
////////////////////////////////////////////////////////////////
CClientWebBrowser* CClientRenderElementManager::CreateWebBrowser(uint uiSizeX, uint uiSizeY, bool bIsLocal, bool bTransparent)
{
    // Create the item
    CWebBrowserItem* pWebBrowserItem = m_pRenderItemManager->CreateWebBrowser(uiSizeX, uiSizeY);

    // Check create worked
    if (!pWebBrowserItem)
        return NULL;

    // Create the element
    CClientWebBrowser* pWebBrowserElement = new CClientWebBrowser(m_pClientManager, INVALID_ELEMENT_ID, pWebBrowserItem, bIsLocal, bTransparent);

    // Add to this manager's list
    MapSet(m_ItemElementMap, pWebBrowserItem, pWebBrowserElement);

    // Update stats
    m_uiStatsWebBrowserCount++;

    return pWebBrowserElement;
}

////////////////////////////////////////////////////////////////
//
// CClientRenderElementManager::CreateVectorGraphic
//
//
//
////////////////////////////////////////////////////////////////
CClientVectorGraphic* CClientRenderElementManager::CreateVectorGraphic(uint width, uint height)
{
    // Create the item
    CVectorGraphicItem* pVectorGraphicItem = m_pRenderItemManager->CreateVectorGraphic(width, height);

    // Check create worked
    if (!pVectorGraphicItem)
        return nullptr;

    // Create the element
    CClientVectorGraphic* pVectorGraphicElement = new CClientVectorGraphic(m_pClientManager, INVALID_ELEMENT_ID, pVectorGraphicItem);

    // Add to this manager's list
    MapSet(m_ItemElementMap, pVectorGraphicElement->GetRenderItem(), pVectorGraphicElement);

    m_uiStatsVectorGraphicCount++;

    return pVectorGraphicElement;
}

////////////////////////////////////////////////////////////////
//
// CClientRenderElementManager::FindAutoTexture
//
// Find texture by unique name. Create if not found.
//
////////////////////////////////////////////////////////////////
CClientTexture* CClientRenderElementManager::FindAutoTexture(const SString& strFullFilePath, const SString& strUniqueName)
{
    // Check if we've already done this file
    CClientTexture** ppTextureElement = MapFind(m_AutoTextureMap, strUniqueName);
    if (!ppTextureElement)
    {
        // Try to create
        CClientTexture* pNewTextureElement = CreateTexture(strFullFilePath);
        if (!pNewTextureElement)
            return NULL;

        // Add to automap if created
        MapSet(m_AutoTextureMap, strUniqueName, pNewTextureElement);
        ppTextureElement = MapFind(m_AutoTextureMap, strUniqueName);
    }

    return *ppTextureElement;
}

////////////////////////////////////////////////////////////////
//
// CClientRenderElementManager::Remove
//
// Called when an element is being deleted.
// Remove from lists and release render item
//
////////////////////////////////////////////////////////////////
void CClientRenderElementManager::Remove(CClientRenderElement* pElement)
{
    // Validate
    assert(pElement == *MapFind(m_ItemElementMap, pElement->GetRenderItem()));

    // Remove from this managers list
    MapRemove(m_ItemElementMap, pElement->GetRenderItem());

    // Remove from auto texture map
    if (pElement->IsA(CClientTexture::GetClassId()))
    {
        for (std::map<SString, CClientTexture*>::iterator iter = m_AutoTextureMap.begin(); iter != m_AutoTextureMap.end(); ++iter)
        {
            if (iter->second == pElement)
            {
                m_AutoTextureMap.erase(iter);
                break;
            }
        }
    }

    // Update stats
    if (pElement->IsA(CClientDxFont::GetClassId()))
        m_uiStatsDxFontCount--;
    else if (pElement->IsA(CClientGuiFont::GetClassId()))
        m_uiStatsGuiFontCount--;
    else if (pElement->IsA(CClientShader::GetClassId()))
        m_uiStatsShaderCount--;
    else if (pElement->IsA(CClientSceneView::GetClassId()))
    {
        m_SceneViews.erase(static_cast<CClientSceneView*>(pElement));
        m_uiStatsSceneViewCount--;

        // Stage 1 has one shared native off-screen raster pair, also used by cube-map face rendering (see
        // RenderRequestedCubemaps - it goes through the exact same RenderSecondaryScene() call). Once the
        // last owner of either kind disappears (normally because a resource stopped), release that pair
        // immediately instead of retaining GPU memory until the multiplayer module or D3D device is reset.
        if (m_SceneViews.empty() && m_Cubemaps.empty() && g_pMultiplayer)
            g_pMultiplayer->ReleaseSecondarySceneResources();
    }
    else if (pElement->IsA(CClientCubemapRenderTarget::GetClassId()))
    {
        m_Cubemaps.erase(static_cast<CClientCubemapRenderTarget*>(pElement));
        m_uiStatsCubemapRenderTargetCount--;

        if (m_SceneViews.empty() && m_Cubemaps.empty() && g_pMultiplayer)
            g_pMultiplayer->ReleaseSecondarySceneResources();
    }
    else if (pElement->IsA(CClientRenderTarget::GetClassId()))
        m_uiStatsRenderTargetCount--;
    else if (pElement->IsA(CClientDepthStencilTarget::GetClassId()))
        m_uiStatsDepthStencilTargetCount--;
    else if (pElement->IsA(CClientMrtSet::GetClassId()))
        m_uiStatsMrtSetCount--;
    else if (pElement->IsA(CClientScreenSource::GetClassId()))
        m_uiStatsScreenSourceCount--;
    else if (pElement->IsA(CClientWebBrowser::GetClassId()))
        m_uiStatsWebBrowserCount--;
    else if (pElement->IsA(CClientVectorGraphic::GetClassId()))
        m_uiStatsVectorGraphicCount--;
    else if (pElement->IsA(CClientTexture::GetClassId()))
        m_uiStatsTextureCount--;

    // Release render item
    CRenderItem* pRenderItem = pElement->GetRenderItem();
    SAFE_RELEASE(pRenderItem);
}
