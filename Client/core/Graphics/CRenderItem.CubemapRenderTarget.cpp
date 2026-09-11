/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        core/CRenderItem.CubemapRenderTarget.cpp
 *  PURPOSE:
 *
 *****************************************************************************/

#include "StdInc.h"

////////////////////////////////////////////////////////////////
//
// CCubemapRenderTargetItem::PostConstruct
//
////////////////////////////////////////////////////////////////
void CCubemapRenderTargetItem::PostConstruct(CRenderItemManager* pManager, uint uiEdgeSize, int surfaceFormat)
{
    Super::PostConstruct(pManager, true);
    m_uiEdgeSize = uiEdgeSize;
    m_eSurfaceFormat = surfaceFormat;
    m_uiSurfaceSizeX = uiEdgeSize;
    m_uiSurfaceSizeY = uiEdgeSize;

    for (auto& pFace : m_pD3DFaceSurface)
        pFace = nullptr;

    CreateUnderlyingData();
}

////////////////////////////////////////////////////////////////
//
// CCubemapRenderTargetItem::PreDestruct
//
////////////////////////////////////////////////////////////////
void CCubemapRenderTargetItem::PreDestruct()
{
    ReleaseUnderlyingData();
    Super::PreDestruct();
}

////////////////////////////////////////////////////////////////
//
// CCubemapRenderTargetItem::IsValid
//
////////////////////////////////////////////////////////////////
bool CCubemapRenderTargetItem::IsValid()
{
    if (!m_pD3DTexture || !m_pD3DDepthStencilSurface)
        return false;
    for (IDirect3DSurface9* pFace : m_pD3DFaceSurface)
        if (!pFace)
            return false;
    return true;
}

////////////////////////////////////////////////////////////////
//
// CCubemapRenderTargetItem::OnLostDevice
//
////////////////////////////////////////////////////////////////
void CCubemapRenderTargetItem::OnLostDevice()
{
    ReleaseUnderlyingData();
}

////////////////////////////////////////////////////////////////
//
// CCubemapRenderTargetItem::OnResetDevice
//
////////////////////////////////////////////////////////////////
void CCubemapRenderTargetItem::OnResetDevice()
{
    CreateUnderlyingData();
}

////////////////////////////////////////////////////////////////
//
// CCubemapRenderTargetItem::CreateUnderlyingData
//
////////////////////////////////////////////////////////////////
void CCubemapRenderTargetItem::CreateUnderlyingData()
{
    assert(!m_pD3DTexture);

    IDirect3DCubeTexture9* pCubeTexture = nullptr;
    if (FAILED(m_pDevice->CreateCubeTexture(m_uiEdgeSize, 1, D3DUSAGE_RENDERTARGET, (D3DFORMAT)m_eSurfaceFormat, D3DPOOL_DEFAULT, &pCubeTexture, nullptr)))
        return;

    for (uint i = 0; i < 6; i++)
    {
        if (FAILED(pCubeTexture->GetCubeMapSurface((D3DCUBEMAP_FACES)i, 0, &m_pD3DFaceSurface[i])))
        {
            ReleaseUnderlyingData();
            SAFE_RELEASE(pCubeTexture);
            return;
        }
    }

    // One depth-stencil surface, reused for every face - see the class comment in
    // CRenderItemManagerInterface.h for why a single shared surface is sufficient here.
    if (FAILED(
            m_pDevice->CreateDepthStencilSurface(m_uiEdgeSize, m_uiEdgeSize, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, TRUE, &m_pD3DDepthStencilSurface, nullptr)))
    {
        ReleaseUnderlyingData();
        SAFE_RELEASE(pCubeTexture);
        return;
    }

    m_pD3DTexture = pCubeTexture;

    // Update memory used - approximate as 6 faces at the base format's bytes-per-pixel, plus the shared
    // depth-stencil surface, matching the style of CalcD3DResourceMemoryKBUsage's own surface-based sizing.
    m_iMemoryKBUsed = 6 * CRenderItemManager::CalcD3DResourceMemoryKBUsage(m_pD3DFaceSurface[0]) +
                      CRenderItemManager::CalcD3DResourceMemoryKBUsage(m_pD3DDepthStencilSurface);

    m_iRevision++;
}

////////////////////////////////////////////////////////////////
//
// CCubemapRenderTargetItem::ReleaseUnderlyingData
//
////////////////////////////////////////////////////////////////
void CCubemapRenderTargetItem::ReleaseUnderlyingData()
{
    for (IDirect3DSurface9*& pFace : m_pD3DFaceSurface)
        SAFE_RELEASE(pFace)
    SAFE_RELEASE(m_pD3DDepthStencilSurface)
    SAFE_RELEASE(m_pD3DTexture)
}
