/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CNativeWorldCacheSA.h
 *  PURPOSE:     Immutable content-addressed cache for native world packs
 *
 *****************************************************************************/

#pragma once

#include <string>

struct SNativeWorldCacheFileSA
{
    std::string  name;
    std::string  sha256;
    unsigned int bytes{};
};

struct SNativeWorldCacheRequestSA
{
    unsigned int            format{};
    std::string             sourceRelativeDirectory;
    std::string             packId;
    std::string             manifestFileName;
    std::string             sourceManifestSha256;
    unsigned int            sourceManifestBytes{};
    unsigned int            maximumManifestBytes{};
    std::string             contentId;
    SNativeWorldCacheFileSA ide;
    SNativeWorldCacheFileSA img;
};

std::string GenerateNativeWorldContentId(const SNativeWorldCacheRequestSA& request);

// Publishes a legacy, locally installed pack into the process-owned cache and
// returns the absolute directory GTA may use. The returned pending lease must
// be released on every precommit refusal or committed after native activation.
bool PrepareAndLockNativeWorldCache(const SNativeWorldCacheRequestSA& request, std::string& publishedDirectory, bool& cacheHit, std::string& error);
void CommitNativeWorldCacheLease();
void ReleaseNativeWorldCacheLease();
