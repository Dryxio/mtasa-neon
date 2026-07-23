/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/CResourceManager.cpp
 *  PURPOSE:     Resource manager
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CChecksum.h"

using std::list;

CResourceManager::CResourceManager()
{
}

CResourceManager::~CResourceManager()
{
    CChecksum::ClearChecksumCache();

    while (!m_resources.empty())
    {
        CResource* pResource = m_resources.back();
        Remove(pResource);

        // Force delete all objects in cache (see https://github.com/multitheftauto/mtasa-blue/issues/1840).
        g_pClientGame->GetElementDeleter()->DoDeleteAll();
    }
    PulseNativeWorldAuthorizationRevocations(true);
    if (!m_retiredNativeWorldAuthorizationRevocations.empty())
    {
        const SString message(
            "[NativeWorldAuthorization] state=revocation-abandoned pending=%u activation=no lease=no restart-required=no manual-clear-required=yes",
            static_cast<unsigned int>(m_retiredNativeWorldAuthorizationRevocations.size()));
        AddReportLog(7474, message);
        WriteDebugEvent(message);
    }
}

CResource* CResourceManager::Add(unsigned short usNetID, const char* szResourceName, CClientEntity* pResourceEntity, CClientEntity* pResourceDynamicEntity,
                                 const CMtaVersion& strMinServerReq, const CMtaVersion& strMinClientReq, bool bEnableOOP)
{
    CResource* pResource = new CResource(usNetID, szResourceName, pResourceEntity, pResourceDynamicEntity, strMinServerReq, strMinClientReq, bEnableOOP);
    if (pResource)
    {
        m_resources.push_back(pResource);
        assert(!MapContains(m_NetIdResourceMap, pResource->GetNetID()));
        MapSet(m_NetIdResourceMap, usNetID, pResource);
        return pResource;
    }
    return NULL;
}

CResource* CResourceManager::GetResourceFromNetID(unsigned short usNetID)
{
    CResource* pResource = MapFindRef(m_NetIdResourceMap, usNetID);
    if (pResource)
    {
        assert(pResource->GetNetID() == usNetID);
        return pResource;
    }

    list<CResource*>::const_iterator iter = m_resources.begin();
    for (; iter != m_resources.end(); ++iter)
    {
        if ((*iter)->GetNetID() == usNetID)
        {
            assert(0);  // Should be in map
            return (*iter);
        }
    }
    return NULL;
}

CResource* CResourceManager::GetResourceFromScriptID(uint uiScriptID)
{
    CResource* pResource = (CResource*)CIdArray::FindEntry(uiScriptID, EIdClass::RESOURCE);
    dassert(!pResource || ListContains(m_resources, pResource));
    return pResource;
}

CResource* CResourceManager::GetResourceFromLuaState(lua_State* luaVM)
{
    CLuaMain*  pLuaMain = g_pClientGame->GetLuaManager()->GetVirtualMachine(luaVM);
    CResource* pResource = pLuaMain ? pLuaMain->GetResource() : NULL;
    return pResource;
}

SString CResourceManager::GetResourceName(lua_State* luaVM)
{
    CResource* pResource = GetResourceFromLuaState(luaVM);
    if (pResource)
        return pResource->GetName();
    return "";
}

CResource* CResourceManager::GetResource(const char* szResourceName)
{
    list<CResource*>::const_iterator iter = m_resources.begin();
    for (; iter != m_resources.end(); ++iter)
    {
        if (stricmp((*iter)->GetName(), szResourceName) == 0)
            return (*iter);
    }
    return NULL;
}

void CResourceManager::OnDownloadGroupFinished()
{
    CDownloadableResource::EndChecksumBatch();

    // Try to load newly ready resources
    for (std::list<CResource*>::const_iterator iter = m_resources.begin(); iter != m_resources.end(); ++iter)
    {
        CResource* pResource = *iter;
        if (!pResource->IsActive())
        {
            if (!pResource->CanBeLoaded())
            {
                // Stop as soon as we hit a resource which hasn't downloaded yet (as per previous behaviour)
                if (pResource->IsWaitingForInitialDownloads())
                    break;

                continue;
            }

            if (pResource->IsWaitingForInitialDownloads())
                break;

            pResource->Load();
        }
    }
}

void CResourceManager::PulseNativeWorldTransportPublications()
{
    PulseNativeWorldAuthorizationRevocations(false);

    for (auto iter = m_retiredNativeWorldPublications.begin(); iter != m_retiredNativeWorldPublications.end();)
    {
        if (iter->wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            ++iter;
            continue;
        }
        try
        {
            iter->get();
        }
        catch (...)
        {
            // A retired job has no resource left to consume its result. The
            // worker already owns the refusal/cleanup boundary.
        }
        iter = m_retiredNativeWorldPublications.erase(iter);
    }

    for (CResource* resource : m_resources)
    {
        if (!resource->IsActive() && resource->IsNativeWorldTransportPublicationPending() && resource->CanBeLoaded())
            resource->Load();
    }
}

void CResourceManager::RetireNativeWorldTransportPublication(std::future<SNativeWorldTransportPublishResult>&& publication)
{
    if (publication.valid())
        m_retiredNativeWorldPublications.emplace_back(std::move(publication));
}

void CResourceManager::RetireNativeWorldAuthorizationRevocation(const SNativeWorldStartupAuthorization& authorization, const std::string& contentId,
                                                                const SString& resourceName)
{
    const auto duplicate = std::find_if(m_retiredNativeWorldAuthorizationRevocations.begin(), m_retiredNativeWorldAuthorizationRevocations.end(),
                                        [&authorization, &contentId](const SRetiredNativeWorldAuthorizationRevocation& retired)
                                        {
                                            return retired.contentId == contentId && retired.authorization.serverIdDigest == authorization.serverIdDigest &&
                                                   retired.authorization.resourceNetId == authorization.resourceNetId &&
                                                   retired.authorization.resourceStartCounter == authorization.resourceStartCounter &&
                                                   retired.authorization.connectionGeneration == authorization.connectionGeneration;
                                        });
    if (duplicate != m_retiredNativeWorldAuthorizationRevocations.end())
        return;
    m_retiredNativeWorldAuthorizationRevocations.push_back({authorization, contentId, resourceName});
}

void CResourceManager::PulseNativeWorldAuthorizationRevocations(bool force)
{
    const auto now = std::chrono::steady_clock::now();
    for (auto iter = m_retiredNativeWorldAuthorizationRevocations.begin(); iter != m_retiredNativeWorldAuthorizationRevocations.end();)
    {
        if (!force && now < iter->nextAttempt)
        {
            ++iter;
            continue;
        }
        const SNativeWorldAuthorizationRecordResult result = g_pCore->RevokeDetachedNativeWorldStartupAuthorization(iter->authorization, iter->contentId);
        if (!result.success)
        {
            iter->nextAttempt = now + std::chrono::seconds(1);
            ++iter;
            continue;
        }
        const SString message("[NativeWorldAuthorization] state=revoked-detached resource=%s ticket=%s activation=no lease=no restart-required=no",
                              iter->resourceName.c_str(), result.ticketId.substr(0, 8).c_str());
        AddReportLog(7475, message);
        WriteDebugEvent(message);
        g_pCore->GetConsole()->Printf("%s", *message);
        iter = m_retiredNativeWorldAuthorizationRevocations.erase(iter);
    }
}

bool CResourceManager::RemoveResource(unsigned short usNetID)
{
    CResource* pResource = GetResourceFromNetID(usNetID);
    if (pResource)
    {
        Remove(pResource);
        return true;
    }
    return false;
}

void CResourceManager::Remove(CResource* pResource)
{
    // Triggger the onStop event, and set resource state to 'stopping'
    pResource->Stop();

    // Delete the resource
    m_resources.remove(pResource);
    assert(MapContains(m_NetIdResourceMap, pResource->GetNetID()));
    MapRemove(m_NetIdResourceMap, pResource->GetNetID());
    delete pResource;
}

bool CResourceManager::Exists(CResource* pResource)
{
    return m_resources.Contains(pResource);
}

void CResourceManager::StopAll()
{
    while (m_resources.size() > 0)
    {
        Remove(m_resources.front());
    }
}

// pResource may be changed on return, and it could be NULL if the function returns false.
bool CResourceManager::ParseResourcePathInput(std::string strInput, CResource*& pResource, std::string* pStrPath, std::string* pStrMetaPath, bool bPassSize)
{
    ReplaceOccurrencesInString(strInput, "\\", "/");

    // Disallow file paths with a directory separator at the end
    if (strInput.back() == '/')
        return false;

    eAccessType accessType = ACCESS_PUBLIC;
    std::string strMetaPath;

    if (strInput[0] == '@')
    {
        accessType = ACCESS_PRIVATE;
        strInput = strInput.substr(1);
    }

    if (strInput[0] == ':')
    {
        unsigned int iEnd = strInput.find_first_of("/");
        if (iEnd)
        {
            std::string strResourceName = strInput.substr(1, iEnd - 1);
            pResource = g_pClientGame->GetResourceManager()->GetResource(strResourceName.c_str());
            if (pResource && strInput[iEnd + 1])
            {
                strMetaPath = strInput.substr(iEnd + 1);

                if (pStrMetaPath)
                    *pStrMetaPath = strMetaPath;

                if (IsValidFilePath(strMetaPath.c_str()))
                {
                    if (pStrPath)
                        *pStrPath = pResource->GetResourceDirectoryPath(accessType, strMetaPath);
                    return true;
                }
            }
        }
    }
    else if (pResource && (bPassSize ? IsValidFilePath(strInput.c_str(), strInput.size()) : IsValidFilePath(strInput.c_str())))
    {
        if (pStrPath)
            *pStrPath = pResource->GetResourceDirectoryPath(accessType, strInput);
        if (pStrMetaPath)
            *pStrMetaPath = strInput;
        return true;
    }
    return false;
}

// When a resource file is created
void CResourceManager::OnAddResourceFile(CDownloadableResource* pResourceFile)
{
    SString strFilename = PathConform(pResourceFile->GetName()).ToLower();
    dassert(!MapContains(m_ResourceFileMap, strFilename));
    MapSet(m_ResourceFileMap, strFilename, pResourceFile);
}

// When a resource file is delected
void CResourceManager::OnRemoveResourceFile(CDownloadableResource* pResourceFile)
{
    SString strFilename = PathConform(pResourceFile->GetName()).ToLower();
    dassert(MapFindRef(m_ResourceFileMap, strFilename) == pResourceFile);
    MapRemove(m_ResourceFileMap, strFilename);
}

// Update downloaded flag for this file
void CResourceManager::OnDownloadedResourceFile(const SString& strInFilename)
{
    SString                strFilename = PathConform(strInFilename).ToLower();
    CDownloadableResource* pResourceFile = MapFindRef(m_ResourceFileMap, strFilename);
    if (pResourceFile)
        pResourceFile->SetDownloaded();
}

// Check given file name is a resource file
bool CResourceManager::IsResourceFile(const SString& strInFilename)
{
    SString strFilename = PathConform(strInFilename).ToLower();
    return MapContains(m_ResourceFileMap, strFilename);
}

// Remove this file from the checks as it has been changed by script actions
void CResourceManager::OnFileModifedByScript(const SString& strInFilename, const SString& strReason)
{
    SString                strFilename = PathConform(strInFilename).ToLower();
    CDownloadableResource* pResourceFile = MapFindRef(m_ResourceFileMap, strFilename);
    if (pResourceFile && !pResourceFile->IsModifedByScript())
    {
        pResourceFile->SetModifedByScript(true);
    }
}

// Check resource file data matches server checksum
void CResourceManager::ValidateResourceFile(const SString& strInFilename, const char* buffer, size_t bufferSize)
{
    SString                strFilename = PathConform(strInFilename).ToLower();
    CDownloadableResource* pResourceFile = MapFindRef(m_ResourceFileMap, strFilename);
    if (pResourceFile && !pResourceFile->IsModifedByScript())
    {
        if (!pResourceFile->IsAutoDownload() && !pResourceFile->IsDownloaded())
        {
            // Scripting error
            g_pClientGame->GetScriptDebugging()->LogWarning(NULL, "Attempt to load '%s' before onClientFileDownloadComplete event",
                                                            *ConformResourcePath(strInFilename));
        }
        else
        {
            CChecksum checksum;
            if (buffer)
                checksum = CChecksum::GenerateChecksumFromBuffer(buffer, bufferSize);
            else
                checksum = CChecksum::GenerateChecksumFromFileUnsafe(strInFilename);
            if (checksum != pResourceFile->GetServerChecksum())
            {
                char szMd5[33];
                CMD5Hasher::ConvertToHex(checksum.md5, szMd5);
                char szMd5Wanted[33];
                CMD5Hasher::ConvertToHex(pResourceFile->GetServerChecksum().md5, szMd5Wanted);
                const int iGotSize = buffer ? static_cast<int>(bufferSize) : static_cast<int>(FileSize(strInFilename));
                SString   strMessage("%s [Expected Size:%d CRC:%08lX MD5:%s][Got Size:%d CRC:%08lX MD5:%s] ", *ConformResourcePath(strInFilename),
                                     pResourceFile->GetDownloadSize(), pResourceFile->GetServerChecksum().ulCRC, szMd5Wanted, iGotSize, checksum.ulCRC, szMd5);
                if (pResourceFile->IsDownloaded())
                {
                    strMessage = "Resource file unexpected change: " + strMessage;
                    g_pClientGame->TellServerSomethingImportant(1007, strMessage);
                    g_pClientGame->GetScriptDebugging()->LogWarning(NULL, strMessage);
                }
                else if (pResourceFile->IsAutoDownload())
                {
                    strMessage = "Attempt to load resource file before it is ready: " + strMessage;
                    g_pClientGame->TellServerSomethingImportant(1008, strMessage);
                }
            }
        }
    }
}
