/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CNativeModelStoreSA.h
 *  PURPOSE:     Opt-in native model-store foundation for extended worlds
 *
 *****************************************************************************/

#pragma once

#include <game/CGame.h>
#include <string>
#include <vector>

class CBaseModelInfoSAInterface;

enum class ENativeModelStoreKindSA : unsigned char
{
    Atomic,
    DamageAtomic,
    Time,
};

struct SNativeModelStoreCountsSA
{
    unsigned int atomic{};
    unsigned int damageAtomic{};
    unsigned int time{};
};

struct SNativeModelStoreOwnedEntrySA
{
    ENativeModelStoreKindSA    kind{};
    unsigned int               index{};
    CBaseModelInfoSAInterface* model{};
};

struct SNativeModelStoreTailSnapshotSA
{
    SNativeModelStoreCountsSA                  before{};
    SNativeModelStoreCountsSA                  after{};
    std::vector<SNativeModelStoreOwnedEntrySA> entries;
};

class CNativeModelStoreSA
{
public:
    // Installs the content-neutral store and collision-buffer substrate before
    // GTA populates its inline model stores. This is deliberately independent
    // of any server, cache object, or selected set so a later connected
    // admission never has to patch populated retail stores.
    static bool InstallProcessFoundation(eGameVersion gameVersion, std::string& error);

    // Performs the complete executable/patch/store audit used by startup
    // selection without allocating memory or changing any process state.
    static bool ValidateExecutableAndPatchManifestReadOnly(eGameVersion gameVersion, std::string& error);

    // Repeats the executable audit and installs the foundation after an
    // authorization ticket has been durably claimed. A later same-process
    // admission revalidates the exact installed foundation without allocating
    // or rewriting it. Unlike the developer route below, this entry point has
    // no environment selector.
    static bool InstallForAuthorizedStartup(eGameVersion gameVersion, std::string& error);

    // This must run before GTA calls CModelInfo::Initialise. The process-start
    // environment is intentionally the only switch so a resource cannot turn
    // executable patching on after the stores are already in use.
    static void InstallFromEnvironment(eGameVersion gameVersion);

    static bool        IsInstalled();
    static const char* GetExecutableIdentityName();
    static void        GetCapacities(unsigned int& atomic, unsigned int& damageAtomic, unsigned int& time);
    static bool        GetUsage(unsigned int& atomic, unsigned int& damageAtomic, unsigned int& time);

    // Captures the exact appended tails of the three relocated inline stores.
    // Entries are grouped by kind and ordered by increasing store index; they
    // describe ownership only and do not change GTA state.
    static bool CaptureOwnedTail(const SNativeModelStoreCountsSA& before, SNativeModelStoreTailSnapshotSA& snapshot, std::string& error);

    // Revalidates a previously captured ownership slice against the private
    // relocated store bases and current counts. This deliberately performs no
    // Shutdown, destructor, count rewind, or other engine mutation.
    static bool ValidateOwnedTail(const SNativeModelStoreTailSnapshotSA& snapshot, std::string& error);

    // Shuts down the exact generation-owned inline-store tail in reverse
    // allocation order and rewinds all three store counts. Returning false
    // means validation failed before the first destructive call. Once the
    // first virtual Shutdown executes, any failed postcondition terminates the
    // process because the compact ownership snapshot cannot reconstruct the
    // released RenderWare or collision state.
    static bool ShutdownAndRewindOwnedTail(const SNativeModelStoreTailSnapshotSA& snapshot, std::string& error);

    // Emits read-only occupancy/high-water diagnostics when the opt-in patch is
    // active. It has no command surface and cannot mutate the relocated stores.
    static void LogDiagnostics(const char* context);
};
