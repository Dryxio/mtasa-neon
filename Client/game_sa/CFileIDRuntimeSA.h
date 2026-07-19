/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CFileIDRuntimeSA.h
 *  PURPOSE:     Validated runtime view of GTA SA's FileID namespace
 *
 *****************************************************************************/

#pragma once

#include <game/CGame.h>
#include <game/CStreaming.h>

#include <cstdint>
#include <string>

class CFileIDRuntimeSA
{
public:
    bool CaptureStockLayout(eGameVersion gameVersion, std::string& error);
    bool InstallStockRelocation(std::string& error);

    const SFileIDLayout& GetLayout() const { return m_layout; }
    void*                GetModelInfoArray() const { return m_modelInfoArray; }
    CStreamingInfo*      GetStreamingInfoArray() const { return m_streamingInfoArray; }

private:
    SFileIDLayout   m_layout{};
    void*           m_modelInfoArray{};
    CStreamingInfo* m_streamingInfoArray{};
    std::uint32_t   m_imageSize{};
    bool            m_relocationPrepared{};
    bool            m_relocationInstalled{};
    bool            m_installStarted{};
};
