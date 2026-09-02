/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *               (Shared logic for modifications)
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        core/CWebView.cpp
 *  PURPOSE:     Web view class
 *
 *****************************************************************************/
#include "StdInc.h"
#include "CWebView.h"
#include "CAjaxResourceHandler.h"
#include <cef3/cef/include/cef_parser.h>
#include <cef3/cef/include/cef_request_context.h>
#include <cef3/cef/include/cef_task.h>
#include "CWebDevTools.h"
#include <chrono>
#include "CWebViewAuth.h"  // AUTH: IPC validation helpers
#include <utility>
#include <algorithm>
#include <array>
#include <d3d11_1.h>

#pragma comment(lib, "d3d11.lib")

namespace
{
    const int CEF_PIXEL_STRIDE = 4;

    uint64_t GetSteadyClockNanoseconds()
    {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    void UpdateAtomicMaximum(std::atomic<uint64_t>& maximum, uint64_t value)
    {
        uint64_t previous = maximum.load(std::memory_order_relaxed);
        while (previous < value && !maximum.compare_exchange_weak(previous, value, std::memory_order_relaxed))
        {
        }
    }
}

struct CWebView::FAcceleratedPaintBackend
{
    static constexpr size_t BUFFER_COUNT = 3;

    struct FReadbackSlot
    {
        ID3D11Texture2D*     stagingTexture = nullptr;
        ID3D11Query*         completionQuery = nullptr;
        std::vector<CefRect> dirtyRects;
        CefRect              visibleRect;
        cef_color_type_t     format = CEF_COLOR_TYPE_BGRA_8888;
        uint64_t             sequence = 0;
        bool                 pending = false;
    };

    struct FReadbackPipeline
    {
        std::array<FReadbackSlot, BUFFER_COUNT> slots;
        D3D11_TEXTURE2D_DESC                    description{};
        uint64_t                                nextSequence = 1;
        bool                                    initialized = false;
    };

    ~FAcceleratedPaintBackend()
    {
        ReleasePipeline(viewPipeline);
        ReleasePipeline(popupPipeline);
        if (deviceContext)
            deviceContext->Release();
        if (device)
            device->Release();
    }

    bool EnsureDevice()
    {
        if (device && deviceContext)
            return true;
        if (initializationAttempted)
            return false;

        initializationAttempted = true;

        ID3D11Device*        baseDevice = nullptr;
        D3D_FEATURE_LEVEL    featureLevel{};
        ID3D11DeviceContext* newContext = nullptr;
        const HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                                                 &baseDevice, &featureLevel, &newContext);
        if (FAILED(result))
        {
            lastFailure = result;
            return false;
        }

        const HRESULT queryResult = baseDevice->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void**>(&device));
        baseDevice->Release();
        if (FAILED(queryResult))
        {
            newContext->Release();
            lastFailure = queryResult;
            return false;
        }

        deviceContext = newContext;
        return true;
    }

    static void ReleasePipeline(FReadbackPipeline& pipeline)
    {
        for (auto& slot : pipeline.slots)
        {
            if (slot.completionQuery)
                slot.completionQuery->Release();
            if (slot.stagingTexture)
                slot.stagingTexture->Release();
            slot = {};
        }
        pipeline = {};
    }

    bool EnsurePipeline(FReadbackPipeline& pipeline, const D3D11_TEXTURE2D_DESC& sourceDescription)
    {
        if (pipeline.initialized && pipeline.description.Width == sourceDescription.Width && pipeline.description.Height == sourceDescription.Height &&
            pipeline.description.Format == sourceDescription.Format)
        {
            return true;
        }

        ReleasePipeline(pipeline);
        pipeline.description = sourceDescription;
        pipeline.description.Usage = D3D11_USAGE_STAGING;
        pipeline.description.BindFlags = 0;
        pipeline.description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        pipeline.description.MiscFlags = 0;

        D3D11_QUERY_DESC queryDescription{};
        queryDescription.Query = D3D11_QUERY_EVENT;
        for (auto& slot : pipeline.slots)
        {
            lastFailure = device->CreateTexture2D(&pipeline.description, nullptr, &slot.stagingTexture);
            if (FAILED(lastFailure))
            {
                ReleasePipeline(pipeline);
                return false;
            }

            lastFailure = device->CreateQuery(&queryDescription, &slot.completionQuery);
            if (FAILED(lastFailure))
            {
                ReleasePipeline(pipeline);
                return false;
            }
        }

        pipeline.initialized = true;
        return true;
    }

    FReadbackSlot* AcquireSubmissionSlot(FReadbackPipeline& pipeline, bool& droppedCompletedFrame)
    {
        for (auto& slot : pipeline.slots)
        {
            if (!slot.pending)
                return &slot;
        }

        FReadbackSlot* oldestCompleted = nullptr;
        for (auto& slot : pipeline.slots)
        {
            if (deviceContext->GetData(slot.completionQuery, nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
                (!oldestCompleted || slot.sequence < oldestCompleted->sequence))
            {
                oldestCompleted = &slot;
            }
        }

        if (oldestCompleted)
        {
            oldestCompleted->pending = false;
            droppedCompletedFrame = true;
        }
        return oldestCompleted;
    }

    FReadbackSlot* FindNewestCompleted(FReadbackPipeline& pipeline)
    {
        FReadbackSlot* newestCompleted = nullptr;
        for (auto& slot : pipeline.slots)
        {
            if (slot.pending && deviceContext->GetData(slot.completionQuery, nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
                (!newestCompleted || slot.sequence > newestCompleted->sequence))
            {
                newestCompleted = &slot;
            }
        }

        if (!newestCompleted)
            return nullptr;

        // A complete texture contains the whole current browser frame. Older
        // completed submissions can be discarded without breaking dirty-rect
        // reconstruction, keeping latency bounded when the game misses a beat.
        for (auto& slot : pipeline.slots)
        {
            if (slot.pending && slot.sequence < newestCompleted->sequence)
                slot.pending = false;
        }
        return newestCompleted;
    }

    bool HasPendingFrames() const
    {
        const auto PipelineHasPending = [](const FReadbackPipeline& pipeline)
        { return std::ranges::any_of(pipeline.slots, [](const FReadbackSlot& slot) { return slot.pending; }); };
        return PipelineHasPending(viewPipeline) || PipelineHasPending(popupPipeline);
    }

    std::mutex           mutex;
    ID3D11Device1*       device = nullptr;
    ID3D11DeviceContext* deviceContext = nullptr;
    FReadbackPipeline    viewPipeline;
    FReadbackPipeline    popupPipeline;
    std::vector<byte>    tightPixels;
    bool                 initializationAttempted = false;
    bool                 failureLogged = false;
    HRESULT              lastFailure = S_OK;
};

CWebView::CWebView(bool bIsLocal, CWebBrowserItem* pWebBrowserRenderItem, bool bTransparent, bool bFrameStatsEnabled)
{
    m_pEventTarget = std::make_shared<FEventTarget>();
    m_pAcceleratedPaintBackend = std::make_unique<FAcceleratedPaintBackend>();
    m_bIsLocal = bIsLocal;
    m_bIsTransparent = bTransparent;
    m_bFrameStatsEnabled = bFrameStatsEnabled;
    m_pWebBrowserRenderItem = pWebBrowserRenderItem;
    if (m_pWebBrowserRenderItem)
        m_pWebBrowserRenderItem->AddRef();

    m_pEventsInterface = nullptr;
    m_bBeingDestroyed = false;
    m_bIsRenderingPaused = false;
    m_fVolume = 1.0f;
    m_bHasInputFocus = false;
    m_vecMousePosition = {0, 0};
    m_vecPendingMousePosition = {0, 0};
    m_lastMouseMoveTime = std::chrono::steady_clock::now();
    memset(m_mouseButtonStates, 0, sizeof(m_mouseButtonStates));

    // Initialise properties
    m_Properties["mobile"] = "0";
}

CWebView::~CWebView()
{
    m_bBeingDestroyed = true;

    if (m_pEventTarget)
        m_pEventTarget->Clear(m_pEventsInterface);

    if (IsMainThread())
    {
        if (auto pWebCore = g_pCore->GetWebCore(); pWebCore)
        {
            if (pWebCore->GetFocusedWebView() == this)
                pWebCore->SetFocusedWebView(nullptr);
        }
    }

    if (m_pWebBrowserRenderItem)
    {
        m_pWebBrowserRenderItem->Release();
        m_pWebBrowserRenderItem = nullptr;
    }

    // Clean up AJAX handlers to prevent accumulation
    m_AjaxHandlers.clear();

    // Break circular reference: ensure browser reference is cleared
    // This is to prevent memory leaks from CWebView <-> CefBrowser cycles
    if (m_pWebView)
    {
        // Stop any loading immediately
        m_pWebView->StopLoad();

        // Navigate to blank page to force V8/DOM cleanup and release video/audio resources
        // We do this BEFORE hiding to ensure the navigation request is processed
        m_pWebView->GetMainFrame()->LoadURL("about:blank");

        // Notify that the browser is hidden and lost focus to release rendering resources
        m_pWebView->GetHost()->WasHidden(true);
        m_pWebView->GetHost()->SetFocus(false);

        // Force close the browser host to ensure the renderer process terminates immediately
        m_pWebView->GetHost()->CloseBrowser(true);
        m_pWebView = nullptr;
    }

    OutputDebugLine("CWebView::~CWebView");
}

void CWebView::SetWebBrowserEvents(CWebBrowserEventsInterface* pInterface)
{
    m_pEventsInterface = pInterface;

    if (m_pEventTarget)
        m_pEventTarget->Assign(pInterface);
}

void CWebView::ClearWebBrowserEvents(CWebBrowserEventsInterface* pInterface)
{
    if (m_pEventTarget)
        m_pEventTarget->Clear(pInterface);

    if (m_pEventsInterface == pInterface)
        m_pEventsInterface = nullptr;
}

void CWebView::QueueBrowserEvent(const char* name, std::function<void(CWebBrowserEventsInterface*)>&& fn)
{
    auto target = m_pEventTarget;
    if (!target)
        return;

    const auto token = target->CreateDispatchToken();

    g_pCore->GetWebCore()->AddEventToEventQueue(
        [target, token, fn = std::move(fn)]() mutable
        {
            if (!target)
                return;

            target->Dispatch(token, fn);
        },
        this, name);
}

void CWebView::Initialise()
{
    // Create the CEF browser eagerly so onClientBrowserCreated fires
    // even if loadBrowserURL hasn't been called yet.
    // Scripts rely on this event to know when the browser is ready.
    EnsureBrowserCreated();
}

bool CWebView::EnsureBrowserCreated()
{
    if (m_bBrowserCreated || m_bBeingDestroyed)
        return m_bBrowserCreated;

    // Initialise the web session (which holds the actual settings) in in-memory mode
    CefBrowserSettings browserSettings;
    const auto         pWebCore = static_cast<CWebCore*>(g_pCore->GetWebCore());
    browserSettings.windowless_frame_rate = pWebCore ? pWebCore->GetWindowlessFrameRate() : 60;
    browserSettings.javascript_access_clipboard = cef_state_t::STATE_DISABLED;
    browserSettings.javascript_dom_paste = cef_state_t::STATE_DISABLED;
    browserSettings.webgl = cef_state_t::STATE_ENABLED;

    if (!m_bIsLocal)
    {
        const auto pWebCore = g_pCore->GetWebCore();
        const bool bEnabledJavascript = pWebCore ? pWebCore->GetRemoteJavascriptEnabled() : false;
        browserSettings.javascript = bEnabledJavascript ? cef_state_t::STATE_ENABLED : cef_state_t::STATE_DISABLED;
    }

    // Set background color to opaque white if transparency is disabled
    if (!m_bIsTransparent)
        browserSettings.background_color = 0xffffffff;

    CefWindowInfo windowInfo;
    windowInfo.SetAsWindowless(g_pCore->GetHookedWindow());

    // Automatic CEF scheduling is the default because OnPaint is asynchronous. The external path remains available as a measured fallback and is driven
    // only after MTA has consumed the previous completed frame.
    windowInfo.external_begin_frame_enabled = pWebCore && pWebCore->IsExternalFrameSchedulingEnabled();
    // CEF's accelerated OSR callback avoids the synchronous GPU-to-CPU
    // readback performed before software OnPaint. It is opt-in because the
    // shared handle must still be bridged to MTA's D3D9 renderer and some
    // virtualized adapters may not support that interop reliably.
    windowInfo.shared_texture_enabled = pWebCore && pWebCore->IsSharedTextureEnabled();

    CefRefPtr<CefRequestContext> requestContext;
    if (const auto it = m_Properties.find("isolated_request_context"); it != m_Properties.end() && it->second == "1")
    {
        // Local resource pages all use the http://mta origin. Give core-owned
        // UI a distinct browser context so a busy or wedged resource renderer
        // cannot share and block the Neon menu's renderer process.
        CefRequestContextSettings contextSettings;
        requestContext = CefRequestContext::CreateContext(contextSettings, nullptr);
        if (!pWebCore || !pWebCore->RegisterMtaSchemeHandlerFactory(requestContext))
        {
            // Preserve a working menu if CEF cannot configure the isolated context. The global context already owns the http://mta handler.
            requestContext = nullptr;
        }
    }

    CefBrowserHost::CreateBrowser(windowInfo, this, "", browserSettings, nullptr, requestContext);
    m_bBrowserCreated = true;
    return true;
}

void CWebView::CloseBrowser()
{
    // CefBrowserHost::CloseBrowser calls the destructor after the browser has been destroyed
    m_bBeingDestroyed = true;

    // Clear AJAX handlers early to prevent late event processing
    m_AjaxHandlers.clear();

    if (m_pWebView)
    {
        // Stop any loading immediately
        m_pWebView->StopLoad();

        // Navigate to blank page to force V8/DOM cleanup and release video/audio resources
        // We do this BEFORE hiding to ensure the navigation request is processed
        m_pWebView->GetMainFrame()->LoadURL("about:blank");

        // Notify that the browser is hidden and lost focus to release rendering resources
        m_pWebView->GetHost()->WasHidden(true);
        m_pWebView->GetHost()->SetFocus(false);

        m_pWebView->GetHost()->CloseBrowser(true);
        m_pWebView = nullptr;
    }
}

bool CWebView::LoadURL(const SString& strURL, bool bFilterEnabled, const SString& strPostData, bool bURLEncoded)
{
    // Lazy creation: create browser on first use
    EnsureBrowserCreated();

    // If browser isn't ready yet (async creation), store the URL to load when ready
    if (!m_pWebView)
    {
        m_strPendingURL = strURL;
        m_bPendingURLFilterEnabled = bFilterEnabled;
        m_strPendingPostData = strPostData;
        m_bPendingURLEncoded = bURLEncoded;
        return true;  // Return true - we'll load it when browser is ready
    }

    CefURLParts urlParts;
    if (strURL.empty() || !CefParseURL(strURL, urlParts))
        return false;  // Invalid URL

    // Are we allowed to browse this website?
    if (bFilterEnabled)
    {
        auto pWebCore = g_pCore->GetWebCore();
        if (pWebCore && pWebCore->GetDomainState(UTF16ToMbUTF8(urlParts.host.str), true) != eURLState::WEBPAGE_ALLOWED)
            return false;
    }

    // Load it!
    auto pFrame = m_pWebView->GetMainFrame();
    if (strPostData.empty())
    {
        pFrame->LoadURL(strURL);
    }
    else
    {
        // Load URL first, see https://bitbucket.org/chromiumembedded/cef/issue/579
        pFrame->LoadURL("about:blank");

        // Perform HTTP POST
        auto request = CefRequest::Create();
        auto postData = CefPostData::Create();
        auto postDataElement = CefPostDataElement::Create();
        postDataElement->SetToBytes(strPostData.size(), strPostData.c_str());
        postData->AddElement(postDataElement);

        if (bURLEncoded)
        {
            CefRequest::HeaderMap headerMap;
            headerMap.insert(std::make_pair("Content-Type", "application/x-www-form-urlencoded"));
            headerMap.insert(std::make_pair("Content-Length", std::to_string(strPostData.size())));
            // headerMap.insert ( std::make_pair ( "Connection", "close" ) );
            request->SetHeaderMap(headerMap);
        }

        request->SetURL(strURL);
        request->SetMethod("POST");
        request->SetPostData(postData);
        pFrame->LoadRequest(request);
    }

    return true;
}

bool CWebView::IsLoading()
{
    if (!m_pWebView)
        return false;

    return m_pWebView->IsLoading();
}

SString CWebView::GetURL()
{
    if (!m_pWebView)
        return "";

    return UTF16ToMbUTF8(m_pWebView->GetMainFrame()->GetURL());
}

const SString& CWebView::GetTitle()
{
    return m_CurrentTitle;
}

void CWebView::SetRenderingPaused(bool bPaused)
{
    ApplyRenderingPaused(bPaused, false);
}

void CWebView::SetRenderingPausedPreservingLastFrame(bool bPaused)
{
    ApplyRenderingPaused(bPaused, true);
}

void CWebView::ApplyRenderingPaused(bool bPaused, bool preserveLastFrame)
{
    // Store pause state even when the host is not created yet so async
    // browser creation cannot lose the requested visibility state.
    const bool wasPaused = m_bIsRenderingPaused;
    m_bIsRenderingPaused = bPaused;

    if (bPaused)
        m_iInteractionRefreshFrames.store(0, std::memory_order_relaxed);

    if (m_pWebView)
    {
        CefRefPtr<CefBrowserHost> host = m_pWebView->GetHost();
        host->WasHidden(bPaused);

        if (wasPaused && !bPaused)
        {
            // CEF stops layout and OnPaint delivery while an OSR browser is
            // hidden. On some busy clients WasHidden(false) alone leaves the
            // compositor dormant even though input and JavaScript resume.
            // A resize notification guarantees an asynchronous paint, while
            // the short invalidation tail covers the context update queued by
            // the menu immediately after wake-up. This work only runs on the
            // hidden -> visible transition.
            host->WasResized();
            host->Invalidate(PET_VIEW);
            ArmInteractionRefreshFrames(8);
        }

        if (bPaused && !preserveLastFrame)
        {
            // CEF owns paintFrame while copying; take both locks so a late
            // OnPaint cannot publish a buffer while the hidden view is freed.
            std::scoped_lock lock{m_RenderData.paintMutex, m_RenderData.dataMutex};
            m_RenderData.pendingChanged = false;
            m_RenderData.forceFullUpload = false;
            m_RenderData.popupShown = false;
            m_RenderData.popupChanged = false;
            m_RenderData.paintFrame = {};
            m_RenderData.pendingFrame = {};
            m_RenderData.uploadFrame = {};
            m_RenderData.latestPaintGeneration = 0;
            m_RenderData.dirtyHistoryWidth = 0;
            m_RenderData.dirtyHistoryHeight = 0;
            m_RenderData.dirtyHistory.clear();
            m_RenderData.popupBuffer.reset();
        }
    }
}

const bool CWebView::GetRenderingPaused() const
{
    return m_pWebView ? m_bIsRenderingPaused : false;
}

void CWebView::Focus(bool state)
{
    if (m_pWebView)
        m_pWebView->GetHost()->SetFocus(state);

    auto pWebCore = g_pCore->GetWebCore();
    if (!pWebCore)
        return;

    if (state)
        pWebCore->SetFocusedWebView(this);
    else if (pWebCore->GetFocusedWebView() == this)
        pWebCore->SetFocusedWebView(nullptr);
}

void CWebView::ClearTexture()
{
    if (!m_pWebBrowserRenderItem) [[unlikely]]
        return;

    auto* const pD3DSurface = m_pWebBrowserRenderItem->m_pD3DRenderTargetSurface;
    if (!pD3DSurface) [[unlikely]]
        return;

    D3DSURFACE_DESC SurfaceDesc;
    if (FAILED(pD3DSurface->GetDesc(&SurfaceDesc))) [[unlikely]]
        return;

    D3DLOCKED_RECT LockedRect;
    if (SUCCEEDED(pD3DSurface->LockRect(&LockedRect, nullptr, D3DLOCK_DISCARD)))
    {
        // Check for integer overflow in size calculation: height * pitch must fit in size_t
        // Ensure both are positive and that multiplication won't overflow
        if (SurfaceDesc.Height > 0 && LockedRect.Pitch > 0 && static_cast<size_t>(SurfaceDesc.Height) <= SIZE_MAX / static_cast<size_t>(LockedRect.Pitch))
            [[likely]]
        {
            const auto memsetSize = static_cast<size_t>(SurfaceDesc.Height) * static_cast<size_t>(LockedRect.Pitch);
            std::memset(LockedRect.pBits, 0xFF, memsetSize);
        }
        pD3DSurface->UnlockRect();
    }
}

void CWebView::UpdateTexture()
{
    if (!m_pWebBrowserRenderItem) [[unlikely]]
        return;

    if (m_bBeingDestroyed) [[unlikely]]
        return;

    // Poll the non-blocking D3D11 readback ring before consuming the regular
    // mailbox. Software OnPaint views skip this with one atomic load.
    ConsumeAcceleratedPaint();

    auto* const pSurface = m_pWebBrowserRenderItem->m_pD3DRenderTargetSurface;

    if (!pSurface) [[unlikely]]
    {
        // Keep pending frame flags intact. Surface recreation can lag one or
        // more pulses; clearing flags here can permanently drop the only paint
        // we received for static pages and leave the browser visually blank.
        return;
    }

    bool              requestFreshPaint = false;
    bool              uploadMainFrame = false;
    uint64_t          uploadedPaintTimestamp = 0;
    CefRect           popupRect;
    std::vector<byte> popupPixels;

    {
        const uint64_t   mutexWaitStarted = m_bFrameStatsEnabled ? GetSteadyClockNanoseconds() : 0;
        std::unique_lock dataLock{m_RenderData.dataMutex};
        if (m_bFrameStatsEnabled)
        {
            m_FrameStats.uploadMutexWaitNanoseconds.fetch_add(GetSteadyClockNanoseconds() - mutexWaitStarted, std::memory_order_relaxed);
            m_FrameStats.uploadMutexWaitSamples.fetch_add(1, std::memory_order_relaxed);
        }

        const auto renderWidth = static_cast<int>(m_pWebBrowserRenderItem->m_uiSizeX);
        const auto renderHeight = static_cast<int>(m_pWebBrowserRenderItem->m_uiSizeY);

        // An asynchronous resize can leave one old-sized paint in the mailbox.
        // Drop only that pending frame and explicitly request the correctly sized one.
        if (m_RenderData.pendingChanged && (m_RenderData.pendingFrame.width != renderWidth || m_RenderData.pendingFrame.height != renderHeight)) [[unlikely]]
        {
            m_RenderData.pendingChanged = false;
            requestFreshPaint = true;
        }

        if (m_pWebBrowserRenderItem->m_bTextureWasRecreated)
        {
            m_pWebBrowserRenderItem->m_bTextureWasRecreated = false;
            m_RenderData.forceFullUpload = true;
        }

        if (m_RenderData.pendingChanged)
        {
            std::swap(m_RenderData.pendingFrame, m_RenderData.uploadFrame);
            m_RenderData.pendingChanged = false;
            uploadMainFrame = true;
        }

        const auto& uploadFrame = m_RenderData.uploadFrame;
        const bool  uploadFrameMatches =
            uploadFrame.buffer && uploadFrame.bufferSize > 0 && uploadFrame.width == renderWidth && uploadFrame.height == renderHeight;

        if (m_RenderData.forceFullUpload && uploadFrameMatches)
            uploadMainFrame = true;

        // Popup visibility changes must restore or composite against the full
        // main frame because D3DLOCK_DISCARD invalidates the entire texture.
        if (m_RenderData.popupChanged && uploadFrameMatches)
            uploadMainFrame = true;

        if (uploadMainFrame && uploadFrameMatches)
        {
            uploadedPaintTimestamp = uploadFrame.paintCompletedNanoseconds;
            m_RenderData.forceFullUpload = false;
            m_RenderData.popupChanged = false;

            const auto& currentPopupRect = m_RenderData.popupRect;
            const bool  popupRectValid = currentPopupRect.x >= 0 && currentPopupRect.y >= 0 && currentPopupRect.width > 0 && currentPopupRect.height > 0 &&
                                        currentPopupRect.x <= renderWidth - currentPopupRect.width &&
                                        currentPopupRect.y <= renderHeight - currentPopupRect.height;
            if (m_RenderData.popupShown && popupRectValid && m_RenderData.popupBuffer)
            {
                const size_t popupSize = static_cast<size_t>(currentPopupRect.width) * static_cast<size_t>(currentPopupRect.height) * CEF_PIXEL_STRIDE;
                popupRect = currentPopupRect;
                popupPixels.assign(m_RenderData.popupBuffer.get(), m_RenderData.popupBuffer.get() + popupSize);
            }
        }
    }

    if (requestFreshPaint && m_pWebView)
    {
        m_pWebView->GetHost()->WasResized();
        m_pWebView->GetHost()->Invalidate(PET_VIEW);
    }

    if (!uploadMainFrame)
        return;

    const auto& uploadFrame = m_RenderData.uploadFrame;
    if (!uploadFrame.buffer || uploadFrame.width <= 0 || uploadFrame.height <= 0 || uploadFrame.width > INT_MAX / CEF_PIXEL_STRIDE) [[unlikely]]
        return;

    const uint64_t uploadStarted = m_bFrameStatsEnabled ? GetSteadyClockNanoseconds() : 0;
    const auto*    sourceData = uploadFrame.buffer.get();
    const int      sourcePitch = uploadFrame.width * CEF_PIXEL_STRIDE;
    D3DLOCKED_RECT lockedRect;

    if (FAILED(pSurface->LockRect(&lockedRect, nullptr, D3DLOCK_DISCARD))) [[unlikely]]
    {
        OutputDebugLine("[CWebView] UpdateTexture: LockRect failed");
        std::lock_guard retryLock{m_RenderData.dataMutex};
        m_RenderData.forceFullUpload = true;
        return;
    }

    if (lockedRect.Pitch <= 0) [[unlikely]]
    {
        pSurface->UnlockRect();
        return;
    }

    auto* const destData = static_cast<byte*>(lockedRect.pBits);
    if (lockedRect.Pitch == sourcePitch) [[likely]]
    {
        std::memcpy(destData, sourceData, static_cast<size_t>(sourcePitch) * static_cast<size_t>(uploadFrame.height));
    }
    else
    {
        const size_t rowBytes = std::min(static_cast<size_t>(sourcePitch), static_cast<size_t>(lockedRect.Pitch));
        for (int y = 0; y < uploadFrame.height; ++y)
        {
            std::memcpy(destData + static_cast<size_t>(y) * static_cast<size_t>(lockedRect.Pitch),
                        sourceData + static_cast<size_t>(y) * static_cast<size_t>(sourcePitch), rowBytes);
        }
    }

    if (!popupPixels.empty())
    {
        const int popupPitch = popupRect.width * CEF_PIXEL_STRIDE;
        for (int y = 0; y < popupRect.height; ++y)
        {
            const size_t destOffset =
                static_cast<size_t>(popupRect.y + y) * static_cast<size_t>(lockedRect.Pitch) + static_cast<size_t>(popupRect.x) * CEF_PIXEL_STRIDE;
            const size_t sourceOffset = static_cast<size_t>(y) * static_cast<size_t>(popupPitch);
            std::memcpy(destData + destOffset, popupPixels.data() + sourceOffset, static_cast<size_t>(popupPitch));
        }
    }

    pSurface->UnlockRect();

    if (m_bFrameStatsEnabled)
    {
        const uint64_t uploadCompleted = GetSteadyClockNanoseconds();
        const uint64_t uploadDuration = uploadCompleted - uploadStarted;
        const uint64_t uploadAge = uploadedPaintTimestamp > 0 ? uploadCompleted - uploadedPaintTimestamp : 0;
        m_FrameStats.uploads.fetch_add(1, std::memory_order_relaxed);
        m_FrameStats.uploadNanoseconds.fetch_add(uploadDuration, std::memory_order_relaxed);
        if (uploadAge > 0)
        {
            m_FrameStats.uploadAgeNanoseconds.fetch_add(uploadAge, std::memory_order_relaxed);
            m_FrameStats.uploadAgeSamples.fetch_add(1, std::memory_order_relaxed);
            UpdateAtomicMaximum(m_FrameStats.maxUploadAgeNanoseconds, uploadAge);
        }
    }
}

void CWebView::RecordExternalBeginFrame()
{
    if (m_bFrameStatsEnabled)
        m_FrameStats.beginFrames.fetch_add(1, std::memory_order_relaxed);
}

void CWebView::ArmInteractionRefreshFrames(int frameCount)
{
    int current = m_iInteractionRefreshFrames.load(std::memory_order_relaxed);
    while (current < frameCount &&
           !m_iInteractionRefreshFrames.compare_exchange_weak(current, frameCount, std::memory_order_relaxed, std::memory_order_relaxed))
    {
    }
}

bool CWebView::HasInteractionRefreshFrames() const
{
    return m_iInteractionRefreshFrames.load(std::memory_order_relaxed) > 0;
}

bool CWebView::ConsumeInteractionRefreshFrame()
{
    int remaining = m_iInteractionRefreshFrames.load(std::memory_order_relaxed);
    while (remaining > 0)
    {
        if (m_iInteractionRefreshFrames.compare_exchange_weak(remaining, remaining - 1, std::memory_order_relaxed))
            return true;
    }
    return false;
}

void CWebView::RecordInteractionInvalidate()
{
    if (m_bFrameStatsEnabled)
        m_FrameStats.interactionInvalidates.fetch_add(1, std::memory_order_relaxed);
}

void CWebView::LogFrameStatsIfDue(int targetFrameRate, bool externalScheduling)
{
    if (!m_bFrameStatsEnabled)
        return;

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = now - m_FrameStats.lastReportTime;
    if (elapsed < std::chrono::seconds(2))
        return;

    m_FrameStats.lastReportTime = now;
    const double seconds = std::chrono::duration<double>(elapsed).count();
    const auto   beginFrames = m_FrameStats.beginFrames.exchange(0, std::memory_order_relaxed);
    const auto   interactionInvalidates = m_FrameStats.interactionInvalidates.exchange(0, std::memory_order_relaxed);
    const auto   paints = m_FrameStats.paints.exchange(0, std::memory_order_relaxed);
    const auto   acceleratedPaints = m_FrameStats.acceleratedPaints.exchange(0, std::memory_order_relaxed);
    const auto   acceleratedFailures = m_FrameStats.acceleratedFailures.exchange(0, std::memory_order_relaxed);
    const auto   acceleratedDroppedFrames = m_FrameStats.acceleratedDroppedFrames.exchange(0, std::memory_order_relaxed);
    const auto   acceleratedSubmitNanoseconds = m_FrameStats.acceleratedSubmitNanoseconds.exchange(0, std::memory_order_relaxed);
    const auto   acceleratedReadbacks = m_FrameStats.acceleratedReadbacks.exchange(0, std::memory_order_relaxed);
    const auto   acceleratedReadbackNanoseconds = m_FrameStats.acceleratedReadbackNanoseconds.exchange(0, std::memory_order_relaxed);
    const auto   uploads = m_FrameStats.uploads.exchange(0, std::memory_order_relaxed);
    const auto   supersededPaints = m_FrameStats.supersededPaints.exchange(0, std::memory_order_relaxed);
    const auto   dirtyPixels = m_FrameStats.dirtyPixels.exchange(0, std::memory_order_relaxed);
    const auto   paintBytes = m_FrameStats.paintBytes.exchange(0, std::memory_order_relaxed);
    const auto   paintCopyNanoseconds = m_FrameStats.paintCopyNanoseconds.exchange(0, std::memory_order_relaxed);
    const auto   paintMutexWaitNanoseconds = m_FrameStats.paintMutexWaitNanoseconds.exchange(0, std::memory_order_relaxed);
    const auto   paintMutexWaitSamples = m_FrameStats.paintMutexWaitSamples.exchange(0, std::memory_order_relaxed);
    const auto   uploadNanoseconds = m_FrameStats.uploadNanoseconds.exchange(0, std::memory_order_relaxed);
    const auto   uploadMutexWaitNanoseconds = m_FrameStats.uploadMutexWaitNanoseconds.exchange(0, std::memory_order_relaxed);
    const auto   uploadMutexWaitSamples = m_FrameStats.uploadMutexWaitSamples.exchange(0, std::memory_order_relaxed);
    const auto   paintIntervalNanoseconds = m_FrameStats.paintIntervalNanoseconds.exchange(0, std::memory_order_relaxed);
    const auto   paintIntervalSamples = m_FrameStats.paintIntervalSamples.exchange(0, std::memory_order_relaxed);
    const auto   maxPaintIntervalNanoseconds = m_FrameStats.maxPaintIntervalNanoseconds.exchange(0, std::memory_order_relaxed);
    const auto   uploadAgeNanoseconds = m_FrameStats.uploadAgeNanoseconds.exchange(0, std::memory_order_relaxed);
    const auto   uploadAgeSamples = m_FrameStats.uploadAgeSamples.exchange(0, std::memory_order_relaxed);
    const auto   maxUploadAgeNanoseconds = m_FrameStats.maxUploadAgeNanoseconds.exchange(0, std::memory_order_relaxed);

    const auto width = m_pWebBrowserRenderItem ? m_pWebBrowserRenderItem->m_uiSizeX : 0;
    const auto height = m_pWebBrowserRenderItem ? m_pWebBrowserRenderItem->m_uiSizeY : 0;
    const auto framePixels = static_cast<double>(width) * static_cast<double>(height);
    const auto dirtyPercent = paints > 0 && framePixels > 0.0 ? std::min(100.0, static_cast<double>(dirtyPixels) * 100.0 / (framePixels * paints)) : 0.0;
    const auto paintCopyMs = paints > 0 ? static_cast<double>(paintCopyNanoseconds) / paints / 1'000'000.0 : 0.0;
    const auto acceleratedSubmitMs = acceleratedPaints > 0 ? static_cast<double>(acceleratedSubmitNanoseconds) / acceleratedPaints / 1'000'000.0 : 0.0;
    const auto acceleratedReadbackMs =
        acceleratedReadbacks > 0 ? static_cast<double>(acceleratedReadbackNanoseconds) / acceleratedReadbacks / 1'000'000.0 : 0.0;
    const auto paintWaitMs = paintMutexWaitSamples > 0 ? static_cast<double>(paintMutexWaitNanoseconds) / paintMutexWaitSamples / 1'000'000.0 : 0.0;
    const auto uploadMs = uploads > 0 ? static_cast<double>(uploadNanoseconds) / uploads / 1'000'000.0 : 0.0;
    const auto uploadWaitMs = uploadMutexWaitSamples > 0 ? static_cast<double>(uploadMutexWaitNanoseconds) / uploadMutexWaitSamples / 1'000'000.0 : 0.0;
    const auto paintIntervalMs = paintIntervalSamples > 0 ? static_cast<double>(paintIntervalNanoseconds) / paintIntervalSamples / 1'000'000.0 : 0.0;
    const auto uploadAgeMs = uploadAgeSamples > 0 ? static_cast<double>(uploadAgeNanoseconds) / uploadAgeSamples / 1'000'000.0 : 0.0;
    const auto browserId = m_pWebView ? m_pWebView->GetIdentifier() : -1;

    // This path is reached only when browser_frame_stats is enabled. Persist
    // the sample in MTA's event log so VM profiling does not require an
    // attached Windows debugger that can itself perturb frame pacing.
    WriteDebugEvent(
        SString("[CEF PERF] id=%d size=%ux%u transparent=%d scheduler=%s target=%d begin=%.1fHz forcedInvalidate=%.1fHz paint=%.1fHz "
                "accelerated=%.1fHz accelFail=%llu "
                "accelDrop=%llu accelSubmit=%.2fms accelReadback=%.2fms upload=%.1fHz superseded=%llu dirty=%.1f%% copy=%.2fms paintWait=%.2fms "
                "upload=%.2fms uploadWait=%.2fms interval=%.2fms maxInterval=%.2fms age=%.2fms maxAge=%.2fms copyRate=%.1fMiB/s",
                browserId, width, height, m_bIsTransparent, externalScheduling ? "external" : "cef", targetFrameRate, beginFrames / seconds,
                interactionInvalidates / seconds, paints / seconds, acceleratedPaints / seconds, static_cast<unsigned long long>(acceleratedFailures),
                static_cast<unsigned long long>(acceleratedDroppedFrames), acceleratedSubmitMs, acceleratedReadbackMs, uploads / seconds,
                static_cast<unsigned long long>(supersededPaints), dirtyPercent, paintCopyMs, paintWaitMs, uploadMs, uploadWaitMs, paintIntervalMs,
                static_cast<double>(maxPaintIntervalNanoseconds) / 1'000'000.0, uploadAgeMs, static_cast<double>(maxUploadAgeNanoseconds) / 1'000'000.0,
                static_cast<double>(paintBytes) / seconds / (1024.0 * 1024.0)));
}

void CWebView::ExecuteJavascript(const SString& strJavascriptCode)
{
    if (m_pWebView)
        m_pWebView->GetMainFrame()->ExecuteJavaScript(strJavascriptCode, "", 0);
}

bool CWebView::SetProperty(const SString& strKey, const SString& strValue)
{
    if (strKey == "mobile" && (strValue == "0" || strValue == "1"))
    {
    }
    else if (strKey == "isolated_request_context" && !m_bBrowserCreated && (strValue == "0" || strValue == "1"))
    {
    }
    else
        return false;

    m_Properties[strKey] = strValue;
    return true;
}

bool CWebView::GetProperty(const SString& strKey, SString& outValue)
{
    auto iter = m_Properties.find(strKey);
    if (iter == m_Properties.end())
        return false;

    outValue = iter->second;
    return true;
}

void CWebView::InjectMouseMove(int iPosX, int iPosY)
{
    if (!m_pWebView)
        return;

    // Throttle mouse move events to reduce excessive CEF repaints
    // Allow ~60 mouse updates per second (16ms interval)
    constexpr auto MOUSE_THROTTLE_INTERVAL = std::chrono::milliseconds(16);
    auto           now = std::chrono::steady_clock::now();

    // Always update the pending position
    m_vecPendingMousePosition.x = iPosX;
    m_vecPendingMousePosition.y = iPosY;
    m_bHasPendingMouseMove = true;

    // A scrollbar drag changes compositor state on every mouse move. Keep a
    // short refresh tail armed even when this particular move is retained by
    // the input throttle, otherwise CEF OSR can publish drag frames sparsely.
    if (m_mouseButtonStates[BROWSER_MOUSEBUTTON_LEFT])
        ArmInteractionRefreshFrames(3);

    // Check if enough time has passed since last mouse move
    if (now - m_lastMouseMoveTime < MOUSE_THROTTLE_INTERVAL)
    {
        // Keep the latest coordinates for the next move or input action.
        return;
    }

    FlushPendingMouseMove();
}

void CWebView::FlushPendingMouseMove()
{
    if (!m_pWebView || !m_bHasPendingMouseMove)
        return;

    m_vecMousePosition = m_vecPendingMousePosition;
    m_bHasPendingMouseMove = false;
    m_lastMouseMoveTime = std::chrono::steady_clock::now();

    CefMouseEvent mouseEvent;
    mouseEvent.x = m_vecMousePosition.x;
    mouseEvent.y = m_vecMousePosition.y;

    if (m_mouseButtonStates[BROWSER_MOUSEBUTTON_LEFT])
        mouseEvent.modifiers |= EVENTFLAG_LEFT_MOUSE_BUTTON;
    if (m_mouseButtonStates[BROWSER_MOUSEBUTTON_MIDDLE])
        mouseEvent.modifiers |= EVENTFLAG_MIDDLE_MOUSE_BUTTON;
    if (m_mouseButtonStates[BROWSER_MOUSEBUTTON_RIGHT])
        mouseEvent.modifiers |= EVENTFLAG_RIGHT_MOUSE_BUTTON;

    m_pWebView->GetHost()->SendMouseMoveEvent(mouseEvent, false);
}

void CWebView::InjectMouseDown(eWebBrowserMouseButton mouseButton, int count)
{
    if (!m_pWebView)
        return;

    // Actions must use the latest physical cursor position even when the
    // 60 Hz move throttle retained the final move of a short gesture.
    FlushPendingMouseMove();

    CefMouseEvent mouseEvent;
    mouseEvent.x = m_vecMousePosition.x;
    mouseEvent.y = m_vecMousePosition.y;

    // Save mouse button states
    m_mouseButtonStates[static_cast<int>(mouseButton)] = true;

    if (mouseButton == BROWSER_MOUSEBUTTON_LEFT)
        ArmInteractionRefreshFrames(4);

    m_pWebView->GetHost()->SendMouseClickEvent(mouseEvent, static_cast<CefBrowserHost::MouseButtonType>(mouseButton), false, count);
}

void CWebView::InjectMouseUp(eWebBrowserMouseButton mouseButton)
{
    if (!m_pWebView)
        return;

    FlushPendingMouseMove();

    CefMouseEvent mouseEvent;
    mouseEvent.x = m_vecMousePosition.x;
    mouseEvent.y = m_vecMousePosition.y;

    // Save mouse button states
    m_mouseButtonStates[static_cast<int>(mouseButton)] = false;

    if (mouseButton == BROWSER_MOUSEBUTTON_LEFT)
        ArmInteractionRefreshFrames(4);

    m_pWebView->GetHost()->SendMouseClickEvent(mouseEvent, static_cast<CefBrowserHost::MouseButtonType>(mouseButton), true, 1);
}

void CWebView::InjectMouseWheel(int iScrollVert, int iScrollHorz)
{
    if (!m_pWebView)
        return;

    // Without this flush, a wheel immediately following a throttled move is
    // hit-tested at stale coordinates and can target a non-scrollable panel.
    FlushPendingMouseMove();

    CefMouseEvent mouseEvent;
    mouseEvent.x = m_vecMousePosition.x;
    mouseEvent.y = m_vecMousePosition.y;

    m_pWebView->GetHost()->SendMouseWheelEvent(mouseEvent, iScrollHorz, iScrollVert);

    // CEF's off-screen frame sink can publish scroll damage much more slowly
    // than its configured frame-rate ceiling. Request a bounded refresh
    // window after input without continuously redrawing static menus.
    ArmInteractionRefreshFrames(24);
}

void CWebView::InjectKeyboardEvent(const CefKeyEvent& keyEvent)
{
    if (m_pWebView)
        m_pWebView->GetHost()->SendKeyEvent(keyEvent);
}

bool CWebView::SetAudioVolume(float fVolume)
{
    // NOTE: Keep this function thread-safe
    if (!m_pWebView || fVolume < 0.0f || fVolume > 1.0f)
        return false;

    // Since the necessary interfaces of the core audio API were introduced in Win7, we've to fallback to HTML5 audio
    SString strJSCode(
        "function mta_adjustAudioVol(elem, vol) { elem.volume = vol; elem.onvolumechange = function() { if (Math.abs(elem.volume - vol) >= 0.001) elem.volume "
        "= vol; } }"
        "var tags = document.getElementsByTagName('audio'); for (var i = 0; i<tags.length; ++i) { mta_adjustAudioVol(tags[i], %f); }"
        "tags = document.getElementsByTagName('video'); for (var i = 0; i<tags.length; ++i) { mta_adjustAudioVol(tags[i], %f); }",
        fVolume, fVolume);

    // Note: GetFrameNames is deprecated, but no modern alternative exists for audio volume control
    // This is a legacy thing that works with CEF3
    std::vector<CefString> frameNames;
    m_pWebView->GetFrameNames(frameNames);

    for (auto& name : frameNames)
    {
#ifdef MTA_MAETRO
        auto frame = m_pWebView->GetFrame(name);
#else
        auto frame = m_pWebView->GetFrameByName(name);
#endif
        if (frame)
            frame->ExecuteJavaScript(strJSCode, "", 0);
    }
    m_fVolume = fVolume;
    return true;
}

void CWebView::GetSourceCode(const std::function<void(const std::string& code)>& callback)
{
    if (!m_pWebView)
        return;

    class MyStringVisitor : public CefStringVisitor
    {
    private:
        CefRefPtr<CWebView>                     webView;
        std::function<void(const std::string&)> callback;

    public:
        MyStringVisitor(CWebView* webView_, const std::function<void(const std::string&)>& callback_) : webView(webView_), callback(callback_) {}

        virtual void Visit(const CefString& code) override
        {
            // Check if webview is being destroyed to prevent UAF
            if (webView->IsBeingDestroyed())
                return;

            // Limit to 2MiB for now to prevent freezes (TODO: Optimize that and increase later)
            if (code.size() <= 2097152)
            {
                // Call callback on main thread
                g_pCore->GetWebCore()->AddEventToEventQueue(std::bind(callback, code), webView.get(), "GetSourceCode_Visit");
            }
        }

        IMPLEMENT_REFCOUNTING(MyStringVisitor);
    };

    CefRefPtr<CefStringVisitor> visitor{new MyStringVisitor(this, callback)};
    m_pWebView->GetMainFrame()->GetSource(visitor);
}

void CWebView::Resize(const CVector2D& size)
{
    // Validate render item exists
    if (!m_pWebBrowserRenderItem) [[unlikely]]
        return;

    // Resize underlying texture
    m_pWebBrowserRenderItem->Resize(size);

    // Send resize event to CEF
    if (m_pWebView)
        m_pWebView->GetHost()->WasResized();
}

CVector2D CWebView::GetSize()
{
    if (!m_pWebBrowserRenderItem) [[unlikely]]
        return CVector2D(0.0f, 0.0f);

    return CVector2D(static_cast<float>(m_pWebBrowserRenderItem->m_uiSizeX), static_cast<float>(m_pWebBrowserRenderItem->m_uiSizeY));
}

bool CWebView::GetFullPathFromLocal(SString& strPath)
{
    bool result = false;

    g_pCore->GetWebCore()->WaitForTask(
        [&](bool aborted)
        {
            if (aborted)
                return;

            auto* events = m_pEventsInterface;
            if (!events)
                return;

            result = events->Events_OnResourcePathCheck(strPath);
        },
        this);

    return result;
}

bool CWebView::RegisterAjaxHandler(const SString& strURL)
{
    auto [iter, inserted] = m_AjaxHandlers.insert(strURL);
    return inserted;
}

bool CWebView::UnregisterAjaxHandler(const SString& strURL)
{
    return m_AjaxHandlers.erase(strURL) == 1;
}

bool CWebView::HasAjaxHandler(const SString& strURL)
{
    auto iterCB = m_AjaxHandlers.find(strURL);
    return iterCB != m_AjaxHandlers.end();
}

void CWebView::HandleAjaxRequest(const SString& strURL, CAjaxResourceHandler* pHandler)
{
    // Only queue event if not being destroyed to prevent UAF
    if (!m_bBeingDestroyed)
    {
        QueueBrowserEvent("AjaxResourceRequest",
                          [handler = pHandler, url = strURL](CWebBrowserEventsInterface* iface) { iface->Events_OnAjaxRequest(handler, url); });
    }
}

bool CWebView::ToggleDevTools(bool visible)
{
    if (visible)
        return CWebDevTools::Show(this);

    return CWebDevTools::Close(this);
}

bool CWebView::VerifyFile(const SString& strPath, CBuffer& outFileData)
{
    bool result = false;

    g_pCore->GetWebCore()->WaitForTask(
        [&](bool aborted)
        {
            if (aborted)
                return;

            auto* events = m_pEventsInterface;
            if (!events)
                return;

            result = events->Events_OnResourceFileCheck(strPath, outFileData);
        },
        this);

    return result;
}

bool CWebView::CanGoBack()
{
    if (!m_pWebView)
        return false;

    return m_pWebView->CanGoBack();
}

bool CWebView::CanGoForward()
{
    if (!m_pWebView)
        return false;

    return m_pWebView->CanGoForward();
}

bool CWebView::GoBack()
{
    if (!m_pWebView)
        return false;

    if (!m_pWebView->CanGoBack())
        return false;

    m_pWebView->GoBack();
    return true;
}

bool CWebView::GoForward()
{
    if (!m_pWebView)
        return false;

    if (!m_pWebView->CanGoForward())
        return false;

    m_pWebView->GoForward();
    return true;
}

void CWebView::Refresh(bool bIgnoreCache)
{
    if (!m_pWebView)
        return;

    if (bIgnoreCache)
    {
        m_pWebView->ReloadIgnoreCache();
    }
    else
    {
        m_pWebView->Reload();
    }
}

////////////////////////////////////////////////////////////////////
//                                                                //
// Implementation: CefClient::OnProcessMessageReceived            //
// https://magpcss.org/ceforum/apidocs3/projects/(default)/CefClient.html#OnProcessMessageReceived(CefRefPtr%3CCefBrowser%3E,CefProcessId,CefRefPtr%3CCefProcessMessage%3E)
// //
//                                                                //
////////////////////////////////////////////////////////////////////
bool CWebView::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId source_process,
                                        CefRefPtr<CefProcessMessage> message)
{
    if (m_bBeingDestroyed)
        return false;

    CefRefPtr<CefListValue> argList = message->GetArgumentList();
    if (message->GetName() == "TriggerLuaEvent")
        return WebViewAuth::HandleTriggerLuaEvent(this, argList, m_bIsLocal);  // AUTH

    if (message->GetName() == "InputFocus")
        return WebViewAuth::HandleInputFocus(this, argList, m_bIsLocal);  // AUTH

    // The message wasn't handled
    return false;
}

////////////////////////////////////////////////////////////////////
//                                                                //
// Implementation: CefRenderHandler::GetViewRect                  //
// https://magpcss.org/ceforum/apidocs3/projects/(default)/CefRenderHandler.html#GetViewRect(CefRefPtr%3CCefBrowser%3E,CefRect&) //
//                                                                //
////////////////////////////////////////////////////////////////////
void CWebView::GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect)
{
    rect.x = 0;
    rect.y = 0;

    if (m_bBeingDestroyed || !m_pWebBrowserRenderItem) [[unlikely]]
    {
        rect.width = 1;
        rect.height = 1;
        return;
    }

    rect.width = static_cast<int>(m_pWebBrowserRenderItem->m_uiSizeX);
    rect.height = static_cast<int>(m_pWebBrowserRenderItem->m_uiSizeY);
}

////////////////////////////////////////////////////////////////////
//                                                                //
// Implementation: CefRenderHandler::OnPopupShow                  //
// https://magpcss.org/ceforum/apidocs3/projects/(default)/CefRenderHandler.html#OnPopupShow(CefRefPtr<CefBrowser>,bool) //
//                                                                //
////////////////////////////////////////////////////////////////////
void CWebView::OnPopupShow(CefRefPtr<CefBrowser> browser, bool show)
{
    std::lock_guard<std::mutex> lock{m_RenderData.dataMutex};
    m_RenderData.popupShown = show;
    m_RenderData.popupChanged = true;

    // Free popup buffer memory if hidden
    if (!show)
        m_RenderData.popupBuffer.reset();
}

////////////////////////////////////////////////////////////////////
//                                                                //
// Implementation: CefRenderHandler::OnPopupSize                  //
// https://magpcss.org/ceforum/apidocs3/projects/(default)/CefRenderHandler.html#OnPopupSize(CefRefPtr<CefBrowser>,constCefRect&) //
//                                                                //
////////////////////////////////////////////////////////////////////
void CWebView::OnPopupSize(CefRefPtr<CefBrowser> browser, const CefRect& rect)
{
    std::lock_guard<std::mutex> lock{m_RenderData.dataMutex};

    // If dimensions change, the current popup buffer is no longer valid for the new size
    // We must release it to prevent UpdateTexture from reading past the end of the buffer
    // using the new (larger) dimensions
    if (m_RenderData.popupRect.width != rect.width || m_RenderData.popupRect.height != rect.height)
    {
        m_RenderData.popupBuffer.reset();
    }

    // Update rect
    m_RenderData.popupRect = rect;
    m_RenderData.popupChanged = true;

    // Note: Don't allocate buffer here - OnPaint may provide different dimensions
    // Buffer allocation moved to OnPaint to prevent dimension mismatch
}

////////////////////////////////////////////////////////////////////
//                                                                //
// Implementation: CefRenderHandler::OnPaint                      //
// https://magpcss.org/ceforum/apidocs3/projects/(default)/CefRenderHandler.html#OnPaint(CefRefPtr%3CCefBrowser%3E,PaintElementType,constRectList&,constvoid*,int,int)
// //
//                                                                //
////////////////////////////////////////////////////////////////////
void CWebView::OnPaint(CefRefPtr<CefBrowser> browser, CefRenderHandler::PaintElementType paintType, const CefRenderHandler::RectList& dirtyRects,
                       const void* buffer, int width, int height)
{
    if (m_bBeingDestroyed) [[unlikely]]
        return;

    // Copy popup buffer
    if (paintType == PET_POPUP)
    {
        // Validate buffer parameter from CEF
        if (!buffer || width <= 0 || height <= 0) [[unlikely]]
            return;

        // Allocate buffer based on actual paint dimensions, not OnPopupSize rect
        // This prevents buffer overflow when CEF provides different dimensions
        // Check for integer overflow in size calculation: width * height * CEF_PIXEL_STRIDE must fit in size_t
        constexpr auto maxDimension = INT_MAX / CEF_PIXEL_STRIDE;
        if (width > maxDimension || height > maxDimension) [[unlikely]]
            return;  // Individual dimension too large
        if (static_cast<size_t>(width) > SIZE_MAX / (static_cast<size_t>(height) * CEF_PIXEL_STRIDE)) [[unlikely]]
            return;  // width * height * stride would overflow

        std::lock_guard<std::mutex> lock{m_RenderData.dataMutex};
        const auto                  requiredSize = static_cast<size_t>(width) * static_cast<size_t>(height) * CEF_PIXEL_STRIDE;

        // Calculate current size safely to avoid overflow
        size_t      currentSize = 0;
        const auto& popupRect = m_RenderData.popupRect;
        if (popupRect.width > 0 && popupRect.height > 0 && popupRect.width <= maxDimension && popupRect.height <= maxDimension &&
            static_cast<size_t>(popupRect.width) <= SIZE_MAX / (static_cast<size_t>(popupRect.height) * CEF_PIXEL_STRIDE)) [[likely]]
        {
            currentSize = static_cast<size_t>(popupRect.width) * static_cast<size_t>(popupRect.height) * CEF_PIXEL_STRIDE;
        }

        // Reallocate if size changed or buffer doesn't exist
        if (!m_RenderData.popupBuffer || requiredSize != currentSize) [[unlikely]]
        {
            m_RenderData.popupBuffer = std::make_unique<byte[]>(requiredSize);
            // Update rect to reflect actual dimensions
            m_RenderData.popupRect.width = width;
            m_RenderData.popupRect.height = height;
        }

        std::memcpy(m_RenderData.popupBuffer.get(), buffer, requiredSize);
        m_RenderData.popupChanged = true;

        return;
    }

    // Only the CEF callback touches paintFrame. A dedicated lock lets pause
    // safely free it without serialising the game-thread texture upload.
    const uint64_t   mutexWaitStarted = m_bFrameStatsEnabled ? GetSteadyClockNanoseconds() : 0;
    std::unique_lock paintLock{m_RenderData.paintMutex};
    if (m_bFrameStatsEnabled)
    {
        m_FrameStats.paintMutexWaitNanoseconds.fetch_add(GetSteadyClockNanoseconds() - mutexWaitStarted, std::memory_order_relaxed);
        m_FrameStats.paintMutexWaitSamples.fetch_add(1, std::memory_order_relaxed);
    }

    // Validate main frame buffer parameter
    if (!buffer || width <= 0 || height <= 0) [[unlikely]]
        return;

    // Check for integer overflow in size calculation
    constexpr auto maxDimension = INT_MAX / CEF_PIXEL_STRIDE;
    if (width > maxDimension || height > maxDimension) [[unlikely]]
        return;

    if (static_cast<size_t>(width) > SIZE_MAX / (static_cast<size_t>(height) * CEF_PIXEL_STRIDE)) [[unlikely]]
        return;

    auto&      paintFrame = m_RenderData.paintFrame;
    const auto requiredSize = static_cast<size_t>(width) * static_cast<size_t>(height) * CEF_PIXEL_STRIDE;
    const auto sourcePitch = static_cast<size_t>(width) * CEF_PIXEL_STRIDE;

    std::vector<CefRect> currentDirtyRects;
    currentDirtyRects.reserve(dirtyRects.size());
    uint64_t currentDirtyPixels = 0;
    for (const auto& rect : dirtyRects)
    {
        const int64_t left = std::clamp<int64_t>(rect.x, 0, width);
        const int64_t top = std::clamp<int64_t>(rect.y, 0, height);
        const int64_t right = std::clamp<int64_t>(static_cast<int64_t>(rect.x) + rect.width, 0, width);
        const int64_t bottom = std::clamp<int64_t>(static_cast<int64_t>(rect.y) + rect.height, 0, height);
        if (right <= left || bottom <= top)
            continue;

        currentDirtyRects.emplace_back(static_cast<int>(left), static_cast<int>(top), static_cast<int>(right - left), static_cast<int>(bottom - top));
        currentDirtyPixels += static_cast<uint64_t>(right - left) * static_cast<uint64_t>(bottom - top);
    }

    // An empty damage list is not useful for synchronising a recycled mailbox
    // buffer. Treat it as a full paint rather than risk retaining stale pixels.
    if (currentDirtyRects.empty())
    {
        currentDirtyRects.emplace_back(0, 0, width, height);
        currentDirtyPixels = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
    }

    if (m_RenderData.dirtyHistoryWidth != width || m_RenderData.dirtyHistoryHeight != height)
    {
        m_RenderData.dirtyHistoryWidth = width;
        m_RenderData.dirtyHistoryHeight = height;
        m_RenderData.latestPaintGeneration = 0;
        m_RenderData.dirtyHistory.clear();
    }
    const uint64_t incomingGeneration = ++m_RenderData.latestPaintGeneration;

    // Allocate or reallocate buffer if size changed
    const bool bSizeChanged = !paintFrame.buffer || paintFrame.bufferSize != requiredSize;
    if (bSizeChanged) [[unlikely]]
    {
        paintFrame.buffer = std::make_unique<byte[]>(requiredSize);
        paintFrame.bufferSize = requiredSize;
        // Zero-initialize new buffer to avoid garbage pixels in areas not painted yet
        std::memset(paintFrame.buffer.get(), 0, requiredSize);
        paintFrame.generation = 0;
    }

    std::vector<CefRect> copyRects;
    bool                 copyFullFrame = bSizeChanged || paintFrame.generation == 0;
    if (!copyFullFrame)
    {
        const uint64_t oldestNeededGeneration = paintFrame.generation + 1;
        if (oldestNeededGeneration < incomingGeneration &&
            (m_RenderData.dirtyHistory.empty() || m_RenderData.dirtyHistory.front().generation > oldestNeededGeneration))
        {
            // This mailbox buffer is older than the retained damage history.
            copyFullFrame = true;
        }
        else
        {
            for (const auto& dirtyFrame : m_RenderData.dirtyHistory)
            {
                if (dirtyFrame.generation > paintFrame.generation)
                    copyRects.insert(copyRects.end(), dirtyFrame.rects.begin(), dirtyFrame.rects.end());
            }
            copyRects.insert(copyRects.end(), currentDirtyRects.begin(), currentDirtyRects.end());

            uint64_t accumulatedPixels = 0;
            for (const auto& rect : copyRects)
                accumulatedPixels += static_cast<uint64_t>(rect.width) * static_cast<uint64_t>(rect.height);

            // Many overlapping rectangles cost more to walk than one linear copy.
            // At that point the full path is both simpler and faster.
            const uint64_t framePixels = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
            copyFullFrame = accumulatedPixels >= framePixels * 65 / 100;
        }
    }

    const uint64_t copyStarted = m_bFrameStatsEnabled ? GetSteadyClockNanoseconds() : 0;
    size_t         copiedBytes = 0;
    if (copyFullFrame)
    {
        std::memcpy(paintFrame.buffer.get(), buffer, requiredSize);
        copiedBytes = requiredSize;
    }
    else
    {
        const auto* sourcePixels = static_cast<const byte*>(buffer);
        for (const auto& rect : copyRects)
        {
            const size_t rowBytes = static_cast<size_t>(rect.width) * CEF_PIXEL_STRIDE;
            for (int y = 0; y < rect.height; ++y)
            {
                const size_t offset = static_cast<size_t>(rect.y + y) * sourcePitch + static_cast<size_t>(rect.x) * CEF_PIXEL_STRIDE;
                std::memcpy(paintFrame.buffer.get() + offset, sourcePixels + offset, rowBytes);
                copiedBytes += rowBytes;
            }
        }
    }
    const uint64_t paintCompleted = m_bFrameStatsEnabled ? GetSteadyClockNanoseconds() : 0;

    paintFrame.width = width;
    paintFrame.height = height;
    paintFrame.paintCompletedNanoseconds = paintCompleted;
    paintFrame.generation = incomingGeneration;

    m_RenderData.dirtyHistory.push_back({incomingGeneration, currentDirtyRects});
    constexpr size_t MAX_DIRTY_HISTORY = 8;
    while (m_RenderData.dirtyHistory.size() > MAX_DIRTY_HISTORY)
        m_RenderData.dirtyHistory.pop_front();

    bool supersededPendingFrame = false;
    {
        std::lock_guard publishLock{m_RenderData.dataMutex};
        supersededPendingFrame = m_RenderData.pendingChanged;
        std::swap(m_RenderData.paintFrame, m_RenderData.pendingFrame);
        m_RenderData.pendingChanged = true;
    }

    if (m_bFrameStatsEnabled)
    {
        m_FrameStats.paints.fetch_add(1, std::memory_order_relaxed);
        if (supersededPendingFrame)
            m_FrameStats.supersededPaints.fetch_add(1, std::memory_order_relaxed);
        m_FrameStats.dirtyPixels.fetch_add(currentDirtyPixels, std::memory_order_relaxed);
        m_FrameStats.paintBytes.fetch_add(copiedBytes, std::memory_order_relaxed);
        m_FrameStats.paintCopyNanoseconds.fetch_add(paintCompleted - copyStarted, std::memory_order_relaxed);

        const uint64_t previousPaint = m_FrameStats.lastPaintTimestampNanoseconds.exchange(paintCompleted, std::memory_order_relaxed);
        if (previousPaint > 0)
        {
            const uint64_t interval = paintCompleted - previousPaint;
            m_FrameStats.paintIntervalNanoseconds.fetch_add(interval, std::memory_order_relaxed);
            m_FrameStats.paintIntervalSamples.fetch_add(1, std::memory_order_relaxed);
            UpdateAtomicMaximum(m_FrameStats.maxPaintIntervalNanoseconds, interval);
        }
    }
}

////////////////////////////////////////////////////////////////////
//                                                                //
// Implementation: CefRenderHandler::OnAcceleratedPaint           //
//                                                                //
////////////////////////////////////////////////////////////////////
void CWebView::OnAcceleratedPaint(CefRefPtr<CefBrowser> browser, CefRenderHandler::PaintElementType paintType, const CefRenderHandler::RectList& dirtyRects,
                                  const CefAcceleratedPaintInfo& info)
{
    if (m_bBeingDestroyed || !info.shared_texture_handle) [[unlikely]]
        return;

    auto&           backend = *m_pAcceleratedPaintBackend;
    std::lock_guard backendLock{backend.mutex};

    const uint64_t submitStarted = m_bFrameStatsEnabled ? GetSteadyClockNanoseconds() : 0;
    const auto     RecordFailure = [&]()
    {
        if (m_bFrameStatsEnabled)
            m_FrameStats.acceleratedFailures.fetch_add(1, std::memory_order_relaxed);

        if (!backend.failureLogged)
        {
            backend.failureLogged = true;
            WriteDebugEvent(
                SString("[CEF ACCEL] Shared-texture readback failed (HRESULT=0x%08X); restart with browser_shared_texture=0 to use "
                        "software OnPaint",
                        static_cast<unsigned int>(backend.lastFailure)));
        }
    };

    if (!backend.EnsureDevice()) [[unlikely]]
    {
        RecordFailure();
        return;
    }

    ID3D11Texture2D* sourceTexture = nullptr;
    backend.lastFailure = backend.device->OpenSharedResource1(info.shared_texture_handle, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&sourceTexture));
    if (FAILED(backend.lastFailure) || !sourceTexture) [[unlikely]]
    {
        RecordFailure();
        return;
    }

    D3D11_TEXTURE2D_DESC sourceDescription{};
    sourceTexture->GetDesc(&sourceDescription);

    const bool supportedFormat = sourceDescription.Format == DXGI_FORMAT_B8G8R8A8_UNORM || sourceDescription.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
                                 sourceDescription.Format == DXGI_FORMAT_R8G8B8A8_UNORM || sourceDescription.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    const bool supportedLayout = sourceDescription.MipLevels == 1 && sourceDescription.ArraySize == 1 && sourceDescription.SampleDesc.Count == 1;
    auto&      pipeline = paintType == PET_POPUP ? backend.popupPipeline : backend.viewPipeline;
    if (!supportedFormat || !supportedLayout || !backend.EnsurePipeline(pipeline, sourceDescription)) [[unlikely]]
    {
        if (SUCCEEDED(backend.lastFailure))
            backend.lastFailure = E_NOTIMPL;
        sourceTexture->Release();
        RecordFailure();
        return;
    }

    bool  droppedCompletedFrame = false;
    auto* slot = backend.AcquireSubmissionSlot(pipeline, droppedCompletedFrame);
    if (!slot) [[unlikely]]
    {
        sourceTexture->Release();
        if (m_bFrameStatsEnabled)
            m_FrameStats.acceleratedDroppedFrames.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    const auto& visibleRect = info.extra.visible_rect;
    const int   width = visibleRect.width;
    const int   height = visibleRect.height;
    const bool  visibleRectValid = visibleRect.x >= 0 && visibleRect.y >= 0 && width > 0 && height > 0 &&
                                  static_cast<uint64_t>(visibleRect.x) + static_cast<uint64_t>(width) <= sourceDescription.Width &&
                                  static_cast<uint64_t>(visibleRect.y) + static_cast<uint64_t>(height) <= sourceDescription.Height;
    const bool sizeValid =
        visibleRectValid && width <= INT_MAX / CEF_PIXEL_STRIDE && static_cast<size_t>(width) <= SIZE_MAX / (static_cast<size_t>(height) * CEF_PIXEL_STRIDE);
    if (!sizeValid) [[unlikely]]
    {
        sourceTexture->Release();
        backend.lastFailure = E_INVALIDARG;
        RecordFailure();
        return;
    }

    // Submit the cross-device copy and return to CEF without waiting for a CPU
    // Map. The event query lets the game thread consume only completed copies,
    // so the expensive readback no longer throttles Chromium's UI thread.
    backend.deviceContext->CopyResource(slot->stagingTexture, sourceTexture);
    sourceTexture->Release();
    backend.deviceContext->End(slot->completionQuery);
    backend.deviceContext->Flush();

    slot->dirtyRects.assign(dirtyRects.begin(), dirtyRects.end());
    slot->visibleRect = visibleRect;
    slot->format = info.format;
    slot->sequence = pipeline.nextSequence++;
    slot->pending = true;
    m_bHasPendingAcceleratedPaint.store(true, std::memory_order_release);

    if (m_bFrameStatsEnabled)
    {
        m_FrameStats.acceleratedPaints.fetch_add(1, std::memory_order_relaxed);
        if (droppedCompletedFrame)
            m_FrameStats.acceleratedDroppedFrames.fetch_add(1, std::memory_order_relaxed);
        m_FrameStats.acceleratedSubmitNanoseconds.fetch_add(GetSteadyClockNanoseconds() - submitStarted, std::memory_order_relaxed);
    }
}

void CWebView::ConsumeAcceleratedPaint()
{
    if (!m_bHasPendingAcceleratedPaint.load(std::memory_order_acquire))
        return;

    auto&           backend = *m_pAcceleratedPaintBackend;
    std::lock_guard backendLock{backend.mutex};

    const auto ConsumePipeline = [&](FAcceleratedPaintBackend::FReadbackPipeline& pipeline, CefRenderHandler::PaintElementType paintType)
    {
        auto* slot = backend.FindNewestCompleted(pipeline);
        if (!slot)
            return;

        const uint64_t           readbackStarted = m_bFrameStatsEnabled ? GetSteadyClockNanoseconds() : 0;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        backend.lastFailure = backend.deviceContext->Map(slot->stagingTexture, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (FAILED(backend.lastFailure) || !mapped.pData) [[unlikely]]
        {
            if (backend.lastFailure != DXGI_ERROR_WAS_STILL_DRAWING)
            {
                if (m_bFrameStatsEnabled)
                    m_FrameStats.acceleratedFailures.fetch_add(1, std::memory_order_relaxed);
                if (!backend.failureLogged)
                {
                    backend.failureLogged = true;
                    WriteDebugEvent(SString("[CEF ACCEL] Asynchronous readback failed (HRESULT=0x%08X)", static_cast<unsigned int>(backend.lastFailure)));
                }
                slot->pending = false;
            }
            return;
        }

        const int    width = slot->visibleRect.width;
        const int    height = slot->visibleRect.height;
        const size_t tightPitch = static_cast<size_t>(width) * CEF_PIXEL_STRIDE;
        const size_t requiredSize = tightPitch * static_cast<size_t>(height);
        backend.tightPixels.resize(requiredSize);

        const auto* sourceBytes = static_cast<const byte*>(mapped.pData) + static_cast<size_t>(slot->visibleRect.y) * mapped.RowPitch +
                                  static_cast<size_t>(slot->visibleRect.x) * CEF_PIXEL_STRIDE;
        for (int y = 0; y < height; ++y)
        {
            std::memcpy(backend.tightPixels.data() + static_cast<size_t>(y) * tightPitch, sourceBytes + static_cast<size_t>(y) * mapped.RowPitch, tightPitch);
        }
        backend.deviceContext->Unmap(slot->stagingTexture, 0);

        if (slot->format == CEF_COLOR_TYPE_RGBA_8888)
        {
            for (size_t offset = 0; offset < requiredSize; offset += CEF_PIXEL_STRIDE)
                std::swap(backend.tightPixels[offset], backend.tightPixels[offset + 2]);
        }

        slot->pending = false;
        if (m_bFrameStatsEnabled)
        {
            m_FrameStats.acceleratedReadbacks.fetch_add(1, std::memory_order_relaxed);
            m_FrameStats.acceleratedReadbackNanoseconds.fetch_add(GetSteadyClockNanoseconds() - readbackStarted, std::memory_order_relaxed);
        }

        OnPaint(m_pWebView, paintType, slot->dirtyRects, backend.tightPixels.data(), width, height);
    };

    ConsumePipeline(backend.viewPipeline, PET_VIEW);
    ConsumePipeline(backend.popupPipeline, PET_POPUP);
    m_bHasPendingAcceleratedPaint.store(backend.HasPendingFrames(), std::memory_order_release);
}

////////////////////////////////////////////////////////////////////
//                                                                //
// Implementation: CefLoadHandler::OnLoadStart                    //
// https://magpcss.org/ceforum/apidocs3/projects/(default)/CefLoadHandler.html#OnLoadStart(CefRefPtr%3CCefBrowser%3E,CefRefPtr%3CCefFrame%3E) //
//                                                                //
////////////////////////////////////////////////////////////////////
void CWebView::OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, TransitionType transitionType)
{
    // Note: TransitionType parameter is deprecated in CEF3 but still required by virtual interface override
    SString strURL = UTF16ToMbUTF8(frame->GetURL());
    if (strURL == "blank")
        return;

    // Queue event to run on the main thread
    QueueBrowserEvent("OnLoadStart",
                      [url = strURL, isMain = frame->IsMain()](CWebBrowserEventsInterface* iface) { iface->Events_OnLoadingStart(url, isMain); });
}

////////////////////////////////////////////////////////////////////
//                                                                //
// Implementation: CefLoadHandler::OnLoadEnd                      //
// https://magpcss.org/ceforum/apidocs3/projects/(default)/CefLoadHandler.html#OnLoadEnd(CefRefPtr%3CCefBrowser%3E,CefRefPtr%3CCefFrame%3E,int) //
//                                                                //
////////////////////////////////////////////////////////////////////
void CWebView::OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int httpStatusCode)
{
    // Set browser volume once again
    SetAudioVolume(m_fVolume);

    if (frame->IsMain())
    {
        SString strURL = UTF16ToMbUTF8(frame->GetURL());

        // Queue event to run on the main thread
        QueueBrowserEvent("OnLoadEnd", [url = strURL](CWebBrowserEventsInterface* iface) { iface->Events_OnDocumentReady(url); });
    }
}

////////////////////////////////////////////////////////////////////
//                                                                //
// Implementation: CefLoadHandler::OnLoadError                    //
// https://magpcss.org/ceforum/apidocs3/projects/(default)/CefLoadHandler.html#OnLoadError(CefRefPtr%3CCefBrowser%3E,CefRefPtr%3CCefFrame%3E,ErrorCode,constCefString&,constCefString&)
// //
//                                                                //
////////////////////////////////////////////////////////////////////
void CWebView::OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefLoadHandler::ErrorCode errorCode, const CefString& errorText,
                           const CefString& failedURL)
{
    SString strURL = UTF16ToMbUTF8(frame->GetURL());

    // Queue event to run on the main thread
    QueueBrowserEvent("OnLoadError", [url = strURL, errorCode, errorDescription = SString(errorText)](CWebBrowserEventsInterface* iface) mutable
                      { iface->Events_OnLoadingFailed(url, errorCode, errorDescription); });
}

////////////////////////////////////////////////////////////////////
//                                                                //
// Implementation: CefRequestHandler::OnBeforeBrowe               //
// https://magpcss.org/ceforum/apidocs3/projects/(default)/CefRequestHandler.html#OnBeforeBrowse(CefRefPtr%3CCefBrowser%3E,CefRefPtr%3CCefFrame%3E,CefRefPtr%3CCefRequest%3E,bool)
// //
//                                                                //
////////////////////////////////////////////////////////////////////
bool CWebView::OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, bool userGesture, bool isRedirect)
{
    /*
        From documentation:
        The |request| object cannot be modified in this callback.
        CefLoadHandler::OnLoadingStateChange will be called twice in all cases. If the navigation is allowed CefLoadHandler::OnLoadStart and
       CefLoadHandler::OnLoadEnd will be called. If the navigation is canceled CefLoadHandler::OnLoadError will be called with an |errorCode| value of
       ERR_ABORTED.
    */

    CefURLParts urlParts;
    if (!CefParseURL(request->GetURL(), urlParts))
        return true;  // Cancel if invalid URL (this line will normally not be executed)

    bool    bResult;
    WString scheme = urlParts.scheme.str;
    if (scheme == L"http" || scheme == L"https")
    {
        SString host = UTF16ToMbUTF8(urlParts.host.str);
        if (host != "mta")
        {
            if (IsLocal() || g_pCore->GetWebCore()->GetDomainState(host, true) != eURLState::WEBPAGE_ALLOWED)
                bResult = true;  // Block remote here
            else
                bResult = false;  // Allow
        }
        else
            bResult = false;
    }
    else
        bResult = true;  // Block other schemes

    // Check if we're in the browser's main frame or only a frame element of the current page
    bool bIsMainFrame = frame->IsMain();

    // Queue event to run on the main thread
    QueueBrowserEvent("OnNavigate", [url = SString(request->GetURL()), blocked = bResult, isMain = bIsMainFrame](CWebBrowserEventsInterface* iface) mutable
                      { iface->Events_OnNavigate(url, blocked, isMain); });

    // Return execution to CEF
    return bResult;
}

////////////////////////////////////////////////////////////////////
//                                                                //
// Implementation: CefRequestHandler::OnBeforeResourceLoad        //
// https://magpcss.org/ceforum/apidocs3/projects/(default)/CefRequestHandler.html#OnBeforeResourceLoad(CefRefPtr%3CCefBrowser%3E,CefRefPtr%3CCefFrame%3E,CefRefPtr%3CCefRequest%3E)
// //
//                                                                //
////////////////////////////////////////////////////////////////////
CefResourceRequestHandler::ReturnValue CWebView::OnBeforeResourceLoad(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request,
                                                                      CefRefPtr<CefCallback> callback)
{
    // Mostly the same as CWebView::OnBeforeBrowse
    CefURLParts urlParts;
    if (!CefParseURL(request->GetURL(), urlParts))
        return RV_CANCEL;  // Cancel if invalid URL (this line will normally not be executed)

    SString domain = UTF16ToMbUTF8(urlParts.host.str);

    // Add some information to the HTTP header
    {
        CefRequest::HeaderMap headerMap;
        request->GetHeaderMap(headerMap);
        auto iter = headerMap.find("User-Agent");

        if (iter != headerMap.end())
        {
            // Add MTA:SA "watermark"
            iter->second = iter->second.ToString() + "; " MTA_CEF_USERAGENT;

            // Add 'Android' to get the mobile version
            SString strPropertyValue;
            if (GetProperty("mobile", strPropertyValue) && strPropertyValue == "1")
                iter->second = iter->second.ToString() + "; Mobile Android";

            // Allow YouTube TV to work (#1162)
            if (domain == "www.youtube.com" && UTF16ToMbUTF8(urlParts.path.str) == "/tv")
                iter->second = iter->second.ToString() + "; SMART-TV; Tizen 4.0";

            request->SetHeaderMap(headerMap);
        }

        // Fix youtube embed (#4531)
        if (domain == "www.youtube.com" && UTF16ToMbUTF8(urlParts.path.str).find("/embed") == 0)
            request->SetReferrer("https://mtasa.com/", REFERRER_POLICY_ORIGIN);
    }

    WString scheme = urlParts.scheme.str;
    if (scheme == L"http" || scheme == L"https")
    {
        if (domain != "mta")
        {
            if (IsLocal())
                return RV_CANCEL;  // Block remote requests in local mode generally

            eURLState urlState = g_pCore->GetWebCore()->GetDomainState(domain, true);
            if (urlState != eURLState::WEBPAGE_ALLOWED)
            {
                // Trigger onClientBrowserResourceBlocked event
                QueueBrowserEvent(
                    "OnResourceBlocked",
                    [url = SString(request->GetURL()), domain, reason = static_cast<unsigned char>(urlState == eURLState::WEBPAGE_NOT_LISTED ? 0 : 1)](
                        CWebBrowserEventsInterface* iface) mutable { iface->Events_OnResourceBlocked(url, domain, reason); });

                return RV_CANCEL;  // Block if explicitly forbidden
            }

            // Allow
            return RV_CONTINUE;
        }
        else
            return RV_CONTINUE;
    }
    else if (scheme == L"blob")
    {
        return RV_CONTINUE;
    }

    // Trigger onClientBrowserResourceBlocked event
    QueueBrowserEvent("OnResourceBlocked",
                      [url = SString(request->GetURL())](CWebBrowserEventsInterface* iface) mutable { iface->Events_OnResourceBlocked(url, "", 2); });

    // Block everything else
    return RV_CANCEL;
}

////////////////////////////////////////////////////////////////////
//                                                                //
// Implementation: CefLifeSpanHandler::OnBeforeClose              //
// https://magpcss.org/ceforum/apidocs3/projects/(default)/CefLifeSpanHandler.html#OnBeforeClose(CefRefPtr%3CCefBrowser%3E) //
//                                                                //
////////////////////////////////////////////////////////////////////
void CWebView::OnBeforeClose(CefRefPtr<CefBrowser> browser)
{
    // Remove events owned by this webview and invoke left callbacks
    if (auto pWebCore = g_pCore->GetWebCore(); pWebCore) [[likely]]
    {
        pWebCore->RemoveWebViewEvents(this);

        // Remove focused web view reference
        if (pWebCore->GetFocusedWebView() == this)
            pWebCore->SetFocusedWebView(nullptr);
    }

    m_pWebView = nullptr;
}

////////////////////////////////////////////////////////////////////
//                                                                //
// Implementation: CefLifeSpanHandler::OnBeforePopup              //
// https://magpcss.org/ceforum/apidocs3/projects/(default)/CefLifeSpanHandler.html#OnBeforePopup(CefRefPtr%3CCefBrowser%3E,CefRefPtr%3CCefFrame%3E,constCefString&,constCefString&,constCefPopupFeatures&,CefWindowInfo&,CefRefPtr%3CCefClient%3E&,CefBrowserSettings&,bool*)
// //
//                                                                //
////////////////////////////////////////////////////////////////////
#ifdef MTA_MAETRO
bool CWebView::OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString& target_url, const CefString& target_frame_name,
                             CefLifeSpanHandler::WindowOpenDisposition target_disposition, bool user_gesture, const CefPopupFeatures& popupFeatures,
                             CefWindowInfo& windowInfo, CefRefPtr<CefClient>& client, CefBrowserSettings& settings, CefRefPtr<CefDictionaryValue>& extra_info,
                             bool* no_javascript_access)
#else
bool CWebView::OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int popup_id, const CefString& target_url,
                             const CefString& target_frame_name, CefLifeSpanHandler::WindowOpenDisposition target_disposition, bool user_gesture,
                             const CefPopupFeatures& popupFeatures, CefWindowInfo& windowInfo, CefRefPtr<CefClient>& client, CefBrowserSettings& settings,
                             CefRefPtr<CefDictionaryValue>& extra_info, bool* no_javascript_access)
#endif
{
    // ATTENTION: This method is called on the IO thread

    // Trigger the popup/new tab event
    SString strTagetURL = UTF16ToMbUTF8(target_url);
    SString strOpenerURL = UTF16ToMbUTF8(frame->GetURL());

    // Queue event to run on the main thread
    QueueBrowserEvent("OnBeforePopup",
                      [target = strTagetURL, opener = strOpenerURL](CWebBrowserEventsInterface* iface) { iface->Events_OnPopup(target, opener); });

    // Block popups generally
    return true;
}

////////////////////////////////////////////////////////////////////
//                                                                //
// Implementation: CefLifeSpanHandler::OnAfterCreated             //
// https://magpcss.org/ceforum/apidocs3/projects/(default)/CefLifeSpanHandler.html#OnAfterCreated(CefRefPtr<CefBrowser>) //
//                                                                //
////////////////////////////////////////////////////////////////////
void CWebView::OnAfterCreated(CefRefPtr<CefBrowser> browser)
{
    if (m_bBeingDestroyed)
    {
        browser->GetHost()->CloseBrowser(true);
        return;
    }

    // Set web view reference
    m_pWebView = browser;

    // Sync host visibility with the stored rendering state. This prevents
    // newly created browsers from becoming permanently hidden when pause
    // state changes race against async host creation.
    m_pWebView->GetHost()->WasHidden(m_bIsRenderingPaused);

    // Force an initial repaint to populate the texture even for pages that
    // become visually static immediately after load.
    m_pWebView->GetHost()->Invalidate(PET_VIEW);

    // If we have a pending URL from lazy loading, load it now
    if (!m_strPendingURL.empty())
    {
        SString pendingURL = m_strPendingURL;
        bool    filterEnabled = m_bPendingURLFilterEnabled;
        SString postData = m_strPendingPostData;
        bool    urlEncoded = m_bPendingURLEncoded;

        // Clear pending state before loading to prevent recursion
        m_strPendingURL.clear();
        m_strPendingPostData.clear();

        // Load the pending URL
        LoadURL(pendingURL, filterEnabled, postData, urlEncoded);
    }

    // Call created event callback
    QueueBrowserEvent("OnAfterCreated", [](CWebBrowserEventsInterface* iface) { iface->Events_OnCreated(); });
}

////////////////////////////////////////////////////////////////////
//                                                                //
// Implementation: CefJSDialogHandler::OnJSDialog                 //
// https://magpcss.org/ceforum/apidocs3/projects/(default)/CefJSDialogHandler.html#OnJSDialog(CefRefPtr%3CCefBrowser%3E,constCefString&,constCefString&,JSDialogType,constCefString&,constCefString&,CefRefPtr%3CCefJSDialogCallback%3E,bool&)
// //
//                                                                //
////////////////////////////////////////////////////////////////////
bool CWebView::OnJSDialog(CefRefPtr<CefBrowser> browser, const CefString& origin_url, CefJSDialogHandler::JSDialogType dialog_type,
                          const CefString& message_text, const CefString& default_prompt_text, CefRefPtr<CefJSDialogCallback> callback, bool& suppress_message)
{
    // TODO: Provide a way to influence Javascript dialogs via Lua
    // e.g. addEventHandler("onClientBrowserDialog", browser, function(message, defaultText) continueBrowserDialog("My input") end)

    // Suppress the dialog
    suppress_message = true;
    return false;
}

////////////////////////////////////////////////////////////////////
//                                                                //
// Implementation: CefDialogHandler::OnFileDialog                 //
// https://magpcss.org/ceforum/apidocs3/projects/(default)/CefDialogHandler.html#OnFileDialog(CefRefPtr%3CCefBrowser%3E,FileDialogMode,constCefString&,constCefString&,conststd::vector%3CCefString%3E&,CefRefPtr%3CCefFileDialogCallback%3E)
// //
//                                                                //
////////////////////////////////////////////////////////////////////
#ifdef MTA_MAETRO
bool CWebView::OnFileDialog(CefRefPtr<CefBrowser> browser, CefDialogHandler::FileDialogMode mode, const CefString& title, const CefString& default_file_path,
                            const std::vector<CefString>& accept_filters, CefRefPtr<CefFileDialogCallback> callback)
#else
bool CWebView::OnFileDialog(CefRefPtr<CefBrowser> browser, FileDialogMode mode, const CefString& title, const CefString& default_file_path,
                            const std::vector<CefString>& accept_filters, const std::vector<CefString>& accept_extensions,
                            const std::vector<CefString>& accept_descriptions, CefRefPtr<CefFileDialogCallback> callback)
#endif
{
    // Don't show the dialog
    return true;
}

////////////////////////////////////////////////////////////////////
//                                                                //
// Implementation: CefDisplayHandler::OnTitleChange               //
// https://magpcss.org/ceforum/apidocs3/projects/(default)/CefDisplayHandler.html#OnTitleChange(CefRefPtr%3CCefBrowser%3E,constCefString&) //
//                                                                //
////////////////////////////////////////////////////////////////////
void CWebView::OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title)
{
    m_CurrentTitle = UTF16ToMbUTF8(title);
}

////////////////////////////////////////////////////////////////////
//                                                                //
// Implementation: CefDisplayHandler::OnTooltip                   //
// https://magpcss.org/ceforum/apidocs3/projects/(default)/CefDisplayHandler.html#OnTooltip(CefRefPtr%3CCefBrowser%3E,CefString&) //
//                                                                //
////////////////////////////////////////////////////////////////////
bool CWebView::OnTooltip(CefRefPtr<CefBrowser> browser, CefString& title)
{
    // Queue event to run on the main thread
    QueueBrowserEvent("OnTooltip", [tooltip = UTF16ToMbUTF8(title)](CWebBrowserEventsInterface* iface) mutable { iface->Events_OnTooltip(tooltip); });

    return true;
}

////////////////////////////////////////////////////////////////////
//                                                                //
// Implementation: CefDisplayHandler::OnConsoleMessage            //
// https://magpcss.org/ceforum/apidocs/projects/%28default%29/CefDisplayHandler.html#OnConsoleMessage%28CefRefPtr%3CCefBrowser%3E,constCefString&,constCefString&,int%29
// //
//                                                                //
////////////////////////////////////////////////////////////////////
bool CWebView::OnConsoleMessage(CefRefPtr<CefBrowser> browser, cef_log_severity_t level, const CefString& message, const CefString& source, int line)
{
    const SString messageText = UTF16ToMbUTF8(message);
    if (m_bFrameStatsEnabled && messageText.BeginsWith("[SETTINGS PERF]"))
        WriteDebugEvent(messageText);

    // Note: cef_log_severity_t parameter is deprecated in CEF3 but required for virtual override
    // Redirect console message to debug window (if development mode is enabled)
    if (g_pCore->GetWebCore()->IsTestModeEnabled())
    {
        g_pCore->GetWebCore()->AddEventToEventQueue(
            [message, source]()
            { g_pCore->DebugPrintfColor("[BROWSER] Console: %s (%s)", 255, 0, 0, UTF16ToMbUTF8(message).c_str(), UTF16ToMbUTF8(source).c_str()); }, this,
            "OnConsoleMessage");
    }

    return true;
}

////////////////////////////////////////////////////////////////////
//                                                                //
// Implementation: CefDisplayHandler::OnCursorChange              //
// https://magpcss.org/ceforum/apidocs3/projects/(default)/CefRenderHandler.html#OnCursorChange(CefRefPtr%3CCefBrowser%3E,CefCursorHandle) //
//                                                                //
////////////////////////////////////////////////////////////////////
bool CWebView::OnCursorChange(CefRefPtr<CefBrowser> browser, CefCursorHandle cursor, cef_cursor_type_t type, const CefCursorInfo& cursorInfo)
{
    // Find the cursor index by the cursor handle
    unsigned char cursorIndex = static_cast<unsigned char>(type);

    // Queue event to run on the main thread
    QueueBrowserEvent("OnCursorChange", [cursorIndex](CWebBrowserEventsInterface* iface) { iface->Events_OnChangeCursor(cursorIndex); });

    return false;
}

////////////////////////////////////////////////////////////////////
//                                                                //
// Implementation: CefContextMenuHandler::OnBeforeContextMenu     //
// https://magpcss.org/ceforum/apidocs3/projects/(default)/CefContextMenuHandler.html#OnBeforeContextMenu(CefRefPtr%3CCefBrowser%3E,CefRefPtr%3CCefFrame%3E,CefRefPtr%3CCefContextMenuParams%3E,CefRefPtr%3CCefMenuModel%3E)
// //
//                                                                //
////////////////////////////////////////////////////////////////////
void CWebView::OnBeforeContextMenu(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefContextMenuParams> params,
                                   CefRefPtr<CefMenuModel> model)
{
    // Show no context menu
    model->Clear();
}
