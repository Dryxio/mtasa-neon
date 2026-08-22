/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/CResource.h
 *  PURPOSE:     Resource object class
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include "lua/CLuaManager.h"
#include "CClientEntity.h"
#include "CResourceConfigItem.h"
#include "CResourceFile.h"
#include "CResourceModelStreamer.h"
#include "CElementGroup.h"
#include "CPedNativeEventProfile.h"
#include <game/CGame.h>
#include <chrono>
#include <ctime>
#include <future>
#include <list>
#include <memory>
#include <unordered_map>
#include <vector>

#define MAX_RESOURCE_NAME_LENGTH 255
#define MAX_FUNCTION_NAME_LENGTH 50

struct SNoClientCacheScript
{
    CBuffer buffer;
    SString strFilename;
};

struct SPendingFileDownload
{
    SString strUrl;
    SString strFilename;
    double  dDownloadSize;
};

class CResource
{
public:
    CResource(unsigned short usNetID, const char* szResourceName, CClientEntity* pResourceEntity, CClientEntity* pResourceDynamicEntity,
              const CMtaVersion& strMinServerReq, const CMtaVersion& strMinClientReq, bool bEnableOOP);
    ~CResource();

    unsigned short GetNetID() { return m_usNetID; };
    uint           GetScriptID() const { return m_uiScriptID; };
    const char*    GetName() { return m_strResourceName; };
    CLuaMain*      GetVM() { return m_pLuaVM; };
    bool           IsActive() { return m_bActive; };
    bool           CanBeLoaded();

    void    Load();
    void    Stop();
    SString GetState();

    unsigned int                     AcquireElementStreamingLease(class CClientStreamElement* pElement);
    bool                             ReleaseElementStreamingLease(unsigned int uiToken);
    void                             ReleaseAllElementStreamingLeases();
    unsigned int                     AcquirePedNativeEventProfile(class CClientPed* pPed, ePedNativeEventProfile profile);
    bool                             ReleasePedNativeEventProfile(unsigned int uiToken);
    bool                             IsPedNativeEventProfileActive(class CClientPed* pPed, unsigned int uiToken) const;
    bool                             HasPedNativeEventProfileLease(class CClientPed* pPed, unsigned int uiToken) const;
    void                             ReleaseAllPedNativeEventProfiles();
    unsigned int                     AcquirePedNativeGroup(const std::vector<class CClientPed*>& peds);
    bool                             ReleasePedNativeGroup(unsigned int uiToken);
    bool                             IsPedNativeGroupActive(unsigned int uiToken) const;
    SAmbientPedNativeGroupDiagnostic GetPedNativeGroupDiagnostic(unsigned int uiToken) const;
    void                             ReleaseAllPedNativeGroups();
    bool         ValidatePedNativeCouple(class CClientPed* pPedA, class CClientPed* pPedB, SAmbientPedNativeCoupleValidation& validation) const;
    unsigned int AcquirePedNativeCouple(class CClientPed* pPedA, class CClientPed* pPedB, bool aLeader);
    bool         ReleasePedNativeCouple(unsigned int uiToken);
    bool         IsPedNativeCoupleActive(unsigned int uiToken) const;
    SAmbientPedNativeCoupleDiagnostic GetPedNativeCoupleDiagnostic(unsigned int uiToken) const;
    void                              ReleaseAllPedNativeCouples();
    unsigned int                      AcquirePedNativeCouplePresentation(class CClientPed* pPedA, class CClientPed* pPedB);
    bool                              UpdatePedNativeCouplePresentation(unsigned int uiToken, unsigned char sideA = 0, unsigned char sideB = 0);
    bool                              ReleasePedNativeCouplePresentation(unsigned int uiToken);
    bool                              IsPedNativeCouplePresentationActive(unsigned int uiToken) const;
    void                              ReleaseAllPedNativeCouplePresentations();

    CDownloadableResource* AddResourceFile(CDownloadableResource::eResourceType resourceType, const char* szFileName, uint uiDownloadSize,
                                           CChecksum serverChecksum, bool bAutoDownload);
    CDownloadableResource* AddConfigFile(const char* szFileName, uint uiDownloadSize, CChecksum serverChecksum);

    std::list<class CResourceConfigItem*>::iterator ConfigIterBegin() { return m_ConfigFiles.begin(); }
    std::list<class CResourceConfigItem*>::iterator ConfigIterEnd() { return m_ConfigFiles.end(); }

    CElementGroup* GetElementGroup() { return m_pDefaultElementGroup; }
    void           AddToElementGroup(CClientEntity* pElement);

    void AddExportedFunction(const SString& name) { m_exportedFunctions.insert(name); }
    bool CallExportedFunction(const SString& name, CLuaArguments& args, CLuaArguments& returns, CResource& caller);

    class CClientEntity* GetResourceEntity() { return m_pResourceEntity; }
    void                 SetResourceEntity(CClientEntity* pEntity) { m_pResourceEntity = pEntity; }
    class CClientEntity* GetResourceDynamicEntity() { return m_pResourceDynamicEntity; }
    void                 SetResourceDynamicEntity(CClientEntity* pEntity) { m_pResourceDynamicEntity = pEntity; }
    SString              GetResourceDirectoryPath() { return GetResourceDirectoryPath(eAccessType::ACCESS_PUBLIC, ""); }
    SString              GetResourceDirectoryPath(eAccessType accessType, const SString& strMetaPath);
    class CClientEntity* GetResourceGUIEntity() { return m_pResourceGUIEntity; }
    void                 SetResourceGUIEntity(CClientEntity* pEntity) { m_pResourceGUIEntity = pEntity; }
    CClientEntity*       GetResourceCOLModelRoot() { return m_pResourceCOLRoot; };
    CClientEntity*       GetResourceDFFRoot() { return m_pResourceDFFEntity; };
    CClientEntity*       GetResourceTXDRoot() { return m_pResourceTXDRoot; };
    CClientEntity*       GetResourceIFPRoot() { return m_pResourceIFPRoot; };
    CClientEntity*       GetResourceIMGRoot() { return m_pResourceIMGRoot; };

    CResourceModelStreamer* GetResourceModelStreamer() { return &m_modelStreamer; };

    // This is to delete all the elements created in this resource that are created locally in this client
    void DeleteClientChildren();

    // Use this for cursor showing/hiding
    void ShowCursor(bool bShow, bool bToggleControls = true);

    const auto& GetExportedFunctions() const noexcept { return m_exportedFunctions; }

    std::list<CResourceFile*>::iterator IterBeginResourceFiles() { return m_ResourceFiles.begin(); }
    std::list<CResourceFile*>::iterator IterEndResourceFiles() { return m_ResourceFiles.end(); }

    /**
     * @brief Searches for a CResourceFile with the given relative path.
     * @param relativePath Relative resource file path (from meta)
     * @return A pointer to CResourceFile on success, null otherwise
     */
    CResourceFile* GetResourceFile(const SString& relativePath) const;

    void               SetRemainingNoClientCacheScripts(unsigned short usRemaining) { m_usRemainingNoClientCacheScripts = usRemaining; }
    void               LoadNoClientCacheScript(const char* chunk, unsigned int length, const SString& strFilename);
    const CMtaVersion& GetMinServerReq() const { return m_strMinServerReq; }
    const CMtaVersion& GetMinClientReq() const { return m_strMinClientReq; }
    bool               IsOOPEnabled() { return m_bOOPEnabled; }
    void               HandleDownloadedFileTrouble(CResourceFile* pResourceFile, bool bScript);
    bool               IsWaitingForInitialDownloads();
    int                GetDownloadPriorityGroup() { return m_iDownloadPriorityGroup; }
    void               SetDownloadPriorityGroup(int iDownloadPriorityGroup) { m_iDownloadPriorityGroup = iDownloadPriorityGroup; }

    void         SetStartCounter(unsigned int startCounter) { m_startCounter = startCounter; }
    unsigned int GetStartCounter() const noexcept { return m_startCounter; }

    bool HasNativeWorldTransport() const noexcept { return m_nativeWorldTransport.present; }
    bool SetNativeWorldTransport(unsigned char format, const SString& manifestPath, unsigned char expectedFileCount);
    bool AddNativeWorldTransportFile(CDownloadableResource* file);
    bool IsNativeWorldTransportDescriptorValid() const;
    bool IsNativeWorldTransportPublicationPending() const noexcept;
    bool SetNativeWorldStartupAuthorization(unsigned char wireVersion, unsigned char startupMode, unsigned char policy);
    void RevokeNativeWorldStartupAuthorization();
    void PulseNativeWorldRuntimeAdmission();

private:
    unsigned short       m_usNetID;
    uint                 m_uiScriptID;
    unsigned int         m_startCounter{};
    SString              m_strResourceName;
    CLuaMain*            m_pLuaVM;
    CLuaManager*         m_pLuaManager;
    class CClientEntity* m_pRootEntity;
    bool                 m_bActive;
    bool                 m_bStarting;
    bool                 m_bStopping;
    class CClientEntity* m_pResourceEntity;         // no idea what this is used for anymore
    class CClientEntity* m_pResourceDynamicEntity;  // parent of elements created by the resource
    class CClientEntity* m_pResourceCOLRoot;
    class CClientEntity* m_pResourceDFFEntity;
    class CClientEntity* m_pResourceGUIEntity;
    class CClientEntity* m_pResourceTXDRoot;
    class CClientEntity* m_pResourceIFPRoot;
    class CClientEntity* m_pResourceIMGRoot;
    unsigned short       m_usRemainingNoClientCacheScripts;
    bool                 m_bLoadAfterReceivingNoClientCacheScripts;
    CMtaVersion          m_strMinServerReq;
    CMtaVersion          m_strMinClientReq;
    bool                 m_bOOPEnabled;
    int                  m_iDownloadPriorityGroup;

    // To control cursor show/hide
    static int m_iShowingCursor;
    bool       m_bShowingCursor;
    static int m_iToggleControls;
    bool       m_bToggleControls;

    SString m_strResourceDirectoryPath;            // stores the path to /mods/deathmatch/resources/resource_name
    SString m_strResourcePrivateDirectoryPath;     // stores the path to /mods/deathmatch/priv/server-id/resource_name
    SString m_strResourcePrivateDirectoryPathOld;  // stores the path to /mods/deathmatch/priv/old-server-id/resource_name

    std::list<class CResourceFile*>       m_ResourceFiles;
    std::list<class CResourceConfigItem*> m_ConfigFiles;
    CFastHashSet<SString>                 m_exportedFunctions;
    CElementGroup*                        m_pDefaultElementGroup;  // stores elements created by scripts in this resource
    std::list<SNoClientCacheScript>       m_NoClientCacheScriptList;

    CResourceModelStreamer m_modelStreamer{};

    struct SElementStreamingLease
    {
        CClientEntityPtr element;
    };

    unsigned int                                                              m_uiNextElementStreamingLeaseToken{1};
    std::unordered_map<unsigned int, std::unique_ptr<SElementStreamingLease>> m_elementStreamingLeases;

    struct SPedNativeEventProfileLease
    {
        CClientEntityPtr       ped;
        ePedNativeEventProfile profile{ePedNativeEventProfile::NONE};
    };

    unsigned int                                                                   m_uiNextPedNativeEventProfileToken{1};
    std::unordered_map<unsigned int, std::unique_ptr<SPedNativeEventProfileLease>> m_pedNativeEventProfileLeases;

    struct SPedNativeGroupLease
    {
        std::vector<CClientEntityPtr> peds;
        unsigned int                  nativeGroupId{};
    };

    unsigned int                                                            m_uiNextPedNativeGroupToken{1};
    std::unordered_map<unsigned int, std::unique_ptr<SPedNativeGroupLease>> m_pedNativeGroupLeases;

    struct SPedNativeCoupleLease
    {
        CClientEntityPtr peds[2];
        unsigned int     nativeCoupleId{};
        bool             aLeader{};
    };

    unsigned int                                                             m_uiNextPedNativeCoupleToken{1};
    std::unordered_map<unsigned int, std::unique_ptr<SPedNativeCoupleLease>> m_pedNativeCoupleLeases;

    struct SPedNativeCouplePresentationLease
    {
        CClientEntityPtr peds[2];
        unsigned int     nativePresentationId{};
    };

    unsigned int                                                                         m_uiNextPedNativeCouplePresentationToken{1};
    std::unordered_map<unsigned int, std::unique_ptr<SPedNativeCouplePresentationLease>> m_pedNativeCouplePresentationLeases;

    struct
    {
        bool                                            present{};
        bool                                            logged{};
        bool                                            publicationStarted{};
        bool                                            publicationCompleted{};
        unsigned char                                   format{};
        SString                                         manifestPath;
        std::vector<CDownloadableResource*>             files;
        size_t                                          expectedFileCount{};
        std::shared_ptr<std::atomic_bool>               cancellation;
        std::future<SNativeWorldTransportPublishResult> publication;
        bool                                            authorizationRequested{};
        bool                                            authorizationCaptureAttempted{};
        bool                                            authorizationRecordPublished{};
        bool                                            authorizationPublicationAmbiguous{};
        bool                                            authorizationRuntimeDeferred{};
        unsigned char                                   authorizationWireVersion{};
        unsigned char                                   authorizationStartupMode{};
        unsigned char                                   authorizationPolicy{};
        SNativeWorldStartupAuthorization                authorizationSnapshot;
        SString                                         authorizationError;
        std::string                                     authorizationContentId;
        SNativeWorldAuthorizationPublication            authorizationPublication;
        SNativeWorldAuthorizationRecordResult           authorizationPersistedResult;
        std::chrono::steady_clock::time_point           authorizationRuntimeNextAttempt{};
    } m_nativeWorldTransport;

    bool VerifyPendingClientChecksums();
    bool VerifyNativeWorldTransportReady();
};
