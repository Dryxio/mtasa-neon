/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *               (Shared logic for modifications)
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        CMaterialPrimitive3DBatcher.cpp
 *  PURPOSE:
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/
#include <StdInc.h>
#include "CMaterialPrimitive3DBatcher.h"
#include "DXHook/CProxyDirect3DDevice9.h"
#include "DXHook/CDirect3DEvents9.h"

CMaterialPrimitive3DBatcher::CMaterialPrimitive3DBatcher(bool bPreGUI, CGraphics* pGraphics) : m_bPreGUI(bPreGUI), m_pGraphics(pGraphics)
{
}

CMaterialPrimitive3DBatcher::~CMaterialPrimitive3DBatcher()
{
    ClearQueue();
}

void CMaterialPrimitive3DBatcher::OnDeviceCreate(IDirect3DDevice9* pDevice, float fViewportSizeX, float fViewportSizeY)
{
    m_pDevice = pDevice;
}

void CMaterialPrimitive3DBatcher::Flush()
{
    if (m_primitiveList.empty())
        return;

    D3DXMATRIX matWorld;
    D3DXMatrixIdentity(&matWorld);
    const D3DXMATRIX& matView = g_pDeviceState->TransformState.VIEW;
    const D3DXMATRIX& matProjection = g_pDeviceState->TransformState.PROJECTION;

    m_pDevice->SetTransform(D3DTS_WORLD, &matWorld);
    m_pDevice->SetTransform(D3DTS_VIEW, &matView);
    m_pDevice->SetTransform(D3DTS_PROJECTION, &matProjection);

    IDirect3DStateBlock9* pSavedStateBlock = nullptr;
    m_pDevice->CreateStateBlock(D3DSBT_ALL, &pSavedStateBlock);

    if (g_pDeviceState->AdapterState.bRequiresClipping)
        m_pDevice->SetRenderState(D3DRS_CLIPPING, TRUE);
    m_pDevice->SetRenderState(D3DRS_ZENABLE, m_bPreGUI ? D3DZB_TRUE : D3DZB_FALSE);
    m_pDevice->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    m_pDevice->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    m_pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    m_pDevice->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
    m_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    m_pDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    m_pDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    m_pDevice->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
    m_pDevice->SetRenderState(D3DRS_ALPHAREF, 0x01);
    m_pDevice->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
    m_pDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
    m_pDevice->SetRenderState(D3DRS_FOGENABLE, TRUE);
    m_pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    m_pDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    m_pDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    m_pDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    m_pDevice->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    m_pDevice->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    m_pDevice->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    m_pDevice->SetFVF(PrimitiveMaterialVertice::FNV);

    m_pDevice->SetTexture(0, nullptr);
    CMaterialItem* pLastMaterial = nullptr;
    const uint uiVertexStreamZeroStride = sizeof(PrimitiveMaterialVertice);

    for (auto& primitive : m_primitiveList)
    {
        if (!primitive.pVecVertices || primitive.pVecVertices->empty())
            continue;

        const void* pVertexStreamZeroData = primitive.pVecVertices->data();

        if (primitive.pRawTexture)
        {
            // pRawTexture is already unwrapped to the real D3D texture when it
            // enters the queue. Keeping only the real COM object here makes the
            // queued lifetime safe and avoids calling AddRef/Release on GTA's
            // proxy wrapper.
            m_pDevice->SetTexture(0, primitive.pRawTexture);
            DrawPrimitive(primitive.eType, primitive.pVecVertices->size(), pVertexStreamZeroData, uiVertexStreamZeroStride);
            pLastMaterial = nullptr;
            continue;
        }

        CMaterialItem* pMaterial = primitive.pMaterial;
        if (!pMaterial)
            continue;

        if (pMaterial != pLastMaterial)
        {
            m_pDevice->SetSamplerState(0, D3DSAMP_ADDRESSU, pMaterial->m_TextureAddress);
            m_pDevice->SetSamplerState(0, D3DSAMP_ADDRESSV, pMaterial->m_TextureAddress);
            if (pMaterial->m_TextureAddress == TADDRESS_BORDER)
                m_pDevice->SetSamplerState(0, D3DSAMP_BORDERCOLOR, pMaterial->m_uiBorderColor);
        }

        if (CTextureItem* pTextureItem = DynamicCast<CTextureItem>(pMaterial))
        {
            if (CRenderTargetItem* pRenderTarget = DynamicCast<CRenderTargetItem>(pTextureItem))
            {
                if (!pRenderTarget->TryEnsureValid())
                    continue;
            }
            else if (CScreenSourceItem* pScreenSource = DynamicCast<CScreenSourceItem>(pTextureItem))
            {
                if (!pScreenSource->TryEnsureValid())
                    continue;
            }

            if (!pTextureItem->m_pD3DTexture)
                continue;

            m_pDevice->SetTexture(0, pTextureItem->m_pD3DTexture);
            DrawPrimitive(primitive.eType, primitive.pVecVertices->size(), pVertexStreamZeroData, uiVertexStreamZeroStride);
        }
        else if (CShaderInstance* pShaderInstance = DynamicCast<CShaderInstance>(pMaterial))
        {
            ID3DXEffect* pD3DEffect = pShaderInstance->m_pEffectWrap->m_pD3DEffect;
            if (pMaterial != pLastMaterial)
            {
                pShaderInstance->ApplyShaderParameters();
                pShaderInstance->m_pEffectWrap->ApplyCommonHandles();
                pShaderInstance->m_pEffectWrap->ApplyMappedHandles();
            }

            DWORD dwFlags = D3DXFX_DONOTSAVESHADERSTATE;
            uint uiNumPasses = 0;
            pShaderInstance->m_pEffectWrap->Begin(&uiNumPasses, dwFlags, false);
            for (uint uiPass = 0; uiPass < uiNumPasses; uiPass++)
            {
                pD3DEffect->BeginPass(uiPass);
                DrawPrimitive(primitive.eType, primitive.pVecVertices->size(), pVertexStreamZeroData, uiVertexStreamZeroStride);
                pD3DEffect->EndPass();
            }
            pShaderInstance->m_pEffectWrap->End();
            if (dwFlags & D3DXFX_DONOTSAVESHADERSTATE)
            {
                m_pDevice->SetVertexShader(NULL);
                m_pDevice->SetPixelShader(NULL);
            }
        }

        pLastMaterial = pMaterial;
        pMaterial->Release();
        primitive.pMaterial = nullptr;
    }

    ClearQueue();

    if (pSavedStateBlock)
    {
        pSavedStateBlock->Apply();
        SAFE_RELEASE(pSavedStateBlock);
    }
}

void CMaterialPrimitive3DBatcher::ClearQueue()
{
    for (auto& primitive : m_primitiveList)
    {
        SAFE_RELEASE(primitive.pMaterial);
        SAFE_RELEASE(primitive.pRawTexture);
        delete primitive.pVecVertices;
        primitive.pVecVertices = nullptr;
    }
    m_primitiveList.clear();
}

void CMaterialPrimitive3DBatcher::DrawPrimitive(D3DPRIMITIVETYPE eType, size_t iCollectionSize, const void* pDataAddr, size_t uiVertexStride)
{
    int iSize = 1;
    switch (eType)
    {
        case D3DPT_POINTLIST: iSize = iCollectionSize; break;
        case D3DPT_LINELIST: iSize = iCollectionSize / 2; break;
        case D3DPT_LINESTRIP: iSize = iCollectionSize - 1; break;
        case D3DPT_TRIANGLEFAN:
        case D3DPT_TRIANGLESTRIP: iSize = iCollectionSize - 2; break;
        case D3DPT_TRIANGLELIST: iSize = iCollectionSize / 3; break;
    }
    m_pDevice->DrawPrimitiveUP(eType, iSize, pDataAddr, uiVertexStride);
}

void CMaterialPrimitive3DBatcher::AddPrimitive(D3DPRIMITIVETYPE eType, CMaterialItem* pMaterial, std::vector<PrimitiveMaterialVertice>* pVecVertices)
{
    if (!pMaterial || !pVecVertices)
    {
        delete pVecVertices;
        return;
    }

    pMaterial->AddRef();
    m_primitiveList.push_back({eType, pMaterial, nullptr, pVecVertices});
}

void CMaterialPrimitive3DBatcher::AddRawPrimitive(D3DPRIMITIVETYPE eType, IDirect3DTexture9* pTexture,
                                                   std::vector<PrimitiveMaterialVertice>* pVecVertices)
{
    if (!pTexture || !pVecVertices)
    {
        delete pVecVertices;
        return;
    }

    // GTA/RenderWare gives us a CProxyDirect3DTexture9-facing pointer. Never
    // call COM methods on that pointer from the raw core renderer. Resolve the
    // underlying real texture first, then retain that object across the queue.
    IDirect3DBaseTexture9* pRealBaseTexture = CDirect3DEvents9::GetRealTexture(pTexture);
    IDirect3DTexture9* pRealTexture = reinterpret_cast<IDirect3DTexture9*>(pRealBaseTexture);
    if (!pRealTexture)
    {
        delete pVecVertices;
        return;
    }

    pRealTexture->AddRef();
    m_primitiveList.push_back({eType, nullptr, pRealTexture, pVecVertices});
}

void CGraphics::DrawRawMaterialPrimitive3DQueued(std::vector<PrimitiveMaterialVertice>* pVecVertices, D3DPRIMITIVETYPE eType,
                                                  IDirect3DTexture9* pTexture, eRenderStage stage)
{
    if (!pVecVertices)
        return;
    if (g_pCore->IsWindowMinimized() || !pTexture)
    {
        delete pVecVertices;
        return;
    }

    switch (stage)
    {
        case eRenderStage::POST_GUI:
            m_pMaterialPrimitive3DBatcherPostGUI->AddRawPrimitive(eType, pTexture, pVecVertices);
            break;
        case eRenderStage::POST_FX:
            m_pMaterialPrimitive3DBatcherPostFX->AddRawPrimitive(eType, pTexture, pVecVertices);
            break;
        case eRenderStage::PRE_FX:
        default:
            m_pMaterialPrimitive3DBatcherPreGUI->AddRawPrimitive(eType, pTexture, pVecVertices);
            break;
    }
}
