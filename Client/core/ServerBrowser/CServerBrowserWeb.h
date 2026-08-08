/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        core/ServerBrowser/CServerBrowserWeb.h
 *  PURPOSE:     Neon main-menu web shell and native server-browser bridge
 *
 *****************************************************************************/

#pragma once

#include <core/CWebBrowserEventsInterface.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

class CMainMenu;
class CServerBrowser;
class CServerList;
class CServerListItem;
class CTextureItem;
class CWebBrowserItem;
class CWebViewInterface;
class CGUIWebBrowser;
class CAjaxResourceHandlerInterface;
class CNeonServerRegistry;

class CServerBrowserWeb final : public CWebBrowserEventsInterface
{
public:
    CServerBrowserWeb(CMainMenu& mainMenu, CServerBrowser& serverBrowser);
    ~CServerBrowserWeb();

    bool        Initialise();
    void        SetVisible(bool visible);
    void        SetNativeDialogVisible(bool visible);
    static bool HandleEscapeKey();
    static bool IsInputRoutedToWeb();
    static bool RouteInputMessage(UINT message, WPARAM wParam, LPARAM lParam);
    static bool CanHandleConnectionUi();
    static bool OwnsConnectionUi();
    static bool NotifyConnectionStarted(const std::string& host, unsigned short port);
    static bool NotifyConnectionProgress(const std::string& stage, const std::string& message);
    static bool NotifyConnectionFailed(const std::string& code, const std::string& message);
    static bool NotifyConnectionSucceeded();
    bool        IsAvailable() const { return m_webView != nullptr && m_widget != nullptr; }
    bool        DoPulse();
    void        DrawLoadingPlaceholder();

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
    enum class Source
    {
        Internet,
        Lan,
        Favourites,
        Recent,
    };

    void HandleMenuEvent(const SString& eventName, const std::vector<std::string>& arguments);
    void HandleServerBrowserEvent(const SString& eventName, const std::vector<std::string>& arguments);
    bool InitialiseWebView();
    bool CanHibernate() const;
    void CancelHibernateRequest();
    void QueueHibernateRequest();
    void UpdateRenderingPauseState();
    void PlayUiSound(const std::string& soundName);
    void QueueMenuInit();
    void QueueMenuContext(bool force);
    void QueueIdentity(bool force);
    void QueueFeaturedServer();
    void QueueFavourites();
    void QueueListReset();
    void QueueServer(const CServerListItem& server);
    void QueueEvent(const std::string& channel, const std::string& json);
    void QueueConnectionEvent(const std::string& json);
    void FlushEvents();
    void SelectSource(const std::string& sourceName, bool refresh);
    void RefreshCurrentSource();
    void SendSnapshot(bool forceAll);
    void SetFavourite(const std::string& host, unsigned short port, bool favourite);
    void Connect(const std::string& host, unsigned short port, const std::string& password);
    bool IsVisibleInCurrentSource(const CServerListItem& server) const;

    CServerList*       GetCurrentList() const;
    static const char* GetSourceName(Source source);
    bool               IsFavourite(const CServerListItem& server) const;

    CMainMenu&                           m_mainMenu;
    CServerBrowser&                      m_serverBrowser;
    std::unique_ptr<CNeonServerRegistry> m_registry;
    CTextureItem*                        m_loadingTexture{};
    CGUIWebBrowser*                      m_widget{};
    CWebViewInterface*                   m_webView{};
    unsigned int                         m_loadscreenIndex{1};
    unsigned int                         m_initialisationDelayPulses{2};
    bool                                 m_documentReady{};
    bool                                 m_visualReady{};
    bool                                 m_initialisationFailed{};
    bool                                 m_visible{};
    bool                                 m_nativeDialogVisible{};
    bool                                 m_serverBrowserReady{};
    bool                                 m_connectionUiActive{};
    bool                                 m_refreshing{};
    bool                                 m_sentRefreshFinished{};
    unsigned int                         m_hibernateGeneration{};
    unsigned int                         m_pendingHibernateGeneration{};
    unsigned int                         m_hibernatePauseDelayPulses{};
    Source                               m_source{Source::Internet};
    std::map<std::string, unsigned int>  m_sentRevisions;
    std::vector<std::string>             m_serverEvents;
    std::vector<std::string>             m_menuEvents;
    int                                  m_lastInGameContext{-1};
    std::string                          m_lastIdentitySignature;

    static CServerBrowserWeb* ms_instance;
};
