/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/CResource.cpp
 *  PURPOSE:     Resource object
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#define DECLARE_PROFILER_SECTION_CResource
#include "profiler/SharedUtil.Profiler.h"
#include "CServerIdManager.h"
#include "luadefs/CLuaAudioDefs.h"
#include "luadefs/CLuaPlayerDefs.h"
#include "luadefs/CLuaVehicleDefs.h"

#include <cstdint>
#include <limits>

using namespace std;

extern CClientGame* g_pClientGame;

int CResource::m_iShowingCursor = 0;
int CResource::m_iToggleControls = 0;

CResource::CResource(unsigned short usNetID, const char* szResourceName, CClientEntity* pResourceEntity, CClientEntity* pResourceDynamicEntity,
                     const CMtaVersion& strMinServerReq, const CMtaVersion& strMinClientReq, bool bEnableOOP)
{
    m_uiScriptID = CIdArray::PopUniqueId(this, EIdClass::RESOURCE);
    m_usNetID = usNetID;
    m_bActive = false;
    m_bStarting = true;
    m_bStopping = false;
    m_bShowingCursor = false;
    m_bToggleControls = false;
    m_usRemainingNoClientCacheScripts = 0;
    m_bLoadAfterReceivingNoClientCacheScripts = false;
    m_strMinServerReq = strMinServerReq;
    m_strMinClientReq = strMinClientReq;

    if (szResourceName)
        m_strResourceName.AssignLeft(szResourceName, MAX_RESOURCE_NAME_LENGTH);

    m_pLuaManager = g_pClientGame->GetLuaManager();
    m_pRootEntity = g_pClientGame->GetRootEntity();
    m_pDefaultElementGroup = new CElementGroup();  // for use by scripts
    m_pResourceEntity = pResourceEntity;
    m_pResourceDynamicEntity = pResourceDynamicEntity;

    // Create our GUI root element. We set its parent when we're loaded.
    // Make it a system entity so nothing but us can delete it.
    m_pResourceGUIEntity = new CClientDummy(g_pClientGame->GetManager(), INVALID_ELEMENT_ID, "guiroot");
    m_pResourceGUIEntity->MakeSystemEntity();

    // Create our COL root element. We set its parent when we're loaded.
    // Make it a system entity so nothing but us can delete it.
    m_pResourceCOLRoot = new CClientDummy(g_pClientGame->GetManager(), INVALID_ELEMENT_ID, "colmodelroot");
    m_pResourceCOLRoot->MakeSystemEntity();

    // Create our DFF root element. We set its parent when we're loaded.
    // Make it a system entity so nothing but us can delete it.
    m_pResourceDFFEntity = new CClientDummy(g_pClientGame->GetManager(), INVALID_ELEMENT_ID, "dffroot");
    m_pResourceDFFEntity->MakeSystemEntity();

    // Create our TXD root element. We set its parent when we're loaded.
    // Make it a system entity so nothing but us can delete it.
    m_pResourceTXDRoot = new CClientDummy(g_pClientGame->GetManager(), INVALID_ELEMENT_ID, "txdroot");
    m_pResourceTXDRoot->MakeSystemEntity();

    // Create our IFP root element. We set its parent when we're loaded.
    // Make it a system entity so nothing but us can delete it.
    m_pResourceIFPRoot = new CClientDummy(g_pClientGame->GetManager(), INVALID_ELEMENT_ID, "ifproot");
    m_pResourceIFPRoot->MakeSystemEntity();

    // Create our IMG root element. We set its parent when we're loaded.
    // Make it a system entity so nothing but us can delete it.
    m_pResourceIMGRoot = new CClientDummy(g_pClientGame->GetManager(), INVALID_ELEMENT_ID, "imgroot");
    m_pResourceIMGRoot->MakeSystemEntity();

    m_strResourceDirectoryPath = SString("%s/resources/%s", g_pClientGame->GetFileCacheRoot(), *m_strResourceName);
    m_strResourcePrivateDirectoryPath = PathJoin(CServerIdManager::GetSingleton()->GetConnectionPrivateDirectory(), m_strResourceName);

    m_strResourcePrivateDirectoryPathOld = CServerIdManager::GetSingleton()->GetConnectionPrivateDirectory(true);
    if (!m_strResourcePrivateDirectoryPathOld.empty())
        m_strResourcePrivateDirectoryPathOld = PathJoin(m_strResourcePrivateDirectoryPathOld, m_strResourceName);

    // Move this after the CreateVirtualMachine line and heads will roll
    m_bOOPEnabled = bEnableOOP;
    m_iDownloadPriorityGroup = 0;

    m_pLuaVM = m_pLuaManager->CreateVirtualMachine(this, bEnableOOP);
    if (m_pLuaVM)
    {
        m_pLuaVM->SetScriptName(szResourceName);
        m_pLuaVM->LoadEmbeddedScripts();
    }
}

CResource::~CResource()
{
    // Resource stop invalidates this offer immediately. The manager retains
    // the future until its cooperative worker exits so std::future destruction
    // cannot block this lifecycle-sensitive destructor.
    if (m_nativeWorldTransport.cancellation)
        m_nativeWorldTransport.cancellation->store(true, std::memory_order_release);
    if (m_nativeWorldTransport.publication.valid())
        g_pClientGame->GetResourceManager()->RetireNativeWorldTransportPublication(std::move(m_nativeWorldTransport.publication));

    // Streaming leases can target server-owned elements which are not children
    // of this resource. Release them while both the resource identity and any
    // surviving target elements are still valid.
    ReleaseAllElementStreamingLeases();

    // Native event profiles can also target server-owned peds. Revoke their
    // narrow GTA AI exception before the resource identity disappears.
    ReleaseAllPedNativeEventProfiles();

    // Mission-audio handles lease GTA-global hardware slots rather than child
    // elements, so resource teardown must stop only this resource's sounds.
    CLuaAudioDefs::ReleaseMissionAudioForResource(this);

    // Mission GXT blocks and native HUD queues contain GTA-global pointers.
    // Clear this resource's prints/help before its Lua state disappears.
    CLuaPlayerDefs::ReleaseMissionTextForResource(this);

    // Native gang tags outlive streamed GTA objects and are not represented by
    // child elements alone. Revoke this resource's spray registrations before
    // its Lua state and ownership identity disappear.
    if (g_pClientGame && g_pClientGame->GetObjectManager())
        g_pClientGame->GetObjectManager()->ReleaseGangTagsForResource(this);

    // Recorded-car buffers and active slots are native global state, not child
    // elements. Stop and release them before this resource's Lua VM disappears.
    CLuaVehicleDefs::ReleaseVehicleRecordings(this);

    // Script cameras own GTA-global camera and input state rather than child
    // elements. Revoke the lease while the camera and resource identity still
    // exist so stop, restart, and disconnect all share the same restoration.
    if (g_pClientGame && g_pClientGame->GetManager() && g_pClientGame->GetManager()->GetCamera())
        g_pClientGame->GetManager()->GetCamera()->ReleaseScriptCamera(this);

    // Custom CULL zones are client-native state rather than elements, so restore
    // vanilla edits and remove this resource's additions explicitly.
    if (g_pGame && g_pGame->GetWorld())
        g_pGame->GetWorld()->RemoveCullZoneChangesByOwner(this);

    // Remove refrences from requested models
    m_modelStreamer.ReleaseAll();

    // Fully delete local entities before freeing their model infos. Merely queuing
    // destroyElement leaves native buildings alive until the next pulse, where
    // GTA can request a model that this destructor has already deallocated.
    DeleteClientChildren();
    g_pClientGame->GetElementDeleter()->DoDeleteAll();

    // IMG links remember the previous streaming entry for every dynamic slot.
    // Restore and close the archives while those slots still exist, then delete
    // the model infos. This also drains any reads that still use the IMG handle.
    if (m_pResourceIMGRoot)
    {
        g_pClientGame->GetElementDeleter()->DeleteRecursive(m_pResourceIMGRoot);
        g_pClientGame->GetElementDeleter()->DoDeleteAll();
        m_pResourceIMGRoot = nullptr;
    }

    // Deallocate all models that this resource allocated earlier
    g_pClientGame->GetManager()->GetModelManager()->DeallocateModelsAllocatedByResource(this);

    CIdArray::PushUniqueId(this, EIdClass::RESOURCE, m_uiScriptID);
    // Make sure we don't force the cursor on
    ShowCursor(false);

    // Do this before we delete our elements.
    m_pRootEntity->CleanUpForVM(m_pLuaVM, true);
    g_pClientGame->GetElementDeleter()->CleanUpForVM(m_pLuaVM);
    m_pLuaManager->RemoveVirtualMachine(m_pLuaVM);

    // Remove all keybinds on this VM
    g_pClientGame->GetScriptKeyBinds()->RemoveAllKeys(m_pLuaVM);

    // Remove all resource-specific command bindings while preserving user bindings
    CKeyBindsInterface* pKeyBinds = g_pCore->GetKeyBinds();
    pKeyBinds->SetAllCommandsActive(m_strResourceName, false);

    // Additional cleanup: remove any remaining resource bindings that weren't caught by SetAllCommandsActive
    for (auto& bind : *pKeyBinds)
    {
        if (bind->type == KeyBindType::COMMAND)
        {
            auto commandBind = static_cast<CCommandBind*>(bind.get());
            if (commandBind->context == BindingContext::RESOURCE && commandBind->resource == m_strResourceName)
            {
                pKeyBinds->Remove(commandBind);
            }
        }
    }

    // Destroy the txd root so all dff elements are deleted except those moved out
    g_pClientGame->GetElementDeleter()->DeleteRecursive(m_pResourceTXDRoot);
    m_pResourceTXDRoot = NULL;

    // Destroy the ifp root so all ifp elements are deleted except those moved out
    g_pClientGame->GetElementDeleter()->DeleteRecursive(m_pResourceIFPRoot);
    m_pResourceIFPRoot = NULL;

    // The IMG root is normally destroyed before model deallocation above.
    if (m_pResourceIMGRoot)
    {
        g_pClientGame->GetElementDeleter()->DeleteRecursive(m_pResourceIMGRoot);
        m_pResourceIMGRoot = NULL;
    }

    // Destroy the ddf root so all dff elements are deleted except those moved out
    g_pClientGame->GetElementDeleter()->DeleteRecursive(m_pResourceDFFEntity);
    m_pResourceDFFEntity = NULL;

    // Destroy the colmodel root so all colmodel elements are deleted except those moved out
    g_pClientGame->GetElementDeleter()->DeleteRecursive(m_pResourceCOLRoot);
    m_pResourceCOLRoot = NULL;

    // Destroy the gui root so all gui elements are deleted except those moved out
    g_pClientGame->GetElementDeleter()->DeleteRecursive(m_pResourceGUIEntity);
    m_pResourceGUIEntity = NULL;

    // Undo all changes to water
    g_pGame->GetWaterManager()->UndoChanges(this);

    // Cancel all downloads started by this resource
    if (g_pClientGame->GetSingularFileDownloadManager())
        g_pClientGame->GetSingularFileDownloadManager()->CancelResourceDownloads(this);

    // Destroy the element group attached directly to this resource
    if (m_pDefaultElementGroup)
        delete m_pDefaultElementGroup;
    m_pDefaultElementGroup = NULL;

    m_pRootEntity = NULL;
    m_pResourceEntity = NULL;

    list<CResourceFile*>::iterator iter = m_ResourceFiles.begin();
    for (; iter != m_ResourceFiles.end(); ++iter)
    {
        delete (*iter);
    }
    m_ResourceFiles.clear();

    list<CResourceConfigItem*>::iterator iterc = m_ConfigFiles.begin();
    for (; iterc != m_ConfigFiles.end(); ++iterc)
    {
        delete (*iterc);
    }
    m_ConfigFiles.clear();
}

unsigned int CResource::AcquireElementStreamingLease(CClientStreamElement* pElement)
{
    if (!pElement || pElement->GetStreamReferences(true) == 0xFFFF)
        return 0;

    unsigned int uiToken = 0;
    do
    {
        uiToken = m_uiNextElementStreamingLeaseToken++;
    } while (uiToken == 0 || m_elementStreamingLeases.contains(uiToken));

    auto pLease = std::make_unique<SElementStreamingLease>();
    pLease->element = pElement;
    m_elementStreamingLeases.emplace(uiToken, std::move(pLease));
    pElement->AddStreamReference(true);
    return uiToken;
}

bool CResource::ReleaseElementStreamingLease(unsigned int uiToken)
{
    const auto iter = m_elementStreamingLeases.find(uiToken);
    if (iter == m_elementStreamingLeases.end())
        return false;

    CClientEntity* pEntity = iter->second->element;
    if (pEntity && pEntity->IsStreamingCompatibleClass())
        static_cast<CClientStreamElement*>(pEntity)->RemoveStreamReference(true);

    m_elementStreamingLeases.erase(iter);
    return true;
}

void CResource::ReleaseAllElementStreamingLeases()
{
    while (!m_elementStreamingLeases.empty())
        ReleaseElementStreamingLease(m_elementStreamingLeases.begin()->first);
}

unsigned int CResource::AcquirePedNativeEventProfile(CClientPed* pPed)
{
    if (!pPed)
        return 0;

    unsigned int uiToken = 0;
    do
    {
        uiToken = m_uiNextPedNativeEventProfileToken++;
    } while (uiToken == 0 || m_pedNativeEventProfileLeases.contains(uiToken));

    if (!pPed->AcquireNativeMissionEventProfile(this, uiToken))
        return 0;

    auto pLease = std::make_unique<SPedNativeEventProfileLease>();
    pLease->ped = pPed;
    m_pedNativeEventProfileLeases.emplace(uiToken, std::move(pLease));
    return uiToken;
}

bool CResource::ReleasePedNativeEventProfile(unsigned int uiToken)
{
    const auto iter = m_pedNativeEventProfileLeases.find(uiToken);
    if (iter == m_pedNativeEventProfileLeases.end())
        return false;

    CClientEntity* pEntity = iter->second->ped;
    if (pEntity && pEntity->GetType() == CCLIENTPED)
        static_cast<CClientPed*>(pEntity)->ReleaseNativeMissionEventProfile(this, uiToken);

    m_pedNativeEventProfileLeases.erase(iter);
    return true;
}

bool CResource::IsPedNativeEventProfileActive(CClientPed* pPed, unsigned int uiToken) const
{
    const auto           iter = m_pedNativeEventProfileLeases.find(uiToken);
    const CClientEntity* pLeasedEntity = iter != m_pedNativeEventProfileLeases.end() ? iter->second->ped : nullptr;
    return pPed && pLeasedEntity == pPed && pPed->IsNativeMissionEventProfileActive(this, uiToken);
}

void CResource::ReleaseAllPedNativeEventProfiles()
{
    while (!m_pedNativeEventProfileLeases.empty())
        ReleasePedNativeEventProfile(m_pedNativeEventProfileLeases.begin()->first);
}

CDownloadableResource* CResource::AddResourceFile(CDownloadableResource::eResourceType resourceType, const char* szFileName, uint uiDownloadSize,
                                                  CChecksum serverChecksum, bool bAutoDownload)
{
    // Create the resource file and add it to the list
    SString strBuffer("%s\\resources\\%s\\%s", g_pClientGame->GetFileCacheRoot(), *m_strResourceName, szFileName);

    // Reject duplicates
    if (g_pClientGame->GetResourceManager()->IsResourceFile(strBuffer))
    {
        g_pClientGame->GetScriptDebugging()->LogError(NULL, "Ignoring duplicate file in resource '%s': '%s'", *m_strResourceName, szFileName);
        return NULL;
    }

    CResourceFile* pResourceFile = new CResourceFile(this, resourceType, szFileName, strBuffer, uiDownloadSize, serverChecksum, bAutoDownload);
    if (pResourceFile)
    {
        m_ResourceFiles.push_back(pResourceFile);
    }

    return pResourceFile;
}

CDownloadableResource* CResource::AddConfigFile(const char* szFileName, uint uiDownloadSize, CChecksum serverChecksum)
{
    // Create the config file and add it to the list
    SString strBuffer("%s\\resources\\%s\\%s", g_pClientGame->GetFileCacheRoot(), *m_strResourceName, szFileName);

    // Reject duplicates
    if (g_pClientGame->GetResourceManager()->IsResourceFile(strBuffer))
    {
        g_pClientGame->GetScriptDebugging()->LogError(NULL, "Ignoring duplicate file in resource '%s': '%s'", *m_strResourceName, szFileName);
        return NULL;
    }

    CResourceConfigItem* pConfig = new CResourceConfigItem(this, szFileName, strBuffer, uiDownloadSize, serverChecksum);
    if (pConfig)
    {
        m_ConfigFiles.push_back(pConfig);
    }

    return pConfig;
}

bool CResource::CallExportedFunction(const SString& name, CLuaArguments& args, CLuaArguments& returns, CResource& caller)
{
    if (m_exportedFunctions.find(name) != m_exportedFunctions.end())
        return args.CallGlobal(m_pLuaVM, name.c_str(), &returns);
    return false;
}

bool CResource::VerifyPendingClientChecksums()
{
    bool bQueuedDownload = false;

    const auto queueDownloadForMismatch = [&bQueuedDownload](CDownloadableResource* pDownloadableResource)
    {
        if (!pDownloadableResource->IsAutoDownload() || pDownloadableResource->IsWaitingForDownload() || pDownloadableResource->HasVerifiedClientChecksum())
            return;

        const CChecksum clientChecksum = pDownloadableResource->GenerateClientChecksum();
        if (clientChecksum == pDownloadableResource->GetServerChecksum())
            return;

        const SString strName = pDownloadableResource->GetName();
        FileDelete(strName);
        if (FileExists(strName))
        {
            SString strMessage("Unable to delete old file %s", *ConformResourcePath(strName));
            g_pClientGame->TellServerSomethingImportant(1009, strMessage);
        }

        MakeSureDirExists(strName);
        g_pClientGame->GetResourceFileDownloadManager()->AddPendingFileDownload(pDownloadableResource);
        bQueuedDownload = true;
    };

    for (CResourceConfigItem* pConfigFile : m_ConfigFiles)
        queueDownloadForMismatch(pConfigFile);

    for (CResourceFile* pResourceFile : m_ResourceFiles)
        queueDownloadForMismatch(pResourceFile);

    return bQueuedDownload;
}

bool CResource::CanBeLoaded()
{
    if (IsActive() || IsWaitingForInitialDownloads())
        return false;

    if (VerifyPendingClientChecksums())
        return false;

    return !IsWaitingForInitialDownloads() && VerifyNativeWorldTransportReady();
}

bool CResource::SetNativeWorldTransport(unsigned char format, const SString& manifestPath, unsigned char expectedFileCount)
{
    const bool validFileCount = format == 3 ? expectedFileCount == 1 || (expectedFileCount >= 4 && expectedFileCount <= 35) : expectedFileCount == 3;
    if (m_nativeWorldTransport.present || (format != 1 && format != 2 && format != 3) || !validFileCount)
        return false;

    m_nativeWorldTransport.present = true;
    m_nativeWorldTransport.format = format;
    m_nativeWorldTransport.manifestPath = manifestPath;
    m_nativeWorldTransport.expectedFileCount = expectedFileCount;
    m_nativeWorldTransport.files.reserve(expectedFileCount);
    return true;
}

bool CResource::SetNativeWorldStartupAuthorization(unsigned char wireVersion, unsigned char startupMode, unsigned char policy)
{
    if (!m_nativeWorldTransport.present || m_nativeWorldTransport.authorizationRequested ||
        !IsClosedNativeWorldStartupAuthorization(wireVersion, startupMode, policy, m_nativeWorldTransport.format))
        return false;

    m_nativeWorldTransport.authorizationRequested = true;
    m_nativeWorldTransport.authorizationWireVersion = wireVersion;
    m_nativeWorldTransport.authorizationStartupMode = startupMode;
    m_nativeWorldTransport.authorizationPolicy = policy;
    return true;
}

bool CResource::AddNativeWorldTransportFile(CDownloadableResource* file)
{
    if (!m_nativeWorldTransport.present || !file || m_nativeWorldTransport.files.size() >= m_nativeWorldTransport.expectedFileCount)
        return false;

    for (CDownloadableResource* existing : m_nativeWorldTransport.files)
    {
        if (existing == file || !strcmp(existing->GetShortName(), file->GetShortName()))
            return false;
    }

    m_nativeWorldTransport.files.push_back(file);
    file->SetNativeWorldTransportFile();
    return true;
}

bool CResource::IsNativeWorldTransportDescriptorValid() const
{
    if (!m_nativeWorldTransport.present || (m_nativeWorldTransport.format != 1 && m_nativeWorldTransport.format != 2 && m_nativeWorldTransport.format != 3) ||
        m_nativeWorldTransport.expectedFileCount != m_nativeWorldTransport.files.size())
        return false;

    constexpr uint64_t LEGACY_MAXIMUM_MANIFEST_BYTES = 4096;
    constexpr uint64_t LEGACY_MAXIMUM_IDE_BYTES = 1024 * 1024;
    constexpr uint64_t V3_MAXIMUM_MANIFEST_BYTES = 64 * 1024;
    constexpr uint64_t V3_SET_MAXIMUM_MANIFEST_BYTES = 16 * 1024;
    constexpr uint64_t V3_MAXIMUM_IDE_BYTES = 8 * 1024 * 1024;
    constexpr uint64_t V3_MAXIMUM_LOD_BYTES = 256 * 1024;
    constexpr uint64_t MAXIMUM_IMG_BYTES = 256ULL * 1024 * 1024;
    constexpr uint64_t V3_MAXIMUM_TOTAL_BYTES = 8ULL * 1024 * 1024 * 1024;
    const bool         isV3 = m_nativeWorldTransport.format == 3;
    const bool         isV3Set =
        isV3 && m_nativeWorldTransport.authorizationRequested && m_nativeWorldTransport.authorizationPolicy == NATIVE_WORLD_STATIC_V3_SET_POLICY;
    if (isV3Set != (m_nativeWorldTransport.expectedFileCount == 1))
        return false;
    const uint64_t maximumManifestBytes = isV3Set ? V3_SET_MAXIMUM_MANIFEST_BYTES : isV3 ? V3_MAXIMUM_MANIFEST_BYTES : LEGACY_MAXIMUM_MANIFEST_BYTES;
    const uint64_t maximumIdeBytes = isV3 ? V3_MAXIMUM_IDE_BYTES : LEGACY_MAXIMUM_IDE_BYTES;
    const uint64_t maximumTotalBytes =
        isV3 ? V3_MAXIMUM_MANIFEST_BYTES + V3_MAXIMUM_TOTAL_BYTES : LEGACY_MAXIMUM_MANIFEST_BYTES + LEGACY_MAXIMUM_IDE_BYTES + MAXIMUM_IMG_BYTES;
    uint64_t      totalBytes = 0;
    uint64_t      v3PayloadBytes = 0;
    unsigned int  manifestCount = 0;
    unsigned int  ideCount = 0;
    unsigned int  lodCount = 0;
    unsigned int  imgCount = 0;
    std::uint64_t v3ImageIndexMask = 0;

    for (CDownloadableResource* file : m_nativeWorldTransport.files)
    {
        if (!file || file->GetDownloadSize() == 0)
            return false;

        const std::filesystem::path relativePath(file->GetShortName());
        const std::string           leaf = relativePath.filename().generic_string();
        const uint64_t              bytes = file->GetDownloadSize();
        if (bytes > maximumTotalBytes - totalBytes)
            return false;
        totalBytes += bytes;
        if (m_nativeWorldTransport.manifestPath == file->GetShortName())
        {
            if (leaf != (isV3Set ? "static-world-v3-set.json" : "native-world.json") || bytes > maximumManifestBytes)
                return false;
            ++manifestCount;
        }
        else if (relativePath.extension() == ".ide")
        {
            if ((isV3 && leaf != "world.ide") || bytes > maximumIdeBytes)
                return false;
            if (isV3 && bytes > V3_MAXIMUM_TOTAL_BYTES - v3PayloadBytes)
                return false;
            v3PayloadBytes += bytes;
            ++ideCount;
        }
        else if (relativePath.extension() == ".lod")
        {
            if (!isV3 || isV3Set || leaf != "world.lod" || bytes > V3_MAXIMUM_LOD_BYTES || bytes > V3_MAXIMUM_TOTAL_BYTES - v3PayloadBytes)
                return false;
            v3PayloadBytes += bytes;
            ++lodCount;
        }
        else if (relativePath.extension() == ".img")
        {
            const bool canonicalV3ImageName = leaf.size() == 8 && leaf.front() == 'w' &&
                                              std::all_of(leaf.begin() + 1, leaf.begin() + 4, [](unsigned char c) { return c >= '0' && c <= '9'; }) &&
                                              leaf.compare(4, 4, ".img") == 0;
            if (bytes > MAXIMUM_IMG_BYTES || (isV3 && !canonicalV3ImageName))
                return false;
            if (isV3)
            {
                const unsigned int index = static_cast<unsigned int>((leaf[1] - '0') * 100 + (leaf[2] - '0') * 10 + leaf[3] - '0');
                if (index >= 32 || (v3ImageIndexMask & (1ULL << index)))
                    return false;
                v3ImageIndexMask |= 1ULL << index;
                if (bytes > V3_MAXIMUM_TOTAL_BYTES - v3PayloadBytes)
                    return false;
                v3PayloadBytes += bytes;
            }
            ++imgCount;
        }
        else
            return false;
    }

    const bool validV3Images = imgCount >= 1 && imgCount <= 32 && v3ImageIndexMask == ((1ULL << imgCount) - 1);
    return manifestCount == 1 && (isV3Set || (ideCount == 1 && lodCount == static_cast<unsigned int>(isV3) && (isV3 ? validV3Images : imgCount == 1))) &&
           totalBytes <= maximumTotalBytes && (!isV3 || v3PayloadBytes <= V3_MAXIMUM_TOTAL_BYTES);
}

bool CResource::VerifyNativeWorldTransportReady()
{
    if (!m_nativeWorldTransport.present || m_nativeWorldTransport.publicationCompleted)
        return true;

    if (!IsNativeWorldTransportDescriptorValid())
        return false;

    for (CDownloadableResource* file : m_nativeWorldTransport.files)
    {
        if (file->GetResourceType() != CDownloadableResource::RESOURCE_FILE_TYPE_CLIENT_FILE || !file->IsAutoDownload() || file->IsWaitingForDownload() ||
            !file->HasVerifiedClientChecksum() || !file->DoesClientAndServerChecksumMatch())
        {
            return false;
        }
    }

    if (!m_nativeWorldTransport.publicationStarted)
    {
        SNativeWorldTransportOffer offer;
        offer.resourceName = m_strResourceName;
        offer.format = m_nativeWorldTransport.format;
        offer.manifestRelativePath = m_nativeWorldTransport.manifestPath;
        offer.cancelled = std::make_shared<std::atomic_bool>(false);
        m_nativeWorldTransport.cancellation = offer.cancelled;
        if (m_nativeWorldTransport.authorizationRequested)
        {
            m_nativeWorldTransport.authorizationCaptureAttempted = true;
            std::string captureError;
            if (g_pCore->CaptureNativeWorldStartupAuthorization(m_nativeWorldTransport.authorizationWireVersion,
                                                                m_nativeWorldTransport.authorizationStartupMode, m_nativeWorldTransport.authorizationPolicy,
                                                                m_nativeWorldTransport.format, m_strResourceName.c_str(), GetNetID(), GetStartCounter(),
                                                                m_nativeWorldTransport.authorizationSnapshot, captureError))
            {
                offer.startupAuthorization = std::make_shared<const SNativeWorldStartupAuthorization>(m_nativeWorldTransport.authorizationSnapshot);
            }
            else
                m_nativeWorldTransport.authorizationError = captureError.c_str();
        }
        for (CDownloadableResource* file : m_nativeWorldTransport.files)
        {
            offer.files.push_back({file->GetShortName(), file->GetName(), file->GetDownloadSize()});
        }

        // Hashing and the closed IMG payload audit can process hundreds of MB.
        // The worker owns value copies only; no CResource or downloadable
        // pointer crosses the thread boundary.
        try
        {
            m_nativeWorldTransport.publication =
                std::async(std::launch::async, [offer = std::move(offer)]() { return g_pGame->PublishNativeWorldTransportOffer(offer); });
            m_nativeWorldTransport.publicationStarted = true;
        }
        catch (const std::exception& exception)
        {
            m_nativeWorldTransport.publicationCompleted = true;
            const SString message(
                "[NativeWorldTransport] state=refused resource=%s reason=async-start-failed detail=%s activation=no lease=no "
                "stock-behavior=preserved",
                *m_strResourceName, exception.what());
            AddReportLog(7472, message);
            WriteDebugEvent(message);
            return true;
        }
        const SString message("[NativeWorldTransport] state=audit-started resource=%s format=%u manifest=%s files=%u activation=no lease=no",
                              *m_strResourceName, m_nativeWorldTransport.format, *m_nativeWorldTransport.manifestPath,
                              static_cast<unsigned int>(m_nativeWorldTransport.files.size()));
        AddReportLog(7470, message);
        WriteDebugEvent(message);
        return false;
    }

    if (m_nativeWorldTransport.publication.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return false;

    SNativeWorldTransportPublishResult result;
    try
    {
        result = m_nativeWorldTransport.publication.get();
    }
    catch (const std::exception& exception)
    {
        result.error = SString("async-publication-exception: %s", exception.what());
    }
    catch (...)
    {
        result.error = "async-publication-unknown-exception";
    }
    m_nativeWorldTransport.publicationCompleted = true;
    if (result.success)
    {
        SNativeWorldAuthorizationRecordResult authorizationResult;
        if (m_nativeWorldTransport.authorizationRequested)
        {
            if (!m_nativeWorldTransport.authorizationSnapshot.present)
                authorizationResult.error = m_nativeWorldTransport.authorizationError.c_str();
            else if (!m_nativeWorldTransport.cancellation || m_nativeWorldTransport.cancellation->load(std::memory_order_acquire) || !g_pNet->IsConnected())
                authorizationResult.error = "resource or network was cancelled before authorization publication";
            else
            {
                SNativeWorldAuthorizationPublication publication;
                publication.success = true;
                publication.offerId = result.offerId;
                publication.contentId = result.contentId;
                authorizationResult = g_pCore->PersistNativeWorldStartupAuthorization(m_nativeWorldTransport.authorizationSnapshot, publication);
            }

            if (authorizationResult.success)
            {
                m_nativeWorldTransport.authorizationRecordPublished = true;
                m_nativeWorldTransport.authorizationContentId = result.contentId;
                const SString authorizationMessage(
                    "[NativeWorldAuthorization] state=pending resource=%s contentId=%s ticket=%s issued=%llu expires=%llu disposition=%s "
                    "activation=no lease=no restart-required=yes action=nativeworldauth-restart",
                    *m_strResourceName, result.contentId.c_str(), authorizationResult.ticketId.substr(0, 8).c_str(), authorizationResult.issuedAt,
                    authorizationResult.expiresAt,
                    authorizationResult.attached     ? "attached"
                    : authorizationResult.idempotent ? "idempotent"
                                                     : "published");
                AddReportLog(7473, authorizationMessage);
                WriteDebugEvent(authorizationMessage);
                g_pCore->GetConsole()->Printf("%s", *authorizationMessage);
            }
            else
            {
                // A successful pending rename followed by an inconclusive
                // reopen must remain attached to this resource so an explicit
                // ResourceStop can retry terminalization under the store lock.
                if (authorizationResult.publicationAmbiguous)
                {
                    m_nativeWorldTransport.authorizationPublicationAmbiguous = true;
                    m_nativeWorldTransport.authorizationContentId = result.contentId;
                }
                const SString authorizationMessage(
                    "[NativeWorldAuthorization] state=refused resource=%s contentId=%s reason=%s activation=no lease=no restart-required=no "
                    "stock-behavior=preserved",
                    *m_strResourceName, result.contentId.c_str(), authorizationResult.error.c_str());
                AddReportLog(7474, authorizationMessage);
                WriteDebugEvent(authorizationMessage);
                g_pCore->GetConsole()->Printf("%s", *authorizationMessage);
            }
        }
        const SString message(
            "[NativeWorldTransport] state=cached resource=%s format=%u manifest=%s files=%u offerId=%s contentId=%s disposition=%s directory=%s "
            "audit=%s publish=atomic activation=no lease=no restart-required=%s",
            *m_strResourceName, m_nativeWorldTransport.format, *m_nativeWorldTransport.manifestPath,
            static_cast<unsigned int>(m_nativeWorldTransport.files.size()), result.offerId.c_str(), result.contentId.c_str(),
            result.cacheHit ? "hit" : "published", result.publishedDirectory.c_str(), result.auditProfile.c_str(),
            m_nativeWorldTransport.authorizationRecordPublished ? "yes" : "no");
        AddReportLog(7471, message);
        WriteDebugEvent(message);
        g_pCore->GetConsole()->Printf("%s", *message);
    }
    else
    {
        const char*   activationState = result.existingActivationActive ? "active" : "no";
        const char*   leaseState = result.existingActivationActive ? "process" : "no";
        const char*   preservedState = result.existingActivationActive ? "existing-native-world=preserved" : "stock-behavior=preserved";
        const SString message("[NativeWorldTransport] state=refused resource=%s format=%u manifest=%s files=%u reason=%s activation=%s lease=%s %s",
                              *m_strResourceName, m_nativeWorldTransport.format, *m_nativeWorldTransport.manifestPath,
                              static_cast<unsigned int>(m_nativeWorldTransport.files.size()), result.error.c_str(), activationState, leaseState,
                              preservedState);
        AddReportLog(7472, message);
        WriteDebugEvent(message);
        g_pCore->GetConsole()->Printf("%s", *message);
        if (m_nativeWorldTransport.authorizationRequested)
        {
            const SString authorizationMessage(
                "[NativeWorldAuthorization] state=refused resource=%s reason=transport-publication-failed activation=%s lease=%s restart-required=no %s",
                *m_strResourceName, activationState, leaseState, preservedState);
            AddReportLog(7474, authorizationMessage);
            WriteDebugEvent(authorizationMessage);
        }
    }
    return true;
}

void CResource::RevokeNativeWorldStartupAuthorization()
{
    if ((!m_nativeWorldTransport.authorizationRecordPublished && !m_nativeWorldTransport.authorizationPublicationAmbiguous) ||
        !m_nativeWorldTransport.authorizationSnapshot.present || m_nativeWorldTransport.authorizationContentId.empty())
        return;

    const SNativeWorldAuthorizationRecordResult result =
        g_pCore->RevokeNativeWorldStartupAuthorization(m_nativeWorldTransport.authorizationSnapshot, m_nativeWorldTransport.authorizationContentId);
    const SString message =
        result.success ? SString("[NativeWorldAuthorization] state=revoked resource=%s ticket=%s activation=no lease=no restart-required=no",
                                 *m_strResourceName, result.ticketId.substr(0, 8).c_str())
                       : SString("[NativeWorldAuthorization] state=revocation-refused resource=%s reason=%s activation=no lease=no restart-required=no",
                                 *m_strResourceName, result.error.c_str());
    AddReportLog(result.success ? 7475 : 7474, message);
    WriteDebugEvent(message);
    g_pCore->GetConsole()->Printf("%s", *message);
    if (result.success)
    {
        m_nativeWorldTransport.authorizationRecordPublished = false;
        m_nativeWorldTransport.authorizationPublicationAmbiguous = false;
    }
    else
    {
        // Keep retry ownership outside the resource: Packet_ResourceStop
        // destroys this object immediately after this call. Generic teardown
        // must not invoke revocation because an intentional restart destroys
        // resources while preserving the pending launch ticket.
        g_pClientGame->GetResourceManager()->RetireNativeWorldAuthorizationRevocation(m_nativeWorldTransport.authorizationSnapshot,
                                                                                      m_nativeWorldTransport.authorizationContentId, m_strResourceName);
        m_nativeWorldTransport.authorizationRecordPublished = false;
        m_nativeWorldTransport.authorizationPublicationAmbiguous = false;
        m_nativeWorldTransport.authorizationContentId.clear();
    }
}

bool CResource::IsNativeWorldTransportPublicationPending() const noexcept
{
    return m_nativeWorldTransport.present && m_nativeWorldTransport.publicationStarted && !m_nativeWorldTransport.publicationCompleted;
}

bool CResource::IsWaitingForInitialDownloads()
{
    for (std::list<CResourceConfigItem*>::iterator iter = m_ConfigFiles.begin(); iter != m_ConfigFiles.end(); ++iter)
        if ((*iter)->IsWaitingForDownload())
            return true;

    for (std::list<CResourceFile*>::iterator iter = m_ResourceFiles.begin(); iter != m_ResourceFiles.end(); ++iter)
        if ((*iter)->IsAutoDownload())
            if ((*iter)->IsWaitingForDownload())
                return true;
    return false;
}

void CResource::Load()
{
    dassert(CanBeLoaded());
    m_pRootEntity = g_pClientGame->GetRootEntity();

    if (m_usRemainingNoClientCacheScripts > 0)
    {
        m_bLoadAfterReceivingNoClientCacheScripts = true;
        return;
    }

    if (m_pRootEntity)
    {
        // Set the GUI parent to the resource root entity
        m_pResourceCOLRoot->SetParent(m_pResourceEntity);
        m_pResourceDFFEntity->SetParent(m_pResourceEntity);
        m_pResourceGUIEntity->SetParent(m_pResourceEntity);
        m_pResourceTXDRoot->SetParent(m_pResourceEntity);
    }

    CLogger::LogPrintf("> Starting resource '%s'\n", *m_strResourceName);

    // Flag resource files as readable
    for (std::list<CResourceConfigItem*>::iterator iter = m_ConfigFiles.begin(); iter != m_ConfigFiles.end(); ++iter)
        (*iter)->SetDownloaded();

    for (std::list<CResourceFile*>::iterator iter = m_ResourceFiles.begin(); iter != m_ResourceFiles.end(); ++iter)
        if ((*iter)->IsAutoDownload())
            (*iter)->SetDownloaded();

    // Load config files
    list<CResourceConfigItem*>::iterator iterc = m_ConfigFiles.begin();
    for (; iterc != m_ConfigFiles.end(); ++iterc)
    {
        if (!(*iterc)->Start())
        {
            CLogger::LogPrintf("Failed to start resource item %s in %s\n", (*iterc)->GetName(), *m_strResourceName);
        }
    }

    for (auto& list = m_NoClientCacheScriptList; !list.empty(); list.pop_front())
    {
        DECLARE_PROFILER_SECTION(OnPreLoadNoClientCacheScript)

        auto& item = list.front();
        GetVM()->LoadScriptFromBuffer(item.buffer.GetData(), item.buffer.GetSize(), item.strFilename);
        item.buffer.ZeroClear();

        DECLARE_PROFILER_SECTION(OnPostLoadNoClientCacheScript)
    }

    // Load the files that are queued in the list "to be loaded"
    list<CResourceFile*>::iterator iter = m_ResourceFiles.begin();
    for (; iter != m_ResourceFiles.end(); ++iter)
    {
        CResourceFile* pResourceFile = *iter;
        // Only load the resource file if it is a client script
        if (pResourceFile->GetResourceType() == CDownloadableResource::RESOURCE_FILE_TYPE_CLIENT_SCRIPT)
        {
            // Load the file
            std::vector<char> buffer;
            const bool        bLoaded = FileLoad(pResourceFile->GetName(), buffer);
            const char*       pBufferData = buffer.empty() ? nullptr : &buffer.at(0);

            DECLARE_PROFILER_SECTION(OnPreLoadScript)
            // Check the contents
            if (bLoaded)
            {
                const CChecksum checksum = CChecksum::GenerateChecksumFromBuffer(pBufferData, buffer.size());
                pResourceFile->SetLastClientChecksum(checksum);

                if (checksum == pResourceFile->GetServerChecksum())
                    m_pLuaVM->LoadScriptFromBuffer(pBufferData, buffer.size(), pResourceFile->GetName());
                else
                    HandleDownloadedFileTrouble(pResourceFile, true);
            }
            else
            {
                pResourceFile->SetLastClientChecksum(CChecksum());
                HandleDownloadedFileTrouble(pResourceFile, true);
            }
            DECLARE_PROFILER_SECTION(OnPostLoadScript)
        }
        else if (pResourceFile->IsAutoDownload())
        {
            if (!pResourceFile->DoesClientAndServerChecksumMatch())
            {
                HandleDownloadedFileTrouble(pResourceFile, false);
            }
        }
    }

    // Set active flag
    m_bActive = true;
    m_bStarting = false;

    // Did we get a resource root entity?
    if (m_pResourceEntity)
    {
        // Call the Lua "onClientResourceStart" event
        CLuaArguments Arguments;
        Arguments.PushResource(this);
        m_pResourceEntity->CallEvent("onClientResourceStart", Arguments, true);

        NetBitStreamInterface* pBitStream = g_pNet->AllocateNetBitStream();
        if (pBitStream)
        {
            // Write resource net ID
            pBitStream->Write(GetNetID());
            pBitStream->Write(GetStartCounter());
            g_pNet->SendPacket(PACKET_ID_PLAYER_RESOURCE_START, pBitStream, PACKET_PRIORITY_HIGH, PACKET_RELIABILITY_RELIABLE_ORDERED);
            g_pNet->DeallocateNetBitStream(pBitStream);
        }
    }
    else
        assert(0);
}

void CResource::Stop()
{
    m_bStarting = false;
    m_bStopping = true;
    CLuaArguments Arguments;
    Arguments.PushResource(this);
    m_pResourceEntity->CallEvent("onClientResourceStop", Arguments, true);

    if (g_pGame && g_pGame->GetWorld())
        g_pGame->GetWorld()->RemoveCullZoneChangesByOwner(this);

    // When a custom application is used - reset discord stuff
    const auto discord = g_pCore->GetDiscord();
    if (discord && !discord->IsDiscordCustomDetailsDisallowed() && discord->GetDiscordResourceName() == m_strResourceName)
    {
        if (discord->IsDiscordRPCEnabled())
        {
            discord->ResetDiscordData();
            discord->SetPresenceState(_("In-game"), false);
            const time_t  now = time(nullptr);
            unsigned long startTimestamp = 0;
            if (now > 0)
            {
                const auto maxValue = std::numeric_limits<unsigned long>::max();
                const auto nowUnsigned = static_cast<unsigned long long>(now);
                startTimestamp = (nowUnsigned > maxValue) ? maxValue : static_cast<unsigned long>(now);
            }

            discord->SetPresenceStartTimestamp(startTimestamp);
            discord->UpdatePresence();
        }
    }
}

SString CResource::GetState()
{
    if (m_bStarting)
        return "starting";
    else if (m_bStopping)
        return "stopping";
    else if (m_bActive)
        return "running";
    else
        return "loaded";
}

void CResource::DeleteClientChildren()
{
    // Run this on our resource entity if we have one
    if (m_pResourceEntity)
        m_pResourceEntity->DeleteClientChildren();
}

void CResource::ShowCursor(bool bShow, bool bToggleControls)
{
    // Different cursor showing state than earlier?
    if (bShow != m_bShowingCursor)
    {
        // Going to show the cursor?
        if (bShow)
        {
            // Increase the cursor ref count
            m_iShowingCursor += 1;
        }
        else
        {
            // Decrease the cursor ref count
            m_iShowingCursor -= 1;
        }

        // Update our showing cursor state
        m_bShowingCursor = bShow;
    }

    bool bWantsToggle = m_bShowingCursor && bToggleControls;
    if (bWantsToggle != m_bToggleControls)
    {
        if (bWantsToggle)
            m_iToggleControls += 1;
        else
            m_iToggleControls -= 1;

        m_bToggleControls = bWantsToggle;
    }

    // Always update cursor and controls state regardless of cursor visibility change
    g_pCore->ForceCursorVisible(m_iShowingCursor > 0, m_iToggleControls > 0);
    g_pClientGame->SetCursorEventsEnabled(m_iShowingCursor > 0);
}

SString CResource::GetResourceDirectoryPath(eAccessType accessType, const SString& strMetaPath)
{
    // See if private files should be moved to a new directory
    if (accessType == ACCESS_PRIVATE)
    {
        if (!m_strResourcePrivateDirectoryPathOld.empty())
        {
            SString strNewFilePath = PathJoin(m_strResourcePrivateDirectoryPath, strMetaPath);
            SString strOldFilePath = PathJoin(m_strResourcePrivateDirectoryPathOld, strMetaPath);

            if (FileExists(strOldFilePath))
            {
                if (FileExists(strNewFilePath))
                {
                    // If file exists in old and new, delete from old
                    OutputDebugLine(SString("Deleting %s", *strOldFilePath));
                    FileDelete(strOldFilePath);
                }
                else
                {
                    // If file exists in old only, move from old to new
                    OutputDebugLine(SString("Moving %s to %s", *strOldFilePath, *strNewFilePath));
                    MakeSureDirExists(strNewFilePath);
                    FileRename(strOldFilePath, strNewFilePath);
                }
            }
        }
        return PathJoin(m_strResourcePrivateDirectoryPath, strMetaPath);
    }
    return PathJoin(m_strResourceDirectoryPath, strMetaPath);
}

CResourceFile* CResource::GetResourceFile(const SString& relativePath) const
{
    for (CResourceFile* resourceFile : m_ResourceFiles)
    {
        if (!stricmp(relativePath.c_str(), resourceFile->GetShortName()))
        {
            return resourceFile;
        }
    }

    return nullptr;
}

void CResource::LoadNoClientCacheScript(const char* chunk, unsigned int len, const SString& strFilename)
{
    if (m_usRemainingNoClientCacheScripts > 0)
    {
        --m_usRemainingNoClientCacheScripts;

        // Store for later
        m_NoClientCacheScriptList.push_back(SNoClientCacheScript());
        SNoClientCacheScript& item = m_NoClientCacheScriptList.back();
        item.buffer = CBuffer(chunk, len);
        item.strFilename = strFilename;

        if (m_usRemainingNoClientCacheScripts == 0 && m_bLoadAfterReceivingNoClientCacheScripts)
        {
            m_bLoadAfterReceivingNoClientCacheScripts = false;
            Load();
        }
    }
}

//
// Add element to the default element group
//
void CResource::AddToElementGroup(CClientEntity* pElement)
{
    if (m_pDefaultElementGroup)
    {
        m_pDefaultElementGroup->Add(pElement);
    }
}

//
// Handle when things go wrong
//
void CResource::HandleDownloadedFileTrouble(CResourceFile* pResourceFile, bool bScript)
{
    SString errorMessage;

    CChecksum clientChecksum = pResourceFile->GetLastClientChecksum();
    if (!pResourceFile->HasVerifiedClientChecksum())
    {
        errorMessage = "Client checksum was not verified before load";
    }
    else if (clientChecksum == CChecksum())
    {
        errorMessage = SString("File not readable: %s", pResourceFile->GetName());
    }
    else
    {
        SString strGotMd5 = ConvertDataToHexString(clientChecksum.md5.data, sizeof(MD5));
        SString strWantedMd5 = ConvertDataToHexString(pResourceFile->GetServerChecksum().md5.data, sizeof(MD5));
        errorMessage =
            SString("Got CRC:%08lX MD5:%s, wanted CRC:%08lX MD5:%s", clientChecksum.ulCRC, *strGotMd5, pResourceFile->GetServerChecksum().ulCRC, *strWantedMd5);
    }

    SString strFilename = ExtractFilename(PathConform(pResourceFile->GetShortName()));
    SString strMessage = SString("HTTP server file mismatch! (%s) %s [%s]", GetName(), *strFilename, *errorMessage);

    // Log to the server & client console
    g_pClientGame->TellServerSomethingImportant(bScript ? 1002 : 1013, strMessage, 4);
    g_pCore->GetConsole()->Printf("Download error: %s", *strMessage);
}
