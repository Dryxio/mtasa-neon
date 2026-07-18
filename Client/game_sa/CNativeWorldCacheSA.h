/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CNativeWorldCacheSA.h
 *  PURPOSE:     Immutable content-addressed cache for native world packs
 *
 *****************************************************************************/

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>

struct SNativeWorldCacheFileSA
{
    std::string  name;
    std::string  sha256;
    unsigned int bytes{};
};

struct SNativeWorldCacheRequestSA
{
    unsigned int                      format{};
    std::string                       sourceRelativeDirectory;
    std::string                       sourceAbsoluteDirectory;
    std::string                       policyKey;
    std::string                       packId;
    std::string                       manifestFileName;
    std::string                       sourceManifestSha256;
    unsigned int                      sourceManifestBytes{};
    unsigned int                      maximumManifestBytes{};
    std::string                       contentId;
    std::shared_ptr<std::atomic_bool> cancellation;
    SNativeWorldCacheFileSA           ide;
    SNativeWorldCacheFileSA           img;
};

using NativeWorldCacheAuditSA = std::function<bool(const std::string& quarantineDirectory, std::string& error)>;

class CNativeWorldCacheLeaseSA
{
public:
    CNativeWorldCacheLeaseSA();
    ~CNativeWorldCacheLeaseSA();
    CNativeWorldCacheLeaseSA(CNativeWorldCacheLeaseSA&&) noexcept;
    CNativeWorldCacheLeaseSA& operator=(CNativeWorldCacheLeaseSA&&) noexcept;

    CNativeWorldCacheLeaseSA(const CNativeWorldCacheLeaseSA&) = delete;
    CNativeWorldCacheLeaseSA& operator=(const CNativeWorldCacheLeaseSA&) = delete;

    bool IsValid() const;
    bool RevalidateClosedObject(std::string& error) const;
    bool Commit(const std::string& policy, const std::string& contentId, const std::string& ticketId, std::string& error);
    void Release();

private:
    struct SImpl;
    std::unique_ptr<SImpl> m_impl;

    friend bool AcquireExistingNativeWorldCacheLease(const SNativeWorldCacheRequestSA&, const std::string&, const NativeWorldCacheAuditSA&,
                                                     CNativeWorldCacheLeaseSA&, std::string&, std::string&);
};

std::string GenerateNativeWorldContentId(const SNativeWorldCacheRequestSA& request);

// Publishes a legacy, locally installed pack into the process-owned cache and
// returns the absolute directory GTA may use. The returned pending lease must
// be released on every precommit refusal or committed after native activation.
bool PrepareAndLockNativeWorldCache(const SNativeWorldCacheRequestSA& request, std::string& publishedDirectory, bool& cacheHit, std::string& error);
// Repeatable transport path: publishes exact audited bytes but deliberately
// returns with every handle closed and no activation lease retained.
bool PublishNativeWorldCache(const SNativeWorldCacheRequestSA& request, const NativeWorldCacheAuditSA& audit, std::string& publishedDirectory, bool& cacheHit,
                             std::string& error);
// Opens only the exact already-published semantic object. It never creates,
// repairs, quarantines, or scans, and runs the closed audit while every exact
// handle is held by a transaction-typed lease.
bool AcquireExistingNativeWorldCacheLease(const SNativeWorldCacheRequestSA& request, const std::string& ticketId, const NativeWorldCacheAuditSA& audit,
                                          CNativeWorldCacheLeaseSA& lease, std::string& publishedDirectory, std::string& error);
void CommitNativeWorldCacheLease();
void ReleaseNativeWorldCacheLease();
