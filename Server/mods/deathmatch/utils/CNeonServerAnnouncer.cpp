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
#include "CNeonIdentityTicket.h"
#include "CNeonServerIdentity.h"
#include <json.h>

namespace
{
    constexpr long long HEARTBEAT_INTERVAL_MS = 2 * 60 * 1000;
    constexpr long long RETRY_INTERVAL_MS = 15 * 1000;
    constexpr long long MAX_RETRY_INTERVAL_MS = 2 * 60 * 1000;
    constexpr int       MIN_LEASE_SECONDS = 60;
    constexpr int       MAX_LEASE_SECONDS = 10 * 60;

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

CNeonServerAnnouncer::CNeonServerAnnouncer(CMainConfig& config, CNeonServerIdentity* identity, CNeonIdentityTicketVerifier* ticketVerifier)
    : m_config(config), m_identity(identity), m_ticketVerifier(ticketVerifier)
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
    if (m_identity)
    {
        std::string       timestamp;
        std::string       nonce;
        std::string       signature;
        std::string       error;
        const std::string heartbeatBody(options.strPostData.data(), options.strPostData.size());
        if (!m_identity->SignHeartbeat(heartbeatBody, timestamp, nonce, signature, error))
        {
            if (!m_loggedFailure)
                CLogger::LogPrintf("[Neon Identity] %s\n", error.c_str());
            m_loggedFailure = true;
            m_nextHeartbeatAt = now + RETRY_INTERVAL_MS;
            return;
        }
        options.requestHeaders["X-Neon-Server-Key"] = m_identity->GetPublicKey();
        options.requestHeaders["X-Neon-Server-Timestamp"] = timestamp;
        options.requestHeaders["X-Neon-Server-Nonce"] = nonce;
        options.requestHeaders["X-Neon-Server-Signature"] = signature;
    }

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
    bool        accepted = result.bSuccess && result.iErrorCode >= 200 && result.iErrorCode < 300;
    int         expiresIn = 0;
    std::string endpoint;
    if (accepted && m_identity)
    {
        accepted = false;
        if (result.pData && result.dataSize > 0 && result.dataSize <= 4096)
        {
            json_tokener* tokener = json_tokener_new();
            json_object*  response = tokener ? json_tokener_parse_ex(tokener, result.pData, static_cast<int>(result.dataSize)) : nullptr;
            const bool    parsed = tokener && json_tokener_get_error(tokener) == json_tokener_success && response &&
                                json_object_get_type(response) == json_type_object && json_tokener_get_parse_end(tokener) == result.dataSize;
            json_object* statusValue = nullptr;
            json_object* serverIdValue = nullptr;
            json_object* endpointValue = nullptr;
            json_object* expiresValue = nullptr;
            if (parsed && json_object_object_get_ex(response, "status", &statusValue) && json_object_get_type(statusValue) == json_type_string &&
                strcmp(json_object_get_string(statusValue), "registered") == 0 && json_object_object_get_ex(response, "server_id", &serverIdValue) &&
                json_object_get_type(serverIdValue) == json_type_string && m_identity->GetServerId() == json_object_get_string(serverIdValue) &&
                json_object_object_get_ex(response, "endpoint", &endpointValue) && json_object_get_type(endpointValue) == json_type_string &&
                json_object_object_get_ex(response, "expires_in", &expiresValue) && json_object_get_type(expiresValue) == json_type_int)
            {
                endpoint = json_object_get_string(endpointValue);
                expiresIn = json_object_get_int(expiresValue);
                const std::string portSuffix = ":" + std::to_string(m_config.GetServerPort());
                accepted = endpoint.size() > portSuffix.size() && endpoint.compare(endpoint.size() - portSuffix.size(), portSuffix.size(), portSuffix) == 0 &&
                           expiresIn >= MIN_LEASE_SECONDS && expiresIn <= MAX_LEASE_SECONDS;
            }
            if (response)
                json_object_put(response);
            if (tokener)
                json_tokener_free(tokener);
        }
    }
    if (accepted)
    {
        const long long now = GetTickCount64_();
        const long long leaseInterval = m_identity ? static_cast<long long>(expiresIn) * 1000 : HEARTBEAT_INTERVAL_MS + 60 * 1000;
        m_identityExpiresAt = m_identity ? now + leaseInterval - 10 * 1000 : 0;
        m_nextHeartbeatAt = now + std::min(HEARTBEAT_INTERVAL_MS, leaseInterval / 2);
        m_consecutiveFailures = 0;
        m_loggedFailure = false;
        if (m_identity && m_ticketVerifier)
            m_ticketVerifier->SetExpectedEndpoint(endpoint);
        if (!m_registered)
        {
            if (m_identity)
                CLogger::LogPrintf("[Neon Identity] Registration accepted for %s%s\n", endpoint.c_str(),
                                   m_config.GetNeonRegistryEnabled() ? " and published" : "");
            else
                CLogger::LogPrint("Neon registry heartbeat accepted; this public server is discoverable\n");
        }
        m_registered = true;
        return;
    }

    m_nextHeartbeatAt = GetTickCount64_() + RetryDelay(result.iErrorCode);
    ++m_consecutiveFailures;
    if (!IsIdentityActive())
        m_registered = false;
    if (!m_loggedFailure)
    {
        CLogger::LogPrintf("Neon registry heartbeat failed (%d); retrying later\n", result.iErrorCode);
        m_loggedFailure = true;
    }
}

long long CNeonServerAnnouncer::RetryDelay(int statusCode)
{
    if (statusCode == 401 || statusCode == 403 || statusCode == 409)
        return 5 * 60 * 1000;
    if (statusCode == 429)
        return 60 * 1000;

    const unsigned int exponent = std::min(m_consecutiveFailures, 3u);
    const long long    delay = std::min(MAX_RETRY_INTERVAL_MS, RETRY_INTERVAL_MS << exponent);
    return delay + static_cast<long long>(rand() % 5000);
}

SString CNeonServerAnnouncer::BuildHeartbeatBody() const
{
    json_object* root = json_object_new_object();
    json_object_object_add(root, "registry_protocol", json_object_new_int(m_identity ? 2 : 1));
    if (m_identity)
    {
        json_object_object_add(root, "auth_enabled", json_object_new_boolean(true));
        json_object_object_add(root, "published", json_object_new_boolean(m_config.GetNeonRegistryEnabled()));
    }
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
    if (!m_config.GetNeonRegistryLogo().empty())
        AddString(root, "logo_url", m_config.GetNeonRegistryLogo());
    if (!m_config.GetNeonRegistryBanner().empty())
        AddString(root, "banner_url", m_config.GetNeonRegistryBanner());

    const SString body = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
    json_object_put(root);
    return body;
}

bool CNeonServerAnnouncer::IsEnabled() const
{
    return (m_config.GetNeonRegistryEnabled() || m_identity) && (m_config.GetAseInternetListenEnabled() || m_config.GetAseInternetPushEnabled());
}

bool CNeonServerAnnouncer::IsIdentityActive() const
{
    return !m_identity || (m_registered && GetTickCount64_() < m_identityExpiresAt);
}
