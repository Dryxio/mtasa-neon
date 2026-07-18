/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        core/CNativeWorldAuthorizationStore.h
 *  PURPOSE:     DPAPI-backed native-world authorization store
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <core/CNativeWorldAuthorization.h>

namespace NativeWorldAuthorizationStore
{
    struct SRestartTarget
    {
        std::array<unsigned char, 4> serverIpv4{};
        unsigned short               serverPort{};
    };

    SNativeWorldAuthorizationRecordResult Persist(const SNativeWorldStartupAuthorization&     authorization,
                                                  const SNativeWorldAuthorizationPublication& publication);
    SNativeWorldAuthorizationRecordResult Inspect();
    SNativeWorldAuthorizationRecordResult InspectFreshRestartTarget(SRestartTarget& target);
    SNativeWorldAuthorizationRecordResult Clear();
    SNativeWorldAuthorizationRecordResult Revoke(const SNativeWorldStartupAuthorization& authorization, const std::string& contentId);
    SNativeWorldStartupSelection BeginStartup(const std::array<unsigned char, 4>* endpointIpv4, unsigned short endpointPort, bool legacySelectorEnabled);
    SNativeWorldAuthorizationRecordResult FinishStartup(const std::string& ticketId, bool claim, const std::string& refusalReason);
    void                                  CancelStartup(const std::string& ticketId);
    bool                                  IsStartupCancelled(const std::string& ticketId);
    void                                  CancelActiveStartup();
}
