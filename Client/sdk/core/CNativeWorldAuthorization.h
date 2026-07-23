/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        sdk/core/CNativeWorldAuthorization.h
 *  PURPOSE:     Inert native-world startup authorization value types
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <array>
#include <string>

constexpr unsigned char NATIVE_WORLD_BULLWORTH_FORMAT = 1;
constexpr unsigned char NATIVE_WORLD_BULLWORTH_AUTHORIZATION_VERSION = 1;
constexpr unsigned char NATIVE_WORLD_BULLWORTH_POLICY = 1;
constexpr unsigned char NATIVE_WORLD_STATIC_V1_FORMAT = 2;
constexpr unsigned char NATIVE_WORLD_STATIC_V1_AUTHORIZATION_VERSION = 2;
constexpr unsigned char NATIVE_WORLD_STATIC_V1_POLICY = 2;
constexpr unsigned char NATIVE_WORLD_STATIC_V3_SET_FORMAT = 3;
constexpr unsigned char NATIVE_WORLD_STATIC_V3_SET_AUTHORIZATION_VERSION = 3;
constexpr unsigned char NATIVE_WORLD_STATIC_V3_SET_POLICY = 3;
constexpr unsigned char NATIVE_WORLD_ONE_SHOT_STARTUP_MODE = 1;

inline bool IsClosedNativeWorldStartupAuthorization(unsigned char wireVersion, unsigned char startupMode, unsigned char policy, unsigned char packFormat)
{
    return (packFormat == NATIVE_WORLD_BULLWORTH_FORMAT && wireVersion == NATIVE_WORLD_BULLWORTH_AUTHORIZATION_VERSION &&
            startupMode == NATIVE_WORLD_ONE_SHOT_STARTUP_MODE && policy == NATIVE_WORLD_BULLWORTH_POLICY) ||
           (packFormat == NATIVE_WORLD_STATIC_V1_FORMAT && wireVersion == NATIVE_WORLD_STATIC_V1_AUTHORIZATION_VERSION &&
            startupMode == NATIVE_WORLD_ONE_SHOT_STARTUP_MODE && policy == NATIVE_WORLD_STATIC_V1_POLICY) ||
           (packFormat == NATIVE_WORLD_STATIC_V3_SET_FORMAT && wireVersion == NATIVE_WORLD_STATIC_V3_SET_AUTHORIZATION_VERSION &&
            startupMode == NATIVE_WORLD_ONE_SHOT_STARTUP_MODE && policy == NATIVE_WORLD_STATIC_V3_SET_POLICY);
}

inline const char* GetNativeWorldStartupPolicyName(unsigned char packFormat)
{
    return packFormat == NATIVE_WORLD_BULLWORTH_FORMAT       ? "bullworth"
           : packFormat == NATIVE_WORLD_STATIC_V1_FORMAT     ? "static-world-v1"
           : packFormat == NATIVE_WORLD_STATIC_V3_SET_FORMAT ? "static-world-v3-set"
                                                             : "invalid";
}

struct SNativeWorldStartupAuthorization
{
    bool                          present{};
    unsigned char                 wireVersion{};
    unsigned char                 startupMode{};
    unsigned char                 policy{};
    unsigned char                 packFormat{};
    std::array<unsigned char, 32> serverIdDigest{};
    std::array<unsigned char, 4>  serverIpv4{};
    unsigned short                serverPort{};
    unsigned short                resourceNetId{};
    unsigned int                  resourceStartCounter{};
    unsigned short                bitstreamVersion{};
    unsigned long long            connectionGeneration{};
    unsigned long long            authorizationEpoch{};
    std::string                   resourceName;
};

struct SNativeWorldAuthorizationPublication
{
    bool        success{};
    std::string offerId;
    std::string contentId;
};

struct SNativeWorldAuthorizationRecordResult
{
    bool               success{};
    bool               found{};
    bool               idempotent{};
    bool               attached{};
    bool               publicationAmbiguous{};
    bool               claimed{};
    unsigned long long issuedAt{};
    unsigned long long expiresAt{};
    std::string        ticketId;
    std::string        diagnostic;
    std::string        error;
};

// Value-only view of a validated launch-2 record. Core keeps the transaction
// lock opaque; Game SA receives only the immutable identities needed to audit
// the exact cache object before asking Core to burn or claim the ticket.
struct SNativeWorldStartupSelection
{
    bool                          success{};
    bool                          found{};
    bool                          ready{};
    bool                          terminalRefusalRequired{};
    unsigned char                 wireVersion{};
    unsigned char                 startupMode{};
    unsigned char                 policy{};
    unsigned char                 packFormat{};
    std::array<unsigned char, 32> serverIdDigest{};
    std::array<unsigned char, 4>  serverIpv4{};
    unsigned short                serverPort{};
    unsigned short                bitstreamVersion{};
    unsigned long long            issuedAt{};
    unsigned long long            expiresAt{};
    std::string                   resourceName;
    std::string                   offerId;
    std::string                   contentId;
    std::string                   ticketId;
    std::string                   diagnostic;
    std::string                   error;
};
