/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        core/CRenderItemManager.cpp
 *  PURPOSE:
 *
 *****************************************************************************/

#include "StdInc.h"
#include <game/CGame.h>
#include <game/CRenderWare.h>
#include <game/CSettings.h>
#include "DXHook/CProxyDirect3DDevice9.h"
#include "CRenderItem.EffectCloner.h"
#include "CRenderStateScope.h"

extern std::atomic<bool> g_bInMTAScene;
extern std::atomic<bool> g_bInGTAScene;

// Type of vertex used to emulate StretchRect for SwiftShader bug
struct SRTVertex
{
    static const uint FVF = D3DFVF_XYZRHW | D3DFVF_TEX1;
    float             x, y, z, w;
    float             u, v;
};

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::CRenderItemManager
//
//
//
////////////////////////////////////////////////////////////////
CRenderItemManager::CRenderItemManager()
    : m_uiLastRenderTargetRetryTime(0), m_uiRenderTargetRetryDelayMs(0), m_uiRenderTargetRetryAttempts(0), m_uiRenderTargetRetryCooldownUntil(0)
{
    m_pEffectCloner = new CEffectCloner(this);
    m_pSceneViewShaderLayers = new SShaderItemLayers();
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::~CRenderItemManager
//
//
//
////////////////////////////////////////////////////////////////
CRenderItemManager::~CRenderItemManager()
{
    ForceCloseAllRenderPasses();
    SAFE_DELETE(m_pSceneViewShaderLayers);
    SAFE_DELETE(m_pEffectCloner);
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::GetDeviceCooperativeLevel
//
// Helper to evaluate device readiness for GPU operations
//
////////////////////////////////////////////////////////////////
HRESULT CRenderItemManager::GetDeviceCooperativeLevel(const char* szContext, bool bLogLost) const
{
    if (!m_pDevice)
        return D3DERR_INVALIDCALL;

    const HRESULT hrCoopLevel = m_pDevice->TestCooperativeLevel();
    if (hrCoopLevel == D3D_OK)
        return hrCoopLevel;

    if (bLogLost)  // Expand (without log spam) later
    {
        (void)szContext;
    }

    return hrCoopLevel;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::OnDeviceCreate
//
//
//
////////////////////////////////////////////////////////////////
void CRenderItemManager::OnDeviceCreate(IDirect3DDevice9* pDevice, float fViewportSizeX, float fViewportSizeY)
{
    m_pDevice = pDevice;
    m_uiDefaultViewportSizeX = fViewportSizeX;
    m_uiDefaultViewportSizeY = fViewportSizeY;

    m_pRenderWare = CCore::GetSingleton().GetGame()->GetRenderWare();

    // Get some stats
    m_strVideoCardName = (const char*)g_pDeviceState->AdapterState.Name;
    m_iVideoCardMemoryKBTotal = g_pDeviceState->AdapterState.InstalledMemoryKB;

    m_iVideoCardMemoryKBForMTATotal = (m_iVideoCardMemoryKBTotal - (64 * 1024)) * 4 / 5;
    m_iVideoCardMemoryKBForMTATotal = std::max(0, m_iVideoCardMemoryKBForMTATotal);

    D3DCAPS9 caps;
    pDevice->GetDeviceCaps(&caps);
    int iMinor = caps.PixelShaderVersion & 0xFF;
    int iMajor = (caps.PixelShaderVersion & 0xFF00) >> 8;
    m_strVideoCardPSVersion = SString("%d", iMajor);
    if (iMinor)
        m_strVideoCardPSVersion += SString(".%d", iMinor);

    UpdateMemoryUsage();

    // Check if using SwiftShader dll
    SLibVersionInfo libVersionInfo;
    if (GetLibVersionInfo("d3d9.dll", &libVersionInfo))
    {
        if (libVersionInfo.strProductName.ContainsI("Swift"))
            m_bIsSwiftShader = true;
    }
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::OnLostDevice
//
// Release device stuff
//
////////////////////////////////////////////////////////////////
void CRenderItemManager::OnLostDevice()
{
    // Close any open render passes first - their captured D3D surfaces are about to be released
    // by the individual items' own OnLostDevice() calls below.
    ForceCloseAllRenderPasses();

    for (std::set<CRenderItem*>::iterator iter = m_CreatedItemList.begin(); iter != m_CreatedItemList.end();)
    {
        std::set<CRenderItem*>::iterator current = iter++;
        (*current)->OnLostDevice();
    }

    m_uiLastRenderTargetRetryTime = 0;
    m_uiRenderTargetRetryDelayMs = 0;
    m_uiRenderTargetRetryAttempts = 0;
    m_uiRenderTargetRetryCooldownUntil = 0;

    SAFE_RELEASE(m_pSavedSceneDepthSurface);
    SAFE_RELEASE(m_pSavedSceneRenderTargetAA);
    SAFE_RELEASE(g_pDeviceState->MainSceneState.DepthBuffer);
    SAFE_RELEASE(m_pNonAARenderTargetTexture);
    SAFE_RELEASE(m_pNonAARenderTarget);
    SAFE_RELEASE(m_pNonAADepthSurface2);
    SAFE_RELEASE(m_pDefaultD3DRenderTarget);
    SAFE_RELEASE(m_pDefaultD3DZStencilSurface);
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::OnResetDevice
//
// Recreate device stuff
//
////////////////////////////////////////////////////////////////
void CRenderItemManager::OnResetDevice()
{
    for (std::set<CRenderItem*>::iterator iter = m_CreatedItemList.begin(); iter != m_CreatedItemList.end(); iter++)
        (*iter)->OnResetDevice();

    m_uiLastRenderTargetRetryTime = 0;
    m_uiRenderTargetRetryDelayMs = 0;
    m_uiRenderTargetRetryAttempts = 0;
    m_uiRenderTargetRetryCooldownUntil = 0;

    UpdateMemoryUsage();
}

void CRenderItemManager::OnViewportSizeChanged(uint uiNewViewportSizeX, uint uiNewViewportSizeY)
{
    if (m_uiDefaultViewportSizeX == uiNewViewportSizeX && m_uiDefaultViewportSizeY == uiNewViewportSizeY)
        return;

    m_uiDefaultViewportSizeX = uiNewViewportSizeX;
    m_uiDefaultViewportSizeY = uiNewViewportSizeY;
    m_bBackBufferCopyMaybeNeedsResize = true;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::CreateTexture
//
// TODO: Make underlying data for textures shared
//
////////////////////////////////////////////////////////////////
CTextureItem* CRenderItemManager::CreateTexture(const SString& strFullFilePath, const CPixels* pPixels, bool bMipMaps, uint uiSizeX, uint uiSizeY,
                                                ERenderFormat format, ETextureAddress textureAddress, ETextureType textureType, uint uiVolumeDepth)
{
    CFileTextureItem* pTextureItem = new CFileTextureItem();
    pTextureItem->PostConstruct(this, strFullFilePath, pPixels, bMipMaps, uiSizeX, uiSizeY, format, textureAddress, textureType, uiVolumeDepth);

    if (!pTextureItem->IsValid())
    {
        SAFE_RELEASE(pTextureItem);
        return nullptr;
    }

    UpdateMemoryUsage();

    return pTextureItem;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::CreateVectorGraphic
//
//
//
////////////////////////////////////////////////////////////////
CVectorGraphicItem* CRenderItemManager::CreateVectorGraphic(uint width, uint height)
{
    if (!CanCreateRenderItem(CVectorGraphicItem::GetClassId()))
        return nullptr;

    CVectorGraphicItem* pVectorItem = new CVectorGraphicItem;
    pVectorItem->PostConstruct(this, width, height);

    if (!pVectorItem->IsValid())
    {
        SAFE_RELEASE(pVectorItem);
        return nullptr;
    }

    UpdateMemoryUsage();

    return pVectorItem;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::CreateRenderTarget
//
//
//
////////////////////////////////////////////////////////////////
CRenderTargetItem* CRenderItemManager::CreateRenderTarget(uint uiSizeX, uint uiSizeY, bool bHasSurfaceFormat, bool bWithAlphaChannel, int surfaceFormat,
                                                          bool bForce)
{
    if (!bForce && !CanCreateRenderItem(CRenderTargetItem::GetClassId()))
        return nullptr;

    // Reject an explicitly requested format the device can't actually render to, rather than
    // letting CreateUnderlyingData's retry loop burn through several failed CreateTexture calls
    // and return an unexplained nullptr.
    if (bHasSurfaceFormat && m_pDevice)
    {
        IDirect3D9* pD3D = nullptr;
        m_pDevice->GetDirect3D(&pD3D);
        if (pD3D)
        {
            D3DDISPLAYMODE displayMode;
            if (pD3D->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &displayMode) == D3D_OK)
            {
                HRESULT hrFormatCheck = pD3D->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, displayMode.Format, D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE,
                                                                (D3DFORMAT)surfaceFormat);
                if (hrFormatCheck != D3D_OK)
                {
                    WriteDebugEvent(
                        SString("CreateRenderTarget - Format %d is not supported as a render target on this GPU (0x%08x)", surfaceFormat, hrFormatCheck));
                    SAFE_RELEASE(pD3D);
                    return nullptr;
                }
            }
        }
        SAFE_RELEASE(pD3D);
    }

    // Include in memory stats only if render target is not for MTA internal use
    bool bIncludeInMemoryStats = (bForce == false);

    CRenderTargetItem* pRenderTargetItem = new CRenderTargetItem();
    pRenderTargetItem->PostConstruct(this, uiSizeX, uiSizeY, bHasSurfaceFormat, bWithAlphaChannel, surfaceFormat, bIncludeInMemoryStats);

    if (!pRenderTargetItem->IsValid())
    {
        const bool bAllowDeferredCreate = !bForce;
        if (!bAllowDeferredCreate || GetDeviceCooperativeLevel("CreateRenderTarget", false) == D3D_OK)
        {
            SAFE_RELEASE(pRenderTargetItem);
            return nullptr;
        }
    }

    UpdateMemoryUsage();

    return pRenderTargetItem;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::CreateDepthStencilTarget
//
////////////////////////////////////////////////////////////////
CDepthStencilTargetItem* CRenderItemManager::CreateDepthStencilTarget(uint uiSizeX, uint uiSizeY, int surfaceFormat, bool bSampleable)
{
    if (!CanCreateRenderItem(CDepthStencilTargetItem::GetClassId()))
        return nullptr;

    if (bSampleable)
    {
        // Only one vendor FourCC format is ever discovered/valid per GPU (see
        // CDirect3DEvents9::DiscoverReadableDepthFormat, run once at device creation for MTA's own
        // primary-scene readable depth buffer) - the caller-supplied surfaceFormat only applies to the
        // non-sampleable path below, since CheckDeviceFormat itself does not reliably report these
        // formats as valid for D3DRTYPE_TEXTURE even on hardware where creating one actually works, so
        // there is nothing more specific to validate against than "was a format discovered at all".
        if (m_depthBufferFormat == RFORMAT_UNKNOWN)
        {
            WriteDebugEvent("CreateDepthStencilTarget - sampleable depth targets are not supported on this GPU");
            return nullptr;
        }
        surfaceFormat = m_depthBufferFormat;
    }
    else
    {
        // Reject a format the device can't actually use as a depth-stencil surface, rather than
        // letting CreateDepthStencilSurface fail with no explanation.
        if (m_pDevice)
        {
            IDirect3D9* pD3D = nullptr;
            m_pDevice->GetDirect3D(&pD3D);
            if (pD3D)
            {
                D3DDISPLAYMODE displayMode;
                if (pD3D->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &displayMode) == D3D_OK)
                {
                    HRESULT hrFormatCheck = pD3D->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, displayMode.Format, D3DUSAGE_DEPTHSTENCIL,
                                                                    D3DRTYPE_SURFACE, (D3DFORMAT)surfaceFormat);
                    if (hrFormatCheck != D3D_OK)
                    {
                        WriteDebugEvent(SString("CreateDepthStencilTarget - Format %d is not supported as a depth-stencil surface on this GPU (0x%08x)",
                                                surfaceFormat, hrFormatCheck));
                        SAFE_RELEASE(pD3D);
                        return nullptr;
                    }
                }
            }
            SAFE_RELEASE(pD3D);
        }
    }

    CDepthStencilTargetItem* pDepthStencilTargetItem = new CDepthStencilTargetItem();
    pDepthStencilTargetItem->PostConstruct(this, uiSizeX, uiSizeY, surfaceFormat, bSampleable, true);

    if (!pDepthStencilTargetItem->IsValid())
    {
        SAFE_RELEASE(pDepthStencilTargetItem);
        return nullptr;
    }

    UpdateMemoryUsage();

    return pDepthStencilTargetItem;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::CreateCubemapRenderTarget
//
// Capability-gated on Stage 1's bCubemapRenderTargetSupported/iMaxCubemapEdgeLength (populated from
// D3DPTEXTURECAPS_CUBEMAP/MaxTextureWidth in GetDxCapabilities) - rejected outright rather than attempting
// creation and failing with a driver-specific error on hardware that never supported it.
//
////////////////////////////////////////////////////////////////
CCubemapRenderTargetItem* CRenderItemManager::CreateCubemapRenderTarget(uint uiEdgeSize, int surfaceFormat)
{
    if (!CanCreateRenderItem(CCubemapRenderTargetItem::GetClassId()))
        return nullptr;

    SDxCapabilities capabilities;
    GetDxCapabilities(capabilities);
    if (!capabilities.bCubemapRenderTargetSupported)
    {
        WriteDebugEvent("CreateCubemapRenderTarget - cubemap render targets are not supported on this GPU");
        return nullptr;
    }
    if (uiEdgeSize == 0 || (int)uiEdgeSize > capabilities.iMaxCubemapEdgeLength)
    {
        WriteDebugEvent(SString("CreateCubemapRenderTarget - edge size must be between 1 and %d on this GPU", capabilities.iMaxCubemapEdgeLength));
        return nullptr;
    }

    // Reject a format the device can't actually render a cube face to, rather than letting
    // CreateCubeTexture's retry loop burn through several failed attempts and return an unexplained nullptr.
    if (m_pDevice)
    {
        IDirect3D9* pD3D = nullptr;
        m_pDevice->GetDirect3D(&pD3D);
        if (pD3D)
        {
            D3DDISPLAYMODE displayMode;
            if (pD3D->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &displayMode) == D3D_OK)
            {
                HRESULT hrFormatCheck = pD3D->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, displayMode.Format, D3DUSAGE_RENDERTARGET,
                                                                D3DRTYPE_CUBETEXTURE, (D3DFORMAT)surfaceFormat);
                if (hrFormatCheck != D3D_OK)
                {
                    WriteDebugEvent(SString("CreateCubemapRenderTarget - Format %d is not supported as a cubemap render target on this GPU (0x%08x)",
                                            surfaceFormat, hrFormatCheck));
                    SAFE_RELEASE(pD3D);
                    return nullptr;
                }
            }
        }
        SAFE_RELEASE(pD3D);
    }

    CCubemapRenderTargetItem* pCubemapItem = new CCubemapRenderTargetItem();
    pCubemapItem->PostConstruct(this, uiEdgeSize, surfaceFormat);

    if (!pCubemapItem->IsValid())
    {
        SAFE_RELEASE(pCubemapItem);
        return nullptr;
    }

    UpdateMemoryUsage();

    return pCubemapItem;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::CreateMrtSet
//
// Validates target count against the device's actual NumSimultaneousRTs and
// that every target (color and depth-stencil) shares the same dimensions -
// both hard DX9 requirements - before creating the item. No new GPU memory
// is allocated here; the item only references existing render targets.
//
////////////////////////////////////////////////////////////////
CMrtSetItem* CRenderItemManager::CreateMrtSet(CRenderTargetItem* const targets[MAX_MRT_RENDER_TARGETS], uint uiNumTargets,
                                              CDepthStencilTargetItem* pDepthStencilTargetItem)
{
    if (uiNumTargets == 0 || !targets || !targets[0])
    {
        WriteDebugEvent("CreateMrtSet - at least one render target is required");
        return nullptr;
    }

    uint uiMaxSlots = std::max<uint>(1, std::min<uint>(g_pDeviceState->DeviceCaps.NumSimultaneousRTs, MAX_MRT_RENDER_TARGETS));
    if (uiNumTargets > uiMaxSlots)
    {
        WriteDebugEvent(
            SString("CreateMrtSet - %d render targets requested, but this GPU only supports %d simultaneous render targets", uiNumTargets, uiMaxSlots));
        return nullptr;
    }

    // All targets must share the same dimensions - a hard DX9 requirement for SetRenderTarget/SetDepthStencilSurface
    uint uiWidth = targets[0]->m_uiSizeX;
    uint uiHeight = targets[0]->m_uiSizeY;
    for (uint i = 1; i < uiNumTargets; i++)
    {
        if (!targets[i] || targets[i]->m_uiSizeX != uiWidth || targets[i]->m_uiSizeY != uiHeight)
        {
            WriteDebugEvent("CreateMrtSet - all render targets must have the same dimensions");
            return nullptr;
        }
    }
    if (pDepthStencilTargetItem && (pDepthStencilTargetItem->m_uiSizeX != uiWidth || pDepthStencilTargetItem->m_uiSizeY != uiHeight))
    {
        WriteDebugEvent("CreateMrtSet - depth-stencil target dimensions must match the color render targets");
        return nullptr;
    }

    if (!CanCreateRenderItem(CMrtSetItem::GetClassId()))
        return nullptr;

    CMrtSetItem* pMrtSetItem = new CMrtSetItem();
    pMrtSetItem->PostConstruct(this, targets, uiNumTargets, pDepthStencilTargetItem);

    if (!pMrtSetItem->IsValid())
    {
        SAFE_RELEASE(pMrtSetItem);
        return nullptr;
    }

    return pMrtSetItem;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::BeginRenderPass
//
// Pushes a CRenderStateScope and applies the requested targets. Every new
// rendering feature that needs to temporarily rebind render targets/depth/
// viewport is required to go through this (or CRenderStateScope directly for
// internal C++-scoped uses) rather than hand-rolling GetRenderTarget/
// SetRenderTarget pairs.
//
////////////////////////////////////////////////////////////////
bool CRenderItemManager::BeginRenderPass(CRenderTargetItem* const targets[MAX_MRT_RENDER_TARGETS], uint uiNumTargets,
                                         CDepthStencilTargetItem* pDepthStencilTargetItem, bool bClear)
{
    if (uiNumTargets == 0 || !targets || !targets[0] || !targets[0]->IsValid())
    {
        ++m_uiRenderPassFailures;
        WriteDebugEvent("BeginRenderPass - at least one valid render target is required");
        return false;
    }

    const size_t kMaxRenderPassNestingDepth = 4;
    if (m_RenderPassStack.size() >= kMaxRenderPassNestingDepth)
    {
        ++m_uiRenderPassFailures;
        WriteDebugEvent("BeginRenderPass - maximum render pass nesting depth exceeded");
        return false;
    }

    uint uiMaxSlots = std::max<uint>(1, std::min<uint>(g_pDeviceState->DeviceCaps.NumSimultaneousRTs, MAX_MRT_RENDER_TARGETS));
    if (uiNumTargets > uiMaxSlots)
    {
        ++m_uiRenderPassFailures;
        WriteDebugEvent(
            SString("BeginRenderPass - %d render targets requested, but this GPU only supports %d simultaneous render targets", uiNumTargets, uiMaxSlots));
        return false;
    }

    uint               uiWidth = targets[0]->m_uiSizeX;
    uint               uiHeight = targets[0]->m_uiSizeY;
    IDirect3DSurface9* d3dTargets[MAX_MRT_RENDER_TARGETS] = {nullptr, nullptr, nullptr, nullptr};
    d3dTargets[0] = targets[0]->m_pD3DRenderTargetSurface;

    for (uint i = 1; i < uiNumTargets; i++)
    {
        if (!targets[i] || !targets[i]->IsValid() || targets[i]->m_uiSizeX != uiWidth || targets[i]->m_uiSizeY != uiHeight)
        {
            ++m_uiRenderPassFailures;
            WriteDebugEvent("BeginRenderPass - all render targets must have the same dimensions");
            return false;
        }
        d3dTargets[i] = targets[i]->m_pD3DRenderTargetSurface;
    }

    IDirect3DSurface9* pD3DDepthStencil = nullptr;
    if (pDepthStencilTargetItem)
    {
        if (!pDepthStencilTargetItem->IsValid() || pDepthStencilTargetItem->m_uiSizeX != uiWidth || pDepthStencilTargetItem->m_uiSizeY != uiHeight)
        {
            ++m_uiRenderPassFailures;
            WriteDebugEvent("BeginRenderPass - depth-stencil target dimensions must match the color render targets");
            return false;
        }
        pD3DDepthStencil = pDepthStencilTargetItem->m_pD3DDepthStencilSurface;
    }

    if (GetDeviceCooperativeLevel("BeginRenderPass") != D3D_OK)
    {
        ++m_uiRenderPassFailures;
        return false;
    }

    CRenderStateScope* pScope = new CRenderStateScope(m_pDevice);
    if (!pScope->ApplyRenderTargets(d3dTargets, pD3DDepthStencil, uiWidth, uiHeight))
    {
        ++m_uiRenderPassFailures;
        delete pScope;
        WriteDebugEvent("BeginRenderPass - failed to apply render targets");
        return false;
    }

    if (bClear)
    {
        DWORD dwClearFlags = D3DCLEAR_TARGET | (pD3DDepthStencil ? (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL) : 0);
        m_pDevice->Clear(0, nullptr, dwClearFlags, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);
    }

    m_RenderPassStack.push_back(pScope);
    ++m_uiRenderPassesStarted;
    return true;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::EndRenderPass
//
////////////////////////////////////////////////////////////////
bool CRenderItemManager::EndRenderPass()
{
    if (m_RenderPassStack.empty())
    {
        ++m_uiRenderPassFailures;
        WriteDebugEvent("EndRenderPass - no render pass is currently open");
        return false;
    }

    CRenderStateScope* pScope = m_RenderPassStack.back();
    m_RenderPassStack.pop_back();
    delete pScope;
    return true;
}

bool CRenderItemManager::BeginSceneViewRender(CRenderTargetItem* pTarget, CDepthStencilTargetItem* pDepthStencilTargetItem, const CMatrix& cameraMatrix,
                                              float fFOV, bool bClear)
{
    CRenderTargetItem* targets[MAX_MRT_RENDER_TARGETS] = {pTarget, nullptr, nullptr, nullptr};
    if (!BeginRenderPass(targets, 1, pDepthStencilTargetItem, bClear))
        return false;

    // Camera restoration belongs to the same scope as target/depth/viewport restoration. If applying the
    // camera fails, closing the pass immediately leaves the primary frame untouched.
    if (!m_RenderPassStack.back()->ApplyCamera(cameraMatrix, fFOV))
    {
        EndRenderPass();
        return false;
    }

    // RenderWare caches fixed-function states such as COLORVERTEX and the material-source selectors used by
    // GTA's pre-lit meshes. IDirect3DStateBlock9::Apply bypasses both that cache and MTA's device proxy, making
    // the next SceneView/primary render incorrectly skip state changes. Native world passes therefore leave
    // draw state owned by RenderWare while this scope still restores all attachment and camera state.
    m_RenderPassStack.back()->DiscardSavedDrawState();
    return true;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::BeginCubemapFaceRender
//
////////////////////////////////////////////////////////////////
bool CRenderItemManager::BeginCubemapFaceRender(CCubemapRenderTargetItem* pCubemap, uint uiFace, const CMatrix& cameraMatrix, bool bClear)
{
    if (!pCubemap || !pCubemap->IsValid() || uiFace >= 6)
    {
        ++m_uiRenderPassFailures;
        WriteDebugEvent("BeginCubemapFaceRender - invalid cubemap or face index");
        return false;
    }

    const size_t kMaxRenderPassNestingDepth = 4;
    if (m_RenderPassStack.size() >= kMaxRenderPassNestingDepth)
    {
        ++m_uiRenderPassFailures;
        WriteDebugEvent("BeginCubemapFaceRender - maximum render pass nesting depth exceeded");
        return false;
    }

    if (GetDeviceCooperativeLevel("BeginCubemapFaceRender") != D3D_OK)
    {
        ++m_uiRenderPassFailures;
        return false;
    }

    IDirect3DSurface9* d3dTargets[MAX_MRT_RENDER_TARGETS] = {pCubemap->m_pD3DFaceSurface[uiFace], nullptr, nullptr, nullptr};

    CRenderStateScope* pScope = new CRenderStateScope(m_pDevice);
    if (!pScope->ApplyRenderTargets(d3dTargets, pCubemap->m_pD3DDepthStencilSurface, pCubemap->m_uiEdgeSize, pCubemap->m_uiEdgeSize))
    {
        ++m_uiRenderPassFailures;
        delete pScope;
        WriteDebugEvent("BeginCubemapFaceRender - failed to apply render targets");
        return false;
    }

    if (bClear)
    {
        m_pDevice->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);
    }

    m_RenderPassStack.push_back(pScope);
    ++m_uiRenderPassesStarted;

    // 90 degrees is not a configurable parameter - it is the only FOV where a square target's 6 faces tile
    // seamlessly into a full sphere. Camera restoration belongs to the same scope as target/depth/viewport
    // restoration, exactly like BeginSceneViewRender above.
    if (!m_RenderPassStack.back()->ApplyCamera(cameraMatrix, 90.0f))
    {
        EndRenderPass();
        return false;
    }

    m_RenderPassStack.back()->DiscardSavedDrawState();
    return true;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::ForceCloseAllRenderPasses
//
// Defensive cleanup for scripts that never call dxEndRenderPass (matching the
// existing "Restore in case script forgets" RestoreDefaultRenderTarget() call
// in CDirect3DEvents9::OnPresent) and for device loss, where the D3D surfaces
// a scope would try to restore are about to be released anyway.
//
////////////////////////////////////////////////////////////////
void CRenderItemManager::ForceCloseAllRenderPasses()
{
    if (m_RenderPassStack.empty())
        return;

    WriteDebugEvent(SString("ForceCloseAllRenderPasses - closing %d render pass(es) left open", (int)m_RenderPassStack.size()));
    m_uiForcedRenderPassClosures += static_cast<uint>(m_RenderPassStack.size());

    while (!m_RenderPassStack.empty())
    {
        CRenderStateScope* pScope = m_RenderPassStack.back();
        m_RenderPassStack.pop_back();
        delete pScope;
    }
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::CreateScreenSource
//
//
//
////////////////////////////////////////////////////////////////
CScreenSourceItem* CRenderItemManager::CreateScreenSource(uint uiSizeX, uint uiSizeY)
{
    if (!CanCreateRenderItem(CScreenSourceItem::GetClassId()))
        return nullptr;

    CScreenSourceItem* pScreenSourceItem = new CScreenSourceItem();
    pScreenSourceItem->PostConstruct(this, uiSizeX, uiSizeY);

    if (!pScreenSourceItem->IsValid())
    {
        SAFE_RELEASE(pScreenSourceItem);
        return nullptr;
    }

    UpdateMemoryUsage();

    return pScreenSourceItem;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::CreateWebBrowser
//
//
//
////////////////////////////////////////////////////////////////
CWebBrowserItem* CRenderItemManager::CreateWebBrowser(uint uiSizeX, uint uiSizeY)
{
    if (!CanCreateRenderItem(CWebBrowserItem::GetClassId()))
        return nullptr;

    CWebBrowserItem* pWebBrowserItem = new CWebBrowserItem;
    pWebBrowserItem->PostConstruct(this, uiSizeX, uiSizeY);

    if (!pWebBrowserItem->IsValid())
    {
        SAFE_RELEASE(pWebBrowserItem);
        return nullptr;
    }

    UpdateMemoryUsage();

    return pWebBrowserItem;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::CreateShader
//
// Create a D3DX effect from .fx file
//
////////////////////////////////////////////////////////////////
CShaderItem* CRenderItemManager::CreateShader(const SString& strFile, const SString& strRootPath, bool bIsRawData, SString& strOutStatus, float fPriority,
                                              float fMaxDistance, bool bLayered, bool bDebug, int iTypeMask, const EffectMacroList& macros)
{
    if (!CanCreateRenderItem(CShaderItem::GetClassId()))
        return nullptr;

    strOutStatus = "";

    CShaderItem* pShaderItem = new CShaderItem();
    pShaderItem->PostConstruct(this, strFile, strRootPath, bIsRawData, strOutStatus, fPriority, fMaxDistance, bLayered, bDebug, iTypeMask, macros);

    if (!pShaderItem->IsValid())
    {
        SAFE_RELEASE(pShaderItem);
        return nullptr;
    }

    return pShaderItem;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::CreateDxFont
//
// TODO: Make underlying data for fonts shared
//
////////////////////////////////////////////////////////////////
CDxFontItem* CRenderItemManager::CreateDxFont(const SString& strFullFilePath, uint uiSize, bool bBold, DWORD ulQuality)
{
    if (!CanCreateRenderItem(CDxFontItem::GetClassId()))
        return nullptr;

    CDxFontItem* pDxFontItem = new CDxFontItem();
    pDxFontItem->PostConstruct(this, strFullFilePath, uiSize, bBold, ulQuality);

    if (!pDxFontItem->IsValid())
    {
        SAFE_RELEASE(pDxFontItem);
        return nullptr;
    }

    UpdateMemoryUsage();

    return pDxFontItem;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::CreateGuiFont
//
// TODO: Make underlying data for fonts shared
//
////////////////////////////////////////////////////////////////
CGuiFontItem* CRenderItemManager::CreateGuiFont(const SString& strFullFilePath, const SString& strFontName, uint uiSize)
{
    if (!CanCreateRenderItem(CGuiFontItem::GetClassId()))
        return nullptr;

    CGuiFontItem* pGuiFontItem = new CGuiFontItem();
    pGuiFontItem->PostConstruct(this, strFullFilePath, strFontName, uiSize);

    if (!pGuiFontItem->IsValid())
    {
        SAFE_RELEASE(pGuiFontItem);
        return nullptr;
    }

    UpdateMemoryUsage();

    return pGuiFontItem;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::NotifyContructRenderItem
//
// Add to managers list
//
////////////////////////////////////////////////////////////////
void CRenderItemManager::NotifyContructRenderItem(CRenderItem* pItem)
{
    assert(!MapContains(m_CreatedItemList, pItem));
    MapInsert(m_CreatedItemList, pItem);

    if (CScreenSourceItem* pScreenSourceItem = DynamicCast<CScreenSourceItem>(pItem))
        m_bBackBufferCopyMaybeNeedsResize = true;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::NotifyDestructRenderItem
//
// Remove from managers list
//
////////////////////////////////////////////////////////////////
void CRenderItemManager::NotifyDestructRenderItem(CRenderItem* pItem)
{
    assert(MapContains(m_CreatedItemList, pItem));
    MapRemove(m_CreatedItemList, pItem);

    if (CScreenSourceItem* pScreenSourceItem = DynamicCast<CScreenSourceItem>(pItem))
        m_bBackBufferCopyMaybeNeedsResize = true;
    else if (CShaderItem* pShaderItem = DynamicCast<CShaderItem>(pItem))
        RemoveShaderItemFromWatchLists(pShaderItem);

    UpdateMemoryUsage();
}

//
//
// Render targets and back buffers
//
//

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::DoPulse
//
// Update stuff
//
////////////////////////////////////////////////////////////////
void CRenderItemManager::DoPulse()
{
    m_pRenderWare->PulseWorldTextureWatch();

    m_PrevFrameTextureUsage = m_FrameTextureUsage;
    m_FrameTextureUsage.clear();

    m_pEffectCloner->DoPulse();

    UpdateBackBufferCopy();
}

void CRenderItemManager::RetryInvalidRenderTargets()
{
    TryRecreateInvalidRenderTargets();
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::TryRecreateInvalidRenderTargets
//
// Retry render target creation when the device is ready
//
////////////////////////////////////////////////////////////////
void CRenderItemManager::TryRecreateInvalidRenderTargets()
{
    if (GetDeviceCooperativeLevel("TryRecreateInvalidRenderTargets", false) != D3D_OK)
        return;

    const uint kRetryIntervalMinMs = 250;
    const uint kRetryIntervalMaxMs = 2000;
    const uint kRetryAttemptCap = 20;
    const uint kRetryCooldownMs = 15000;

    const uint uiNow = GetTickCount32();
    if (m_uiRenderTargetRetryAttempts >= kRetryAttemptCap)
    {
        if (m_uiRenderTargetRetryCooldownUntil == 0)
            m_uiRenderTargetRetryCooldownUntil = uiNow + kRetryCooldownMs;

        if (uiNow < m_uiRenderTargetRetryCooldownUntil)
            return;

        m_uiRenderTargetRetryAttempts = 0;
        m_uiRenderTargetRetryDelayMs = kRetryIntervalMinMs;
        m_uiRenderTargetRetryCooldownUntil = 0;
    }

    if (m_uiRenderTargetRetryDelayMs == 0)
        m_uiRenderTargetRetryDelayMs = kRetryIntervalMinMs;

    if (uiNow - m_uiLastRenderTargetRetryTime < m_uiRenderTargetRetryDelayMs)
        return;

    m_uiLastRenderTargetRetryTime = uiNow;

    bool anyInvalid = false;
    bool anyRecreated = false;
    for (CRenderItem* pItem : m_CreatedItemList)
    {
        if (CRenderTargetItem* pRenderTarget = DynamicCast<CRenderTargetItem>(pItem))
        {
            if (pRenderTarget->IsValid())
                continue;

            anyInvalid = true;
            if (pRenderTarget->TryEnsureValid())
                anyRecreated = true;
        }
        else if (CScreenSourceItem* pScreenSource = DynamicCast<CScreenSourceItem>(pItem))
        {
            if (pScreenSource->IsValid())
                continue;

            anyInvalid = true;
            if (pScreenSource->TryEnsureValid())
                anyRecreated = true;
        }
    }

    if (!anyInvalid)
        return;

    if (anyRecreated)
    {
        m_uiRenderTargetRetryAttempts = 0;
        m_uiRenderTargetRetryDelayMs = kRetryIntervalMinMs;
        return;
    }

    ++m_uiRenderTargetRetryAttempts;
    m_uiRenderTargetRetryDelayMs = std::min(m_uiRenderTargetRetryDelayMs * 2, kRetryIntervalMaxMs);
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::UpdateBackBufferCopy
//
// Save back buffer pixels in our special place
//
////////////////////////////////////////////////////////////////
void CRenderItemManager::UpdateBackBufferCopy()
{
    if (m_bBackBufferCopyMaybeNeedsResize)
        UpdateBackBufferCopySize();

    // Don't bother doing this unless at least one screen source in active
    if (!m_pBackBufferCopy)
        return;

    // Try to get the back buffer
    IDirect3DSurface9* pD3DBackBufferSurface = nullptr;
    m_pDevice->GetRenderTarget(0, &pD3DBackBufferSurface);
    if (!pD3DBackBufferSurface)
        return;

    // Copy back buffer into our private render target
    if (!m_pBackBufferCopy->m_pD3DRenderTargetSurface)
    {
        SAFE_RELEASE(pD3DBackBufferSurface);
        return;
    }

    D3DTEXTUREFILTERTYPE FilterType = D3DTEXF_LINEAR;
    const HRESULT        hr = m_pDevice->StretchRect(pD3DBackBufferSurface, nullptr, m_pBackBufferCopy->m_pD3DRenderTargetSurface, nullptr, FilterType);
    if (SUCCEEDED(hr))
    {
        ++m_uiBackBufferCopyRevision;
    }
    else
    {
        WriteDebugEvent(SString("CRenderItemManager::UpdateBackBufferCopy: StretchRect failed: %08x", hr));
    }

    // Clean up
    SAFE_RELEASE(pD3DBackBufferSurface);
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::UpdateScreenSource
//
// Copy from back buffer store to screen source
// TODO - Optimize the case where the screen source is the same size as the back buffer copy (i.e. Use back buffer copy resources instead)
//
////////////////////////////////////////////////////////////////
void CRenderItemManager::UpdateScreenSource(CScreenSourceItem* pScreenSourceItem, bool bResampleNow)
{
    if (!pScreenSourceItem)
        return;

    if (!pScreenSourceItem->IsValid() && !pScreenSourceItem->TryEnsureValid())
    {
        return;
    }

    if (bResampleNow)
    {
        // Tell graphics things are about to change
        CGraphics::GetSingleton().OnChangingRenderTarget(m_uiDefaultViewportSizeX, m_uiDefaultViewportSizeY);

        // Try to get the back buffer
        IDirect3DSurface9* pD3DBackBufferSurface = nullptr;
        m_pDevice->GetRenderTarget(0, &pD3DBackBufferSurface);
        if (!pD3DBackBufferSurface)
            return;

        // Copy back buffer into our private render target
        if (pScreenSourceItem->m_pD3DRenderTargetSurface)
        {
            D3DTEXTUREFILTERTYPE FilterType = D3DTEXF_LINEAR;
            m_pDevice->StretchRect(pD3DBackBufferSurface, nullptr, pScreenSourceItem->m_pD3DRenderTargetSurface, nullptr, FilterType);
        }

        // Clean up
        SAFE_RELEASE(pD3DBackBufferSurface);
        return;
    }

    // Only do update if back buffer copy has changed
    if (pScreenSourceItem->m_uiRevision == m_uiBackBufferCopyRevision)
        return;

    pScreenSourceItem->m_uiRevision = m_uiBackBufferCopyRevision;

    if (m_pBackBufferCopy && m_pBackBufferCopy->m_pD3DRenderTargetSurface && pScreenSourceItem->m_pD3DRenderTargetSurface)
    {
        D3DTEXTUREFILTERTYPE FilterType = D3DTEXF_LINEAR;
        m_pDevice->StretchRect(m_pBackBufferCopy->m_pD3DRenderTargetSurface, nullptr, pScreenSourceItem->m_pD3DRenderTargetSurface, nullptr, FilterType);
    }
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::UpdateBackBufferCopySize
//
// Create/resize/destroy back buffer copy depending on what is required
//
////////////////////////////////////////////////////////////////
void CRenderItemManager::UpdateBackBufferCopySize()
{
    m_bBackBufferCopyMaybeNeedsResize = false;

    // Set what the max size requirement is for the back buffer copy
    uint uiSizeX = 0;
    uint uiSizeY = 0;
    for (std::set<CRenderItem*>::iterator iter = m_CreatedItemList.begin(); iter != m_CreatedItemList.end(); iter++)
    {
        if (CScreenSourceItem* pScreenSourceItem = DynamicCast<CScreenSourceItem>(*iter))
        {
            uiSizeX = std::max(uiSizeX, pScreenSourceItem->m_uiSizeX);
            uiSizeY = std::max(uiSizeY, pScreenSourceItem->m_uiSizeY);
        }
    }

    // Update?
    if (!m_pBackBufferCopy || m_pBackBufferCopy->m_uiSizeX != uiSizeX || m_pBackBufferCopy->m_uiSizeY != uiSizeY)
    {
        // Delete old one if it exists
        SAFE_RELEASE(m_pBackBufferCopy);

        // Try to create new one if needed
        if (uiSizeX > 0)
            m_pBackBufferCopy = CreateRenderTarget(uiSizeX, uiSizeY, false, false, (_D3DFORMAT)0, true);
    }
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::SetRenderTarget
//
// Set current render target to a custom one
//
////////////////////////////////////////////////////////////////
bool CRenderItemManager::SetRenderTarget(CRenderTargetItem* pItem, bool bClear)
{
    if (GetDeviceCooperativeLevel("SetRenderTarget") != D3D_OK)
        return false;

    if (pItem && !pItem->IsValid())
        pItem->TryEnsureValid();

    if (!pItem || !pItem->IsValid())
    {
        if (!m_pDefaultD3DRenderTarget)
            SaveDefaultRenderTarget();
        RestoreDefaultRenderTarget();
        return false;
    }

    if (!m_pDefaultD3DRenderTarget)
        SaveDefaultRenderTarget();

    if (!ChangeRenderTarget(pItem->m_uiSizeX, pItem->m_uiSizeY, pItem->m_pD3DRenderTargetSurface, pItem->m_pD3DZStencilSurface))
        return false;

    if (bClear)
        m_pDevice->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_ARGB(0, 0, 0, 0), 1, 0);

    return true;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::IsSetRenderTargetEnabledOldVer
//
// See if in enabled zones for old versions
//
////////////////////////////////////////////////////////////////
bool CRenderItemManager::IsSetRenderTargetEnabledOldVer()
{
    return m_bSetRenderTargetEnabledOldVer;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::EnableSetRenderTargetOldVer
//
// Enabled zones for old versions
//
////////////////////////////////////////////////////////////////
void CRenderItemManager::EnableSetRenderTargetOldVer(bool bEnable)
{
    m_bSetRenderTargetEnabledOldVer = bEnable;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::SaveDefaultRenderTarget
//
// Save current render target as the default one
//
////////////////////////////////////////////////////////////////
bool CRenderItemManager::SaveDefaultRenderTarget()
{
    if (GetDeviceCooperativeLevel("SaveDefaultRenderTarget") != D3D_OK)
        return false;

    // Make sure any special depth buffer has been removed
    SaveReadableDepthBuffer();

    // Update our info about what rendertarget is active
    IDirect3DSurface9* pActiveD3DRenderTarget = nullptr;
    const HRESULT      hrRenderTarget = m_pDevice->GetRenderTarget(0, &pActiveD3DRenderTarget);
    if (FAILED(hrRenderTarget) || !pActiveD3DRenderTarget)
    {
        SAFE_RELEASE(pActiveD3DRenderTarget);
        SAFE_RELEASE(m_pDefaultD3DRenderTarget);
        SAFE_RELEASE(m_pDefaultD3DZStencilSurface);
        return false;
    }

    IDirect3DSurface9* pActiveD3DZStencilSurface = nullptr;
    const HRESULT      hrDepthStencil = m_pDevice->GetDepthStencilSurface(&pActiveD3DZStencilSurface);
    if (FAILED(hrDepthStencil) && hrDepthStencil != D3DERR_NOTFOUND)
    {
        SAFE_RELEASE(pActiveD3DRenderTarget);
        SAFE_RELEASE(pActiveD3DZStencilSurface);
        SAFE_RELEASE(m_pDefaultD3DRenderTarget);
        SAFE_RELEASE(m_pDefaultD3DZStencilSurface);
        return false;
    }

    SAFE_RELEASE(m_pDefaultD3DRenderTarget);
    SAFE_RELEASE(m_pDefaultD3DZStencilSurface);

    m_pDefaultD3DRenderTarget = pActiveD3DRenderTarget;
    if (m_pDefaultD3DRenderTarget)
        m_pDefaultD3DRenderTarget->AddRef();

    m_pDefaultD3DZStencilSurface = pActiveD3DZStencilSurface;
    if (m_pDefaultD3DZStencilSurface)
        m_pDefaultD3DZStencilSurface->AddRef();

    SAFE_RELEASE(pActiveD3DRenderTarget);
    SAFE_RELEASE(pActiveD3DZStencilSurface);

    // Do this in case dxSetRenderTarget is being called from some unexpected place
    CGraphics::GetSingleton().MaybeEnteringMTARenderZone();
    return true;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::RestoreDefaultRenderTarget
//
// Set render target back to the default one
//
////////////////////////////////////////////////////////////////
bool CRenderItemManager::RestoreDefaultRenderTarget()
{
    // Only need to change if we have info
    if (m_pDefaultD3DRenderTarget)
    {
        if (ChangeRenderTarget(m_uiDefaultViewportSizeX, m_uiDefaultViewportSizeY, m_pDefaultD3DRenderTarget, m_pDefaultD3DZStencilSurface))
        {
            SAFE_RELEASE(m_pDefaultD3DRenderTarget);
            SAFE_RELEASE(m_pDefaultD3DZStencilSurface);

            // Do this in case dxSetRenderTarget is being called from some unexpected place
            CGraphics::GetSingleton().MaybeLeavingMTARenderZone();
        }
        else
        {
            return false;
        }
    }

    return true;
}

bool CRenderItemManager::ApplySceneViewOutputShader(CRenderTargetItem* pSource, CRenderTargetItem* pDestination, CShaderItem* pShader,
                                                    const SString& strInputName)
{
    m_strLastSceneViewOutputError.clear();
    if (!pSource || !pDestination || !pShader || !pSource->TryEnsureValid() || !pDestination->TryEnsureValid() || !pShader->IsValid())
    {
        m_strLastSceneViewOutputError = "invalid output-pass render item";
        WriteDebugEvent("ApplySceneViewOutputShader - an input render item is invalid");
        return false;
    }

    pShader->MaybeRenewShaderInstance();
    CShaderInstance* pInstance = pShader->m_pShaderInstance;
    ID3DXEffect*     pEffect = pInstance && pInstance->m_pEffectWrap ? pInstance->m_pEffectWrap->m_pD3DEffect : nullptr;
    D3DXHANDLE       hInput = pEffect ? pEffect->GetParameterByName(nullptr, strInputName) : nullptr;
    if (!hInput)
    {
        m_strLastSceneViewOutputError = SString("texture parameter '%s' is unavailable", *strInputName);
        WriteDebugEvent(SString("ApplySceneViewOutputShader - texture parameter '%s' is unavailable", *strInputName));
        return false;
    }

    // The native secondary-world hook can leave arbitrary GTA draw state active. Preserve it independently
    // of the render-target scope because D3D9 state blocks cover shader/blend/depth/sampler state while
    // CRenderStateScope deliberately owns targets, viewport and transforms.
    IDirect3DStateBlock9* pSavedDrawState = nullptr;
    if (FAILED(m_pDevice->CreateStateBlock(D3DSBT_ALL, &pSavedDrawState)) || !pSavedDrawState)
    {
        m_strLastSceneViewOutputError = "could not capture D3D draw state";
        WriteDebugEvent("ApplySceneViewOutputShader - could not capture D3D draw state");
        return false;
    }
    struct CScopedDrawState
    {
        ~CScopedDrawState()
        {
            pState->Apply();
            pState->Release();
        }
        IDirect3DStateBlock9* pState;
    } savedDrawState{pSavedDrawState};

    // Bind through the shader instance so ApplyShaderParameters cannot restore the effect default over
    // the SceneView input immediately before drawing. The binding is removed after the draw, avoiding a
    // persistent SceneView -> shader -> SceneView texture reference cycle.
    pInstance->SetTextureValue(hInput, pSource);

    CRenderTargetItem* targets[MAX_MRT_RENDER_TARGETS] = {pDestination, nullptr, nullptr, nullptr};
    if (!BeginRenderPass(targets, 1, nullptr, true))
    {
        m_strLastSceneViewOutputError = "could not begin intermediate render pass";
        pInstance->SetTextureValue(hInput, nullptr);
        WriteDebugEvent("ApplySceneViewOutputShader - could not begin intermediate render pass");
        return false;
    }

    CGraphics::GetSingleton().DrawMaterialImmediate(pInstance, static_cast<float>(pDestination->m_uiSizeX), static_cast<float>(pDestination->m_uiSizeY));
    const bool bEnded = EndRenderPass();
    pInstance->SetTextureValue(hInput, nullptr);
    pEffect->SetTexture(hInput, nullptr);
    if (!bEnded)
    {
        m_strLastSceneViewOutputError = "could not end intermediate render pass";
        WriteDebugEvent("ApplySceneViewOutputShader - could not end intermediate render pass");
        return false;
    }

    // D3DX leaves effect sampler bindings active after EndPass. Copying into pSource while it is still bound
    // as a texture is an invalid read/write hazard on D3D9 and was driver-timing dependent across resource
    // restarts. Unbind every pixel/vertex sampler before publishing; the saved state restores them on exit.
    for (DWORD i = 0; i < 16; ++i)
        m_pDevice->SetTexture(i, nullptr);
    for (DWORD i = 0; i < 4; ++i)
        m_pDevice->SetTexture(D3DVERTEXTEXTURESAMPLER0 + i, nullptr);

    const HRESULT hResult = HandleStretchRect(pDestination->m_pD3DRenderTargetSurface, nullptr, pSource->m_pD3DRenderTargetSurface, nullptr, D3DTEXF_NONE);
    if (FAILED(hResult))
    {
        m_strLastSceneViewOutputError = SString("final StretchRect failed: %08x", hResult);
        WriteDebugEvent(SString("ApplySceneViewOutputShader - final StretchRect failed: %08x", hResult));
    }
    return SUCCEEDED(hResult);
}

bool CRenderItemManager::IsSceneViewOutputShaderValid(CShaderItem* pShader, const SString& strInputName)
{
    ID3DXEffect* pEffect = pShader && pShader->m_pEffectWrap ? pShader->m_pEffectWrap->m_pD3DEffect : nullptr;
    return pEffect && pEffect->GetParameterByName(nullptr, strInputName);
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::IsUsingDefaultRenderTarget
//
// Check if currently drawing to the default render target
//
////////////////////////////////////////////////////////////////
bool CRenderItemManager::IsUsingDefaultRenderTarget()
{
    // If this is NULL, it means we haven't saved it, so aren't using another render target
    return m_pDefaultD3DRenderTarget == nullptr;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::ChangeRenderTarget
//
// Worker function to change the D3D render target
//
////////////////////////////////////////////////////////////////
bool CRenderItemManager::ChangeRenderTarget(uint uiSizeX, uint uiSizeY, IDirect3DSurface9* pD3DRenderTarget, IDirect3DSurface9* pD3DZStencilSurface)
{
    if (GetDeviceCooperativeLevel("ChangeRenderTarget") != D3D_OK)
        return false;

    // Make sure any special depth buffer has been removed
    SaveReadableDepthBuffer();

    // Check if we need to change
    IDirect3DSurface9* pCurrentRenderTarget = nullptr;
    HRESULT            hrRenderTarget = m_pDevice->GetRenderTarget(0, &pCurrentRenderTarget);
    if (FAILED(hrRenderTarget) || !pCurrentRenderTarget)
    {
        SAFE_RELEASE(pCurrentRenderTarget);
        return false;
    }

    IDirect3DSurface9* pCurrentZStencilSurface = nullptr;
    HRESULT            hrDepthStencil = m_pDevice->GetDepthStencilSurface(&pCurrentZStencilSurface);
    if (FAILED(hrDepthStencil) && hrDepthStencil != D3DERR_NOTFOUND)
    {
        SAFE_RELEASE(pCurrentRenderTarget);
        SAFE_RELEASE(pCurrentZStencilSurface);
        return false;
    }

    const bool bAlreadySet = (pD3DRenderTarget == pCurrentRenderTarget && pD3DZStencilSurface == pCurrentZStencilSurface);

    if (bAlreadySet)
    {
        SAFE_RELEASE(pCurrentRenderTarget);
        SAFE_RELEASE(pCurrentZStencilSurface);
        return true;
    }

    // Tell graphics things are about to change
    CGraphics::GetSingleton().OnChangingRenderTarget(uiSizeX, uiSizeY);

    // Do change
    hrRenderTarget = m_pDevice->SetRenderTarget(0, pD3DRenderTarget);
    if (FAILED(hrRenderTarget))
    {
        SAFE_RELEASE(pCurrentRenderTarget);
        SAFE_RELEASE(pCurrentZStencilSurface);
        return false;
    }

    HRESULT hrSetDepth = m_pDevice->SetDepthStencilSurface(pD3DZStencilSurface);
    if (FAILED(hrSetDepth))
    {
        m_pDevice->SetRenderTarget(0, pCurrentRenderTarget);
        m_pDevice->SetDepthStencilSurface(pCurrentZStencilSurface);
        SAFE_RELEASE(pCurrentRenderTarget);
        SAFE_RELEASE(pCurrentZStencilSurface);
        return false;
    }

    D3DVIEWPORT9 viewport;
    viewport.X = 0;
    viewport.Y = 0;
    viewport.Width = uiSizeX;
    viewport.Height = uiSizeY;
    viewport.MinZ = 0.0f;
    viewport.MaxZ = 1.0f;
    m_pDevice->SetViewport(&viewport);

    SAFE_RELEASE(pCurrentRenderTarget);
    SAFE_RELEASE(pCurrentZStencilSurface);

    return true;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::CanCreateRenderItem
//
//
//
////////////////////////////////////////////////////////////////
bool CRenderItemManager::CanCreateRenderItem(ClassId classId)
{
    // none:        Don't create font or rendertarget if no memory
    // no_mem:      Don't create font or rendertarget
    // low_mem:     Don't create font or rendertarget randomly
    // no_shader:   Don't validate any shaders

    if (classId == CRenderTargetItem::GetClassId() || classId == CGuiFontItem::GetClassId() || classId == CDxFontItem::GetClassId())
    {
        if (m_iMemoryKBFreeForMTA <= 0)
            return false;

        if (m_TestMode == DX_TEST_MODE_NO_MEM)
            return false;

        if (m_TestMode == DX_TEST_MODE_LOW_MEM)
        {
            if ((rand() % 1000) > 750)
                return false;
        }
    }
    else if (classId == CShaderItem::GetClassId())
    {
        if (m_TestMode == DX_TEST_MODE_NO_SHADER)
            return false;
    }
    return true;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::SetTestMode
//
//
//
////////////////////////////////////////////////////////////////
void CRenderItemManager::SetTestMode(eDxTestMode testMode)
{
    m_TestMode = testMode;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::UpdateMemoryUsage
//
// Should be called when a render item is created/destroyed or changes
//
////////////////////////////////////////////////////////////////
void CRenderItemManager::UpdateMemoryUsage()
{
    m_iTextureMemoryKBUsed = 0;
    m_iRenderTargetMemoryKBUsed = 0;
    m_iFontMemoryKBUsed = 0;
    for (std::set<CRenderItem*>::iterator iter = m_CreatedItemList.begin(); iter != m_CreatedItemList.end(); iter++)
    {
        CRenderItem* pRenderItem = *iter;
        if (!pRenderItem->GetIncludeInMemoryStats())
            continue;
        int iMemoryKBUsed = pRenderItem->GetVideoMemoryKBUsed();

        if (pRenderItem->IsA(CFileTextureItem::GetClassId()) || pRenderItem->IsA(CVectorGraphicItem::GetClassId()))
            m_iTextureMemoryKBUsed += iMemoryKBUsed;
        else if (pRenderItem->IsA(CRenderTargetItem::GetClassId()) || pRenderItem->IsA(CScreenSourceItem::GetClassId()) ||
                 pRenderItem->IsA(CDepthStencilTargetItem::GetClassId()))
            m_iRenderTargetMemoryKBUsed += iMemoryKBUsed;
        else if (pRenderItem->IsA(CGuiFontItem::GetClassId()) || pRenderItem->IsA(CDxFontItem::GetClassId()))
            m_iFontMemoryKBUsed += iMemoryKBUsed;
    }

    m_iMemoryKBFreeForMTA = m_iVideoCardMemoryKBForMTATotal;
    m_iMemoryKBFreeForMTA -= m_iFontMemoryKBUsed / 2;
    m_iMemoryKBFreeForMTA -= m_iTextureMemoryKBUsed / 4;
    m_iMemoryKBFreeForMTA -= m_iRenderTargetMemoryKBUsed;
    m_iMemoryKBFreeForMTA = std::max(0, m_iMemoryKBFreeForMTA);
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::GetDxStatus
//
//
//
////////////////////////////////////////////////////////////////
void CRenderItemManager::GetDxStatus(SDxStatus& outStatus)
{
    outStatus.testMode = m_TestMode;

    // Copy hardware settings
    outStatus.videoCard.strName = m_strVideoCardName;
    outStatus.videoCard.iInstalledMemoryKB = m_iVideoCardMemoryKBTotal;
    outStatus.videoCard.strPSVersion = m_strVideoCardPSVersion;
    outStatus.videoCard.depthBufferFormat = m_depthBufferFormat;
    outStatus.videoCard.iMaxAnisotropy = g_pDeviceState->AdapterState.MaxAnisotropicSetting;
    outStatus.videoCard.iNumSimultaneousRTs = g_pDeviceState->DeviceCaps.NumSimultaneousRTs;

    // State
    outStatus.state.iNumShadersUsingReadableDepthBuffer = m_ShadersUsingDepthBuffer.size();

    // Memory usage
    outStatus.videoMemoryKB.iFreeForMTA = m_iMemoryKBFreeForMTA;
    outStatus.videoMemoryKB.iUsedByFonts = m_iFontMemoryKBUsed;
    outStatus.videoMemoryKB.iUsedByTextures = m_iTextureMemoryKBUsed;
    outStatus.videoMemoryKB.iUsedByRenderTargets = m_iRenderTargetMemoryKBUsed;

    // Option settings
    CGameSettings* gameSettings = CCore::GetSingleton().GetGame()->GetSettings();
    outStatus.settings.bWindowed = GetVideoModeManager()->IsDisplayModeWindowed();
    outStatus.settings.iFullScreenStyle = GetVideoModeManager()->GetFullScreenStyle();
    outStatus.settings.iFXQuality = gameSettings->GetFXQuality();
    ;
    outStatus.settings.iDrawDistance = (gameSettings->GetDrawDistance() - 0.925f) / 0.8749f * 100;
    outStatus.settings.iAntiAliasing = gameSettings->GetAntiAliasing() - 1;
    outStatus.settings.bVolumetricShadows = false;
    outStatus.settings.bAllowScreenUpload = true;
    outStatus.settings.iStreamingMemory = 0;
    outStatus.settings.bGrassEffect = false;
    outStatus.settings.bHeatHaze = false;
    outStatus.settings.iAnisotropicFiltering = 0;
    outStatus.settings.aspectRatio = gameSettings->GetAspectRatio();
    outStatus.settings.bHUDMatchAspectRatio = true;
    outStatus.settings.fFieldOfView = 70;
    outStatus.settings.bHighDetailVehicles = false;
    outStatus.settings.bHighDetailPeds = false;
    outStatus.settings.bBlur = true;
    outStatus.settings.bCoronaReflections = false;
    outStatus.settings.bDynamicPedShadows = false;

    CVARS_GET("streaming_memory", outStatus.settings.iStreamingMemory);
    CVARS_GET("volumetric_shadows", outStatus.settings.bVolumetricShadows);
    CVARS_GET("allow_screen_upload", outStatus.settings.bAllowScreenUpload);
    CVARS_GET("grass", outStatus.settings.bGrassEffect);
    CVARS_GET("heat_haze", outStatus.settings.bHeatHaze);
    CVARS_GET("anisotropic", outStatus.settings.iAnisotropicFiltering);
    CVARS_GET("hud_match_aspect_ratio", outStatus.settings.bHUDMatchAspectRatio);
    CVARS_GET("fov", outStatus.settings.fFieldOfView);
    CVARS_GET("high_detail_vehicles", outStatus.settings.bHighDetailVehicles);
    CVARS_GET("high_detail_peds", outStatus.settings.bHighDetailPeds);
    CVARS_GET("blur", outStatus.settings.bBlur);
    CVARS_GET("corona_reflections", outStatus.settings.bCoronaReflections);
    CVARS_GET("dynamic_ped_shadows", outStatus.settings.bDynamicPedShadows);

    if (outStatus.settings.iFXQuality == 0)
    {
        // These are always off with low fx quality
        outStatus.settings.bVolumetricShadows = false;
        outStatus.settings.bGrassEffect = false;
    }

    if (outStatus.settings.iFXQuality < 2)
    {
        outStatus.settings.bDynamicPedShadows = false;
    }

    // Display color depth
    D3DFORMAT BackBufferFormat = g_pDeviceState->CreationState.PresentationParameters.BackBufferFormat;
    if (BackBufferFormat >= D3DFMT_R5G6B5 && BackBufferFormat < D3DFMT_A8R3G3B2)
        outStatus.settings.b32BitColor = 0;
    else
        outStatus.settings.b32BitColor = 1;

    // Modify if using test mode
    if (m_TestMode == DX_TEST_MODE_NO_MEM)
        outStatus.videoMemoryKB.iFreeForMTA = 0;

    if (m_TestMode == DX_TEST_MODE_LOW_MEM)
        outStatus.videoMemoryKB.iFreeForMTA = 1;

    if (m_TestMode == DX_TEST_MODE_NO_SHADER)
        outStatus.videoCard.strPSVersion = "0";
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::GetDxCapabilities
//
// Query actual device support for render-target formats, MRT and sampleable
// depth. Computed on demand (dxGetRenderCapabilities) rather than cached, since
// it is only ever CheckDeviceFormat calls - cheap, and only runs when a script
// actually asks. Every later creation function (typed RT, depth target, MRT
// set, scene view, cubemap) must validate against this instead of assuming
// support.
//
////////////////////////////////////////////////////////////////
void CRenderItemManager::GetDxCapabilities(SDxCapabilities& outCapabilities)
{
    outCapabilities = SDxCapabilities();

    const D3DCAPS9& caps = g_pDeviceState->DeviceCaps;

    int iPSMajor = (caps.PixelShaderVersion & 0xFF00) >> 8;
    int iVSMajor = (caps.VertexShaderVersion & 0xFF00) >> 8;
    outCapabilities.bPixelShader3Supported = iPSMajor >= 3;
    outCapabilities.bVertexShader3Supported = iVSMajor >= 3;

    outCapabilities.iMaxSimultaneousRenderTargets = caps.NumSimultaneousRTs;
    outCapabilities.iMaxBoundRenderTargets = std::min<int>(caps.NumSimultaneousRTs, MAX_MRT_RENDER_TARGETS);

    // DX9 has no per-render-target blend state - one blend setup is shared across every bound target
    outCapabilities.bIndependentMRTBlend = false;
    outCapabilities.bIndependentMRTWriteMasks = (caps.PrimitiveMiscCaps & D3DPMISCCAPS_INDEPENDENTWRITEMASKS) != 0;

    outCapabilities.bCubemapRenderTargetSupported = (caps.TextureCaps & D3DPTEXTURECAPS_CUBEMAP) != 0;
    outCapabilities.iMaxCubemapEdgeLength = outCapabilities.bCubemapRenderTargetSupported ? (int)caps.MaxTextureWidth : 0;

    // No fixed engine-side cap on concurrent/per-frame SceneViews or cubemap face renders - each is a full
    // secondary world pass, so cost scales directly with how many a script actually requests. -1 signals
    // "not artificially limited" rather than reporting a specific number that would suggest a real ceiling.
    outCapabilities.iMaxSceneViewsPerFrame = -1;
    outCapabilities.iMaxRenderPassNestingDepth = 4;

    // Reuse the readable-depth-format discovery already performed once at device
    // creation (CDirect3DEvents9::DiscoverReadableDepthFormat -> SetDepthBufferFormat)
    // instead of re-probing INTZ/DF24/DF16/RAWZ here.
    outCapabilities.depthTextureSampleFormat = m_depthBufferFormat;
    outCapabilities.bDepthTextureSamplingSupported = (m_depthBufferFormat != RFORMAT_UNKNOWN);

    IDirect3D9* pD3D = nullptr;
    if (m_pDevice)
        m_pDevice->GetDirect3D(&pD3D);

    if (pD3D)
    {
        D3DDISPLAYMODE displayMode;
        if (pD3D->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &displayMode) == D3D_OK)
        {
            static const struct
            {
                D3DFORMAT   format;
                const char* szName;
            } formatCheckList[] = {
                {D3DFMT_A8R8G8B8, "a8r8g8b8"},           {D3DFMT_X8R8G8B8, "x8r8g8b8"}, {D3DFMT_R5G6B5, "r5g6b5"},
                {D3DFMT_A1R5G5B5, "a1r5g5b5"},           {D3DFMT_A4R4G4B4, "a4r4g4b4"}, {D3DFMT_A2B10G10R10, "a2b10g10r10"},
                {D3DFMT_A2R10G10B10, "a2r10g10b10"},     {D3DFMT_A8B8G8R8, "a8b8g8r8"}, {D3DFMT_G16R16, "g16r16"},
                {D3DFMT_A16B16G16R16, "a16b16g16r16"},   {D3DFMT_R16F, "r16f"},         {D3DFMT_G16R16F, "g16r16f"},
                {D3DFMT_A16B16G16R16F, "a16b16g16r16f"}, {D3DFMT_R32F, "r32f"},         {D3DFMT_G32R32F, "g32r32f"},
                {D3DFMT_A32B32G32R32F, "a32b32g32r32f"},
            };

            outCapabilities.renderTargetFormats.reserve(NUMELMS(formatCheckList));
            for (const auto& entry : formatCheckList)
            {
                SDxCapabilities::SFormatCapability formatCap;
                formatCap.strFormatName = entry.szName;
                formatCap.bRenderable = pD3D->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, displayMode.Format, D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE,
                                                                entry.format) == D3D_OK;
                formatCap.bTextureable =
                    pD3D->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, displayMode.Format, 0, D3DRTYPE_TEXTURE, entry.format) == D3D_OK;
                formatCap.bFilterable = pD3D->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, displayMode.Format, D3DUSAGE_QUERY_FILTER, D3DRTYPE_TEXTURE,
                                                                entry.format) == D3D_OK;
                outCapabilities.renderTargetFormats.push_back(formatCap);
            }
        }
    }
    SAFE_RELEASE(pD3D);
}

void CRenderItemManager::GetShaderDiagnostics(CShaderItem* pShaderItem, SShaderDiagnostics& outDiagnostics)
{
    // Keep effect internals inside core; client modules receive only a pointer-free diagnostic snapshot.
    if (pShaderItem)
        pShaderItem->GetDiagnostics(outDiagnostics);
    else
        outDiagnostics = SShaderDiagnostics();
}

void CRenderItemManager::GetRenderStatistics(SRenderStatistics& outStatistics)
{
    // This snapshot deliberately reports durable counters and owned objects; GPU timings will be added separately
    // when asynchronous D3D9 query support exists, so scripts are never misled by CPU-side timing estimates.
    outStatistics = SRenderStatistics();
    outStatistics.uiOpenRenderPasses = static_cast<uint>(m_RenderPassStack.size());
    outStatistics.uiRenderPassesStarted = m_uiRenderPassesStarted;
    outStatistics.uiRenderPassFailures = m_uiRenderPassFailures;
    outStatistics.uiForcedRenderPassClosures = m_uiForcedRenderPassClosures;
    outStatistics.uiRenderItems = static_cast<uint>(m_CreatedItemList.size());
    outStatistics.iTextureMemoryKB = m_iTextureMemoryKBUsed;
    outStatistics.iRenderTargetMemoryKB = m_iRenderTargetMemoryKBUsed;
    outStatistics.iFontMemoryKB = m_iFontMemoryKBUsed;
    outStatistics.iFreeMemoryKB = m_iMemoryKBFreeForMTA;

    for (CRenderItem* pItem : m_CreatedItemList)
    {
        if (pItem->IsA(CShaderItem::GetClassId()))
            ++outStatistics.uiShaders;
        else if (pItem->IsA(CRenderTargetItem::GetClassId()))
            ++outStatistics.uiRenderTargets;
        else if (pItem->IsA(CDepthStencilTargetItem::GetClassId()))
            ++outStatistics.uiDepthTargets;
        else if (pItem->IsA(CMrtSetItem::GetClassId()))
            ++outStatistics.uiMrtSets;
        else if (pItem->IsA(CScreenSourceItem::GetClassId()))
            ++outStatistics.uiScreenSources;
    }
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::GetBitsPerPixel
//
// Returns bits per pixel for a given D3D format
//
////////////////////////////////////////////////////////////////
int CRenderItemManager::GetBitsPerPixel(D3DFORMAT Format)
{
    switch (Format)
    {
        case D3DFMT_A8R8G8B8:
        case D3DFMT_X8R8G8B8:
        case D3DFMT_Q8W8V8U8:
        case D3DFMT_X8L8V8U8:
        case D3DFMT_A2B10G10R10:
        case D3DFMT_V16U16:
        case D3DFMT_G16R16:
        case D3DFMT_D24X4S4:
        case D3DFMT_D32:
        case D3DFMT_D24X8:
        case D3DFMT_D24S8:
            return 32;

        case D3DFMT_R8G8B8:
            return 24;

        case D3DFMT_X1R5G5B5:
        case D3DFMT_R5G6B5:
        case D3DFMT_A1R5G5B5:
        case D3DFMT_D16:
        case D3DFMT_A8L8:
        case D3DFMT_V8U8:
        case D3DFMT_L6V5U5:
        case D3DFMT_D16_LOCKABLE:
        case D3DFMT_D15S1:
        case D3DFMT_A8P8:
        case D3DFMT_A8R3G3B2:
        case D3DFMT_X4R4G4B4:
        case D3DFMT_A4R4G4B4:
            return 16;

        case D3DFMT_R3G3B2:
        case D3DFMT_A4L4:
        case D3DFMT_P8:
        case D3DFMT_A8:
        case D3DFMT_L8:
        case D3DFMT_DXT2:
        case D3DFMT_DXT3:
        case D3DFMT_DXT4:
        case D3DFMT_DXT5:
            return 8;

        case D3DFMT_DXT1:
            return 4;

        default:
            return 32;  // unknown - guess at 32
    }
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::GetPitchDivisor
//
// Returns block width for a given D3D format
//
////////////////////////////////////////////////////////////////
int CRenderItemManager::GetPitchDivisor(D3DFORMAT Format)
{
    switch (Format)
    {
        case D3DFMT_DXT1:
        case D3DFMT_DXT2:
        case D3DFMT_DXT3:
        case D3DFMT_DXT4:
        case D3DFMT_DXT5:
            return 4;

        default:
            return 1;
    }
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::CalcD3DResourceMemoryKBUsage
//
// Calculate how much video memory a D3D resource will take
//
////////////////////////////////////////////////////////////////
int CRenderItemManager::CalcD3DResourceMemoryKBUsage(IDirect3DResource9* pD3DResource)
{
    D3DRESOURCETYPE type = pD3DResource->GetType();

    if (type == D3DRTYPE_SURFACE)
        return CalcD3DSurfaceMemoryKBUsage((IDirect3DSurface9*)pD3DResource);

    if (type == D3DRTYPE_TEXTURE)
        return CalcD3DTextureMemoryKBUsage((IDirect3DTexture9*)pD3DResource);

    if (type == D3DRTYPE_VOLUMETEXTURE)
        return CalcD3DVolumeTextureMemoryKBUsage((IDirect3DVolumeTexture9*)pD3DResource);

    if (type == D3DRTYPE_CUBETEXTURE)
        return CalcD3DCubeTextureMemoryKBUsage((IDirect3DCubeTexture9*)pD3DResource);

    return 0;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::CalcD3DSurfaceMemoryKBUsage
//
//
//
////////////////////////////////////////////////////////////////
int CRenderItemManager::CalcD3DSurfaceMemoryKBUsage(IDirect3DSurface9* pD3DSurface)
{
    D3DSURFACE_DESC surfaceDesc;
    pD3DSurface->GetDesc(&surfaceDesc);

    int iBitsPerPixel = GetBitsPerPixel(surfaceDesc.Format);
    int iMemoryUsed = surfaceDesc.Width * surfaceDesc.Height / 8 * iBitsPerPixel;

    return iMemoryUsed / 1024;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::CalcD3DTextureMemoryKBUsage
//
//
//
////////////////////////////////////////////////////////////////
int CRenderItemManager::CalcD3DTextureMemoryKBUsage(IDirect3DTexture9* pD3DTexture)
{
    int iMemoryUsed = 0;

    // Calc memory usage
    int iLevelCount = pD3DTexture->GetLevelCount();
    for (int i = 0; i < iLevelCount; i++)
    {
        D3DSURFACE_DESC surfaceDesc;
        pD3DTexture->GetLevelDesc(i, &surfaceDesc);

        int iBitsPerPixel = GetBitsPerPixel(surfaceDesc.Format);
        iMemoryUsed += surfaceDesc.Width * surfaceDesc.Height / 8 * iBitsPerPixel;
    }

    return iMemoryUsed / 1024;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::CalcD3DVolumeTextureMemoryKBUsage
//
//
//
////////////////////////////////////////////////////////////////
int CRenderItemManager::CalcD3DVolumeTextureMemoryKBUsage(IDirect3DVolumeTexture9* pD3DVolumeTexture)
{
    int iMemoryUsed = 0;

    // Calc memory usage
    int iLevelCount = pD3DVolumeTexture->GetLevelCount();
    for (int i = 0; i < iLevelCount; i++)
    {
        D3DVOLUME_DESC volumeDesc;
        pD3DVolumeTexture->GetLevelDesc(i, &volumeDesc);

        int iBitsPerPixel = GetBitsPerPixel(volumeDesc.Format);
        iMemoryUsed += volumeDesc.Width * volumeDesc.Height * volumeDesc.Depth / 8 * iBitsPerPixel;
    }

    return iMemoryUsed / 1024;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::CalcD3DCubeTextureMemoryKBUsage
//
//
//
////////////////////////////////////////////////////////////////
int CRenderItemManager::CalcD3DCubeTextureMemoryKBUsage(IDirect3DCubeTexture9* pD3DCubeTexture)
{
    int iMemoryUsed = 0;

    // Calc memory usage
    int iLevelCount = pD3DCubeTexture->GetLevelCount();
    for (int i = 0; i < iLevelCount; i++)
    {
        D3DSURFACE_DESC surfaceDesc;
        pD3DCubeTexture->GetLevelDesc(i, &surfaceDesc);

        int iBitsPerPixel = GetBitsPerPixel(surfaceDesc.Format);
        iMemoryUsed += surfaceDesc.Width * surfaceDesc.Height / 8 * iBitsPerPixel;
    }

    return iMemoryUsed * 6 / 1024;
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::NotifyShaderItemUsesDepthBuffer
//
//
//
////////////////////////////////////////////////////////////////
void CRenderItemManager::NotifyShaderItemUsesDepthBuffer(CShaderItem* pShaderItem, bool bUsesDepthBuffer)
{
    if (bUsesDepthBuffer)
        MapInsert(m_ShadersUsingDepthBuffer, pShaderItem);
    else
        MapRemove(m_ShadersUsingDepthBuffer, pShaderItem);
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::NotifyShaderItemUsesMultipleRenderTargets
//
//
//
////////////////////////////////////////////////////////////////
void CRenderItemManager::NotifyShaderItemUsesMultipleRenderTargets(CShaderItem* pShaderItem, bool bUsesMultipleRenderTargets)
{
    if (bUsesMultipleRenderTargets)
        MapInsert(m_ShadersUsingMultipleRenderTargets, pShaderItem);
    else
        MapRemove(m_ShadersUsingMultipleRenderTargets, pShaderItem);
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::PreDrawWorld
//
//
//
////////////////////////////////////////////////////////////////
void CRenderItemManager::PreDrawWorld()
{
    // Save scene matrices
    g_pDeviceState->MainSceneState.TransformState = g_pDeviceState->TransformState;
    IDirect3DTexture9*& pReadableDepthBuffer = g_pDeviceState->MainSceneState.DepthBuffer;

    // Determine what is needed
    bool bRequireDepthBuffer = false;
    if (!m_ShadersUsingDepthBuffer.empty() && m_depthBufferFormat != RFORMAT_UNKNOWN)
        bRequireDepthBuffer = true;

    bool bRequireNonAADisplay = false;
    if (g_pDeviceState->CreationState.PresentationParameters.MultiSampleType != D3DMULTISAMPLE_NONE)
        bRequireNonAADisplay = bRequireDepthBuffer || !m_ShadersUsingMultipleRenderTargets.empty();

    // Readable depth buffer is not compatible with volumetric shadows
    CCore::GetSingleton().GetGame()->GetSettings()->SetVolumetricShadowsSuspended(bRequireDepthBuffer);

    // Destroy old stuff that we don't need anymore
    if (!bRequireDepthBuffer)
    {
        SAFE_RELEASE(pReadableDepthBuffer);
    }
    if (!bRequireNonAADisplay)
    {
        SAFE_RELEASE(m_pNonAARenderTargetTexture);
        SAFE_RELEASE(m_pNonAARenderTarget);
        SAFE_RELEASE(m_pNonAADepthSurface2);
    }

    // Create new stuff that we need now
    if (bRequireDepthBuffer && !pReadableDepthBuffer)
    {
        // Create readable depth buffer
        m_pDevice->CreateTexture(m_uiDefaultViewportSizeX, m_uiDefaultViewportSizeY, 1, D3DUSAGE_DEPTHSTENCIL, (D3DFORMAT)m_depthBufferFormat, D3DPOOL_DEFAULT,
                                 &pReadableDepthBuffer, nullptr);
    }
    if (bRequireNonAADisplay && !m_pNonAARenderTarget)
    {
        // Create a non-AA render target and depth buffer
        assert(!m_pNonAARenderTargetTexture);
        assert(!m_pNonAADepthSurface2);

        const D3DPRESENT_PARAMETERS& pp = g_pDeviceState->CreationState.PresentationParameters;
        HRESULT                      hr = D3D_OK;
        if (!m_bIsSwiftShader)
        {
            hr = m_pDevice->CreateRenderTarget(m_uiDefaultViewportSizeX, m_uiDefaultViewportSizeY, pp.BackBufferFormat, D3DMULTISAMPLE_NONE, 0, false,
                                               &m_pNonAARenderTarget, nullptr);
            if (FAILED(hr) || !m_pNonAARenderTarget)
            {
                SAFE_RELEASE(m_pNonAARenderTarget);
                hr = FAILED(hr) ? hr : E_FAIL;
            }
        }
        else
        {
            // Render target texture is needed when emulating StretchRect
            hr = m_pDevice->CreateTexture(m_uiDefaultViewportSizeX, m_uiDefaultViewportSizeY, 1, D3DUSAGE_RENDERTARGET, pp.BackBufferFormat, D3DPOOL_DEFAULT,
                                          &m_pNonAARenderTargetTexture, nullptr);
            if (FAILED(hr) || !m_pNonAARenderTargetTexture)
            {
                SAFE_RELEASE(m_pNonAARenderTargetTexture);
                hr = FAILED(hr) ? hr : E_FAIL;
            }
            else
            {
                hr = m_pNonAARenderTargetTexture->GetSurfaceLevel(0, &m_pNonAARenderTarget);
                if (FAILED(hr) || !m_pNonAARenderTarget)
                {
                    SAFE_RELEASE(m_pNonAARenderTarget);
                    SAFE_RELEASE(m_pNonAARenderTargetTexture);
                    hr = FAILED(hr) ? hr : E_FAIL;
                }
            }
        }

        if (SUCCEEDED(hr))
        {
            hr = m_pDevice->CreateDepthStencilSurface(m_uiDefaultViewportSizeX, m_uiDefaultViewportSizeY, pp.AutoDepthStencilFormat, D3DMULTISAMPLE_NONE, 0,
                                                      true, &m_pNonAADepthSurface2, nullptr);
            if (FAILED(hr) || !m_pNonAADepthSurface2)
            {
                SAFE_RELEASE(m_pNonAADepthSurface2);
                SAFE_RELEASE(m_pNonAARenderTarget);
                SAFE_RELEASE(m_pNonAARenderTargetTexture);
            }
        }
    }

    // Set depth buffer and maybe render target
    if ((pReadableDepthBuffer || m_pNonAADepthSurface2) && m_pSavedSceneDepthSurface == nullptr)
    {
        if (m_pDevice->GetDepthStencilSurface(&m_pSavedSceneDepthSurface) == D3D_OK)
        {
            if (pReadableDepthBuffer)
            {
                // Set readable depth buffer
                IDirect3DSurface9* pSurf = nullptr;
                if (pReadableDepthBuffer->GetSurfaceLevel(0, &pSurf) == D3D_OK)
                {
                    m_pDevice->SetDepthStencilSurface(pSurf);
                    m_bUsingReadableDepthBuffer = true;
                    pSurf->Release();
                }
            }
            else
            {
                // Set non-AA depth buffer
                m_pDevice->SetDepthStencilSurface(m_pNonAADepthSurface2);
            }

            // Also switch to non-AA render target if created
            if (m_pNonAARenderTarget)
            {
                if (m_pDevice->GetRenderTarget(0, &m_pSavedSceneRenderTargetAA) == D3D_OK)
                {
                    m_pDevice->SetRenderTarget(0, m_pNonAARenderTarget);
                }
            }
            m_pDevice->Clear(0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, D3DCOLOR_ARGB(0, 0, 0, 0), 1, 0);
        }
    }
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::SaveReadableDepthBuffer
//
// Ensure our readable depth buffer is no longer being used
//
////////////////////////////////////////////////////////////////
void CRenderItemManager::SaveReadableDepthBuffer()
{
    if (m_bUsingReadableDepthBuffer)
    {
        m_bUsingReadableDepthBuffer = false;

        const HRESULT hrDeviceState = GetDeviceCooperativeLevel("SaveReadableDepthBuffer");
        const bool    bDeviceReady = (hrDeviceState == D3D_OK);

        // Ensure device operations are synchronous for GPU driver (especially Nvidia) compatibility
        if (bDeviceReady)
        {
            IDirect3DSurface9* pCurrentDepthSurface = nullptr;
            if (SUCCEEDED(m_pDevice->GetDepthStencilSurface(&pCurrentDepthSurface)))
            {
                // Force GPU to complete any pending depth buffer operations
                D3DLOCKED_RECT lockedRect;
                if (SUCCEEDED(pCurrentDepthSurface->LockRect(&lockedRect, nullptr, D3DLOCK_READONLY | D3DLOCK_DONOTWAIT)))
                {
                    pCurrentDepthSurface->UnlockRect();
                }
                SAFE_RELEASE(pCurrentDepthSurface);
            }
        }

        if (m_pNonAADepthSurface2)
        {
            // If using AA hacks, change to the other depth buffer we created
            if (bDeviceReady)
            {
                m_pDevice->SetDepthStencilSurface(m_pNonAADepthSurface2);
                m_pDevice->Clear(0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, D3DCOLOR_ARGB(0, 0, 0, 0), 1, 0);
            }
        }
        else
        {
            // If not using AA hacks, just change back to the GTA depth buffer
            if (m_pSavedSceneDepthSurface)
            {
                if (bDeviceReady)
                    m_pDevice->SetDepthStencilSurface(m_pSavedSceneDepthSurface);
                SAFE_RELEASE(m_pSavedSceneDepthSurface);
            }
        }

        // Additional sync point for GPU driver
        // Force immediate execution of depth buffer state changes when we can safely begin a scene
        if (bDeviceReady && !g_bInMTAScene.load(std::memory_order_acquire) && !g_bInGTAScene.load(std::memory_order_acquire))
        {
            if (!BeginSceneWithoutProxy(m_pDevice, ESceneOwner::MTA))
            {
                WriteDebugEvent("CRenderItemManager::SaveReadableDepthBuffer - BeginSceneWithoutProxy failed");
            }
            else if (!EndSceneWithoutProxy(m_pDevice, ESceneOwner::MTA))
            {
                WriteDebugEvent("CRenderItemManager::SaveReadableDepthBuffer - EndSceneWithoutProxy failed");
            }
        }
    }
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::FlushNonAARenderTarget
//
// If using AA hacks, change everything back
//
////////////////////////////////////////////////////////////////
void CRenderItemManager::FlushNonAARenderTarget()
{
    const HRESULT hrDeviceState = GetDeviceCooperativeLevel("FlushNonAARenderTarget");
    const bool    bDeviceReady = (hrDeviceState == D3D_OK);

    if (m_pSavedSceneDepthSurface)
    {
        if (bDeviceReady)
            m_pDevice->SetDepthStencilSurface(m_pSavedSceneDepthSurface);
        SAFE_RELEASE(m_pSavedSceneDepthSurface);
    }

    if (m_pSavedSceneRenderTargetAA)
    {
        // Restore GTA AA render target, and copy our non-AA data to it
        if (bDeviceReady && SUCCEEDED(m_pDevice->SetRenderTarget(0, m_pSavedSceneRenderTargetAA)))
        {
            if (m_pNonAARenderTarget)
            {
                if (!m_bIsSwiftShader)
                {
                    m_pDevice->StretchRect(m_pNonAARenderTarget, nullptr, m_pSavedSceneRenderTargetAA, nullptr, D3DTEXF_POINT);
                }
                else
                {
                    // Emulate StretchRect using DrawPrimitive

                    // Save render states
                    IDirect3DStateBlock9* pSavedStateBlock = nullptr;
                    m_pDevice->CreateStateBlock(D3DSBT_ALL, &pSavedStateBlock);

                    // Prepare vertex buffer
                    float fX1 = -0.5f;
                    float fY1 = -0.5f;
                    float fX2 = m_uiDefaultViewportSizeX + fX1;
                    float fY2 = m_uiDefaultViewportSizeY + fY1;
                    float fU1 = 0;
                    float fV1 = 0;
                    float fU2 = 1;
                    float fV2 = 1;

                    const SRTVertex vertices[] = {{fX1, fY1, 0, 1, fU1, fV1}, {fX2, fY1, 0, 1, fU2, fV1}, {fX1, fY2, 0, 1, fU1, fV2},
                                                  {fX2, fY1, 0, 1, fU2, fV1}, {fX2, fY2, 0, 1, fU2, fV2}, {fX1, fY2, 0, 1, fU1, fV2}};

                    // Set vertex stream
                    uint        PrimitiveCount = NUMELMS(vertices) / 3;
                    const void* pVertexStreamZeroData = &vertices[0];
                    uint        VertexStreamZeroStride = sizeof(SRTVertex);
                    m_pDevice->SetFVF(SRTVertex::FVF);

                    // Set render states
                    m_pDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
                    m_pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
                    m_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
                    m_pDevice->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
                    m_pDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
                    m_pDevice->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
                    m_pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
                    m_pDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
                    m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
                    m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
                    m_pDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
                    m_pDevice->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
                    m_pDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
                    m_pDevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
                    m_pDevice->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_POINT);

                    // Draw using texture
                    m_pDevice->SetTexture(0, m_pNonAARenderTargetTexture);
                    m_pDevice->DrawPrimitiveUP(D3DPT_TRIANGLELIST, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride);

                    // Restore render states
                    if (pSavedStateBlock)
                    {
                        pSavedStateBlock->Apply();
                        SAFE_RELEASE(pSavedStateBlock);
                    }
                }
            }
        }
        SAFE_RELEASE(m_pSavedSceneRenderTargetAA);
    }
}

////////////////////////////////////////////////////////////////
//
// CRenderItemManager::HandleStretchRect
//
// Maybe replace source surface with our non-AA rt, depending on things
//
////////////////////////////////////////////////////////////////
HRESULT CRenderItemManager::HandleStretchRect(IDirect3DSurface9* pSourceSurface, CONST RECT* pSourceRect, IDirect3DSurface9* pDestSurface,
                                              CONST RECT* pDestRect, int Filter)
{
    if (!pSourceSurface || !pDestSurface)
        return D3DERR_INVALIDCALL;

    const HRESULT hrDeviceState = GetDeviceCooperativeLevel("HandleStretchRect");
    if (hrDeviceState != D3D_OK)
        return hrDeviceState;

    if (pSourceSurface == m_pSavedSceneRenderTargetAA)
    {
        // If trying to copy from the saved render target, use the active render target instead
        IDirect3DSurface9* pActiveRenderTarget = nullptr;
        const HRESULT      hrGetRenderTarget = m_pDevice->GetRenderTarget(0, &pActiveRenderTarget);
        if (SUCCEEDED(hrGetRenderTarget) && pActiveRenderTarget)
        {
            const HRESULT hrStretch = m_pDevice->StretchRect(pActiveRenderTarget, pSourceRect, pDestSurface, pDestRect, (D3DTEXTUREFILTERTYPE)Filter);
            SAFE_RELEASE(pActiveRenderTarget);
            return hrStretch;
        }

        SAFE_RELEASE(pActiveRenderTarget);
        return SUCCEEDED(hrGetRenderTarget) ? D3DERR_INVALIDCALL : hrGetRenderTarget;
    }

    return m_pDevice->StretchRect(pSourceSurface, pSourceRect, pDestSurface, pDestRect, (D3DTEXTUREFILTERTYPE)Filter);
}
