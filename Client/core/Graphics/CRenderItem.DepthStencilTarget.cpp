/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        core/CRenderItem.DepthStencilTarget.cpp
 *  PURPOSE:
 *
 *****************************************************************************/

#include "StdInc.h"

////////////////////////////////////////////////////////////////
//
// CDepthStencilTargetItem::PostConstruct
//
////////////////////////////////////////////////////////////////
void CDepthStencilTargetItem::PostConstruct(CRenderItemManager* pManager, uint uiSizeX, uint uiSizeY, int surfaceFormat, bool bSampleable,
                                            bool bIncludeInMemoryStats)
{
    Super::PostConstruct(pManager, bIncludeInMemoryStats);
    m_uiSizeX = uiSizeX;
    m_uiSizeY = uiSizeY;
    m_eFormat = surfaceFormat;
    m_bSampleable = bSampleable;

    // Initial creation of d3d data
    CreateUnderlyingData();
}

////////////////////////////////////////////////////////////////
//
// CDepthStencilTargetItem::PreDestruct
//
////////////////////////////////////////////////////////////////
void CDepthStencilTargetItem::PreDestruct()
{
    ReleaseUnderlyingData();
    Super::PreDestruct();
}

////////////////////////////////////////////////////////////////
//
// CDepthStencilTargetItem::IsValid
//
////////////////////////////////////////////////////////////////
bool CDepthStencilTargetItem::IsValid()
{
    if (!m_pD3DDepthStencilSurface)
        return false;
    return !m_bSampleable || m_pD3DTexture != nullptr;
}

////////////////////////////////////////////////////////////////
//
// CDepthStencilTargetItem::OnLostDevice
//
////////////////////////////////////////////////////////////////
void CDepthStencilTargetItem::OnLostDevice()
{
    ReleaseUnderlyingData();
    m_uiLastEnsureAttempt = 0;
    m_uiEnsureDelayMs = 0;
}

////////////////////////////////////////////////////////////////
//
// CDepthStencilTargetItem::OnResetDevice
//
////////////////////////////////////////////////////////////////
void CDepthStencilTargetItem::OnResetDevice()
{
    CreateUnderlyingData();
    m_uiLastEnsureAttempt = 0;
    m_uiEnsureDelayMs = 0;
}

////////////////////////////////////////////////////////////////
//
// CDepthStencilTargetItem::CreateUnderlyingData
//
////////////////////////////////////////////////////////////////
void CDepthStencilTargetItem::CreateUnderlyingData()
{
    assert(!m_pD3DDepthStencilSurface);
    assert(!m_pD3DTexture);

    if (m_bSampleable)
    {
        // The vendor FourCC trick: a texture created with D3DUSAGE_DEPTHSTENCIL and one of the
        // INTZ/DF24/DF16/RAWZ formats is simultaneously bindable as a real depth-stencil surface (via
        // GetSurfaceLevel) and sampleable as an ordinary texture. m_pD3DTexture (inherited from
        // CTextureItem) keeps it alive for sampling; m_pD3DDepthStencilSurface is the same underlying
        // memory exposed as the surface BeginCubemapFaceRender/BeginSceneViewRender-style binding needs.
        IDirect3DTexture9* pTexture = nullptr;
        if (FAILED(m_pDevice->CreateTexture(m_uiSizeX, m_uiSizeY, 1, D3DUSAGE_DEPTHSTENCIL, (D3DFORMAT)m_eFormat, D3DPOOL_DEFAULT, &pTexture, nullptr)))
            return;

        if (FAILED(pTexture->GetSurfaceLevel(0, &m_pD3DDepthStencilSurface)))
        {
            SAFE_RELEASE(pTexture);
            return;
        }

        m_pD3DTexture = pTexture;
        m_uiSurfaceSizeX = m_uiSizeX;
        m_uiSurfaceSizeY = m_uiSizeY;
    }
    else
    {
        if (FAILED(m_pDevice->CreateDepthStencilSurface(m_uiSizeX, m_uiSizeY, (D3DFORMAT)m_eFormat, D3DMULTISAMPLE_NONE, 0, TRUE, &m_pD3DDepthStencilSurface,
                                                        NULL)))
            return;
    }

    // Update memory used
    m_iMemoryKBUsed = CRenderItemManager::CalcD3DResourceMemoryKBUsage(m_pD3DDepthStencilSurface);

    // Update revision counter
    m_iRevision++;
}

////////////////////////////////////////////////////////////////
//
// CDepthStencilTargetItem::ReleaseUnderlyingData
//
////////////////////////////////////////////////////////////////
void CDepthStencilTargetItem::ReleaseUnderlyingData()
{
    SAFE_RELEASE(m_pD3DDepthStencilSurface)
    SAFE_RELEASE(m_pD3DTexture)
}
