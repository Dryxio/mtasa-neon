/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/shared_logic/CClientCubemapRenderTarget.h
 *  PURPOSE:     Independently rendered 6-face cubemap render target
 *
 *****************************************************************************/

// Face indices match D3DCUBEMAP_FACES (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z), each rendered from the same
// fixed probe position - unlike a SceneView, a cubemap has no per-face orientation to configure; the 6
// fixed axis-aligned directions are what makes it a cubemap. Purely request-driven (no update-mode
// scheduling like SceneView): scripts choose which faces to request and when, which is the "partial
// per-frame face updates for budget control" the plan calls for - the scheduler does not second-guess it.
class CClientCubemapRenderTarget : public CClientTexture
{
    DECLARE_CLASS(CClientCubemapRenderTarget, CClientTexture)
public:
    CClientCubemapRenderTarget(CClientManager* pManager, ElementID ID, CCubemapRenderTargetItem* pCubemapItem)
        : ClassInit(this), CClientTexture(pManager, ID, pCubemapItem)
    {
        SetTypeName("dx-cubemaprendertarget");
    }

    eClientEntityType GetType() const { return CCLIENTCUBEMAPRENDERTARGET; }

    CCubemapRenderTargetItem* GetCubemapRenderTargetItem() { return (CCubemapRenderTargetItem*)m_pRenderItem; }

    void SetCamera(const CVector& position, float fNearClip, float fFarClip)
    {
        m_vecPosition = position;
        m_fNearClip = fNearClip;
        m_fFarClip = fFarClip;
        m_bPositionConfigured = true;
    }
    const CVector& GetCameraPosition() const { return m_vecPosition; }
    bool           IsPositionConfigured() const { return m_bPositionConfigured; }
    float          GetNearClip() const { return m_fNearClip; }
    float          GetFarClip() const { return m_fFarClip; }

    // Bits accumulate across multiple RequestFaces calls within the same frame (eg. one call per face from
    // a round-robin update loop). There is no per-frame render cap, but clearing is still split from
    // reading: ClearRenderedFaces only drops the bits that actually got rendered, so a face whose render
    // pass fails to begin (eg. device not ready) stays queued for the next frame instead of being dropped.
    void  RequestFaces(uint8 uiFaceMask) { m_uiRequestedFaceMask |= uiFaceMask; }
    uint8 GetRequestedFaces() const { return m_uiRequestedFaceMask; }
    void  ClearRenderedFaces(uint8 uiFaceMask) { m_uiRequestedFaceMask &= ~uiFaceMask; }

    void           SetLastRenderError(const SString& strError) { m_strLastRenderError = strError; }
    const SString& GetLastRenderError() const { return m_strLastRenderError; }
    bool           DidLastRenderSucceed() const { return m_bLastRenderSucceeded; }
    uint           GetRenderCount() const { return m_uiRenderCount; }
    void           OnRenderCompleted(bool bSucceeded)
    {
        m_bLastRenderSucceeded = bSucceeded;
        if (bSucceeded)
            ++m_uiRenderCount;
    }

private:
    CVector m_vecPosition;
    bool    m_bPositionConfigured = false;
    float   m_fNearClip = 0.3f;
    float   m_fFarClip = 500.0f;
    uint8   m_uiRequestedFaceMask = 0;
    bool    m_bLastRenderSucceeded = false;
    uint    m_uiRenderCount = 0;
    SString m_strLastRenderError;
};
