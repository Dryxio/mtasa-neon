/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto: Neon
 *  LICENSE:     See LICENSE in the top level directory
 *  PURPOSE:     Native server-configured HD vehicle audio and backfire manager
 *
 *****************************************************************************/

#pragma once

#include <memory>
class CClientManager;
class CClientVehicle;
class CResource;
class SString;

class CVehicleSoundManager
{
public:
    explicit CVehicleSoundManager(CClientManager* clientManager);
    ~CVehicleSoundManager();

    void DoPulse();
    bool IsReplacingAudio(const CClientVehicle* vehicle) const;

    bool LoadServerConfig(CResource* owner, const SString& configPath);
    bool ReloadServerConfig(CResource* owner);
    bool UnloadServerConfig(CResource* owner);
    bool PlayBackfire(CResource* owner, CClientVehicle* vehicle, unsigned int mode);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
