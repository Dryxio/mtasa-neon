/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        core/CRenderItem.MrtSet.cpp
 *  PURPOSE:
 *
 *****************************************************************************/

#include "StdInc.h"

////////////////////////////////////////////////////////////////
//
// CMrtSetItem::PostConstruct
//
// All validation (dimensions, device slot count) already happened in
// CRenderItemManager::CreateMrtSet - this just takes references.
//
////////////////////////////////////////////////////////////////
void CMrtSetItem::PostConstruct(CRenderItemManager* pManager, CRenderTargetItem* const targets[MAX_MRT_RENDER_TARGETS], uint uiNumTargets,
                                CDepthStencilTargetItem* pDepthStencilTargetItem)
{
    // Not included in memory stats - this item references existing GPU memory, it doesn't own any
    Super::PostConstruct(pManager, false);

    m_uiNumTargets = uiNumTargets;
    for (uint i = 0; i < MAX_MRT_RENDER_TARGETS; i++)
    {
        m_ColorTargets[i] = (i < uiNumTargets) ? targets[i] : nullptr;
        if (m_ColorTargets[i])
            m_ColorTargets[i]->AddRef();
    }

    m_pDepthStencilTarget = pDepthStencilTargetItem;
    if (m_pDepthStencilTarget)
        m_pDepthStencilTarget->AddRef();
}

////////////////////////////////////////////////////////////////
//
// CMrtSetItem::PreDestruct
//
////////////////////////////////////////////////////////////////
void CMrtSetItem::PreDestruct()
{
    for (uint i = 0; i < MAX_MRT_RENDER_TARGETS; i++)
        SAFE_RELEASE(m_ColorTargets[i]);

    SAFE_RELEASE(m_pDepthStencilTarget);

    Super::PreDestruct();
}

////////////////////////////////////////////////////////////////
//
// CMrtSetItem::IsValid
//
// Reflects current validity of the referenced items (which may have been
// recreated after a device reset), not a value cached at construction time.
//
////////////////////////////////////////////////////////////////
bool CMrtSetItem::IsValid()
{
    if (m_uiNumTargets == 0 || !m_ColorTargets[0] || !m_ColorTargets[0]->IsValid())
        return false;

    for (uint i = 1; i < m_uiNumTargets; i++)
    {
        if (!m_ColorTargets[i] || !m_ColorTargets[i]->IsValid())
            return false;
    }

    if (m_pDepthStencilTarget && !m_pDepthStencilTarget->IsValid())
        return false;

    return true;
}

////////////////////////////////////////////////////////////////
//
// CMrtSetItem::OnLostDevice / OnResetDevice
//
// No-ops - this item owns no D3D resource of its own. Each referenced color/
// depth-stencil target already receives its own OnLostDevice/OnResetDevice
// call directly from CRenderItemManager's created-item list.
//
////////////////////////////////////////////////////////////////
void CMrtSetItem::OnLostDevice()
{
}
void CMrtSetItem::OnResetDevice()
{
}
