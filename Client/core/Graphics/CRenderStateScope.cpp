/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        core/Graphics/CRenderStateScope.cpp
 *  PURPOSE:
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CRenderStateScope.h"
#include "DXHook/CProxyDirect3DDevice9.h"
#include <game/CCamera.h>
#include <game/CCam.h>
#include <game/CRenderWare.h>
#include <cmath>

namespace
{
    bool IsFiniteVector(const CVector& vec)
    {
        return std::isfinite(vec.fX) && std::isfinite(vec.fY) && std::isfinite(vec.fZ);
    }

    bool IsFiniteCameraMatrix(const CMatrix& matrix)
    {
        return IsFiniteVector(matrix.vPos) && IsFiniteVector(matrix.vFront) && IsFiniteVector(matrix.vUp) && IsFiniteVector(matrix.vRight);
    }
}  // namespace

////////////////////////////////////////////////////////////////
//
// CRenderStateScope::CRenderStateScope
//
// Captures render targets, depth-stencil and viewport unconditionally.
// Camera is not touched unless ApplyCamera() is called.
//
////////////////////////////////////////////////////////////////
CRenderStateScope::CRenderStateScope(IDirect3DDevice9* pDevice)
    : m_pDevice(pDevice),
      m_pSavedDrawState(nullptr),
      m_iNumRenderTargetSlots(0),
      m_bHasSavedDepthStencil(false),
      m_pSavedDepthStencil(nullptr),
      m_bHasSavedTransforms(false),
      m_bCameraApplied(false),
      m_fSavedCameraFOV(0.0f)
{
    for (auto& pSurface : m_SavedRenderTargets)
        pSurface = nullptr;
    ZeroMemory(&m_SavedViewport, sizeof(m_SavedViewport));

    if (!m_pDevice)
        return;

    // Render-target textures may still be bound by the previous frame's dxDrawImage. Preserve all draw and
    // sampler state before ApplyRenderTargets unbinds those textures to avoid D3D9 read/write hazards.
    m_pDevice->CreateStateBlock(D3DSBT_ALL, &m_pSavedDrawState);

    // Only capture as many slots as the device actually exposes - calling
    // GetRenderTarget beyond NumSimultaneousRTs is an invalid call on DX9.
    m_iNumRenderTargetSlots = std::max(1, std::min<int>(g_pDeviceState->DeviceCaps.NumSimultaneousRTs, MAX_MRT_RENDER_TARGETS));

    for (int i = 0; i < m_iNumRenderTargetSlots; ++i)
    {
        if (FAILED(m_pDevice->GetRenderTarget(i, &m_SavedRenderTargets[i])))
            m_SavedRenderTargets[i] = nullptr;
    }

    m_bHasSavedDepthStencil = SUCCEEDED(m_pDevice->GetDepthStencilSurface(&m_pSavedDepthStencil));
    if (!m_bHasSavedDepthStencil)
        m_pSavedDepthStencil = nullptr;

    m_pDevice->GetViewport(&m_SavedViewport);
    m_bHasSavedTransforms = SUCCEEDED(m_pDevice->GetTransform(D3DTS_WORLD, &m_SavedWorld)) && SUCCEEDED(m_pDevice->GetTransform(D3DTS_VIEW, &m_SavedView)) &&
                            SUCCEEDED(m_pDevice->GetTransform(D3DTS_PROJECTION, &m_SavedProjection));
}

////////////////////////////////////////////////////////////////
//
// CRenderStateScope::~CRenderStateScope
//
// Restores everything captured in the constructor, unconditionally.
//
////////////////////////////////////////////////////////////////
CRenderStateScope::~CRenderStateScope()
{
    if (!m_pDevice)
        return;

    // Flush anything drawn while this scope's targets were active, before switching back
    CGraphics::GetSingleton().OnChangingRenderTarget(m_SavedViewport.Width, m_SavedViewport.Height);

    if (m_pSavedDrawState)
    {
        m_pSavedDrawState->Apply();
        SAFE_RELEASE(m_pSavedDrawState);
    }

    for (int i = 0; i < m_iNumRenderTargetSlots; ++i)
    {
        m_pDevice->SetRenderTarget(i, m_SavedRenderTargets[i]);
        SAFE_RELEASE(m_SavedRenderTargets[i]);
    }

    m_pDevice->SetDepthStencilSurface(m_bHasSavedDepthStencil ? m_pSavedDepthStencil : nullptr);
    SAFE_RELEASE(m_pSavedDepthStencil);

    m_pDevice->SetViewport(&m_SavedViewport);

    if (m_bHasSavedTransforms)
    {
        // SceneViews execute native RenderWare draws, whose D3D9 backend caches transforms independently
        // from both the device and MTA's proxy. Restoring through raw IDirect3DDevice9::SetTransform leaves
        // that cache holding the secondary camera. An unchanged following SceneView then skips its own
        // SetTransform calls and renders with the restored primary matrix until its camera moves. Use the
        // backend entry point so all three views of transform state remain coherent.
        CGame*       pGame = CCore::GetSingleton().GetGame();
        CRenderWare* pRenderWare = pGame ? pGame->GetRenderWare() : nullptr;
        const bool   bWorldRestored = pRenderWare && pRenderWare->SetD3D9Transform(D3DTS_WORLD, &m_SavedWorld);
        const bool   bViewRestored = pRenderWare && pRenderWare->SetD3D9Transform(D3DTS_VIEW, &m_SavedView);
        const bool   bProjectionRestored = pRenderWare && pRenderWare->SetD3D9Transform(D3DTS_PROJECTION, &m_SavedProjection);
        if (!bWorldRestored || !bViewRestored || !bProjectionRestored)
        {
            // Device startup/shutdown can temporarily make the game interface unavailable. Raw restoration
            // is still preferable to leaking a SceneView transform in those non-rendering edge cases.
            m_pDevice->SetTransform(D3DTS_WORLD, &m_SavedWorld);
            m_pDevice->SetTransform(D3DTS_VIEW, &m_SavedView);
            m_pDevice->SetTransform(D3DTS_PROJECTION, &m_SavedProjection);
        }
    }

    if (m_bCameraApplied)
    {
        CCam*    pCam = nullptr;
        CCamera* pCamera = GetActiveGameCam(pCam);
        if (pCamera && pCam)
        {
            if (!m_SavedNativeCameraState.empty() && pCamera->SetStateSnapshot(m_SavedNativeCameraState.data(), m_SavedNativeCameraState.size()))
            {
                // The snapshot already contains every derived GTA camera field. Only publish its restored
                // matrix to the primary RenderWare frame; recalculating derived values here would overwrite
                // the exact state we just recovered.
                pCamera->CopyCameraMatrixToRWCam(true);
                return;
            }

            // Restore the exact camera values captured before the pass. Reconstructing them through
            // ApplyCameraMatrixToGame would orthonormalize and flip the matrix again, subtly changing the
            // primary camera on every SceneView. GTA's procedural sky reads the raw global camera matrix,
            // making that otherwise small difference visible as sky polygons generated for a wrong angle.
            pCamera->SetMatrix(&m_SavedCameraMatrix);
            *pCam->GetFront() = m_SavedCamFront;
            *pCam->GetUp() = m_SavedCamUp;
            *pCam->GetSource() = m_SavedCamSource;
            pCam->SetFOV(m_fSavedCameraFOV);
            pCamera->CopyCameraMatrixToRWCam(true);
            pCamera->CalculateDerivedValues(false, false);
        }
    }
}

void CRenderStateScope::DiscardSavedDrawState()
{
    SAFE_RELEASE(m_pSavedDrawState);
}

////////////////////////////////////////////////////////////////
//
// CRenderStateScope::ApplyRenderTargets
//
////////////////////////////////////////////////////////////////
bool CRenderStateScope::ApplyRenderTargets(IDirect3DSurface9* const targets[MAX_MRT_RENDER_TARGETS], IDirect3DSurface9* pDepthStencil, uint uiViewportSizeX,
                                           uint uiViewportSizeY)
{
    if (!m_pDevice || !targets || !targets[0])
        return false;

    CGraphics::GetSingleton().OnChangingRenderTarget(uiViewportSizeX, uiViewportSizeY);

    // A texture cannot be sampled while one of its surfaces is used as a render target. Clear every DX9
    // pixel and vertex sampler before binding targets; the scope's state block restores prior bindings.
    // D3DCAPS9::MaxSimultaneousTextures is the fixed-function texture-stage count and may be only 8;
    // programmable SM3 pixel shaders can bind all 16 sampler registers.
    for (DWORD i = 0; i < 16; ++i)
        m_pDevice->SetTexture(i, nullptr);
    for (DWORD i = 0; i < 4; ++i)
        m_pDevice->SetTexture(D3DVERTEXTEXTURESAMPLER0 + i, nullptr);

    for (int i = 0; i < m_iNumRenderTargetSlots; ++i)
    {
        if (FAILED(m_pDevice->SetRenderTarget(i, targets[i])))
            return false;
    }

    if (FAILED(m_pDevice->SetDepthStencilSurface(pDepthStencil)))
        return false;

    D3DVIEWPORT9 viewport;
    viewport.X = 0;
    viewport.Y = 0;
    viewport.Width = uiViewportSizeX;
    viewport.Height = uiViewportSizeY;
    viewport.MinZ = 0.0f;
    viewport.MaxZ = 1.0f;
    return SUCCEEDED(m_pDevice->SetViewport(&viewport));
}

////////////////////////////////////////////////////////////////
//
// CRenderStateScope::ApplyViewport
//
////////////////////////////////////////////////////////////////
bool CRenderStateScope::ApplyViewport(const D3DVIEWPORT9& viewport)
{
    if (!m_pDevice)
        return false;

    return SUCCEEDED(m_pDevice->SetViewport(&viewport));
}

////////////////////////////////////////////////////////////////
//
// CRenderStateScope::ApplyCamera
//
// Mirrors CClientCamera::GetGtaMatrix/SetGtaMatrix's exact approach to
// reading/writing the active CCam, since that is the proven-safe way MTA
// already moves the camera (backs the scripting setCameraMatrix function).
//
////////////////////////////////////////////////////////////////
bool CRenderStateScope::ApplyCamera(const CMatrix& matrix, float fFOV)
{
    if (!IsFiniteCameraMatrix(matrix) || !std::isfinite(fFOV) || fFOV <= 0.0f)
        return false;

    CCam*    pCam = nullptr;
    CCamera* pCamera = GetActiveGameCam(pCam);
    if (!pCamera || !pCam)
        return false;

    if (!m_bCameraApplied)
    {
        const size_t uiSnapshotSize = pCamera->GetStateSnapshotSize();
        if (uiSnapshotSize)
        {
            m_SavedNativeCameraState.resize(uiSnapshotSize);
            if (!pCamera->GetStateSnapshot(m_SavedNativeCameraState.data(), m_SavedNativeCameraState.size()))
                m_SavedNativeCameraState.clear();
        }

        pCamera->GetMatrix(&m_SavedCameraMatrix);
        m_SavedCamFront = *pCam->GetFront();
        m_SavedCamUp = *pCam->GetUp();
        m_SavedCamSource = *pCam->GetSource();
        m_fSavedCameraFOV = pCam->GetFOV();
        m_bCameraApplied = true;
    }

    ApplyCameraMatrixToGame(pCamera, pCam, matrix, fFOV);
    return true;
}

////////////////////////////////////////////////////////////////
//
// CRenderStateScope::GetActiveGameCam
//
////////////////////////////////////////////////////////////////
CCamera* CRenderStateScope::GetActiveGameCam(CCam*& outCam)
{
    outCam = nullptr;

    CGame* pGame = CCore::GetSingleton().GetGame();
    if (!pGame)
        return nullptr;

    CCamera* pCamera = pGame->GetCamera();
    if (!pCamera)
        return nullptr;

    outCam = pCamera->GetCam(pCamera->GetActiveCam());
    return pCamera;
}

////////////////////////////////////////////////////////////////
//
// CRenderStateScope::ApplyCameraMatrixToGame
//
////////////////////////////////////////////////////////////////
void CRenderStateScope::ApplyCameraMatrixToGame(CCamera* pCamera, CCam* pCam, const CMatrix& matrix, float fFOV)
{
    CMatrix matNew = matrix;
    matNew.OrthoNormalize(CMatrix::AXIS_FRONT, CMatrix::AXIS_UP);
    matNew.vRight = -matNew.vRight;  // Camera has this the other way round

    pCamera->SetMatrix(&matNew);
    *pCam->GetUp() = matNew.vUp;
    *pCam->GetFront() = matNew.vFront;
    *pCam->GetSource() = matNew.vPos;
    pCam->SetFOV(fFOV);

    // GTA's mirror path performs these exact two operations after replacing m_cameraMatrix. SetMatrix alone
    // does not update the RenderWare camera frame or world-space frustum planes, which leaves static world
    // sectors using the primary camera while dynamic entities partially follow the secondary CCam.
    pCamera->CopyCameraMatrixToRWCam(true);
    pCamera->CalculateDerivedValues(false, false);
}
