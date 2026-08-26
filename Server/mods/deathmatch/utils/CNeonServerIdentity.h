/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto: San Andreas
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/utils/CNeonServerIdentity.h
 *  PURPOSE:     Persistent cryptographic identity for automatic Neon onboarding
 *
 *****************************************************************************/

#pragma once

#include <string>

class CNeonServerIdentity
{
public:
    bool Initialize(const SString& path, std::string& error);
    bool SignHeartbeat(const std::string& body, std::string& timestamp, std::string& nonce, std::string& signature, std::string& error) const;

    const std::string& GetServerId() const noexcept { return m_serverId; }
    const std::string& GetPublicKey() const noexcept { return m_publicKey; }
    const SString&     GetPath() const noexcept { return m_path; }

private:
    bool Load(const SString& path, std::string& error);
    bool Create(const SString& path, std::string& error);
    bool InitializeFromSeed(const std::string& seed, std::string& error);

    SString     m_path;
    std::string m_seed;
    std::string m_publicKey;
    std::string m_serverId;
};
