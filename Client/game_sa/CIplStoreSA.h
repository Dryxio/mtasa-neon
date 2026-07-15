/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CIplStore.h
 *  PURPOSE:     IPL store class
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include "CIplSA.h"
#include "CPoolsSA.h"
#include <game/CIplStore.h>
#include <functional>

class CIplStoreSA : public CIplStore
{
public:
    CIplStoreSA();
    ~CIplStoreSA() = default;

    void SetDynamicIplStreamingEnabled(bool state);
    void SetDynamicIplStreamingEnabled(bool state, std::function<bool(CIplSAInterface* ipl)> filter);
    void ClampBuildingRanges(std::size_t buildingPoolSize);

private:
    void UnloadAndDisableStreaming(int iplId);
    void EnableStreaming(int iplId);
    void ClampBuildingRange(CIplSAInterface& ipl, std::size_t buildingPoolSize);

private:
    CPoolSAInterface<CIplSAInterface>** m_ppIplPoolInterface;

    bool m_isStreamingEnabled;
};
