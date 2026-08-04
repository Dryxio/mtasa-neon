/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto: San Andreas
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/utils/CNeonServerAnnouncer.cpp
 *  PURPOSE:     Neon public server registry heartbeat
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CNeonServerAnnouncer.h"

#include "CMainConfig.h"
#include <json.h>

namespace
{
    constexpr long long HEARTBEAT_INTERVAL_MS = 2 * 60 * 1000;
    constexpr long long RETRY_INTERVAL_MS = 30 * 1000;

    void AddString(json_object* object, const char* name, const std::string& value)
    {
        json_object_object_add(object, name, json_object_new_string_len(value.data(), static_cast<int>(value.size())));
    }

    void AddStringArray(json_object* object, const char* name, const std::vector<SString>& values)
    {
        json_object* array = json_object_new_array();
        for (const SString& value : values)
            json_object_array_add(array, json_object_new_string_len(value.data(), static_cast<int>(value.size())));
        json_object_object_add(object, name, array);
    }
}

CNeonServerAnnouncer::CNeonServerAnnouncer(CMainConfig& config) : m_config(config)
{
}

CNeonServerAnnouncer::~CNeonServerAnnouncer()
{
    CNetHTTPDownloadManagerInterface* http = g_pNetServer ? g_pNetServer->GetHTTPDownloadManager(EDownloadMode::ASE) : nullptr;
    if (http && m_requestPending)
        http->CancelDownload(this, StaticDownloadFinished);
}

void CNeonServerAnnouncer::Pulse()
{
    if (!IsEnabled() || m_requestPending)
        return;

    const long long now = GetTickCount64_();
    if (now < m_nextHeartbeatAt)
        return;

    CNetHTTPDownloadManagerInterface* http = g_pNetServer ? g_pNetServer->GetHTTPDownloadManager(EDownloadMode::ASE) : nullptr;
    if (!http)
        return;

    SHttpRequestOptions options;
    options.strRequestMethod = "POST";
    options.strPostData = BuildHeartbeatBody();
    options.uiConnectionAttempts = 1;
    options.uiConnectTimeoutMs = 10000;
    options.uiMaxRedirects = 0;
    options.requestHeaders["Content-Type"] = "application/json";
    options.requestHeaders["Accept"] = "application/json";

    m_nextHeartbeatAt = now + RETRY_INTERVAL_MS;
    if (http->QueueFile(m_config.GetNeonRegistryUrl().c_str(), nullptr, this, StaticDownloadFinished, options))
        m_requestPending = true;
    else if (!m_loggedFailure)
    {
        CLogger::LogPrint("Neon registry heartbeat could not be queued; retrying later\n");
        m_loggedFailure = true;
    }
}

void CNeonServerAnnouncer::StaticDownloadFinished(const SHttpDownloadResult& result)
{
    if (result.pObj)
        static_cast<CNeonServerAnnouncer*>(result.pObj)->DownloadFinished(result);
}

void CNeonServerAnnouncer::DownloadFinished(const SHttpDownloadResult& result)
{
    m_requestPending = false;
    const bool accepted = result.bSuccess && result.iErrorCode >= 200 && result.iErrorCode < 300;
    if (accepted)
    {
        m_nextHeartbeatAt = GetTickCount64_() + HEARTBEAT_INTERVAL_MS;
        m_loggedFailure = false;
        if (!m_registered)
            CLogger::LogPrint("Neon registry heartbeat accepted; this public server is discoverable\n");
        m_registered = true;
        return;
    }

    m_nextHeartbeatAt = GetTickCount64_() + RETRY_INTERVAL_MS;
    m_registered = false;
    if (!m_loggedFailure)
    {
        CLogger::LogPrintf("Neon registry heartbeat failed (%d); retrying later\n", result.iErrorCode);
        m_loggedFailure = true;
    }
}

SString CNeonServerAnnouncer::BuildHeartbeatBody() const
{
    json_object* root = json_object_new_object();
    json_object_object_add(root, "registry_protocol", json_object_new_int(1));
    json_object_object_add(root, "game_port", json_object_new_int(m_config.GetServerPort()));
    json_object_object_add(root, "http_port", json_object_new_int(m_config.GetHTTPPort()));

    const SString version("%d.%d.%d-%d.%05d", MTASA_VERSION_MAJOR, MTASA_VERSION_MINOR, MTASA_VERSION_MAINTENANCE, MTASA_VERSION_TYPE, MTASA_VERSION_BUILD);
    AddString(root, "server_version", version);
    AddString(root, "name", m_config.GetServerName());
    AddString(root, "tagline", m_config.GetNeonRegistryTagline());
    AddString(root, "description", m_config.GetNeonRegistryDescription());
    AddStringArray(root, "countries", m_config.GetNeonRegistryCountries());
    AddStringArray(root, "languages", m_config.GetNeonRegistryLanguages());

    json_object* links = json_object_new_array();
    const auto   addLink = [links](const char* kind, const char* label, const std::string& url)
    {
        if (url.empty())
            return;
        json_object* link = json_object_new_object();
        AddString(link, "kind", kind);
        AddString(link, "label", label);
        AddString(link, "url", url);
        json_object_array_add(links, link);
    };
    addLink("website", "Website", m_config.GetNeonRegistryWebsite());
    addLink("discord", "Discord", m_config.GetNeonRegistryDiscord());
    json_object_object_add(root, "links", links);
    if (!m_config.GetNeonRegistryAccent().empty())
        AddString(root, "accent", m_config.GetNeonRegistryAccent());

    const SString body = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
    json_object_put(root);
    return body;
}

bool CNeonServerAnnouncer::IsEnabled() const
{
    return m_config.GetNeonRegistryEnabled() && (m_config.GetAseInternetListenEnabled() || m_config.GetAseInternetPushEnabled());
}
