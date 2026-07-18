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

class CNativeModelStoreSA
{
public:
    // Performs the complete executable/patch/store audit used by startup
    // selection without allocating memory or changing any process state.
    static bool ValidateExecutableAndPatchManifestReadOnly(eGameVersion gameVersion, std::string& error);

    // Repeats the executable audit and installs the foundation after an
    // authorization ticket has been durably claimed. Unlike the developer
    // route below, this entry point has no environment selector.
    static bool InstallForAuthorizedStartup(eGameVersion gameVersion, std::string& error);

    // This must run before GTA calls CModelInfo::Initialise. The process-start
    // environment is intentionally the only switch so a resource cannot turn
    // executable patching on after the stores are already in use.
    static void InstallFromEnvironment(eGameVersion gameVersion);

    static bool        IsInstalled();
    static const char* GetExecutableIdentityName();
    static bool        GetUsage(unsigned int& atomic, unsigned int& damageAtomic, unsigned int& time);

    // Emits read-only occupancy/high-water diagnostics when the opt-in patch is
    // active. It has no command surface and cannot mutate the relocated stores.
    static void LogDiagnostics(const char* context);
};
