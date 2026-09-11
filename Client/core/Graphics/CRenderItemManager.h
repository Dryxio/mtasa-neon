/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        CRenderItemManager.h
 *  PURPOSE:
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

class CRenderStateScope;

//
// CRenderItemManager
//
// Make/delete/fiddle with render items
//
class CRenderItemManager : public CRenderItemManagerInterface
{
public:
    ZERO_ON_NEW
    CRenderItemManager();
    ~CRenderItemManager();

    // CRenderItemManagerInterface
    virtual void                      DoPulse();
    virtual CDxFontItem*              CreateDxFont(const SString& strFullFilePath, uint uiSize, bool bBold, DWORD ulQuality = DEFAULT_QUALITY);
    virtual CGuiFontItem*             CreateGuiFont(const SString& strFullFilePath, const SString& strFontName, uint uiSize);
    virtual CTextureItem*             CreateTexture(const SString& strFullFilePath, const CPixels* pPixels, bool bMipMaps = true, uint uiSizeX = RDEFAULT,
                                                    uint uiSizeY = RDEFAULT, ERenderFormat format = RFORMAT_UNKNOWN, ETextureAddress textureAddress = TADDRESS_WRAP,
                                                    ETextureType textureType = TTYPE_TEXTURE, uint uiVolumeDepth = 1);
    virtual CShaderItem*              CreateShader(const SString& strFile, const SString& strRootPath, bool bIsRawData, SString& strOutStatus, float fPriority,
                                                   float fMaxDistance, bool bLayered, bool bDebug, int iTypeMask, const EffectMacroList& macros);
    virtual CRenderTargetItem*        CreateRenderTarget(uint uiSizeX, uint uiSizeY, bool bHasSurfaceFormat, bool bWithAlphaChannel, int surfaceFormat,
                                                         bool bForce = false);
    virtual CDepthStencilTargetItem*  CreateDepthStencilTarget(uint uiSizeX, uint uiSizeY, int surfaceFormat, bool bSampleable);
    virtual CCubemapRenderTargetItem* CreateCubemapRenderTarget(uint uiEdgeSize, int surfaceFormat);
    virtual CMrtSetItem*              CreateMrtSet(CRenderTargetItem* const targets[MAX_MRT_RENDER_TARGETS], uint uiNumTargets,
                                                   CDepthStencilTargetItem* pDepthStencilTargetItem);
    virtual CScreenSourceItem*        CreateScreenSource(uint uiSizeX, uint uiSizeY);
    virtual bool BeginRenderPass(CRenderTargetItem* const targets[MAX_MRT_RENDER_TARGETS], uint uiNumTargets, CDepthStencilTargetItem* pDepthStencilTargetItem,
                                 bool bClear);
    virtual bool BeginSceneViewRender(CRenderTargetItem* pTarget, CDepthStencilTargetItem* pDepthStencilTargetItem, const CMatrix& cameraMatrix, float fFOV,
                                      bool bClear);
    virtual bool BeginCubemapFaceRender(CCubemapRenderTargetItem* pCubemap, uint uiFace, const CMatrix& cameraMatrix, bool bClear);
    virtual bool EndRenderPass();
    virtual uint GetRenderPassDepth() { return (uint)m_RenderPassStack.size(); }
    virtual void ForceCloseAllRenderPasses();
    virtual CVectorGraphicItem* CreateVectorGraphic(uint width, uint height);
    virtual CWebBrowserItem*    CreateWebBrowser(uint uiSizeX, uint uiSizeY);
    virtual bool                SetRenderTarget(CRenderTargetItem* pItem, bool bClear);
    virtual void                EnableSetRenderTargetOldVer(bool bEnable);
    virtual bool                IsSetRenderTargetEnabledOldVer();
    virtual bool                RestoreDefaultRenderTarget();
    virtual void                UpdateBackBufferCopy();
    virtual void                UpdateScreenSource(CScreenSourceItem* pScreenSourceItem, bool bResampleNow);
    virtual SShaderItemLayers*  GetAppliedShaderForD3DData(CD3DDUMMY* pD3DData);
    virtual void                SetSceneViewShaderContext(const std::vector<SSceneViewShaderAssignment>* pAssignments);
    virtual bool ApplySceneViewOutputShader(CRenderTargetItem* pSource, CRenderTargetItem* pDestination, CShaderItem* pShader, const SString& strInputName);
    virtual bool IsSceneViewOutputShaderValid(CShaderItem* pShader, const SString& strInputName);
    virtual const SString& GetLastSceneViewOutputError() const { return m_strLastSceneViewOutputError; }
    virtual bool           ApplyShaderItemToWorldTexture(CShaderItem* pShaderItem, const SString& strTextureNameMatch, CClientEntityBase* pClientEntity,
                                                         bool bAppendLayers);
    virtual bool           RemoveShaderItemFromWorldTexture(CShaderItem* pShaderItem, const SString& strTextureNameMatch, CClientEntityBase* pClientEntity);
    virtual void           RemoveClientEntityRefs(CClientEntityBase* pClientEntity);
    virtual void           GetVisibleTextureNames(std::vector<SString>& outNameList, const SString& strTextureNameMatch, ushort usModelID);
    virtual eDxTestMode    GetTestMode() { return m_TestMode; }
    virtual void           SetTestMode(eDxTestMode testMode);
    virtual void           GetDxStatus(SDxStatus& outStatus);
    virtual void           GetDxCapabilities(SDxCapabilities& outCapabilities);
    virtual void           GetShaderDiagnostics(CShaderItem* pShaderItem, SShaderDiagnostics& outDiagnostics);
    virtual void           GetRenderStatistics(SRenderStatistics& outStatistics);
    virtual CEffectCloner* GetEffectCloner() { return m_pEffectCloner; }
    virtual void           PreDrawWorld();
    virtual void           SetDepthBufferFormat(ERenderFormat depthBufferFormat) { m_depthBufferFormat = depthBufferFormat; }
    virtual ERenderFormat  GetDepthBufferFormat() { return m_depthBufferFormat; }
    virtual void           SaveReadableDepthBuffer();
    virtual void           FlushNonAARenderTarget();
    virtual HRESULT        HandleStretchRect(IDirect3DSurface9* pSourceSurface, CONST RECT* pSourceRect, IDirect3DSurface9* pDestSurface, CONST RECT* pDestRect,
                                             int Filter);

    // CRenderItemManager
    void NotifyContructRenderItem(CRenderItem* pItem);
    void NotifyDestructRenderItem(CRenderItem* pItem);
    void OnDeviceCreate(IDirect3DDevice9* pDevice, float fViewportSizeX, float fViewportSizeY);
    void OnLostDevice();
    void OnResetDevice();
    void OnViewportSizeChanged(uint uiNewViewportSizeX, uint uiNewViewportSizeY);
    void UpdateBackBufferCopySize();
    bool SaveDefaultRenderTarget();
    bool IsUsingDefaultRenderTarget();
    bool ChangeRenderTarget(uint uiSizeX, uint uiSizeY, IDirect3DSurface9* pD3DRenderTarget, IDirect3DSurface9* pD3DZStencilSurface);
    void RemoveShaderItemFromWatchLists(CShaderItem* pShaderItem);
    void UpdateMemoryUsage();
    bool CanCreateRenderItem(ClassId classId);
    void NotifyShaderItemUsesDepthBuffer(CShaderItem* pShaderItem, bool bUsesDepthBuffer);
    void NotifyShaderItemUsesMultipleRenderTargets(CShaderItem* pShaderItem, bool bUsesMultipleRenderTargets);

    HRESULT GetDeviceCooperativeLevel(const char* szContext, bool bLogLost = true) const;

    void RetryInvalidRenderTargets();

    static int GetBitsPerPixel(D3DFORMAT Format);
    static int GetPitchDivisor(D3DFORMAT Format);
    static int CalcD3DResourceMemoryKBUsage(IDirect3DResource9* pD3DResource);
    static int CalcD3DSurfaceMemoryKBUsage(IDirect3DSurface9* pD3DSurface);
    static int CalcD3DTextureMemoryKBUsage(IDirect3DTexture9* pD3DTexture);
    static int CalcD3DVolumeTextureMemoryKBUsage(IDirect3DVolumeTexture9* pD3DVolumeTexture);
    static int CalcD3DCubeTextureMemoryKBUsage(IDirect3DCubeTexture9* pD3DCubeTexture);

    IDirect3DDevice9* m_pDevice;

protected:
    void TryRecreateInvalidRenderTargets();

    std::set<CRenderItem*>                         m_CreatedItemList;
    IDirect3DSurface9*                             m_pDefaultD3DRenderTarget;
    IDirect3DSurface9*                             m_pDefaultD3DZStencilSurface;
    uint                                           m_uiDefaultViewportSizeX;
    uint                                           m_uiDefaultViewportSizeY;
    CRenderTargetItem*                             m_pBackBufferCopy;
    bool                                           m_bBackBufferCopyMaybeNeedsResize;
    uint                                           m_uiBackBufferCopyRevision;
    CFastHashSet<CD3DDUMMY*>                       m_FrameTextureUsage;
    CFastHashSet<CD3DDUMMY*>                       m_PrevFrameTextureUsage;
    class CRenderWare*                             m_pRenderWare;
    CEffectCloner*                                 m_pEffectCloner;
    eDxTestMode                                    m_TestMode;
    SString                                        m_strVideoCardName;
    int                                            m_iVideoCardMemoryKBTotal;
    int                                            m_iVideoCardMemoryKBForMTATotal;
    SString                                        m_strVideoCardPSVersion;
    int                                            m_iTextureMemoryKBUsed;
    int                                            m_iRenderTargetMemoryKBUsed;
    int                                            m_iFontMemoryKBUsed;
    int                                            m_iMemoryKBFreeForMTA;
    bool                                           m_bSetRenderTargetEnabledOldVer;
    bool                                           m_bUsingReadableDepthBuffer;
    ERenderFormat                                  m_depthBufferFormat;
    std::set<CShaderItem*>                         m_ShadersUsingDepthBuffer;
    std::set<CShaderItem*>                         m_ShadersUsingMultipleRenderTargets;
    const std::vector<SSceneViewShaderAssignment>* m_pSceneViewShaderContext;
    SShaderItemLayers*                             m_pSceneViewShaderLayers;
    IDirect3DSurface9*                             m_pSavedSceneDepthSurface;
    IDirect3DSurface9*                             m_pSavedSceneRenderTargetAA;
    IDirect3DSurface9*                             m_pNonAADepthSurface2;
    IDirect3DSurface9*                             m_pNonAARenderTarget;
    IDirect3DTexture9*                             m_pNonAARenderTargetTexture;
    bool                                           m_bIsSwiftShader;
    uint                                           m_uiLastRenderTargetRetryTime;
    uint                                           m_uiRenderTargetRetryDelayMs;
    uint                                           m_uiRenderTargetRetryAttempts;
    uint                                           m_uiRenderTargetRetryCooldownUntil;

    // Stack of open dxBeginRenderPass/dxEndRenderPass scopes (innermost/most-recent last).
    // Force-closed defensively in OnPresent (in case a script forgets dxEndRenderPass) and in
    // OnLostDevice (the D3D surfaces a scope would try to restore are about to be released).
    std::vector<CRenderStateScope*> m_RenderPassStack;
    uint                            m_uiRenderPassesStarted;
    uint                            m_uiRenderPassFailures;
    uint                            m_uiForcedRenderPassClosures;
    SString                         m_strLastSceneViewOutputError;
};
