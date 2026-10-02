/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *               (Shared logic for modifications)
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/shared_logic/CClientDepthStencilTarget.h
 *  PURPOSE:
 *
 *****************************************************************************/

// A standalone depth-stencil surface script element. Extends CClientTexture so a sampleable instance
// (dxCreateDepthStencilTarget's bSampleable = true) works with dxSetShaderValue/dxDrawImage exactly like
// any other texture-wrapping element, for free - see CDepthStencilTargetItem's own class comment for why
// it now extends CTextureItem. A non-sampleable instance still has no underlying texture (m_pD3DTexture
// stays null), so passing one to dxSetShaderValue/dxDrawImage fails the same way any texture element with
// no valid D3D texture would - not a new failure mode this class introduces.
class CClientDepthStencilTarget : public CClientTexture
{
    DECLARE_CLASS(CClientDepthStencilTarget, CClientTexture)
public:
    CClientDepthStencilTarget(CClientManager* pManager, ElementID ID, CDepthStencilTargetItem* pDepthStencilTargetItem)
        : ClassInit(this), CClientTexture(pManager, ID, pDepthStencilTargetItem)
    {
        SetTypeName("dx-depthstenciltarget");
    }

    eClientEntityType GetType() const { return CCLIENTDEPTHSTENCILTARGET; }

    // CClientDepthStencilTarget methods
    CDepthStencilTargetItem* GetDepthStencilTargetItem() { return (CDepthStencilTargetItem*)m_pRenderItem; }
};
