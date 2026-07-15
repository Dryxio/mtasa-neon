/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CNativeBullworthSA.h
 *  PURPOSE:     Opt-in registration of the native Bullworth streaming pack
 *
 *****************************************************************************/

#pragma once

class CStreamingSA;

class CNativeBullworthSA
{
public:
    // Installs only the startup call hook. The pack itself is validated and
    // registered after GTA has loaded all stock CD directories.
    static void InstallFromEnvironment(CStreamingSA* streaming);

    // Returns zero unless the native pack completed registration. Once active,
    // the pack remains registered for the GTA process lifetime, including MTA
    // disconnect/reconnect cycles.
    static unsigned int GetRequiredStreamingBufferSizeBlocks();
};
