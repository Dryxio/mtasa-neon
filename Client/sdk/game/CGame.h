/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        sdk/game/CGame.h
 *  PURPOSE:     Game base interface
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <memory>
#include <map>
#include <atomic>
#include <string>
#include <vector>
#include <SString.h>
#include <CVector.h>
#include <core/CNativeWorldAuthorization.h>
#include "Common.h"
#include "CWeaponInfo.h"
#include "enums/SystemState.h"

class C3DMarkers;
class CAEAudioHardware;
class CAERadioTrackManager;
class CAESoundManager;
class CAnimBlendAssocGroup;
class CAnimManager;
class CAudioContainer;
class CAudioEngine;
class CCamera;
class CCarEnterExit;
class CCheckpoints;
class CClock;
class CColStore;
class CControllerConfigManager;
class CCoronas;
class CEventList;
class CExplosionManager;
class CFireManager;
class CFx;
class CFxManager;
class CGameSettings;
class CGarages;
class CHandlingManager;
class CHud;
class CKeyGen;
class CModelInfo;
class CObjectGroupPhysicalProperties;
class CPad;
class CPathFind;
class CPed;
class CPickups;
class CPlayerInfo;
class CPointLights;
class CPools;
class CProjectileInfo;
class CRadar;
class CRenderWare;
class CRopes;
class CStats;
class CStreaming;
struct CStreamingInfo;
class CTasks;
class CVisibilityPlugins;
class CWaterManager;
class CVehicle;
class CWeapon;
class CWeaponInfo;
class CWeaponStat;
class CWeaponStatManager;
class CWeather;
class CWorld;
class CIplStore;
class CBuildingRemoval;
class CRenderer;
class CVehicleAudioSettingsManager;
enum eEntityType;
enum ePedPieceTypes;

typedef bool(PreWeaponFireHandler)(class CPlayerPed* pPlayer, bool bStopIfUsingBulletSync);
typedef void(PostWeaponFireHandler)();
typedef void(TaskSimpleBeHitHandler)(class CPedSAInterface* pPedAttacker, ePedPieceTypes hitBodyPart, int hitBodySide, int weaponId);

// Captured immediately after GTA's stock group decision maker has selected a
// response. Ped pointers are only for in-process MTA identity resolution;
// diagnostic consumers must persist the stable fields below, never addresses.
struct SNativeAIGroupDecision
{
    class CPed*  representative{};
    class CPed*  sourcePed{};
    unsigned int nativeGroupId{0xFFFFFFFFU};
    int          eventType{-1};
    int          eventSourceType{-1};
    int          taskType{-1};
    unsigned int representativeModel{};
    int          representativePedType{-1};
    unsigned int sourceModel{};
    int          sourcePedType{-1};
    bool         sourceIsPed{};
    bool         sourceIsPlayer{};
    bool         threatened{};
    bool         friendly{};
};

typedef void(NativeAIGroupDecisionHandler)(const SNativeAIGroupDecision& decision);

enum eGameVersion
{
    VERSION_ALL = 0,
    VERSION_EU_10 = 5,
    VERSION_US_10 = 11,
    VERSION_11 = 15,
    VERSION_20 = 20,
    VERSION_UNKNOWN = 0xFF,
};

// File IDs are one contiguous GTA streaming namespace. Keep the partition
// layout as runtime state because a native limit patch relocates every base;
// callers must never reconstruct a later partition from stock constants.
struct SFileIDLayout
{
    std::uint32_t dff{};
    std::uint32_t txd{};
    std::uint32_t col{};
    std::uint32_t ipl{};
    std::uint32_t dat{};
    std::uint32_t ifp{};
    std::uint32_t rrr{};
    std::uint32_t scm{};
    std::uint32_t loadedList{};
    std::uint32_t requestedList{};
    std::uint32_t total{};
};

struct SMatchChannelStats
{
    SString strTag;
    uint    uiNumMatchedTextures;
    uint    uiNumShaderAndEntities;
};

struct SShaderReplacementStats
{
    uint                               uiNumReplacementRequests;
    uint                               uiNumReplacementMatches;
    uint                               uiTotalTextures;
    uint                               uiTotalShaders;
    uint                               uiTotalEntitesRefed;
    std::map<uint, SMatchChannelStats> channelStatsList;
};

struct SNativeWorldTransportFile
{
    std::string  relativePath;
    std::string  absolutePath;
    unsigned int declaredBytes{};
};

struct SNativeWorldStartupAuthorization;

enum class EAmbientPedPopulationClass : unsigned char
{
    Civilian,
    Gang,
};

enum class EAmbientPedPopulationSelection : unsigned char
{
    Automatic,
    Civilian,
    Gang,
};

enum class EAmbientPedPopulationZoneField : unsigned int
{
    PopulationType = 1 << 0,
    Races = 1 << 1,
    DealerStrength = 1 << 2,
    NoCops = 1 << 3,
};

struct SAmbientPedPopulationZoneState
{
    unsigned int   fields{};
    unsigned short gangMask{};
    unsigned char  populationType{};
    unsigned char  races{};
    unsigned char  dealerStrength{};
    unsigned char  noCops{};
    unsigned char  gangStrength[10]{};
};
static_assert(sizeof(SAmbientPedPopulationZoneState) == 20, "Ambient population zone state ABI changed");

// Read directly from GTA's active popcycle state after Neon's reversible
// vanilla zone bootstrap and authoritative campaign mutations have been
// applied. supportedTarget names the civilian/gang subset implemented by the
// current traffic checkpoint; target still reports all four vanilla classes.
struct SAmbientPedPopulationProfile
{
    float         target{};
    float         supportedTarget{};
    float         civilianTarget{};
    float         rawCopTarget{};
    float         copTarget{};
    float         gangTarget{};
    float         dealerTarget{};
    unsigned char zoneType{};
    unsigned char timeIndex{};
    unsigned char weekend{};
    unsigned char dealerStrength{};
    unsigned char raceFlags{};
    unsigned char noCops{};
    unsigned char gangWeights[10]{};
};
static_assert(sizeof(SAmbientPedPopulationProfile) == 44, "Ambient population profile ABI changed");

// A read-only proposal produced by GTA's stock population rules. The caller
// still owns the network element and its lifetime; Game SA never creates an
// unmanaged ambient ped from this structure.
struct SAmbientPedSpawnCandidate
{
    CVector                    position{};
    unsigned int               modelId{};
    unsigned char              pedType{};
    unsigned char              wanderDirection{};
    float                      pathLerp{};
    float                      headingDegrees{};
    EAmbientPedPopulationClass populationClass{EAmbientPedPopulationClass::Civilian};
    unsigned char              gangId{0xFF};
};
static_assert(sizeof(SAmbientPedSpawnCandidate) == 32, "Ambient population candidate ABI changed");
static_assert(offsetof(SAmbientPedSpawnCandidate, pathLerp) == 20, "Ambient population candidate path ABI changed");
static_assert(offsetof(SAmbientPedSpawnCandidate, populationClass) == 28, "Ambient population candidate class ABI changed");

constexpr std::size_t AMBIENT_PED_GROUP_MAX_MEMBERS = 5;

enum class EAmbientPedNativeGroupDiagnosticStatus : unsigned char
{
    Active,
    ResourceLeaseMissing,
    ResourceElementMissing,
    ResourceElementNotPed,
    ResourcePedNotSyncing,
    ResourceGamePedMissing,
    GameLeaseMissing,
    MemberCountMismatch,
    SlotInactive,
    LeaseMemberMismatch,
    MemberFlagMissing,
    MemberDetached,
    NoTrackedMember,
};

struct SAmbientPedNativeGroupMemberDiagnostic
{
    bool          resourceElementPresent{};
    bool          resourceElementIsPed{};
    bool          resourcePedSyncing{};
    bool          gamePedPresent{};
    bool          leaseMemberMatches{};
    bool          nativeAmbientGroupFlag{};
    bool          attachedToExpectedGroup{};
    std::uint32_t gamePedAddress{};
    std::uint32_t expectedGamePedAddress{};
    std::uint32_t primaryTaskAddress{};
    std::uint32_t expectedPrimaryTaskAddress{};
    std::uint32_t defaultTaskAddress{};
    std::uint32_t expectedDefaultTaskAddress{};
    int           primaryTaskType{-1};
    int           defaultTaskType{-1};
};

// This snapshot is intentionally pull-only: diagnostics can inspect a failed
// lease without adding a per-frame logging path to ambient traffic.
struct SAmbientPedNativeGroupDiagnostic
{
    EAmbientPedNativeGroupDiagnosticStatus status{EAmbientPedNativeGroupDiagnosticStatus::ResourceLeaseMissing};
    bool                                   active{};
    bool                                   resourceLeasePresent{};
    bool                                   gameLeasePresent{};
    bool                                   memberCountMatches{};
    bool                                   slotActive{};
    bool                                   hasTrackedMember{};
    unsigned int                           nativeGroupId{};
    unsigned char                          memberCount{};
    SAmbientPedNativeGroupMemberDiagnostic members[AMBIENT_PED_GROUP_MAX_MEMBERS]{};
};

// PlaceRandomGroup normally creates local CPeds. Neon only asks GTA for the
// placement/model proposal, then lets the server create the network elements.
struct SAmbientPedGroupSpawnCandidate
{
    SAmbientPedSpawnCandidate members[AMBIENT_PED_GROUP_MAX_MEMBERS]{};
    unsigned char             count{};
};
static_assert(sizeof(SAmbientPedGroupSpawnCandidate) == 164, "Ambient population group candidate ABI changed");

enum class EAmbientPedSpawnCandidateResult : unsigned char
{
    Success,
    InvalidOrigin,
    NoModel,
    UnsupportedModel,
    NoPath,
    PathDensity,
    VisibleTooClose,
    Blocked,
};

struct SNativeWorldTransportOffer
{
    std::string                                             resourceName;
    unsigned char                                           format{};
    std::string                                             manifestRelativePath;
    std::vector<SNativeWorldTransportFile>                  files;
    std::shared_ptr<std::atomic_bool>                       cancelled;
    std::shared_ptr<const SNativeWorldStartupAuthorization> startupAuthorization;
};

struct SNativeWorldTransportPublishResult
{
    bool        success{};
    bool        cacheHit{};
    bool        existingActivationActive{};
    std::string offerId;
    std::string contentId;
    std::string auditProfile;
    std::string publishedDirectory;
    std::string error;
};

enum class ENativeWorldRuntimeAdmissionReadiness : unsigned char
{
    Ineligible,
    WaitingForIo,
    Ready,
};

// Physical GTA slots owned by the native-world registrar. These are not MTA
// logical model IDs and are unavailable to dynamic allocation while any
// native-world activation is prepared or active.
constexpr uint32_t NATIVE_WORLD_MODEL_ARENA_FIRST = 20000;
constexpr uint32_t NATIVE_WORLD_MODEL_ARENA_LAST = 29999;

class __declspec(novtable) CGame
{
    typedef std::unique_ptr<CAnimBlendAssocGroup> AssocGroup_type;

public:
    virtual CPools*                   GetPools() const noexcept = 0;
    virtual CPlayerInfo*              GetPlayerInfo() = 0;
    virtual CProjectileInfo*          GetProjectileInfo() = 0;
    virtual CRadar*                   GetRadar() = 0;
    virtual CClock*                   GetClock() = 0;
    virtual CCheckpoints*             GetCheckpoints() = 0;
    virtual CCoronas*                 GetCoronas() = 0;
    virtual CEventList*               GetEventList() = 0;
    virtual CFireManager*             GetFireManager() = 0;
    virtual CExplosionManager*        GetExplosionManager() = 0;
    virtual CGarages*                 GetGarages() = 0;
    virtual CHud*                     GetHud() = 0;
    virtual CWeather*                 GetWeather() = 0;
    virtual CWorld*                   GetWorld() = 0;
    virtual CCamera*                  GetCamera() = 0;
    virtual CPickups*                 GetPickups() = 0;
    virtual C3DMarkers*               Get3DMarkers() = 0;
    virtual CPad*                     GetPad() = 0;
    virtual CAERadioTrackManager*     GetAERadioTrackManager() = 0;
    virtual CAudioEngine*             GetAudioEngine() = 0;
    virtual CAEAudioHardware*         GetAEAudioHardware() = 0;
    virtual CAESoundManager*          GetAESoundManager() = 0;
    virtual CAudioContainer*          GetAudioContainer() = 0;
    virtual CStats*                   GetStats() = 0;
    virtual CTasks*                   GetTasks() = 0;
    virtual CGameSettings*            GetSettings() = 0;
    virtual CCarEnterExit*            GetCarEnterExit() = 0;
    virtual CControllerConfigManager* GetControllerConfigManager() = 0;
    virtual CRenderWare*              GetRenderWare() = 0;
    virtual CHandlingManager*         GetHandlingManager() const noexcept = 0;
    virtual CAnimManager*             GetAnimManager() = 0;
    virtual CStreaming*               GetStreaming() = 0;
    virtual CVisibilityPlugins*       GetVisibilityPlugins() = 0;
    virtual CKeyGen*                  GetKeyGen() = 0;
    virtual CRopes*                   GetRopes() = 0;
    virtual CFx*                      GetFx() = 0;
    virtual CFxManager*               GetFxManager() = 0;
    virtual CWaterManager*            GetWaterManager() = 0;
    virtual CWeaponStatManager*       GetWeaponStatManager() = 0;
    virtual CPointLights*             GetPointLights() = 0;
    virtual CColStore*                GetCollisionStore() = 0;
    virtual CBuildingRemoval*         GetBuildingRemoval() = 0;
    virtual CRenderer*                GetRenderer() const noexcept = 0;

    virtual CVehicleAudioSettingsManager* GetVehicleAudioSettingsManager() const noexcept = 0;

    virtual CWeaponInfo* GetWeaponInfo(eWeaponType weapon, eWeaponSkill skill = WEAPONSKILL_STD) = 0;
    virtual CModelInfo*  GetModelInfo(DWORD dwModelID, bool bCanBeInvalid = false) = 0;

    virtual DWORD       GetSystemTime() = 0;
    virtual int         GetSystemFrameCounter() const = 0;
    virtual bool        IsAtMenu() = 0;
    virtual void        StartGame() = 0;
    virtual void        SetSystemState(SystemState State) = 0;
    virtual SystemState GetSystemState() = 0;
    virtual void        Pause(bool bPaused) = 0;
    virtual void        SetTimeScale(float fTimeScale) = 0;
    virtual float       GetFPS() = 0;
    virtual float       GetTimeStep() = 0;
    virtual float       GetOldTimeStep() = 0;
    virtual float       GetTimeScale() = 0;

    virtual void Initialize() = 0;
    virtual void Reset() = 0;
    virtual void Terminate() = 0;

    virtual bool InitLocalPlayer(class CClientPed* pClientPed) = 0;

    virtual float GetGravity() = 0;
    virtual void  SetGravity(float fGravity) = 0;

    virtual float GetGameSpeed() = 0;
    virtual void  SetGameSpeed(float fSpeed) = 0;

    virtual unsigned long GetMinuteDuration() = 0;
    virtual void          SetMinuteDuration(unsigned long ulDelay) = 0;

    virtual unsigned char GetBlurLevel() = 0;
    virtual void          SetBlurLevel(unsigned char ucLevel) = 0;

    virtual void SetJetpackWeaponEnabled(eWeaponType weaponType, bool bEnabled);
    virtual bool GetJetpackWeaponEnabled(eWeaponType weaponType);

    virtual eGameVersion GetGameVersion() = 0;

    // Audits and publishes downloaded native-world bytes only. This API never
    // registers an archive, mutates GTA pools, or retains an activation lease.
    virtual SNativeWorldTransportPublishResult PublishNativeWorldTransportOffer(const SNativeWorldTransportOffer& offer) = 0;

    virtual bool IsCheatEnabled(const char* szCheatName) = 0;
    virtual bool SetCheatEnabled(const char* szCheatName, bool bEnable) = 0;
    virtual void ResetCheats() = 0;

    virtual bool IsRandomFoliageEnabled() = 0;
    virtual void SetRandomFoliageEnabled(bool bEnable) = 0;

    virtual bool IsMoonEasterEggEnabled() = 0;
    virtual void SetMoonEasterEggEnabled(bool bEnable) = 0;

    virtual bool IsExtraAirResistanceEnabled() = 0;
    virtual void SetExtraAirResistanceEnabled(bool bEnable) = 0;

    virtual bool IsUnderWorldWarpEnabled() = 0;
    virtual void SetUnderWorldWarpEnabled(bool bEnable) = 0;

    virtual void SetVehicleSunGlareEnabled(bool bEnabled) = 0;
    virtual bool IsVehicleSunGlareEnabled() = 0;

    virtual void SetCoronaZTestEnabled(bool isEnabled) = 0;
    virtual bool IsCoronaZTestEnabled() const noexcept = 0;

    virtual bool IsWaterCreaturesEnabled() const noexcept = 0;
    virtual void SetWaterCreaturesEnabled(bool isEnabled) = 0;

    virtual bool IsBurnFlippedCarsEnabled() const noexcept = 0;
    virtual void SetBurnFlippedCarsEnabled(bool isEnabled) = 0;

    virtual bool IsFireballDestructEnabled() const noexcept = 0;
    virtual void SetFireballDestructEnabled(bool isEnabled) = 0;

    virtual bool IsExtendedWaterCannonsEnabled() const noexcept = 0;
    virtual void SetExtendedWaterCannonsEnabled(bool isEnabled) = 0;

    virtual bool IsRoadSignsTextEnabled() const noexcept = 0;
    virtual void SetRoadSignsTextEnabled(bool isEnabled) = 0;

    virtual bool IsTunnelWeatherBlendEnabled() const noexcept = 0;
    virtual void SetTunnelWeatherBlendEnabled(bool isEnabled) = 0;

    virtual bool IsIgnoreFireStateEnabled() const noexcept = 0;
    virtual void SetIgnoreFireStateEnabled(bool isEnabled) = 0;

    virtual bool IsVehicleBurnExplosionsEnabled() const noexcept = 0;
    virtual void SetVehicleBurnExplosionsEnabled(bool isEnabled) = 0;

    virtual CWeapon*     CreateWeapon() = 0;
    virtual CWeaponStat* CreateWeaponStat(eWeaponType weaponType, eWeaponSkill weaponSkill) = 0;

    virtual void SetWeaponRenderEnabled(bool enabled) = 0;
    virtual bool IsWeaponRenderEnabled() const = 0;

    virtual bool VerifySADataFileNames() = 0;
    virtual bool PerformChecks() = 0;
    virtual int& GetCheckStatus() = 0;

    virtual void SetAsyncLoadingFromScript(bool bScriptEnabled, bool bScriptForced) = 0;
    virtual void SuspendASyncLoading(bool bSuspend, uint uiAutoUnsuspendDelay = 0) = 0;
    virtual bool IsASyncLoadingEnabled(bool bIgnoreSuspend = false) = 0;

    virtual void FlushPendingRestreamIPL() = 0;
    virtual void ResetModelLodDistances() = 0;
    virtual void ResetModelFlags() = 0;
    virtual void ResetAlphaTransparencies() = 0;
    virtual void DisableVSync() = 0;
    virtual void ResetModelTimes() = 0;

    virtual void  OnPedContextChange(CPed* pPedContext) = 0;
    virtual CPed* GetPedContext() = 0;

    virtual void GetShaderReplacementStats(SShaderReplacementStats& outStats) = 0;

    virtual void SetPreWeaponFireHandler(PreWeaponFireHandler* pPreWeaponFireHandler) = 0;
    virtual void SetPostWeaponFireHandler(PostWeaponFireHandler* pPostWeaponFireHandler) = 0;
    virtual void SetTaskSimpleBeHitHandler(TaskSimpleBeHitHandler* pTaskSimpleBeHitHandler) = 0;

    virtual CObjectGroupPhysicalProperties* GetObjectGroupPhysicalProperties(unsigned char ucObjectGroup) = 0;

    virtual uint32_t             GetBaseIDforDFF() = 0;
    virtual uint32_t             GetBaseIDforTXD() = 0;
    virtual uint32_t             GetBaseIDforCOL() = 0;
    virtual uint32_t             GetBaseIDforIPL() = 0;
    virtual uint32_t             GetBaseIDforDAT() = 0;
    virtual uint32_t             GetBaseIDforIFP() = 0;
    virtual uint32_t             GetBaseIDforRRR() = 0;
    virtual uint32_t             GetBaseIDforSCM() = 0;
    virtual uint32_t             GetCountOfAllFileIDs() = 0;
    virtual const SFileIDLayout& GetFileIDLayout() const = 0;
    virtual void*                GetModelInfoArray() const = 0;
    virtual CStreamingInfo*      GetStreamingInfoArray() const = 0;

    virtual void RemoveGameWorld() = 0;
    virtual void RestoreGameWorld() = 0;

    virtual bool SetBuildingPoolSize(size_t size) = 0;

    // GTA's recorded-car player is global engine state rather than a vehicle
    // task. Keep it behind the game interface so client.dll never calls fixed
    // executable addresses or touches the 16-slot native pool directly.
    virtual bool RequestVehicleRecording(int recordingId) = 0;
    virtual bool IsVehicleRecordingLoaded(int recordingId) = 0;
    virtual bool StartVehiclePlayback(CVehicle* vehicle, int recordingId) = 0;
    virtual bool StopVehiclePlayback(CVehicle* vehicle) = 0;
    virtual bool IsVehiclePlaybackActive(CVehicle* vehicle) = 0;
    virtual bool RemoveVehicleRecording(int recordingId) = 0;

    // GTA mission text is backed by one global GXT block plus the native
    // CMessages/CHud queues. Keep fixed executable calls behind game_sa so
    // client resources can use SCM text semantics without depending on SA
    // addresses or retaining pointers into a mission block themselves.
    virtual bool LoadMissionTextBlock(const char* blockName) = 0;
    virtual bool ShowMissionText(const char* key, unsigned int duration, unsigned short flags) = 0;
    virtual bool ShowMissionHelp(const char* key, bool permanent) = 0;
    virtual bool ShowMissionBigText(const char* key, unsigned int duration, unsigned int style, bool hasNumber, int number) = 0;
    virtual void ClearMissionText(const char* key, bool big) = 0;
    virtual void ClearMissionHelp() = 0;

    // Record-driven native-world startup is verified at the last reversible
    // boundary before GTA leaves the frontend. These are append-only ABI
    // additions shared by Core, Client Deathmatch, and Game SA.
    virtual bool VerifyNativeWorldStartupBeforeStartGame() = 0;
    virtual void CancelNativeWorldStartupActivation() = 0;

    // GTA file cutscenes own global camera, streaming, audio and player-safe
    // state. Keep every fixed-address operation behind Game SA and append this
    // surface so existing cross-module vtable slots remain unchanged.
    virtual bool LoadFileCutscene(const char* name) = 0;
    virtual bool IsFileCutsceneActive() const = 0;
    virtual bool IsFileCutsceneLoaded() const = 0;
    virtual bool StartFileCutscene() = 0;
    virtual bool HasFileCutsceneFinished() const = 0;
    virtual bool IsFileCutsceneSkipInputPressed() const = 0;
    virtual bool WasFileCutsceneSkipped() const = 0;
    virtual bool SkipFileCutscene() = 0;
    virtual bool DeleteFileCutscene() = 0;

    // Native-world physical slots belong to the process registrar, not to the
    // MTA logical-model registry or script replacement APIs. Keep this query
    // behind Game SA so client.dll never duplicates activation state.
    virtual bool IsNativeWorldModelIdReserved(uint32_t modelId) const = 0;

    // Position-driven native-city residency must run immediately before GTA's
    // streamer. Keep the generation fence in Game SA while Multiplayer SA and
    // blocking scene loaders provide the exact position.
    virtual void PrepareNativeWorldStreaming(const CVector& position) = 0;

    // Local lifecycle coordination enters a one-way drain fence before any
    // native content teardown. This append-only ABI surface is not exposed to
    // downloaded Lua resources.
    virtual bool BeginNativeWorldDrain() = 0;
    virtual bool IsNativeWorldDrainQuiescent() const = 0;

    // Removes the generation-owned native catalog only after the drain fence
    // has proved that no streamer, entity, or RenderWare object can retain it.
    // This remains a local lifecycle primitive and is not exposed to Lua.
    virtual bool TeardownNativeWorldContent() = 0;

    // Releasing endpoint ownership is a separate boundary from content
    // teardown. Core may cross it only after Client Deathmatch and its
    // resource-owned GTA state have been completely destroyed.
    virtual bool                                  IsNativeWorldContentDetached() const = 0;
    virtual ENativeWorldRuntimeAdmissionReadiness GetNativeWorldRuntimeAdmissionReadiness() const = 0;
    virtual bool                                  ActivateNativeWorldRuntimeSelection(const SNativeWorldStartupSelection& selection, std::string& error) = 0;
    virtual bool ReleaseDetachedNativeWorldSession(const SNativeWorldStartupSelection& expectedSelection, std::string& error) = 0;

    // Ambient traffic uses GTA as a placement/model oracle while keeping
    // entity creation, ownership and cleanup under the multiplayer runtime.
    // Append-only: CGame is shared across the Game SA/Client modules.
    virtual void                            UpdateAmbientPedPopulationModels(const CVector& origin) = 0;
    virtual void                            ResetAmbientPedPopulationModels() = 0;
    virtual EAmbientPedSpawnCandidateResult GetAmbientPedSpawnCandidate(const CVector& origin, SAmbientPedSpawnCandidate& candidate) = 0;
    virtual bool                            GetAmbientPedPopulationProfile(SAmbientPedPopulationProfile& profile) const = 0;
    virtual bool                            ResetAmbientPedPopulationZonesToBootstrap() = 0;
    virtual bool                            SetAmbientPedPopulationZoneState(const char* label, const SAmbientPedPopulationZoneState& state) = 0;
    virtual EAmbientPedSpawnCandidateResult GetAmbientPedSpawnCandidateForPopulation(const CVector& origin, EAmbientPedPopulationSelection selection,
                                                                                     unsigned char gangId, SAmbientPedSpawnCandidate& candidate) = 0;
    virtual EAmbientPedSpawnCandidateResult GetAmbientPedGangGroupCandidate(const CVector& origin, unsigned char gangId, unsigned char maxMembers,
                                                                            SAmbientPedGroupSpawnCandidate& candidate) = 0;
    virtual bool                            AcquireAmbientPedNativeGroup(CPed* const* members, unsigned char count, unsigned int& nativeGroupId) = 0;
    virtual bool                            ReleaseAmbientPedNativeGroup(unsigned int nativeGroupId, CPed* const* members, unsigned char count) = 0;
    virtual bool                            IsAmbientPedNativeGroupActive(unsigned int nativeGroupId, CPed* const* members, unsigned char count) const = 0;
    virtual void                            GetAmbientPedNativeGroupDiagnostic(unsigned int nativeGroupId, CPed* const* members, unsigned char count,
                                                                               SAmbientPedNativeGroupDiagnostic& diagnostic) const = 0;
    // Append-only diagnostic ABI: keep these at the end so enabling telemetry
    // does not move any established CGame virtual slot.
    virtual void SetNativeAIGroupDecisionHandler(NativeAIGroupDecisionHandler* pHandler) = 0;
    virtual bool HasNativeAIGroupDecisionHandler() const noexcept = 0;
    virtual void ReportNativeAIGroupDecision(const SNativeAIGroupDecision& decision) const = 0;
};
