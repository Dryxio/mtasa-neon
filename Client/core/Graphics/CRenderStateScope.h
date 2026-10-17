/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        core/Graphics/CRenderStateScope.h
 *  PURPOSE:
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <CMatrix.h>
#include <vector>

class CCamera;
class CCam;

////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////
//
// CRenderStateScope
//
// The one RAII render-target/depth-stencil/viewport/camera save-apply-restore
// primitive every new rendering feature (render passes, MRT sets, scene
// views, cubemap faces, shadow cameras) must use instead of hand-rolling its
// own GetRenderTarget/SetRenderTarget pair. Render targets, depth-stencil
// and viewport are captured unconditionally on construction and restored
// unconditionally on destruction - including on early-return paths, since
// restoration happens in the destructor. Camera state is only touched if
// ApplyCamera() is actually called.
//
// Instances nest on the C++ call stack (do not heap-allocate and outlive the
// scope you intend to guard). Not copyable - one scope owns one save/restore
// cycle.
//
////////////////////////////////////////////////////////////////
class CRenderStateScope
{
public:
    explicit CRenderStateScope(IDirect3DDevice9* pDevice);
    ~CRenderStateScope();

    CRenderStateScope(const CRenderStateScope&) = delete;
    CRenderStateScope& operator=(const CRenderStateScope&) = delete;

    // targets[0] must be non-null (D3D9 requires render-target slot 0 to always be bound).
    // Unused slots must be null. Slots beyond the device's actual NumSimultaneousRTs are
    // silently not touched here - the higher-level MRT-set creation API is responsible for
    // rejecting a slot count the device can't support before it ever reaches this call.
    // Also applies a default full-surface viewport sized uiViewportSizeX/Y; call
    // ApplyViewport() afterwards on the same scope for a custom viewport (eg. one cubemap face).
    bool ApplyRenderTargets(IDirect3DSurface9* const targets[MAX_MRT_RENDER_TARGETS], IDirect3DSurface9* pDepthStencil, uint uiViewportSizeX,
                            uint uiViewportSizeY);

    bool ApplyViewport(const D3DVIEWPORT9& viewport);

    // Snapshots the currently active GTA camera's matrix + FOV (the same values
    // setCameraMatrix ultimately writes) on first use, and overrides them for the
    // active cam. Safe to call more than once on the same scope (only the state from
    // before the *first* call is restored by the destructor).
    bool ApplyCamera(const CMatrix& matrix, float fFOV);

    // Native RenderWare world rendering maintains its own D3D9 state cache. Applying a raw D3D state block
    // afterwards would change the device without updating that cache, so SceneViews discard this snapshot and
    // retain only the explicitly scoped target, viewport, transform and camera restoration.
    void DiscardSavedDrawState();

private:
    static CCamera* GetActiveGameCam(CCam*& outCam);
    static void     ApplyCameraMatrixToGame(CCamera* pCamera, CCam* pCam, const CMatrix& matrix, float fFOV);

    IDirect3DDevice9*     m_pDevice;
    IDirect3DStateBlock9* m_pSavedDrawState;

    int                m_iNumRenderTargetSlots;
    IDirect3DSurface9* m_SavedRenderTargets[MAX_MRT_RENDER_TARGETS];
    bool               m_bHasSavedDepthStencil;
    IDirect3DSurface9* m_pSavedDepthStencil;
    D3DVIEWPORT9       m_SavedViewport;
    D3DXMATRIX         m_SavedWorld;
    D3DXMATRIX         m_SavedView;
    D3DXMATRIX         m_SavedProjection;
    bool               m_bHasSavedTransforms;

    bool                       m_bCameraApplied;
    CMatrix                    m_SavedCameraMatrix;
    CVector                    m_SavedCamFront;
    CVector                    m_SavedCamUp;
    CVector                    m_SavedCamSource;
    float                      m_fSavedCameraFOV;
    std::vector<unsigned char> m_SavedNativeCameraState;
};
