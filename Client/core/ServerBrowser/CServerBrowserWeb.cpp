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
#include "CJoystickManager.h"
#include "CKeyBinds.h"
#include "CNickGen.h"
#include "CSettings.h"
#include "CServerBrowser.h"
#include "CServerList.h"
#include "CSteamClient.h"
#include "DXHook/CProxyDirect3DDevice9.h"
#include "Graphics/CVideoModeManager.h"
#include "SkyGfx/CSkyGfxManager.h"
#include <core/CAjaxResourceHandlerInterface.h>
#include <core/CClientCommands.h>
#include <core/CWebCoreInterface.h>
#include <core/CWebViewInterface.h>
#include <game/CAudioEngine.h>
#include <game/CCoronas.h>
#include <game/CSettings.h>
#include <gui/CGUIWebBrowser.h>
#include <json.h>
#include <SharedUtil.Misc.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <utility>

extern SBindableGTAControl g_bcControls[];

namespace
{
    constexpr std::size_t MAX_EVENTS_PER_FRAME = 60;
    constexpr char        WEB_ROOT[] = "MTA\\cef\\serverbrowser";
    // Set this to a registry server ID whenever Neon should promote a server
    // on the main menu again. An empty ID keeps the reusable card hidden.
    constexpr char  FEATURED_SERVER_ID[] = "";
    constexpr float DISPLAY_CALIBRATION_MIN = 0.5f;
    constexpr float DISPLAY_CALIBRATION_MAX = 2.0f;

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
        {"main.featuredServer", _td("Neon featured"), _td("Neon featured"), EWebTranslationDomain::Client},
        {"main.playFeatured", _td("Play now"), _td("Play now"), EWebTranslationDomain::Client},
        {"main.linkDiscordToPlay", _td("Link Discord to play"), _td("Link Discord to play"), EWebTranslationDomain::Client},
        {"main.resumeGame", _td("Resume game"), _td("Resume game"), EWebTranslationDomain::Client},
        {"main.browseServers", _td("Browse servers"), _td("Server browser"), EWebTranslationDomain::MainMenu},
        {"main.quickConnect", _td("Quick connect"), _td("Quick connect"), EWebTranslationDomain::MainMenu},
        {"main.quickConnectCaption", _td("Join by IP, hostname or mtaneon:// link."), _td("Join by IP, hostname or mtaneon:// link."),
         EWebTranslationDomain::Client},
        {"main.mapEditor", _td("Map editor"), _td("Map editor"), EWebTranslationDomain::MainMenu},
        {"main.quitGame", _td("Quit game"), _td("Quit"), EWebTranslationDomain::MainMenu},
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
        {"browser.destinationsCount", _td("{count} servers"), _td("{count} servers"), EWebTranslationDomain::Client},
        {"browser.playersOnlineCount", _td("{count} players online"), _td("{count} players online"), EWebTranslationDomain::Client},
        {"browser.backToMain", _td("Back to main menu"), _td("Back to main menu"), EWebTranslationDomain::Client},
        {"browser.searchPlaceholder", _td("Search servers or enter an address"), _td("Search servers..."), EWebTranslationDomain::Client},
        {"browser.direct", _td("Direct"), _td("Direct"), EWebTranslationDomain::Client},
        {"browser.directConnect", _td("Direct connect"), _td("Direct connect"), EWebTranslationDomain::Client},
        {"browser.connectToAddress", _td("Connect to address"), _td("Connect to address"), EWebTranslationDomain::Client},
        {"browser.directReady", _td("Direct connect"), _td("Direct connect"), EWebTranslationDomain::Client},
        {"browser.directHint", _td("Press Enter to connect."), _td("Press Enter to connect."), EWebTranslationDomain::Client},
        {"browser.heading.destinations", _td("Servers"), _td("Servers"), EWebTranslationDomain::Client},
        {"browser.filter.hideFull", _td("Hide full servers"), _td("Hide full servers"), EWebTranslationDomain::Client},
        {"browser.filter.hideEmpty", _td("Hide empty servers"), _td("Hide empty servers"), EWebTranslationDomain::Client},
        {"browser.filter.hideLocked", _td("Hide locked servers"), _td("Hide locked servers"), EWebTranslationDomain::Client},
        {"browser.filter.hideIncompatible", _td("Hide other versions"), _td("Hide other versions"), EWebTranslationDomain::Client},
        {"browser.filter.hideOffline", _td("Hide offline servers"), _td("Hide offline servers"), EWebTranslationDomain::Client},
        {"browser.empty", _td("No servers found."), _td("No servers found."), EWebTranslationDomain::Client},
        {"browser.emptyHint", _td("Clear filters or refresh."), _td("Clear filters or refresh."), EWebTranslationDomain::Client},
        {"server.passwordProtected", _td("Password protected"), _td("Password protected"), EWebTranslationDomain::Client},
        {"server.removeFavourite", _td("Remove from favourites"), _td("Remove from favourites"), EWebTranslationDomain::Client},
        {"server.addFavourite", _td("Add to favourites"), _td("Add Favorite"), EWebTranslationDomain::Client},
        {"server.playersUnverified", _td("Player count not verified"), _td("Player count not verified"), EWebTranslationDomain::Client},
        {"server.offline", _td("Offline"), _td("Offline"), EWebTranslationDomain::Client},
        {"server.featured", _td("Neon featured"), _td("Neon featured"), EWebTranslationDomain::Client},
        {"details.selectServer", _td("Select a server to view details."), _td("Select a server to view details."), EWebTranslationDomain::Client},
        {"details.selectedDestination", _td("Selected server"), _td("Selected server"), EWebTranslationDomain::Client},
        {"details.copyAddress", _td("Copy {address}"), _td("Copy {address}"), EWebTranslationDomain::Client},
        {"details.serverLink", _td("Server link"), _td("Server link"), EWebTranslationDomain::Client},
        {"details.copyLink", _td("Copy link"), _td("Copy link"), EWebTranslationDomain::Client},
        {"details.copied", _td("Copied"), _td("Copied"), EWebTranslationDomain::Client},
        {"details.copyFailed", _td("Copy failed"), _td("Copy failed"), EWebTranslationDomain::Client},
        {"details.regionsLanguages", _td("Regions & languages"), _td("Regions & languages"), EWebTranslationDomain::Client},
        {"details.playersOnline", _td("{count} players online"), _td("{count} players online"), EWebTranslationDomain::Client},
        {"details.viewAllPlayers", _td("View all {count} players"), _td("View all {count} players"), EWebTranslationDomain::Client},
        {"details.noPlayersOnline", _td("No players online"), _td("No players online"), EWebTranslationDomain::Client},
        {"details.backToServerDetails", _td("Back to server details"), _td("Back to server details"), EWebTranslationDomain::Client},
        {"details.joinServer", _td("Join server"), _td("Join Game"), EWebTranslationDomain::Client},
        {"modal.passwordRequired", _td("Password required"), _td("Password required"), EWebTranslationDomain::Client},
        {"modal.restrictedServer", _td("Restricted server"), _td("Restricted server"), EWebTranslationDomain::Client},
        {"modal.thisServer", _td("This server"), _td("This server"), EWebTranslationDomain::Client},
        {"modal.protectedServer", _td("{server} requires a password."), _td("{server} requires a password."), EWebTranslationDomain::Client},
        {"modal.serverPassword", _td("Server password"), _td("Server password"), EWebTranslationDomain::Client},
        {"modal.connecting", _td("Connecting…"), _td("Connecting…"), EWebTranslationDomain::Client},
        {"modal.joining", _td("Joining {server}"), _td("Joining {server}"), EWebTranslationDomain::Client},
        {"modal.enteringSanAndreas", _td("Entering San Andreas"), _td("Entering San Andreas"), EWebTranslationDomain::Client},
        {"modal.contactingServer", _td("Checking server…"), _td("Checking server…"), EWebTranslationDomain::Client},
        {"modal.authorizingIdentity", _td("Neon Identity"), _td("Neon Identity"), EWebTranslationDomain::Client},
        {"modal.authorizingHint", _td("Authorizing identity…"), _td("Authorizing identity…"), EWebTranslationDomain::Client},
        {"modal.connectionAccepted", _td("Welcome to the streets"), _td("Welcome to the streets"), EWebTranslationDomain::Client},
        {"modal.enteringGame", _td("Loading game…"), _td("Loading game…"), EWebTranslationDomain::Client},
        {"modal.connectionFailed", _td("Connection failed"), _td("Connection failed"), EWebTranslationDomain::Client},
        {"modal.unknownError", _td("Unknown error."), _td("Unknown error."), EWebTranslationDomain::Client},
        {"modal.serverFull", _td("Server is full"), _td("Server is full"), EWebTranslationDomain::Client},
        {"modal.serverFullHint", _td("No free slot is available. Try again in a moment."), _td("No free slot is available. Try again in a moment."),
         EWebTranslationDomain::Client},
        {"modal.connectionTimedOut", _td("Connection timed out"), _td("Connection timed out"), EWebTranslationDomain::Client},
        {"modal.connectionTimedOutHint", _td("The server did not respond. Check your connection and try again."),
         _td("The server did not respond. Check your connection and try again."), EWebTranslationDomain::Client},
        {"modal.passwordRejected", _td("Wrong password"), _td("Wrong password"), EWebTranslationDomain::Client},
        {"modal.passwordRejectedHint", _td("Enter the server password again."), _td("Enter the server password again."), EWebTranslationDomain::Client},
        {"modal.identityRequired", _td("Neon Identity required"), _td("Neon Identity required"), EWebTranslationDomain::Client},
        {"modal.identityRequiredHint", _td("Return to the main menu and link Discord before joining this server."),
         _td("Return to the main menu and link Discord before joining this server."), EWebTranslationDomain::Client},
        {"modal.identityFailed", _td("Identity check failed"), _td("Identity check failed"), EWebTranslationDomain::Client},
        {"modal.identityFailedHint", _td("Authorization failed. Try again."), _td("Authorization failed. Try again."), EWebTranslationDomain::Client},
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
        {"modal.connectionLostHint", _td("The server closed the connection. Try again."), _td("The server closed the connection. Try again."),
         EWebTranslationDomain::Client},
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

    void AddDouble(json_object* object, const char* name, double value)
    {
        json_object_object_add(object, name, json_object_new_double(value));
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

    void Start(bool refreshStatuses)
    {
        if (m_started)
        {
            if (refreshStatuses && !m_refreshStatuses)
            {
                m_refreshStatuses = true;
                m_servers.Refresh();
            }
            return;
        }

        m_started = true;
        m_refreshStatuses = refreshStatuses;
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

    CServerListItem* FindByServerId(const std::string& serverId)
    {
        for (auto it = m_servers.IteratorBegin(); it != m_servers.IteratorEnd(); ++it)
        {
            CServerListItem*           server = *it;
            const SNeonServerMetadata* metadata = server ? FindMetadata(*server) : nullptr;
            if (metadata && metadata->serverId == serverId)
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
        // The main menu only needs the small trusted catalogue and its
        // artwork. Defer ASE status queries until the server browser opens so
        // the featured card does not start a background server scan.
        if (m_refreshStatuses)
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
    bool                                       m_refreshStatuses{};
};

class CWebSettingsSession
{
public:
    struct SBindRow
    {
        std::string              id;
        std::string              section;
        std::string              label;
        std::string              control;
        std::string              command;
        std::string              arguments;
        std::string              resource;
        std::vector<std::string> keys;
        bool                     gtaControl{};
    };

    struct SChatPreset
    {
        std::string                        id;
        std::string                        name;
        std::map<std::string, std::string> values;
    };

    struct SState
    {
        std::string gameNickname;
        bool        gameSavePasswords{true};
        bool        gameAutoRefreshBrowser{true};
        bool        gameAllowScreenUpload{true};
        bool        gameAllowExternalSounds{true};
        bool        gameAlwaysShowTransferBox{};
        bool        gameDiscordRichPresence{true};
        bool        gameDiscordShareData{};
        bool        gameSteamStatus{};
        bool        gameSaveCameraPhotos{};
        bool        gameAskBeforeDisconnect{true};
        bool        gameCustomizedSAFiles{};
        int         gameMapOpacity{61};
        int         gameMapAlpha{155};
        int         gameMapImage{};

        int  audioMasterVolume{100};
        int  audioRadioVolume{100};
        int  audioSfxVolume{100};
        int  audioMtaVolume{100};
        int  audioVoiceVolume{100};
        bool audioRadioEqualizer{true};
        bool audioRadioAutotune{true};
        bool audioUserTrackAutoScan{};
        int  audioUserTrackMode{};
        bool audioMuteMaster{};
        bool audioMuteRadio{};
        bool audioMuteSfx{};
        bool audioMuteMta{};
        bool audioMuteVoice{};

        bool controlsInvertMouse{};
        bool controlsSteerWithMouse{};
        bool controlsFlyWithMouse{};
        int  controlsMouseSensitivity{50};
        int  controlsVerticalAimSensitivity{50};
        bool controlsUseMouseSensitivityForAiming{};
        bool controlsClassic{};
        int  controlsJoypadDeadZone{15};
        int  controlsJoypadSaturation{100};

        std::string   interfaceLocale{"en_US"};
        std::string   interfaceSkin{"Default"};
        std::uint32_t interfaceChatBackgroundColor{};
        std::uint32_t interfaceChatTextColor{0xFFACD5FE};
        std::uint32_t interfaceChatInputBackgroundColor{};
        std::uint32_t interfaceChatInputTextColor{0xFFACD5FE};
        int           interfaceChatFont{2};
        int           interfaceChatLines{10};
        float         interfaceChatScaleX{1.0f};
        float         interfaceChatScaleY{1.0f};
        float         interfaceChatWidth{1.5f};
        bool          interfaceChatCssText{};
        bool          interfaceChatCssBackground{};
        bool          interfaceChatNickCompletion{true};
        bool          interfaceChatTextOutline{};
        float         interfaceChatLineLife{12.0f};
        float         interfaceChatLineFadeOut{3.0f};
        int           interfaceChatPositionHorizontal{};
        int           interfaceChatPositionVertical{};
        int           interfaceChatTextAlignment{};
        float         interfaceChatOffsetX{0.0125f};
        float         interfaceChatOffsetY{0.015f};
        bool          interfaceFlashWindow{true};
        bool          interfaceTrayNotifications{true};
        bool          browserRemoteWebsites{true};
        bool          browserRemoteJavascript{true};
        bool          browserGpuRendering{true};
        bool          browserVideoAcceleration{true};

        int  advancedFastClothesLoading{1};
        int  advancedBrowserSpeed{1};
        int  advancedSingleConnection{};
        int  advancedPacketTag{};
        int  advancedProgressAnimation{1};
        int  advancedProcessPriority{};
        int  advancedDebugSetting{};
        int  advancedStreamingMemory{50};
        bool advancedCpuAffinity{true};
        int  advancedUpdateBuildType{};
        int  advancedUpdateAutoInstall{1};

        bool              extendedWorldEnabled{};
        int               extendedWorldDistance{2000};
        bool              distantLightsEnabled{};
        int               distantLightsDistance{2000};
        float             distantLightsCoronaSize{0.25f};
        SkyGfxMTAConfigV1 skyGfx{};
        int               radarStyle{};
        float             radarPositionX{40.0f};
        float             radarPositionY{104.0f};
        float             radarWidth{85.5f};
        float             radarHeight{78.0f};
        bool              radarWidescreenSafe{true};

        int   graphicsVideoMode{};
        int   graphicsDisplayMode{1};
        int   graphicsFullscreenStyle{FULLSCREEN_STANDARD};
        bool  graphicsFullscreenMinimize{};
        bool  graphicsVSync{true};
        bool  graphicsDPIAware{};
        int   graphicsFieldOfView{70};
        int   graphicsDrawDistance{31};
        int   graphicsBrightness{66};
        int   graphicsFXQuality{2};
        int   graphicsAnisotropic{};
        int   graphicsAntiAliasing{1};
        int   graphicsAspectRatio{};
        bool  graphicsHudMatchAspectRatio{true};
        bool  graphicsVolumetricShadows{};
        bool  graphicsGrass{true};
        bool  graphicsHeatHaze{};
        bool  graphicsTyreSmoke{true};
        bool  graphicsDynamicPedShadows{};
        bool  graphicsMotionBlur{};
        bool  graphicsCoronaReflections{};
        bool  graphicsHighDetailVehicles{};
        bool  graphicsHighDetailPeds{};
        bool  graphicsShowUnsafeResolutions{};
        bool  graphicsDeviceSelectionDialog{true};
        float graphicsGamma{0.95f};
        float graphicsBrightnessScale{1.03f};
        float graphicsContrast{1.0f};
        float graphicsSaturation{1.0f};
        bool  graphicsGammaEnabled{};
        bool  graphicsBrightnessEnabled{};
        bool  graphicsContrastEnabled{};
        bool  graphicsSaturationEnabled{};
        bool  graphicsApplyWindowed{};
        bool  graphicsApplyFullscreen{};

        std::vector<std::string> browserBlacklist;
        std::vector<std::string> browserWhitelist;
        std::vector<SBindRow>    binds;
    };

    struct SResolution
    {
        int  mode{};
        int  width{};
        int  height{};
        int  depth{};
        bool unsafe{};
    };

    void Begin()
    {
        m_draft = ReadCurrentState();
        ReadBrowserLists(m_draft);
        ReadBindings(m_draft);
        ReadChatPresets();
        m_original = m_draft;
        m_restartBaseline = m_draft;
        m_connectionBaseline = m_draft;
        m_active = true;
        m_restartRequired = false;
        m_disconnectRequired = false;
        m_captureBindId.clear();
        m_captureBindSlot = -1;
        m_captureAxis = -1;
        m_error.clear();
        m_wasSkyGfxManaged = IsSkyGfxManaged();
        m_wasRadarManaged = IsRadarManaged();
        m_wasConnected = g_pCore->IsConnected();
    }

    bool SetValue(const std::string& id, const std::string& value)
    {
        if (!m_active)
            return false;
        m_error.clear();

        if (SetLegacyValue(id, value))
            return true;

        bool   booleanValue{};
        double numberValue{};
        if (id == "extendedWorld.enabled" && ParseBoolean(value, booleanValue))
            m_draft.extendedWorldEnabled = booleanValue;
        else if (id == "extendedWorld.distance" && ParseNumber(value, numberValue))
            m_draft.extendedWorldDistance = QuantizeInteger(numberValue, 300, 5000, 100);
        else if (id == "distantLights.enabled" && ParseBoolean(value, booleanValue))
            m_draft.distantLightsEnabled = booleanValue;
        else if (id == "distantLights.distance" && ParseNumber(value, numberValue))
            m_draft.distantLightsDistance = QuantizeInteger(numberValue, 300, 5000, 100);
        else if (id == "distantLights.coronaSize" && ParseNumber(value, numberValue))
            m_draft.distantLightsCoronaSize = QuantizeFloat(numberValue, 0.1f, 1.0f, 0.05f);
        else if (id == "skyGfx.enabled" && !IsSkyGfxManaged() && ParseBoolean(value, booleanValue))
            m_draft.skyGfx.enabled = booleanValue ? 1u : 0u;
        else if (id == "skyGfx.colorFilter" && !IsSkyGfxManaged() && ParseBoolean(value, booleanValue))
            m_draft.skyGfx.ps2ColorFilter = booleanValue ? 1u : 0u;
        else if (id == "skyGfx.colorFilterBlur" && !IsSkyGfxManaged() && ParseBoolean(value, booleanValue))
            m_draft.skyGfx.ps2ColorFilterBlur = booleanValue ? 1u : 0u;
        else if (id == "skyGfx.pcTimecycle" && !IsSkyGfxManaged() && ParseBoolean(value, booleanValue))
            m_draft.skyGfx.ps2ColorFilterPcTimecycle = booleanValue ? 1u : 0u;
        else if (id == "skyGfx.depthBias" && !IsSkyGfxManaged() && ParseBoolean(value, booleanValue))
            m_draft.skyGfx.ps2DepthBias = booleanValue ? 1u : 0u;
        else if (id == "skyGfx.ycbcr" && !IsSkyGfxManaged() && ParseBoolean(value, booleanValue))
            m_draft.skyGfx.ycbcrCorrection = booleanValue ? 1u : 0u;
        else if (id == "skyGfx.radiosity" && !IsSkyGfxManaged() && ParseBoolean(value, booleanValue))
            m_draft.skyGfx.ps2Radiosity = booleanValue ? 1u : 0u;
        else if (id == "skyGfx.radiosityIntensity" && !IsSkyGfxManaged() && ParseNumber(value, numberValue) &&
                 (numberValue == 24.0 || numberValue == 35.0 || numberValue == 48.0 || numberValue == 64.0))
            m_draft.skyGfx.ps2RadiosityIntensity = static_cast<std::uint32_t>(numberValue);
        else if (id == "skyGfx.radiosityFilterPasses" && !IsSkyGfxManaged() && ParseNumber(value, numberValue))
            m_draft.skyGfx.ps2RadiosityFilterPasses = static_cast<std::uint32_t>(QuantizeInteger(numberValue, 1, 4, 1));
        else if (id == "skyGfx.radiosityRenderPasses" && !IsSkyGfxManaged() && ParseNumber(value, numberValue))
            m_draft.skyGfx.ps2RadiosityRenderPasses = static_cast<std::uint32_t>(QuantizeInteger(numberValue, 1, 4, 1));
        else if (id == "radar.style" && !IsRadarManaged() && ParseNumber(value, numberValue))
            m_draft.radarStyle = QuantizeInteger(numberValue, 0, 1, 1);
        else if (id == "radar.positionX" && !IsRadarManaged() && ParseNumber(value, numberValue))
            m_draft.radarPositionX = QuantizeFloat(numberValue, 0.0f, 640.0f, 1.0f);
        else if (id == "radar.positionY" && !IsRadarManaged() && ParseNumber(value, numberValue))
            m_draft.radarPositionY = QuantizeFloat(numberValue, 0.0f, 448.0f, 1.0f);
        else if (id == "radar.width" && !IsRadarManaged() && ParseNumber(value, numberValue))
            m_draft.radarWidth = QuantizeFloat(numberValue, 40.0f, 200.0f, 0.5f);
        else if (id == "radar.height" && !IsRadarManaged() && ParseNumber(value, numberValue))
            m_draft.radarHeight = QuantizeFloat(numberValue, 40.0f, 200.0f, 0.5f);
        else if (id == "radar.widescreenSafe" && !IsRadarManaged() && ParseBoolean(value, booleanValue))
            m_draft.radarWidescreenSafe = booleanValue;
        else if (id == "graphics.videoMode" && ParseNumber(value, numberValue) && IsVideoModeAvailable(static_cast<int>(numberValue)))
            m_draft.graphicsVideoMode = static_cast<int>(numberValue);
        else if (id == "graphics.displayMode" && ParseNumber(value, numberValue))
        {
            m_draft.graphicsDisplayMode = QuantizeInteger(numberValue, 0, 3, 1);
            if (m_draft.graphicsDisplayMode > 0)
                m_draft.graphicsFullscreenStyle = m_draft.graphicsDisplayMode - 1;
        }
        else if (id == "graphics.fullscreenMinimize" && ParseBoolean(value, booleanValue))
            m_draft.graphicsFullscreenMinimize = booleanValue;
        else if (id == "graphics.vsync" && ParseBoolean(value, booleanValue))
            m_draft.graphicsVSync = booleanValue;
        else if (id == "graphics.dpiAware" && ParseBoolean(value, booleanValue))
            m_draft.graphicsDPIAware = booleanValue;
        else if (id == "graphics.fov" && ParseNumber(value, numberValue))
            m_draft.graphicsFieldOfView = QuantizeInteger(numberValue, 70, 90, 5);
        else if (id == "graphics.drawDistance" && ParseNumber(value, numberValue))
            m_draft.graphicsDrawDistance = QuantizeInteger(numberValue, 0, 100, 1);
        else if (id == "graphics.brightness" && ParseNumber(value, numberValue))
            m_draft.graphicsBrightness = QuantizeInteger(numberValue, 0, 100, 1);
        else if (id == "graphics.fxQuality" && ParseNumber(value, numberValue))
        {
            m_draft.graphicsFXQuality = QuantizeInteger(numberValue, 0, 3, 1);
            if (m_draft.graphicsFXQuality == 0)
                m_draft.graphicsVolumetricShadows = false;
        }
        else if (id == "graphics.anisotropic" && ParseNumber(value, numberValue))
            m_draft.graphicsAnisotropic = QuantizeInteger(numberValue, 0, GetMaxAnisotropic(), 1);
        else if (id == "graphics.antiAliasing" && ParseNumber(value, numberValue))
            m_draft.graphicsAntiAliasing = QuantizeInteger(numberValue, 1, 4, 1);
        else if (id == "graphics.aspectRatio" && ParseNumber(value, numberValue))
            m_draft.graphicsAspectRatio = QuantizeInteger(numberValue, ASPECT_RATIO_AUTO, ASPECT_RATIO_16_9, 1);
        else if (id == "graphics.hudMatchAspectRatio" && ParseBoolean(value, booleanValue))
            m_draft.graphicsHudMatchAspectRatio = booleanValue;
        else if (id == "graphics.volumetricShadows" && ParseBoolean(value, booleanValue) && m_draft.graphicsFXQuality != 0)
            m_draft.graphicsVolumetricShadows = booleanValue;
        else if (id == "graphics.grass" && ParseBoolean(value, booleanValue) && m_draft.graphicsFXQuality != 0)
            m_draft.graphicsGrass = booleanValue;
        else if (id == "graphics.heatHaze" && ParseBoolean(value, booleanValue))
            m_draft.graphicsHeatHaze = booleanValue;
        else if (id == "graphics.tyreSmoke" && ParseBoolean(value, booleanValue))
            m_draft.graphicsTyreSmoke = booleanValue;
        else if (id == "graphics.dynamicPedShadows" && ParseBoolean(value, booleanValue) && m_draft.graphicsFXQuality >= 2)
            m_draft.graphicsDynamicPedShadows = booleanValue;
        else if (id == "graphics.motionBlur" && ParseBoolean(value, booleanValue))
            m_draft.graphicsMotionBlur = booleanValue;
        else if (id == "graphics.coronaReflections" && ParseBoolean(value, booleanValue))
            m_draft.graphicsCoronaReflections = booleanValue;
        else if (id == "graphics.highDetailVehicles" && ParseBoolean(value, booleanValue))
            m_draft.graphicsHighDetailVehicles = booleanValue;
        else if (id == "graphics.highDetailPeds" && ParseBoolean(value, booleanValue))
            m_draft.graphicsHighDetailPeds = booleanValue;
        else if (id == "graphics.showUnsafeResolutions" && ParseBoolean(value, booleanValue))
            m_draft.graphicsShowUnsafeResolutions = booleanValue;
        else if (id == "graphics.deviceSelectionDialog" && ParseBoolean(value, booleanValue))
            m_draft.graphicsDeviceSelectionDialog = booleanValue;
        else if (id == "graphics.gamma" && ParseNumber(value, numberValue))
            m_draft.graphicsGamma = QuantizeFloat(numberValue, DISPLAY_CALIBRATION_MIN, DISPLAY_CALIBRATION_MAX, 0.01f);
        else if (id == "graphics.brightnessScale" && ParseNumber(value, numberValue))
            m_draft.graphicsBrightnessScale = QuantizeFloat(numberValue, DISPLAY_CALIBRATION_MIN, DISPLAY_CALIBRATION_MAX, 0.01f);
        else if (id == "graphics.contrast" && ParseNumber(value, numberValue))
            m_draft.graphicsContrast = QuantizeFloat(numberValue, DISPLAY_CALIBRATION_MIN, DISPLAY_CALIBRATION_MAX, 0.01f);
        else if (id == "graphics.saturation" && ParseNumber(value, numberValue))
            m_draft.graphicsSaturation = QuantizeFloat(numberValue, DISPLAY_CALIBRATION_MIN, DISPLAY_CALIBRATION_MAX, 0.01f);
        else if (id == "graphics.gammaEnabled" && ParseBoolean(value, booleanValue))
            m_draft.graphicsGammaEnabled = booleanValue;
        else if (id == "graphics.brightnessEnabled" && ParseBoolean(value, booleanValue))
            m_draft.graphicsBrightnessEnabled = booleanValue;
        else if (id == "graphics.contrastEnabled" && ParseBoolean(value, booleanValue))
            m_draft.graphicsContrastEnabled = booleanValue;
        else if (id == "graphics.saturationEnabled" && ParseBoolean(value, booleanValue))
            m_draft.graphicsSaturationEnabled = booleanValue;
        else if (id == "graphics.applyWindowed" && ParseBoolean(value, booleanValue))
            m_draft.graphicsApplyWindowed = booleanValue;
        else if (id == "graphics.applyFullscreen" && ParseBoolean(value, booleanValue))
            m_draft.graphicsApplyFullscreen = booleanValue;
        else
            return false;

        if (id == "graphics.vsync")
            ApplyVSyncPreview(m_draft);
        else if (id.compare(0, 9, "graphics.") == 0 &&
                 (id == "graphics.gamma" || id == "graphics.brightnessScale" || id == "graphics.contrast" || id == "graphics.saturation" ||
                  id == "graphics.gammaEnabled" || id == "graphics.brightnessEnabled" || id == "graphics.contrastEnabled" ||
                  id == "graphics.saturationEnabled" || id == "graphics.applyWindowed" || id == "graphics.applyFullscreen"))
            ApplyDisplayCalibrationPreview(m_draft);

        return true;
    }

    bool RunAction(const std::string& action, const std::string& argument)
    {
        if (!m_active)
            return false;
        m_error.clear();

        if (action == "rebuildDistantLights" && IsDistantLightsRebuildAvailable())
        {
            // Rebuilding is deliberately immediate: unlike preference edits,
            // it is a one-shot repair action with no meaningful draft state.
            CCore::GetSingleton().GetGame()->GetCoronas()->RebuildDistantLights();
            return true;
        }

        if (action == "randomNickname")
        {
            m_draft.gameNickname = CNickGen::GetRandomNickname();
            return true;
        }
        if (action == "chatPreset")
        {
            const auto found = std::find_if(m_chatPresets.begin(), m_chatPresets.end(), [&](const SChatPreset& preset) { return preset.id == argument; });
            if (found == m_chatPresets.end())
                return false;
            ApplyChatPreset(*found);
            return true;
        }
        if (action == "captureBind")
        {
            const std::size_t separator = argument.rfind('|');
            if (separator == std::string::npos)
                return false;
            const int         slot = std::atoi(argument.substr(separator + 1).c_str());
            const std::string id = argument.substr(0, separator);
            if (slot < 0 || slot > 3 || std::none_of(m_draft.binds.begin(), m_draft.binds.end(), [&](const SBindRow& row) { return row.id == id; }))
                return false;
            m_captureBindId = id;
            m_captureBindSlot = slot;
            return true;
        }
        if (action == "resetBinds")
        {
            // Legacy CEGUI also applies this operation immediately rather than
            // rolling it back on Cancel. Preserve that long-standing behavior.
            CCore::GetSingleton().GetKeyBinds()->LoadDefaultBinds();
            ReadBindings(m_draft);
            ReadBindings(m_original);
            return true;
        }
        if (action == "captureJoypadAxis")
        {
            const int                  index = std::atoi(argument.c_str());
            CJoystickManagerInterface* joystick = GetJoystickManager();
            if (!joystick || index < 0 || index >= joystick->GetOutputCount() || !joystick->BindNextUsedAxisToOutput(index))
                return false;
            m_captureAxis = index;
            return true;
        }
        if (action == "browserBlacklistAdd")
        {
            if (AddDomain(m_draft.browserBlacklist, argument))
                return true;
            m_error = "Enter a valid domain that is not already in the blacklist.";
            return false;
        }
        if (action == "browserBlacklistRemove")
            return RemoveDomain(m_draft.browserBlacklist, argument);
        if (action == "browserBlacklistClear")
        {
            m_draft.browserBlacklist.clear();
            return true;
        }
        if (action == "browserWhitelistAdd")
        {
            if (AddDomain(m_draft.browserWhitelist, argument))
                return true;
            m_error = "Enter a valid domain that is not already in the whitelist.";
            return false;
        }
        if (action == "browserWhitelistRemove")
            return RemoveDomain(m_draft.browserWhitelist, argument);
        if (action == "browserWhitelistClear")
        {
            m_draft.browserWhitelist.clear();
            return true;
        }
        if (action == "openResourceFolder")
        {
            const SString path = GetCommonRegistryValue("", "File Cache Path");
            if (DirectoryExists(path))
                ShellExecuteNonBlocking("open", path);
            return true;
        }
        if (action == "checkForUpdates")
        {
            CVARS_SET("update_build_type", m_draft.advancedUpdateBuildType);
            GetVersionUpdater()->InitiateManualCheck();
            return true;
        }
        if (action == "disconnectNow" && m_disconnectRequired && g_pCore->IsConnected())
        {
            m_disconnectRequired = false;
            CCore::GetSingleton().GetCommands()->Execute("disconnect", "");
            return true;
        }

        if (action == "radarPreset" && !IsRadarManaged())
        {
            m_draft.radarPositionX = 40.0f;
            m_draft.radarPositionY = 104.0f;
            m_draft.radarWidescreenSafe = true;
            if (argument == "neon")
            {
                m_draft.radarWidth = 85.5f;
                m_draft.radarHeight = 78.0f;
                return true;
            }
            if (argument == "vanilla")
            {
                m_draft.radarWidth = 94.0f;
                m_draft.radarHeight = 76.0f;
                return true;
            }
        }
        if (action == "resetDisplayCalibration")
        {
            m_draft.graphicsGamma = 1.0f;
            m_draft.graphicsBrightnessScale = 1.0f;
            m_draft.graphicsContrast = 1.0f;
            m_draft.graphicsSaturation = 1.0f;
            m_draft.graphicsGammaEnabled = false;
            m_draft.graphicsBrightnessEnabled = false;
            m_draft.graphicsContrastEnabled = false;
            m_draft.graphicsSaturationEnabled = false;
            m_draft.graphicsApplyWindowed = false;
            m_draft.graphicsApplyFullscreen = false;
            ApplyDisplayCalibrationPreview(m_draft);
            return true;
        }
        if (action == "restartNow" && m_restartRequired)
        {
            SetOnQuitCommand("restart");
            CCore::GetSingleton().Quit();
            return true;
        }
        return false;
    }

    void Reset(const std::string& section)
    {
        if (!m_active)
            return;

        SState defaults;
        if (section == "game")
        {
            const std::string nickname = m_draft.gameNickname;
            CopyGame(defaults, m_draft);
            m_draft.gameNickname = nickname;
        }
        else if (section == "audio")
        {
            CopyAudio(defaults, m_draft);
            ApplyAudioPreview(m_draft);
        }
        else if (section == "controls")
        {
            CopyControls(defaults, m_draft);
            GetJoystickManager()->SetDefaults();
            m_draft.controlsJoypadDeadZone = GetJoystickManager()->GetDeadZone();
            m_draft.controlsJoypadSaturation = GetJoystickManager()->GetSaturation();
        }
        else if (section == "interface")
        {
            const std::string locale = m_draft.interfaceLocale;
            const std::string skin = m_draft.interfaceSkin;
            CopyInterface(defaults, m_draft);
            m_draft.interfaceLocale = locale;
            m_draft.interfaceSkin = skin;
        }
        else if (section == "advanced")
        {
            CopyAdvanced(defaults, m_draft);
            // The static schema fallback predates the runtime streaming-memory
            // bounds. Match the legacy defaults action and never create a
            // draft below the safe range detected for this machine.
            m_draft.advancedStreamingMemory = static_cast<int>(g_pCore->GetMaxStreamingMemory());
        }
        else if (section == "neon")
        {
            if (!IsSkyGfxManaged())
                m_draft.skyGfx = defaults.skyGfx;
            if (!IsRadarManaged())
            {
                m_draft.radarStyle = defaults.radarStyle;
                m_draft.radarPositionX = defaults.radarPositionX;
                m_draft.radarPositionY = defaults.radarPositionY;
                m_draft.radarWidth = defaults.radarWidth;
                m_draft.radarHeight = defaults.radarHeight;
                m_draft.radarWidescreenSafe = defaults.radarWidescreenSafe;
            }
            m_draft.extendedWorldEnabled = defaults.extendedWorldEnabled;
            m_draft.extendedWorldDistance = defaults.extendedWorldDistance;
            m_draft.distantLightsEnabled = defaults.distantLightsEnabled;
            m_draft.distantLightsDistance = defaults.distantLightsDistance;
            m_draft.distantLightsCoronaSize = defaults.distantLightsCoronaSize;
        }
        else if (section == "graphics")
        {
            const SState current = m_draft;
            CopyGraphics(defaults, m_draft);
            if (!IsMultiMonitor())
            {
                m_draft.graphicsFullscreenMinimize = current.graphicsFullscreenMinimize;
                m_draft.graphicsDeviceSelectionDialog = current.graphicsDeviceSelectionDialog;
            }
            if (!HasUnsafeResolutions())
                m_draft.graphicsShowUnsafeResolutions = current.graphicsShowUnsafeResolutions;
            ApplyVSyncPreview(m_draft);
            ApplyDisplayCalibrationPreview(m_draft);
        }
    }

    void Cancel()
    {
        if (m_active)
        {
            ApplyVSyncPreview(m_original);
            ApplyDisplayCalibrationPreview(m_original);
            ApplyAudioPreview(m_original);
            if (GetJoystickManager()->IsCapturingAxis())
                GetJoystickManager()->CancelCaptureAxis(false);
            m_draft = m_original;
            m_captureBindId.clear();
            m_captureBindSlot = -1;
            m_captureAxis = -1;
            m_error.clear();
        }
    }

    void Apply()
    {
        if (!m_active)
            return;

        m_error.clear();
        if (!CCore::GetSingleton().IsValidNick(m_draft.gameNickname.c_str()))
        {
            m_error = "Your nickname contains invalid characters.";
            return;
        }

        ApplyGameSettings();
        ApplyAudioSettings();
        ApplyControlSettings();
        ApplyInterfaceSettings();
        ApplyAdvancedSettings();
        ApplyBindings();
        // Bind conflict resolution can legitimately discard a duplicate key.
        // Re-read the authoritative table so the post-Apply UI and the new
        // Cancel baseline exactly match what the input system accepted.
        ReadBindings(m_draft);

        CVARS_SET("extended_draw_distance_enabled", m_draft.extendedWorldEnabled);
        CVARS_SET("extended_draw_distance", m_draft.extendedWorldDistance);
        CCore::GetSingleton().ApplyExtendedWorldDrawDistancePreferences();

        CVARS_SET("distant_lights_enabled", m_draft.distantLightsEnabled);
        CVARS_SET("distant_lights_draw_distance", m_draft.distantLightsDistance);
        CVARS_SET("distant_lights_corona_radius_multiplier", m_draft.distantLightsCoronaSize);
        if (CGame* game = g_pCore->GetGame())
        {
            CCoronas* coronas = game->GetCoronas();
            coronas->SetDistantLightsDrawDistance(static_cast<float>(m_draft.distantLightsDistance));
            coronas->SetDistantLightsCoronaRadiusMultiplier(m_draft.distantLightsCoronaSize);
            coronas->SetDistantLightsEnabled(m_draft.distantLightsEnabled);
        }

        if (!IsSkyGfxManaged())
        {
            m_draft.skyGfx.preset = SkyGfxMTAPreset::PlayStation2;
            SkyGfx::CManager::Get().SetConfig(m_draft.skyGfx, true);
        }

        if (!IsRadarManaged())
        {
            CVARS_SET("radar_style", m_draft.radarStyle);
            CVARS_SET("radar_position_x", m_draft.radarPositionX);
            CVARS_SET("radar_position_y", m_draft.radarPositionY);
            CVARS_SET("radar_width", m_draft.radarWidth);
            CVARS_SET("radar_height", m_draft.radarHeight);
            CVARS_SET("radar_widescreen_safe", m_draft.radarWidescreenSafe);
        }

        CGameSettings* gameSettings = CCore::GetSingleton().GetGame()->GetSettings();
        if (gameSettings)
        {
            const bool windowed = m_draft.graphicsDisplayMode == 0;
            const bool videoModeChanged =
                GetVideoModeManager()->SetVideoMode(m_draft.graphicsVideoMode, windowed, m_draft.graphicsFullscreenMinimize, m_draft.graphicsFullscreenStyle);
            const bool antiAliasingChanged = gameSettings->GetAntiAliasing() != static_cast<unsigned int>(m_draft.graphicsAntiAliasing);
            const bool dpiAwareChanged = CVARS_GET_VALUE<bool>("process_dpi_aware") != m_draft.graphicsDPIAware;
            const bool deviceSelectionChanged = (GetApplicationSettingInt("device-selection-disabled") == 0) != m_draft.graphicsDeviceSelectionDialog;

            gameSettings->SetAntiAliasing(m_draft.graphicsAntiAliasing, true);
            gameSettings->SetDrawDistance((static_cast<float>(m_draft.graphicsDrawDistance) / 100.0f * 0.875f) + 0.925f);
            gameSettings->SetBrightness(static_cast<unsigned int>(std::round(static_cast<float>(m_draft.graphicsBrightness) / 100.0f * 384.0f)));
            gameSettings->SetFXQuality(m_draft.graphicsFXQuality);

            CVARS_SET("process_dpi_aware", m_draft.graphicsDPIAware);
            CVARS_SET("fov", m_draft.graphicsFieldOfView);
            gameSettings->UpdateFieldOfViewFromSettings();
            CVARS_SET("anisotropic", m_draft.graphicsAnisotropic);
            CVARS_SET("aspect_ratio", m_draft.graphicsAspectRatio);
            CVARS_SET("hud_match_aspect_ratio", m_draft.graphicsHudMatchAspectRatio);
            gameSettings->SetAspectRatio(static_cast<eAspectRatio>(m_draft.graphicsAspectRatio), m_draft.graphicsHudMatchAspectRatio);

            CVARS_SET("volumetric_shadows", m_draft.graphicsVolumetricShadows);
            gameSettings->SetVolumetricShadowsEnabled(m_draft.graphicsVolumetricShadows);
            CVARS_SET("grass", m_draft.graphicsGrass);
            gameSettings->SetGrassEnabled(m_draft.graphicsGrass);
            CVARS_SET("heat_haze", m_draft.graphicsHeatHaze);
            CVARS_SET("tyre_smoke_enabled", m_draft.graphicsTyreSmoke);
            if (g_pCore->GetMultiplayer())
            {
                g_pCore->GetMultiplayer()->SetHeatHazeEnabled(m_draft.graphicsHeatHaze);
                g_pCore->GetMultiplayer()->SetTyreSmokeEnabled(m_draft.graphicsTyreSmoke);
            }

            CVARS_SET("high_detail_vehicles", m_draft.graphicsHighDetailVehicles);
            gameSettings->ResetVehiclesLODDistance(false);
            CVARS_SET("high_detail_peds", m_draft.graphicsHighDetailPeds);
            gameSettings->ResetPedsLODDistance(false);
            CVARS_SET("blur", m_draft.graphicsMotionBlur);
            gameSettings->ResetBlurEnabled();
            CVARS_SET("corona_reflections", m_draft.graphicsCoronaReflections);
            gameSettings->ResetCoronaReflectionsEnabled();
            CVARS_SET("dynamic_ped_shadows", m_draft.graphicsDynamicPedShadows);
            gameSettings->SetDynamicPedShadowsEnabled(m_draft.graphicsDynamicPedShadows);

            CVARS_SET("show_unsafe_resolutions", m_draft.graphicsShowUnsafeResolutions);
            SetApplicationSettingInt("device-selection-disabled", m_draft.graphicsDeviceSelectionDialog ? 0 : 1);
            ApplyVSyncPreview(m_draft);
            ApplyDisplayCalibrationPreview(m_draft);

            // Video mode, multisampling and process DPI awareness are consumed
            // during device creation, so applying them schedules one restart
            // instead of attempting an unsafe D3D reset from the web menu.
            m_restartRequired = m_restartRequired || videoModeChanged || antiAliasingChanged || dpiAwareChanged || deviceSelectionChanged;
            gameSettings->Save();
        }

        CClientVariables::GetSingleton().ValidateValues();
        CCore::GetSingleton().SaveConfig();
        // Requirements describe the current applied configuration relative to
        // the state that is actually running, not whether such a change ever
        // happened earlier in this Settings session. Reverting a change before
        // restarting/reconnecting therefore clears the corresponding prompt.
        m_restartRequired = !RestartSensitiveEqual(m_draft, m_restartBaseline);
        m_disconnectRequired = g_pCore->IsConnected() && !BrowserPermissionsEqual(m_draft, m_connectionBaseline);
        m_original = m_draft;
    }

    bool RefreshManagedValues()
    {
        bool       changed = false;
        const bool connected = g_pCore->IsConnected();
        if (connected != m_wasConnected)
        {
            changed = true;
            m_wasConnected = connected;
            if (!connected)
                m_disconnectRequired = false;
            else
            {
                m_connectionBaseline = ReadCurrentState();
                ReadBrowserLists(m_connectionBaseline);

                // Locale and skin cannot change while connected. If a
                // connection starts with either value still in the draft,
                // discard only those edits so Apply cannot report a rejected
                // value as successfully applied.
                m_draft.interfaceLocale = m_connectionBaseline.interfaceLocale;
                m_draft.interfaceSkin = m_connectionBaseline.interfaceSkin;
                m_original.interfaceLocale = m_connectionBaseline.interfaceLocale;
                m_original.interfaceSkin = m_connectionBaseline.interfaceSkin;
            }
        }
        const bool skyGfxManaged = IsSkyGfxManaged();
        changed = changed || skyGfxManaged != m_wasSkyGfxManaged;
        if (skyGfxManaged)
        {
            const auto runtimeConfig = SkyGfx::CManager::Get().GetConfig();
            if (std::memcmp(&m_draft.skyGfx, &runtimeConfig, sizeof(runtimeConfig)) != 0)
            {
                m_draft.skyGfx = runtimeConfig;
                changed = true;
            }
        }
        else if (m_wasSkyGfxManaged)
        {
            m_draft.skyGfx = SkyGfx::CManager::Get().GetUserConfig();
            m_original.skyGfx = m_draft.skyGfx;
        }
        m_wasSkyGfxManaged = skyGfxManaged;

        const bool radarManaged = IsRadarManaged();
        changed = changed || radarManaged != m_wasRadarManaged;
        if (radarManaged || m_wasRadarManaged)
        {
            const int    previousStyle = m_draft.radarStyle;
            const double previousX = m_draft.radarPositionX;
            const double previousY = m_draft.radarPositionY;
            const double previousWidth = m_draft.radarWidth;
            const double previousHeight = m_draft.radarHeight;
            const bool   previousWidescreenSafe = m_draft.radarWidescreenSafe;
            ReadRadarValues(m_draft);
            changed = changed || previousStyle != m_draft.radarStyle || previousX != m_draft.radarPositionX || previousY != m_draft.radarPositionY ||
                      previousWidth != m_draft.radarWidth || previousHeight != m_draft.radarHeight || previousWidescreenSafe != m_draft.radarWidescreenSafe;
        }
        if (!radarManaged && m_wasRadarManaged)
        {
            m_original.radarStyle = m_draft.radarStyle;
            m_original.radarPositionX = m_draft.radarPositionX;
            m_original.radarPositionY = m_draft.radarPositionY;
            m_original.radarWidth = m_draft.radarWidth;
            m_original.radarHeight = m_draft.radarHeight;
            m_original.radarWidescreenSafe = m_draft.radarWidescreenSafe;
        }
        m_wasRadarManaged = radarManaged;
        return changed;
    }

    const SState& GetState() const { return m_draft; }
    bool          IsActive() const { return m_active; }
    bool          IsSkyGfxManaged() const { return SkyGfx::CManager::Get().HasRuntimeOverrides(); }
    bool          IsRadarManaged() const
    {
        const CClientVariables& variables = CClientVariables::GetSingleton();
        return variables.HasRuntimeOverride("radar_style") || variables.HasRuntimeOverride("radar_position_x") ||
               variables.HasRuntimeOverride("radar_position_y") || variables.HasRuntimeOverride("radar_width") ||
               variables.HasRuntimeOverride("radar_height") || variables.HasRuntimeOverride("radar_widescreen_safe");
    }
    bool IsDistantLightsRebuildAvailable() const
    {
        return m_draft.distantLightsEnabled && CModManager::GetSingleton().IsLoaded() && g_pCore && g_pCore->GetGame() && g_pCore->GetGame()->GetCoronas();
    }

    int GetMaxAnisotropic() const { return g_pDeviceState ? std::max(0, g_pDeviceState->AdapterState.MaxAnisotropicSetting) : 0; }

    bool IsMultiMonitor() const { return GetVideoModeManager() && GetVideoModeManager()->IsMultiMonitor(); }
    bool HasUnsafeResolutions() const
    {
        CGame* game = g_pCore ? g_pCore->GetGame() : nullptr;
        return game && game->GetSettings() && game->GetSettings()->HasUnsafeResolutions();
    }
    bool RequiresRestart() const { return m_restartRequired; }

    std::vector<SResolution> GetResolutions() const
    {
        std::vector<SResolution> resolutions;
        CGame*                   game = g_pCore ? g_pCore->GetGame() : nullptr;
        CGameSettings*           settings = game ? game->GetSettings() : nullptr;
        if (!settings)
            return resolutions;

        VideoMode info;
        for (unsigned int mode = 0; mode < settings->GetNumVideoModes(); ++mode)
        {
            if (!settings->GetVideoModeInfo(&info, mode) || info.width < 640 || info.height < 480 || !(info.flags & rwVIDEOMODEEXCLUSIVE))
                continue;

            const bool unsafe = settings->IsUnsafeResolution(info.width, info.height);
            if (unsafe && !m_draft.graphicsShowUnsafeResolutions && static_cast<int>(mode) != m_draft.graphicsVideoMode)
                continue;

            const auto duplicate = std::find_if(resolutions.begin(), resolutions.end(), [&](const SResolution& value)
                                                { return value.width == info.width && value.height == info.height && value.depth == info.depth; });
            if (duplicate == resolutions.end())
                resolutions.push_back({static_cast<int>(mode), info.width, info.height, info.depth, unsafe});
            else if (static_cast<int>(mode) == m_draft.graphicsVideoMode)
                *duplicate = {static_cast<int>(mode), info.width, info.height, info.depth, unsafe};
        }
        std::sort(resolutions.begin(), resolutions.end(),
                  [](const SResolution& left, const SResolution& right)
                  {
                      if (left.width != right.width)
                          return left.width > right.width;
                      if (left.height != right.height)
                          return left.height > right.height;
                      return left.depth > right.depth;
                  });
        return resolutions;
    }

    bool IsDirty() const
    {
        const bool generalDirty =
            m_draft.extendedWorldEnabled != m_original.extendedWorldEnabled || m_draft.extendedWorldDistance != m_original.extendedWorldDistance ||
            m_draft.distantLightsEnabled != m_original.distantLightsEnabled || m_draft.distantLightsDistance != m_original.distantLightsDistance ||
            m_draft.distantLightsCoronaSize != m_original.distantLightsCoronaSize;
        const bool skyGfxDirty = !IsSkyGfxManaged() && std::memcmp(&m_draft.skyGfx, &m_original.skyGfx, sizeof(m_draft.skyGfx)) != 0;
        const bool radarDirty =
            !IsRadarManaged() && (m_draft.radarStyle != m_original.radarStyle || m_draft.radarPositionX != m_original.radarPositionX ||
                                  m_draft.radarPositionY != m_original.radarPositionY || m_draft.radarWidth != m_original.radarWidth ||
                                  m_draft.radarHeight != m_original.radarHeight || m_draft.radarWidescreenSafe != m_original.radarWidescreenSafe);
        return generalDirty || skyGfxDirty || radarDirty || !GraphicsEqual(m_draft, m_original) || !LegacyEqual(m_draft, m_original);
    }

    bool ProcessCapturedInput(UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (m_captureBindId.empty())
            return false;

        if (message == WM_KEYDOWN && wParam == VK_ESCAPE)
        {
            SetCapturedBindKey("");
            return true;
        }

        bool                state = false;
        const SBindableKey* key = CCore::GetSingleton().GetKeyBinds()->GetBindableFromMessage(message, wParam, lParam, state);
        if (!key || !state)
            return false;

        SetCapturedBindKey(key->szKey);
        return true;
    }

    bool UpdateInputCapture()
    {
        CJoystickManagerInterface* joystick = GetJoystickManager();
        if (m_captureAxis >= 0 && joystick && joystick->IsAxisBindComplete())
        {
            m_captureAxis = -1;
            return true;
        }
        return false;
    }

    const std::string& GetCapturedBindId() const { return m_captureBindId; }
    int                GetCapturedBindSlot() const { return m_captureBindSlot; }
    int                GetCapturedAxis() const { return m_captureAxis; }
    const std::string& GetError() const { return m_error; }
    bool               RequiresDisconnect() const { return m_disconnectRequired; }

    std::vector<std::string> GetLocales() const
    {
        const auto locales = g_pCore->GetLocalization()->GetAvailableLocales();
        return {locales.begin(), locales.end()};
    }

    std::vector<std::string>        GetSkins() const { return GetAvailableSkins(); }
    const std::vector<SChatPreset>& GetChatPresets() const { return m_chatPresets; }

    static bool HasCustomizedSAFilesOption() { return GetApplicationSettingInt("customized-sa-files-show") != 0; }

    const char* GetSkyGfxStatus() const
    {
        switch (SkyGfx::CManager::Get().GetStatus())
        {
            case SkyGfx::IntegrationStatus::Disabled:
                return "Disabled";
            case SkyGfx::IntegrationStatus::ModuleMissing:
                return "skygfx_mta.dll was not found";
            case SkyGfx::IntegrationStatus::ApiMismatch:
                return "Incompatible API version";
            case SkyGfx::IntegrationStatus::BridgeReady:
                return "Active";
            case SkyGfx::IntegrationStatus::Failed:
                return "Initialization failed";
        }
        return "Unavailable";
    }

private:
    static std::uint32_t PackColor(const CColor& color)
    {
        return (static_cast<std::uint32_t>(color.A) << 24) | (static_cast<std::uint32_t>(color.R) << 16) | (static_cast<std::uint32_t>(color.G) << 8) |
               static_cast<std::uint32_t>(color.B);
    }

    static CColor UnpackColor(std::uint32_t value)
    {
        return CColor(static_cast<unsigned char>((value >> 16) & 0xFF), static_cast<unsigned char>((value >> 8) & 0xFF),
                      static_cast<unsigned char>(value & 0xFF), static_cast<unsigned char>((value >> 24) & 0xFF));
    }

    static std::uint32_t ClampPackedAlpha(std::uint32_t value, unsigned char minimum)
    {
        const std::uint32_t alpha = std::max<std::uint32_t>(minimum, value >> 24);
        return (alpha << 24) | (value & 0x00FFFFFF);
    }

    static bool IsAvailableLocale(const std::string& locale)
    {
        const auto locales = g_pCore->GetLocalization()->GetAvailableLocales();
        return std::find(locales.begin(), locales.end(), locale) != locales.end();
    }

    static std::vector<std::string> GetAvailableSkins()
    {
        std::vector<std::string> skins;
        for (const SString& skin : FindFiles(CalcMTASAPath("skins/*"), false, true))
            skins.emplace_back(skin);
        if (skins.empty())
        {
            std::string current;
            CVARS_GET("current_skin", current);
            skins.push_back(current.empty() ? "Default" : current);
        }
        return skins;
    }

    static bool IsAvailableSkin(const std::string& skin)
    {
        const auto skins = GetAvailableSkins();
        return std::find(skins.begin(), skins.end(), skin) != skins.end();
    }

    static bool IsValidDomain(const std::string& domain)
    {
        if (domain.empty() || domain.size() > 253 || domain.front() == '.' || domain.back() == '.')
            return false;
        return std::all_of(domain.begin(), domain.end(), [](unsigned char c) { return std::isalnum(c) || c == '.' || c == '-' || c == '*'; });
    }

    static bool AddDomain(std::vector<std::string>& domains, std::string domain)
    {
        std::transform(domain.begin(), domain.end(), domain.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!IsValidDomain(domain) || std::find(domains.begin(), domains.end(), domain) != domains.end())
            return false;
        domains.push_back(std::move(domain));
        std::sort(domains.begin(), domains.end());
        return true;
    }

    static bool RemoveDomain(std::vector<std::string>& domains, const std::string& domain)
    {
        const auto found = std::find(domains.begin(), domains.end(), domain);
        if (found == domains.end())
            return false;
        domains.erase(found);
        return true;
    }

    void ReadChatPresets()
    {
        m_chatPresets.clear();
        const SString presetsPath = g_pCore->GetClientProfilePath(CHAT_PRESETS_PATH);
        SString       fullPresetsPath = CalcMTASAPath(presetsPath);
        if (g_pCore->IsSecondaryClient() && !FileExists(fullPresetsPath))
            fullPresetsPath = CalcMTASAPath(CHAT_PRESETS_PATH);
        CXMLFile* file = CCore::GetSingleton().GetXML()->CreateXML(fullPresetsPath);
        if (!file || !file->Parse())
        {
            if (file)
                CCore::GetSingleton().GetXML()->DeleteXML(file);
            return;
        }

        if (CXMLNode* root = file->GetRootNode())
        {
            unsigned int index = 0;
            for (auto iter = root->ChildrenBegin(); iter != root->ChildrenEnd(); ++iter)
            {
                CXMLNode* node = *iter;
                if (!node || node->GetTagName() != "preset")
                    continue;
                CXMLAttribute* name = node->GetAttributes().Find("name");
                if (!name || name->GetValue().size() < 2)
                    continue;

                SChatPreset preset;
                preset.id = std::to_string(index++);
                preset.name = name->GetValue();
                for (auto child = node->ChildrenBegin(); child != node->ChildrenEnd(); ++child)
                {
                    CXMLNode* value = *child;
                    if (value && !value->GetTagContent().empty())
                        preset.values[value->GetTagName()] = value->GetTagContent();
                }
                m_chatPresets.emplace_back(std::move(preset));
            }
        }
        CCore::GetSingleton().GetXML()->DeleteXML(file);
    }

    static bool ParsePresetColor(const std::string& value, std::uint32_t& packed)
    {
        std::istringstream stream(value);
        int                red{}, green{}, blue{}, alpha{};
        if (!(stream >> red >> green >> blue >> alpha))
            return false;
        packed = PackColor(CColor(static_cast<unsigned char>(std::clamp(red, 0, 255)), static_cast<unsigned char>(std::clamp(green, 0, 255)),
                                  static_cast<unsigned char>(std::clamp(blue, 0, 255)), static_cast<unsigned char>(std::clamp(alpha, 0, 255))));
        return true;
    }

    void ApplyChatPreset(const SChatPreset& preset)
    {
        const auto get = [&](const char* name) -> const std::string*
        {
            const auto found = preset.values.find(name);
            return found == preset.values.end() ? nullptr : &found->second;
        };
        const auto set = [&](const char* tag, const char* id)
        {
            if (const std::string* value = get(tag))
                SetLegacyValue(id, *value);
        };

        const std::pair<const char*, std::uint32_t*> colors[] = {
            {"color_text", &m_draft.interfaceChatTextColor},
            {"color_background", &m_draft.interfaceChatBackgroundColor},
            {"color_input_text", &m_draft.interfaceChatInputTextColor},
            {"color_input_background", &m_draft.interfaceChatInputBackgroundColor},
        };
        for (const auto& [tag, target] : colors)
            if (const std::string* value = get(tag))
                ParsePresetColor(*value, *target);
        m_draft.interfaceChatTextColor = ClampPackedAlpha(m_draft.interfaceChatTextColor, 128);
        m_draft.interfaceChatInputTextColor = ClampPackedAlpha(m_draft.interfaceChatInputTextColor, 128);

        set("font", "interface.chatFont");
        set("lines", "interface.chatLines");
        set("position_horizontal", "interface.chatPositionHorizontal");
        set("position_vertical", "interface.chatPositionVertical");
        set("text_alignment", "interface.chatTextAlignment");
        set("offset_x", "interface.chatOffsetX");
        set("offset_y", "interface.chatOffsetY");
        set("width", "interface.chatWidth");
        set("css_text", "interface.chatCssText");
        set("css_background", "interface.chatCssBackground");

        if (const std::string* scale = get("scale"))
        {
            std::istringstream stream(*scale);
            double             x{}, y{};
            if (stream >> x >> y)
            {
                m_draft.interfaceChatScaleX = QuantizeFloat(x, 0.5f, 3.0f, 0.1f);
                m_draft.interfaceChatScaleY = QuantizeFloat(y, 0.5f, 3.0f, 0.1f);
            }
        }
        if (const std::string* milliseconds = get("line_life"))
            SetLegacyValue("interface.chatLineLife", std::to_string(std::atof(milliseconds->c_str()) / 1000.0));
        if (const std::string* milliseconds = get("line_fadeout"))
            SetLegacyValue("interface.chatLineFadeOut", std::to_string(std::atof(milliseconds->c_str()) / 1000.0));
    }

    static void ReadBrowserLists(SState& state)
    {
        state.browserBlacklist.clear();
        state.browserWhitelist.clear();
        CWebCoreInterface* webCore = g_pCore->GetWebCore();
        if (!webCore)
            return;
        std::vector<std::pair<SString, bool>> entries;
        webCore->GetFilterEntriesByType(entries, eWebFilterType::WEBFILTER_USER);
        for (const auto& [domain, allowed] : entries)
            (allowed ? state.browserWhitelist : state.browserBlacklist).emplace_back(domain);
        std::sort(state.browserBlacklist.begin(), state.browserBlacklist.end());
        std::sort(state.browserWhitelist.begin(), state.browserWhitelist.end());
    }

    static void ReadBindings(SState& state)
    {
        state.binds.clear();
        CKeyBindsInterface* keyBinds = CCore::GetSingleton().GetKeyBinds();
        if (!keyBinds)
            return;

        for (int index = 0; g_bcControls[index].szControl[0] != '\0'; ++index)
        {
            SBindableGTAControl* control = &g_bcControls[index];
            SBindRow             row;
            row.id = std::string("gta:") + control->szControl;
            row.section = "GTA game controls";
            row.label = _(control->szDescription);
            row.control = control->szControl;
            row.gtaControl = true;
            std::list<CGTAControlBind*> matches;
            keyBinds->GetBoundControls(control, matches);
            for (CGTAControlBind* bind : matches)
                if (bind && !bind->isBeingDeleted && bind->boundKey && row.keys.size() < 4)
                    row.keys.emplace_back(bind->boundKey->szKey);
            state.binds.emplace_back(std::move(row));
        }

        std::map<std::string, std::size_t> commandRows;
        for (const CKeyBindsInterface::KeyBindPtr& bind : *keyBinds)
        {
            if (!bind || bind->isBeingDeleted || bind->type != KeyBindType::COMMAND)
                continue;
            const CCommandBind* command = static_cast<const CCommandBind*>(bind.get());
            if (!command->triggerState || !command->isActive || !command->boundKey)
                continue;
            const std::string identity = command->resource + "\x1f" + command->command + "\x1f" + command->arguments;
            auto              found = commandRows.find(identity);
            if (found == commandRows.end())
            {
                SBindRow row;
                row.id = "cmd:" + std::to_string(commandRows.size());
                row.section = command->resource.empty() ? "Multiplayer controls" : command->resource;
                row.label = command->arguments.empty() ? command->command : command->command + ": " + command->arguments;
                row.command = command->command;
                row.arguments = command->arguments;
                row.resource = command->resource;
                state.binds.emplace_back(std::move(row));
                found = commandRows.emplace(identity, state.binds.size() - 1).first;
            }
            SBindRow& row = state.binds[found->second];
            if (row.keys.size() < 4)
                row.keys.emplace_back(command->boundKey->szKey);
        }
    }

    void SetCapturedBindKey(const std::string& key)
    {
        const auto found = std::find_if(m_draft.binds.begin(), m_draft.binds.end(), [&](const SBindRow& row) { return row.id == m_captureBindId; });
        if (found != m_draft.binds.end() && m_captureBindSlot >= 0 && m_captureBindSlot < 4)
        {
            found->keys.resize(4);
            found->keys[m_captureBindSlot] = key;
            while (!found->keys.empty() && found->keys.back().empty())
                found->keys.pop_back();
        }
        m_captureBindId.clear();
        m_captureBindSlot = -1;
    }

    static void ApplyAudioPreview(const SState& state)
    {
        CGame*         game = g_pCore ? g_pCore->GetGame() : nullptr;
        CGameSettings* settings = game ? game->GetSettings() : nullptr;
        if (!settings)
            return;
        const float master = state.audioMasterVolume / 100.0f;
        CVARS_SET("mastervolume", master);
        CVARS_SET("mtavolume", state.audioMtaVolume / 100.0f);
        CVARS_SET("voicevolume", state.audioVoiceVolume / 100.0f);
        settings->SetRadioVolume(static_cast<unsigned char>(std::round(state.audioRadioVolume / 100.0f * master * 64.0f)));
        settings->SetSFXVolume(static_cast<unsigned char>(std::round(state.audioSfxVolume / 100.0f * master * 64.0f)));
    }

    void ApplyGameSettings()
    {
        std::string currentNickname;
        CVARS_GET("nick", currentNickname);
        if (currentNickname != m_draft.gameNickname)
        {
            if (CModManager::GetSingleton().IsLoaded())
                CCore::GetSingleton().GetCommands()->Execute("nick", m_draft.gameNickname.c_str());
            else
                CVARS_SET("nick", m_draft.gameNickname);
        }
        CVARS_SET("save_server_passwords", m_draft.gameSavePasswords);
        if (!m_draft.gameSavePasswords)
            CLocalGUI::GetSingleton().GetMainMenu()->GetServerBrowser()->ClearServerPasswords();
        CVARS_SET("auto_refresh_browser", m_draft.gameAutoRefreshBrowser);
        CVARS_SET("allow_screen_upload", m_draft.gameAllowScreenUpload);
        CVARS_SET("allow_external_sounds", m_draft.gameAllowExternalSounds);
        CVARS_SET("always_show_transferbox", m_draft.gameAlwaysShowTransferBox);
        g_pCore->GetModManager()->TriggerCommand(mtasa::CMD_ALWAYS_SHOW_TRANSFERBOX, m_draft.gameAlwaysShowTransferBox);
        const bool discordShareChanged = CVARS_GET_VALUE<bool>("discord_rpc_share_data") != m_draft.gameDiscordShareData;
        CVARS_SET("allow_discord_rpc", m_draft.gameDiscordRichPresence);
        CVARS_SET("discord_rpc_share_data", m_draft.gameDiscordShareData);
        if (g_pCore->GetDiscord())
        {
            // Consent is consulted when Discord supplies its user ID. Restart
            // an already-enabled session when that consent changes so the
            // stored identity immediately follows the new choice.
            if (m_draft.gameDiscordRichPresence && discordShareChanged)
                g_pCore->GetDiscord()->SetDiscordRPCEnabled(false);
            g_pCore->GetDiscord()->SetDiscordRPCEnabled(m_draft.gameDiscordRichPresence);
            if (m_draft.gameDiscordRichPresence)
            {
                const char* state = _("Main menu");
                if (g_pCore->IsConnected())
                {
                    state = _("In-game");
                    g_pCore->GetDiscord()->SetPresenceDetails(g_pCore->GetLastConnectedServerName().c_str(), false);
                }
                g_pCore->GetDiscord()->SetPresenceState(state, false);
            }
        }
        CVARS_SET("allow_steam_client", m_draft.gameSteamStatus);
        if (m_draft.gameSteamStatus && g_pCore->GetSteamClient())
            g_pCore->GetSteamClient()->Connect();
        CVARS_SET("photosaving", m_draft.gameSaveCameraPhotos);
        CScreenShot::SetPhotoSavingInsideDocuments(m_draft.gameSaveCameraPhotos);
        CVARS_SET("ask_before_disconnect", m_draft.gameAskBeforeDisconnect);
        const bool customizedChanged = (GetApplicationSettingInt("customized-sa-files-request") != 0) != m_draft.gameCustomizedSAFiles;
        SetApplicationSettingInt("customized-sa-files-request", m_draft.gameCustomizedSAFiles ? 1 : 0);
        m_restartRequired = m_restartRequired || customizedChanged;
        CVARS_SET("mapalpha", m_draft.gameMapAlpha);
        CVARS_SET("mapimage", m_draft.gameMapImage);
    }

    void ApplyAudioSettings()
    {
        ApplyAudioPreview(m_draft);
        CVARS_SET("radiovolume", m_draft.audioRadioVolume / 100.0f);
        CVARS_SET("sfxvolume", m_draft.audioSfxVolume / 100.0f);
        CVARS_SET("mute_master_when_minimized", m_draft.audioMuteMaster);
        CVARS_SET("mute_radio_when_minimized", m_draft.audioMuteRadio);
        CVARS_SET("mute_sfx_when_minimized", m_draft.audioMuteSfx);
        CVARS_SET("mute_mta_when_minimized", m_draft.audioMuteMta);
        CVARS_SET("mute_voice_when_minimized", m_draft.audioMuteVoice);
        CGameSettings* settings = CCore::GetSingleton().GetGame()->GetSettings();
        settings->SetRadioEqualizerEnabled(m_draft.audioRadioEqualizer);
        settings->SetRadioAutotuneEnabled(m_draft.audioRadioAutotune);
        settings->SetUsertrackAutoScan(m_draft.audioUserTrackAutoScan);
        settings->SetUsertrackMode(m_draft.audioUserTrackMode);
    }

    void ApplyControlSettings()
    {
        CGameSettings*            settings = CCore::GetSingleton().GetGame()->GetSettings();
        CControllerConfigManager* controller = g_pCore->GetGame()->GetControllerConfigManager();
        CVARS_SET("invert_mouse", m_draft.controlsInvertMouse);
        CVARS_SET("steer_with_mouse", m_draft.controlsSteerWithMouse);
        CVARS_SET("fly_with_mouse", m_draft.controlsFlyWithMouse);
        CVARS_SET("classic_controls", m_draft.controlsClassic);
        controller->SetMouseInverted(m_draft.controlsInvertMouse);
        controller->SetSteerWithMouse(m_draft.controlsSteerWithMouse);
        controller->SetFlyWithMouse(m_draft.controlsFlyWithMouse);
        controller->SetClassicControls(m_draft.controlsClassic);
        settings->SetMouseSensitivity(m_draft.controlsMouseSensitivity / 100.0f);
        controller->SetVerticalAimSensitivity(m_draft.controlsVerticalAimSensitivity / 100.0f);
        CVARS_SET("vertical_aim_sensitivity", controller->GetVerticalAimSensitivityRawValue());
        CVARS_SET("use_mouse_sensitivity_for_aiming", m_draft.controlsUseMouseSensitivityForAiming);
        controller->SetVerticalAimSensitivitySameAsHorizontal(m_draft.controlsUseMouseSensitivityForAiming);
        GetJoystickManager()->SetDeadZone(m_draft.controlsJoypadDeadZone);
        GetJoystickManager()->SetSaturation(m_draft.controlsJoypadSaturation);
        GetJoystickManager()->SaveToXML();
    }

    void ApplyInterfaceSettings()
    {
        if (!g_pCore->IsConnected())
        {
            const std::string currentLocale = CVARS_GET_VALUE<std::string>("locale");
            if (currentLocale != m_draft.interfaceLocale)
                CLocalGUI::GetSingleton().RequestLocaleChange(m_draft.interfaceLocale);
            CVARS_SET("current_skin", m_draft.interfaceSkin);
        }
        else
        {
            // A connection can complete between the web edit and Apply. Keep
            // the draft/baseline authoritative instead of persisting values
            // that CLocalGUI must reject while a mod is loaded.
            CVARS_GET("locale", m_draft.interfaceLocale);
            CVARS_GET("current_skin", m_draft.interfaceSkin);
        }
        CVARS_SET("chat_color", UnpackColor(m_draft.interfaceChatBackgroundColor));
        CVARS_SET("chat_text_color", UnpackColor(m_draft.interfaceChatTextColor));
        CVARS_SET("chat_input_color", UnpackColor(m_draft.interfaceChatInputBackgroundColor));
        CVARS_SET("chat_input_text_color", UnpackColor(m_draft.interfaceChatInputTextColor));
        CVARS_SET("chat_font", m_draft.interfaceChatFont);
        CVARS_SET("chat_lines", m_draft.interfaceChatLines);
        const CVector2D chatScale(m_draft.interfaceChatScaleX, m_draft.interfaceChatScaleY);
        CVARS_SET("chat_scale", chatScale);
        CVARS_SET("chat_width", m_draft.interfaceChatWidth);
        CVARS_SET("chat_css_style_text", m_draft.interfaceChatCssText);
        CVARS_SET("chat_css_style_background", m_draft.interfaceChatCssBackground);
        CVARS_SET("chat_nickcompletion", m_draft.interfaceChatNickCompletion);
        CVARS_SET("chat_text_outline", m_draft.interfaceChatTextOutline);
        CVARS_SET("chat_line_life", static_cast<int>(std::round(m_draft.interfaceChatLineLife * 1000.0f)));
        CVARS_SET("chat_line_fade_out", static_cast<int>(std::round(m_draft.interfaceChatLineFadeOut * 1000.0f)));
        CVARS_SET("chat_position_horizontal", m_draft.interfaceChatPositionHorizontal);
        CVARS_SET("chat_position_vertical", m_draft.interfaceChatPositionVertical);
        CVARS_SET("chat_text_alignment", m_draft.interfaceChatTextAlignment);
        CVARS_SET("chat_position_offset_x", m_draft.interfaceChatOffsetX);
        CVARS_SET("chat_position_offset_y", m_draft.interfaceChatOffsetY);
        CVARS_SET("server_can_flash_window", m_draft.interfaceFlashWindow);
        CVARS_SET("allow_tray_notifications", m_draft.interfaceTrayNotifications);

        const bool permissionsChanged = CVARS_GET_VALUE<bool>("browser_remote_websites") != m_draft.browserRemoteWebsites ||
                                        CVARS_GET_VALUE<bool>("browser_remote_javascript") != m_draft.browserRemoteJavascript ||
                                        m_draft.browserBlacklist != m_original.browserBlacklist || m_draft.browserWhitelist != m_original.browserWhitelist;
        CVARS_SET("browser_remote_websites", m_draft.browserRemoteWebsites);
        CVARS_SET("browser_remote_javascript", m_draft.browserRemoteJavascript);
        if (CWebCoreInterface* webCore = g_pCore->GetWebCore())
        {
            std::vector<SString> blacklist(m_draft.browserBlacklist.begin(), m_draft.browserBlacklist.end());
            std::vector<SString> whitelist(m_draft.browserWhitelist.begin(), m_draft.browserWhitelist.end());
            webCore->WriteCustomList("customblacklist", blacklist);
            webCore->WriteCustomList("customwhitelist", whitelist);
        }
        m_disconnectRequired = m_disconnectRequired || (g_pCore->IsConnected() && permissionsChanged);
        const bool gpuChanged = CVARS_GET_VALUE<bool>("browser_enable_gpu") != m_draft.browserGpuRendering;
        const bool videoChanged = CVARS_GET_VALUE<bool>("browser_enable_video_acceleration") != m_draft.browserVideoAcceleration;
        CVARS_SET("browser_enable_gpu", m_draft.browserGpuRendering);
        CVARS_SET("browser_enable_video_acceleration", m_draft.browserVideoAcceleration);
        m_restartRequired = m_restartRequired || gpuChanged || videoChanged;
    }

    void ApplyAdvancedSettings()
    {
        CVARS_SET("fast_clothes_loading", m_draft.advancedFastClothesLoading);
        if (g_pCore->GetMultiplayer())
            g_pCore->GetMultiplayer()->SetFastClothesLoading(static_cast<CMultiplayer::EFastClothesLoading>(m_draft.advancedFastClothesLoading));
        CVARS_SET("browser_speed", m_draft.advancedBrowserSpeed);
        CVARS_SET("single_download", m_draft.advancedSingleConnection);
        CVARS_SET("packet_tag", m_draft.advancedPacketTag);
        CVARS_SET("progress_animation", m_draft.advancedProgressAnimation);
        // Keep this defensive clamp next to the array access as well as in
        // global CVar validation; Settings can be opened with a hand-edited or
        // historical coreconfig before the user changes this field.
        m_draft.advancedProcessPriority = std::clamp(m_draft.advancedProcessPriority, 0, 2);
        CVARS_SET("process_priority", m_draft.advancedProcessPriority);
        const DWORD priorities[] = {NORMAL_PRIORITY_CLASS, ABOVE_NORMAL_PRIORITY_CLASS, HIGH_PRIORITY_CLASS};
        SetPriorityClass(GetCurrentProcess(), priorities[m_draft.advancedProcessPriority]);
        g_pCore->SetDiagnosticDebug(static_cast<EDiagnosticDebugType>(m_draft.advancedDebugSetting));
        CVARS_SET("streaming_memory", m_draft.advancedStreamingMemory);
        CVARS_SET("process_cpu_affinity", m_draft.advancedCpuAffinity);
        DWORD_PTR processMask = 0;
        DWORD_PTR systemMask = 0;
        if (GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask))
        {
            DWORD_PTR desiredMask = m_draft.advancedCpuAffinity ? (systemMask & ~static_cast<DWORD_PTR>(1)) : systemMask;
            if (desiredMask != 0)
                SetProcessAffinityMask(GetCurrentProcess(), desiredMask);
        }
        CVARS_SET("update_build_type", m_draft.advancedUpdateBuildType);
        CVARS_SET("update_auto_install", m_draft.advancedUpdateAutoInstall);
    }

    void ApplyBindings()
    {
        CKeyBindsInterface* keyBinds = CCore::GetSingleton().GetKeyBinds();
        for (const SBindRow& row : m_draft.binds)
        {
            if (row.gtaControl)
            {
                SBindableGTAControl* control = keyBinds->GetBindableFromControl(row.control.c_str());
                if (!control)
                    continue;
                std::list<CGTAControlBind*> existingList;
                keyBinds->GetBoundControls(control, existingList);
                std::vector<CGTAControlBind*> existing(existingList.begin(), existingList.end());
                for (std::size_t slot = 0; slot < 4; ++slot)
                {
                    const std::string   desired = slot < row.keys.size() ? row.keys[slot] : std::string{};
                    const SBindableKey* key = desired.empty() ? nullptr : keyBinds->GetBindableFromKey(desired.c_str());
                    if (slot < existing.size() && existing[slot])
                    {
                        if (!key)
                            keyBinds->Remove(existing[slot]);
                        else if (existing[slot]->boundKey != key)
                        {
                            if (slot == 0 || !keyBinds->GTAControlExists(key, control))
                                existing[slot]->boundKey = key;
                            else
                                keyBinds->Remove(existing[slot]);
                        }
                    }
                    else if (key && !keyBinds->GTAControlExists(key, control))
                        keyBinds->AddGTAControl(key, control);
                }
                continue;
            }

            std::vector<CCommandBind*> existing;
            for (const CKeyBindsInterface::KeyBindPtr& bind : *keyBinds)
            {
                if (!bind || bind->isBeingDeleted || bind->type != KeyBindType::COMMAND)
                    continue;
                CCommandBind* command = static_cast<CCommandBind*>(bind.get());
                if (command->triggerState && command->command == row.command && command->arguments == row.arguments && command->resource == row.resource)
                    existing.push_back(command);
            }

            // Some commands are represented by a down/up pair. Remember the
            // release-side metadata before editing the down binds so a newly
            // filled secondary slot keeps the same press/release semantics as
            // the mature CEGUI editor.
            bool        hasMatchingUp = false;
            std::string matchingUpArguments;
            std::string matchingUpResource;
            for (CCommandBind* command : existing)
            {
                if (CCommandBind* up = keyBinds->FindMatchingUpBind(command))
                {
                    hasMatchingUp = true;
                    matchingUpArguments = up->arguments;
                    matchingUpResource = up->resource;
                    break;
                }
            }
            for (std::size_t slot = 0; slot < 4; ++slot)
            {
                const std::string   desired = slot < row.keys.size() ? row.keys[slot] : std::string{};
                const SBindableKey* key = desired.empty() ? nullptr : keyBinds->GetBindableFromKey(desired.c_str());
                if (slot < existing.size() && existing[slot])
                {
                    if (!key)
                    {
                        if (CCommandBind* up = keyBinds->FindMatchingUpBind(existing[slot]))
                            keyBinds->UserRemoveCommandBoundKey(up);
                        keyBinds->UserRemoveCommandBoundKey(existing[slot]);
                    }
                    else if (existing[slot]->boundKey != key)
                    {
                        if (CCommandBind* up = keyBinds->FindMatchingUpBind(existing[slot]))
                            keyBinds->UserChangeCommandBoundKey(up, key);
                        keyBinds->UserChangeCommandBoundKey(existing[slot], key);
                    }
                }
                else if (key)
                {
                    keyBinds->AddCommand(key->szKey, row.command.c_str(), row.arguments.empty() ? nullptr : row.arguments.c_str(), true,
                                         row.resource.empty() ? nullptr : row.resource.c_str());
                    if (hasMatchingUp)
                        keyBinds->AddCommand(key->szKey, row.command.c_str(), matchingUpArguments.empty() ? nullptr : matchingUpArguments.c_str(), false,
                                             matchingUpResource.empty() ? nullptr : matchingUpResource.c_str());
                }
            }
        }
    }

    static bool IsVideoModeAvailable(int requestedMode)
    {
        CGame*         game = g_pCore ? g_pCore->GetGame() : nullptr;
        CGameSettings* settings = game ? game->GetSettings() : nullptr;
        VideoMode      info;
        return requestedMode >= 0 && settings && settings->GetVideoModeInfo(&info, static_cast<unsigned int>(requestedMode)) && info.width >= 640 &&
               info.height >= 480 && (info.flags & rwVIDEOMODEEXCLUSIVE);
    }

    static void RefreshDisplayCalibration()
    {
        CScopedActiveProxyDevice proxyDevice;
        if (proxyDevice)
            proxyDevice->ApplyBorderlessPresentationTuning();
    }

    static void ApplyVSyncPreview(const SState& state)
    {
        if (g_pCore && g_pCore->GetFPSLimiter())
            g_pCore->GetFPSLimiter()->SetDisplayVSync(state.graphicsVSync);
    }

    static void ApplyDisplayCalibrationPreview(const SState& state)
    {
        CVARS_SET("borderless_gamma_power", state.graphicsGamma);
        CVARS_SET("borderless_brightness_scale", state.graphicsBrightnessScale);
        CVARS_SET("borderless_contrast_scale", state.graphicsContrast);
        CVARS_SET("borderless_saturation_scale", state.graphicsSaturation);
        CVARS_SET("borderless_gamma_enabled", state.graphicsGammaEnabled);
        CVARS_SET("borderless_brightness_enabled", state.graphicsBrightnessEnabled);
        CVARS_SET("borderless_contrast_enabled", state.graphicsContrastEnabled);
        CVARS_SET("borderless_saturation_enabled", state.graphicsSaturationEnabled);
        CVARS_SET("borderless_apply_windowed", state.graphicsApplyWindowed);
        CVARS_SET("borderless_apply_fullscreen", state.graphicsApplyFullscreen);
        RefreshDisplayCalibration();
    }

    static void CopyGraphics(const SState& source, SState& destination)
    {
        destination.graphicsVideoMode = source.graphicsVideoMode;
        destination.graphicsDisplayMode = source.graphicsDisplayMode;
        destination.graphicsFullscreenStyle = source.graphicsFullscreenStyle;
        destination.graphicsFullscreenMinimize = source.graphicsFullscreenMinimize;
        destination.graphicsVSync = source.graphicsVSync;
        destination.graphicsDPIAware = source.graphicsDPIAware;
        destination.graphicsFieldOfView = source.graphicsFieldOfView;
        destination.graphicsDrawDistance = source.graphicsDrawDistance;
        destination.graphicsBrightness = source.graphicsBrightness;
        destination.graphicsFXQuality = source.graphicsFXQuality;
        destination.graphicsAnisotropic = source.graphicsAnisotropic;
        destination.graphicsAntiAliasing = source.graphicsAntiAliasing;
        destination.graphicsAspectRatio = source.graphicsAspectRatio;
        destination.graphicsHudMatchAspectRatio = source.graphicsHudMatchAspectRatio;
        destination.graphicsVolumetricShadows = source.graphicsVolumetricShadows;
        destination.graphicsGrass = source.graphicsGrass;
        destination.graphicsHeatHaze = source.graphicsHeatHaze;
        destination.graphicsTyreSmoke = source.graphicsTyreSmoke;
        destination.graphicsDynamicPedShadows = source.graphicsDynamicPedShadows;
        destination.graphicsMotionBlur = source.graphicsMotionBlur;
        destination.graphicsCoronaReflections = source.graphicsCoronaReflections;
        destination.graphicsHighDetailVehicles = source.graphicsHighDetailVehicles;
        destination.graphicsHighDetailPeds = source.graphicsHighDetailPeds;
        destination.graphicsShowUnsafeResolutions = source.graphicsShowUnsafeResolutions;
        destination.graphicsDeviceSelectionDialog = source.graphicsDeviceSelectionDialog;
        destination.graphicsGamma = source.graphicsGamma;
        destination.graphicsBrightnessScale = source.graphicsBrightnessScale;
        destination.graphicsContrast = source.graphicsContrast;
        destination.graphicsSaturation = source.graphicsSaturation;
        destination.graphicsGammaEnabled = source.graphicsGammaEnabled;
        destination.graphicsBrightnessEnabled = source.graphicsBrightnessEnabled;
        destination.graphicsContrastEnabled = source.graphicsContrastEnabled;
        destination.graphicsSaturationEnabled = source.graphicsSaturationEnabled;
        destination.graphicsApplyWindowed = source.graphicsApplyWindowed;
        destination.graphicsApplyFullscreen = source.graphicsApplyFullscreen;
    }

    static bool GraphicsEqual(const SState& left, const SState& right)
    {
        SState copy = left;
        CopyGraphics(right, copy);
        return copy.graphicsVideoMode == left.graphicsVideoMode && copy.graphicsDisplayMode == left.graphicsDisplayMode &&
               copy.graphicsFullscreenStyle == left.graphicsFullscreenStyle && copy.graphicsFullscreenMinimize == left.graphicsFullscreenMinimize &&
               copy.graphicsVSync == left.graphicsVSync && copy.graphicsDPIAware == left.graphicsDPIAware &&
               copy.graphicsFieldOfView == left.graphicsFieldOfView && copy.graphicsDrawDistance == left.graphicsDrawDistance &&
               copy.graphicsBrightness == left.graphicsBrightness && copy.graphicsFXQuality == left.graphicsFXQuality &&
               copy.graphicsAnisotropic == left.graphicsAnisotropic && copy.graphicsAntiAliasing == left.graphicsAntiAliasing &&
               copy.graphicsAspectRatio == left.graphicsAspectRatio && copy.graphicsHudMatchAspectRatio == left.graphicsHudMatchAspectRatio &&
               copy.graphicsVolumetricShadows == left.graphicsVolumetricShadows && copy.graphicsGrass == left.graphicsGrass &&
               copy.graphicsHeatHaze == left.graphicsHeatHaze && copy.graphicsTyreSmoke == left.graphicsTyreSmoke &&
               copy.graphicsDynamicPedShadows == left.graphicsDynamicPedShadows && copy.graphicsMotionBlur == left.graphicsMotionBlur &&
               copy.graphicsCoronaReflections == left.graphicsCoronaReflections && copy.graphicsHighDetailVehicles == left.graphicsHighDetailVehicles &&
               copy.graphicsHighDetailPeds == left.graphicsHighDetailPeds && copy.graphicsShowUnsafeResolutions == left.graphicsShowUnsafeResolutions &&
               copy.graphicsDeviceSelectionDialog == left.graphicsDeviceSelectionDialog && copy.graphicsGamma == left.graphicsGamma &&
               copy.graphicsBrightnessScale == left.graphicsBrightnessScale && copy.graphicsContrast == left.graphicsContrast &&
               copy.graphicsSaturation == left.graphicsSaturation && copy.graphicsGammaEnabled == left.graphicsGammaEnabled &&
               copy.graphicsBrightnessEnabled == left.graphicsBrightnessEnabled && copy.graphicsContrastEnabled == left.graphicsContrastEnabled &&
               copy.graphicsSaturationEnabled == left.graphicsSaturationEnabled && copy.graphicsApplyWindowed == left.graphicsApplyWindowed &&
               copy.graphicsApplyFullscreen == left.graphicsApplyFullscreen;
    }

    static void CopyGame(const SState& source, SState& destination)
    {
        destination.gameNickname = source.gameNickname;
        destination.gameSavePasswords = source.gameSavePasswords;
        destination.gameAutoRefreshBrowser = source.gameAutoRefreshBrowser;
        destination.gameAllowScreenUpload = source.gameAllowScreenUpload;
        destination.gameAllowExternalSounds = source.gameAllowExternalSounds;
        destination.gameAlwaysShowTransferBox = source.gameAlwaysShowTransferBox;
        destination.gameDiscordRichPresence = source.gameDiscordRichPresence;
        destination.gameDiscordShareData = source.gameDiscordShareData;
        destination.gameSteamStatus = source.gameSteamStatus;
        destination.gameSaveCameraPhotos = source.gameSaveCameraPhotos;
        destination.gameAskBeforeDisconnect = source.gameAskBeforeDisconnect;
        destination.gameCustomizedSAFiles = source.gameCustomizedSAFiles;
        destination.gameMapOpacity = source.gameMapOpacity;
        destination.gameMapAlpha = source.gameMapAlpha;
        destination.gameMapImage = source.gameMapImage;
    }

    static void CopyAudio(const SState& source, SState& destination)
    {
        destination.audioMasterVolume = source.audioMasterVolume;
        destination.audioRadioVolume = source.audioRadioVolume;
        destination.audioSfxVolume = source.audioSfxVolume;
        destination.audioMtaVolume = source.audioMtaVolume;
        destination.audioVoiceVolume = source.audioVoiceVolume;
        destination.audioRadioEqualizer = source.audioRadioEqualizer;
        destination.audioRadioAutotune = source.audioRadioAutotune;
        destination.audioUserTrackAutoScan = source.audioUserTrackAutoScan;
        destination.audioUserTrackMode = source.audioUserTrackMode;
        destination.audioMuteMaster = source.audioMuteMaster;
        destination.audioMuteRadio = source.audioMuteRadio;
        destination.audioMuteSfx = source.audioMuteSfx;
        destination.audioMuteMta = source.audioMuteMta;
        destination.audioMuteVoice = source.audioMuteVoice;
    }

    static void CopyControls(const SState& source, SState& destination)
    {
        destination.controlsInvertMouse = source.controlsInvertMouse;
        destination.controlsSteerWithMouse = source.controlsSteerWithMouse;
        destination.controlsFlyWithMouse = source.controlsFlyWithMouse;
        destination.controlsMouseSensitivity = source.controlsMouseSensitivity;
        destination.controlsVerticalAimSensitivity = source.controlsVerticalAimSensitivity;
        destination.controlsUseMouseSensitivityForAiming = source.controlsUseMouseSensitivityForAiming;
        destination.controlsClassic = source.controlsClassic;
        destination.controlsJoypadDeadZone = source.controlsJoypadDeadZone;
        destination.controlsJoypadSaturation = source.controlsJoypadSaturation;
    }

    static void CopyInterface(const SState& source, SState& destination)
    {
        destination.interfaceLocale = source.interfaceLocale;
        destination.interfaceSkin = source.interfaceSkin;
        destination.interfaceChatBackgroundColor = source.interfaceChatBackgroundColor;
        destination.interfaceChatTextColor = source.interfaceChatTextColor;
        destination.interfaceChatInputBackgroundColor = source.interfaceChatInputBackgroundColor;
        destination.interfaceChatInputTextColor = source.interfaceChatInputTextColor;
        destination.interfaceChatFont = source.interfaceChatFont;
        destination.interfaceChatLines = source.interfaceChatLines;
        destination.interfaceChatScaleX = source.interfaceChatScaleX;
        destination.interfaceChatScaleY = source.interfaceChatScaleY;
        destination.interfaceChatWidth = source.interfaceChatWidth;
        destination.interfaceChatCssText = source.interfaceChatCssText;
        destination.interfaceChatCssBackground = source.interfaceChatCssBackground;
        destination.interfaceChatNickCompletion = source.interfaceChatNickCompletion;
        destination.interfaceChatTextOutline = source.interfaceChatTextOutline;
        destination.interfaceChatLineLife = source.interfaceChatLineLife;
        destination.interfaceChatLineFadeOut = source.interfaceChatLineFadeOut;
        destination.interfaceChatPositionHorizontal = source.interfaceChatPositionHorizontal;
        destination.interfaceChatPositionVertical = source.interfaceChatPositionVertical;
        destination.interfaceChatTextAlignment = source.interfaceChatTextAlignment;
        destination.interfaceChatOffsetX = source.interfaceChatOffsetX;
        destination.interfaceChatOffsetY = source.interfaceChatOffsetY;
        destination.interfaceFlashWindow = source.interfaceFlashWindow;
        destination.interfaceTrayNotifications = source.interfaceTrayNotifications;
        destination.browserRemoteWebsites = source.browserRemoteWebsites;
        destination.browserRemoteJavascript = source.browserRemoteJavascript;
        destination.browserGpuRendering = source.browserGpuRendering;
        destination.browserVideoAcceleration = source.browserVideoAcceleration;
        destination.browserBlacklist = source.browserBlacklist;
        destination.browserWhitelist = source.browserWhitelist;
    }

    static void CopyAdvanced(const SState& source, SState& destination)
    {
        destination.advancedFastClothesLoading = source.advancedFastClothesLoading;
        destination.advancedBrowserSpeed = source.advancedBrowserSpeed;
        destination.advancedSingleConnection = source.advancedSingleConnection;
        destination.advancedPacketTag = source.advancedPacketTag;
        destination.advancedProgressAnimation = source.advancedProgressAnimation;
        destination.advancedProcessPriority = source.advancedProcessPriority;
        destination.advancedDebugSetting = source.advancedDebugSetting;
        destination.advancedStreamingMemory = source.advancedStreamingMemory;
        destination.advancedCpuAffinity = source.advancedCpuAffinity;
        destination.advancedUpdateBuildType = source.advancedUpdateBuildType;
        destination.advancedUpdateAutoInstall = source.advancedUpdateAutoInstall;
    }

    static bool BindingsEqual(const std::vector<SBindRow>& left, const std::vector<SBindRow>& right)
    {
        if (left.size() != right.size())
            return false;
        for (std::size_t index = 0; index < left.size(); ++index)
            if (left[index].id != right[index].id || left[index].keys != right[index].keys)
                return false;
        return true;
    }

    static bool LegacyEqual(const SState& left, const SState& right)
    {
        SState copy = left;
        CopyGame(right, copy);
        CopyAudio(right, copy);
        CopyControls(right, copy);
        CopyInterface(right, copy);
        CopyAdvanced(right, copy);
        return copy.gameNickname == left.gameNickname && copy.gameSavePasswords == left.gameSavePasswords &&
               copy.gameAutoRefreshBrowser == left.gameAutoRefreshBrowser && copy.gameAllowScreenUpload == left.gameAllowScreenUpload &&
               copy.gameAllowExternalSounds == left.gameAllowExternalSounds && copy.gameAlwaysShowTransferBox == left.gameAlwaysShowTransferBox &&
               copy.gameDiscordRichPresence == left.gameDiscordRichPresence && copy.gameDiscordShareData == left.gameDiscordShareData &&
               copy.gameSteamStatus == left.gameSteamStatus && copy.gameSaveCameraPhotos == left.gameSaveCameraPhotos &&
               copy.gameAskBeforeDisconnect == left.gameAskBeforeDisconnect && copy.gameCustomizedSAFiles == left.gameCustomizedSAFiles &&
               copy.gameMapOpacity == left.gameMapOpacity && copy.gameMapAlpha == left.gameMapAlpha && copy.gameMapImage == left.gameMapImage &&
               copy.audioMasterVolume == left.audioMasterVolume && copy.audioRadioVolume == left.audioRadioVolume &&
               copy.audioSfxVolume == left.audioSfxVolume && copy.audioMtaVolume == left.audioMtaVolume && copy.audioVoiceVolume == left.audioVoiceVolume &&
               copy.audioRadioEqualizer == left.audioRadioEqualizer && copy.audioRadioAutotune == left.audioRadioAutotune &&
               copy.audioUserTrackAutoScan == left.audioUserTrackAutoScan && copy.audioUserTrackMode == left.audioUserTrackMode &&
               copy.audioMuteMaster == left.audioMuteMaster && copy.audioMuteRadio == left.audioMuteRadio && copy.audioMuteSfx == left.audioMuteSfx &&
               copy.audioMuteMta == left.audioMuteMta && copy.audioMuteVoice == left.audioMuteVoice && copy.controlsInvertMouse == left.controlsInvertMouse &&
               copy.controlsSteerWithMouse == left.controlsSteerWithMouse && copy.controlsFlyWithMouse == left.controlsFlyWithMouse &&
               copy.controlsMouseSensitivity == left.controlsMouseSensitivity && copy.controlsVerticalAimSensitivity == left.controlsVerticalAimSensitivity &&
               copy.controlsUseMouseSensitivityForAiming == left.controlsUseMouseSensitivityForAiming && copy.controlsClassic == left.controlsClassic &&
               copy.controlsJoypadDeadZone == left.controlsJoypadDeadZone && copy.controlsJoypadSaturation == left.controlsJoypadSaturation &&
               copy.interfaceLocale == left.interfaceLocale && copy.interfaceSkin == left.interfaceSkin &&
               copy.interfaceChatBackgroundColor == left.interfaceChatBackgroundColor && copy.interfaceChatTextColor == left.interfaceChatTextColor &&
               copy.interfaceChatInputBackgroundColor == left.interfaceChatInputBackgroundColor &&
               copy.interfaceChatInputTextColor == left.interfaceChatInputTextColor && copy.interfaceChatFont == left.interfaceChatFont &&
               copy.interfaceChatLines == left.interfaceChatLines && copy.interfaceChatScaleX == left.interfaceChatScaleX &&
               copy.interfaceChatScaleY == left.interfaceChatScaleY && copy.interfaceChatWidth == left.interfaceChatWidth &&
               copy.interfaceChatCssText == left.interfaceChatCssText && copy.interfaceChatCssBackground == left.interfaceChatCssBackground &&
               copy.interfaceChatNickCompletion == left.interfaceChatNickCompletion && copy.interfaceChatTextOutline == left.interfaceChatTextOutline &&
               copy.interfaceChatLineLife == left.interfaceChatLineLife && copy.interfaceChatLineFadeOut == left.interfaceChatLineFadeOut &&
               copy.interfaceChatPositionHorizontal == left.interfaceChatPositionHorizontal &&
               copy.interfaceChatPositionVertical == left.interfaceChatPositionVertical && copy.interfaceChatTextAlignment == left.interfaceChatTextAlignment &&
               copy.interfaceChatOffsetX == left.interfaceChatOffsetX && copy.interfaceChatOffsetY == left.interfaceChatOffsetY &&
               copy.interfaceFlashWindow == left.interfaceFlashWindow && copy.interfaceTrayNotifications == left.interfaceTrayNotifications &&
               copy.browserRemoteWebsites == left.browserRemoteWebsites && copy.browserRemoteJavascript == left.browserRemoteJavascript &&
               copy.browserGpuRendering == left.browserGpuRendering && copy.browserVideoAcceleration == left.browserVideoAcceleration &&
               copy.browserBlacklist == left.browserBlacklist && copy.browserWhitelist == left.browserWhitelist &&
               copy.advancedFastClothesLoading == left.advancedFastClothesLoading && copy.advancedBrowserSpeed == left.advancedBrowserSpeed &&
               copy.advancedSingleConnection == left.advancedSingleConnection && copy.advancedPacketTag == left.advancedPacketTag &&
               copy.advancedProgressAnimation == left.advancedProgressAnimation && copy.advancedProcessPriority == left.advancedProcessPriority &&
               copy.advancedDebugSetting == left.advancedDebugSetting && copy.advancedStreamingMemory == left.advancedStreamingMemory &&
               copy.advancedCpuAffinity == left.advancedCpuAffinity && copy.advancedUpdateBuildType == left.advancedUpdateBuildType &&
               copy.advancedUpdateAutoInstall == left.advancedUpdateAutoInstall && BindingsEqual(left.binds, right.binds);
    }

    static bool RestartSensitiveEqual(const SState& left, const SState& right)
    {
        return left.gameCustomizedSAFiles == right.gameCustomizedSAFiles && left.graphicsVideoMode == right.graphicsVideoMode &&
               left.graphicsDisplayMode == right.graphicsDisplayMode && left.graphicsFullscreenStyle == right.graphicsFullscreenStyle &&
               left.graphicsFullscreenMinimize == right.graphicsFullscreenMinimize && left.graphicsAntiAliasing == right.graphicsAntiAliasing &&
               left.graphicsDPIAware == right.graphicsDPIAware && left.graphicsDeviceSelectionDialog == right.graphicsDeviceSelectionDialog &&
               left.browserGpuRendering == right.browserGpuRendering && left.browserVideoAcceleration == right.browserVideoAcceleration;
    }

    static bool BrowserPermissionsEqual(const SState& left, const SState& right)
    {
        return left.browserRemoteWebsites == right.browserRemoteWebsites && left.browserRemoteJavascript == right.browserRemoteJavascript &&
               left.browserBlacklist == right.browserBlacklist && left.browserWhitelist == right.browserWhitelist;
    }

    static bool ParseBoolean(const std::string& value, bool& result)
    {
        if (value == "1" || value == "true")
        {
            result = true;
            return true;
        }
        if (value == "0" || value == "false")
        {
            result = false;
            return true;
        }
        return false;
    }

    static bool ParseNumber(const std::string& value, double& result)
    {
        if (value.empty())
            return false;
        char* end = nullptr;
        errno = 0;
        result = std::strtod(value.c_str(), &end);
        return errno == 0 && end != value.c_str() && *end == '\0' && std::isfinite(result);
    }

    static int QuantizeInteger(double value, int minimum, int maximum, int step)
    {
        const int quantized = static_cast<int>(std::round(value / step)) * step;
        return std::clamp(quantized, minimum, maximum);
    }

    static float QuantizeFloat(double value, float minimum, float maximum, float step)
    {
        const float quantized = std::round(static_cast<float>(value) / step) * step;
        return std::clamp(quantized, minimum, maximum);
    }

    bool SetLegacyValue(const std::string& id, const std::string& value)
    {
        // Keep the mature CEGUI preferences data-driven here. Apart from
        // avoiding a compiler-depth limit, one table makes the native ranges
        // auditable against the web controls instead of duplicating branches.
        if (id == "game.nickname" && value.size() <= 22)
        {
            m_draft.gameNickname = value;
            return true;
        }
        if (id == "interface.locale" && !g_pCore->IsConnected() && IsAvailableLocale(value))
        {
            m_draft.interfaceLocale = value;
            return true;
        }
        if (id == "interface.skin" && !g_pCore->IsConnected() && value.size() <= 128 && IsAvailableSkin(value))
        {
            m_draft.interfaceSkin = value;
            return true;
        }

        struct SBooleanBinding
        {
            const char* id;
            bool SState::* member;
        };
        static constexpr SBooleanBinding booleanBindings[] = {
            {"game.savePasswords", &SState::gameSavePasswords},
            {"game.autoRefreshBrowser", &SState::gameAutoRefreshBrowser},
            {"game.allowScreenUpload", &SState::gameAllowScreenUpload},
            {"game.allowExternalSounds", &SState::gameAllowExternalSounds},
            {"game.alwaysShowTransferBox", &SState::gameAlwaysShowTransferBox},
            {"game.discordRichPresence", &SState::gameDiscordRichPresence},
            {"game.discordShareData", &SState::gameDiscordShareData},
            {"game.steamStatus", &SState::gameSteamStatus},
            {"game.saveCameraPhotos", &SState::gameSaveCameraPhotos},
            {"game.askBeforeDisconnect", &SState::gameAskBeforeDisconnect},
            {"game.customizedSAFiles", &SState::gameCustomizedSAFiles},
            {"audio.radioEqualizer", &SState::audioRadioEqualizer},
            {"audio.radioAutotune", &SState::audioRadioAutotune},
            {"audio.userTrackAutoScan", &SState::audioUserTrackAutoScan},
            {"audio.muteMaster", &SState::audioMuteMaster},
            {"audio.muteRadio", &SState::audioMuteRadio},
            {"audio.muteSfx", &SState::audioMuteSfx},
            {"audio.muteMta", &SState::audioMuteMta},
            {"audio.muteVoice", &SState::audioMuteVoice},
            {"controls.invertMouse", &SState::controlsInvertMouse},
            {"controls.steerWithMouse", &SState::controlsSteerWithMouse},
            {"controls.flyWithMouse", &SState::controlsFlyWithMouse},
            {"controls.useMouseSensitivityForAiming", &SState::controlsUseMouseSensitivityForAiming},
            {"controls.classicControls", &SState::controlsClassic},
            {"interface.chatCssText", &SState::interfaceChatCssText},
            {"interface.chatCssBackground", &SState::interfaceChatCssBackground},
            {"interface.chatNickCompletion", &SState::interfaceChatNickCompletion},
            {"interface.chatTextOutline", &SState::interfaceChatTextOutline},
            {"interface.flashWindow", &SState::interfaceFlashWindow},
            {"interface.trayNotifications", &SState::interfaceTrayNotifications},
            {"browser.remoteWebsites", &SState::browserRemoteWebsites},
            {"browser.remoteJavascript", &SState::browserRemoteJavascript},
            {"browser.gpuRendering", &SState::browserGpuRendering},
            {"browser.videoAcceleration", &SState::browserVideoAcceleration},
            {"advanced.cpuAffinity", &SState::advancedCpuAffinity},
        };
        for (const SBooleanBinding& binding : booleanBindings)
        {
            if (id != binding.id)
                continue;
            if (id == "game.customizedSAFiles" && !HasCustomizedSAFilesOption())
                return false;
            bool parsed{};
            if (!ParseBoolean(value, parsed))
                return false;
            m_draft.*binding.member = parsed;
            return true;
        }

        struct SIntegerBinding
        {
            const char* id;
            int SState::* member;
            int           minimum;
            int           maximum;
        };
        static constexpr SIntegerBinding integerBindings[] = {
            {"game.mapImage", &SState::gameMapImage, 0, 1},
            {"audio.masterVolume", &SState::audioMasterVolume, 0, 100},
            {"audio.radioVolume", &SState::audioRadioVolume, 0, 100},
            {"audio.sfxVolume", &SState::audioSfxVolume, 0, 100},
            {"audio.mtaVolume", &SState::audioMtaVolume, 0, 100},
            {"audio.voiceVolume", &SState::audioVoiceVolume, 0, 100},
            {"audio.userTrackMode", &SState::audioUserTrackMode, 0, 2},
            {"controls.mouseSensitivity", &SState::controlsMouseSensitivity, 0, 100},
            {"controls.verticalAimSensitivity", &SState::controlsVerticalAimSensitivity, 0, 100},
            {"controls.joypadDeadZone", &SState::controlsJoypadDeadZone, 0, 49},
            {"controls.joypadSaturation", &SState::controlsJoypadSaturation, 0, 100},
            {"interface.chatFont", &SState::interfaceChatFont, 0, 3},
            {"interface.chatLines", &SState::interfaceChatLines, 3, 62},
            {"interface.chatPositionHorizontal", &SState::interfaceChatPositionHorizontal, 0, 2},
            {"interface.chatPositionVertical", &SState::interfaceChatPositionVertical, 0, 2},
            {"interface.chatTextAlignment", &SState::interfaceChatTextAlignment, 0, 1},
            {"advanced.fastClothesLoading", &SState::advancedFastClothesLoading, 0, 2},
            {"advanced.browserSpeed", &SState::advancedBrowserSpeed, 0, 2},
            {"advanced.singleConnection", &SState::advancedSingleConnection, 0, 1},
            {"advanced.packetTag", &SState::advancedPacketTag, 0, 1},
            {"advanced.progressAnimation", &SState::advancedProgressAnimation, 0, 1},
            {"advanced.processPriority", &SState::advancedProcessPriority, 0, 2},
            {"advanced.debugSetting", &SState::advancedDebugSetting, 0, 8},
            {"advanced.updateAutoInstall", &SState::advancedUpdateAutoInstall, 0, 1},
        };
        for (const SIntegerBinding& binding : integerBindings)
        {
            if (id != binding.id)
                continue;
            double parsed{};
            if (!ParseNumber(value, parsed) || (id == "advanced.debugSetting" && static_cast<int>(parsed) == static_cast<int>(EDiagnosticDebug::BIDI_6778)))
                return false;
            m_draft.*binding.member = QuantizeInteger(parsed, binding.minimum, binding.maximum, 1);
            if (id.compare(0, 6, "audio") == 0 && id.find("Volume") != std::string::npos)
                ApplyAudioPreview(m_draft);
            return true;
        }

        if (id == "game.mapOpacity")
        {
            double parsed{};
            if (!ParseNumber(value, parsed))
                return false;
            m_draft.gameMapOpacity = QuantizeInteger(parsed, 0, 100, 1);
            m_draft.gameMapAlpha = static_cast<int>(std::round(m_draft.gameMapOpacity / 100.0 * 255.0));
            return true;
        }

        struct SFloatBinding
        {
            const char* id;
            float SState::* member;
            float           minimum;
            float           maximum;
            float           step;
        };
        static constexpr SFloatBinding floatBindings[] = {
            {"interface.chatScaleX", &SState::interfaceChatScaleX, 0.5f, 3.0f, 0.1f},
            {"interface.chatScaleY", &SState::interfaceChatScaleY, 0.5f, 3.0f, 0.1f},
            {"interface.chatWidth", &SState::interfaceChatWidth, 0.5f, 4.0f, 0.1f},
            {"interface.chatLineLife", &SState::interfaceChatLineLife, 1.0f, 120000.0f, 1.0f},
            {"interface.chatLineFadeOut", &SState::interfaceChatLineFadeOut, 1.0f, 30000.0f, 1.0f},
            {"interface.chatOffsetX", &SState::interfaceChatOffsetX, -1.0f, 1.0f, 0.0025f},
            {"interface.chatOffsetY", &SState::interfaceChatOffsetY, -1.0f, 1.0f, 0.0025f},
        };
        for (const SFloatBinding& binding : floatBindings)
        {
            if (id != binding.id)
                continue;
            double parsed{};
            if (!ParseNumber(value, parsed))
                return false;
            m_draft.*binding.member = QuantizeFloat(parsed, binding.minimum, binding.maximum, binding.step);
            return true;
        }

        std::uint32_t SState::* colorMember = nullptr;
        if (id == "interface.chatBackgroundColor")
            colorMember = &SState::interfaceChatBackgroundColor;
        else if (id == "interface.chatTextColor")
            colorMember = &SState::interfaceChatTextColor;
        else if (id == "interface.chatInputBackgroundColor")
            colorMember = &SState::interfaceChatInputBackgroundColor;
        else if (id == "interface.chatInputTextColor")
            colorMember = &SState::interfaceChatInputTextColor;
        if (colorMember)
        {
            double parsed{};
            if (!ParseNumber(value, parsed))
                return false;
            std::uint32_t packed = static_cast<std::uint32_t>(std::clamp(parsed, 0.0, 4294967295.0));
            if (id == "interface.chatTextColor" || id == "interface.chatInputTextColor")
                packed = ClampPackedAlpha(packed, 128);
            m_draft.*colorMember = packed;
            return true;
        }

        double parsed{};
        if (id == "advanced.streamingMemory" && ParseNumber(value, parsed))
        {
            m_draft.advancedStreamingMemory = QuantizeInteger(parsed, g_pCore->GetMinStreamingMemory(), g_pCore->GetMaxStreamingMemory(), 1);
            return true;
        }
        if (id == "advanced.updateBuildType" && ParseNumber(value, parsed) && (parsed == 0.0 || parsed == 2.0))
        {
            m_draft.advancedUpdateBuildType = static_cast<int>(parsed);
            return true;
        }
        return false;
    }

    static void ReadRadarValues(SState& state)
    {
        CVARS_GET("radar_style", state.radarStyle);
        CVARS_GET("radar_position_x", state.radarPositionX);
        CVARS_GET("radar_position_y", state.radarPositionY);
        CVARS_GET("radar_width", state.radarWidth);
        CVARS_GET("radar_height", state.radarHeight);
        CVARS_GET("radar_widescreen_safe", state.radarWidescreenSafe);
    }

    static SState ReadCurrentState()
    {
        SState state;
        CVARS_GET("nick", state.gameNickname);
        if (!CCore::GetSingleton().IsValidNick(state.gameNickname.c_str()))
            state.gameNickname = CNickGen::GetRandomNickname();
        CVARS_GET("save_server_passwords", state.gameSavePasswords);
        CVARS_GET("auto_refresh_browser", state.gameAutoRefreshBrowser);
        CVARS_GET("allow_screen_upload", state.gameAllowScreenUpload);
        CVARS_GET("allow_external_sounds", state.gameAllowExternalSounds);
        CVARS_GET("always_show_transferbox", state.gameAlwaysShowTransferBox);
        CVARS_GET("allow_discord_rpc", state.gameDiscordRichPresence);
        CVARS_GET("discord_rpc_share_data", state.gameDiscordShareData);
        CVARS_GET("allow_steam_client", state.gameSteamStatus);
        CVARS_GET("photosaving", state.gameSaveCameraPhotos);
        CVARS_GET("ask_before_disconnect", state.gameAskBeforeDisconnect);
        state.gameCustomizedSAFiles = GetApplicationSettingInt("customized-sa-files-request") != 0;
        CVARS_GET("mapalpha", state.gameMapAlpha);
        state.gameMapAlpha = std::clamp(state.gameMapAlpha, 0, 255);
        state.gameMapOpacity = std::clamp(static_cast<int>(std::round(state.gameMapAlpha / 255.0 * 100.0)), 0, 100);
        CVARS_GET("mapimage", state.gameMapImage);

        state.audioMasterVolume = QuantizeInteger(CVARS_GET_VALUE<float>("mastervolume") * 100.0, 0, 100, 1);
        state.audioRadioVolume = QuantizeInteger(CVARS_GET_VALUE<float>("radiovolume") * 100.0, 0, 100, 1);
        state.audioSfxVolume = QuantizeInteger(CVARS_GET_VALUE<float>("sfxvolume") * 100.0, 0, 100, 1);
        state.audioMtaVolume = QuantizeInteger(CVARS_GET_VALUE<float>("mtavolume") * 100.0, 0, 100, 1);
        state.audioVoiceVolume = QuantizeInteger(CVARS_GET_VALUE<float>("voicevolume") * 100.0, 0, 100, 1);
        CVARS_GET("mute_master_when_minimized", state.audioMuteMaster);
        CVARS_GET("mute_radio_when_minimized", state.audioMuteRadio);
        CVARS_GET("mute_sfx_when_minimized", state.audioMuteSfx);
        CVARS_GET("mute_mta_when_minimized", state.audioMuteMta);
        CVARS_GET("mute_voice_when_minimized", state.audioMuteVoice);

        CVARS_GET("invert_mouse", state.controlsInvertMouse);
        CVARS_GET("steer_with_mouse", state.controlsSteerWithMouse);
        CVARS_GET("fly_with_mouse", state.controlsFlyWithMouse);
        CVARS_GET("classic_controls", state.controlsClassic);
        CVARS_GET("use_mouse_sensitivity_for_aiming", state.controlsUseMouseSensitivityForAiming);
        state.controlsJoypadDeadZone = std::clamp(GetJoystickManager()->GetDeadZone(), 0, 49);
        state.controlsJoypadSaturation = std::clamp(GetJoystickManager()->GetSaturation(), 0, 100);

        CVARS_GET("locale", state.interfaceLocale);
        CVARS_GET("current_skin", state.interfaceSkin);
        CColor chatColor;
        CVARS_GET("chat_color", chatColor);
        state.interfaceChatBackgroundColor = PackColor(chatColor);
        CVARS_GET("chat_text_color", chatColor);
        state.interfaceChatTextColor = PackColor(chatColor);
        CVARS_GET("chat_input_color", chatColor);
        state.interfaceChatInputBackgroundColor = PackColor(chatColor);
        CVARS_GET("chat_input_text_color", chatColor);
        state.interfaceChatInputTextColor = PackColor(chatColor);
        CVARS_GET("chat_font", state.interfaceChatFont);
        CVARS_GET("chat_lines", state.interfaceChatLines);
        CVector2D chatScale;
        CVARS_GET("chat_scale", chatScale);
        state.interfaceChatScaleX = chatScale.fX;
        state.interfaceChatScaleY = chatScale.fY;
        CVARS_GET("chat_width", state.interfaceChatWidth);
        CVARS_GET("chat_css_style_text", state.interfaceChatCssText);
        CVARS_GET("chat_css_style_background", state.interfaceChatCssBackground);
        CVARS_GET("chat_nickcompletion", state.interfaceChatNickCompletion);
        CVARS_GET("chat_text_outline", state.interfaceChatTextOutline);
        int milliseconds = 0;
        CVARS_GET("chat_line_life", milliseconds);
        state.interfaceChatLineLife = std::max(1.0f, milliseconds / 1000.0f);
        CVARS_GET("chat_line_fade_out", milliseconds);
        state.interfaceChatLineFadeOut = std::max(1.0f, milliseconds / 1000.0f);
        CVARS_GET("chat_position_horizontal", state.interfaceChatPositionHorizontal);
        CVARS_GET("chat_position_vertical", state.interfaceChatPositionVertical);
        CVARS_GET("chat_text_alignment", state.interfaceChatTextAlignment);
        CVARS_GET("chat_position_offset_x", state.interfaceChatOffsetX);
        CVARS_GET("chat_position_offset_y", state.interfaceChatOffsetY);
        CVARS_GET("server_can_flash_window", state.interfaceFlashWindow);
        CVARS_GET("allow_tray_notifications", state.interfaceTrayNotifications);
        CVARS_GET("browser_remote_websites", state.browserRemoteWebsites);
        CVARS_GET("browser_remote_javascript", state.browserRemoteJavascript);
        CVARS_GET("browser_enable_gpu", state.browserGpuRendering);
        CVARS_GET("browser_enable_video_acceleration", state.browserVideoAcceleration);

        CVARS_GET("fast_clothes_loading", state.advancedFastClothesLoading);
        CVARS_GET("browser_speed", state.advancedBrowserSpeed);
        CVARS_GET("single_download", state.advancedSingleConnection);
        CVARS_GET("packet_tag", state.advancedPacketTag);
        CVARS_GET("progress_animation", state.advancedProgressAnimation);
        CVARS_GET("process_priority", state.advancedProcessPriority);
        state.advancedProcessPriority = std::clamp(state.advancedProcessPriority, 0, 2);
        state.advancedDebugSetting = static_cast<int>(g_pCore->GetDiagnosticDebug());
        CVARS_GET("streaming_memory", state.advancedStreamingMemory);
        state.advancedStreamingMemory =
            std::clamp(state.advancedStreamingMemory, static_cast<int>(g_pCore->GetMinStreamingMemory()), static_cast<int>(g_pCore->GetMaxStreamingMemory()));
        CVARS_GET("process_cpu_affinity", state.advancedCpuAffinity);
        CVARS_GET("update_build_type", state.advancedUpdateBuildType);
        CVARS_GET("update_auto_install", state.advancedUpdateAutoInstall);

        CVARS_GET("extended_draw_distance_enabled", state.extendedWorldEnabled);
        CVARS_GET("extended_draw_distance", state.extendedWorldDistance);
        CVARS_GET("distant_lights_enabled", state.distantLightsEnabled);
        CVARS_GET("distant_lights_draw_distance", state.distantLightsDistance);
        CVARS_GET("distant_lights_corona_radius_multiplier", state.distantLightsCoronaSize);
        state.skyGfx = SkyGfx::CManager::Get().HasRuntimeOverrides() ? SkyGfx::CManager::Get().GetConfig() : SkyGfx::CManager::Get().GetUserConfig();
        ReadRadarValues(state);

        CGame*         game = g_pCore ? g_pCore->GetGame() : nullptr;
        CGameSettings* gameSettings = game ? game->GetSettings() : nullptr;
        if (gameSettings)
        {
            state.audioRadioEqualizer = gameSettings->IsRadioEqualizerEnabled();
            state.audioRadioAutotune = gameSettings->IsRadioAutotuneEnabled();
            state.audioUserTrackAutoScan = gameSettings->IsUsertrackAutoScan();
            state.audioUserTrackMode = static_cast<int>(gameSettings->GetUsertrackMode());
            state.controlsMouseSensitivity = QuantizeInteger(gameSettings->GetMouseSensitivity() * 100.0, 0, 100, 1);
            if (CControllerConfigManager* controller = game->GetControllerConfigManager())
                state.controlsVerticalAimSensitivity = QuantizeInteger(controller->GetVerticalAimSensitivity() * 100.0, 0, 100, 1);
            bool windowed = false;
            bool fullscreenMinimize = false;
            int  fullscreenStyle = FULLSCREEN_STANDARD;
            GetVideoModeManager()->GetNextVideoMode(state.graphicsVideoMode, windowed, fullscreenMinimize, fullscreenStyle);
            state.graphicsFullscreenStyle = fullscreenStyle;
            state.graphicsDisplayMode = windowed ? 0 : 1;
            if (!windowed && fullscreenStyle == FULLSCREEN_BORDERLESS)
                state.graphicsDisplayMode = 2;
            else if (!windowed && fullscreenStyle == FULLSCREEN_BORDERLESS_KEEP_RES)
                state.graphicsDisplayMode = 3;
            state.graphicsFullscreenMinimize = fullscreenMinimize;
            state.graphicsDrawDistance = std::clamp(static_cast<int>(std::round((gameSettings->GetDrawDistance() - 0.925f) / 0.875f * 100.0f)), 0, 100);
            state.graphicsBrightness = std::clamp(static_cast<int>(std::round(static_cast<float>(gameSettings->GetBrightness()) / 384.0f * 100.0f)), 0, 100);
            state.graphicsFXQuality = std::clamp(static_cast<int>(gameSettings->GetFXQuality()), 0, 3);
            state.graphicsAntiAliasing = std::clamp(static_cast<int>(gameSettings->GetAntiAliasing()), 1, 4);
        }

        CVARS_GET("vsync", state.graphicsVSync);
        CVARS_GET("process_dpi_aware", state.graphicsDPIAware);
        CVARS_GET("fov", state.graphicsFieldOfView);
        CVARS_GET("anisotropic", state.graphicsAnisotropic);
        CVARS_GET("aspect_ratio", state.graphicsAspectRatio);
        CVARS_GET("hud_match_aspect_ratio", state.graphicsHudMatchAspectRatio);
        CVARS_GET("volumetric_shadows", state.graphicsVolumetricShadows);
        CVARS_GET("grass", state.graphicsGrass);
        CVARS_GET("heat_haze", state.graphicsHeatHaze);
        CVARS_GET("tyre_smoke_enabled", state.graphicsTyreSmoke);
        CVARS_GET("dynamic_ped_shadows", state.graphicsDynamicPedShadows);
        CVARS_GET("blur", state.graphicsMotionBlur);
        CVARS_GET("corona_reflections", state.graphicsCoronaReflections);
        CVARS_GET("high_detail_vehicles", state.graphicsHighDetailVehicles);
        CVARS_GET("high_detail_peds", state.graphicsHighDetailPeds);
        CVARS_GET("show_unsafe_resolutions", state.graphicsShowUnsafeResolutions);
        state.graphicsDeviceSelectionDialog = GetApplicationSettingInt("device-selection-disabled") == 0;
        CVARS_GET("borderless_gamma_power", state.graphicsGamma);
        CVARS_GET("borderless_brightness_scale", state.graphicsBrightnessScale);
        CVARS_GET("borderless_contrast_scale", state.graphicsContrast);
        CVARS_GET("borderless_saturation_scale", state.graphicsSaturation);
        CVARS_GET("borderless_gamma_enabled", state.graphicsGammaEnabled);
        CVARS_GET("borderless_brightness_enabled", state.graphicsBrightnessEnabled);
        CVARS_GET("borderless_contrast_enabled", state.graphicsContrastEnabled);
        CVARS_GET("borderless_saturation_enabled", state.graphicsSaturationEnabled);
        CVARS_GET("borderless_apply_windowed", state.graphicsApplyWindowed);
        CVARS_GET("borderless_apply_fullscreen", state.graphicsApplyFullscreen);
        return state;
    }

    SState                   m_original;
    SState                   m_draft;
    SState                   m_restartBaseline;
    SState                   m_connectionBaseline;
    bool                     m_active{};
    bool                     m_wasSkyGfxManaged{};
    bool                     m_wasRadarManaged{};
    bool                     m_wasConnected{};
    bool                     m_restartRequired{};
    bool                     m_disconnectRequired{};
    std::string              m_captureBindId;
    int                      m_captureBindSlot{-1};
    int                      m_captureAxis{-1};
    std::string              m_error;
    std::vector<SChatPreset> m_chatPresets;
};

CServerBrowserWeb* CServerBrowserWeb::ms_instance = nullptr;

CServerBrowserWeb::CServerBrowserWeb(CMainMenu& mainMenu, CServerBrowser& serverBrowser)
    : m_mainMenu(mainMenu),
      m_serverBrowser(serverBrowser),
      m_registry(std::make_unique<CNeonServerRegistry>()),
      m_settings(std::make_unique<CWebSettingsSession>())
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

    // Resource GUIs and browsers are allowed to activate themselves while
    // gameplay owns the screen. That transient state must not revoke input
    // from the visible core menu: otherwise a resource which repeatedly calls
    // focusBrowser or guiFocus leaves Neon displaying a valid but inert frame.
    // Native dialogs remain the only higher-priority input owner.
    return g_pCore->GetWebCoreUnchecked() && !ms_instance->m_mainMenu.HasNativeInputOwner() && ms_instance->m_widget->IsVisible() &&
           ms_instance->m_widget->IsEnabled();
}

bool CServerBrowserWeb::HandleEscapeKey()
{
    if (!ms_instance || !ms_instance->m_visible || !ms_instance->m_documentReady || ms_instance->m_nativeDialogVisible || !ms_instance->m_webView)
        return false;

    if (ms_instance->m_settingsReady && ms_instance->m_settings && ms_instance->m_settings->ProcessCapturedInput(WM_KEYDOWN, VK_ESCAPE, 0))
    {
        ms_instance->QueueSettingsState(false);
        return true;
    }

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
    return ms_instance && ms_instance->m_menuReady;
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

    // A progress event belongs to the web connection flow. Keep that ownership
    // until the user dismisses it, the connection succeeds, or we explicitly
    // hand off to a native dialog such as the full-server queue.
    ms_instance->m_connectionUiActive = true;
    ms_instance->CancelHibernateRequest();
    ms_instance->UpdateRenderingPauseState();

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

    // The network attempt has ended, but the web failure/password dialog is
    // still the active connection UI. Releasing ownership here lets delayed
    // connection events fall through to the legacy CEGUI dialogs underneath
    // the web shell. Ownership is released only by an explicit terminal path.
    ms_instance->m_connectionUiActive = true;
    ms_instance->CancelHibernateRequest();
    ms_instance->UpdateRenderingPauseState();

    JsonPtr event = MakeObject();
    AddString(event.get(), "type", "connect-failed");
    AddString(event.get(), "code", code);
    AddString(event.get(), "message", message);
    ms_instance->QueueConnectionEvent(ToJson(event.get()));
    return true;
}

bool CServerBrowserWeb::NotifyConnectionPasswordRequired(const std::string& host, unsigned short port, bool rejected)
{
    if (!CanHandleConnectionUi() || host.empty() || !port)
        return false;

    ms_instance->m_connectionUiActive = true;
    ms_instance->CancelHibernateRequest();

    // A password can be rejected after the deathmatch module has already
    // hidden the menu. Reopen the shell before that module unloads so the
    // replacement prompt never falls through to CServerInfo underneath it.
    if (!ms_instance->m_visible)
        ms_instance->m_mainMenu.Show(true);
    else
        ms_instance->UpdateRenderingPauseState();

    JsonPtr event = MakeObject();
    AddString(event.get(), "type", "connect-password-required");
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
    if (rejected)
        NotifyConnectionFailed("bad-password", _("Invalid password"));
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

void CServerBrowserWeb::RelinquishConnectionUi()
{
    if (!ms_instance)
        return;

    if (CanHandleConnectionUi())
    {
        JsonPtr event = MakeObject();
        AddString(event.get(), "type", "connect-dismissed");
        ms_instance->QueueConnectionEvent(ToJson(event.get()));
    }
    ms_instance->m_connectionUiActive = false;
    ms_instance->UpdateRenderingPauseState();
}

bool CServerBrowserWeb::CanDeferQuestionDialog()
{
    return ms_instance && !ms_instance->m_initialisationFailed;
}

bool CServerBrowserWeb::ShowQuestionDialog(const std::string& title, const std::string& message, const std::vector<std::string>& buttons)
{
    if (!ms_instance || !ms_instance->m_menuReady || ms_instance->m_nativeDialogVisible || ms_instance->m_messageDialogActive)
        return false;

    if (!ms_instance->m_questionDialogActive)
    {
        if (++ms_instance->m_messageDialogId == 0)
            ++ms_instance->m_messageDialogId;
        ms_instance->m_questionDialogActive = true;
        ms_instance->m_questionDialogOpenedMenu = !ms_instance->m_visible;
        ms_instance->m_questionDialogResponded = false;
        ms_instance->CancelHibernateRequest();

        if (ms_instance->m_questionDialogOpenedMenu)
            ms_instance->m_mainMenu.Show(true);
        else
            ms_instance->UpdateRenderingPauseState();
    }

    ms_instance->m_questionDialogButtonCount = static_cast<unsigned int>(buttons.size());

    JsonPtr event = MakeObject();
    AddString(event.get(), "type", "dialog-show");
    AddInteger(event.get(), "id", ms_instance->m_messageDialogId);
    AddString(event.get(), "severity", "question");
    AddString(event.get(), "title", title);
    AddString(event.get(), "message", message);
    AddBoolean(event.get(), "dismissible", false);

    json_object* actions = json_object_new_array();
    for (std::size_t index = 0; index < buttons.size(); ++index)
    {
        json_object* action = json_object_new_object();
        AddString(action, "id", SString("button-%u", static_cast<unsigned int>(index)));
        AddString(action, "label", buttons[index]);
        AddString(action, "variant", index + 1 == buttons.size() ? "primary" : "default");
        json_object_array_add(actions, action);
    }
    json_object_object_add(event.get(), "actions", actions);
    if (!buttons.empty())
        AddString(event.get(), "escapeAction", "button-0");

    ms_instance->QueueEvent("menu", ToJson(event.get()));
    return true;
}

bool CServerBrowserWeb::CloseQuestionDialog()
{
    if (!ms_instance || !ms_instance->m_questionDialogActive)
        return false;

    JsonPtr event = MakeObject();
    AddString(event.get(), "type", "dialog-close");
    AddInteger(event.get(), "id", ms_instance->m_messageDialogId);
    ms_instance->QueueEvent("menu", ToJson(event.get()));

    const bool restoreGameplay = ms_instance->m_questionDialogOpenedMenu && ms_instance->m_mainMenu.GetIsIngame();
    ms_instance->m_questionDialogActive = false;
    ms_instance->m_questionDialogOpenedMenu = false;
    ms_instance->m_questionDialogResponded = false;
    ms_instance->m_questionDialogButtonCount = 0;
    if (restoreGameplay && !ms_instance->m_mainMenu.HasNativeInputOwner())
        ms_instance->m_mainMenu.Hide();
    else
        ms_instance->UpdateRenderingPauseState();
    return true;
}

bool CServerBrowserWeb::ShowMessageDialog(const std::string& title, const std::string& message, unsigned int flags)
{
    if (!ms_instance || !ms_instance->m_menuReady || ms_instance->m_nativeDialogVisible || ms_instance->m_questionDialogActive)
        return false;

    const bool important = (flags & (MB_ICON_ERROR | MB_ICON_WARNING)) != 0;
    if (!ms_instance->m_visible && !important)
        return false;

    if (++ms_instance->m_messageDialogId == 0)
        ++ms_instance->m_messageDialogId;

    ms_instance->m_messageDialogActive = true;
    ms_instance->m_messageDialogOpenedMenu = !ms_instance->m_visible;
    ms_instance->CancelHibernateRequest();

    // Lifecycle errors can be raised immediately before the deathmatch module
    // unloads. Surface the web overlay at once; when the user closes it we only
    // resume gameplay if that module is still active.
    if (ms_instance->m_messageDialogOpenedMenu)
        ms_instance->m_mainMenu.Show(true);
    else
        ms_instance->UpdateRenderingPauseState();

    const char* severity = "info";
    if (flags & MB_ICON_ERROR)
        severity = "error";
    else if (flags & MB_ICON_WARNING)
        severity = "warning";
    else if (flags & MB_ICON_QUESTION)
        severity = "question";

    const char* buttonLabel = nullptr;
    const char* buttonAction = nullptr;
    if (flags & MB_BUTTON_OK)
    {
        buttonLabel = _("OK");
        buttonAction = "close";
    }
    else if (flags & MB_BUTTON_CANCEL)
    {
        buttonLabel = _("Cancel");
        buttonAction = "cancel";
    }
    else if (flags & MB_BUTTON_YES)
    {
        buttonLabel = _("Yes");
        buttonAction = "confirm";
    }
    else if (flags & MB_BUTTON_NO)
    {
        buttonLabel = _("No");
        buttonAction = "cancel";
    }

    ms_instance->m_messageDialogAction = buttonAction ? buttonAction : "";

    JsonPtr event = MakeObject();
    AddString(event.get(), "type", "dialog-show");
    AddInteger(event.get(), "id", ms_instance->m_messageDialogId);
    AddString(event.get(), "severity", severity);
    AddString(event.get(), "title", title);
    AddString(event.get(), "message", message);
    AddBoolean(event.get(), "dismissible", buttonLabel != nullptr);
    if (buttonLabel)
    {
        json_object* actions = json_object_new_array();
        json_object* action = json_object_new_object();
        AddString(action, "id", buttonAction);
        AddString(action, "label", buttonLabel);
        AddString(action, "variant", "primary");
        json_object_array_add(actions, action);
        json_object_object_add(event.get(), "actions", actions);
        if (strcmp(buttonAction, "close") == 0 || strcmp(buttonAction, "cancel") == 0)
            AddString(event.get(), "escapeAction", buttonAction);
    }
    ms_instance->QueueEvent("menu", ToJson(event.get()));
    return true;
}

bool CServerBrowserWeb::CloseMessageDialog()
{
    if (!ms_instance || !ms_instance->m_messageDialogActive)
        return false;

    JsonPtr event = MakeObject();
    AddString(event.get(), "type", "dialog-close");
    AddInteger(event.get(), "id", ms_instance->m_messageDialogId);
    ms_instance->QueueEvent("menu", ToJson(event.get()));

    const bool restoreGameplay = ms_instance->m_messageDialogOpenedMenu && ms_instance->m_mainMenu.GetIsIngame();
    ms_instance->m_messageDialogActive = false;
    ms_instance->m_messageDialogOpenedMenu = false;
    ms_instance->m_messageDialogAction.clear();
    // Native flows such as CQuestionBox::Show can display their own owner
    // before removing the previous core message box. Keep the menu visible
    // instead of hiding that replacement during the transition.
    if (restoreGameplay && !ms_instance->m_mainMenu.HasNativeInputOwner())
        ms_instance->m_mainMenu.Hide();
    else
        ms_instance->UpdateRenderingPauseState();
    return true;
}

bool CServerBrowserWeb::RouteInputMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (!IsInputRoutedToWeb())
        return false;

    if (CaptureSettingsInputMessage(message, wParam, lParam))
        return true;

    CWebCoreInterface* webCore = g_pCore->GetWebCoreUnchecked();
    if (!webCore)
        return false;

    const bool keyboardMessage =
        message == WM_KEYDOWN || message == WM_KEYUP || message == WM_CHAR || message == WM_SYSCHAR || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP;
    const bool mouseMessage = message == WM_MOUSEMOVE || message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL || message == WM_LBUTTONDOWN ||
                              message == WM_LBUTTONUP || message == WM_LBUTTONDBLCLK || message == WM_MBUTTONDOWN || message == WM_MBUTTONUP ||
                              message == WM_MBUTTONDBLCLK || message == WM_RBUTTONDOWN || message == WM_RBUTTONUP || message == WM_RBUTTONDBLCLK ||
                              message == WM_XBUTTONDOWN || message == WM_XBUTTONUP || message == WM_XBUTTONDBLCLK;
    if (!keyboardMessage && !mouseMessage)
        return false;

    // Reclaim modal ownership only when an input message actually arrives.
    // This avoids a per-frame focus contest with resource browsers while
    // guaranteeing that the visible core menu receives the complete gesture.
    if (!ms_instance->m_widget->IsActive())
    {
        ms_instance->m_widget->BringToFront();
        ms_instance->m_widget->Activate();
    }
    if (webCore->GetFocusedWebView() != ms_instance->m_webView)
        ms_instance->m_webView->Focus(true);

    // Mouse input must still pass through the regular CEGUI widget so its
    // click counting and cursor behaviour remain intact. Activating the core
    // widget above first releases any capture held by a resource GUI; the
    // message can then continue to CLocalGUI and be injected exactly once.
    if (!keyboardMessage)
        return false;

    webCore->ProcessInputMessage(message, wParam, lParam);
    return true;
}

bool CServerBrowserWeb::CaptureSettingsInputMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (!IsInputRoutedToWeb() || !ms_instance->m_settingsReady || !ms_instance->m_settings ||
        !ms_instance->m_settings->ProcessCapturedInput(message, wParam, lParam))
    {
        return false;
    }

    // Binding capture must happen before the regular key pipeline. Apart from
    // preventing Chromium navigation, this keeps the captured press out of
    // onClientKey and the active GTA/command bind state.
    ms_instance->QueueSettingsState(false);
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
    // During the initial offline launch CEF is intentionally warmed before
    // the menu becomes visible. Draw the native shell in that short window as
    // well, otherwise CefInitialize blocks presentation on a black GTA frame
    // even though the placeholder texture was already prepared.
    const bool offlineStartup = !m_mainMenu.GetIsIngame() && !m_visualReady;
    if ((!m_visible && !offlineStartup) || m_visualReady || m_nativeDialogVisible || !m_loadingTexture || g_pCore->IsWindowMinimized())
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
    // Resource browsers share the http://mta origin. Keep the core menu in a
    // separate renderer process so heavy resource JS cannot freeze its input
    // and painting while the game remains responsive.
    m_webView->SetProperty("isolated_request_context", "1");
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
    return m_webView && m_documentReady && m_visualReady && !m_visible && !m_nativeDialogVisible && !m_connectionUiActive && !m_messageDialogActive &&
           !m_questionDialogActive && m_mainMenu.GetIsIngame();
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
    if (m_settingsReady && ++m_settingsUpdatePulses >= 30)
    {
        m_settingsUpdatePulses = 0;
        // Managed overrides can change at runtime, but rebuilding the full
        // settings JSON and resolution list every half-second is unnecessary.
        // Axis capture is the one other asynchronous settings operation.
        if (m_settings->UpdateInputCapture() || m_settings->RefreshManagedValues())
            QueueSettingsState(false);
    }
    UpdateRenderingPauseState();

    if (m_serverBrowserReady && m_serverBrowserActive && m_registry)
    {
        m_registry->DoPulse();
        if (m_registry->ConsumeChanged())
        {
            QueueFeaturedServer();
            m_sentRevisions.clear();
            QueueListReset();
            m_refreshing = true;
            m_sentRefreshFinished = false;
            m_hasSentRefreshProgress = false;
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
    else if (m_registry)
    {
        // Keep the lightweight catalogue/artwork request alive on the main
        // menu without starting ASE status queries for the full list.
        m_registry->DoPulse();
        if (m_registry->ConsumeChanged())
            QueueFeaturedServer();
    }

    CServerList* list = GetCurrentList();
    if (m_visible && m_serverBrowserReady && m_serverBrowserActive && list)
    {
        list->Pulse();
        SendSnapshot(false);

        if (m_refreshing)
        {
            const unsigned int scanned = list->GetScannedCount();
            const unsigned int total = list->GetServerCount();
            // ASE can stay in the refreshing state for many frames while
            // waiting for a reply. Publishing only changed counters avoids
            // waking and repainting the whole web UI on every client pulse.
            if (!m_hasSentRefreshProgress || scanned != m_lastRefreshScanned || total != m_lastRefreshTotal)
            {
                JsonPtr progress = MakeObject();
                AddString(progress.get(), "type", "progress");
                AddString(progress.get(), "source", GetSourceName(m_source));
                AddInteger(progress.get(), "scanned", scanned);
                AddInteger(progress.get(), "total", total);
                QueueEvent("server", ToJson(progress.get()));
                m_hasSentRefreshProgress = true;
                m_lastRefreshScanned = scanned;
                m_lastRefreshTotal = total;
            }

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

void CServerBrowserWeb::Events_OnLoadingStart(const SString&, bool mainFrame)
{
    if (mainFrame)
    {
        m_documentReady = false;
        m_menuReady = false;
    }
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

    // Startup question boxes wait for React to install the menu bridge so an
    // updater prompt cannot briefly replace the Neon shell with CEGUI. Stop
    // waiting when the shell document itself fails, allowing CQuestionBox to
    // use its native fallback instead of leaving the user without a dialog.
    if (url.BeginsWith("http://mta/local/index.html"))
        m_initialisationFailed = true;
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
    else if (eventName.BeginsWith("settings:"))
        HandleSettingsEvent(eventName, arguments);
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
        // DocumentReady fires before React effects install __neonMenu. This
        // explicit handshake prevents lifecycle dialogs from being accepted
        // and then discarded into a receiver that does not exist yet.
        m_menuReady = true;
        QueueMenuInit();
        QueueIdentity(true);
        if (m_registry)
        {
            m_registry->Start(false);
            QueueFeaturedServer();
        }
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
    else if (eventName == "menu:dialogAction" && arguments.size() == 2)
    {
        const char* text = arguments[0].c_str();
        char*       end = nullptr;
        errno = 0;
        const unsigned long dialogId = std::strtoul(text, &end, 10);
        if (errno == 0 && end != text && *end == '\0' && dialogId == m_messageDialogId)
        {
            if (m_messageDialogActive && arguments[1] == m_messageDialogAction)
                g_pCore->RespondToWebMessageBox();
            else if (m_questionDialogActive && !m_questionDialogResponded && arguments[1].rfind("button-", 0) == 0)
            {
                const char* buttonText = arguments[1].c_str() + 7;
                char*       buttonEnd = nullptr;
                errno = 0;
                const unsigned long button = std::strtoul(buttonText, &buttonEnd, 10);
                if (errno == 0 && buttonEnd != buttonText && *buttonEnd == '\0' && button < m_questionDialogButtonCount)
                {
                    m_questionDialogResponded = true;
                    if (CQuestionBox* questionBox = m_mainMenu.GetQuestionWindow())
                        questionBox->RespondToWeb(static_cast<unsigned int>(button));
                }
            }
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
        JsonPtr event = MakeObject();
        AddString(event.get(), "type", "open-settings");
        QueueEvent("menu", ToJson(event.get()));
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
    else if (eventName == "menu:playFeatured" && m_registry)
    {
        CServerListItem* server = m_registry->FindByServerId(FEATURED_SERVER_ID);
        if (!server)
            return;

        CNeonIdentityManager* identity = g_pCore->GetNeonIdentityManager();
        if (!identity || !identity->IsAuthenticated())
        {
            if (!identity || !identity->IsSigningIn())
                m_mainMenu.OnNeonIdentityButtonClick(nullptr);
            return;
        }
        Connect(server->strHost, server->usGamePort, "");
    }
    else if (eventName == "menu:sound" && arguments.size() == 1)
        PlayUiSound(arguments[0]);
    else if (eventName == "menu:setLanguage" && arguments.size() == 1)
    {
        const auto locales = g_pCore->GetLocalization()->GetAvailableLocales();
        if (std::find(locales.begin(), locales.end(), arguments[0]) != locales.end())
            CLocalGUI::GetSingleton().RequestLocaleChange(arguments[0]);
    }
}

void CServerBrowserWeb::HandleSettingsEvent(const SString& eventName, const std::vector<std::string>& arguments)
{
    if (eventName == "settings:ready")
    {
        m_settingsReady = true;
        m_settings->Begin();
        m_lastSettingsState.clear();
        QueueSettingsState(true);
    }
    else if (eventName == "settings:set" && arguments.size() == 2 && m_settingsReady)
    {
        m_settings->SetValue(arguments[0], arguments[1]);
        // Always acknowledge with authoritative native state. React updates
        // optimistically for responsive sliders; rejected/stale edits must be
        // snapped back instead of remaining dirty only in the web process.
        QueueSettingsState(false);
    }
    else if (eventName == "settings:resetSection" && arguments.size() == 1 &&
             (arguments[0] == "game" || arguments[0] == "graphics" || arguments[0] == "audio" || arguments[0] == "controls" || arguments[0] == "interface" ||
              arguments[0] == "neon" || arguments[0] == "advanced") &&
             m_settingsReady)
    {
        m_settings->Reset(arguments[0]);
        QueueSettingsState(false);
    }
    else if (eventName == "settings:action" && !arguments.empty() && arguments.size() <= 2 && m_settingsReady)
    {
        m_settings->RunAction(arguments[0], arguments.size() == 2 ? arguments[1] : std::string{});
        QueueSettingsState(false);
    }
    else if (eventName == "settings:apply" && m_settingsReady)
    {
        m_settings->Apply();
        QueueSettingsState(false);
    }
    else if (eventName == "settings:cancel" && m_settingsReady)
    {
        m_settings->Cancel();
        QueueSettingsState(false);
    }
    else if (eventName == "settings:close")
    {
        if (m_settings && m_settings->IsActive())
            m_settings->Cancel();
        m_settingsReady = false;
        m_settingsUpdatePulses = 0;
        m_lastSettingsState.clear();
        m_settingsEvents.clear();
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
        m_serverBrowserActive = true;
        if (m_registry)
            m_registry->Start(true);
        JsonPtr init = MakeObject();
        AddString(init.get(), "type", "init");
        AddString(init.get(), "version", g_pCore->GetProductVersion());
        AddString(init.get(), "source", GetSourceName(m_source));
        QueueEvent("server", ToJson(init.get()));
        QueueFavourites();
        // App::init selects and refreshes the source immediately afterwards.
        // Waiting for it avoids an eager duplicate snapshot and scan.
    }
    else if (eventName == "sb:resume")
        m_serverBrowserActive = true;
    else if (eventName == "sb:suspend")
    {
        // The backend singleton survives route changes. Stop feeding its
        // hidden store so Settings does not compete with an ASE scan.
        m_serverBrowserActive = false;
        m_serverEvents.clear();
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
    else if (eventName == "sb:copyServerLink" && arguments.size() == 3)
    {
        const std::string&   requestId = arguments[0];
        const std::string&   host = arguments[1];
        const unsigned short port = ParsePort(arguments[2]);
        const bool           safeRequest =
            !requestId.empty() && requestId.size() <= 64 && !host.empty() && host.size() <= 253 && port != 0 &&
            std::none_of(host.begin(), host.end(), [](unsigned char character)
                         { return std::isspace(character) || character == '/' || character == '\\' || character == '@' || character == '#'; });

        // Local CEF pages intentionally have JavaScript clipboard access
        // disabled. Keeping this narrowly scoped bridge avoids granting that
        // permission globally while still providing an honest copy result.
        if (safeRequest)
            SharedUtil::SetClipboardText(SString("mtaneon://%s:%u", host.c_str(), port));

        JsonPtr result = MakeObject();
        AddString(result.get(), "type", "clipboard-result");
        AddString(result.get(), "requestId", requestId);
        AddBoolean(result.get(), "success", safeRequest);
        QueueEvent("server", ToJson(result.get()));
    }
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
    // The web shell must use the identity compiled into this exact binary for
    // one-time release notices. A catalogue constant can drift from the signed
    // updater manifest, while this value is shared by both producers.
    AddInteger(init.get(), "buildNumber", MTASA_VERSION_BUILD);
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

void CServerBrowserWeb::QueueFeaturedServer()
{
    JsonPtr event = MakeObject();
    AddString(event.get(), "type", "featured-server");

    CServerListItem*           server = m_registry ? m_registry->FindByServerId(FEATURED_SERVER_ID) : nullptr;
    const SNeonServerMetadata* metadata = server && m_registry ? m_registry->FindMetadata(*server) : nullptr;
    if (!server || !metadata)
    {
        json_object_object_add(event.get(), "server", json_object_new_null());
        QueueEvent("menu", ToJson(event.get()));
        return;
    }

    json_object* value = json_object_new_object();
    AddString(value, "serverId", metadata->serverId);
    AddString(value, "host", server->strHost);
    AddInteger(value, "port", server->usGamePort);
    AddString(value, "name", metadata->name);
    AddString(value, "tagline", metadata->tagline);
    AddString(value, "logoUrl", metadata->logoUrl);
    AddString(value, "bannerUrl", metadata->bannerUrl);
    json_object_object_add(event.get(), "server", value);
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
    AddBoolean(value, "featured", metadata->serverId == FEATURED_SERVER_ID);
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

void CServerBrowserWeb::QueueSettingsState(bool initial)
{
    if (!m_settingsReady || !m_settings || !m_settings->IsActive())
        return;

    m_settings->UpdateInputCapture();
    m_settings->RefreshManagedValues();
    const CWebSettingsSession::SState& state = m_settings->GetState();

    JsonPtr      event = MakeObject();
    json_object* values = json_object_new_object();
    AddString(values, "game.nickname", state.gameNickname);
    AddBoolean(values, "game.savePasswords", state.gameSavePasswords);
    AddBoolean(values, "game.autoRefreshBrowser", state.gameAutoRefreshBrowser);
    AddBoolean(values, "game.allowScreenUpload", state.gameAllowScreenUpload);
    AddBoolean(values, "game.allowExternalSounds", state.gameAllowExternalSounds);
    AddBoolean(values, "game.alwaysShowTransferBox", state.gameAlwaysShowTransferBox);
    AddBoolean(values, "game.discordRichPresence", state.gameDiscordRichPresence);
    AddBoolean(values, "game.discordShareData", state.gameDiscordShareData);
    AddBoolean(values, "game.steamStatus", state.gameSteamStatus);
    AddBoolean(values, "game.saveCameraPhotos", state.gameSaveCameraPhotos);
    AddBoolean(values, "game.askBeforeDisconnect", state.gameAskBeforeDisconnect);
    AddBoolean(values, "game.customizedSAFiles", state.gameCustomizedSAFiles);
    AddInteger(values, "game.mapOpacity", state.gameMapOpacity);
    AddInteger(values, "game.mapImage", state.gameMapImage);
    AddInteger(values, "audio.masterVolume", state.audioMasterVolume);
    AddInteger(values, "audio.radioVolume", state.audioRadioVolume);
    AddInteger(values, "audio.sfxVolume", state.audioSfxVolume);
    AddInteger(values, "audio.mtaVolume", state.audioMtaVolume);
    AddInteger(values, "audio.voiceVolume", state.audioVoiceVolume);
    AddBoolean(values, "audio.radioEqualizer", state.audioRadioEqualizer);
    AddBoolean(values, "audio.radioAutotune", state.audioRadioAutotune);
    AddBoolean(values, "audio.userTrackAutoScan", state.audioUserTrackAutoScan);
    AddInteger(values, "audio.userTrackMode", state.audioUserTrackMode);
    AddBoolean(values, "audio.muteMaster", state.audioMuteMaster);
    AddBoolean(values, "audio.muteRadio", state.audioMuteRadio);
    AddBoolean(values, "audio.muteSfx", state.audioMuteSfx);
    AddBoolean(values, "audio.muteMta", state.audioMuteMta);
    AddBoolean(values, "audio.muteVoice", state.audioMuteVoice);
    AddBoolean(values, "controls.invertMouse", state.controlsInvertMouse);
    AddBoolean(values, "controls.steerWithMouse", state.controlsSteerWithMouse);
    AddBoolean(values, "controls.flyWithMouse", state.controlsFlyWithMouse);
    AddInteger(values, "controls.mouseSensitivity", state.controlsMouseSensitivity);
    AddInteger(values, "controls.verticalAimSensitivity", state.controlsVerticalAimSensitivity);
    AddBoolean(values, "controls.useMouseSensitivityForAiming", state.controlsUseMouseSensitivityForAiming);
    AddBoolean(values, "controls.classicControls", state.controlsClassic);
    AddInteger(values, "controls.joypadDeadZone", state.controlsJoypadDeadZone);
    AddInteger(values, "controls.joypadSaturation", state.controlsJoypadSaturation);
    AddString(values, "interface.locale", state.interfaceLocale);
    AddString(values, "interface.skin", state.interfaceSkin);
    AddDouble(values, "interface.chatBackgroundColor", state.interfaceChatBackgroundColor);
    AddDouble(values, "interface.chatTextColor", state.interfaceChatTextColor);
    AddDouble(values, "interface.chatInputBackgroundColor", state.interfaceChatInputBackgroundColor);
    AddDouble(values, "interface.chatInputTextColor", state.interfaceChatInputTextColor);
    AddInteger(values, "interface.chatFont", state.interfaceChatFont);
    AddInteger(values, "interface.chatLines", state.interfaceChatLines);
    AddDouble(values, "interface.chatScaleX", state.interfaceChatScaleX);
    AddDouble(values, "interface.chatScaleY", state.interfaceChatScaleY);
    AddDouble(values, "interface.chatWidth", state.interfaceChatWidth);
    AddBoolean(values, "interface.chatCssText", state.interfaceChatCssText);
    AddBoolean(values, "interface.chatCssBackground", state.interfaceChatCssBackground);
    AddBoolean(values, "interface.chatNickCompletion", state.interfaceChatNickCompletion);
    AddBoolean(values, "interface.chatTextOutline", state.interfaceChatTextOutline);
    AddDouble(values, "interface.chatLineLife", state.interfaceChatLineLife);
    AddDouble(values, "interface.chatLineFadeOut", state.interfaceChatLineFadeOut);
    AddInteger(values, "interface.chatPositionHorizontal", state.interfaceChatPositionHorizontal);
    AddInteger(values, "interface.chatPositionVertical", state.interfaceChatPositionVertical);
    AddInteger(values, "interface.chatTextAlignment", state.interfaceChatTextAlignment);
    AddDouble(values, "interface.chatOffsetX", state.interfaceChatOffsetX);
    AddDouble(values, "interface.chatOffsetY", state.interfaceChatOffsetY);
    AddBoolean(values, "interface.flashWindow", state.interfaceFlashWindow);
    AddBoolean(values, "interface.trayNotifications", state.interfaceTrayNotifications);
    AddBoolean(values, "browser.remoteWebsites", state.browserRemoteWebsites);
    AddBoolean(values, "browser.remoteJavascript", state.browserRemoteJavascript);
    AddBoolean(values, "browser.gpuRendering", state.browserGpuRendering);
    AddBoolean(values, "browser.videoAcceleration", state.browserVideoAcceleration);
    AddInteger(values, "advanced.fastClothesLoading", state.advancedFastClothesLoading);
    AddInteger(values, "advanced.browserSpeed", state.advancedBrowserSpeed);
    AddInteger(values, "advanced.singleConnection", state.advancedSingleConnection);
    AddInteger(values, "advanced.packetTag", state.advancedPacketTag);
    AddInteger(values, "advanced.progressAnimation", state.advancedProgressAnimation);
    AddInteger(values, "advanced.processPriority", state.advancedProcessPriority);
    AddInteger(values, "advanced.debugSetting", state.advancedDebugSetting);
    AddInteger(values, "advanced.streamingMemory", state.advancedStreamingMemory);
    AddBoolean(values, "advanced.cpuAffinity", state.advancedCpuAffinity);
    AddInteger(values, "advanced.updateBuildType", state.advancedUpdateBuildType);
    AddInteger(values, "advanced.updateAutoInstall", state.advancedUpdateAutoInstall);
    AddBoolean(values, "extendedWorld.enabled", state.extendedWorldEnabled);
    AddInteger(values, "extendedWorld.distance", state.extendedWorldDistance);
    AddBoolean(values, "distantLights.enabled", state.distantLightsEnabled);
    AddInteger(values, "distantLights.distance", state.distantLightsDistance);
    AddDouble(values, "distantLights.coronaSize", state.distantLightsCoronaSize);
    AddBoolean(values, "skyGfx.enabled", state.skyGfx.enabled != 0);
    AddBoolean(values, "skyGfx.colorFilter", state.skyGfx.ps2ColorFilter != 0);
    AddBoolean(values, "skyGfx.colorFilterBlur", state.skyGfx.ps2ColorFilterBlur != 0);
    AddBoolean(values, "skyGfx.pcTimecycle", state.skyGfx.ps2ColorFilterPcTimecycle != 0);
    AddBoolean(values, "skyGfx.depthBias", state.skyGfx.ps2DepthBias != 0);
    AddBoolean(values, "skyGfx.ycbcr", state.skyGfx.ycbcrCorrection != 0);
    AddBoolean(values, "skyGfx.radiosity", state.skyGfx.ps2Radiosity != 0);
    AddInteger(values, "skyGfx.radiosityIntensity", state.skyGfx.ps2RadiosityIntensity);
    AddInteger(values, "skyGfx.radiosityFilterPasses", state.skyGfx.ps2RadiosityFilterPasses);
    AddInteger(values, "skyGfx.radiosityRenderPasses", state.skyGfx.ps2RadiosityRenderPasses);
    AddInteger(values, "radar.style", state.radarStyle);
    AddDouble(values, "radar.positionX", state.radarPositionX);
    AddDouble(values, "radar.positionY", state.radarPositionY);
    AddDouble(values, "radar.width", state.radarWidth);
    AddDouble(values, "radar.height", state.radarHeight);
    AddBoolean(values, "radar.widescreenSafe", state.radarWidescreenSafe);
    AddInteger(values, "graphics.videoMode", state.graphicsVideoMode);
    AddInteger(values, "graphics.displayMode", state.graphicsDisplayMode);
    AddBoolean(values, "graphics.fullscreenMinimize", state.graphicsFullscreenMinimize);
    AddBoolean(values, "graphics.vsync", state.graphicsVSync);
    AddBoolean(values, "graphics.dpiAware", state.graphicsDPIAware);
    AddInteger(values, "graphics.fov", state.graphicsFieldOfView);
    AddInteger(values, "graphics.drawDistance", state.graphicsDrawDistance);
    AddInteger(values, "graphics.brightness", state.graphicsBrightness);
    AddInteger(values, "graphics.fxQuality", state.graphicsFXQuality);
    AddInteger(values, "graphics.anisotropic", state.graphicsAnisotropic);
    AddInteger(values, "graphics.antiAliasing", state.graphicsAntiAliasing);
    AddInteger(values, "graphics.aspectRatio", state.graphicsAspectRatio);
    AddBoolean(values, "graphics.hudMatchAspectRatio", state.graphicsHudMatchAspectRatio);
    AddBoolean(values, "graphics.volumetricShadows", state.graphicsVolumetricShadows);
    AddBoolean(values, "graphics.grass", state.graphicsGrass);
    AddBoolean(values, "graphics.heatHaze", state.graphicsHeatHaze);
    AddBoolean(values, "graphics.tyreSmoke", state.graphicsTyreSmoke);
    AddBoolean(values, "graphics.dynamicPedShadows", state.graphicsDynamicPedShadows);
    AddBoolean(values, "graphics.motionBlur", state.graphicsMotionBlur);
    AddBoolean(values, "graphics.coronaReflections", state.graphicsCoronaReflections);
    AddBoolean(values, "graphics.highDetailVehicles", state.graphicsHighDetailVehicles);
    AddBoolean(values, "graphics.highDetailPeds", state.graphicsHighDetailPeds);
    AddBoolean(values, "graphics.showUnsafeResolutions", state.graphicsShowUnsafeResolutions);
    AddBoolean(values, "graphics.deviceSelectionDialog", state.graphicsDeviceSelectionDialog);
    AddDouble(values, "graphics.gamma", state.graphicsGamma);
    AddDouble(values, "graphics.brightnessScale", state.graphicsBrightnessScale);
    AddDouble(values, "graphics.contrast", state.graphicsContrast);
    AddDouble(values, "graphics.saturation", state.graphicsSaturation);
    AddBoolean(values, "graphics.gammaEnabled", state.graphicsGammaEnabled);
    AddBoolean(values, "graphics.brightnessEnabled", state.graphicsBrightnessEnabled);
    AddBoolean(values, "graphics.contrastEnabled", state.graphicsContrastEnabled);
    AddBoolean(values, "graphics.saturationEnabled", state.graphicsSaturationEnabled);
    AddBoolean(values, "graphics.applyWindowed", state.graphicsApplyWindowed);
    AddBoolean(values, "graphics.applyFullscreen", state.graphicsApplyFullscreen);
    json_object_object_add(event.get(), "values", values);

    json_object* managed = json_object_new_object();
    AddBoolean(managed, "skyGfx", m_settings->IsSkyGfxManaged());
    AddBoolean(managed, "radar", m_settings->IsRadarManaged());
    json_object_object_add(event.get(), "managed", managed);

    json_object* availability = json_object_new_object();
    AddBoolean(availability, "rebuildDistantLights", m_settings->IsDistantLightsRebuildAvailable());
    AddInteger(availability, "maxAnisotropic", m_settings->GetMaxAnisotropic());
    AddBoolean(availability, "multiMonitor", m_settings->IsMultiMonitor());
    AddBoolean(availability, "unsafeResolutions", m_settings->HasUnsafeResolutions());
    AddBoolean(availability, "customizedSAFiles", CWebSettingsSession::HasCustomizedSAFilesOption());
    AddBoolean(availability, "connected", g_pCore->IsConnected());
    AddInteger(availability, "streamingMemoryMin", g_pCore->GetMinStreamingMemory());
    AddInteger(availability, "streamingMemoryMax", g_pCore->GetMaxStreamingMemory());
    AddString(availability, "resourceCachePath", GetCommonRegistryValue("", "File Cache Path"));
    json_object_object_add(event.get(), "availability", availability);
    json_object* resolutions = json_object_new_array();
    for (const CWebSettingsSession::SResolution& resolution : m_settings->GetResolutions())
    {
        json_object* value = json_object_new_object();
        AddInteger(value, "mode", resolution.mode);
        AddInteger(value, "width", resolution.width);
        AddInteger(value, "height", resolution.height);
        AddInteger(value, "depth", resolution.depth);
        AddBoolean(value, "unsafe", resolution.unsafe);
        json_object_array_add(resolutions, value);
    }
    json_object_object_add(event.get(), "resolutions", resolutions);

    json_object* locales = json_object_new_array();
    for (const std::string& locale : m_settings->GetLocales())
    {
        json_object* value = json_object_new_object();
        AddString(value, "code", locale);
        AddString(value, "label", g_pLocalization->GetLanguageNativeName(locale));
        json_object_array_add(locales, value);
    }
    json_object_object_add(event.get(), "locales", locales);

    json_object* chatPresets = json_object_new_array();
    for (const CWebSettingsSession::SChatPreset& preset : m_settings->GetChatPresets())
    {
        json_object* value = json_object_new_object();
        AddString(value, "id", preset.id);
        AddString(value, "name", preset.name);
        json_object_array_add(chatPresets, value);
    }
    json_object_object_add(event.get(), "chatPresets", chatPresets);

    json_object* skins = json_object_new_array();
    for (const std::string& skin : m_settings->GetSkins())
        json_object_array_add(skins, json_object_new_string(skin.c_str()));
    json_object_object_add(event.get(), "skins", skins);

    const auto addStringArray = [](json_object* owner, const char* name, const std::vector<std::string>& entries)
    {
        json_object* array = json_object_new_array();
        for (const std::string& entry : entries)
            json_object_array_add(array, json_object_new_string(entry.c_str()));
        json_object_object_add(owner, name, array);
    };
    addStringArray(event.get(), "browserBlacklist", state.browserBlacklist);
    addStringArray(event.get(), "browserWhitelist", state.browserWhitelist);

    json_object* binds = json_object_new_array();
    for (const CWebSettingsSession::SBindRow& bind : state.binds)
    {
        json_object* value = json_object_new_object();
        AddString(value, "id", bind.id);
        AddString(value, "section", bind.section);
        AddString(value, "label", bind.label);
        addStringArray(value, "keys", bind.keys);
        json_object_array_add(binds, value);
    }
    json_object_object_add(event.get(), "binds", binds);

    CJoystickManagerInterface* joystick = GetJoystickManager();
    json_object*               joypad = json_object_new_object();
    const bool                 joypadConnected = joystick && joystick->IsJoypadConnected();
    AddBoolean(joypad, "connected", joypadConnected);
    AddString(joypad, "name", joypadConnected ? joystick->GetControllerName() : "No joypad detected");
    AddInteger(joypad, "capturingAxis", m_settings->GetCapturedAxis());
    json_object* axes = json_object_new_array();
    if (joystick)
    {
        for (int index = 0; index < joystick->GetOutputCount(); ++index)
        {
            json_object* axis = json_object_new_object();
            AddInteger(axis, "index", index);
            AddString(axis, "output", joystick->GetOutputName(index));
            AddString(axis, "input", joystick->GetOutputInputName(index));
            json_object_array_add(axes, axis);
        }
    }
    json_object_object_add(joypad, "axes", axes);
    json_object_object_add(event.get(), "joypad", joypad);

    if (m_settings->GetCapturedBindId().empty())
        json_object_object_add(event.get(), "capture", json_object_new_null());
    else
    {
        json_object* capture = json_object_new_object();
        AddString(capture, "bindId", m_settings->GetCapturedBindId());
        AddInteger(capture, "slot", m_settings->GetCapturedBindSlot());
        json_object_object_add(event.get(), "capture", capture);
    }
    AddString(event.get(), "error", m_settings->GetError());
    AddString(event.get(), "skyGfxStatus", m_settings->GetSkyGfxStatus());
    AddBoolean(event.get(), "dirty", m_settings->IsDirty());
    AddBoolean(event.get(), "restartRequired", m_settings->RequiresRestart());
    AddBoolean(event.get(), "disconnectRequired", m_settings->RequiresDisconnect());

    const std::string signature = ToJson(event.get());
    if (!initial && signature == m_lastSettingsState)
        return;

    m_lastSettingsState = signature;
    AddString(event.get(), "type", initial ? "init" : "state");
    QueueEvent("settings", ToJson(event.get()));
}

void CServerBrowserWeb::QueueEvent(const std::string& channel, const std::string& json)
{
    if (channel == "menu")
        m_menuEvents.push_back(json);
    else if (channel == "settings")
        m_settingsEvents.push_back(json);
    else
        m_serverEvents.push_back(json);
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
    if (!m_webView || !m_documentReady || (m_menuEvents.empty() && m_serverEvents.empty() && m_settingsEvents.empty()))
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
    appendChannel("__neonSettings", m_settingsEvents, remaining, script);
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
    m_hasSentRefreshProgress = false;
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

    // Claim the connection UI before any early modal path. Passworded servers
    // can ask for credentials without starting a network attempt, and that
    // prompt must still remain owned by the web server browser.
    m_connectionUiActive = true;
    CancelHibernateRequest();
    UpdateRenderingPauseState();

    CServerListItem* server = m_registry ? m_registry->Find(host, port) : nullptr;
    if (!server)
        server = m_serverBrowser.FindServer(host, port);
    if (server && server->bPassworded && password.empty())
    {
        NotifyConnectionPasswordRequired(host, port, false);
        return;
    }

    // Publish the target before native validation begins. This gives the
    // always-mounted shell enough context to present even an immediate local
    // failure (for example an invalid nickname or unavailable network module).
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
