/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        core/ServerBrowser/CServerBrowserWeb.cpp
 *  PURPOSE:     Neon main-menu web shell and native server-browser bridge
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CServerBrowserWeb.h"

#include "CMainMenu.h"
#include "CNeonIdentityManager.h"
#include "CServerBrowser.h"
#include "CServerList.h"
#include <core/CAjaxResourceHandlerInterface.h>
#include <core/CWebCoreInterface.h>
#include <core/CWebViewInterface.h>
#include <game/CAudioEngine.h>
#include <gui/CGUIWebBrowser.h>
#include <json.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <set>
#include <utility>

namespace
{
    constexpr std::size_t MAX_EVENTS_PER_FRAME = 60;
    constexpr char        WEB_ROOT[] = "MTA\\cef\\serverbrowser";

    enum class EWebTranslationDomain
    {
        Client,
        MainMenu,
    };

    struct SWebTranslation
    {
        const char*           key;
        const char*           fallback;
        const char*           source;
        EWebTranslationDomain domain;
    };

    // The semantic key is stable for React while source can point at a legacy
    // MTA message. This lets the new shell reuse mature translations without
    // forcing its English art direction to copy old CEGUI wording verbatim.
    constexpr SWebTranslation WEB_TRANSLATIONS[] = {
        {"route.loadingServerBrowser", _td("Loading server browser"), _td("Loading server browser"), EWebTranslationDomain::Client},
        {"aria.mainMenu", _td("MTA Neon main menu"), _td("MTA Neon main menu"), EWebTranslationDomain::Client},
        {"aria.mainNavigation", _td("Main navigation"), _td("Main navigation"), EWebTranslationDomain::Client},
        {"aria.chooseLanguage", _td("Choose language"), _td("Choose language"), EWebTranslationDomain::Client},
        {"aria.serverList", _td("Server list"), _td("Server list"), EWebTranslationDomain::Client},
        {"main.discordConnecting", _td("Connecting Discord"), _td("Connecting Discord"), EWebTranslationDomain::Client},
        {"main.discordConnected", _td("Discord connected"), _td("Discord connected"), EWebTranslationDomain::Client},
        {"main.discordLink", _td("Link your Discord"), _td("Link your Discord"), EWebTranslationDomain::Client},
        {"main.discordSignOut", _td("Sign out"), _td("Sign out"), EWebTranslationDomain::Client},
        {"main.resumeGame", _td("Resume game"), _td("Resume game"), EWebTranslationDomain::Client},
        {"main.resumeCaption", _td("Return to the streets."), _td("Return to the streets."), EWebTranslationDomain::Client},
        {"main.settingsCaption", _td("Video, audio, controls and account preferences."), _td("Video, audio, controls and account preferences."),
         EWebTranslationDomain::Client},
        {"main.disconnectCaption", _td("Leave the current server and return to the main menu."), _td("Leave the current server and return to the main menu."),
         EWebTranslationDomain::Client},
        {"main.quitInGameCaption", _td("Leave the server and return to the desktop."), _td("Leave the server and return to the desktop."),
         EWebTranslationDomain::Client},
        {"main.browseServers", _td("Browse servers"), _td("Server browser"), EWebTranslationDomain::MainMenu},
        {"main.browseCaption", _td("Find a world and join the streets of San Andreas."), _td("Find a world and join the streets of San Andreas."),
         EWebTranslationDomain::Client},
        {"main.quickConnect", _td("Quick connect"), _td("Quick connect"), EWebTranslationDomain::MainMenu},
        {"main.quickConnectCaption", _td("Reconnect instantly or enter a server address."), _td("Reconnect instantly or enter a server address."),
         EWebTranslationDomain::Client},
        {"main.mapEditor", _td("Map editor"), _td("Map editor"), EWebTranslationDomain::MainMenu},
        {"main.mapEditorCaption", _td("Build your own corner of San Andreas."), _td("Build your own corner of San Andreas."), EWebTranslationDomain::Client},
        {"main.aboutCaption", _td("Credits, contributors and information about MTA Neon."), _td("Credits, contributors and information about MTA Neon."),
         EWebTranslationDomain::Client},
        {"main.quitGame", _td("Quit game"), _td("Quit"), EWebTranslationDomain::MainMenu},
        {"main.quitCaption", _td("Return to the desktop."), _td("Return to the desktop."), EWebTranslationDomain::Client},
        {"main.identity", _td("Neon Identity"), _td("Neon Identity"), EWebTranslationDomain::Client},
        {"main.inGame", _td("MTA Neon — In game"), _td("MTA Neon — In game"), EWebTranslationDomain::Client},
        {"main.preview", _td("v1.6 — Neon Preview"), _td("v1.6 — Neon Preview"), EWebTranslationDomain::Client},
        {"common.settings", _td("Settings"), _td("Settings"), EWebTranslationDomain::MainMenu},
        {"common.disconnect", _td("Disconnect"), _td("Disconnect"), EWebTranslationDomain::MainMenu},
        {"common.about", _td("About"), _td("About"), EWebTranslationDomain::MainMenu},
        {"common.cancel", _td("Cancel"), _td("Cancel"), EWebTranslationDomain::Client},
        {"common.connect", _td("Connect"), _td("Connect"), EWebTranslationDomain::Client},
        {"common.close", _td("Close"), _td("Close"), EWebTranslationDomain::Client},
        {"common.retry", _td("Retry"), _td("Retry"), EWebTranslationDomain::Client},
        {"common.resume", _td("Resume"), _td("Resume"), EWebTranslationDomain::Client},
        {"common.select", _td("Select"), _td("Select"), EWebTranslationDomain::Client},
        {"common.confirm", _td("Confirm"), _td("Confirm"), EWebTranslationDomain::Client},
        {"common.back", _td("Back"), _td("Back"), EWebTranslationDomain::Client},
        {"common.refresh", _td("Refresh"), _td("Refresh"), EWebTranslationDomain::Client},
        {"common.filters", _td("Filters"), _td("Filters"), EWebTranslationDomain::Client},
        {"common.players", _td("Players"), _td("Players"), EWebTranslationDomain::Client},
        {"common.ping", _td("Ping"), _td("Ping"), EWebTranslationDomain::Client},
        {"common.mode", _td("Mode"), _td("Gamemode"), EWebTranslationDomain::Client},
        {"common.navigate", _td("Navigate"), _td("Navigate"), EWebTranslationDomain::Client},
        {"source.neon", _td("Neon servers"), _td("Neon servers"), EWebTranslationDomain::Client},
        {"source.local", _td("Local"), _td("Local"), EWebTranslationDomain::Client},
        {"source.favourites", _td("Favourites"), _td("Favourites"), EWebTranslationDomain::Client},
        {"source.recent", _td("Recent"), _td("Recent"), EWebTranslationDomain::Client},
        {"browser.title", _td("Choose your server"), _td("Server browser"), EWebTranslationDomain::MainMenu},
        {"browser.destinationsCount", _td("{count} destinations"), _td("{count} destinations"), EWebTranslationDomain::Client},
        {"browser.playersOnlineCount", _td("{count} players online"), _td("{count} players online"), EWebTranslationDomain::Client},
        {"browser.backToMain", _td("Back to main menu"), _td("Back to main menu"), EWebTranslationDomain::Client},
        {"browser.searchPlaceholder", _td("Find a Neon server"), _td("Search servers..."), EWebTranslationDomain::Client},
        {"browser.heading.destinations", _td("Neon destinations"), _td("Neon destinations"), EWebTranslationDomain::Client},
        {"browser.filter.hideFull", _td("Hide full servers"), _td("Hide full servers"), EWebTranslationDomain::Client},
        {"browser.filter.hideEmpty", _td("Hide empty servers"), _td("Hide empty servers"), EWebTranslationDomain::Client},
        {"browser.filter.hideLocked", _td("Hide locked servers"), _td("Hide locked servers"), EWebTranslationDomain::Client},
        {"browser.filter.hideIncompatible", _td("Hide other versions"), _td("Hide other versions"), EWebTranslationDomain::Client},
        {"browser.filter.hideOffline", _td("Hide offline servers"), _td("Hide offline servers"), EWebTranslationDomain::Client},
        {"browser.empty", _td("No servers match your search."), _td("No servers match your search."), EWebTranslationDomain::Client},
        {"browser.emptyHint", _td("Try clearing filters or refreshing the list."), _td("Try clearing filters or refreshing the list."),
         EWebTranslationDomain::Client},
        {"server.passwordProtected", _td("Password protected"), _td("Password protected"), EWebTranslationDomain::Client},
        {"server.removeFavourite", _td("Remove from favourites"), _td("Remove from favourites"), EWebTranslationDomain::Client},
        {"server.addFavourite", _td("Add to favourites"), _td("Add Favorite"), EWebTranslationDomain::Client},
        {"server.playersUnverified", _td("Player count not verified"), _td("Player count not verified"), EWebTranslationDomain::Client},
        {"server.offline", _td("Offline"), _td("Offline"), EWebTranslationDomain::Client},
        {"details.selectServer", _td("Select a server to see"), _td("Select a server to see"), EWebTranslationDomain::Client},
        {"details.selectServerHint", _td("its details here."), _td("its details here."), EWebTranslationDomain::Client},
        {"details.selectedDestination", _td("Selected destination"), _td("Server information"), EWebTranslationDomain::Client},
        {"details.copyAddress", _td("Copy {address}"), _td("Copy {address}"), EWebTranslationDomain::Client},
        {"details.regionsLanguages", _td("Regions & languages"), _td("Regions & languages"), EWebTranslationDomain::Client},
        {"details.ready", _td("Selected server — ready to join"), _td("Selected server — ready to join"), EWebTranslationDomain::Client},
        {"details.joinServer", _td("Join server"), _td("Join Game"), EWebTranslationDomain::Client},
        {"modal.passwordRequired", _td("Password required"), _td("Password required"), EWebTranslationDomain::Client},
        {"modal.connectionKicker", _td("Neon network // connection"), _td("Neon network // connection"), EWebTranslationDomain::Client},
        {"modal.restrictedServer", _td("Restricted server"), _td("Restricted server"), EWebTranslationDomain::Client},
        {"modal.thisServer", _td("This server"), _td("This server"), EWebTranslationDomain::Client},
        {"modal.protectedServer", _td("{server} is protected. Enter the server password to join."),
         _td("{server} is protected. Enter the server password to join."), EWebTranslationDomain::Client},
        {"modal.serverPassword", _td("Server password"), _td("Server password"), EWebTranslationDomain::Client},
        {"modal.connecting", _td("Connecting…"), _td("Connecting…"), EWebTranslationDomain::Client},
        {"modal.joining", _td("Joining {server}"), _td("Joining {server}"), EWebTranslationDomain::Client},
        {"modal.enteringSanAndreas", _td("Entering San Andreas"), _td("Entering San Andreas"), EWebTranslationDomain::Client},
        {"modal.contactingServer", _td("Contacting the server and checking its version…"), _td("Contacting the server and checking its version…"),
         EWebTranslationDomain::Client},
        {"modal.authorizingIdentity", _td("Neon Identity"), _td("Neon Identity"), EWebTranslationDomain::Client},
        {"modal.authorizingHint", _td("Authorizing this server with your linked Discord identity…"),
         _td("Authorizing this server with your linked Discord identity…"), EWebTranslationDomain::Client},
        {"modal.connectionAccepted", _td("Welcome to the streets"), _td("Welcome to the streets"), EWebTranslationDomain::Client},
        {"modal.enteringGame", _td("Connection accepted. Preparing the game…"), _td("Connection accepted. Preparing the game…"), EWebTranslationDomain::Client},
        {"modal.connectionFailed", _td("Connection failed"), _td("Connection failed"), EWebTranslationDomain::Client},
        {"modal.unknownError", _td("Unknown error."), _td("Unknown error."), EWebTranslationDomain::Client},
        {"modal.serverFull", _td("Server is full"), _td("Server is full"), EWebTranslationDomain::Client},
        {"modal.serverFullHint", _td("No free slot is available. Try again in a moment."), _td("No free slot is available. Try again in a moment."),
         EWebTranslationDomain::Client},
        {"modal.connectionTimedOut", _td("No answer from the streets"), _td("No answer from the streets"), EWebTranslationDomain::Client},
        {"modal.connectionTimedOutHint", _td("The server did not answer in time. Check your connection and try again."),
         _td("The server did not answer in time. Check your connection and try again."), EWebTranslationDomain::Client},
        {"modal.passwordRejected", _td("Wrong password"), _td("Wrong password"), EWebTranslationDomain::Client},
        {"modal.passwordRejectedHint", _td("Enter the server password again."), _td("Enter the server password again."), EWebTranslationDomain::Client},
        {"modal.identityRequired", _td("Neon Identity required"), _td("Neon Identity required"), EWebTranslationDomain::Client},
        {"modal.identityRequiredHint", _td("Return to the main menu and link Discord before joining this server."),
         _td("Return to the main menu and link Discord before joining this server."), EWebTranslationDomain::Client},
        {"modal.identityFailed", _td("Identity check failed"), _td("Identity check failed"), EWebTranslationDomain::Client},
        {"modal.identityFailedHint", _td("Neon could not authorize this connection. Try again in a moment."),
         _td("Neon could not authorize this connection. Try again in a moment."), EWebTranslationDomain::Client},
        {"modal.versionMismatch", _td("Different game version"), _td("Different game version"), EWebTranslationDomain::Client},
        {"modal.versionMismatchHint", _td("This server requires another MTA version."), _td("This server requires another MTA version."),
         EWebTranslationDomain::Client},
        {"modal.connectionDenied", _td("Access denied"), _td("Access denied"), EWebTranslationDomain::Client},
        {"modal.connectionDeniedHint", _td("This server refused the connection."), _td("This server refused the connection."), EWebTranslationDomain::Client},
        {"modal.serverError", _td("Server response error"), _td("Server response error"), EWebTranslationDomain::Client},
        {"modal.serverErrorHint", _td("The server returned data this client cannot use."), _td("The server returned data this client cannot use."),
         EWebTranslationDomain::Client},
        {"modal.playerNameInvalid", _td("Player name invalid"), _td("Player name invalid"), EWebTranslationDomain::Client},
        {"modal.playerNameInvalidHint", _td("Change your nickname in Settings before connecting."), _td("Change your nickname in Settings before connecting."),
         EWebTranslationDomain::Client},
        {"modal.connectionLost", _td("Connection lost"), _td("Connection lost"), EWebTranslationDomain::Client},
        {"modal.connectionLostHint", _td("The server closed the connection. You can try again."), _td("The server closed the connection. You can try again."),
         EWebTranslationDomain::Client},
        {"modal.playersCount", _td("Players — {count}"), _td("Players — {count}"), EWebTranslationDomain::Client},
        {"status.registeredServers", _td("{count} registered servers"), _td("{count} registered servers"), EWebTranslationDomain::Client},
        {"status.playersOnline", _td("{count} players online."), _td("{count} players online."), EWebTranslationDomain::Client},
        {"status.scanning", _td("Scanning {scanned} / {total}…"), _td("Scanning {scanned} / {total}…"), EWebTranslationDomain::Client},
        {"status.joinServer", _td("Join server"), _td("Join Game"), EWebTranslationDomain::Client},
    };

    struct JsonDeleter
    {
        void operator()(json_object* object) const
        {
            if (object)
                json_object_put(object);
        }
    };

    using JsonPtr = std::unique_ptr<json_object, JsonDeleter>;

    JsonPtr MakeObject()
    {
        return JsonPtr(json_object_new_object());
    }

    std::string ToJson(json_object* object)
    {
        return object ? json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN) : "{}";
    }

    void AddString(json_object* object, const char* name, const std::string& value)
    {
        json_object_object_add(object, name, json_object_new_string_len(value.data(), static_cast<int>(value.size())));
    }

    void AddInteger(json_object* object, const char* name, std::int64_t value)
    {
        json_object_object_add(object, name, json_object_new_int64(value));
    }

    void AddBoolean(json_object* object, const char* name, bool value)
    {
        json_object_object_add(object, name, json_object_new_boolean(value));
    }

    void AddWebTranslations(json_object* init)
    {
        json_object* translations = json_object_new_object();
        for (const SWebTranslation& entry : WEB_TRANSLATIONS)
        {
            SString translated = entry.domain == EWebTranslationDomain::MainMenu ? g_pLocalization->TranslateInDomain("main_menu", entry.source)
                                                                                 : g_pLocalization->Translate(entry.source);

            // Some semantic keys deliberately have newer English copy than
            // the legacy msgid. Use that copy only when the selected catalogue
            // had no translation for the legacy source.
            if (translated.empty() || (translated == entry.source && std::strcmp(entry.source, entry.fallback) != 0))
                translated = entry.fallback;
            AddString(translations, entry.key, translated);
        }
        json_object_object_add(init, "translations", translations);
    }

    std::string TranslateIdentityStatus(const std::string& status)
    {
        if (status == "Not signed in")
            return _("Not signed in");
        if (status == "Neon account connected")
            return _("Neon account connected");
        if (status == "Opening Discord...")
            return _("Opening Discord...");
        if (status == "Complete sign-in in your browser")
            return _("Complete sign-in in your browser");
        if (status == "Discord sign-in failed")
            return _("Discord sign-in failed");
        return status;
    }

    unsigned short ParsePort(const std::string& text)
    {
        if (text.empty() || text.size() > 5 || !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
            return 0;

        const unsigned long value = std::strtoul(text.c_str(), nullptr, 10);
        return value > 0 && value <= std::numeric_limits<unsigned short>::max() ? static_cast<unsigned short>(value) : 0;
    }

    bool IsSafeRelativePath(const SString& path)
    {
        if (path.empty() || path.front() == '/' || path.front() == '\\' || path.find("..") != std::string::npos || path.find(':') != std::string::npos)
            return false;

        return std::all_of(path.begin(), path.end(),
                           [](unsigned char c) { return std::isalnum(c) || c == '/' || c == '\\' || c == '.' || c == '-' || c == '_'; });
    }

    constexpr std::size_t MAX_REGISTRY_RESPONSE_BYTES = 256 * 1024;
    constexpr std::size_t MAX_REGISTRY_SERVERS = 256;
    constexpr std::size_t MAX_REGISTRY_ARTWORK_BYTES = 2 * 1024 * 1024;

    bool ReadJsonString(json_object* object, const char* name, std::string& value, std::size_t maxLength, bool allowEmpty = false)
    {
        json_object* field = nullptr;
        if (!object || !json_object_object_get_ex(object, name, &field) || !field || json_object_get_type(field) != json_type_string)
            return false;

        const char* text = json_object_get_string(field);
        const int   length = json_object_get_string_len(field);
        if (!text || length < 0 || (!allowEmpty && length == 0) || static_cast<std::size_t>(length) > maxLength)
            return false;

        value.assign(text, static_cast<std::size_t>(length));
        return true;
    }

    bool ReadJsonStringArray(json_object* object, const char* name, std::vector<std::string>& values, std::size_t maxItems, std::size_t maxLength)
    {
        json_object* array = nullptr;
        if (!object || !json_object_object_get_ex(object, name, &array) || !array || json_object_get_type(array) != json_type_array)
            return false;

        const std::size_t count = json_object_array_length(array);
        if (count > maxItems)
            return false;

        std::set<std::string> unique;
        for (std::size_t i = 0; i < count; ++i)
        {
            json_object* item = json_object_array_get_idx(array, i);
            if (!item || json_object_get_type(item) != json_type_string)
                return false;

            const char* text = json_object_get_string(item);
            const int   length = json_object_get_string_len(item);
            if (!text || length <= 0 || static_cast<std::size_t>(length) > maxLength)
                return false;

            std::string value(text, static_cast<std::size_t>(length));
            if (!unique.emplace(value).second)
                return false;
            values.emplace_back(std::move(value));
        }
        return true;
    }

    bool IsValidServerId(const std::string& value)
    {
        if (value.empty() || value.size() > 128 || !std::isalnum(static_cast<unsigned char>(value.front())))
            return false;

        return std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isalnum(c) || c == '.' || c == '_' || c == ':' || c == '-'; });
    }

    bool IsKnownLinkKind(const std::string& value)
    {
        static const std::set<std::string> kinds{"website", "discord", "instagram", "x", "facebook", "vk", "youtube", "tiktok"};
        return kinds.contains(value);
    }

    bool ParseCanonicalEndpoint(const std::string& endpoint, in_addr& address, unsigned short& port)
    {
        const std::size_t separator = endpoint.rfind(':');
        if (separator == std::string::npos)
            return false;

        const std::string host = endpoint.substr(0, separator);
        port = ParsePort(endpoint.substr(separator + 1));
        if (!port || !CServerListItem::Parse(host.c_str(), address))
            return false;

        const char* canonical = inet_ntoa(address);
        return canonical && host == canonical;
    }

    bool IsAcceptedRegistryBaseUrl(const std::string& url)
    {
        if (url.rfind("https://", 0) == 0)
            return true;

        return url.rfind("http://127.0.0.1", 0) == 0 || url.rfind("http://localhost", 0) == 0;
    }

    std::string GetRegistryBaseUrl()
    {
        std::string baseUrl = CVARS_GET_VALUE<std::string>("neon_identity_url");
        while (!baseUrl.empty() && baseUrl.back() == '/')
            baseUrl.pop_back();
        return baseUrl;
    }

    SString GetRegistryArtworkCacheRoot()
    {
        const SString relativePath = g_pCore ? g_pCore->GetClientProfilePath("mta/cache/neon/server-artwork") : SStringX("mta/cache/neon/server-artwork");
        return CalcMTASAPath(relativePath);
    }

    bool ExtractRegistryArtworkHash(const std::string& url, std::string& hash)
    {
        const std::string baseUrl = GetRegistryBaseUrl();
        if (!IsAcceptedRegistryBaseUrl(baseUrl))
            return false;

        const std::string prefix = baseUrl + "/v1/server-registry/assets/";
        if (url.size() != prefix.size() + 64 || url.compare(0, prefix.size(), prefix) != 0)
            return false;

        hash = url.substr(prefix.size());
        return std::all_of(hash.begin(), hash.end(), [](unsigned char c) { return std::isdigit(c) || (c >= 'a' && c <= 'f'); });
    }

    bool ResolveRegistryArtworkRequest(const SString& requestPath, SString& fullPath)
    {
        std::string normalized = requestPath;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');

        constexpr char prefix[] = "registry-assets/";
        if (normalized.rfind(prefix, 0) != 0)
            return false;

        const std::string fileName = normalized.substr(sizeof(prefix) - 1);
        if (fileName.size() < 68 || fileName[64] != '.' || fileName.find('/', 0) != std::string::npos)
            return false;

        const std::string hash = fileName.substr(0, 64);
        const std::string extension = fileName.substr(65);
        if (!std::all_of(hash.begin(), hash.end(), [](unsigned char c) { return std::isdigit(c) || (c >= 'a' && c <= 'f'); }) ||
            (extension != "png" && extension != "jpg" && extension != "webp"))
            return false;

        fullPath = PathJoin(GetRegistryArtworkCacheRoot(), fileName.c_str());
        return true;
    }

    const char* DetectRegistryArtworkExtension(const char* data, std::size_t size)
    {
        const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data);
        if (size >= 8 && std::memcmp(bytes, "\x89PNG\r\n\x1a\n", 8) == 0)
            return "png";
        if (size >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF)
            return "jpg";
        if (size >= 12 && std::memcmp(bytes, "RIFF", 4) == 0 && std::memcmp(bytes + 8, "WEBP", 4) == 0)
            return "webp";
        return nullptr;
    }

    bool FindCachedRegistryArtwork(const std::string& hash, std::string& localUrl)
    {
        for (const char* extension : {"png", "jpg", "webp"})
        {
            const std::string fileName = hash + "." + extension;
            if (!FileExists(PathJoin(GetRegistryArtworkCacheRoot(), fileName.c_str())))
                continue;

            localUrl = "http://mta/local/registry-assets/" + fileName;
            return true;
        }
        return false;
    }
}

struct SNeonServerLink
{
    std::string kind;
    std::string label;
    std::string url;
};

struct SNeonServerMetadata
{
    std::string                  serverId;
    std::string                  name;
    std::string                  tagline;
    std::string                  description;
    std::string                  accent;
    std::string                  logoSourceUrl;
    std::string                  bannerSourceUrl;
    std::string                  logoUrl;
    std::string                  bannerUrl;
    std::vector<std::string>     countries;
    std::vector<std::string>     languages;
    std::vector<SNeonServerLink> links;
};

class CNeonServerRegistry
{
public:
    ~CNeonServerRegistry()
    {
        CNet*                             network = g_pCore ? g_pCore->GetNetwork() : nullptr;
        CNetHTTPDownloadManagerInterface* http = network ? network->GetHTTPDownloadManager(EDownloadMode::WEBBROWSER_LISTS) : nullptr;
        if (http && m_requestPending)
            http->CancelDownload(this, StaticDownloadFinished);
        if (http && m_artworkRequestPending)
            http->CancelDownload(this, StaticArtworkDownloadFinished);
    }

    void Start()
    {
        if (m_started)
            return;

        m_started = true;
        std::vector<char> cached;
        if (FileLoad(GetCachePath(), cached, static_cast<int>(MAX_REGISTRY_RESPONSE_BYTES)) && !cached.empty())
            ApplyManifest(cached.data(), cached.size());
        RefreshRemote();
    }

    void DoPulse()
    {
        CNet* network = g_pCore ? g_pCore->GetNetwork() : nullptr;
        if (!network)
            return;

        if (CNetHTTPDownloadManagerInterface* http = network->GetHTTPDownloadManager(EDownloadMode::WEBBROWSER_LISTS))
            http->ProcessQueuedFiles();
    }

    void RefreshRemote()
    {
        if (m_requestPending)
            return;

        const std::string baseUrl = GetRegistryBaseUrl();
        if (!IsAcceptedRegistryBaseUrl(baseUrl))
        {
            SetError("The Neon registry URL must use HTTPS");
            return;
        }

        CNet*                             network = g_pCore ? g_pCore->GetNetwork() : nullptr;
        CNetHTTPDownloadManagerInterface* http = network ? network->GetHTTPDownloadManager(EDownloadMode::WEBBROWSER_LISTS) : nullptr;
        if (!http)
        {
            SetError("The Neon registry network service is unavailable");
            return;
        }

        SHttpRequestOptions options;
        options.strRequestMethod = "GET";
        options.uiConnectionAttempts = 1;
        options.uiConnectTimeoutMs = 10000;
        options.uiMaxRedirects = 0;
        options.requestHeaders["Accept"] = "application/json";
        if (!http->QueueFile((baseUrl + "/.well-known/neon-server-registry").c_str(), nullptr, this, StaticDownloadFinished, options))
        {
            SetError("The Neon server registry request could not be started");
            return;
        }

        m_requestPending = true;
    }

    CServerList* GetList() { return &m_servers; }

    CServerListItem* Find(const std::string& host, unsigned short port)
    {
        for (auto it = m_servers.IteratorBegin(); it != m_servers.IteratorEnd(); ++it)
        {
            CServerListItem* server = *it;
            if (server && server->usGamePort == port && server->strHost == host)
                return server;
        }
        return nullptr;
    }

    const SNeonServerMetadata* FindMetadata(const CServerListItem& server) const
    {
        auto found = m_metadata.find(server.GetEndpoint());
        return found != m_metadata.end() ? &found->second : nullptr;
    }

    bool Contains(const CServerListItem& server) const { return FindMetadata(server) != nullptr; }

    bool Contains(const std::string& endpoint) const { return m_metadata.contains(endpoint); }

    bool ConsumeChanged()
    {
        const bool changed = m_changed;
        m_changed = false;
        return changed;
    }

    std::string ConsumeError() { return std::exchange(m_error, std::string{}); }

private:
    struct SParsedEndpoint
    {
        std::string         endpoint;
        in_addr             address{};
        unsigned short      port{};
        SNeonServerMetadata metadata;
    };

    static void StaticDownloadFinished(const SHttpDownloadResult& result)
    {
        if (result.pObj)
            static_cast<CNeonServerRegistry*>(result.pObj)->DownloadFinished(result);
    }

    void DownloadFinished(const SHttpDownloadResult& result)
    {
        m_requestPending = false;
        if (!result.bSuccess || result.iErrorCode < 200 || result.iErrorCode >= 300 || !result.pData || result.dataSize == 0 ||
            result.dataSize > MAX_REGISTRY_RESPONSE_BYTES)
        {
            SetError("The Neon server registry is temporarily unavailable");
            return;
        }

        if (!ApplyManifest(result.pData, result.dataSize))
        {
            SetError("The Neon server registry returned invalid data");
            return;
        }

        FileSave(GetCachePath(), result.pData, static_cast<unsigned long>(result.dataSize));
    }

    bool ApplyManifest(const char* data, std::size_t size)
    {
        if (!data || size == 0 || size > MAX_REGISTRY_RESPONSE_BYTES)
            return false;

        std::vector<SParsedEndpoint> parsedEndpoints;
        if (!ParseManifest(data, size, parsedEndpoints))
            return false;

        const std::string payload(data, size);
        if (payload == m_manifestPayload)
        {
            RefreshArtworkPaths();
            return true;
        }

        m_servers.SuspendActivity();
        m_servers.Clear();
        m_metadata.clear();
        for (SParsedEndpoint& parsed : parsedEndpoints)
        {
            m_servers.AddUnique(parsed.address, parsed.port);
            m_metadata.emplace(parsed.endpoint, std::move(parsed.metadata));
        }
        RefreshArtworkPaths();
        m_servers.Refresh();
        m_manifestPayload = payload;
        m_changed = true;
        m_error.clear();
        return true;
    }

    bool ParseManifest(const char* data, std::size_t size, std::vector<SParsedEndpoint>& parsedEndpoints) const
    {
        json_tokener* tokener = json_tokener_new();
        if (!tokener)
            return false;

        json_object* root = json_tokener_parse_ex(tokener, data, static_cast<int>(size));
        const bool   parsed = json_tokener_get_error(tokener) == json_tokener_success && root && json_tokener_get_parse_end(tokener) == size;
        json_tokener_free(tokener);
        JsonPtr document(root);
        if (!parsed || json_object_get_type(root) != json_type_object)
            return false;

        json_object* version = nullptr;
        json_object* servers = nullptr;
        if (!json_object_object_get_ex(root, "schema_version", &version) || !version || json_object_get_type(version) != json_type_int ||
            json_object_get_int(version) != 1 || !json_object_object_get_ex(root, "servers", &servers) || !servers ||
            json_object_get_type(servers) != json_type_array)
            return false;

        const std::size_t serverCount = json_object_array_length(servers);
        if (serverCount > MAX_REGISTRY_SERVERS)
            return false;

        std::set<std::string> serverIds;
        std::set<std::string> endpoints;
        for (std::size_t serverIndex = 0; serverIndex < serverCount; ++serverIndex)
        {
            json_object* server = json_object_array_get_idx(servers, serverIndex);
            if (!server || json_object_get_type(server) != json_type_object)
                return false;

            SNeonServerMetadata metadata;
            if (!ReadJsonString(server, "id", metadata.serverId, 128) || !IsValidServerId(metadata.serverId) || !serverIds.emplace(metadata.serverId).second ||
                !ReadJsonString(server, "name", metadata.name, 128) || !ReadJsonString(server, "tagline", metadata.tagline, 256) ||
                !ReadJsonString(server, "description", metadata.description, 2048) || !ReadJsonStringArray(server, "countries", metadata.countries, 16, 2) ||
                !ReadJsonStringArray(server, "languages", metadata.languages, 16, 64))
                return false;

            json_object* accent = nullptr;
            if (json_object_object_get_ex(server, "accent", &accent))
            {
                if (!ReadJsonString(server, "accent", metadata.accent, 7) || metadata.accent.size() != 7 || metadata.accent.front() != '#' ||
                    !std::all_of(metadata.accent.begin() + 1, metadata.accent.end(), [](unsigned char c) { return std::isxdigit(c) != 0; }))
                    return false;
            }

            const auto readOptionalArtworkUrl = [server](const char* name, std::string& value)
            {
                json_object* field = nullptr;
                if (!json_object_object_get_ex(server, name, &field))
                    return true;

                // Artwork must come from the registry's HTTPS cache, never
                // directly from an arbitrary game server or local address.
                return ReadJsonString(server, name, value, 2048) && value.rfind("https://", 0) == 0;
            };
            if (!readOptionalArtworkUrl("logo_url", metadata.logoSourceUrl) || !readOptionalArtworkUrl("banner_url", metadata.bannerSourceUrl))
                return false;

            for (std::string& country : metadata.countries)
            {
                if (country.size() != 2 || !std::all_of(country.begin(), country.end(), [](unsigned char c) { return std::isalpha(c) != 0; }))
                    return false;
                std::transform(country.begin(), country.end(), country.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            }

            json_object* links = nullptr;
            if (!json_object_object_get_ex(server, "links", &links) || !links || json_object_get_type(links) != json_type_array ||
                json_object_array_length(links) > 12)
                return false;
            for (std::size_t linkIndex = 0; linkIndex < json_object_array_length(links); ++linkIndex)
            {
                json_object*    link = json_object_array_get_idx(links, linkIndex);
                SNeonServerLink parsedLink;
                if (!link || json_object_get_type(link) != json_type_object || !ReadJsonString(link, "kind", parsedLink.kind, 16) ||
                    !IsKnownLinkKind(parsedLink.kind) || !ReadJsonString(link, "label", parsedLink.label, 96) ||
                    !ReadJsonString(link, "url", parsedLink.url, 2048) || parsedLink.url.rfind("https://", 0) != 0)
                    return false;
                metadata.links.emplace_back(std::move(parsedLink));
            }

            std::vector<std::string> serverEndpoints;
            if (!ReadJsonStringArray(server, "endpoints", serverEndpoints, 16, 64) || serverEndpoints.empty())
                return false;
            for (const std::string& endpoint : serverEndpoints)
            {
                SParsedEndpoint parsedEndpoint;
                parsedEndpoint.endpoint = endpoint;
                parsedEndpoint.metadata = metadata;
                if (!endpoints.emplace(endpoint).second || !ParseCanonicalEndpoint(endpoint, parsedEndpoint.address, parsedEndpoint.port))
                    return false;
                parsedEndpoints.emplace_back(std::move(parsedEndpoint));
            }
        }
        // An empty, valid catalogue is meaningful: all previous heartbeats may
        // have expired, so retaining cached entries would show offline servers.
        return true;
    }

    void SetError(const std::string& error)
    {
        if (m_metadata.empty())
            m_error = error;
        WriteDebugEvent(SString("Neon server registry: %s", error.c_str()));
    }

    static void StaticArtworkDownloadFinished(const SHttpDownloadResult& result)
    {
        if (result.pObj)
            static_cast<CNeonServerRegistry*>(result.pObj)->ArtworkDownloadFinished(result);
    }

    void RefreshArtworkPaths()
    {
        for (auto& [endpoint, metadata] : m_metadata)
        {
            const auto resolveArtwork = [this](const std::string& sourceUrl, std::string& localUrl)
            {
                localUrl.clear();
                if (sourceUrl.empty())
                    return;

                std::string hash;
                if (!ExtractRegistryArtworkHash(sourceUrl, hash))
                    return;

                if (FindCachedRegistryArtwork(hash, localUrl))
                    return;

                if (m_knownArtworkUrls.emplace(sourceUrl).second)
                    m_pendingArtworkUrls.emplace_back(sourceUrl);
            };

            resolveArtwork(metadata.logoSourceUrl, metadata.logoUrl);
            resolveArtwork(metadata.bannerSourceUrl, metadata.bannerUrl);
        }
        StartNextArtworkDownload();
    }

    void StartNextArtworkDownload()
    {
        if (m_artworkRequestPending || m_pendingArtworkUrls.empty())
            return;

        CNet*                             network = g_pCore ? g_pCore->GetNetwork() : nullptr;
        CNetHTTPDownloadManagerInterface* http = network ? network->GetHTTPDownloadManager(EDownloadMode::WEBBROWSER_LISTS) : nullptr;
        if (!http)
            return;

        m_activeArtworkUrl = std::move(m_pendingArtworkUrls.front());
        m_pendingArtworkUrls.erase(m_pendingArtworkUrls.begin());

        SHttpRequestOptions options;
        options.strRequestMethod = "GET";
        options.uiConnectionAttempts = 1;
        options.uiConnectTimeoutMs = 10000;
        options.uiMaxRedirects = 0;
        options.requestHeaders["Accept"] = "image/png,image/jpeg,image/webp";
        if (!http->QueueFile(m_activeArtworkUrl.c_str(), nullptr, this, StaticArtworkDownloadFinished, options))
        {
            m_knownArtworkUrls.erase(m_activeArtworkUrl);
            m_activeArtworkUrl.clear();
            StartNextArtworkDownload();
            return;
        }
        m_artworkRequestPending = true;
    }

    void ArtworkDownloadFinished(const SHttpDownloadResult& result)
    {
        const std::string sourceUrl = std::exchange(m_activeArtworkUrl, std::string{});
        m_artworkRequestPending = false;

        std::string hash;
        const char* extension = nullptr;
        const bool  valid = result.bSuccess && result.iErrorCode >= 200 && result.iErrorCode < 300 && result.pData && result.dataSize > 0 &&
                           result.dataSize <= MAX_REGISTRY_ARTWORK_BYTES && ExtractRegistryArtworkHash(sourceUrl, hash) &&
                           (extension = DetectRegistryArtworkExtension(result.pData, result.dataSize));

        if (!valid || !FileSave(PathJoin(GetRegistryArtworkCacheRoot(), (hash + "." + (extension ? extension : "")).c_str()), result.pData,
                                static_cast<unsigned long>(result.dataSize)))
        {
            m_knownArtworkUrls.erase(sourceUrl);
            StartNextArtworkDownload();
            return;
        }

        // The web view is intentionally local and may not load remote URLs.
        // Re-resolve the new file through mta/local, then refresh React without
        // weakening CEF's network isolation.
        RefreshArtworkPaths();
        m_changed = true;
    }

    std::string GetCachePath() const
    {
        const SString relativePath = g_pCore ? g_pCore->GetClientProfilePath("mta/config/neon_servers_v1.json") : SStringX("mta/config/neon_servers_v1.json");
        return CalcMTASAPath(relativePath);
    }

    CServerList                                m_servers;
    std::map<std::string, SNeonServerMetadata> m_metadata;
    std::string                                m_manifestPayload;
    std::string                                m_error;
    std::vector<std::string>                   m_pendingArtworkUrls;
    std::set<std::string>                      m_knownArtworkUrls;
    std::string                                m_activeArtworkUrl;
    bool                                       m_started{};
    bool                                       m_requestPending{};
    bool                                       m_artworkRequestPending{};
    bool                                       m_changed{};
};

CServerBrowserWeb* CServerBrowserWeb::ms_instance = nullptr;

CServerBrowserWeb::CServerBrowserWeb(CMainMenu& mainMenu, CServerBrowser& serverBrowser)
    : m_mainMenu(mainMenu), m_serverBrowser(serverBrowser), m_registry(std::make_unique<CNeonServerRegistry>())
{
    ms_instance = this;
}

CServerBrowserWeb::~CServerBrowserWeb()
{
    if (ms_instance == this)
        ms_instance = nullptr;

    if (m_widget)
    {
        g_pCore->GetGUI()->DestroyElementRecursive(m_widget);
        m_widget = nullptr;
    }

    SAFE_RELEASE(m_loadingTexture);

    if (m_webView)
    {
        m_webView->ClearWebBrowserEvents(this);
        if (CWebCoreInterface* webCore = g_pCore->GetWebCoreUnchecked())
            webCore->DestroyWebView(m_webView);
        m_webView = nullptr;
    }
}

bool CServerBrowserWeb::IsInputRoutedToWeb()
{
    if (!ms_instance || !ms_instance->m_visible || !ms_instance->m_visualReady || ms_instance->m_nativeDialogVisible || !ms_instance->m_widget ||
        !ms_instance->m_webView)
        return false;

    CWebCoreInterface* webCore = g_pCore->GetWebCoreUnchecked();
    if (!webCore)
        return false;

    // Query live native state as well as the frame-cached flag. A console or
    // modal can be opened from the Windows message loop between menu pulses.
    return !ms_instance->m_mainMenu.HasNativeInputOwner() && ms_instance->m_widget->IsVisible() && ms_instance->m_widget->IsEnabled() &&
           ms_instance->m_widget->IsActive() && webCore->GetFocusedWebView() == ms_instance->m_webView;
}

bool CServerBrowserWeb::HandleEscapeKey()
{
    if (!ms_instance || !ms_instance->m_visible || !ms_instance->m_documentReady || ms_instance->m_nativeDialogVisible || !ms_instance->m_webView)
        return false;

    // Escape is owned by the always-mounted menu shell while it is visible.
    // Dispatch it directly into the document instead of depending on CEGUI's
    // transient active/focused state, which can change after mouse input.
    ms_instance->m_webView->ExecuteJavascript("window.dispatchEvent(new KeyboardEvent('keydown',{key:'Escape',code:'Escape',bubbles:true}));");
    return true;
}

bool CServerBrowserWeb::CanHandleConnectionUi()
{
    // A connection can hide the menu before its asynchronous network states
    // arrive. Ownership therefore follows the live web document, not the
    // widget's transient visibility or the lazy Server Browser route.
    return ms_instance && ms_instance->m_documentReady;
}

bool CServerBrowserWeb::OwnsConnectionUi()
{
    return ms_instance && ms_instance->m_connectionUiActive;
}

bool CServerBrowserWeb::NotifyConnectionStarted(const std::string& host, unsigned short port)
{
    if (!CanHandleConnectionUi())
        return false;

    // A reconnect can start while the gameplay-owned menu is paused. Wake it
    // before publishing progress so Chromium can settle the next frozen frame.
    ms_instance->m_connectionUiActive = true;
    ms_instance->CancelHibernateRequest();
    ms_instance->UpdateRenderingPauseState();

    JsonPtr event = MakeObject();
    AddString(event.get(), "type", "connect-started");
    AddString(event.get(), "host", host);
    AddInteger(event.get(), "port", port);

    CServerListItem* server = ms_instance->m_registry ? ms_instance->m_registry->Find(host, port) : nullptr;
    if (!server)
        server = ms_instance->m_serverBrowser.FindServer(host, port);
    if (server)
    {
        const SNeonServerMetadata* metadata = ms_instance->m_registry ? ms_instance->m_registry->FindMetadata(*server) : nullptr;
        AddString(event.get(), "name", metadata ? metadata->name : server->strName);
    }

    ms_instance->QueueConnectionEvent(ToJson(event.get()));
    return true;
}

bool CServerBrowserWeb::NotifyConnectionProgress(const std::string& stage, const std::string& message)
{
    if (!CanHandleConnectionUi())
        return false;

    JsonPtr event = MakeObject();
    AddString(event.get(), "type", "connect-progress");
    AddString(event.get(), "stage", stage);
    AddString(event.get(), "message", message);
    ms_instance->QueueConnectionEvent(ToJson(event.get()));
    return true;
}

bool CServerBrowserWeb::NotifyConnectionFailed(const std::string& code, const std::string& message)
{
    if (!CanHandleConnectionUi())
    {
        if (ms_instance)
            ms_instance->m_connectionUiActive = false;
        return false;
    }

    JsonPtr event = MakeObject();
    AddString(event.get(), "type", "connect-failed");
    AddString(event.get(), "code", code);
    AddString(event.get(), "message", message);
    ms_instance->QueueConnectionEvent(ToJson(event.get()));
    ms_instance->m_connectionUiActive = false;
    return true;
}

bool CServerBrowserWeb::NotifyConnectionSucceeded()
{
    if (!CanHandleConnectionUi())
    {
        if (ms_instance)
            ms_instance->m_connectionUiActive = false;
        return false;
    }

    JsonPtr event = MakeObject();
    AddString(event.get(), "type", "connect-succeeded");
    ms_instance->QueueConnectionEvent(ToJson(event.get()));
    ms_instance->m_connectionUiActive = false;
    return true;
}

bool CServerBrowserWeb::RouteInputMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (!IsInputRoutedToWeb())
        return false;

    CWebCoreInterface* webCore = g_pCore->GetWebCoreUnchecked();
    if (!webCore)
        return false;

    // Focus is granted only by the CEGUI widget activation path. Input
    // routing must never steal it back from a native edit box or modal.
    webCore->ProcessInputMessage(message, wParam, lParam);
    return true;
}

bool CServerBrowserWeb::Initialise()
{
    // Prepare a native first frame before starting CEF. CefInitialize is
    // synchronous and can take several seconds on a cold start; deferring it
    // by two pulses lets this artwork reach the screen before that work blocks
    // the render thread.
    if (!FileExists(CalcMTASAPath(PathJoin(WEB_ROOT, "index.html"))))
        return false;

    m_loadscreenIndex = 1 + GetTickCount32() % 14;
    const SString loadscreenPath = CalcMTASAPath(SString("MTA\\cgui\\images\\loadscreens\\loadsc%u.jpg", m_loadscreenIndex));
    m_loadingTexture = g_pCore->GetGraphics()->GetRenderItemManager()->CreateTexture(loadscreenPath, nullptr, false, -1, -1, RFORMAT_UNKNOWN, TADDRESS_CLAMP);
    if (!m_loadingTexture)
        return false;
    return true;
}

void CServerBrowserWeb::DrawLoadingPlaceholder()
{
    if (!m_visible || m_visualReady || m_nativeDialogVisible || !m_loadingTexture || g_pCore->IsWindowMinimized())
        return;

    CGraphicsInterface* graphics = g_pCore->GetGraphics();
    const float         viewportWidth = static_cast<float>(graphics->GetViewportWidth());
    const float         viewportHeight = static_cast<float>(graphics->GetViewportHeight());
    const float         textureWidth = static_cast<float>(m_loadingTexture->m_uiSizeX);
    const float         textureHeight = static_cast<float>(m_loadingTexture->m_uiSizeY);
    if (viewportWidth < 1.0f || viewportHeight < 1.0f || textureWidth < 1.0f || textureHeight < 1.0f)
        return;

    constexpr float WEB_SCENE_SCALE = 1.02f;
    const float     coverScale = std::max(viewportWidth / textureWidth, viewportHeight / textureHeight);
    const float     coverWidth = textureWidth * coverScale;
    const float     coverHeight = textureHeight * coverScale;
    const float     coverX = (viewportWidth - coverWidth) * 0.35f;
    const float     coverY = (viewportHeight - coverHeight) * 0.5f;
    const float     sourceCenterX = (viewportWidth * 0.5f - coverX) / coverScale;
    const float     sourceCenterY = (viewportHeight * 0.5f - coverY) / coverScale;
    const float     sourceWidth = viewportWidth / (coverScale * WEB_SCENE_SCALE);
    const float     sourceHeight = viewportHeight / (coverScale * WEB_SCENE_SCALE);
    const float     u = (sourceCenterX - sourceWidth * 0.5f) / textureWidth;
    const float     v = (sourceCenterY - sourceHeight * 0.5f) / textureHeight;

    // CEGUI can keep a narrower logical root while a Parallels window is
    // resized. Draw against the real D3D viewport so no uncovered strip can
    // remain, while preserving the web menu's object-position and 2% zoom.
    graphics->DrawRectangle(0.0f, 0.0f, viewportWidth, viewportHeight, 0xFF020303);
    graphics->DrawTexture(m_loadingTexture, 0.0f, 0.0f, viewportWidth / textureWidth, viewportHeight / textureHeight, 0.0f, 0.0f, 0.0f, 0xFFFFFFFF, u, v,
                          sourceWidth / textureWidth, sourceHeight / textureHeight, true);

    // CefInitialize blocks the render thread on a cold start, so a smoothly
    // animated percentage would be misleading. Show genuine startup
    // milestones instead: native shell, Chromium created, then document
    // ready. The bar disappears only after React confirms that its artwork,
    // fonts and opening transition are fully painted.
    float progress = 0.18f;
    if (m_webView)
        progress = 0.62f;
    if (m_documentReady)
        progress = 0.88f;

    const float uiScale = Clamp(0.75f, std::min(viewportWidth / 1920.0f, viewportHeight / 1080.0f), 2.0f);
    const float barWidth = std::min(viewportWidth * 0.32f, 560.0f * uiScale);
    const float barHeight = std::max(5.0f, 7.0f * uiScale);
    const float barX = (viewportWidth - barWidth) * 0.5f;
    const float barY = viewportHeight - 48.0f * uiScale;
    graphics->DrawRectangle(barX - 2.0f, barY - 2.0f, barWidth + 4.0f, barHeight + 4.0f, 0xD8000000);
    graphics->DrawRectangle(barX, barY, barWidth, barHeight, 0xB04A5A6B);
    graphics->DrawRectangle(barX, barY, barWidth * progress, barHeight, 0xFFACCBF1);
}

bool CServerBrowserWeb::InitialiseWebView()
{
    CWebCoreInterface* webCore = g_pCore->GetWebCore();
    const CVector2D    resolution = g_pCore->GetGUI()->GetResolution();
    if (!webCore || resolution.fX < 1 || resolution.fY < 1)
        return false;

    CWebBrowserItem* renderItem =
        g_pCore->GetGraphics()->GetRenderItemManager()->CreateWebBrowser(static_cast<uint>(resolution.fX), static_cast<uint>(resolution.fY));
    if (!renderItem)
        return false;

    // The same document also serves as the in-game pause menu. Transparent
    // composition lets that route darken the live world instead of replacing
    // it with the launch artwork; the offline routes still paint opaque art.
    m_webView = webCore->CreateWebView(static_cast<uint>(resolution.fX), static_cast<uint>(resolution.fY), true, renderItem, true);
    renderItem->Release();
    if (!m_webView)
        return false;

    m_webView->SetWebBrowserEvents(this);
    m_webView->Initialise();

    m_widget = g_pCore->GetGUI()->CreateWebBrowser(static_cast<CGUIElement*>(nullptr));
    if (!m_widget)
        return false;

    m_widget->SetPosition(CVector2D(0, 0), false);
    m_widget->SetSize(resolution, false);
    m_widget->SetFrameEnabled(false);
    m_widget->LoadFromWebView(m_webView);
    m_widget->SetAlwaysOnTop(true);
    m_widget->SetZOrderingEnabled(true);
    m_widget->SetVisible(false);

    // The core-owned local page is served through a narrowly scoped resolver
    // below; it never inherits access to a game resource directory.
    return m_webView->LoadURL(SString("http://mta/local/index.html?loadscreen=%u", m_loadscreenIndex), false);
}

void CServerBrowserWeb::SetVisible(bool visible)
{
    m_visible = visible;
    const bool show = visible && !m_nativeDialogVisible;
    if (!m_widget)
        return;

    if (show)
    {
        CancelHibernateRequest();
        if (m_webView->GetRenderingPaused())
            m_webView->SetRenderingPaused(false);

        // A hidden CEF browser is heavily throttled by Chromium. Push the
        // authoritative native context after waking it and before exposing
        // its texture so the first Escape press cannot flash the offline
        // menu while React catches up.
        if (m_documentReady)
        {
            QueueMenuContext(true);
            FlushEvents();
        }

        m_widget->SetEnabled(true);
        // Keep Chromium awake and painting behind the native D3D placeholder
        // without exposing a partial document during its cold start.
        m_widget->SetAlpha(m_visualReady ? 1.0f : 0.0f);
        m_widget->SetVisible(true);
        m_widget->BringToFront();
        m_widget->Activate();
        m_webView->Focus(true);
    }
    else
    {
        // Visibility alone does not relinquish CEGUI's active control. Fully
        // deactivate the widget before handing input to Settings, F8 or a
        // native modal so the hidden browser cannot retain mouse/keyboard
        // ownership.
        m_widget->Deactivate();
        m_widget->SetEnabled(false);
        m_widget->SetVisible(false);
        if (m_webView)
        {
            m_webView->Focus(false);
            UpdateRenderingPauseState();
        }
    }
}

void CServerBrowserWeb::UpdateRenderingPauseState()
{
    if (!m_webView)
        return;

    if (m_nativeDialogVisible)
    {
        CancelHibernateRequest();
        if (!m_webView->GetRenderingPaused())
            m_webView->SetRenderingPausedPreservingLastFrame(true);
        return;
    }

    // Connections and context transitions need a live renderer. The hidden
    // in-game steady state is paused only by the paint acknowledgement below.
    if (!CanHibernate())
    {
        CancelHibernateRequest();
        if (m_webView->GetRenderingPaused())
            m_webView->SetRenderingPaused(false);
    }
    else if (m_webView->GetRenderingPaused() && m_pendingHibernateGeneration == 0)
    {
        // A native dialog may have paused and invalidated an in-flight paint
        // request. Wake once so the next pulse can request a fresh confirmed
        // snapshot instead of treating the dialog-era texture as current.
        m_webView->SetRenderingPaused(false);
    }
}

bool CServerBrowserWeb::CanHibernate() const
{
    return m_webView && m_documentReady && m_visualReady && !m_visible && !m_nativeDialogVisible && !m_connectionUiActive && m_mainMenu.GetIsIngame();
}

void CServerBrowserWeb::CancelHibernateRequest()
{
    m_pendingHibernateGeneration = 0;
    m_hibernatePauseDelayPulses = 0;
}

void CServerBrowserWeb::QueueHibernateRequest()
{
    if (!CanHibernate() || m_pendingHibernateGeneration != 0 || m_webView->GetRenderingPaused())
        return;

    // Zero is reserved for "no request". Wrapping is harmless as stale
    // acknowledgements must also match the currently pending generation.
    if (++m_hibernateGeneration == 0)
        ++m_hibernateGeneration;
    m_pendingHibernateGeneration = m_hibernateGeneration;

    JsonPtr event = MakeObject();
    AddString(event.get(), "type", "hibernate-request");
    AddInteger(event.get(), "generation", m_pendingHibernateGeneration);
    QueueEvent("menu", ToJson(event.get()));
}

void CServerBrowserWeb::SetNativeDialogVisible(bool visible)
{
    if (m_nativeDialogVisible == visible)
        return;

    m_nativeDialogVisible = visible;
    SetVisible(m_visible);
}

bool CServerBrowserWeb::DoPulse()
{
    if (m_initialisationFailed)
        return false;

    if (!m_webView)
    {
        if (m_initialisationDelayPulses > 0)
        {
            --m_initialisationDelayPulses;
            return true;
        }

        if (!InitialiseWebView())
        {
            m_initialisationFailed = true;
            return false;
        }

        SetVisible(m_visible);
    }

    if (!m_documentReady)
        return true;

    QueueIdentity(false);
    QueueMenuContext(false);
    UpdateRenderingPauseState();

    if (m_serverBrowserReady && m_registry)
    {
        m_registry->DoPulse();
        if (m_registry->ConsumeChanged())
        {
            m_sentRevisions.clear();
            QueueListReset();
            m_refreshing = true;
            m_sentRefreshFinished = false;
            SendSnapshot(true);
        }

        const std::string registryError = m_registry->ConsumeError();
        if (!registryError.empty())
        {
            JsonPtr error = MakeObject();
            AddString(error.get(), "type", "registry-error");
            AddString(error.get(), "message", registryError);
            QueueEvent("server", ToJson(error.get()));
        }
    }

    CServerList* list = GetCurrentList();
    if (m_visible && m_serverBrowserReady && list)
    {
        list->Pulse();
        SendSnapshot(false);

        if (m_refreshing)
        {
            const unsigned int scanned = list->GetScannedCount();
            const unsigned int total = list->GetServerCount();
            JsonPtr            progress = MakeObject();
            AddString(progress.get(), "type", "progress");
            AddString(progress.get(), "source", GetSourceName(m_source));
            AddInteger(progress.get(), "scanned", scanned);
            AddInteger(progress.get(), "total", total);
            QueueEvent("server", ToJson(progress.get()));

            if (!list->IsRefreshing() && !m_sentRefreshFinished)
            {
                JsonPtr finished = MakeObject();
                AddString(finished.get(), "type", "refresh-finished");
                AddString(finished.get(), "source", GetSourceName(m_source));
                QueueEvent("server", ToJson(finished.get()));
                m_sentRefreshFinished = true;
                m_refreshing = false;
            }
        }
    }

    QueueHibernateRequest();
    FlushEvents();

    if (m_hibernatePauseDelayPulses > 0 && --m_hibernatePauseDelayPulses == 0)
    {
        if (CanHibernate() && m_pendingHibernateGeneration != 0)
        {
            // Preserve the last CPU frame as well as the D3D texture. This is
            // a small menu-specific cost that lets a device reset restore the
            // frozen frame without waiting for Chromium's first repaint.
            m_webView->SetRenderingPausedPreservingLastFrame(true);
        }
        else
            CancelHibernateRequest();
    }
    return true;
}

void CServerBrowserWeb::Events_OnCreated()
{
}

void CServerBrowserWeb::Events_OnLoadingStart(const SString&, bool)
{
}

void CServerBrowserWeb::Events_OnDocumentReady(const SString& url)
{
    if (!url.BeginsWith("http://mta/local/index.html"))
        return;

    m_documentReady = true;
}

void CServerBrowserWeb::Events_OnLoadingFailed(const SString& url, int errorCode, const SString& errorDescription)
{
    WriteDebugEvent(SString("Neon menu failed to load %s (%d): %s", url.c_str(), errorCode, errorDescription.c_str()));
}

void CServerBrowserWeb::Events_OnNavigate(const SString&, bool, bool)
{
}

void CServerBrowserWeb::Events_OnPopup(const SString&, const SString&)
{
}

void CServerBrowserWeb::Events_OnChangeCursor(unsigned char)
{
}

void CServerBrowserWeb::Events_OnTriggerEvent(const SString& eventName, const std::vector<std::string>& arguments)
{
    if (eventName == "sb:connect" || eventName == "menu:quickConnect")
        WriteDebugEvent(SString("Neon menu connection request event=%s args=%u", eventName.c_str(), static_cast<unsigned>(arguments.size())));

    if (eventName.BeginsWith("menu:"))
        HandleMenuEvent(eventName, arguments);
    else if (eventName.BeginsWith("sb:"))
        HandleServerBrowserEvent(eventName, arguments);
}

void CServerBrowserWeb::Events_OnTooltip(const SString&)
{
}

void CServerBrowserWeb::Events_OnInputFocusChanged(bool)
{
}

bool CServerBrowserWeb::Events_OnResourcePathCheck(SString& url)
{
    if (!IsSafeRelativePath(url))
        return false;

    SString artworkPath;
    if (ResolveRegistryArtworkRequest(url, artworkPath))
    {
        url = artworkPath;
        return FileExists(url);
    }

    std::replace(url.begin(), url.end(), '/', '\\');
    url = CalcMTASAPath(PathJoin(WEB_ROOT, url));
    return FileExists(url);
}

bool CServerBrowserWeb::Events_OnResourceFileCheck(const SString& path, CBuffer& outFileData)
{
    const SString root = CalcMTASAPath(WEB_ROOT);
    bool          allowed = path.BeginsWith(root);
    if (!allowed)
    {
        SString cacheRoot = GetRegistryArtworkCacheRoot();
        std::replace(cacheRoot.begin(), cacheRoot.end(), '/', '\\');

        SString normalizedPath = path;
        std::replace(normalizedPath.begin(), normalizedPath.end(), '/', '\\');
        const SString cachePrefix = cacheRoot + "\\";
        if (normalizedPath.BeginsWith(cachePrefix))
        {
            const SString fileName = normalizedPath.substr(cachePrefix.length());
            SString       expectedPath;
            allowed = ResolveRegistryArtworkRequest("registry-assets/" + fileName, expectedPath) && normalizedPath == expectedPath;
        }
    }

    if (!allowed || !FileExists(path))
        return false;

    return outFileData.LoadFromFile(path);
}

void CServerBrowserWeb::Events_OnResourceBlocked(const SString&, const SString&, unsigned char)
{
}

void CServerBrowserWeb::Events_OnAjaxRequest(CAjaxResourceHandlerInterface* handler, const SString&)
{
    if (handler)
        handler->SetResponse("");
}

void CServerBrowserWeb::Events_OnConsoleMessage(const std::string& message, const std::string& source, int line, std::int16_t)
{
    WriteDebugEvent(SString("Neon menu console: %s (%s:%d)", message.c_str(), source.c_str(), line));
}

void CServerBrowserWeb::HandleMenuEvent(const SString& eventName, const std::vector<std::string>& arguments)
{
    if (eventName == "menu:ready")
    {
        QueueMenuInit();
        QueueIdentity(true);
    }
    else if (eventName == "menu:visualReady")
    {
        m_visualReady = true;
        if (m_widget)
            m_widget->SetAlpha(1.0f);
    }
    else if (eventName == "menu:hibernateReady" && arguments.size() == 1)
    {
        const char* text = arguments[0].c_str();
        char*       end = nullptr;
        errno = 0;
        unsigned long generation = std::strtoul(text, &end, 10);
        if (errno == 0 && end != text && *end == '\0' && generation <= std::numeric_limits<unsigned int>::max() && generation == m_pendingHibernateGeneration &&
            CanHibernate())
        {
            // requestAnimationFrame callbacks run before paint. Leave one
            // complete client pulse alive after the acknowledgement so CEF can
            // publish the confirmed frame before its external clock stops.
            m_hibernatePauseDelayPulses = 2;
        }
    }
    else if (eventName == "menu:quickConnect")
    {
        // The always-mounted menu shell owns the lightweight connection modal,
        // so Quick Connect can remain immediate without loading server data or
        // starting the lazy Server Browser scan.
        m_connectionUiActive = true;
        if (!m_mainMenu.OnQuickConnectButtonClick(nullptr, true))
            m_connectionUiActive = false;
    }
    else if (eventName == "menu:resume" && m_mainMenu.GetIsIngame())
        m_mainMenu.OnResumeButtonClick(nullptr);
    else if (eventName == "menu:disconnect" && g_pCore->IsConnected())
    {
        if (g_pCore->GetCVars()->GetValue("ask_before_disconnect", true))
        {
            SetNativeDialogVisible(true);
            m_mainMenu.AskUserIfHeWantsToDisconnect(CMainMenu::MENU_ITEM_DISCONNECT);
        }
        else
            m_mainMenu.OnDisconnectButtonClick();
    }
    else if (eventName == "menu:mapEditor")
        m_mainMenu.OnEditorButtonClick();
    else if (eventName == "menu:settings")
    {
        SetNativeDialogVisible(true);
        m_mainMenu.OnSettingsButtonClick(nullptr);
    }
    else if (eventName == "menu:about")
    {
        SetNativeDialogVisible(true);
        m_mainMenu.OnAboutButtonClick(nullptr);
    }
    else if (eventName == "menu:quit")
        m_mainMenu.OnQuitButtonClick(nullptr);
    else if (eventName == "menu:identity")
        m_mainMenu.OnNeonIdentityButtonClick(nullptr);
    else if (eventName == "menu:sound" && arguments.size() == 1)
        PlayUiSound(arguments[0]);
    else if (eventName == "menu:setLanguage" && arguments.size() == 1)
    {
        const auto locales = g_pCore->GetLocalization()->GetAvailableLocales();
        if (std::find(locales.begin(), locales.end(), arguments[0]) != locales.end())
            CLocalGUI::GetSingleton().RequestLocaleChange(arguments[0]);
    }
}

void CServerBrowserWeb::PlayUiSound(const std::string& soundName)
{
    DWORD soundId = 0;
    if (soundName == "select")
        soundId = 1;
    else if (soundName == "back")
        soundId = 2;
    else if (soundName == "highlight")
        soundId = 3;
    else if (soundName == "error")
        soundId = 4;
    else
        return;

    // These are GTA SA's resident frontend events, so no sound assets need
    // to be copied or decoded by the web UI.
    if (CGame* game = g_pCore->GetGame())
        if (CAudioEngine* audio = game->GetAudioEngine())
            audio->PlayFrontEndSound(soundId);
}

void CServerBrowserWeb::HandleServerBrowserEvent(const SString& eventName, const std::vector<std::string>& arguments)
{
    if (eventName == "sb:ready")
    {
        m_serverBrowserReady = true;
        if (m_registry)
            m_registry->Start();
        JsonPtr init = MakeObject();
        AddString(init.get(), "type", "init");
        AddString(init.get(), "version", g_pCore->GetProductVersion());
        AddString(init.get(), "source", GetSourceName(m_source));
        QueueEvent("server", ToJson(init.get()));
        QueueFavourites();
        // App::init selects and refreshes the source immediately afterwards.
        // Waiting for it avoids an eager duplicate snapshot and scan.
    }
    else if (eventName == "sb:setSource" && arguments.size() == 1)
        SelectSource(arguments[0], false);
    else if (eventName == "sb:refresh")
        RefreshCurrentSource();
    else if (eventName == "sb:connect" && arguments.size() == 3)
        Connect(arguments[0], ParsePort(arguments[1]), arguments[2]);
    else if (eventName == "sb:cancelConnect")
    {
        g_pCore->GetConnectManager()->CancelWebConnection();
        m_connectionUiActive = false;
    }
    else if (eventName == "sb:favourite" && arguments.size() == 3)
        SetFavourite(arguments[0], ParsePort(arguments[1]), arguments[2] == "1");
    else if (eventName == "sb:openExternal" && arguments.size() == 1)
    {
        const std::string& url = arguments[0];
        if (url.rfind("https://", 0) == 0 || url.rfind("http://", 0) == 0)
            ShellExecuteNonBlocking("open", url.c_str());
    }
    else if (eventName == "sb:close" && m_webView)
        m_webView->ExecuteJavascript("window.location.hash='';");
}

void CServerBrowserWeb::QueueMenuInit()
{
    JsonPtr init = MakeObject();
    AddString(init.get(), "type", "init");
    AddBoolean(init.get(), "inGame", m_mainMenu.GetIsIngame());
    AddString(init.get(), "locale", g_pCore->GetLocalization()->GetLanguageCode());
    AddWebTranslations(init.get());

    CNeonIdentityManager* identity = g_pCore->GetNeonIdentityManager();
    json_object*          identityValue = json_object_new_object();
    AddBoolean(identityValue, "authenticated", identity && identity->IsAuthenticated());
    AddBoolean(identityValue, "signingIn", identity && identity->IsSigningIn());
    AddString(identityValue, "displayName", "");
    AddString(identityValue, "status", TranslateIdentityStatus(identity ? identity->GetStatusText() : "Not signed in"));
    json_object_object_add(init.get(), "identity", identityValue);

    json_object* availableLocales = json_object_new_array();
    for (const SString& locale : g_pCore->GetLocalization()->GetAvailableLocales())
    {
        json_object* language = json_object_new_object();
        AddString(language, "locale", locale);
        AddString(language, "name", g_pLocalization->GetLanguageNativeName(locale));
        json_object_array_add(availableLocales, language);
    }
    json_object_object_add(init.get(), "availableLocales", availableLocales);
    json_object_get(availableLocales);
    json_object_object_add(init.get(), "languages", availableLocales);
    QueueEvent("menu", ToJson(init.get()));
    m_lastInGameContext = m_mainMenu.GetIsIngame() ? 1 : 0;
}

void CServerBrowserWeb::QueueMenuContext(bool force)
{
    const int inGame = m_mainMenu.GetIsIngame() ? 1 : 0;
    if (!force && inGame == m_lastInGameContext)
        return;

    m_lastInGameContext = inGame;
    JsonPtr event = MakeObject();
    AddString(event.get(), "type", "context");
    AddBoolean(event.get(), "inGame", inGame != 0);
    QueueEvent("menu", ToJson(event.get()));
}

void CServerBrowserWeb::QueueIdentity(bool force)
{
    CNeonIdentityManager* identity = g_pCore->GetNeonIdentityManager();
    const bool            authenticated = identity && identity->IsAuthenticated();
    const bool            signingIn = identity && identity->IsSigningIn();
    const std::string     status = TranslateIdentityStatus(identity ? identity->GetStatusText() : "Not signed in");
    const std::string     signature = SString("%d|%d|%s", authenticated, signingIn, status.c_str());
    if (!force && signature == m_lastIdentitySignature)
        return;

    m_lastIdentitySignature = signature;
    JsonPtr event = MakeObject();
    AddString(event.get(), "type", "identity");
    json_object* identityValue = json_object_new_object();
    AddBoolean(identityValue, "authenticated", authenticated);
    AddBoolean(identityValue, "signingIn", signingIn);
    AddString(identityValue, "displayName", "");
    AddString(identityValue, "status", status);
    json_object_object_add(event.get(), "identity", identityValue);
    QueueEvent("menu", ToJson(event.get()));
}

void CServerBrowserWeb::QueueFavourites()
{
    JsonPtr event = MakeObject();
    AddString(event.get(), "type", "favourites");
    json_object* keys = json_object_new_array();
    for (auto it = m_serverBrowser.GetFavouritesList()->IteratorBegin(); it != m_serverBrowser.GetFavouritesList()->IteratorEnd(); ++it)
    {
        if (*it && m_registry && m_registry->Contains((*it)->GetEndpoint()))
            json_object_array_add(keys, json_object_new_string((*it)->GetEndpoint().c_str()));
    }
    json_object_object_add(event.get(), "keys", keys);
    QueueEvent("server", ToJson(event.get()));
}

void CServerBrowserWeb::QueueListReset()
{
    JsonPtr event = MakeObject();
    AddString(event.get(), "type", "list-reset");
    AddString(event.get(), "source", GetSourceName(m_source));
    QueueEvent("server", ToJson(event.get()));
}

void CServerBrowserWeb::QueueServer(const CServerListItem& server)
{
    const SNeonServerMetadata* metadata = m_registry ? m_registry->FindMetadata(server) : nullptr;
    if (!metadata)
        return;

    JsonPtr event = MakeObject();
    AddString(event.get(), "type", "server");
    AddString(event.get(), "source", GetSourceName(m_source));

    json_object* value = json_object_new_object();
    AddString(value, "id", server.GetEndpoint());
    AddString(value, "serverId", metadata->serverId);
    AddString(value, "ip", server.strHost);
    AddInteger(value, "port", server.usGamePort);
    AddInteger(value, "httpPort", server.m_usHttpPort);
    AddString(value, "name", metadata->name);
    AddString(value, "tagline", metadata->tagline);
    AddString(value, "description", metadata->description);
    AddString(value, "accent", metadata->accent);
    AddString(value, "logoUrl", metadata->logoUrl);
    AddString(value, "bannerUrl", metadata->bannerUrl);
    AddString(value, "gameMode", server.strGameMode);
    AddString(value, "map", server.strMap);
    AddString(value, "version", server.strVersion);
    AddInteger(value, "players", server.nPlayers);
    AddInteger(value, "maxPlayers", server.nMaxPlayers);
    AddInteger(value, "ping", server.bScanned && !server.bSkipped ? server.nPing : -1);
    AddBoolean(value, "passworded", server.bPassworded);
    AddBoolean(value, "serials", server.bSerials);
    AddBoolean(value, "verified", server.isStatusVerified);
    AddString(value, "state", server.bSkipped || server.bMaybeOffline ? "offline" : (server.bScanned ? "online" : "queued"));
    AddBoolean(value, "favourite", IsFavourite(server));
    json_object* players = json_object_new_array();
    for (const SString& player : server.vecPlayers)
        json_object_array_add(players, json_object_new_string(player.c_str()));
    json_object_object_add(value, "playerList", players);

    json_object* countries = json_object_new_array();
    for (const std::string& country : metadata->countries)
        json_object_array_add(countries, json_object_new_string(country.c_str()));
    json_object_object_add(value, "countries", countries);

    json_object* languages = json_object_new_array();
    for (const std::string& language : metadata->languages)
        json_object_array_add(languages, json_object_new_string(language.c_str()));
    json_object_object_add(value, "languages", languages);

    json_object* links = json_object_new_array();
    for (const SNeonServerLink& link : metadata->links)
    {
        json_object* linkValue = json_object_new_object();
        AddString(linkValue, "kind", link.kind);
        AddString(linkValue, "label", link.label);
        AddString(linkValue, "url", link.url);
        json_object_array_add(links, linkValue);
    }
    json_object_object_add(value, "links", links);
    json_object_object_add(event.get(), "server", value);
    QueueEvent("server", ToJson(event.get()));
}

void CServerBrowserWeb::QueueEvent(const std::string& channel, const std::string& json)
{
    (channel == "menu" ? m_menuEvents : m_serverEvents).push_back(json);
}

void CServerBrowserWeb::QueueConnectionEvent(const std::string& json)
{
    // The menu channel is always mounted and handles Quick Connect without
    // importing the heavy server list. Mirror into the server channel only
    // after its lazy bridge exists so direct joins keep their store in sync.
    QueueEvent("menu", json);
    if (m_serverBrowserReady)
        QueueEvent("server", json);
}

void CServerBrowserWeb::FlushEvents()
{
    if (!m_webView || !m_documentReady || (m_menuEvents.empty() && m_serverEvents.empty()))
        return;

    std::string script;
    std::size_t remaining = MAX_EVENTS_PER_FRAME;
    const auto  appendChannel = [&](const char* objectName, std::vector<std::string>& events, std::size_t& budget, std::string& output)
    {
        if (events.empty() || budget == 0)
            return;
        const std::size_t count = std::min(events.size(), budget);
        output += "window.";
        output += objectName;
        output += "&&window.";
        output += objectName;
        output += ".emit([";
        for (std::size_t i = 0; i < count; ++i)
        {
            if (i)
                output += ',';
            output += events[i];
        }
        output += "]);";
        events.erase(events.begin(), events.begin() + count);
        budget -= count;
    };

    appendChannel("__neonMenu", m_menuEvents, remaining, script);
    appendChannel("__neonSB", m_serverEvents, remaining, script);
    if (!script.empty())
        m_webView->ExecuteJavascript(script);
}

void CServerBrowserWeb::SelectSource(const std::string& sourceName, bool refresh)
{
    if (sourceName == "favourites")
        m_source = Source::Favourites;
    else if (sourceName == "recent")
        m_source = Source::Recent;
    else
        m_source = Source::Internet;

    m_sentRevisions.clear();
    QueueListReset();
    SendSnapshot(true);
    if (refresh)
        RefreshCurrentSource();
}

void CServerBrowserWeb::RefreshCurrentSource()
{
    CServerList* list = GetCurrentList();
    if (!list)
        return;

    if (m_registry)
        m_registry->RefreshRemote();
    m_sentRevisions.clear();
    QueueListReset();
    list->Refresh();
    m_refreshing = true;
    m_sentRefreshFinished = false;
    SendSnapshot(true);
}

void CServerBrowserWeb::SendSnapshot(bool forceAll)
{
    CServerList* list = GetCurrentList();
    if (!list)
        return;

    for (auto it = list->IteratorBegin(); it != list->IteratorEnd(); ++it)
    {
        CServerListItem* server = *it;
        if (!server || !IsVisibleInCurrentSource(*server))
            continue;
        const std::string id = server->GetEndpoint();
        auto              sent = m_sentRevisions.find(id);
        if (forceAll || sent == m_sentRevisions.end() || sent->second != server->uiRevision)
        {
            QueueServer(*server);
            m_sentRevisions[id] = server->uiRevision;
        }
    }
}

void CServerBrowserWeb::SetFavourite(const std::string& host, unsigned short port, bool favourite)
{
    in_addr address{};
    if (!port || !CServerListItem::Parse(host.c_str(), address))
        return;

    const std::string endpoint = SString("%s:%u", inet_ntoa(address), port);
    if (!m_registry || !m_registry->Contains(endpoint))
        return;

    CServerList* favourites = m_serverBrowser.GetFavouritesList();
    if (favourite)
        favourites->AddUnique(address, port);
    else
        favourites->Remove(address, port);
    m_serverBrowser.SaveFavouritesList();
    QueueFavourites();

    // Favourite state is embedded in every row, so invalidate the web-side
    // revision cache even though ASE data itself did not change.
    m_sentRevisions.clear();
    if (m_source == Source::Favourites)
        QueueListReset();
    SendSnapshot(true);
}

void CServerBrowserWeb::Connect(const std::string& host, unsigned short port, const std::string& password)
{
    if (!port)
        return;

    CServerListItem* server = m_registry ? m_registry->Find(host, port) : nullptr;
    if (!server)
        server = m_serverBrowser.FindServer(host, port);
    if (server && server->bPassworded && password.empty())
    {
        JsonPtr event = MakeObject();
        AddString(event.get(), "type", "connect-password-required");
        AddString(event.get(), "host", host);
        AddInteger(event.get(), "port", port);
        const SNeonServerMetadata* metadata = m_registry ? m_registry->FindMetadata(*server) : nullptr;
        AddString(event.get(), "name", metadata ? metadata->name : server->strName);
        QueueConnectionEvent(ToJson(event.get()));
        return;
    }

    // Publish the target before native validation begins. This gives the
    // always-mounted shell enough context to present even an immediate local
    // failure (for example an invalid nickname or unavailable network module).
    m_connectionUiActive = true;
    NotifyConnectionStarted(host, port);

    std::string nick;
    CVARS_GET("nick", nick);
    if (!g_pCore->IsValidNick(nick.c_str()))
    {
        NotifyConnectionFailed("invalid-nick", _("Invalid nickname. Change it in Settings before connecting."));
        return;
    }

    if (!password.empty())
        m_serverBrowser.SetServerPassword(host + ":" + std::to_string(port), password);

    // Ownership lives on the bridge as well as the current native attempt so
    // a reconnect cannot silently fall back to CEGUI mid-handoff.
    if (!g_pCore->GetConnectManager()->Connect(host.c_str(), port, nick.c_str(), password.c_str(), true, true))
        NotifyConnectionFailed("connection-start-failed", _("The client could not start the connection."));
}

CServerList* CServerBrowserWeb::GetCurrentList() const
{
    return m_registry ? m_registry->GetList() : nullptr;
}

const char* CServerBrowserWeb::GetSourceName(Source source)
{
    switch (source)
    {
        case Source::Lan:
            return "lan";
        case Source::Favourites:
            return "favourites";
        case Source::Recent:
            return "recent";
        default:
            return "internet";
    }
}

bool CServerBrowserWeb::IsFavourite(const CServerListItem& server) const
{
    for (auto it = m_serverBrowser.GetFavouritesList()->IteratorBegin(); it != m_serverBrowser.GetFavouritesList()->IteratorEnd(); ++it)
    {
        if (*it && **it == server)
            return true;
    }
    return false;
}

bool CServerBrowserWeb::IsVisibleInCurrentSource(const CServerListItem& server) const
{
    if (!m_registry || !m_registry->Contains(server))
        return false;
    if (m_source == Source::Internet)
        return true;
    if (m_source == Source::Favourites)
        return IsFavourite(server);
    if (m_source == Source::Recent)
    {
        for (auto it = m_serverBrowser.GetRecentList()->IteratorBegin(); it != m_serverBrowser.GetRecentList()->IteratorEnd(); ++it)
        {
            if (*it && **it == server)
                return true;
        }
    }
    return false;
}
