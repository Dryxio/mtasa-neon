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
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct SNativeWorldCacheFileSA
{
    std::string   name;
    std::string   sha256;
    std::uint64_t bytes{};
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
    std::uint64_t                     sourceManifestBytes{};
    std::uint64_t                     maximumManifestBytes{};
    std::string                       contentId;
    std::shared_ptr<std::atomic_bool> cancellation;
    SNativeWorldCacheFileSA           ide;
    SNativeWorldCacheFileSA           lod;
    // Formats 1 and 2 use img. Format 3 uses images and leaves img empty,
    // keeping the legacy request layout source-compatible for its callers.
    SNativeWorldCacheFileSA              img;
    std::vector<SNativeWorldCacheFileSA> images;
};

struct SNativeWorldV3SetPackSA
{
    std::string packId;
    std::string contentId;
};

struct SNativeWorldV3SetRequestSA
{
    std::string                            sourceAbsoluteDirectory;
    std::string                            manifestFileName;
    std::string                            sourceManifestSha256;
    std::uint64_t                          sourceManifestBytes{};
    std::string                            setId;
    std::array<SNativeWorldV3SetPackSA, 4> packs;
    std::shared_ptr<std::atomic_bool>      cancellation;
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
    bool Commit(unsigned int format, const std::string& policy, const std::string& contentId, const std::string& ticketId, std::string& error);
    void Release();

private:
    struct SImpl;
    std::unique_ptr<SImpl> m_impl;

    friend bool AcquireExistingNativeWorldCacheLease(const SNativeWorldCacheRequestSA&, const std::string&, const NativeWorldCacheAuditSA&,
                                                     CNativeWorldCacheLeaseSA&, std::string&, std::string&);
    friend bool AcquireExistingNativeWorldV3SetLease(const SNativeWorldV3SetRequestSA&, const std::string&, const NativeWorldCacheAuditSA&,
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
bool        AcquireExistingNativeWorldCacheLease(const SNativeWorldCacheRequestSA& request, const std::string& ticketId, const NativeWorldCacheAuditSA& audit,
                                                 CNativeWorldCacheLeaseSA& lease, std::string& publishedDirectory, std::string& error);
std::string GenerateNativeWorldV3SetId(const SNativeWorldV3SetRequestSA& request);
bool PublishNativeWorldV3Set(const SNativeWorldV3SetRequestSA& request, const NativeWorldCacheAuditSA& audit, std::string& publishedDirectory, bool& cacheHit,
                             std::string& error);
bool AcquireExistingNativeWorldV3SetLease(const SNativeWorldV3SetRequestSA& request, const std::string& ticketId, const NativeWorldCacheAuditSA& audit,
                                          CNativeWorldCacheLeaseSA& lease, std::string& publishedDirectory, std::string& error);
void CommitNativeWorldCacheLease();
void ReleaseNativeWorldCacheLease();
