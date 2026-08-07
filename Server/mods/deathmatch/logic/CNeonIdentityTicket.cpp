/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/CNeonIdentityTicket.cpp
 *  PURPOSE:     Neon Identity connection-ticket verification
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CNeonIdentityTicket.h"

#include <cryptopp/base64.h>
#include <cryptopp/filters.h>
#include <cryptopp/xed25519.h>
#include <json.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>

namespace
{
    constexpr std::size_t MAX_TICKET_LENGTH = 4096;
    constexpr std::size_t MAX_CLAIM_LENGTH = 128;
    constexpr std::time_t MAX_TICKET_LIFETIME_SECONDS = 120;
    constexpr std::time_t CLOCK_SKEW_SECONDS = 5;

    class CJsonObject
    {
    public:
        explicit CJsonObject(json_object* object = nullptr) : m_object(object) {}
        ~CJsonObject()
        {
            if (m_object)
                json_object_put(m_object);
        }

        CJsonObject(const CJsonObject&) = delete;
        CJsonObject& operator=(const CJsonObject&) = delete;

        json_object* Get() const { return m_object; }

    private:
        json_object* m_object;
    };

    bool IsBase64Url(const std::string& value)
    {
        if (value.empty() || value.size() % 4 == 1)
            return false;

        return std::all_of(value.begin(), value.end(), [](unsigned char character) { return std::isalnum(character) || character == '-' || character == '_'; });
    }

    bool DecodeBase64Url(const std::string& encoded, std::string& decoded)
    {
        if (!IsBase64Url(encoded))
            return false;

        try
        {
            decoded.clear();
            CryptoPP::StringSource source(encoded, true, new CryptoPP::Base64URLDecoder(new CryptoPP::StringSink(decoded)));
            return true;
        }
        catch (const CryptoPP::Exception&)
        {
            decoded.clear();
            return false;
        }
    }

    bool SplitTicket(const std::string& ticket, std::string& header, std::string& payload, std::string& signature)
    {
        const std::size_t firstSeparator = ticket.find('.');
        if (firstSeparator == std::string::npos)
            return false;

        const std::size_t secondSeparator = ticket.find('.', firstSeparator + 1);
        if (secondSeparator == std::string::npos || ticket.find('.', secondSeparator + 1) != std::string::npos)
            return false;

        header = ticket.substr(0, firstSeparator);
        payload = ticket.substr(firstSeparator + 1, secondSeparator - firstSeparator - 1);
        signature = ticket.substr(secondSeparator + 1);
        return IsBase64Url(header) && IsBase64Url(payload) && IsBase64Url(signature);
    }

    CJsonObject ParseJson(const std::string& value)
    {
        json_tokener* tokener = json_tokener_new();
        if (!tokener)
            return CJsonObject();

        json_object* object = json_tokener_parse_ex(tokener, value.data(), static_cast<int>(value.size()));
        const bool   valid = json_tokener_get_error(tokener) == json_tokener_success && object && json_tokener_get_parse_end(tokener) == value.size();
        json_tokener_free(tokener);

        if (!valid)
        {
            if (object)
                json_object_put(object);
            return CJsonObject();
        }

        return CJsonObject(object);
    }

    bool ReadStringClaim(json_object* object, const char* name, std::string& value, bool allowEmpty = false)
    {
        json_object* field = nullptr;
        if (!json_object_object_get_ex(object, name, &field) || !field || json_object_get_type(field) != json_type_string)
            return false;

        const char* text = json_object_get_string(field);
        const int   length = json_object_get_string_len(field);
        if (!text || length < 0 || static_cast<std::size_t>(length) > MAX_CLAIM_LENGTH || (!allowEmpty && length == 0))
            return false;

        value.assign(text, static_cast<std::size_t>(length));
        return std::all_of(value.begin(), value.end(), [](unsigned char character) { return character >= 0x20 && character <= 0x7E; });
    }

    bool ReadTimeClaim(json_object* object, const char* name, std::time_t& value)
    {
        json_object* field = nullptr;
        if (!json_object_object_get_ex(object, name, &field) || !field || json_object_get_type(field) != json_type_int)
            return false;

        const std::int64_t numericValue = json_object_get_int64(field);
        if (numericValue <= 0 || numericValue > std::numeric_limits<std::time_t>::max())
            return false;

        value = static_cast<std::time_t>(numericValue);
        return true;
    }

    bool IsCanonicalIpv4Endpoint(const std::string& value)
    {
        const std::size_t separator = value.find(':');
        if (separator == std::string::npos || value.find(':', separator + 1) != std::string::npos)
            return false;

        const std::string portText = value.substr(separator + 1);
        if (portText.empty() || portText.size() > 5 || portText.front() == '0' ||
            !std::all_of(portText.begin(), portText.end(), [](unsigned char character) { return std::isdigit(character); }))
            return false;

        unsigned int port = 0;
        for (unsigned char character : portText)
        {
            port = port * 10 + (character - '0');
            if (port > 65535)
                return false;
        }

        const std::string address = value.substr(0, separator);
        std::size_t       start = 0;
        for (unsigned int index = 0; index < 4; ++index)
        {
            const std::size_t end = index == 3 ? address.size() : address.find('.', start);
            if (end == std::string::npos || end == start || (end - start > 1 && address[start] == '0'))
                return false;

            unsigned int octet = 0;
            for (std::size_t position = start; position < end; ++position)
            {
                const unsigned char character = address[position];
                if (!std::isdigit(character))
                    return false;
                octet = octet * 10 + (character - '0');
                if (octet > 255)
                    return false;
            }
            start = end + 1;
        }
        return start == address.size() + 1;
    }
}

bool CNeonIdentityTicketVerifier::IsValidAccountId(const std::string& accountId) noexcept
{
    if (accountId.size() != 36)
        return false;

    for (std::size_t index = 0; index < accountId.size(); ++index)
    {
        if (index == 8 || index == 13 || index == 18 || index == 23)
        {
            if (accountId[index] != '-')
                return false;
        }
        else if (!std::isdigit(static_cast<unsigned char>(accountId[index])) && (accountId[index] < 'a' || accountId[index] > 'f'))
            return false;
    }
    return true;
}

bool CNeonIdentityTicketVerifier::IsValidDiscordId(const std::string& discordId) noexcept
{
    return discordId.size() >= 17 && discordId.size() <= 20 &&
           std::all_of(discordId.begin(), discordId.end(), [](unsigned char character) { return std::isdigit(character); });
}

bool CNeonIdentityTicketVerifier::Configure(const std::string& issuer, const std::string& audience, const std::string& keyId,
                                            const std::string& publicKeyBase64Url, std::string& error)
{
    std::string publicKey;
    if (issuer.empty() || issuer.size() > MAX_CLAIM_LENGTH)
        error = "Neon Identity issuer is missing or too long";
    else if (audience.empty() || audience.size() > MAX_CLAIM_LENGTH)
        error = "Neon Identity server id is missing or too long";
    else if (keyId.empty() || keyId.size() > MAX_CLAIM_LENGTH)
        error = "Neon Identity key id is missing or too long";
    else if (!DecodeBase64Url(publicKeyBase64Url, publicKey) || publicKey.size() != CryptoPP::ed25519Verifier::PUBLIC_KEYLENGTH)
        error = "Neon Identity public key must be a 32-byte base64url Ed25519 key";
    else
    {
        m_issuer = issuer;
        m_audience = audience;
        m_keyId = keyId;
        m_publicKey = std::move(publicKey);
        m_consumedTickets.clear();
        error.clear();
        return true;
    }

    m_issuer.clear();
    m_audience.clear();
    m_keyId.clear();
    m_publicKey.clear();
    m_consumedTickets.clear();
    return false;
}

bool CNeonIdentityTicketVerifier::VerifyAndConsume(const std::string& ticket, SNeonIdentityClaims& claims, std::string& error)
{
    claims = {};
    if (m_publicKey.empty())
    {
        error = "Neon Identity verifier is not configured";
        return false;
    }
    if (ticket.empty() || ticket.size() > MAX_TICKET_LENGTH)
    {
        error = "Neon Identity ticket is missing or too large";
        return false;
    }

    std::string encodedHeader;
    std::string encodedPayload;
    std::string encodedSignature;
    if (!SplitTicket(ticket, encodedHeader, encodedPayload, encodedSignature))
    {
        error = "Neon Identity ticket has an invalid compact format";
        return false;
    }

    std::string headerText;
    std::string payloadText;
    std::string signature;
    if (!DecodeBase64Url(encodedHeader, headerText) || !DecodeBase64Url(encodedPayload, payloadText) || !DecodeBase64Url(encodedSignature, signature) ||
        signature.size() != CryptoPP::ed25519Verifier::SIGNATURE_LENGTH)
    {
        error = "Neon Identity ticket contains invalid base64url data";
        return false;
    }

    const CJsonObject header = ParseJson(headerText);
    const CJsonObject payload = ParseJson(payloadText);
    if (!header.Get() || json_object_get_type(header.Get()) != json_type_object || !payload.Get() || json_object_get_type(payload.Get()) != json_type_object)
    {
        error = "Neon Identity ticket contains invalid JSON";
        return false;
    }

    std::string algorithm;
    std::string type;
    std::string keyId;
    if (!ReadStringClaim(header.Get(), "alg", algorithm) || algorithm != "EdDSA" || !ReadStringClaim(header.Get(), "typ", type) || type != "JWT" ||
        !ReadStringClaim(header.Get(), "kid", keyId) || keyId != m_keyId)
    {
        error = "Neon Identity ticket header is not accepted";
        return false;
    }

    const std::string signedContent = encodedHeader + "." + encodedPayload;
    try
    {
        const CryptoPP::ed25519Verifier verifier(reinterpret_cast<const CryptoPP::byte*>(m_publicKey.data()));
        if (!verifier.VerifyMessage(reinterpret_cast<const CryptoPP::byte*>(signedContent.data()), signedContent.size(),
                                    reinterpret_cast<const CryptoPP::byte*>(signature.data()), signature.size()))
        {
            error = "Neon Identity ticket signature is invalid";
            return false;
        }
    }
    catch (const CryptoPP::Exception&)
    {
        error = "Neon Identity ticket signature could not be verified";
        return false;
    }

    std::string issuer;
    std::string audience;
    std::time_t issuedAt = 0;
    std::time_t notBefore = 0;
    if (!ReadStringClaim(payload.Get(), "iss", issuer) || issuer != m_issuer || !ReadStringClaim(payload.Get(), "aud", audience) || audience != m_audience ||
        !ReadStringClaim(payload.Get(), "sub", claims.accountId) || !ReadStringClaim(payload.Get(), "discord_id", claims.discordId) ||
        !ReadStringClaim(payload.Get(), "server_endpoint", claims.serverEndpoint) || !ReadStringClaim(payload.Get(), "jti", claims.ticketId) ||
        !ReadTimeClaim(payload.Get(), "iat", issuedAt) || !ReadTimeClaim(payload.Get(), "nbf", notBefore) ||
        !ReadTimeClaim(payload.Get(), "exp", claims.expiresAt) || !IsValidAccountId(claims.accountId) || !IsValidDiscordId(claims.discordId) ||
        !IsCanonicalIpv4Endpoint(claims.serverEndpoint))
    {
        error = "Neon Identity ticket claims are invalid";
        return false;
    }

    const std::time_t now = std::time(nullptr);
    if (issuedAt > now + CLOCK_SKEW_SECONDS || notBefore > now + CLOCK_SKEW_SECONDS || claims.expiresAt < now - CLOCK_SKEW_SECONDS ||
        notBefore < issuedAt - CLOCK_SKEW_SECONDS || claims.expiresAt <= issuedAt || claims.expiresAt - issuedAt > MAX_TICKET_LIFETIME_SECONDS)
    {
        error = "Neon Identity ticket is outside its accepted lifetime";
        return false;
    }

    PruneReplayCache(now);
    if (m_consumedTickets.find(claims.ticketId) != m_consumedTickets.end())
    {
        error = "Neon Identity ticket has already been used";
        return false;
    }

    // Tickets are deliberately one-shot. Recording the jti only after every
    // cryptographic and semantic check prevents invalid traffic from growing
    // the replay cache.
    m_consumedTickets.emplace(claims.ticketId, claims.expiresAt + CLOCK_SKEW_SECONDS);
    error.clear();
    return true;
}

void CNeonIdentityTicketVerifier::PruneReplayCache(std::time_t now)
{
    for (auto iter = m_consumedTickets.begin(); iter != m_consumedTickets.end();)
    {
        if (iter->second < now)
            iter = m_consumedTickets.erase(iter);
        else
            ++iter;
    }
}
