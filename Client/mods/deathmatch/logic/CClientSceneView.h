/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/shared_logic/CClientSceneView.h
 *  PURPOSE:     Independently rendered GTA world view
 *
 *****************************************************************************/

// A scene view is itself a drawable render-target texture. Its private depth
// surface is never exposed as a texture, preventing scripts from claiming
// sampleable depth when the device only supplied a regular depth surface.
enum class ESceneViewUpdateMode
{
    MANUAL,
    ONCE,
    ALWAYS,
    EVERY_N_FRAMES,
    INTERVAL,
};

class CClientSceneView : public CClientRenderTarget
{
    DECLARE_CLASS(CClientSceneView, CClientRenderTarget)
public:
    CClientSceneView(CClientManager* pManager, ElementID ID, CRenderTargetItem* pRenderTargetItem, CDepthStencilTargetItem* pDepthStencilTargetItem)
        : ClassInit(this),
          CClientRenderTarget(pManager, ID, pRenderTargetItem),
          m_pDepthStencilTargetItem(pDepthStencilTargetItem),
          m_fFOV(70.0f),
          m_bRenderRequested(false),
          m_bLastRenderSucceeded(false)
    {
        SetTypeName("dx-sceneview");
    }

    ~CClientSceneView()
    {
        for (SSceneViewShaderAssignment& assignment : m_ShaderAssignments)
            SAFE_RELEASE(assignment.pShaderItem);
        SAFE_RELEASE(m_pOutputShaderItem);
        SAFE_RELEASE(m_pPostProcessTargetItem);
        SAFE_RELEASE(m_pDepthStencilTargetItem);
    }

    eClientEntityType GetType() const { return CCLIENTSCENEVIEW; }

    void SetCamera(const CMatrix& matrix, float fFOV)
    {
        m_CameraMatrix = matrix;
        m_fFOV = fFOV;
        m_bCameraConfigured = true;
    }

    // Explicit projection mode, independent of camera position/orientation above. Perspective (using
    // m_fFOV) remains the default - orthographic is strictly opt-in and never assumed. Validation
    // (finite, positive width/height/near/far, far > near) is the Lua binding's responsibility, matching
    // every other SceneView setter in this class.
    void SetOrthographicProjection(float fWidth, float fHeight, float fNearClip, float fFarClip)
    {
        m_bOrthographic = true;
        m_fOrthoWidth = fWidth;
        m_fOrthoHeight = fHeight;
        m_fOrthoNearClip = fNearClip;
        m_fOrthoFarClip = fFarClip;
    }
    void  SetPerspectiveProjection() { m_bOrthographic = false; }
    bool  IsOrthographic() const { return m_bOrthographic; }
    float GetOrthographicWidth() const { return m_fOrthoWidth; }
    float GetOrthographicHeight() const { return m_fOrthoHeight; }
    float GetOrthographicNearClip() const { return m_fOrthoNearClip; }
    float GetOrthographicFarClip() const { return m_fOrthoFarClip; }

    void RequestRender() { m_bRenderRequested = true; }
    bool IsRenderRequested() const { return m_bRenderRequested; }
    bool IsCameraConfigured() const { return m_bCameraConfigured; }
    bool ConsumeRenderRequest()
    {
        if (!m_bRenderRequested)
            return false;
        m_bRenderRequested = false;
        return true;
    }

    void SetUpdateMode(ESceneViewUpdateMode mode, uint uiValue)
    {
        m_UpdateMode = mode;
        m_uiUpdateValue = uiValue;
        if (mode == ESceneViewUpdateMode::ONCE)
            m_bRenderRequested = true;
    }

    bool ShouldRender(uint uiFrame, uint uiTickCount)
    {
        if (!m_bCameraConfigured)
            return false;

        // Explicit requests always take precedence over throttling, preserving dxRequestSceneViewRender's
        // legacy Stage-1 behavior even when a periodic mode is configured.
        if (ConsumeRenderRequest())
            return true;

        switch (m_UpdateMode)
        {
            case ESceneViewUpdateMode::ALWAYS:
                return true;
            case ESceneViewUpdateMode::EVERY_N_FRAMES:
                return m_uiRenderCount == 0 || uiFrame - m_uiLastRenderFrame >= m_uiUpdateValue;
            case ESceneViewUpdateMode::INTERVAL:
                return m_uiRenderCount == 0 || uiTickCount - m_uiLastRenderTick >= m_uiUpdateValue;
            default:
                return false;
        }
    }

    void OnRenderCompleted(bool bSucceeded, uint uiFrame, uint uiTickCount)
    {
        m_bLastRenderSucceeded = bSucceeded;
        m_uiLastRenderFrame = uiFrame;
        m_uiLastRenderTick = uiTickCount;
        ++m_uiRenderCount;
        if (m_UpdateMode == ESceneViewUpdateMode::ONCE)
            m_UpdateMode = ESceneViewUpdateMode::MANUAL;
    }

    const CMatrix&           GetCameraMatrix() const { return m_CameraMatrix; }
    float                    GetFOV() const { return m_fFOV; }
    CDepthStencilTargetItem* GetDepthStencilTargetItem() const { return m_pDepthStencilTargetItem; }
    void                     SetLastRenderSucceeded(bool bSucceeded) { m_bLastRenderSucceeded = bSucceeded; }
    bool                     DidLastRenderSucceed() const { return m_bLastRenderSucceeded; }
    ESceneViewUpdateMode     GetUpdateMode() const { return m_UpdateMode; }
    uint                     GetUpdateValue() const { return m_uiUpdateValue; }
    uint                     GetRenderCount() const { return m_uiRenderCount; }
    uint                     GetLastRenderFrame() const { return m_uiLastRenderFrame; }
    uint                     GetLastRenderTick() const { return m_uiLastRenderTick; }
    void                     SetLastRenderError(const SString& strError) { m_strLastRenderError = strError; }
    const SString&           GetLastRenderError() const { return m_strLastRenderError; }

    // pTargetEntity mirrors engineApplyShaderToWorldTexture's targetElement: nullptr applies the shader to
    // every matching texture in the scene view, a specific element restricts it to that element only. A
    // destroyed target entity is not purged from here (this struct only ever compares its pointer value,
    // never dereferences it), so a stale assignment simply stops matching anything rather than dangling.
    bool AddShaderAssignment(CShaderItem* pShaderItem, const SString& strTextureNameMatch, CClientEntityBase* pTargetEntity = nullptr,
                             bool bAppendLayers = true)
    {
        const SString strMatchLower = strTextureNameMatch.ToLower();
        for (SSceneViewShaderAssignment& assignment : m_ShaderAssignments)
        {
            if (assignment.pShaderItem == pShaderItem && assignment.strTextureNameMatch == strMatchLower && assignment.pTargetEntity == pTargetEntity)
            {
                assignment.bAppendLayers = bAppendLayers;
                return true;
            }
        }

        pShaderItem->AddRef();
        m_ShaderAssignments.push_back({pShaderItem, strMatchLower, pTargetEntity, bAppendLayers});
        return true;
    }

    bool RemoveShaderAssignment(CShaderItem* pShaderItem, const SString& strTextureNameMatch, CClientEntityBase* pTargetEntity = nullptr)
    {
        const SString strMatchLower = strTextureNameMatch.ToLower();
        for (auto iter = m_ShaderAssignments.begin(); iter != m_ShaderAssignments.end(); ++iter)
        {
            if (iter->pShaderItem == pShaderItem && iter->strTextureNameMatch == strMatchLower && iter->pTargetEntity == pTargetEntity)
            {
                SAFE_RELEASE(iter->pShaderItem);
                m_ShaderAssignments.erase(iter);
                return true;
            }
        }
        return false;
    }

    const std::vector<SSceneViewShaderAssignment>& GetShaderAssignments() const { return m_ShaderAssignments; }

    void SetOutputShader(CShaderItem* pShaderItem, CRenderTargetItem* pPostProcessTargetItem, const SString& strInputName)
    {
        SAFE_RELEASE(m_pOutputShaderItem);
        SAFE_RELEASE(m_pPostProcessTargetItem);
        m_pOutputShaderItem = pShaderItem;
        m_pPostProcessTargetItem = pPostProcessTargetItem;
        m_strOutputShaderInputName = strInputName;
        if (m_pOutputShaderItem)
            m_pOutputShaderItem->AddRef();
    }

    void ClearOutputShader()
    {
        SAFE_RELEASE(m_pOutputShaderItem);
        SAFE_RELEASE(m_pPostProcessTargetItem);
        m_strOutputShaderInputName.clear();
    }

    CShaderItem*       GetOutputShaderItem() const { return m_pOutputShaderItem; }
    CRenderTargetItem* GetPostProcessTargetItem() const { return m_pPostProcessTargetItem; }
    const SString&     GetOutputShaderInputName() const { return m_strOutputShaderInputName; }

private:
    CDepthStencilTargetItem*                m_pDepthStencilTargetItem;
    CMatrix                                 m_CameraMatrix;
    float                                   m_fFOV;
    bool                                    m_bRenderRequested;
    bool                                    m_bLastRenderSucceeded;
    bool                                    m_bCameraConfigured = false;
    bool                                    m_bOrthographic = false;
    float                                   m_fOrthoWidth = 0.0f;
    float                                   m_fOrthoHeight = 0.0f;
    float                                   m_fOrthoNearClip = 0.0f;
    float                                   m_fOrthoFarClip = 0.0f;
    ESceneViewUpdateMode                    m_UpdateMode = ESceneViewUpdateMode::MANUAL;
    uint                                    m_uiUpdateValue = 0;
    uint                                    m_uiRenderCount = 0;
    uint                                    m_uiLastRenderFrame = 0;
    uint                                    m_uiLastRenderTick = 0;
    std::vector<SSceneViewShaderAssignment> m_ShaderAssignments;
    CShaderItem*                            m_pOutputShaderItem = nullptr;
    CRenderTargetItem*                      m_pPostProcessTargetItem = nullptr;
    SString                                 m_strOutputShaderInputName;
    SString                                 m_strLastRenderError;
};
