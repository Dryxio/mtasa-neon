/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/CSingularFileDownloadManager.cpp
 *  PURPOSE:     Singular file download manager interface
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include <StdInc.h>

CSingularFileDownload::CSingularFileDownload(CResource* pResource, const char* szName, const char* szNameShort, SString strHTTPURL, CResource* pRequestResource,
                                             CChecksum serverChecksum)
{
    // Store the name
    m_strName = szName;

    // Store the name (short)
    m_strNameShort = szNameShort;

    // store resources
    m_pResource = pResource;
    m_pRequestResource = pRequestResource;

    // Store the server checksum
    m_ServerChecksum = serverChecksum;

    m_bBeingDeleted = false;

    GenerateClientChecksum();

    if (!DoesClientAndServerChecksumMatch())
    {
        SHttpRequestOptions options;
        options.bCheckContents = true;
        CNetHTTPDownloadManagerInterface* pHTTP = g_pCore->GetNetwork()->GetHTTPDownloadManager(EDownloadMode::RESOURCE_SINGULAR_FILES);
        // The HTTP manager replaces this path. Invalidate before handing over
        // the write so an old size/mtime identity cannot survive the transfer.
        CChecksum::InvalidateChecksum(m_strName);
        pHTTP->QueueFile(strHTTPURL.c_str(), szName, this, DownloadFinishedCallBack, options);
        m_bComplete = false;
        g_pClientGame->SetTransferringSingularFiles(true);
    }
    else
    {
        CallFinished(true);
    }
}

CSingularFileDownload::~CSingularFileDownload()
{
}

void CSingularFileDownload::DownloadFinishedCallBack(const SHttpDownloadResult& result)
{
    CSingularFileDownload* pFile = (CSingularFileDownload*)result.pObj;
    bool                   bVerifiedSuccess = false;
    if (result.bSuccess)
    {
        // Successful transport is not sufficient for integrity. Bypass metadata
        // caching and only expose the file after hashing the bytes written by HTTP.
        pFile->m_LastClientChecksum = CChecksum::GenerateChecksumFromFileUnsafe(pFile->m_strName, CChecksum::CachePolicy::BypassAndRefresh);
        bVerifiedSuccess = pFile->DoesClientAndServerChecksumMatch();
    }
    else
    {
        // Failed requests can leave partial files; retire anything cached while
        // the transfer was active before reporting completion.
        CChecksum::InvalidateChecksum(pFile->m_strName);
    }

    pFile->CallFinished(bVerifiedSuccess);
}

void CSingularFileDownload::CallFinished(bool bSuccess)
{
    // Only a cached pre-existing match or a forced post-download match is
    // loadable. Transport success by itself must not cross this boundary.
    if (bSuccess)
        g_pClientGame->GetResourceManager()->OnDownloadedResourceFile(GetName());

    if (!m_bBeingDeleted && m_pResource)
    {
        // Call the onClientbFileDownloadComplete event
        CLuaArguments Arguments;
        Arguments.PushString(GetShortName());  // file name
        Arguments.PushBoolean(bSuccess);       // Completed successfully?
        if (m_pRequestResource)
        {
            Arguments.PushResource(m_pRequestResource);  // Resource that called downloadFile
        }
        else
        {
            Arguments.PushBoolean(false);  // or false
        }

        m_pResource->GetResourceEntity()->CallEvent("onClientFileDownloadComplete", Arguments, false);
    }
    SetComplete();
}

void CSingularFileDownload::Cancel()
{
    m_bBeingDeleted = true;
    m_pResource = NULL;
    m_pRequestResource = NULL;

    // TODO: Cancel also in Net
}

bool CSingularFileDownload::DoesClientAndServerChecksumMatch()
{
    return (m_LastClientChecksum == m_ServerChecksum);
}

CChecksum CSingularFileDownload::GenerateClientChecksum()
{
    m_LastClientChecksum = CChecksum::GenerateChecksumFromFileUnsafe(m_strName);
    return m_LastClientChecksum;
}
