/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/core/DevTools/CDevTools.h
 *  PURPOSE:     Core-owned structured script diagnostics console
 *
 *****************************************************************************/

#pragma once

#include "../CDebugEventStore.h"
#include <core/CWebBrowserEventsInterface.h>

class CWebViewInterface;
class CGUIWebBrowser;
class CAjaxResourceHandlerInterface;

class CDevTools final : public CWebBrowserEventsInterface
{
public:
    CDevTools();
    ~CDevTools();

    void Submit(const SDebugEvent& event);
    void SetVisible(bool visible);
    void Toggle() { SetVisible(!m_visible); }
    bool IsVisible() const noexcept { return m_visible; }
    void DoPulse();

    static bool HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void Events_OnCreated() override;
    void Events_OnLoadingStart(const SString& url, bool mainFrame) override;
    void Events_OnDocumentReady(const SString& url) override;
    void Events_OnLoadingFailed(const SString& url, int errorCode, const SString& errorDescription) override;
    void Events_OnNavigate(const SString& url, bool blocked, bool mainFrame) override;
    void Events_OnPopup(const SString& targetUrl, const SString& openerUrl) override;
    void Events_OnChangeCursor(unsigned char cursor) override;
    void Events_OnTriggerEvent(const SString& eventName, const std::vector<std::string>& arguments) override;
    void Events_OnTooltip(const SString& tooltip) override;
    void Events_OnInputFocusChanged(bool gainedFocus) override;
    bool Events_OnResourcePathCheck(SString& url) override;
    bool Events_OnResourceFileCheck(const SString& path, CBuffer& outFileData) override;
    void Events_OnResourceBlocked(const SString& url, const SString& domain, unsigned char reason) override;
    void Events_OnAjaxRequest(CAjaxResourceHandlerInterface* handler, const SString& url) override;
    void Events_OnConsoleMessage(const std::string& message, const std::string& source, int line, std::int16_t level) override;

private:
    bool        InitialiseWebView();
    void        SaveCursorPosition();
    void        RestoreCursorPosition();
    void        SendSnapshot();
    void        ExportCapture(const char* format);
    std::string BuildSnapshotJson() const;

    CDebugEventStore   m_store;
    CGUIWebBrowser*    m_widget{};
    CWebViewInterface* m_webView{};
    CVector2D          m_resolution;
    std::uint64_t      m_sentRevision{};
    unsigned int       m_initialisationDelayPulses{2};
    unsigned int       m_snapshotDelayPulses{};
    bool               m_documentReady{};
    bool               m_initialisationFailed{};
    bool               m_visible{};
    bool               m_surfaceVisible{};
    bool               m_ownsForcedCursor{};
    bool               m_hasCursorPosition{};
    bool               m_restorePreviousCursorPosition{};
    bool               m_toggleShortcutHeld{};
    bool               m_escapeClosingHeld{};
    POINT              m_cursorClientPosition{};
    POINT              m_previousScreenCursorPosition{};

    static CDevTools* ms_instance;
};
