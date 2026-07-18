/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        core/CCore.cpp
 *  PURPOSE:     Base core class
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CNativeWorldAuthorizationStore.h"
#include "SharedUtil.Hash.h"
#include <game/CGame.h>
#include <game/CSettings.h>
#include <Accctrl.h>
#include <Aclapi.h>
#include <filesystem>
#include <fstream>
#include <array>
#include <algorithm>
#include <limits>
#include "Userenv.h"  // This will enable SharedUtil::ExpandEnvString
#define ALLOC_STATS_MODULE_NAME "core"
#include "SharedUtil.hpp"
#include <clocale>
#include "DXHook/CDirect3DHook9.h"
#include "DXHook/CDirect3DHookManager.h"
#include "CTimingCheckpoints.hpp"
#include "CModelCacheManager.h"
#include <SharedUtil.Detours.h>
#include <ServerBrowser/CServerCache.h>
#include "CDiscordRichPresence.h"
#include "CSteamClient.h"
#include "CCrashDumpWriter.h"
#include "FastFailCrashHandler/WerCrashHandler.h"

using SharedUtil::CalcMTASAPath;
using namespace std;

namespace fs = std::filesystem;

// Set to true to enable the freeze watchdog (monitors main thread responsiveness)
// Do NOT enable it unless you run a QA testing cycle (see commit desc: 3e54dcb2742bccf0319b9552b2ed5a2c0a012425)
constexpr bool  bFreezeWatchdogEnabled = false;
constexpr DWORD uiFreezeWatchdogTimeoutSeconds = 30;  // Player won't be patient beyond this; we get no info

static float fTest = 1;

extern CCore* g_pCore;
bool          g_bBoundsChecker = false;
SString       g_strJingleBells;

extern fs::path g_gtaDirectory;

template <>
CCore* CSingleton<CCore>::m_pSingleton = NULL;

static auto                Win32LoadLibraryA = LoadLibraryA;
static constexpr long long TIME_DISCORD_UPDATE_RICH_PRESENCE_RATE = 10000;

static HMODULE WINAPI SkipDirectPlay_LoadLibraryA(LPCSTR fileName)
{
    // GTA:SA expects a valid module handle for DirectPlay. We return a handle for an already loaded library.
    if (!StrCmpIA("dpnhpast.dll", fileName))
        return Win32LoadLibraryA("d3d8.dll");

    if (!StrCmpIA("enbseries\\enbhelper.dll", fileName))
    {
        std::error_code ec;

        // Try to load enbhelper.dll from our custom launch directory first.
        const fs::path inLaunchDir = fs::path{FromUTF8(GetLaunchPath())} / "enbseries" / "enbhelper.dll";

        if (fs::is_regular_file(inLaunchDir, ec))
            return Win32LoadLibraryA(UTF8FilePath(inLaunchDir).c_str());

        // Try to load enbhelper.dll from the GTA install directory second.
        const fs::path inGTADir = g_gtaDirectory / "enbseries" / "enbhelper.dll";

        if (fs::is_regular_file(inGTADir, ec))
            return Win32LoadLibraryA(UTF8FilePath(inGTADir).c_str());

        return nullptr;
    }

    return Win32LoadLibraryA(fileName);
}

namespace
{
    bool ParseClosedNativeWorldEndpoint(const char* arguments, std::array<unsigned char, 4>& ipv4, unsigned short& port)
    {
        constexpr char SCHEME[] = "mtasa://";
        if (!arguments || strncmp(arguments, SCHEME, sizeof(SCHEME) - 1) != 0)
            return false;

        const char* cursor = arguments + sizeof(SCHEME) - 1;
        for (size_t octetIndex = 0; octetIndex < ipv4.size(); ++octetIndex)
        {
            const char*  start = cursor;
            unsigned int value = 0;
            while (*cursor >= '0' && *cursor <= '9')
            {
                value = value * 10 + static_cast<unsigned int>(*cursor++ - '0');
                if (value > 255)
                    return false;
            }
            if (cursor == start || (cursor - start > 1 && start[0] == '0') || (octetIndex + 1 < ipv4.size() && *cursor++ != '.'))
                return false;
            ipv4[octetIndex] = static_cast<unsigned char>(value);
        }
        if (*cursor++ != ':' || *cursor < '1' || *cursor > '9')
            return false;

        const char*  portStart = cursor;
        unsigned int parsedPort = 0;
        while (*cursor >= '0' && *cursor <= '9')
        {
            parsedPort = parsedPort * 10 + static_cast<unsigned int>(*cursor++ - '0');
            if (parsedPort > std::numeric_limits<unsigned short>::max())
                return false;
        }
        if (*cursor || (cursor - portStart > 1 && portStart[0] == '0') || parsedPort == 0 ||
            std::all_of(ipv4.begin(), ipv4.end(), [](unsigned char octet) { return octet == 0; }))
            return false;
        port = static_cast<unsigned short>(parsedPort);
        return true;
    }
}

CCore::CCore()
{
    // Initialize the global pointer
    g_pCore = this;

    m_pConfigFile = NULL;

    // Set our locale to the C locale, except for character handling which is the system's default
    std::setlocale(LC_ALL, "C");
    std::setlocale(LC_CTYPE, "");
    // check LC_COLLATE is the old-time raw ASCII sort order
    assert(strcoll("a", "B") > 0);

    // Parse the command line
    const char* pszNoValOptions[] = {"window", NULL};
    ParseCommandLine(m_CommandLineOptions, m_szCommandLineArgs, pszNoValOptions);

    // Load our settings and localization as early as possible
    CreateXML();
    ApplyCoreInitSettings();
    g_pLocalization = new CLocalization;

    // Create a logger instance.
    m_pConsoleLogger = new CConsoleLogger();

    // Create interaction objects.
    m_pCommands = new CCommands;
    m_pConnectManager = new CConnectManager;

    // Create the GUI manager and the graphics lib wrapper
    m_pLocalGUI = new CLocalGUI;
    m_pGraphics = new CGraphics(m_pLocalGUI);
    g_pGraphics = m_pGraphics;
    m_pGUI = NULL;

    // Create the mod manager
    m_pModManager = new CModManager;

    CCrashDumpWriter::SetHandlers();

    m_pfnMessageProcessor = NULL;
    m_pMessageBox = NULL;

    m_bIsOfflineMod = false;
    m_bQuitOnPulse = false;
    m_bDestroyMessageBox = false;
    m_bCursorToggleControls = false;
    m_bLastFocused = true;
    m_uiNextRenderTargetRetryTime = 0;
    m_DiagnosticDebug = EDiagnosticDebug::NONE;

    // Create our Direct3DData handler.
    m_pDirect3DData = new CDirect3DData;

    WriteDebugEvent("CCore::CCore");

    // Store initial module bases (will be updated more comprehensively later)
    WerCrash::UpdateModuleBases();

    m_pKeyBinds = new CKeyBinds(this);

    m_pMouseControl = new CMouseControl();

    // Create our hook objects.
    m_pDirect3DHookManager = new CDirect3DHookManager();
    m_pDirectInputHookManager = new CDirectInputHookManager();
    m_pMessageLoopHook = new CMessageLoopHook();
    m_pSetCursorPosHook = new CSetCursorPosHook();

    // Register internal commands.
    RegisterCommands();

    // Setup our hooks.
    ApplyHooks();

    m_pModelCacheManager = nullptr;
    m_iDummyProgressValue = 0;
    m_DummyProgressTimerHandle = NULL;
    m_bDummyProgressUpdateAlways = false;

    m_iUnminimizeFrameCounter = 0;
    m_bDidRecreateRenderTargets = false;
    m_fMinStreamingMemory = 0;
    m_fMaxStreamingMemory = 0;
    m_bGettingIdleCallsFromMultiplayer = false;
    m_bWindowsTimerEnabled = false;
    m_timeDiscordAppLastUpdate = 0;

    // Initialize FPS limiter
    m_pFPSLimiter = std::make_unique<FPSLimiter::FPSLimiter>();

    // Create tray icon
    m_pTrayIcon = new CTrayIcon();
    m_steamClient = std::make_unique<CSteamClient>();

    // Create discord rich presence
    m_pDiscordRichPresence = std::shared_ptr<CDiscordRichPresence>(new CDiscordRichPresence());
}

CCore::~CCore()
{
    WriteDebugEvent("CCore::~CCore");
    NativeWorldAuthorizationStore::CancelActiveStartup();

    if constexpr (bFreezeWatchdogEnabled)
        StopWatchdogThread();

    // Reset Discord rich presence
    if (m_pDiscordRichPresence)
        m_pDiscordRichPresence.reset();

    m_steamClient.reset();

    // Destroy tray icon
    delete m_pTrayIcon;

    m_pFPSLimiter.reset();

    // This will set the GTA volume to the GTA volume value in the settings,
    // and is not affected by the master volume setting.
    m_pLocalGUI->GetMainMenu()->GetSettingsWindow()->ResetGTAVolume();

    // Remove input hook
    CMessageLoopHook::GetSingleton().RemoveHook();

    if (m_bWindowsTimerEnabled)
    {
        KillTimer(GetHookedWindow(), IDT_TIMER1);
        m_bWindowsTimerEnabled = false;
    }

    extern int ms_iDummyProgressTimerCounter;

    if (m_DummyProgressTimerHandle != NULL)
    {
        DeleteTimerQueueTimer(NULL, m_DummyProgressTimerHandle, INVALID_HANDLE_VALUE);
        m_DummyProgressTimerHandle = NULL;
        ms_iDummyProgressTimerCounter = 0;
    }

    // Delete the mod manager
    delete m_pModManager;
    SAFE_DELETE(m_pMessageBox);

    SAFE_DELETE(m_pModelCacheManager);

    // Destroy early subsystems
    m_bModulesLoaded = false;
    DestroyNetwork();
    DestroyMultiplayer();
    DestroyGame();

    // Remove global events
    g_pCore->m_pGUI->ClearInputHandlers(INPUT_CORE);

    // Store core variables to cvars
    CVARS_SET("console_pos", m_pLocalGUI->GetConsole()->GetPosition());
    CVARS_SET("console_size", m_pLocalGUI->GetConsole()->GetSize());

    // Delete interaction objects.
    delete m_pCommands;
    delete m_pConnectManager;
    delete m_pDirect3DData;

    // Delete hooks.
    delete m_pSetCursorPosHook;
    delete m_pDirect3DHookManager;
    delete m_pDirectInputHookManager;

    // Delete the GUI manager
    delete m_pLocalGUI;
    delete m_pGraphics;

    // Delete the web
    DestroyWeb();

    // Delete lazy subsystems
    DestroyGUI();
    DestroyXML();

    SAFE_DELETE(g_pLocalization);

    // Delete keybinds
    delete m_pKeyBinds;

    // Delete Mouse Control
    delete m_pMouseControl;

    // Delete the logger
    delete m_pConsoleLogger;

    // Delete last so calls to GetHookedWindowHandle do not crash
    delete m_pMessageLoopHook;
}

eCoreVersion CCore::GetVersion()
{
    return MTACORE_20;
}

CConsoleInterface* CCore::GetConsole()
{
    return m_pLocalGUI->GetConsole();
}

CCommandsInterface* CCore::GetCommands()
{
    return m_pCommands;
}

CGame* CCore::GetGame()
{
    return m_pGame;
}

CGraphicsInterface* CCore::GetGraphics()
{
    return m_pGraphics;
}

CModManagerInterface* CCore::GetModManager()
{
    return m_pModManager;
}

CMultiplayer* CCore::GetMultiplayer()
{
    return m_pMultiplayer;
}

CXMLNode* CCore::GetConfig()
{
    if (!m_pConfigFile)
        return NULL;
    CXMLNode* pRoot = m_pConfigFile->GetRootNode();
    if (!pRoot)
        pRoot = m_pConfigFile->CreateRootNode(CONFIG_ROOT);
    return pRoot;
}

CGUI* CCore::GetGUI()
{
    return m_pGUI;
}

CNet* CCore::GetNetwork()
{
    return m_pNet;
}

void CCore::AdvanceNetworkConnectionGeneration()
{
    // Zero is reserved for snapshots captured before any connection attempt.
    ++m_networkConnectionGeneration;
    if (m_networkConnectionGeneration == 0)
        ++m_networkConnectionGeneration;
}

bool CCore::CaptureNativeWorldStartupAuthorization(unsigned char wireVersion, unsigned char startupMode, unsigned char policy, unsigned char packFormat,
                                                   const std::string& resourceName, unsigned short resourceNetId, unsigned int resourceStartCounter,
                                                   SNativeWorldStartupAuthorization& authorization, std::string& error)
{
    const bool canonicalResourceName = !resourceName.empty() && resourceName.size() <= 64 &&
                                       std::all_of(resourceName.begin(), resourceName.end(),
                                                   [](unsigned char character)
                                                   {
                                                       return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                                                              (character >= '0' && character <= '9') || character == '_' || character == '-' ||
                                                              character == '.';
                                                   });
    if (wireVersion != 1 || startupMode != 1 || policy != 1 || packFormat != 1 || !canonicalResourceName || resourceNetId == 0xFFFF ||
        resourceStartCounter == 0 || m_networkConnectionGeneration == 0 || m_nativeWorldAuthorizationEpoch == 0 || !m_pNet || !m_pNet->IsConnected())
    {
        error = "native-world authorization cannot capture the current session";
        return false;
    }

    const char*       serverIdValue = m_pNet->GetCurrentServerId(false);
    const std::string serverId = serverIdValue ? serverIdValue : "";
    const char*       numericServerValue = m_pNet->GetConnectedServer(false);
    const std::string numericServer = numericServerValue ? numericServerValue : "";
    const char*       numericEndpointValue = m_pNet->GetConnectedServer(true);
    const std::string numericEndpoint = numericEndpointValue ? numericEndpointValue : "";
    if (serverId.size() < 10 || serverId.size() > 4096 || numericServer.empty() || numericEndpoint.empty())
    {
        error = "native-world authorization server identity or endpoint is unavailable";
        return false;
    }

    in_addr             address{};
    const unsigned long addressValue = inet_addr(numericServer.c_str());
    address.s_addr = addressValue;
    const char* canonicalAddress = addressValue == INADDR_NONE || addressValue == INADDR_ANY ? nullptr : inet_ntoa(address);
    if (!canonicalAddress || numericServer != canonicalAddress)
    {
        error = "native-world authorization endpoint is not numeric IPv4";
        return false;
    }
    const std::string endpoint = numericEndpoint;
    const std::string addressText = numericServer;
    if (endpoint.size() <= addressText.size() + 1 || endpoint.compare(0, addressText.size(), addressText) != 0 || endpoint[addressText.size()] != ':')
    {
        error = "native-world authorization endpoint is not canonical";
        return false;
    }
    char*               portEnd = nullptr;
    const char*         portText = endpoint.c_str() + addressText.size() + 1;
    const unsigned long port = strtoul(portText, &portEnd, 10);
    if (!portText[0] || !portEnd || portEnd[0] || port == 0 || port > std::numeric_limits<unsigned short>::max())
    {
        error = "native-world authorization port is invalid";
        return false;
    }

    std::string serverIdentity = "mta-native-world-server-id-v1\n";
    serverIdentity += serverId;
    authorization = {};
    authorization.present = true;
    authorization.wireVersion = wireVersion;
    authorization.startupMode = startupMode;
    authorization.policy = policy;
    authorization.packFormat = packFormat;
    SharedUtil::GenerateSha256(serverIdentity.data(), static_cast<uint>(serverIdentity.size()), authorization.serverIdDigest.data());
    memcpy(authorization.serverIpv4.data(), &address.s_addr, authorization.serverIpv4.size());
    authorization.serverPort = static_cast<unsigned short>(port);
    authorization.resourceNetId = resourceNetId;
    authorization.resourceStartCounter = resourceStartCounter;
    authorization.bitstreamVersion = m_pNet->GetServerBitStreamVersion();
    authorization.connectionGeneration = m_networkConnectionGeneration;
    authorization.authorizationEpoch = m_nativeWorldAuthorizationEpoch;
    authorization.resourceName = resourceName;
    return true;
}

SNativeWorldAuthorizationRecordResult CCore::PersistNativeWorldStartupAuthorization(const SNativeWorldStartupAuthorization&     authorization,
                                                                                    const SNativeWorldAuthorizationPublication& publication)
{
    SNativeWorldAuthorizationRecordResult result;
    SNativeWorldStartupAuthorization      current;
    if (!CaptureNativeWorldStartupAuthorization(authorization.wireVersion, authorization.startupMode, authorization.policy, authorization.packFormat,
                                                authorization.resourceName, authorization.resourceNetId, authorization.resourceStartCounter, current,
                                                result.error))
        return result;
    if (current.serverIdDigest != authorization.serverIdDigest || current.serverIpv4 != authorization.serverIpv4 ||
        current.serverPort != authorization.serverPort || current.bitstreamVersion != authorization.bitstreamVersion ||
        current.connectionGeneration != authorization.connectionGeneration || current.authorizationEpoch != authorization.authorizationEpoch)
    {
        result.error = "native-world authorization session changed before durable publication";
        return result;
    }
    return NativeWorldAuthorizationStore::Persist(authorization, publication);
}

SNativeWorldAuthorizationRecordResult CCore::InspectNativeWorldStartupAuthorization()
{
    if (m_nativeWorldStartupPhase != ENativeWorldStartupPhase::Off)
        return DescribeNativeWorldStartupProcess();
    return NativeWorldAuthorizationStore::Inspect();
}

SNativeWorldAuthorizationRecordResult CCore::ClearNativeWorldStartupAuthorization()
{
    if (m_nativeWorldStartupPhase != ENativeWorldStartupPhase::Off)
    {
        SNativeWorldAuthorizationRecordResult result = DescribeNativeWorldStartupProcess();
        result.success = false;
        result.error = "native-world authorization cannot be cleared while this process owns startup state";
        result.diagnostic += " action=clear-refused";
        return result;
    }
    ++m_nativeWorldAuthorizationEpoch;
    if (m_nativeWorldAuthorizationEpoch == 0)
        ++m_nativeWorldAuthorizationEpoch;
    return NativeWorldAuthorizationStore::Clear();
}

SNativeWorldAuthorizationRecordResult CCore::PrepareNativeWorldStartupRestart()
{
    if (m_nativeWorldStartupPhase != ENativeWorldStartupPhase::Off)
    {
        SNativeWorldAuthorizationRecordResult result = DescribeNativeWorldStartupProcess();
        result.success = false;
        result.error = "native-world restart is unavailable after startup selection";
        result.diagnostic += " action=restart-refused";
        return result;
    }

    NativeWorldAuthorizationStore::SRestartTarget target;
    SNativeWorldAuthorizationRecordResult         result = NativeWorldAuthorizationStore::InspectFreshRestartTarget(target);
    if (!result.success)
        return result;

    const SString existing = GetRegistryValue("", "OnQuitCommand");
    if (!existing.empty() && existing != "\t\t\t\t")
    {
        result.success = false;
        result.diagnostic.clear();
        result.error = "another post-exit action is already scheduled";
        return result;
    }

    const SString endpoint("%u.%u.%u.%u:%u", target.serverIpv4[0], target.serverIpv4[1], target.serverIpv4[2], target.serverIpv4[3], target.serverPort);
    const SString uri("mtasa://%s", endpoint.c_str());
    const SString expected("restart\t\t%s\t\t", uri.c_str());

    // The loader resolves `restart` to the already trusted MTA executable.
    // Use one flushed write and read back the exact five-field command before
    // ending this process; a partial or competing write leaves the ticket
    // pending and must not silently arm a later exit.
    SaveConfig(true);
    SetRegistryValue("", "OnQuitCommand", expected, true);
    const SString observed = GetRegistryValue("", "OnQuitCommand");
    if (observed != expected)
    {
        result.success = false;
        const bool writeAppearsUnchanged = observed == existing;
        const bool writeAppearsPartial = !observed.empty() && observed.length() < expected.length() && expected.substr(0, observed.length()) == observed;
        if (writeAppearsUnchanged || writeAppearsPartial)
        {
            SetRegistryValue("", "OnQuitCommand", existing, true);
            if (GetRegistryValue("", "OnQuitCommand") == existing)
            {
                result.diagnostic.clear();
                result.error = "native-world restart scheduling could not be verified and was disarmed";
                return result;
            }
        }

        // An unrelated value may have won a concurrent write. Preserve it,
        // but make the unresolved loader state explicit instead of claiming
        // that no post-exit action exists.
        result.diagnostic = "state=restart-scheduling-ambiguous activation=no lease=no action=inspect-onquit";
        result.error = "native-world restart scheduling left an ambiguous post-exit action";
        return result;
    }

    result.diagnostic = SString("state=restart-scheduled endpoint=%s ticket=%s activation=no lease=no credential=suppressed", endpoint.c_str(),
                                result.ticketId.substr(0, 8).c_str());
    WriteDebugEvent(SString("[NativeWorldAuthorization] %s", result.diagnostic.c_str()));
    return result;
}

bool CCore::IsNativeWorldStartupCredentialSuppressed() const
{
    // Server identity is revalidated only after Client Deathmatch starts.
    // Suppress every credential until a future protocol can authenticate the
    // second session before any reusable verifier leaves Core.
    return m_nativeWorldStartupPhase == ENativeWorldStartupPhase::Prepared;
}

SNativeWorldAuthorizationRecordResult CCore::DescribeNativeWorldStartupProcess() const
{
    SNativeWorldAuthorizationRecordResult result;
    result.success = true;
    result.found = true;
    result.ticketId = m_nativeWorldStartupSelection.ticketId;
    result.issuedAt = m_nativeWorldStartupSelection.issuedAt;
    result.expiresAt = m_nativeWorldStartupSelection.expiresAt;

    const char* state = "process-terminal";
    const char* activation = "no";
    const char* lease = "released";
    switch (m_nativeWorldStartupPhase)
    {
        case ENativeWorldStartupPhase::Candidate:
            state = "selected";
            lease = "pending";
            break;
        case ENativeWorldStartupPhase::Prepared:
        case ENativeWorldStartupPhase::SessionValidated:
            state = "prepared";
            activation = "prepared";
            lease = "pending";
            break;
        case ENativeWorldStartupPhase::Active:
            state = "active";
            activation = "yes";
            lease = "process";
            break;
        case ENativeWorldStartupPhase::Refused:
            state = "refused";
            break;
        case ENativeWorldStartupPhase::Terminal:
            break;
        case ENativeWorldStartupPhase::Off:
            result.success = false;
            result.found = false;
            result.error = "native-world process state is unavailable";
            return result;
    }

    result.diagnostic = SString("state=%s endpoint=%u.%u.%u.%u:%u ticket=%s issued=%llu expires=%llu activation=%s lease=%s restart-required=no", state,
                                m_nativeWorldStartupSelection.serverIpv4[0], m_nativeWorldStartupSelection.serverIpv4[1],
                                m_nativeWorldStartupSelection.serverIpv4[2], m_nativeWorldStartupSelection.serverIpv4[3],
                                m_nativeWorldStartupSelection.serverPort, m_nativeWorldStartupSelection.ticketId.substr(0, 8).c_str(),
                                m_nativeWorldStartupSelection.issuedAt, m_nativeWorldStartupSelection.expiresAt, activation, lease);
    return result;
}

SNativeWorldAuthorizationRecordResult CCore::RevokeNativeWorldStartupAuthorization(const SNativeWorldStartupAuthorization& authorization,
                                                                                   const std::string&                      contentId)
{
    SNativeWorldAuthorizationRecordResult result;
    SNativeWorldStartupAuthorization      current;
    if (!CaptureNativeWorldStartupAuthorization(authorization.wireVersion, authorization.startupMode, authorization.policy, authorization.packFormat,
                                                authorization.resourceName, authorization.resourceNetId, authorization.resourceStartCounter, current,
                                                result.error))
        return result;
    if (current.serverIdDigest != authorization.serverIdDigest || current.serverIpv4 != authorization.serverIpv4 ||
        current.serverPort != authorization.serverPort || current.bitstreamVersion != authorization.bitstreamVersion ||
        current.connectionGeneration != authorization.connectionGeneration)
    {
        result.error = "native-world authorization session changed before revocation";
        return result;
    }
    return NativeWorldAuthorizationStore::Revoke(authorization, contentId);
}

SNativeWorldStartupSelection CCore::BeginNativeWorldStartupSelection(bool legacySelectorEnabled)
{
    std::array<unsigned char, 4> endpointIpv4{};
    unsigned short               endpointPort = 0;
    const bool                   hasClosedEndpoint = ParseClosedNativeWorldEndpoint(m_szCommandLineArgs, endpointIpv4, endpointPort);
    SNativeWorldStartupSelection selection =
        NativeWorldAuthorizationStore::BeginStartup(hasClosedEndpoint ? &endpointIpv4 : nullptr, endpointPort, legacySelectorEnabled);
    if (selection.ready)
    {
        m_nativeWorldStartupSelection = selection;
        m_nativeWorldStartupPhase = ENativeWorldStartupPhase::Candidate;
    }
    return selection;
}

SNativeWorldAuthorizationRecordResult CCore::FinishNativeWorldStartupSelection(const std::string& ticketId, bool claim, const std::string& refusalReason)
{
    SNativeWorldAuthorizationRecordResult result = NativeWorldAuthorizationStore::FinishStartup(ticketId, claim, refusalReason);
    const bool exactCandidate = m_nativeWorldStartupPhase == ENativeWorldStartupPhase::Candidate && m_nativeWorldStartupSelection.ticketId == ticketId;
    if (claim && result.success && result.claimed && exactCandidate)
    {
        m_nativeWorldStartupPhase = ENativeWorldStartupPhase::Prepared;
        const SNativeWorldStartupSelection& selection = m_nativeWorldStartupSelection;
        WriteDebugEvent(SString("[NativeWorldAuthorization] state=prepared ticket=%s endpoint=%u.%u.%u.%u:%u activation=prepared lease=pending",
                                selection.ticketId.substr(0, 8).c_str(), selection.serverIpv4[0], selection.serverIpv4[1], selection.serverIpv4[2],
                                selection.serverIpv4[3], selection.serverPort));
    }
    else
    {
        m_nativeWorldStartupPhase = ENativeWorldStartupPhase::Off;
        m_nativeWorldStartupSelection = {};
        if (claim && result.success && result.claimed)
        {
            WriteDebugEvent("[NativeWorldAuthorization] state=process-terminal reason=claimed-ticket-candidate-mismatch activation=no exit=0xE057C003");
            TerminateProcess(GetCurrentProcess(), 0xE057C003);
        }
    }
    return result;
}

void CCore::CancelNativeWorldStartupSelection(const std::string& ticketId)
{
    NativeWorldAuthorizationStore::CancelStartup(ticketId);
}

bool CCore::IsNativeWorldStartupSelectionCancelled(const std::string& ticketId) const
{
    return NativeWorldAuthorizationStore::IsStartupCancelled(ticketId);
}

bool CCore::ValidateNativeWorldStartupEndpoint(const std::string& targetHost, const std::array<unsigned char, 4>& endpointIpv4, unsigned short endpointPort,
                                               std::string& error) const
{
    if (m_nativeWorldStartupPhase == ENativeWorldStartupPhase::Off || m_nativeWorldStartupPhase == ENativeWorldStartupPhase::Candidate)
        return true;
    const SString canonicalHost("%u.%u.%u.%u", m_nativeWorldStartupSelection.serverIpv4[0], m_nativeWorldStartupSelection.serverIpv4[1],
                                m_nativeWorldStartupSelection.serverIpv4[2], m_nativeWorldStartupSelection.serverIpv4[3]);
    if (m_nativeWorldStartupPhase == ENativeWorldStartupPhase::Terminal || targetHost != canonicalHost ||
        endpointIpv4 != m_nativeWorldStartupSelection.serverIpv4 || endpointPort != m_nativeWorldStartupSelection.serverPort)
    {
        error = "connection target differs from the process-pinned native-world endpoint";
        return false;
    }
    return true;
}

bool CCore::ValidateNativeWorldStartupSession(std::string& error)
{
    if (m_nativeWorldStartupPhase == ENativeWorldStartupPhase::Off)
        return true;
    if (m_nativeWorldStartupPhase == ENativeWorldStartupPhase::Terminal || !m_pNet || !m_pNet->IsConnected())
    {
        error = "native-world startup session is unavailable";
        return false;
    }

    const char*       serverIdValue = m_pNet->GetCurrentServerId(false);
    const std::string serverId = serverIdValue ? serverIdValue : "";
    const char*       numericServerValue = m_pNet->GetConnectedServer(false);
    const std::string numericServer = numericServerValue ? numericServerValue : "";
    const char*       numericEndpointValue = m_pNet->GetConnectedServer(true);
    const std::string numericEndpoint = numericEndpointValue ? numericEndpointValue : "";
    if (serverId.size() < 10 || serverId.size() > 4096 || numericServer.empty() || numericEndpoint.empty())
    {
        error = "native-world startup server identity or endpoint is unavailable";
        return false;
    }

    in_addr             address{};
    const unsigned long addressValue = inet_addr(numericServer.c_str());
    address.s_addr = addressValue;
    const char* canonicalAddress = addressValue == INADDR_NONE || addressValue == INADDR_ANY ? nullptr : inet_ntoa(address);
    if (!canonicalAddress || numericServer != canonicalAddress)
    {
        error = "native-world startup endpoint is not canonical numeric IPv4";
        return false;
    }
    if (numericEndpoint.size() <= numericServer.size() + 1 || numericEndpoint.compare(0, numericServer.size(), numericServer) != 0 ||
        numericEndpoint[numericServer.size()] != ':')
    {
        error = "native-world startup endpoint text is not canonical";
        return false;
    }
    char*               portEnd = nullptr;
    const char*         portText = numericEndpoint.c_str() + numericServer.size() + 1;
    const unsigned long port = strtoul(portText, &portEnd, 10);
    if (!portText[0] || !portEnd || portEnd[0] || !port || port > std::numeric_limits<unsigned short>::max())
    {
        error = "native-world startup session port is invalid";
        return false;
    }

    std::array<unsigned char, 4> endpointIpv4{};
    memcpy(endpointIpv4.data(), &address.s_addr, endpointIpv4.size());
    std::array<unsigned char, 32> serverIdDigest{};
    std::string                   serverIdentity = "mta-native-world-server-id-v1\n";
    serverIdentity += serverId;
    SharedUtil::GenerateSha256(serverIdentity.data(), static_cast<uint>(serverIdentity.size()), serverIdDigest.data());
    if (serverIdDigest != m_nativeWorldStartupSelection.serverIdDigest || endpointIpv4 != m_nativeWorldStartupSelection.serverIpv4 ||
        port != m_nativeWorldStartupSelection.serverPort || m_pNet->GetServerBitStreamVersion() != m_nativeWorldStartupSelection.bitstreamVersion)
    {
        error = "native-world startup session identity differs from the claimed record";
        return false;
    }

    if (m_nativeWorldStartupPhase == ENativeWorldStartupPhase::Prepared)
    {
        const __time64_t nowValue = _time64(nullptr);
        if (nowValue < 0 || static_cast<unsigned long long>(nowValue) > m_nativeWorldStartupSelection.expiresAt ||
            static_cast<unsigned long long>(nowValue) + 120 < m_nativeWorldStartupSelection.issuedAt ||
            m_nativeWorldStartupSelection.expiresAt - m_nativeWorldStartupSelection.issuedAt != 900)
        {
            error = "native-world startup authorization expired or the clock moved backwards";
            return false;
        }
        m_nativeWorldStartupPhase = ENativeWorldStartupPhase::SessionValidated;
    }

    const char* activation = m_nativeWorldStartupPhase == ENativeWorldStartupPhase::Active    ? "active"
                             : m_nativeWorldStartupPhase == ENativeWorldStartupPhase::Refused ? "refused"
                                                                                              : "prepared";
    const char* lease = m_nativeWorldStartupPhase == ENativeWorldStartupPhase::Active    ? "process"
                        : m_nativeWorldStartupPhase == ENativeWorldStartupPhase::Refused ? "released"
                                                                                         : "pending";
    WriteDebugEvent(SString("[NativeWorldAuthorization] state=session-validated ticket=%s endpoint=%s activation=%s lease=%s",
                            m_nativeWorldStartupSelection.ticketId.substr(0, 8).c_str(), numericEndpoint.c_str(), activation, lease));
    return true;
}

void CCore::MarkNativeWorldStartupActive()
{
    if (m_nativeWorldStartupPhase != ENativeWorldStartupPhase::SessionValidated)
    {
        TerminateNativeWorldStartup("native-world registrar completed outside the validated startup session");
        return;
    }
    m_nativeWorldStartupPhase = ENativeWorldStartupPhase::Active;
    WriteDebugEvent(
        SString("[NativeWorldAuthorization] state=active ticket=%s activation=yes lease=process", m_nativeWorldStartupSelection.ticketId.substr(0, 8).c_str()));
}

void CCore::MarkNativeWorldStartupRefused()
{
    if (m_nativeWorldStartupPhase == ENativeWorldStartupPhase::SessionValidated)
        m_nativeWorldStartupPhase = ENativeWorldStartupPhase::Refused;
}

void CCore::FailNativeWorldStartupBeforeActive(const std::string& reason)
{
    if (m_nativeWorldStartupPhase == ENativeWorldStartupPhase::Prepared || m_nativeWorldStartupPhase == ENativeWorldStartupPhase::SessionValidated)
        TerminateNativeWorldStartup(reason);
}

void CCore::TerminateNativeWorldStartup(const std::string& reason)
{
    if (m_nativeWorldStartupPhase == ENativeWorldStartupPhase::Off || m_nativeWorldStartupPhase == ENativeWorldStartupPhase::Terminal)
        return;

    const std::string ticket = m_nativeWorldStartupSelection.ticketId.substr(0, 8);
    if (m_pGame)
        m_pGame->CancelNativeWorldStartupActivation();
    m_nativeWorldStartupPhase = ENativeWorldStartupPhase::Terminal;
    WriteDebugEvent(SString("[NativeWorldAuthorization] state=process-terminal ticket=%s reason=%s activation=no lease=released exit=0xE057C003",
                            ticket.c_str(), reason.c_str()));
    TerminateProcess(GetCurrentProcess(), 0xE057C003);
}

CKeyBindsInterface* CCore::GetKeyBinds()
{
    return m_pKeyBinds;
}

CLocalGUI* CCore::GetLocalGUI()
{
    return m_pLocalGUI;
}

void CCore::SaveConfig(bool bWaitUntilFinished)
{
    if (m_pConfigFile)
    {
        CXMLNode* pBindsNode = GetConfig()->FindSubNode(CONFIG_NODE_KEYBINDS);
        if (!pBindsNode)
            pBindsNode = GetConfig()->CreateSubNode(CONFIG_NODE_KEYBINDS);
        m_pKeyBinds->SaveToXML(pBindsNode);
        GetVersionUpdater()->SaveConfigToXML();
        m_pConfigFile->Write();
        GetServerCache()->SaveServerCache(bWaitUntilFinished);
    }
}

void CCore::ChatEcho(const char* szText, bool bColorCoded)
{
    CChat* pChat = m_pLocalGUI->GetChat();
    if (pChat)
    {
        CColor color(255, 255, 255, 255);
        pChat->SetTextColor(color);
    }

    // Echo it to the console and chat
    m_pLocalGUI->EchoChat(szText, bColorCoded);
    if (bColorCoded)
    {
        m_pLocalGUI->EchoConsole(RemoveColorCodes(szText));
    }
    else
        m_pLocalGUI->EchoConsole(szText);
}

void CCore::DebugEcho(const char* szText)
{
    CDebugView* pDebugView = m_pLocalGUI->GetDebugView();
    if (pDebugView)
    {
        CColor color(255, 255, 255, 255);
        pDebugView->SetTextColor(color);
    }

    m_pLocalGUI->EchoDebug(szText);
}

void CCore::DebugPrintf(const char* szFormat, ...)
{
    // Convert it to a string buffer
    char    szBuffer[1024];
    va_list ap;
    va_start(ap, szFormat);
    VSNPRINTF(szBuffer, 1024, szFormat, ap);
    va_end(ap);

    DebugEcho(szBuffer);
}

void CCore::SetDebugVisible(bool bVisible)
{
    if (m_pLocalGUI)
    {
        m_pLocalGUI->SetDebugViewVisible(bVisible);
    }
}

bool CCore::IsDebugVisible()
{
    if (m_pLocalGUI)
        return m_pLocalGUI->IsDebugViewVisible();
    else
        return false;
}

void CCore::DebugEchoColor(const char* szText, unsigned char R, unsigned char G, unsigned char B)
{
    // Set the color
    CDebugView* pDebugView = m_pLocalGUI->GetDebugView();
    if (pDebugView)
    {
        CColor color(R, G, B, 255);
        pDebugView->SetTextColor(color);
    }

    m_pLocalGUI->EchoDebug(szText);
}

void CCore::DebugPrintfColor(const char* szFormat, unsigned char R, unsigned char G, unsigned char B, ...)
{
    // Set the color
    if (szFormat)
    {
        // Convert it to a string buffer
        char    szBuffer[1024];
        va_list ap;
        va_start(ap, B);
        VSNPRINTF(szBuffer, 1024, szFormat, ap);
        va_end(ap);

        // Echo it to the console and chat
        DebugEchoColor(szBuffer, R, G, B);
    }
}

void CCore::DebugClear()
{
    CDebugView* pDebugView = m_pLocalGUI->GetDebugView();
    if (pDebugView)
    {
        pDebugView->Clear();
    }
}

void CCore::ChatEchoColor(const char* szText, unsigned char R, unsigned char G, unsigned char B, bool bColorCoded)
{
    // Set the color
    CChat* pChat = m_pLocalGUI->GetChat();
    if (pChat)
    {
        CColor color(R, G, B, 255);
        pChat->SetTextColor(color);
    }

    // Echo it to the console and chat
    m_pLocalGUI->EchoChat(szText, bColorCoded);
    if (bColorCoded)
    {
        m_pLocalGUI->EchoConsole(RemoveColorCodes(szText));
    }
    else
        m_pLocalGUI->EchoConsole(szText);
}

void CCore::ChatPrintf(const char* szFormat, bool bColorCoded, ...)
{
    // Convert it to a string buffer
    char    szBuffer[1024];
    va_list ap;
    va_start(ap, bColorCoded);
    VSNPRINTF(szBuffer, 1024, szFormat, ap);
    va_end(ap);

    // Echo it to the console and chat
    ChatEcho(szBuffer, bColorCoded);
}

void CCore::ChatPrintfColor(const char* szFormat, bool bColorCoded, unsigned char R, unsigned char G, unsigned char B, ...)
{
    // Set the color
    if (szFormat)
    {
        if (m_pLocalGUI)
        {
            // Convert it to a string buffer
            char    szBuffer[1024];
            va_list ap;
            va_start(ap, B);
            VSNPRINTF(szBuffer, 1024, szFormat, ap);
            va_end(ap);

            // Echo it to the console and chat
            ChatEchoColor(szBuffer, R, G, B, bColorCoded);
        }
    }
}

void CCore::SetChatVisible(bool bVisible, bool bInputBlocked)
{
    if (m_pLocalGUI)
    {
        m_pLocalGUI->SetChatBoxVisible(bVisible, bInputBlocked);
    }
}

bool CCore::IsChatVisible()
{
    if (m_pLocalGUI)
    {
        return m_pLocalGUI->IsChatBoxVisible();
    }
    return false;
}

bool CCore::IsChatInputBlocked()
{
    if (m_pLocalGUI)
    {
        return m_pLocalGUI->IsChatBoxInputBlocked();
    }
    return false;
}

bool CCore::ClearChat()
{
    if (m_pLocalGUI)
    {
        CChat* pChat = m_pLocalGUI->GetChat();
        if (pChat)
        {
            pChat->Clear();
            return true;
        }
    }
    return false;
}

void CCore::InitiateScreenShot(bool bIsCameraShot)
{
    CScreenShot::InitiateScreenShot(bIsCameraShot);
}

void CCore::EnableChatInput(char* szCommand, DWORD dwColor)
{
    if (m_pLocalGUI)
    {
        if (m_pGame->GetSystemState() == SystemState::GS_PLAYING_GAME && m_pModManager->IsLoaded() && !IsOfflineMod() && !m_pGame->IsAtMenu() &&
            !m_pLocalGUI->GetMainMenu()->IsVisible() && !m_pLocalGUI->GetConsole()->IsVisible() && !m_pLocalGUI->IsChatBoxInputEnabled())
        {
            CChat* pChat = m_pLocalGUI->GetChat();
            pChat->SetCommand(szCommand);
            m_pLocalGUI->SetChatBoxInputEnabled(true);
        }
    }
}

bool CCore::IsChatInputEnabled()
{
    if (m_pLocalGUI)
    {
        return (m_pLocalGUI->IsChatBoxInputEnabled());
    }

    return false;
}

bool CCore::SetChatboxCharacterLimit(int charLimit)
{
    CChat* pChat = m_pLocalGUI->GetChat();

    if (!pChat)
        return false;

    pChat->SetCharacterLimit(charLimit);
    return true;
}

void CCore::ResetChatboxCharacterLimit()
{
    CChat* pChat = m_pLocalGUI->GetChat();

    if (!pChat)
        return;

    pChat->SetCharacterLimit(pChat->GetDefaultCharacterLimit());
}

int CCore::GetChatboxCharacterLimit()
{
    CChat* pChat = m_pLocalGUI->GetChat();

    if (!pChat)
        return 0;

    return pChat->GetCharacterLimit();
}

int CCore::GetChatboxMaxCharacterLimit()
{
    CChat* pChat = m_pLocalGUI->GetChat();

    if (!pChat)
        return 0;

    return pChat->GetMaxCharacterLimit();
}

bool CCore::IsSettingsVisible()
{
    if (m_pLocalGUI)
    {
        return (m_pLocalGUI->GetMainMenu()->GetSettingsWindow()->IsVisible());
    }

    return false;
}

bool CCore::IsMenuVisible()
{
    if (m_pLocalGUI)
    {
        return (m_pLocalGUI->GetMainMenu()->IsVisible());
    }

    return false;
}

bool CCore::IsCursorForcedVisible()
{
    if (m_pLocalGUI)
    {
        return (m_pLocalGUI->IsCursorForcedVisible());
    }

    return false;
}

void CCore::ApplyConsoleSettings()
{
    CVector2D vec;
    CConsole* pConsole = m_pLocalGUI->GetConsole();

    CVARS_GET("console_pos", vec);
    pConsole->SetPosition(vec);
    CVARS_GET("console_size", vec);
    pConsole->SetSize(vec);
}

void CCore::ApplyGameSettings()
{
    bool                      bVal;
    int                       iVal;
    float                     fVal;
    CControllerConfigManager* pController = m_pGame->GetControllerConfigManager();
    CGameSettings*            pGameSettings = m_pGame->GetSettings();

    CVARS_GET("invert_mouse", bVal);
    pController->SetMouseInverted(bVal);
    CVARS_GET("fly_with_mouse", bVal);
    pController->SetFlyWithMouse(bVal);
    CVARS_GET("steer_with_mouse", bVal);
    pController->SetSteerWithMouse(bVal);
    CVARS_GET("classic_controls", bVal);
    pController->SetClassicControls(bVal);
    CVARS_GET("volumetric_shadows", bVal);
    pGameSettings->SetVolumetricShadowsEnabled(bVal);
    CVARS_GET("aspect_ratio", iVal);
    pGameSettings->SetAspectRatio((eAspectRatio)iVal, CVARS_GET_VALUE<bool>("hud_match_aspect_ratio"));
    CVARS_GET("grass", bVal);
    pGameSettings->SetGrassEnabled(bVal);
    CVARS_GET("heat_haze", bVal);
    m_pMultiplayer->SetHeatHazeEnabled(bVal);
    CVARS_GET("fast_clothes_loading", iVal);
    m_pMultiplayer->SetFastClothesLoading((CMultiplayer::EFastClothesLoading)iVal);
    CVARS_GET("tyre_smoke_enabled", bVal);
    m_pMultiplayer->SetTyreSmokeEnabled(bVal);
    pGameSettings->UpdateFieldOfViewFromSettings();
    pGameSettings->ResetBlurEnabled();
    pGameSettings->ResetVehiclesLODDistance();
    pGameSettings->ResetPedsLODDistance();
    pGameSettings->ResetCoronaReflectionsEnabled();
    CVARS_GET("dynamic_ped_shadows", bVal);
    pGameSettings->SetDynamicPedShadowsEnabled(bVal);
    pController->SetVerticalAimSensitivityRawValue(CVARS_GET_VALUE<float>("vertical_aim_sensitivity"));
    pController->SetVerticalAimSensitivitySameAsHorizontal(CVARS_GET_VALUE<bool>("use_mouse_sensitivity_for_aiming"));
    CVARS_GET("mastervolume", fVal);
    pGameSettings->SetRadioVolume(pGameSettings->GetRadioVolume() * fVal);
    pGameSettings->SetSFXVolume(pGameSettings->GetSFXVolume() * fVal);
}

void CCore::SetConnected(bool bConnected)
{
    m_pLocalGUI->GetMainMenu()->SetIsIngame(bConnected);
    UpdateIsWindowMinimized();  // Force update of stuff

    if (g_pCore->GetCVars()->GetValue("allow_discord_rpc", false))
    {
        const auto discord = g_pCore->GetDiscord();
        if (!discord->IsDiscordRPCEnabled())
            discord->SetDiscordRPCEnabled(true);

        discord->SetPresenceState(bConnected ? _("In-game") : _("Main menu"), false);
        discord->SetPresenceStartTimestamp(0);
        discord->SetPresenceDetails("", false);

        if (bConnected)
            discord->SetPresenceStartTimestamp(time(nullptr));
    }
}

bool CCore::IsConnected()
{
    return m_pLocalGUI->GetMainMenu() && m_pLocalGUI->GetMainMenu()->GetIsIngame();
}

bool CCore::Reconnect(const char* szHost, unsigned short usPort, const char* szPassword, bool bSave)
{
    return m_pConnectManager->Reconnect(szHost, usPort, szPassword, bSave);
}

void CCore::SetOfflineMod(bool bOffline)
{
    m_bIsOfflineMod = bOffline;
}

const char* CCore::GetModInstallRoot(const char* szModName)
{
    m_strModInstallRoot = CalcMTASAPath(PathJoin("mods", szModName));
    return m_strModInstallRoot;
}

void CCore::ForceCursorVisible(bool bVisible, bool bToggleControls)
{
    m_bCursorToggleControls = bToggleControls;
    m_pLocalGUI->ForceCursorVisible(bVisible);
}

void CCore::SetMessageProcessor(pfnProcessMessage pfnMessageProcessor)
{
    m_pfnMessageProcessor = pfnMessageProcessor;
}

void CCore::ShowMessageBox(const char* szTitle, const char* szText, unsigned int uiFlags, GUI_CALLBACK* ResponseHandler)
{
    RemoveMessageBox();

    // Create the message box
    m_pMessageBox = m_pGUI->CreateMessageBox(szTitle, szText, uiFlags);
    if (ResponseHandler)
        m_pMessageBox->SetClickHandler(*ResponseHandler);

    // Make sure it doesn't auto-destroy, or we'll crash if the msgbox had buttons and the user clicks OK
    m_pMessageBox->SetAutoDestroy(false);
}

void CCore::RemoveMessageBox(bool bNextFrame)
{
    if (bNextFrame)
    {
        m_bDestroyMessageBox = true;
    }
    else
    {
        if (m_pMessageBox)
        {
            delete m_pMessageBox;
            m_pMessageBox = NULL;
        }
    }
}

//
// Show message box with possibility of on-line help
//
void CCore::ShowErrorMessageBox(const SString& strTitle, SString strMessage, const SString& strTroubleLink)
{
    if (strTroubleLink.empty())
    {
        CCore::GetSingleton().ShowMessageBox(strTitle, strMessage, MB_BUTTON_OK | MB_ICON_ERROR);
    }
    else
    {
        CQuestionBox* pQuestionBox = CCore::GetSingleton().GetLocalGUI()->GetMainMenu()->GetQuestionWindow();
        pQuestionBox->Reset();
        pQuestionBox->SetTitle(strTitle);
        pQuestionBox->SetMessage(strMessage);
        pQuestionBox->SetOnLineHelpOption(strTroubleLink);
        pQuestionBox->Show();
    }
}

//
// Show message box with possibility of on-line help
//  + with net error code appended to message and trouble link
//
void CCore::ShowNetErrorMessageBox(const SString& strTitle, SString strMessage, SString strTroubleLink, bool bLinkRequiresErrorCode)
{
    uint uiErrorCode = CCore::GetSingleton().GetNetwork()->GetExtendedErrorCode();
    if (uiErrorCode != 0)
    {
        // Do anti-virus check soon
        SetApplicationSettingInt("noav-user-says-skip", 1);
        strMessage += SString(" \nCode: %08X", uiErrorCode);
        if (!strTroubleLink.empty())
            strTroubleLink += SString("&neterrorcode=%08X", uiErrorCode);
    }
    else if (bLinkRequiresErrorCode)
        strTroubleLink = "";  // No link if no error code

    AddReportLog(7100, SString("Core - NetError (%s) (%s)", *strTitle, *strMessage));
    ShowErrorMessageBox(strTitle, strMessage, strTroubleLink);
}

//
// Callback used in CCore::ShowErrorMessageBox
//
void CCore::ErrorMessageBoxCallBack(void* pData, uint uiButton)
{
    CCore::GetSingleton().GetLocalGUI()->GetMainMenu()->GetQuestionWindow()->Reset();

    SString* pstrTroubleLink = (SString*)pData;
    if (uiButton == 1)
    {
        uint uiErrorCode = (uint)pData;
        BrowseToSolution(*pstrTroubleLink, EXIT_GAME_FIRST);
    }
    delete pstrTroubleLink;
}

//
// Check for disk space problems
// Returns false if low disk space, and dialog is being shown
//
bool CCore::CheckDiskSpace(uint uiResourcesPathMinMB, uint uiDataPathMinMB)
{
    SString strDriveWithNoSpace = GetDriveNameWithNotEnoughSpace(uiResourcesPathMinMB, uiDataPathMinMB);
    if (!strDriveWithNoSpace.empty())
    {
        SString strMessage(_("MTA:SA cannot continue because drive %s does not have enough space."), *strDriveWithNoSpace);
        SString strTroubleLink(SString("low-disk-space&drive=%s", *strDriveWithNoSpace.Left(1)));
        g_pCore->ShowErrorMessageBox(_("Fatal error") + _E("CC43"), strMessage, strTroubleLink);
        return false;
    }
    return true;
}

HWND CCore::GetHookedWindow()
{
    return CMessageLoopHook::GetSingleton().GetHookedWindowHandle();
}

void CCore::HideMainMenu()
{
    m_pLocalGUI->GetMainMenu()->SetVisible(false);
}

void CCore::ShowServerInfo(unsigned int WindowType)
{
    RemoveMessageBox();
    CServerInfo::GetSingletonPtr()->Show((eWindowType)WindowType);
}

void CCore::ApplyHooks()
{
    WriteDebugEvent("CCore::ApplyHooks");

    // Create our hooks.
    m_pDirectInputHookManager->ApplyHook();
    // m_pDirect3DHookManager->ApplyHook ( );
    m_pSetCursorPosHook->ApplyHook();

    // Remove useless DirectPlay dependency (dpnhpast.dll) @ 0x745701
    // We have to patch here as multiplayer_sa and game_sa are loaded too late
    DetourLibraryFunction("kernel32.dll", "LoadLibraryA", Win32LoadLibraryA, SkipDirectPlay_LoadLibraryA);
}

bool UsingAltD3DSetup()
{
    static bool bAltStartup = GetApplicationSettingInt("nvhacks", "optimus-alt-startup") ? true : false;
    return bAltStartup;
}

void CCore::ApplyHooks2()
{
    WriteDebugEvent("CCore::ApplyHooks2");
    // Try this one a little later
    if (!UsingAltD3DSetup())
        m_pDirect3DHookManager->ApplyHook();
    else
    {
        // Done a little later to get past the loading time required to decrypt the gta
        // executable into memory...
        if (!CCore::GetSingleton().AreModulesLoaded())
        {
            CCore::GetSingleton().SetModulesLoaded(true);
            CCore::GetSingleton().CreateNetwork();
            CCore::GetSingleton().CreateGame();
            CCore::GetSingleton().CreateMultiplayer();
            CCore::GetSingleton().CreateXML();
            CCore::GetSingleton().CreateGUI();
        }
    }
}

void CCore::ApplyHooks3(bool bEnable)
{
    if (bEnable)
        CDirect3DHook9::GetSingletonPtr()->ApplyHook();
    else
        CDirect3DHook9::GetSingletonPtr()->RemoveHook();
}

void CCore::SetCenterCursor(bool bEnabled)
{
    if (bEnabled)
        m_pSetCursorPosHook->EnableSetCursorPos();
    else
        m_pSetCursorPosHook->DisableSetCursorPos();
}

////////////////////////////////////////////////////////////////////////
//
// LoadModule
//
// Attempt to load a module. Returns if successful.
// On failure, displays message box and terminates the current process.
//
////////////////////////////////////////////////////////////////////////
void LoadModule(CModuleLoader& m_Loader, const SString& strName, const SString& strModuleName)
{
    WriteDebugEvent("Loading " + strName.ToLower());

    // Ensure DllDirectory has not been changed
    SString strDllDirectory = GetSystemDllDirectory();
    if (CalcMTASAPath("mta").CompareI(strDllDirectory) == false)
    {
        AddReportLog(3119, SString("DllDirectory wrong:  DllDirectory:'%s'  Path:'%s'", *strDllDirectory, *CalcMTASAPath("mta")));
        SetDllDirectory(CalcMTASAPath("mta"));
    }

    // Save current directory (shouldn't change anyway)
    SString strSavedCwd = GetSystemCurrentDirectory();

    // Load appropriate compilation-specific library.
#ifdef MTA_DEBUG
    SString strModuleFileName = strModuleName + "_d.dll";
#else
    SString strModuleFileName = strModuleName + ".dll";
#endif
    m_Loader.LoadModule(CalcMTASAPath(PathJoin("mta", strModuleFileName)));

    if (m_Loader.IsOk() == false)
    {
        SString strMessage("Error loading '%s' module!\n%s", *strName, *m_Loader.GetLastErrorMessage());
        SString strType = "module-not-loadable&name=" + strModuleName;

        // Extra message if d3d9.dll exists
        SString strD3dModuleFilename = PathJoin(GetLaunchPath(), "d3d9.dll");
        if (FileExists(strD3dModuleFilename))
        {
            strMessage += "\n\n";
            strMessage += _("TO FIX, REMOVE THIS FILE:") + "\n";
            strMessage += strD3dModuleFilename;
            strType += "&d3d9=1";
        }
        BrowseToSolution(strType, ASK_GO_ONLINE | EXIT_GAME_FIRST, strMessage);
    }
    // Restore current directory
    SetCurrentDirectory(strSavedCwd);

    WriteDebugEvent(strName + " loaded.");
}

////////////////////////////////////////////////////////////////////////
//
// InitModule
//
// Attempt to initialize a loaded module. Returns if successful.
// On failure, displays message box and terminates the current process.
//
////////////////////////////////////////////////////////////////////////
template <class T, class U>
T* InitModule(CModuleLoader& m_Loader, const SString& strName, const SString& strInitializer, U* pObj)
{
    // Save current directory (shouldn't change anyway)
    SString strSavedCwd = GetSystemCurrentDirectory();

    // Get initializer function from DLL.
    typedef T* (*PFNINITIALIZER)(U*);
    PFNINITIALIZER pfnInit = static_cast<PFNINITIALIZER>(m_Loader.GetFunctionPointer(strInitializer));

    if (pfnInit == NULL)
    {
        MessageBoxUTF8(0, SString(_("%s module is incorrect!"), *strName), "Error" + _E("CC40"), MB_OK | MB_ICONERROR | MB_TOPMOST);
        TerminateProcess(GetCurrentProcess(), 1);
    }

    // If we have a valid initializer, call it.
    T* pResult = pfnInit(pObj);

    // Restore current directory
    SetCurrentDirectory(strSavedCwd);

    WriteDebugEvent(strName + " initialized.");
    return pResult;
}

////////////////////////////////////////////////////////////////////////
//
// CreateModule
//
// Attempt to load and initialize a module. Returns if successful.
// On failure, displays message box and terminates the current process.
//
////////////////////////////////////////////////////////////////////////
template <class T, class U>
T* CreateModule(CModuleLoader& m_Loader, const SString& strName, const SString& strModuleName, const SString& strInitializer, U* pObj)
{
    LoadModule(m_Loader, strName, strModuleName);
    return InitModule<T>(m_Loader, strName, strInitializer, pObj);
}

void CCore::CreateGame()
{
    m_pGame = CreateModule<CGame>(m_GameModule, "Game", "game_sa", "GetGameInterface", this);
    if (m_pGame->GetGameVersion() >= VERSION_11)
    {
        BrowseToSolution("downgrade", TERMINATE_PROCESS,
                         "Only GTA:SA version 1.0 is supported!\n\nYou are now being redirected to a page where you can patch your version.");
    }
}

void CCore::CreateMultiplayer()
{
    m_pMultiplayer = CreateModule<CMultiplayer>(m_MultiplayerModule, "Multiplayer", "multiplayer_sa", "InitMultiplayerInterface", this);
    if (m_pMultiplayer)
        m_pMultiplayer->SetIdleHandler(CCore::StaticIdleHandler);
}

void CCore::DeinitGUI()
{
}

void CCore::InitGUI(IDirect3DDevice9* pDevice)
{
    m_pGUI = InitModule<CGUI>(m_GUIModule, "GUI", "InitGUIInterface", pDevice);

    // Apply CPU affinity here (GTA allocates threads on startup, so we have to do it here instead of earlier)
    bool affinity = CVARS_GET_VALUE<bool>("process_cpu_affinity");
    if (!affinity)
        return;

    DWORD_PTR mask;
    DWORD_PTR sys;
    HANDLE    process = GetCurrentProcess();
    BOOL      result = GetProcessAffinityMask(process, &mask, &sys);

    if (result)
        SetProcessAffinityMask(process, mask & ~1);
}

void CCore::CreateGUI()
{
    LoadModule(m_GUIModule, "GUI", "cgui");
}

void CCore::DestroyGUI()
{
    WriteDebugEvent("CCore::DestroyGUI");
    if (m_pGUI)
    {
        m_pGUI = NULL;
    }
    m_GUIModule.UnloadModule();
}

void CCore::CreateNetwork()
{
    m_pNet = CreateModule<CNet>(m_NetModule, "Network", "netc", "InitNetInterface", this);

    // Network module compatibility check
    typedef unsigned long (*PFNCHECKCOMPATIBILITY)(unsigned long, unsigned long*);
    PFNCHECKCOMPATIBILITY pfnCheckCompatibility = static_cast<PFNCHECKCOMPATIBILITY>(m_NetModule.GetFunctionPointer("CheckCompatibility"));
    if (!pfnCheckCompatibility || !pfnCheckCompatibility(MTA_DM_CLIENT_NET_MODULE_VERSION, NULL))
    {
        // net.dll doesn't like our version number
        ulong ulNetModuleVersion = 0;
        pfnCheckCompatibility(1, &ulNetModuleVersion);
        SString strMessage("Network module not compatible! (Expected 0x%x, got 0x%x)", MTA_DM_CLIENT_NET_MODULE_VERSION, ulNetModuleVersion);
#if !defined(MTA_DM_PUBLIC_CONNECTIONS)
        strMessage += "\n\n(Devs: Update source and run win-install-data.bat)";
#endif
        BrowseToSolution("netc-not-compatible", ASK_GO_ONLINE | TERMINATE_PROCESS, strMessage);
    }

    // Set mta version for report log here
    SetApplicationSetting("mta-version-ext", SString("%d.%d.%d-%d.%05d.%d.%03d", MTASA_VERSION_MAJOR, MTASA_VERSION_MINOR, MTASA_VERSION_MAINTENANCE,
                                                     MTASA_VERSION_TYPE, MTASA_VERSION_BUILD, m_pNet->GetNetRev(), m_pNet->GetNetRel()));
    char szSerial[64];
    m_pNet->GetSerial(szSerial, sizeof(szSerial));
    SetApplicationSetting("serial", szSerial);
}

void CCore::CreateXML()
{
    if (!m_pXML)
        m_pXML = CreateModule<CXML>(m_XMLModule, "XML", "xmll", "InitXMLInterface", *CalcMTASAPath("MTA"));

    if (!m_pConfigFile)
    {
        // Load config XML file
        m_pConfigFile = m_pXML->CreateXML(CalcMTASAPath(MTA_CONFIG_PATH));
        if (!m_pConfigFile)
        {
            assert(false);

            if (m_pXML)
            {
                using PFNReleaseXMLInterface = void (*)();
                if (auto pfnRelease = reinterpret_cast<PFNReleaseXMLInterface>(m_XMLModule.GetFunctionPointer("ReleaseXMLInterface")))
                    pfnRelease();
            }

            m_pXML = NULL;
            m_XMLModule.UnloadModule();
            return;
        }

        m_pConfigFile->Parse();
    }

    // Load the keybinds (loads defaults if the subnode doesn't exist)
    if (m_pKeyBinds)
    {
        m_pKeyBinds->LoadFromXML(GetConfig()->FindSubNode(CONFIG_NODE_KEYBINDS));
        m_pKeyBinds->LoadDefaultCommands(false);
    }

    // Load XML-dependant subsystems
    m_ClientVariables.Load();
}

void CCore::DestroyGame()
{
    WriteDebugEvent("CCore::DestroyGame");

    if (m_pGame)
    {
        m_pGame->Terminate();
        m_pGame = NULL;
    }

    m_GameModule.UnloadModule();
}

void CCore::DestroyMultiplayer()
{
    WriteDebugEvent("CCore::DestroyMultiplayer");

    if (m_pMultiplayer)
    {
        m_pMultiplayer = NULL;
    }

    m_MultiplayerModule.UnloadModule();
}

void CCore::DestroyXML()
{
    WriteDebugEvent("CCore::DestroyXML");

    // Save and unload configuration
    if (m_pConfigFile)
    {
        SaveConfig(true);
        delete m_pConfigFile;
        m_pConfigFile = nullptr;
    }

    if (m_pXML)
    {
        using PFNReleaseXMLInterface = void (*)();
        if (auto pfnRelease = reinterpret_cast<PFNReleaseXMLInterface>(m_XMLModule.GetFunctionPointer("ReleaseXMLInterface")))
            pfnRelease();
        m_pXML = NULL;
    }

    m_XMLModule.UnloadModule();
}

void CCore::DestroyNetwork()
{
    WriteDebugEvent("CCore::DestroyNetwork");

    if (m_pNet)
    {
        m_pNet->Shutdown();
        m_pNet = NULL;
    }

    // Skip unload as it can cause exit crashes due to threading issues
    // m_NetModule.UnloadModule();
}

CWebCoreInterface* CCore::GetWebCore()
{
    if (m_pWebCore == nullptr)
    {
        bool gpuEnabled;
        auto cvars = g_pCore->GetCVars();
        cvars->Get("browser_enable_gpu", gpuEnabled);

        m_pWebCore = CreateModule<CWebCoreInterface>(m_WebCoreModule, "CefWeb", "cefweb", "InitWebCoreInterface", this);
        if (!m_pWebCore)
        {
            WriteDebugEvent("CCore::GetWebCore - CreateModule failed");
            return nullptr;
        }

        // Log current working directory
        wchar_t cwdBeforeWebInit[32768]{};
        DWORD   cwdBeforeWebInitLen = GetCurrentDirectoryW(32768, cwdBeforeWebInit);
        if (cwdBeforeWebInitLen > 0)
        {
            WriteDebugEvent(SString("CCore::GetWebCore - CWD before Initialise: %S", cwdBeforeWebInit));
        }

        // Keep m_pWebCore alive even if Initialise() fails
        // CefInitialize() can only be called once per process
        // Deleting and recreating m_pWebCore causes repeated initialization attempts
        // Track initialization state via IsInitialised() instead
        bool bInitSuccess = false;
        try
        {
            bInitSuccess = m_pWebCore->Initialise(gpuEnabled);
        }
        catch (...)
        {
            WriteDebugEvent("CCore::GetWebCore - Initialise threw exception");
            bInitSuccess = false;
        }

        if (!bInitSuccess)
        {
            WriteDebugEvent("CCore::GetWebCore - Initialise failed");
            return nullptr;
        }
    }
    else
    {
        // On subsequent calls, check if initialization succeeded
        if (!m_pWebCore->IsInitialised())
            return nullptr;
    }

    return m_pWebCore;
}

void CCore::DestroyWeb()
{
    WriteDebugEvent("CCore::DestroyWeb");
    SAFE_DELETE(m_pWebCore);
    m_WebCoreModule.UnloadModule();
}

void CCore::UpdateIsWindowMinimized()
{
    m_bIsWindowMinimized = IsIconic(GetHookedWindow()) ? true : false;
    // Update CPU saver for when minimized and not connected
    g_pCore->GetMultiplayer()->SetIsMinimizedAndNotConnected(m_bIsWindowMinimized && !IsConnected());
    g_pCore->GetMultiplayer()->SetMirrorsEnabled(!m_bIsWindowMinimized);

    // Enable timer if not connected at least once
    bool bEnableTimer = !m_bGettingIdleCallsFromMultiplayer;
    if (m_bWindowsTimerEnabled != bEnableTimer)
    {
        m_bWindowsTimerEnabled = bEnableTimer;
        if (bEnableTimer)
            SetTimer(GetHookedWindow(), IDT_TIMER1, 50, (TIMERPROC)NULL);
        else
            KillTimer(GetHookedWindow(), IDT_TIMER1);
    }
}

bool CCore::IsWindowMinimized()
{
    return m_bIsWindowMinimized;
}

void CCore::DoPreFramePulse()
{
    TIMING_CHECKPOINT("+CorePreFrame");

    if constexpr (bFreezeWatchdogEnabled)
        UpdateWatchdogHeartbeat();

    m_pKeyBinds->DoPreFramePulse();

    // Notify the mod manager
    m_pModManager->DoPulsePreFrame();

    m_pLocalGUI->DoPulse();

    CCrashDumpWriter::UpdateCounters();

    TIMING_CHECKPOINT("-CorePreFrame");
}

void CCore::DoPostFramePulse()
{
    TIMING_CHECKPOINT("+CorePostFrame1");
    if (m_bQuitOnPulse)
        Quit();

    if (m_bDestroyMessageBox)
    {
        RemoveMessageBox();
        m_bDestroyMessageBox = false;
    }

    static bool bFirstPulse = true;
    if (bFirstPulse)
    {
        bFirstPulse = false;

        // Validate CVARS
        CClientVariables::GetSingleton().ValidateValues();

        // Apply all settings
        ApplyConsoleSettings();
        ApplyGameSettings();

        // Allow connecting with the local Steam client
        bool allowSteamClient = false;
        CVARS_GET("allow_steam_client", allowSteamClient);
        if (allowSteamClient)
            m_steamClient->Connect();

        m_pGUI->SelectInputHandlers(INPUT_CORE);
    }

    // This is the first frame in the menu?
    if (m_pGame->GetSystemState() == SystemState::GS_FRONTEND)
    {
        if (m_menuFrame < 255)
            ++m_menuFrame;

        if (m_menuFrame == 1)
        {
            WatchDogCompletedSection("L2");  // gta_sa.set seems ok
            WatchDogCompletedSection("L3");  // No hang on startup

            // Start watchdog thread now that initial loading is complete
            if constexpr (bFreezeWatchdogEnabled)
            {
                if (!StartWatchdogThread(GetCurrentThreadId(), uiFreezeWatchdogTimeoutSeconds))
                {
                    WriteDebugEvent("CCore: WARNING - Failed to start watchdog thread");
                }
            }

            // Disable vsync while it's all dark
            m_pGame->DisableVSync();
        }

        if (!m_bCrashDumpEncryptionDone && m_menuFrame >= 5 && m_pNet && m_pNet->IsReady())
        {
            m_bCrashDumpEncryptionDone = true;
            HandleCrashDumpEncryption();
        }

        if (m_menuFrame >= 5 && !m_isNetworkReady && m_pNet->IsReady())
        {
            m_isNetworkReady = true;

            // Parse the command line
            // Does it begin with mtasa://?
            if (m_szCommandLineArgs && strnicmp(m_szCommandLineArgs, "mtasa://", 8) == 0)
            {
                SString strArguments = GetConnectCommandFromURI(m_szCommandLineArgs);
                // Run the connect command
                if (strArguments.length() > 0 && !m_pCommands->Execute(strArguments))
                {
                    ShowMessageBox(_("Error") + _E("CC41"), _("Error executing URL"), MB_BUTTON_OK | MB_ICON_ERROR);
                }
            }
            else
            {
                // We want to load a mod?
                const char* szOptionValue;
                if (szOptionValue = GetCommandLineOption("c"))
                {
                    CCommandFuncs::Connect(szOptionValue);
                }
            }
        }

        if (m_menuFrame >= 75 && m_requestNewNickname && GetLocalGUI()->GetMainMenu()->IsVisible() && !GetLocalGUI()->GetMainMenu()->IsFading() &&
            !GetLocalGUI()->GetMainMenu()->GetQuestionWindow()->IsVisible())
        {
            // Request a new nickname if we're waiting for one
            GetLocalGUI()->GetMainMenu()->GetSettingsWindow()->RequestNewNickname();
            m_requestNewNickname = false;
        }
    }

    if (!IsFocused() && m_bLastFocused)
    {
        // Fix for #4948
        m_pKeyBinds->CallAllGTAControlBinds(CONTROL_BOTH, false);
        m_bLastFocused = false;
    }
    else if (IsFocused() && !m_bLastFocused)
    {
        m_bLastFocused = true;
    }

    GetJoystickManager()->DoPulse();  // Note: This may indirectly call CMessageLoopHook::ProcessMessage
    m_pKeyBinds->DoPostFramePulse();

    if (m_pWebCore)
        m_pWebCore->DoPulse();

    // Notify the mod manager and the connect manager
    TIMING_CHECKPOINT("-CorePostFrame1");
    m_pModManager->DoPulsePostFrame();
    TIMING_CHECKPOINT("+CorePostFrame2");
    GetMemStats()->Draw();
    GetGraphStats()->Draw();
    m_pConnectManager->DoPulse();

    // Update Discord Rich Presence status
    if (const long long ticks = GetTickCount64_(); ticks > m_timeDiscordAppLastUpdate + TIME_DISCORD_UPDATE_RICH_PRESENCE_RATE)
    {
        if (const auto discord = g_pCore->GetDiscord(); discord && discord->IsDiscordRPCEnabled())
        {
            discord->UpdatePresence();
            m_timeDiscordAppLastUpdate = ticks;
#ifdef DISCORD_DISABLE_IO_THREAD
            // Update manually if we're not using the IO thread
            discord->UpdatePresenceConnection();
#endif
        }
    }

    TIMING_CHECKPOINT("-CorePostFrame2");
}

// Called after MOD is unloaded
void CCore::OnModUnload()
{
    FailNativeWorldStartupBeforeActive("Core began returning to the menu before native-world activation");

    // reattach the global event
    m_pGUI->SelectInputHandlers(INPUT_CORE);
    // remove unused events
    m_pGUI->ClearInputHandlers(INPUT_MOD);

    // Ensure all these have been removed
    m_pKeyBinds->RemoveAllFunctions();
    m_pKeyBinds->RemoveAllControlFunctions();

    // Reset client script frame rate limit
    GetFPSLimiter()->SetClientEnforcedFPS(FPSLimits::FPS_UNLIMITED);

    // Clear web whitelist
    if (m_pWebCore)
        m_pWebCore->ResetFilter();

    // Destroy tray icon
    m_pTrayIcon->DestroyTrayIcon();

    // Reset chatbox character limit
    ResetChatboxCharacterLimit();
}

void CCore::RegisterCommands()
{
    // m_pCommands->Add ( "e", CCommandFuncs::Editor );
    // m_pCommands->Add ( "clear", CCommandFuncs::Clear );
    m_pCommands->Add("help", _("this help screen"), CCommandFuncs::Help);
    m_pCommands->Add("exit", _("exits the application"), CCommandFuncs::Exit);
    m_pCommands->Add("quit", _("exits the application"), CCommandFuncs::Exit);
    m_pCommands->Add("ver", _("shows the version"), CCommandFuncs::Ver);
    m_pCommands->Add("time", _("shows the time"), CCommandFuncs::Time);
    m_pCommands->Add("showhud", _("shows the hud"), CCommandFuncs::HUD);
    m_pCommands->Add("binds", _("shows all the binds"), CCommandFuncs::Binds);
    m_pCommands->Add("serial", _("shows your serial"), CCommandFuncs::Serial);

#if 0
    m_pCommands->Add ( "vid",               "changes the video settings (id)",  CCommandFuncs::Vid );
    m_pCommands->Add ( "window",            "enter/leave windowed mode",        CCommandFuncs::Window );
    m_pCommands->Add ( "load",              "loads a mod (name args)",          CCommandFuncs::Load );
    m_pCommands->Add ( "unload",            "unloads a mod (name)",             CCommandFuncs::Unload );
#endif

    m_pCommands->Add("connect", _("connects to a server (host port nick pass)"), CCommandFuncs::Connect);
    m_pCommands->Add("reconnect", _("connects to a previous server"), CCommandFuncs::Reconnect);
    m_pCommands->Add("bind", _("binds a key (key control)"), CCommandFuncs::Bind);
    m_pCommands->Add("unbind", _("unbinds a key (key)"), CCommandFuncs::Unbind);
    m_pCommands->Add("copygtacontrols", _("copies the default gta controls"), CCommandFuncs::CopyGTAControls);
    m_pCommands->Add("screenshot", _("outputs a screenshot"), CCommandFuncs::ScreenShot);
    m_pCommands->Add("saveconfig", _("immediately saves the config"), CCommandFuncs::SaveConfig);

    m_pCommands->Add("cleardebug", _("clears the debug view"), CCommandFuncs::DebugClear);
    m_pCommands->Add("chatscrollup", _("scrolls the chatbox upwards"), CCommandFuncs::ChatScrollUp);
    m_pCommands->Add("chatscrolldown", _("scrolls the chatbox downwards"), CCommandFuncs::ChatScrollDown);
    m_pCommands->Add("debugscrollup", _("scrolls the debug view upwards"), CCommandFuncs::DebugScrollUp);
    m_pCommands->Add("debugscrolldown", _("scrolls the debug view downwards"), CCommandFuncs::DebugScrollDown);

    m_pCommands->Add("test", "", CCommandFuncs::Test);
    m_pCommands->Add("showmemstat", _("shows the memory statistics"), CCommandFuncs::ShowMemStat);
    m_pCommands->Add("showframegraph", _("shows the frame timing graph"), CCommandFuncs::ShowFrameGraph);
    m_pCommands->Add("timingdebug", "enables or disables native timing checkpoints", CCommandFuncs::TimingDebug);
    m_pCommands->Add("nativeworldauth", "inspects, clears, or restarts into an authorized native world", CCommandFuncs::NativeWorldAuthorization);
    m_pCommands->Add("jinglebells", "", CCommandFuncs::JingleBells);
    m_pCommands->Add("fakelag", "", CCommandFuncs::FakeLag);

    m_pCommands->Add("reloadnews", _("for developers: reload news"), CCommandFuncs::ReloadNews);
}

void CCore::SwitchRenderWindow(HWND hWnd, HWND hWndInput)
{
    assert(0);
#if 0
    // Make GTA windowed
    m_pGame->GetSettings()->SetCurrentVideoMode(0);

    // Get the destination window rectangle
    RECT rect;
    GetWindowRect ( hWnd, &rect );

    // Size the GTA window size to the same size as the destination window rectangle
    HWND hDeviceWindow = CDirect3DData::GetSingleton ().GetDeviceWindow ();
    MoveWindow ( hDeviceWindow,
                 0,
                 0,
                 rect.right - rect.left,
                 rect.bottom - rect.top,
                 TRUE );

    // Turn the GTA window into a child window of our static render container window
    SetParent ( hDeviceWindow, hWnd );
    SetWindowLong ( hDeviceWindow, GWL_STYLE, WS_VISIBLE | WS_CHILD );
#endif
}

bool CCore::IsValidNick(const char* szNick)
{
    // Grab the size of the nick. Check that it's within the player
    size_t sizeNick = strlen(szNick);
    if (sizeNick < MIN_PLAYER_NICK_LENGTH || sizeNick > MAX_PLAYER_NICK_LENGTH)
    {
        return false;
    }

    // Check that each character is valid (visible characters exluding space)
    unsigned char ucTemp;
    for (size_t i = 0; i < sizeNick; i++)
    {
        ucTemp = szNick[i];
        if (ucTemp < 33 || ucTemp > 126)
        {
            return false;
        }
    }

    // Nickname is valid, return true
    return true;
}

void CCore::Quit(bool bInstantly)
{
    NativeWorldAuthorizationStore::CancelActiveStartup();
    if (bInstantly)
    {
        AddReportLog(7101, "Core - Quit");
        // Show that we are quiting (for the crash dump filename)
        SetApplicationSettingInt("last-server-ip", 1);

        WatchDogBeginSection("Q0");  // Allow loader to detect freeze on exit

        // Hide game window to make quit look instant
        PostQuitMessage(0);
        ShowWindow(GetHookedWindow(), SW_HIDE);

        // Destroy the client
        CModManager::GetSingleton().Unload();

        WatchDogCompletedSection("Q0");

        // Use TerminateProcess before destroying CCore to ensure clean exit code (Exiting the normal way also crashes).
        TerminateProcess(GetCurrentProcess(), 0);

        // Destroy ourself (unreachable but kept for completeness)
        delete CCore::GetSingletonPtr();
    }
    else
    {
        m_bQuitOnPulse = true;
    }
}

bool CCore::WasLaunchedWithConnectURI()
{
    if (m_szCommandLineArgs && strnicmp(m_szCommandLineArgs, "mtasa://", 8) == 0)
        return true;
    return false;
}

void CCore::ParseCommandLine(std::map<std::string, std::string>& options, const char*& szArgs, const char** pszNoValOptions)
{
    std::set<std::string> noValOptions;
    if (pszNoValOptions)
    {
        while (*pszNoValOptions)
        {
            noValOptions.insert(*pszNoValOptions);
            pszNoValOptions++;
        }
    }

    const char* szCmdLine = GetCommandLine();

    // Skip the leading game executable path (starts and ends with a quotation mark).
    if (szCmdLine[0] == '"')
    {
        if (const char* afterPath = strchr(szCmdLine + 1, '"'); afterPath != nullptr)
        {
            ++afterPath;

            while (*afterPath && isspace(*afterPath))
                ++afterPath;

            szCmdLine = afterPath;
        }
    }

    char szCmdLineCopy[512];
    STRNCPY(szCmdLineCopy, szCmdLine, sizeof(szCmdLineCopy));

    char*       pCmdLineEnd = szCmdLineCopy + strlen(szCmdLineCopy);
    char*       pStart = szCmdLineCopy;
    char*       pEnd = pStart;
    bool        bInQuoted = false;
    std::string strKey;
    szArgs = NULL;

    while (pEnd != pCmdLineEnd)
    {
        pEnd = strchr(pEnd + 1, ' ');
        if (!pEnd)
            pEnd = pCmdLineEnd;
        if (bInQuoted && *(pEnd - 1) == '"')
            bInQuoted = false;
        else if (*pStart == '"')
            bInQuoted = true;

        if (!bInQuoted)
        {
            *pEnd = 0;
            if (strKey.empty())
            {
                if (*pStart == '-')
                {
                    strKey = pStart + 1;
                    if (noValOptions.find(strKey) != noValOptions.end())
                    {
                        options[strKey] = "";
                        strKey.clear();
                    }
                }
                else
                {
                    szArgs = pStart - szCmdLineCopy + szCmdLine;
                    break;
                }
            }
            else
            {
                if (*pStart == '-')
                {
                    options[strKey] = "";
                    strKey = pStart + 1;
                }
                else
                {
                    if (*pStart == '"')
                        pStart++;
                    if (*(pEnd - 1) == '"')
                        *(pEnd - 1) = 0;
                    options[strKey] = pStart;
                    strKey.clear();
                }
            }
            pStart = pEnd;
            while (pStart != pCmdLineEnd && *(++pStart) == ' ')
                ;
            pEnd = pStart;
        }
    }
}

const char* CCore::GetCommandLineOption(const char* szOption)
{
    std::map<std::string, std::string>::iterator it = m_CommandLineOptions.find(szOption);
    if (it != m_CommandLineOptions.end())
        return it->second.c_str();
    else
        return NULL;
}

SString CCore::GetConnectCommandFromURI(const char* szURI)
{
    unsigned short usPort;
    std::string    strHost, strNick, strPassword;
    GetConnectParametersFromURI(szURI, strHost, usPort, strNick, strPassword);

    // Generate a string with the arguments to send to the mod IF we got a host
    SString strDest;
    if (strHost.size() > 0)
    {
        if (strPassword.size() > 0)
            strDest.Format("connect %s %u %s %s", strHost.c_str(), usPort, strNick.c_str(), strPassword.c_str());
        else
            strDest.Format("connect %s %u %s", strHost.c_str(), usPort, strNick.c_str());
    }

    return strDest;
}

void CCore::GetConnectParametersFromURI(const char* szURI, std::string& strHost, unsigned short& usPort, std::string& strNick, std::string& strPassword)
{
    // Grab the length of the string
    size_t sizeURI = strlen(szURI);

    // Parse it right to left
    char szLeft[256];
    szLeft[255] = 0;
    char* szLeftIter = szLeft + 255;

    char szRight[256];
    szRight[255] = 0;
    char* szRightIter = szRight + 255;

    const char* szIterator = szURI + sizeURI;
    bool        bHitAt = false;

    for (; szIterator >= szURI + 8; szIterator--)
    {
        if (!bHitAt && *szIterator == '@')
        {
            bHitAt = true;
        }
        else
        {
            if (bHitAt)
            {
                if (szLeftIter > szLeft)
                {
                    *(--szLeftIter) = *szIterator;
                }
            }
            else
            {
                if (szRightIter > szRight)
                {
                    *(--szRightIter) = *szIterator;
                }
            }
        }
    }

    // Parse the host/port
    char  szHost[64];
    char  szPort[12];
    char* szHostIter = szHost;
    char* szPortIter = szPort;
    memset(szHost, 0, sizeof(szHost));
    memset(szPort, 0, sizeof(szPort));

    bool   bIsInPort = false;
    size_t sizeRight = strlen(szRightIter);
    for (size_t i = 0; i < sizeRight; i++)
    {
        if (!bIsInPort && szRightIter[i] == ':')
        {
            bIsInPort = true;
        }
        else
        {
            if (bIsInPort)
            {
                if (szPortIter < szPort + 11)
                {
                    *(szPortIter++) = szRightIter[i];
                }
            }
            else
            {
                if (szHostIter < szHost + 63)
                {
                    *(szHostIter++) = szRightIter[i];
                }
            }
        }
    }

    // Parse the nickname / password
    char  szNickname[64];
    char  szPassword[64];
    char* szNicknameIter = szNickname;
    char* szPasswordIter = szPassword;
    memset(szNickname, 0, sizeof(szNickname));
    memset(szPassword, 0, sizeof(szPassword));

    bool   bIsInPassword = false;
    size_t sizeLeft = strlen(szLeftIter);
    for (size_t i = 0; i < sizeLeft; i++)
    {
        if (!bIsInPassword && szLeftIter[i] == ':')
        {
            bIsInPassword = true;
        }
        else
        {
            if (bIsInPassword)
            {
                if (szPasswordIter < szPassword + 63)
                {
                    *(szPasswordIter++) = szLeftIter[i];
                }
            }
            else
            {
                if (szNicknameIter < szNickname + 63)
                {
                    *(szNicknameIter++) = szLeftIter[i];
                }
            }
        }
    }

    // If we got any port, convert it to an integral type
    usPort = 22003;
    if (strlen(szPort) > 0)
    {
        usPort = static_cast<unsigned short>(atoi(szPort));
    }

    // Grab the nickname
    if (strlen(szNickname) > 0)
    {
        strNick = szNickname;
    }
    else
    {
        CVARS_GET("nick", strNick);
    }
    strHost = szHost;
    strPassword = szPassword;
}

void CCore::UpdateRecentlyPlayed()
{
    // Get the current host and port
    unsigned int uiPort;
    std::string  strHost;
    CVARS_GET("host", strHost);
    CVARS_GET("port", uiPort);

    if (uiPort == 0 || uiPort > 0xFFFF)
        return;

    const ushort usPort = static_cast<ushort>(uiPort);
    // Save the connection details into the recently played servers list
    in_addr Address;
    if (CServerListItem::Parse(strHost.c_str(), Address))
    {
        CServerBrowser* pServerBrowser = CCore::GetSingleton().GetLocalGUI()->GetMainMenu()->GetServerBrowser();
        CServerList*    pRecentList = pServerBrowser->GetRecentList();
        pRecentList->Remove(Address, usPort);
        pRecentList->AddUnique(Address, usPort, true);

        pServerBrowser->SaveRecentlyPlayedList();
        if (!m_pConnectManager->m_strLastPassword.empty())
            pServerBrowser->SetServerPassword(strHost + ":" + SString("%u", usPort), m_pConnectManager->m_strLastPassword);
    }
    // Save our configuration file
    CCore::GetSingleton().SaveConfig();
}

void CCore::OnPostColorFilterRender()
{
    if (!CGraphics::GetSingleton().HasLine3DPostFXQueueItems() && !CGraphics::GetSingleton().HasPrimitive3DPostFXQueueItems())
        return;

    CGraphics::GetSingleton().EnteringMTARenderZone();

    CGraphics::GetSingleton().DrawPrimitive3DPostFXQueue();
    CGraphics::GetSingleton().DrawLine3DPostFXQueue();

    CGraphics::GetSingleton().LeavingMTARenderZone();
}

void CCore::ApplyCoreInitSettings()
{
    bool aware = CVARS_GET_VALUE<bool>("process_dpi_aware");

    // The minimum supported client for the function below is Windows Vista (Longhorn).
    // For more information, refer to the Microsoft Learn article:
    // https://learn.microsoft.com/en-us/windows/win32/hidpi/high-dpi-desktop-application-development-on-windows
    if (aware)
        SetProcessDPIAware();

    int revision = GetApplicationSettingInt("reset-settings-revision");

    // Users with the default skin will be switched to the 2023 version by replacing "Default" with "Default 2023".
    // The "Default 2023" GUI skin was introduced in commit 2d9e03324b07e355031ecb3263477477f1a91399.
    if (revision && revision < 21486)
    {
        auto skin = CVARS_GET_VALUE<std::string>("current_skin");

        if (skin == "Default")
            CVARS_SET("current_skin", "Default 2023");

        SetApplicationSettingInt("reset-settings-revision", 21486);
    }

    HANDLE    process = GetCurrentProcess();
    const int priorities[] = {NORMAL_PRIORITY_CLASS, ABOVE_NORMAL_PRIORITY_CLASS, HIGH_PRIORITY_CLASS};
    int       priority = CVARS_GET_VALUE<int>("process_priority") % 3;

    SetPriorityClass(process, priorities[priority]);
}

//
// Called just before GTA calculates frame time deltas
//
void CCore::OnGameTimerUpdate()
{
    // NOTE: (pxd) We are handling the frame limiting updates
    // earlier in the callpath (CModManager::DoPulsePreFrame, CModManager::DoPulsePostFrame)
}

void CCore::OnFPSLimitChange(std::uint16_t fps)
{
    if (m_pNet != nullptr && IsWebCoreLoaded())  // We have to wait for the network module to be loaded
        GetWebCore()->OnFPSLimitChange(fps);
}

//
// DoReliablePulse
//
// This is called once a frame even if minimized
//
void CCore::DoReliablePulse()
{
    TIMING_CHECKPOINT("+CallIdle2");

    UpdateIsWindowMinimized();

    // Non frame rate limit stuff
    if (IsWindowMinimized())
        m_iUnminimizeFrameCounter = 4;  // Tell script we have unminimized after a short delay

    UpdateModuleTickCount64();
}

//
// Debug timings
//
bool CCore::IsTimingCheckpoints()
{
    return ms_TimingCheckpoints.IsTimingCheckpoints();
}

void CCore::OnTimingCheckpoint(const char* szTag)
{
    ms_TimingCheckpoints.OnTimingCheckpoint(szTag);
    // D3D emits one empty checkpoint after Present as the canonical frame
    // boundary. Finish the frame that just rendered, then begin the next one
    // here so GTA simulation is included before the following Present.
    if (!szTag || !szTag[0])
    {
        ms_TimingCheckpoints.EndTimingCheckpoints();
        ms_TimingCheckpoints.BeginTimingCheckpoints();
    }
}

void CCore::OnTimingDetail(const char* szTag)
{
    ms_TimingCheckpoints.OnTimingDetail(szTag);
}

//
// OnDeviceRestore
//
void CCore::OnDeviceRestore()
{
    m_iUnminimizeFrameCounter = 4;  // Tell script we have restored after 4 frames to avoid double sends
    m_bDidRecreateRenderTargets = true;
    m_uiNextRenderTargetRetryTime = 0;
}

//
// OnPreFxRender
//
void CCore::OnPreFxRender()
{
    if (!CGraphics::GetSingleton().HasLine3DPreGUIQueueItems() && !CGraphics::GetSingleton().HasPrimitive3DPreGUIQueueItems())
        return;

    CGraphics::GetSingleton().EnteringMTARenderZone();

    CGraphics::GetSingleton().DrawPrimitive3DPreGUIQueue();
    CGraphics::GetSingleton().DrawLine3DPreGUIQueue();

    CGraphics::GetSingleton().LeavingMTARenderZone();
}

//
// OnPreHUDRender
//
void CCore::OnPreHUDRender()
{
    const uint uiNow = GetTickCount32();
    if (uiNow >= m_uiNextRenderTargetRetryTime)
    {
        CGraphics::GetSingleton().RetryInvalidRenderTargets();
        m_uiNextRenderTargetRetryTime = uiNow + 250;
    }

    CGraphics::GetSingleton().EnteringMTARenderZone();

    // Maybe capture screen and other stuff
    CGraphics::GetSingleton().GetRenderItemManager()->DoPulse();

    // Handle script stuffs
    if (m_iUnminimizeFrameCounter && --m_iUnminimizeFrameCounter == 0)
    {
        m_pModManager->DoPulsePreHUDRender(true, m_bDidRecreateRenderTargets);
        m_bDidRecreateRenderTargets = false;
    }
    else
        m_pModManager->DoPulsePreHUDRender(false, false);

    // Handle saving depth buffer
    CGraphics::GetSingleton().GetRenderItemManager()->SaveReadableDepthBuffer();

    // Restore in case script forgets
    CGraphics::GetSingleton().GetRenderItemManager()->RestoreDefaultRenderTarget();

    // Draw pre-GUI primitives
    CGraphics::GetSingleton().DrawPreGUIQueue();

    CGraphics::GetSingleton().LeavingMTARenderZone();
}

//
// CCore::CalculateStreamingMemoryRange
//
// Streaming memory range based on system installed memory:
//
//     System RAM MB     min     max
//           512     =   64      96
//          1024     =   96     128
//          2048     =  128     256
//
// Also:
//   Max should be no more than 2 * installed video memory
//   Min should be no more than 1 * installed video memory
//   Max should be no less than 96MB
//   Gap between min and max should be no less than 32MB
//
void CCore::CalculateStreamingMemoryRange()
{
    // Only need to do this once
    if (m_fMinStreamingMemory != 0)
        return;

    // Get system info
    int iSystemRamMB = static_cast<int>(GetWMITotalPhysicalMemory() / 1024LL / 1024LL);
    int iVideoMemoryMB = g_pDeviceState->AdapterState.InstalledMemoryKB / 1024;

    // Calc min and max from lookup table
    SSamplePoint<float> minPoints[] = {{512, 64}, {1024, 96}, {2048, 128}};
    SSamplePoint<float> maxPoints[] = {{512, 96}, {1024, 128}, {2048, 256}};

    float fMinAmount = EvalSamplePosition<float>(minPoints, NUMELMS(minPoints), iSystemRamMB);
    float fMaxAmount = EvalSamplePosition<float>(maxPoints, NUMELMS(maxPoints), iSystemRamMB);

    // Scale max if gta3.img is over 1GB
    SString strGta3imgFilename = PathJoin(GetLaunchPath(), "models", "gta3.img");
    uint    uiFileSizeMB = FileSize(strGta3imgFilename) / 0x100000LL;
    float   fSizeScale = UnlerpClamped(1024, uiFileSizeMB, 2048);
    fMaxAmount += fMaxAmount * fSizeScale;

    // Apply cap dependant on video memory
    fMaxAmount = std::min(fMaxAmount, iVideoMemoryMB * 2.f);
    fMinAmount = std::min(fMinAmount, iVideoMemoryMB * 1.f);

    // Apply 96MB lower limit
    fMaxAmount = std::max(fMaxAmount, 96.f);

    // Apply gap limit
    fMinAmount = fMaxAmount - std::max(fMaxAmount - fMinAmount, 32.f);

    m_fMinStreamingMemory = fMinAmount;
    m_fMaxStreamingMemory = fMaxAmount;
}

//
// GetMinStreamingMemory
//
uint CCore::GetMinStreamingMemory()
{
    CalculateStreamingMemoryRange();

#ifdef MTA_DEBUG
    return 1;
#endif
    return m_fMinStreamingMemory;
}

//
// GetMaxStreamingMemory
//
uint CCore::GetMaxStreamingMemory()
{
    CalculateStreamingMemoryRange();
    return m_fMaxStreamingMemory;
}

//
// OnCrashAverted
//
void CCore::OnCrashAverted(uint uiId)
{
    CCrashDumpWriter::OnCrashAverted(uiId);
}

//
// OnEnterCrashZone
//
void CCore::OnEnterCrashZone(uint uiId)
{
    CCrashDumpWriter::OnEnterCrashZone(uiId);
}

void CCore::UpdateWerCrashModuleBases()
{
    WerCrash::UpdateModuleBases();
}

//
// LogEvent
//
void CCore::LogEvent(uint uiDebugId, const char* szType, const char* szContext, const char* szBody, uint uiAddReportLogId)
{
    if (uiAddReportLogId)
        AddReportLog(uiAddReportLogId, SString("%s - %s", szContext, szBody));

    if (GetDebugIdEnabled(uiDebugId))
    {
        CCrashDumpWriter::LogEvent(szType, szContext, szBody);
        OutputDebugLine(SString("[LogEvent] %d %s %s %s", uiDebugId, szType, szContext, szBody));
    }
}

//
// GetDebugIdEnabled
//
bool CCore::GetDebugIdEnabled(uint uiDebugId)
{
    static CFilterMap debugIdFilterMap(GetVersionUpdater()->GetDebugFilterString());
    return (uiDebugId == 0) || !debugIdFilterMap.IsFiltered(uiDebugId);
}

EDiagnosticDebugType CCore::GetDiagnosticDebug()
{
    return m_DiagnosticDebug;
}

void CCore::SetDiagnosticDebug(EDiagnosticDebugType value)
{
    m_DiagnosticDebug = value;
}

CModelCacheManager* CCore::GetModelCacheManager()
{
    if (!m_pModelCacheManager)
        m_pModelCacheManager = NewModelCacheManager();
    return m_pModelCacheManager;
}

void CCore::StaticIdleHandler()
{
    g_pCore->IdleHandler();
}

// Gets called every game loop, after GTA has been loaded for the first time
void CCore::IdleHandler()
{
    m_bGettingIdleCallsFromMultiplayer = true;
    HandleIdlePulse();
}

// Gets called every 50ms, before GTA has been loaded for the first time
void CCore::WindowsTimerHandler()
{
    if (!m_bGettingIdleCallsFromMultiplayer)
        HandleIdlePulse();
}

// Always called, even if minimized
void CCore::HandleIdlePulse()
{
    UpdateIsWindowMinimized();

    if (IsWindowMinimized())
    {
        DoPreFramePulse();
        DoPostFramePulse();
    }

    if (m_pModManager->IsLoaded())
        m_pModManager->GetClient()->IdleHandler();
}

namespace
{
    bool IsCoreDump(const SString& filePath)
    {
        constexpr std::array<std::uint32_t, 4> markers = {
            0x734C4F50,  // 'POLs'
            0x73443344,  // 'D3Ds'
            0x73474F4C,  // 'LOGs'
            0x73524557   // 'WERs' - WER fail-fast crash info
        };

        constexpr std::size_t tailSize = 64 * 1024;
        constexpr std::size_t minFileSize = 1024;

        std::ifstream file(FromUTF8(filePath), std::ios::binary | std::ios::ate);
        if (!file)
            return false;

        const auto fileSize = file.tellg();
        if (fileSize < static_cast<std::streamoff>(minFileSize))
            return false;

        const auto readSize = std::min(static_cast<std::size_t>(fileSize), tailSize);
        const auto readStart = static_cast<std::streamoff>(fileSize) - static_cast<std::streamoff>(readSize);

        file.seekg(readStart);
        std::vector<char> buffer(readSize);
        file.read(buffer.data(), static_cast<std::streamsize>(readSize));

        const auto bytesRead = static_cast<std::size_t>(file.gcount());
        if (bytesRead < sizeof(std::uint32_t))
            return false;

        for (std::size_t i = 0; i + sizeof(std::uint32_t) <= bytesRead; ++i)
        {
            std::uint32_t value;
            std::memcpy(&value, buffer.data() + i, sizeof(std::uint32_t));

            if (std::find(markers.begin(), markers.end(), value) != markers.end())
                return true;
        }

        return false;
    }
}

//
// Handle encryption of Windows crash dump files
//
void CCore::HandleCrashDumpEncryption()
{
    const int iMaxFiles = 10;
    SString   strDumpDirPath = CalcMTASAPath("mta\\dumps");
    SString   strDumpDirPrivatePath = PathJoin(strDumpDirPath, "private");
    SString   strDumpDirPublicPath = PathJoin(strDumpDirPath, "public");
    MakeSureDirExists(strDumpDirPrivatePath + "/");
    MakeSureDirExists(strDumpDirPublicPath + "/");

    SString strMessage = "Dump files in this directory are encrypted and copied to 'dumps\\public' during startup\n\n";
    FileSave(PathJoin(strDumpDirPrivatePath, "README.txt"), strMessage);

    // Limit number of files in the private folder
    {
        std::vector<SString> privateList = FindFiles(PathJoin(strDumpDirPrivatePath, "*.dmp"), true, false, true);
        for (int i = 0; i < (int)privateList.size() - iMaxFiles; i++)
            FileDelete(PathJoin(strDumpDirPrivatePath, privateList[i]));
    }

    // Copy and encrypt private files to public if they don't already exist
    {
        std::vector<SString> privateList = FindFiles(PathJoin(strDumpDirPrivatePath, "*.dmp"), true, false);
        for (uint i = 0; i < privateList.size(); i++)
        {
            const SString& strPrivateFilename = privateList[i];
            SString        strPublicFilename = ExtractBeforeExtension(strPrivateFilename) + ".rsa." + ExtractExtension(strPrivateFilename);
            SString        strPrivatePathFilename = PathJoin(strDumpDirPrivatePath, strPrivateFilename);
            SString        strPublicPathFilename = PathJoin(strDumpDirPublicPath, strPublicFilename);
            if (!FileExists(strPublicPathFilename))
            {
                if (!IsCoreDump(strPrivatePathFilename))
                    continue;

                if (CNet* pNet = GetNetwork(); pNet && pNet->IsReady())
                {
                    pNet->EncryptDumpfile(strPrivatePathFilename, strPublicPathFilename);
                }
            }
        }
    }

    // Limit number of files in the public folder
    {
        std::vector<SString> publicList = FindFiles(PathJoin(strDumpDirPublicPath, "*.dmp"), true, false, true);
        for (int i = 0; i < (int)publicList.size() - iMaxFiles; i++)
            FileDelete(PathJoin(strDumpDirPublicPath, publicList[i]));
    }

    // And while we are here, limit number of items in core.log as well
    {
        SString strCoreLogPathFilename = CalcMTASAPath("mta\\core.log");
        SString strFileContents;
        FileLoad(strCoreLogPathFilename, strFileContents);

        SString              strDelmiter = "** -- Unhandled exception -- **";
        std::vector<SString> parts;
        strFileContents.Split(strDelmiter, parts);

        if (parts.size() > iMaxFiles)
        {
            strFileContents = strDelmiter + strFileContents.Join(strDelmiter, parts, parts.size() - iMaxFiles);
            FileSave(strCoreLogPathFilename, strFileContents);
        }
    }
}

//
// Flag to make sure stuff only gets done when everything is ready
//
void CCore::SetModulesLoaded(bool bLoaded)
{
    m_bModulesLoaded = bLoaded;
}

bool CCore::AreModulesLoaded()
{
    return m_bModulesLoaded;
}

//
// Handle dummy progress when game seems stalled
//
int ms_iDummyProgressTimerCounter = 0;

void CALLBACK TimerProc(void* lpParametar, BOOLEAN TimerOrWaitFired)
{
    ms_iDummyProgressTimerCounter++;
}

//
// Refresh progress output
//
void CCore::UpdateDummyProgress(int iValue, const char* szType)
{
    if (iValue != -1)
    {
        m_iDummyProgressValue = iValue;
        m_strDummyProgressType = szType;
    }

    if (m_DummyProgressTimerHandle == NULL)
    {
        // Using this timer is quicker than checking tick count with every call to UpdateDummyProgress()
        ::CreateTimerQueueTimer(&m_DummyProgressTimerHandle, NULL, TimerProc, this, DUMMY_PROGRESS_ANIMATION_INTERVAL, DUMMY_PROGRESS_ANIMATION_INTERVAL,
                                WT_EXECUTEINTIMERTHREAD);
    }

    if (!ms_iDummyProgressTimerCounter)
        return;
    ms_iDummyProgressTimerCounter = 0;

    // Compose message with amount
    SString strMessage;
    if (m_iDummyProgressValue)
        strMessage = SString("%d%s", m_iDummyProgressValue, *m_strDummyProgressType);

    CGraphics::GetSingleton().SetProgressMessage(strMessage);
}

//
// Do SetCursorPos if allowed
//
void CCore::CallSetCursorPos(int X, int Y)
{
    if (CCore::GetSingleton().IsFocused() && !CLocalGUI::GetSingleton().IsMainMenuVisible())
        m_pLocalGUI->SetCursorPos(X, Y, true);
}

bool CCore::GetRequiredDisplayResolution(int& iOutWidth, int& iOutHeight, int& iOutColorBits, int& iOutAdapterIndex, bool& bOutAllowUnsafeResolutions)
{
    CVARS_GET("show_unsafe_resolutions", bOutAllowUnsafeResolutions);
    return GetVideoModeManager()->GetRequiredDisplayResolution(iOutWidth, iOutHeight, iOutColorBits, iOutAdapterIndex);
}

bool CCore::GetDeviceSelectionEnabled()
{
    return GetApplicationSettingInt("device-selection-disabled") ? false : true;
}

void CCore::NotifyRenderingGrass(bool bIsRenderingGrass)
{
    m_bIsRenderingGrass = bIsRenderingGrass;
    CDirect3DEvents9::CloseActiveShader();
}

bool CCore::GetRightSizeTxdEnabled()
{
    if (g_pCore->GetDiagnosticDebug() == EDiagnosticDebug::RESIZE_NEVER_0000)
        return false;
    if (g_pCore->GetDiagnosticDebug() == EDiagnosticDebug::RESIZE_ALWAYS_0000)
        return true;

    // 32 bit users get rightsized txds
    if (!Is64BitOS())
        return true;

    // Low ram users get rightsized txds
    int iSystemRamMB = static_cast<int>(GetWMITotalPhysicalMemory() / 1024LL / 1024LL);
    if (iSystemRamMB <= 2048)
        return true;

    return false;
}

SString CCore::GetBlueCopyrightString()
{
    SString strCopyright = BLUE_COPYRIGHT_STRING;
    return strCopyright.Replace("%BUILD_YEAR%", std::to_string(BUILD_YEAR).c_str());
}

// Set streaming memory size override [See `engineStreamingSetMemorySize`]
// Use `0` to turn it off, and thus restore the value to the `cvar` setting
void CCore::SetCustomStreamingMemory(size_t sizeBytes)
{
    // NOTE: The override is applied to the game in `CClientGame::DoPulsePostFrame`
    // There's no specific reason we couldn't do it here, but we wont
    m_CustomStreamingMemoryLimitBytes = sizeBytes;
}

bool CCore::IsUsingCustomStreamingMemorySize()
{
    return m_CustomStreamingMemoryLimitBytes != 0;
}

// Streaming memory size used [In Bytes]
size_t CCore::GetStreamingMemory()
{
    return IsUsingCustomStreamingMemorySize() ? m_CustomStreamingMemoryLimitBytes
                                              : CVARS_GET_VALUE<size_t>("streaming_memory") * 1024 * 1024;  // MB to B conversion
}

// Discord rich presence
std::shared_ptr<CDiscordInterface> CCore::GetDiscord()
{
    return m_pDiscordRichPresence;
}
