/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *               (Shared logic for modifications)
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/shared_logic/CClientMrtSet.h
 *  PURPOSE:
 *
 *****************************************************************************/

// A validated group of color render targets (+ optional depth-stencil target),
// bindable together via dxBeginRenderPass. Extends CClientRenderElement
// directly, not CClientTexture - it isn't itself drawable via dxDrawImage.
class CClientMrtSet : public CClientRenderElement
{
    DECLARE_CLASS(CClientMrtSet, CClientRenderElement)
public:
    CClientMrtSet(CClientManager* pManager, ElementID ID, CMrtSetItem* pMrtSetItem) : ClassInit(this), CClientRenderElement(pManager, ID)
    {
        SetTypeName("dx-mrtset");
        m_pRenderItem = pMrtSetItem;
    }

    eClientEntityType GetType() const { return CCLIENTMRTSET; }

    // CClientMrtSet methods
    CMrtSetItem* GetMrtSetItem() { return (CMrtSetItem*)m_pRenderItem; }
};
