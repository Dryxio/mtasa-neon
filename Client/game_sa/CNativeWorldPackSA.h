/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CNativeWorldPackSA.h
 *  PURPOSE:     Native GTA streaming registration for reviewed world packs
 *
 *****************************************************************************/

#pragma once

#include "CNativeWorldPayloadValidatorSA.h"

#include <game/CGame.h>
#include <string>
#include <vector>

struct SNativeWorldTransportOffer;
struct SNativeWorldTransportPublishResult;
struct SNativeWorldStartupSelection;

class CStreamingSA;
class CVector;

struct SNativeTxdSlotFingerprintSA
{
    bool           configured;
    unsigned char  poolFlag;
    unsigned int   dictionary;
    unsigned short usages;
    unsigned short parent;
    unsigned int   hash;
    unsigned short prev;
    unsigned short next;
    unsigned short nextInImg;
    unsigned char  streamingFlags;
    unsigned char  archive;
    unsigned int   offset;
    unsigned int   size;
    unsigned int   loadState;
};

struct SNativeTxdPoolProfileSA
{
    const char*                 executableIdentity;
    const char*                 name;
    unsigned int                occupied;
    int                         firstFree;
    int                         fingerprintSlot;
    SNativeTxdSlotFingerprintSA fingerprint;
};

struct SNativeModelStoreUsageSA
{
    unsigned int atomic;
    unsigned int damageAtomic;
    unsigned int time;
};

// This policy is compiled into the client. Runtime manifests are treated as
// untrusted input and cannot select executable fingerprints, native pool
// capacities, stock occupancy, or archive allocation rules.
struct SNativeWorldPackPolicySA
{
    unsigned int format;
    const char*  key;
    const char*  auditProfile;
    const char*  displayName;
    const char*  logPrefix;
    const char*  featureEnvironment;
    const char*  relativeDirectory;
    const char*  runtimeManifestFileName;
    bool         acceptsVariablePackId;

    unsigned int  maximumManifestBytes;
    unsigned int  maximumIdeBytes;
    unsigned int  maximumImgSectors;
    unsigned int  maximumImgEntries;
    unsigned int  maximumImgEntryBlocks;
    unsigned int  maximumModelId;
    unsigned int  maximumModelCount;
    unsigned int  maximumTxdCount;
    unsigned int  maximumIplInstances;
    unsigned int  txdPoolCapacity;
    unsigned int  stockColOccupied;
    unsigned int  colPoolCapacity;
    unsigned int  stockIplOccupied;
    unsigned int  iplPoolCapacity;
    unsigned char expectedArchiveId;

    SNativeModelStoreUsageSA       stockModelStores;
    SNativeModelStoreUsageSA       modelStoreCapacities;
    SNativeWorldPayloadBudgetSA    payloadBudget;
    const SNativeTxdPoolProfileSA* txdPoolProfiles;
    unsigned int                   txdPoolProfileCount;
};

// Holds the minimal payload identity loaded from JSON plus inventory derived
// from IDE/IMG bytes. Derived values never come from manifest claims.
struct SNativeWorldPackRuntimeDataSA
{
    unsigned int format{};
    std::string  policyKey;
    std::string  packId;
    std::string  manifestSha256;
    unsigned int manifestBytes{};
    std::string  ideFileName;
    std::string  imgFileName;
    std::string  colFileName;
    std::string  ideSha256;
    std::string  imgSha256;
    unsigned int ideBytes{};
    unsigned int imgBytes{};

    unsigned int             modelFirst{};
    unsigned int             modelLast{};
    unsigned int             modelCount{};
    unsigned int             txdCount{};
    std::vector<std::string> iplNames;
    unsigned int             imgEntryCount{};
    unsigned int             imgSectorCount{};
    unsigned int             largestImgEntryBlocks{};
    SNativeModelStoreUsageSA addedModelStores{};
};

// Internal merged view. It exists only after the untrusted manifest has been
// parsed and checked against the compiled policy, which keeps the registrar's
// native-commit code independent of the manifest representation.
struct SNativeWorldPackDescriptorSA
{
    const char* key;
    const char* displayName;
    const char* logPrefix;
    const char* featureEnvironment;
    const char* directoryPath;
    const char* ideFileName;
    const char* imgFileName;
    const char* colFileName;
    const char* ideSha256;
    const char* imgSha256;

    unsigned int       modelFirst;
    unsigned int       modelLast;
    unsigned int       modelCount;
    unsigned int       txdCount;
    unsigned int       txdPoolCapacity;
    unsigned int       stockColOccupied;
    unsigned int       colPoolCapacity;
    unsigned int       stockIplOccupied;
    unsigned int       iplPoolCapacity;
    const char* const* iplNames;
    unsigned int       iplCount;
    unsigned int       imgEntryCount;
    unsigned int       imgSectorCount;
    unsigned int       largestImgEntryBlocks;
    unsigned char      expectedArchiveId;

    SNativeModelStoreUsageSA       stockModelStores;
    SNativeModelStoreUsageSA       addedModelStores;
    const SNativeTxdPoolProfileSA* txdPoolProfiles;
    unsigned int                   txdPoolProfileCount;
};

class CNativeWorldPackManagerSA
{
public:
    // Completes selection, claims the one-shot ticket, retains its exact-cache
    // lease, and prepares model stores before GTA population. The pack hook is
    // still deferred until the second server session is verified.
    static void HandleStartupSelection(eGameVersion gameVersion, const SNativeWorldStartupSelection& selection);

    static void AttachAuthorizedStreaming(CStreamingSA* streaming);
    static bool VerifyAuthorizedStartupBeforeStartGame();
    static void CancelAuthorizedActivation();

    // Installs only the startup call hook. The pack itself is validated and
    // registered after GTA has loaded all stock CD directories.
    static void InstallFromEnvironment(CStreamingSA* streaming);

    // Returns zero unless the native pack completed registration. Once active,
    // the pack remains registered for the GTA process lifetime, including MTA
    // disconnect/reconnect cycles.
    static unsigned int GetRequiredStreamingBufferSizeBlocks();

    static SNativeWorldTransportPublishResult PublishTransportOffer(const SNativeWorldTransportOffer& offer);

    // Reports process-owned physical model slots while a prepared, committing,
    // or active generation owns them. A reversible refusal may release them
    // only after proving that no native mutation survived its rollback.
    static bool IsModelIdReserved(unsigned int modelId);

    // Runs on GTA's streaming thread immediately before a position-driven
    // update or blocking scene load. It owns the city working-set transition;
    // callers must not mutate model/FileID tables themselves.
    static void PrepareStreamingAtPosition(const CVector& position);

    // Enters the one-way runtime drain checkpoint on GTA's main thread. This
    // retires only the spatial working set and proves streaming quiescence;
    // the committed catalog and cache leases remain owned until teardown.
    static bool BeginRuntimeDrain();

    // Observes the drain fence without advancing lifecycle state or touching
    // GTA. Status probes must not turn into hidden teardown work.
    static bool IsRuntimeDrainQuiescent();

    // Destroys only the committed format-3 content generation after the
    // quiescence preflight. Process-wide relocated stores and buffers remain.
    static bool TeardownRuntimeContent();

    // Detached still owns the authorizing endpoint. Neutral is published only
    // after Core has destroyed Client Deathmatch and passes back the exact
    // immutable startup selection which owns the detached generation.
    static bool IsRuntimeContentDetached();
    static bool ReleaseDetachedRuntimeSession(const SNativeWorldStartupSelection& expectedSelection, std::string& error);

    // Samples the process-wide substrate and generation-owned native state.
    // This is intentionally read-only: later hot-unload checkpoints will use
    // the same contract as their commit fence instead of inventing new checks.
    static void LogLifecycleTelemetry(const char* context);

    // Keeps lifecycle-sensitive diagnostics in one place and prefixes them
    // with the active descriptor's stable log tag.
    static void LogStreamingBufferClamp(unsigned int requestedBlocks, unsigned int effectiveBlocks, unsigned int requiredBlocks);
};
