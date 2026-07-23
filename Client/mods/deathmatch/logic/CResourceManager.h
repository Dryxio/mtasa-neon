/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/CResourceManager.h
 *  PURPOSE:     Header for resource manager class
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

class CResourceManager;

#pragma once

#include <game/CGame.h>
#include <chrono>
#include <future>
#include <list>
#include <vector>

class CClientEntity;
class CResource;
class CDownloadableResource;

enum eAccessType
{
    ACCESS_PUBLIC,
    ACCESS_PRIVATE,
};

class CResourceManager
{
public:
    CResourceManager();
    ~CResourceManager();

    CResource* Add(unsigned short usNetID, const char* szResourceName, CClientEntity* pResourceEntity, CClientEntity* pResourceDynamicEntity,
                   const CMtaVersion& strMinServerReq, const CMtaVersion& strMinClientReq, bool bEnableOOP);
    CResource* GetResource(const char* szResourceName);
    CResource* GetResourceFromNetID(unsigned short usNetID);
    CResource* GetResourceFromScriptID(uint uiScriptID);
    CResource* GetResourceFromLuaState(struct lua_State* luaVM);
    SString    GetResourceName(struct lua_State* luaVM);

    std::list<CResource*>::const_iterator IterBegin() { return m_resources.begin(); };
    std::list<CResource*>::const_iterator IterEnd() { return m_resources.end(); };

    bool RemoveResource(unsigned short usID);
    void Remove(CResource* pResource);
    bool Exists(CResource* pResource);
    void StopAll();

    void OnDownloadGroupFinished();
    void PulseNativeWorldTransportPublications();
    void RetireNativeWorldTransportPublication(std::future<SNativeWorldTransportPublishResult>&& publication);
    void RetireNativeWorldAuthorizationRevocation(const SNativeWorldStartupAuthorization& authorization, const std::string& contentId,
                                                  const SString& resourceName);

    void                   OnAddResourceFile(CDownloadableResource* pResourceFile);
    void                   OnRemoveResourceFile(CDownloadableResource* pResourceFile);
    void                   OnDownloadedResourceFile(const SString& strFilename);
    bool                   IsResourceFile(const SString& strFilename);
    void                   OnFileModifedByScript(const SString& strFilename, const SString& strReason);
    void                   ValidateResourceFile(const SString& strFilename, const char* buffer, size_t bufferSize);
    CDownloadableResource* GetDownloadableResourceFile(const SString& strFilename) { return MapFindRef(m_ResourceFileMap, strFilename); }

    static bool ParseResourcePathInput(std::string strInput, CResource*& pResource, std::string* pStrPath, std::string* pStrMetaPath = nullptr,
                                       bool bPassSize = false);

private:
    struct SRetiredNativeWorldAuthorizationRevocation
    {
        SNativeWorldStartupAuthorization      authorization;
        std::string                           contentId;
        SString                               resourceName;
        std::chrono::steady_clock::time_point nextAttempt{};
    };

    void PulseNativeWorldAuthorizationRevocations(bool force);

    CMappedList<CResource*>                                      m_resources;
    std::map<ushort, CResource*>                                 m_NetIdResourceMap;
    std::map<SString, CDownloadableResource*>                    m_ResourceFileMap;
    std::vector<std::future<SNativeWorldTransportPublishResult>> m_retiredNativeWorldPublications;
    std::vector<SRetiredNativeWorldAuthorizationRevocation>      m_retiredNativeWorldAuthorizationRevocations;
};
