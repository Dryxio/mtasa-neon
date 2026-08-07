/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto: Neon
 *  LICENSE:     See LICENSE in the top level directory
 *  PURPOSE:     Native server-configured HD vehicle audio and backfire manager
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CVehicleSoundManager.h"

#include "CClientEffect.h"
#include "CClientEffectManager.h"
#include "CClientManager.h"
#include "CClientPlayer.h"
#include "CClientPlayerManager.h"
#include "CClientVehicle.h"
#include "CClientVehicleManager.h"
#include "CResource.h"
#include "CResourceManager.h"
#include "game/CHandlingEntry.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using SharedUtil::CalcMTASAPath;
using SharedUtil::FileExists;
using SharedUtil::PathJoin;

namespace
{
    constexpr unsigned int FMOD_VERSION_COMPATIBLE = 0x00020226;
    constexpr unsigned int FMOD_INIT_NORMAL = 0;
    constexpr unsigned int FMOD_STUDIO_INIT_NORMAL = 0;
    constexpr unsigned int FMOD_STUDIO_LOAD_BANK_NORMAL = 0;
    constexpr int          FMOD_STUDIO_STOP_ALLOWFADEOUT = 0;
    constexpr int          FMOD_OK = 0;

    struct FmodVector
    {
        float x;
        float y;
        float z;
    };

    struct Fmod3DAttributes
    {
        FmodVector position;
        FmodVector velocity;
        FmodVector forward;
        FmodVector up;
    };

    struct FmodStudioSystem;
    struct FmodSystem;
    struct FmodStudioBank;
    struct FmodStudioEventDescription;
    struct FmodStudioEventInstance;

    template <class T>
    bool LoadFunction(HMODULE module, const char* name, T& output)
    {
        output = reinterpret_cast<T>(GetProcAddress(module, name));
        return output != nullptr;
    }

    std::string Trim(std::string value)
    {
        const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char character) { return std::isspace(character) != 0; });
        const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char character) { return std::isspace(character) != 0; }).base();
        if (first >= last)
            return {};
        return std::string(first, last);
    }

    std::string ToLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        return value;
    }

    std::string ExpandBankNamePlaceholder(std::string value, const SString& metadataPath)
    {
        constexpr std::string_view placeholder = "<name>";
        const std::string          bankName = std::filesystem::path(metadataPath.c_str()).stem().string();
        for (std::size_t position = value.find(placeholder); position != std::string::npos; position = value.find(placeholder, position + bankName.size()))
            value.replace(position, placeholder.size(), bankName);
        return value;
    }

    FmodVector ToFmodVector(const CVector& vector)
    {
        return {vector.fX, vector.fY, vector.fZ};
    }

    Fmod3DAttributes ToFmodAttributes(const CMatrix& matrix, const CVector& velocity)
    {
        return {ToFmodVector(matrix.vPos), ToFmodVector(velocity), ToFmodVector(matrix.vFront), ToFmodVector(matrix.vUp)};
    }

    float CalculateGearMaximum(float maximumSpeed, int numberOfGears, int gear)
    {
        // The reference audio behavior reads GTA's generated transmission gear maximum. Rebuilding it from the public handling values keeps this backend
        // independent from game_sa's private transmission layout while producing the same ratio for forward gears.
        const float halfFirstGear = 0.5f * maximumSpeed / static_cast<float>(numberOfGears);
        const float gearStep = (maximumSpeed - halfFirstGear) / static_cast<float>(numberOfGears);
        return std::max(0.01f, std::clamp(gear, 1, numberOfGears) * gearStep + halfFirstGear);
    }

    unsigned int RandomInt(unsigned int minimum, unsigned int maximum)
    {
        if (maximum <= minimum)
            return minimum;
        return minimum + static_cast<unsigned int>(std::rand()) % (maximum - minimum);
    }

    constexpr float COMPETITIVE_MIX_NEAR_DISTANCE = 25.0f;
    constexpr float COMPETITIVE_MIX_FAR_DISTANCE = 100.0f;
    constexpr float COMPETITIVE_RIVAL_ENGINE_GAIN = 1.7782794f;    // +5 dB
    constexpr float COMPETITIVE_RIVAL_BACKFIRE_GAIN = 2.2387211f;  // +7 dB
    constexpr float COMPETITIVE_LOCAL_ENGINE_VOLUME_OFFSET = -3.0f;
    constexpr float COMPETITIVE_MIX_ATTACK_MS = 150.0f;
    constexpr float COMPETITIVE_MIX_RELEASE_MS = 500.0f;

    struct SoundDefinition
    {
        unsigned int model = 0;
        std::string  bankName;
        float        volume = 1.0f;
        float        minimumRpm = 1000.0f;
        float        maximumRpm = 8000.0f;
        float        gearRpm = 7000.0f;
        float        rpmUp = 50.0f;
        float        rpmDown = 100.0f;
        unsigned int gearTimeMs = 800;
        unsigned int backfireTimeMs = 0;
        float        turboBlowOff = 0.0f;
        unsigned int flags = 0;
    };

    struct BankMetadata
    {
        std::string engineExteriorEvent;
        std::string backfireExteriorEvent;
        std::string gearExteriorEvent;
        std::string engineInteriorEvent;
        std::string backfireInteriorEvent;
        std::string gearInteriorEvent;
        std::string turboEvent;
        std::string rpmParameter = "rpms";
        std::string throttleParameter = "throttle";
        std::string gearParameter = "state";
        std::string turboBoostParameter = "boost";
        std::string turboBlowOffParameter = "bov";
        float       engineExteriorVolume = 1.0f;
        float       backfireExteriorVolume = 1.0f;
        float       gearExteriorVolume = 1.0f;
        float       engineInteriorVolume = 1.0f;
        float       backfireInteriorVolume = 1.0f;
        float       gearInteriorVolume = 1.0f;
        float       turboVolume = 1.0f;
    };

    bool ParseFloat(const std::string& value, float& output)
    {
        try
        {
            std::size_t consumed = 0;
            output = std::stof(value, &consumed);
            return consumed == value.size() && std::isfinite(output);
        }
        catch (...)
        {
            return false;
        }
    }

    bool ParseUnsigned(const std::string& value, unsigned int& output)
    {
        try
        {
            std::size_t consumed = 0;
            const auto  parsed = std::stoul(value, &consumed);
            if (consumed != value.size())
                return false;
            output = static_cast<unsigned int>(parsed);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool ParseSoundDefinition(const std::string& line, SoundDefinition& definition)
    {
        std::istringstream input(line);
        std::string        modelName;
        std::string        gearTime;
        std::string        backfireTime;
        std::string        flags;

        if (!(input >> modelName >> std::quoted(definition.bankName) >> definition.volume >> definition.minimumRpm >> definition.maximumRpm >>
              definition.gearRpm >> definition.rpmUp >> definition.rpmDown >> gearTime >> backfireTime))
            return false;

        if (!(input >> definition.turboBlowOff))
        {
            input.clear();
            definition.turboBlowOff = 0.0f;
        }
        if (!(input >> flags))
        {
            input.clear();
            flags = "0";
        }

        definition.model = CModelNames::ResolveModelID(modelName.c_str());
        return CClientVehicleManager::IsValidModel(definition.model) && ParseUnsigned(gearTime, definition.gearTimeMs) &&
               ParseUnsigned(backfireTime, definition.backfireTimeMs) && ParseUnsigned(flags, definition.flags) && definition.minimumRpm >= 0.0f &&
               definition.maximumRpm > definition.minimumRpm && definition.rpmUp > 0.0f && definition.rpmDown > 0.0f;
    }

    bool LoadDefinitions(const SString& path, std::map<unsigned int, SoundDefinition>& definitions)
    {
        std::ifstream input(path.c_str());
        if (!input)
            return false;

        bool        inVehicleSection = false;
        std::string line;
        while (std::getline(input, line))
        {
            const std::size_t comment = line.find('#');
            if (comment != std::string::npos)
                line.erase(comment);
            line = Trim(line);
            if (line.empty())
                continue;

            const std::string lower = ToLower(line);
            if (lower == "vehicle")
            {
                inVehicleSection = true;
                continue;
            }
            if (lower == "end")
                break;
            if (!inVehicleSection || lower.rfind("version", 0) == 0)
                continue;

            SoundDefinition definition;
            if (ParseSoundDefinition(line, definition))
                definitions[definition.model] = std::move(definition);
        }
        return !definitions.empty();
    }

    bool LoadBankMetadata(const SString& path, BankMetadata& metadata)
    {
        std::ifstream input(path.c_str());
        if (!input)
            return false;

        std::string section;
        std::string line;
        while (std::getline(input, line))
        {
            const std::size_t comment = line.find_first_of(";#");
            if (comment != std::string::npos)
                line.erase(comment);
            line = Trim(line);
            if (line.empty())
                continue;
            if (line.front() == '[' && line.back() == ']')
            {
                section = ToLower(Trim(line.substr(1, line.size() - 2)));
                continue;
            }

            const std::size_t separator = line.find('=');
            if (separator == std::string::npos)
                continue;
            const std::string key = ToLower(Trim(line.substr(0, separator)));
            const std::string value = Trim(line.substr(separator + 1));

            if (section == "event")
            {
                if (key == "engineextevent")
                    metadata.engineExteriorEvent = ExpandBankNamePlaceholder(value, path);
                else if (key == "backfireextevent")
                    metadata.backfireExteriorEvent = ExpandBankNamePlaceholder(value, path);
                else if (key == "gearextevent")
                    metadata.gearExteriorEvent = ExpandBankNamePlaceholder(value, path);
                else if (key == "engineintevent")
                    metadata.engineInteriorEvent = ExpandBankNamePlaceholder(value, path);
                else if (key == "backfireintevent")
                    metadata.backfireInteriorEvent = ExpandBankNamePlaceholder(value, path);
                else if (key == "gearintevent")
                    metadata.gearInteriorEvent = ExpandBankNamePlaceholder(value, path);
                else if (key == "turboevent")
                    metadata.turboEvent = ExpandBankNamePlaceholder(value, path);
                else if (key == "engineextvolume")
                    ParseFloat(value, metadata.engineExteriorVolume);
                else if (key == "backfireextvolume")
                    ParseFloat(value, metadata.backfireExteriorVolume);
                else if (key == "gearextvolume")
                    ParseFloat(value, metadata.gearExteriorVolume);
                else if (key == "engineintvolume")
                    ParseFloat(value, metadata.engineInteriorVolume);
                else if (key == "backfireintvolume")
                    ParseFloat(value, metadata.backfireInteriorVolume);
                else if (key == "gearintvolume")
                    ParseFloat(value, metadata.gearInteriorVolume);
                else if (key == "turbovolume")
                    ParseFloat(value, metadata.turboVolume);
            }
            else if (section == "parameter")
            {
                if (key == "rpmparameter")
                    metadata.rpmParameter = value;
                else if (key == "throttleparameter")
                    metadata.throttleParameter = value;
                else if (key == "gearstateparameter")
                    metadata.gearParameter = value;
                else if (key == "turboboostparameter")
                    metadata.turboBoostParameter = value;
                else if (key == "turbobovparameter")
                    metadata.turboBlowOffParameter = value;
            }
        }
        return !metadata.engineExteriorEvent.empty();
    }

    class FmodBackend
    {
    public:
        using StudioSystemCreate = int(__stdcall*)(FmodStudioSystem**, unsigned int);
        using StudioSystemInitialize = int(__stdcall*)(FmodStudioSystem*, int, unsigned int, unsigned int, void*);
        using StudioSystemRelease = int(__stdcall*)(FmodStudioSystem*);
        using StudioSystemUpdate = int(__stdcall*)(FmodStudioSystem*);
        using StudioSystemLoadBankFile = int(__stdcall*)(FmodStudioSystem*, const char*, unsigned int, FmodStudioBank**);
        using StudioSystemGetEvent = int(__stdcall*)(FmodStudioSystem*, const char*, FmodStudioEventDescription**);
        using StudioSystemSetListenerAttributes = int(__stdcall*)(FmodStudioSystem*, int, const Fmod3DAttributes*, const FmodVector*);
        using StudioSystemGetCoreSystem = int(__stdcall*)(FmodStudioSystem*, FmodSystem**);
        using BankUnload = int(__stdcall*)(FmodStudioBank*);
        using EventDescriptionCreateInstance = int(__stdcall*)(FmodStudioEventDescription*, FmodStudioEventInstance**);
        using EventInstanceRelease = int(__stdcall*)(FmodStudioEventInstance*);
        using EventInstanceStart = int(__stdcall*)(FmodStudioEventInstance*);
        using EventInstanceStop = int(__stdcall*)(FmodStudioEventInstance*, int);
        using EventInstanceSet3DAttributes = int(__stdcall*)(FmodStudioEventInstance*, const Fmod3DAttributes*);
        using EventInstanceSetParameterByName = int(__stdcall*)(FmodStudioEventInstance*, const char*, float, int);
        using EventInstanceSetVolume = int(__stdcall*)(FmodStudioEventInstance*, float);
        using SystemLoadPlugin = int(__stdcall*)(FmodSystem*, const char*, unsigned int*, unsigned int);
        using SystemSetDSPBufferSize = int(__stdcall*)(FmodSystem*, unsigned int, int);
        using SystemSetSoftwareFormat = int(__stdcall*)(FmodSystem*, int, int, int);

        ~FmodBackend() { Shutdown(); }

        bool Initialize(const SString& runtimeRoot, const SString& contentRoot)
        {
            if (m_system)
                return true;

            m_coreModule = LoadLibraryA(PathJoin(runtimeRoot, "fmod.dll"));
            if (!m_coreModule)
                return false;
            m_studioModule = LoadLibraryA(PathJoin(runtimeRoot, "fmodstudio.dll"));
            if (!m_studioModule || !LoadApi())
            {
                Shutdown();
                return false;
            }

            if (m_studioSystemCreate(&m_system, FMOD_VERSION_COMPATIBLE) != FMOD_OK)
            {
                Shutdown();
                return false;
            }

            FmodSystem* coreSystem = nullptr;
            if (m_studioSystemGetCoreSystem(m_system, &coreSystem) != FMOD_OK || !coreSystem)
            {
                Shutdown();
                return false;
            }
            m_systemSetDSPBufferSize(coreSystem, 512, 4);
            m_systemSetSoftwareFormat(coreSystem, 0, 0, 0);

            if (!LoadRequiredPlugin(coreSystem, PathJoin(runtimeRoot, "plugins", "fmod_distance_filter.dll")) ||
                !LoadRequiredPlugin(coreSystem, PathJoin(runtimeRoot, "plugins", "fmod_gain.dll")))
            {
                Shutdown();
                return false;
            }

            if (m_studioSystemInitialize(m_system, 1024, FMOD_STUDIO_INIT_NORMAL, FMOD_INIT_NORMAL, nullptr) != FMOD_OK)
            {
                Shutdown();
                return false;
            }

            if (!LoadRequiredBank(PathJoin(contentRoot, "base", "common.bank"), m_commonBank) ||
                !LoadRequiredBank(PathJoin(contentRoot, "base", "common.strings.bank"), m_commonStringsBank))
            {
                Shutdown();
                return false;
            }
            return true;
        }

        void Shutdown()
        {
            if (m_commonBank && m_bankUnload)
                m_bankUnload(m_commonBank);
            if (m_commonStringsBank && m_bankUnload)
                m_bankUnload(m_commonStringsBank);
            m_commonBank = nullptr;
            m_commonStringsBank = nullptr;

            if (m_system && m_studioSystemRelease)
                m_studioSystemRelease(m_system);
            m_system = nullptr;

            if (m_studioModule)
                FreeLibrary(m_studioModule);
            if (m_coreModule)
                FreeLibrary(m_coreModule);
            m_studioModule = nullptr;
            m_coreModule = nullptr;
        }

        FmodStudioBank* LoadBank(const SString& path)
        {
            FmodStudioBank* bank = nullptr;
            if (!m_system || m_studioSystemLoadBankFile(m_system, path.c_str(), FMOD_STUDIO_LOAD_BANK_NORMAL, &bank) != FMOD_OK)
                return nullptr;
            return bank;
        }

        void UnloadBank(FmodStudioBank* bank)
        {
            if (bank)
                m_bankUnload(bank);
        }

        FmodStudioEventInstance* CreateEvent(const std::string& path)
        {
            if (!m_system || path.empty())
                return nullptr;
            FmodStudioEventDescription* description = nullptr;
            FmodStudioEventInstance*    instance = nullptr;
            if (m_studioSystemGetEvent(m_system, path.c_str(), &description) != FMOD_OK || !description ||
                m_eventDescriptionCreateInstance(description, &instance) != FMOD_OK)
                return nullptr;
            return instance;
        }

        void Start(FmodStudioEventInstance* instance)
        {
            if (instance)
                m_eventInstanceStart(instance);
        }

        void StopAndRelease(FmodStudioEventInstance*& instance)
        {
            if (!instance)
                return;
            m_eventInstanceStop(instance, FMOD_STUDIO_STOP_ALLOWFADEOUT);
            m_eventInstanceRelease(instance);
            instance = nullptr;
        }

        void PlayOneShot(const std::string& path, const Fmod3DAttributes& attributes, float volume, const BankMetadata& metadata, float rpm, float throttle,
                         float gear, float turboBoost)
        {
            FmodStudioEventInstance* instance = CreateEvent(path);
            if (!instance)
                return;
            Set3DAttributes(instance, attributes);
            SetVolume(instance, volume);
            SetParameter(instance, metadata.rpmParameter, rpm);
            SetParameter(instance, metadata.throttleParameter, throttle * 2.0f - 1.0f);
            SetParameter(instance, metadata.gearParameter, gear);
            SetParameter(instance, metadata.turboBoostParameter, turboBoost);
            Start(instance);
            m_eventInstanceRelease(instance);
        }

        void Set3DAttributes(FmodStudioEventInstance* instance, const Fmod3DAttributes& attributes)
        {
            if (instance)
                m_eventInstanceSet3DAttributes(instance, &attributes);
        }

        void SetParameter(FmodStudioEventInstance* instance, const std::string& name, float value)
        {
            if (instance && !name.empty())
                m_eventInstanceSetParameterByName(instance, name.c_str(), value, false);
        }

        void SetVolume(FmodStudioEventInstance* instance, float volume)
        {
            if (instance)
                m_eventInstanceSetVolume(instance, std::max(0.0f, volume));
        }

        void SetListener(const Fmod3DAttributes& attributes)
        {
            if (m_system)
                m_studioSystemSetListenerAttributes(m_system, 0, &attributes, nullptr);
        }

        void Update()
        {
            if (m_system)
                m_studioSystemUpdate(m_system);
        }

    private:
        bool LoadApi()
        {
            return LoadFunction(m_studioModule, "FMOD_Studio_System_Create", m_studioSystemCreate) &&
                   LoadFunction(m_studioModule, "FMOD_Studio_System_Initialize", m_studioSystemInitialize) &&
                   LoadFunction(m_studioModule, "FMOD_Studio_System_Release", m_studioSystemRelease) &&
                   LoadFunction(m_studioModule, "FMOD_Studio_System_Update", m_studioSystemUpdate) &&
                   LoadFunction(m_studioModule, "FMOD_Studio_System_LoadBankFile", m_studioSystemLoadBankFile) &&
                   LoadFunction(m_studioModule, "FMOD_Studio_System_GetEvent", m_studioSystemGetEvent) &&
                   LoadFunction(m_studioModule, "FMOD_Studio_System_SetListenerAttributes", m_studioSystemSetListenerAttributes) &&
                   LoadFunction(m_studioModule, "FMOD_Studio_System_GetCoreSystem", m_studioSystemGetCoreSystem) &&
                   LoadFunction(m_studioModule, "FMOD_Studio_Bank_Unload", m_bankUnload) &&
                   LoadFunction(m_studioModule, "FMOD_Studio_EventDescription_CreateInstance", m_eventDescriptionCreateInstance) &&
                   LoadFunction(m_studioModule, "FMOD_Studio_EventInstance_Release", m_eventInstanceRelease) &&
                   LoadFunction(m_studioModule, "FMOD_Studio_EventInstance_Start", m_eventInstanceStart) &&
                   LoadFunction(m_studioModule, "FMOD_Studio_EventInstance_Stop", m_eventInstanceStop) &&
                   LoadFunction(m_studioModule, "FMOD_Studio_EventInstance_Set3DAttributes", m_eventInstanceSet3DAttributes) &&
                   LoadFunction(m_studioModule, "FMOD_Studio_EventInstance_SetParameterByName", m_eventInstanceSetParameterByName) &&
                   LoadFunction(m_studioModule, "FMOD_Studio_EventInstance_SetVolume", m_eventInstanceSetVolume) &&
                   LoadFunction(m_coreModule, "FMOD_System_LoadPlugin", m_systemLoadPlugin) &&
                   LoadFunction(m_coreModule, "FMOD_System_SetDSPBufferSize", m_systemSetDSPBufferSize) &&
                   LoadFunction(m_coreModule, "FMOD_System_SetSoftwareFormat", m_systemSetSoftwareFormat);
        }

        bool LoadRequiredBank(const SString& path, FmodStudioBank*& bank)
        {
            if (!FileExists(path))
                return false;
            bank = LoadBank(path);
            return bank != nullptr;
        }

        bool LoadRequiredPlugin(FmodSystem* system, const SString& path)
        {
            if (!FileExists(path))
                return false;
            unsigned int handle = 0;
            return m_systemLoadPlugin(system, path.c_str(), &handle, 0) == FMOD_OK;
        }

        HMODULE m_coreModule = nullptr;
        HMODULE m_studioModule = nullptr;

        FmodStudioSystem* m_system = nullptr;
        FmodStudioBank*   m_commonBank = nullptr;
        FmodStudioBank*   m_commonStringsBank = nullptr;

        StudioSystemCreate                m_studioSystemCreate = nullptr;
        StudioSystemInitialize            m_studioSystemInitialize = nullptr;
        StudioSystemRelease               m_studioSystemRelease = nullptr;
        StudioSystemUpdate                m_studioSystemUpdate = nullptr;
        StudioSystemLoadBankFile          m_studioSystemLoadBankFile = nullptr;
        StudioSystemGetEvent              m_studioSystemGetEvent = nullptr;
        StudioSystemSetListenerAttributes m_studioSystemSetListenerAttributes = nullptr;
        StudioSystemGetCoreSystem         m_studioSystemGetCoreSystem = nullptr;
        BankUnload                        m_bankUnload = nullptr;
        EventDescriptionCreateInstance    m_eventDescriptionCreateInstance = nullptr;
        EventInstanceRelease              m_eventInstanceRelease = nullptr;
        EventInstanceStart                m_eventInstanceStart = nullptr;
        EventInstanceStop                 m_eventInstanceStop = nullptr;
        EventInstanceSet3DAttributes      m_eventInstanceSet3DAttributes = nullptr;
        EventInstanceSetParameterByName   m_eventInstanceSetParameterByName = nullptr;
        EventInstanceSetVolume            m_eventInstanceSetVolume = nullptr;
        SystemLoadPlugin                  m_systemLoadPlugin = nullptr;
        SystemSetDSPBufferSize            m_systemSetDSPBufferSize = nullptr;
        SystemSetSoftwareFormat           m_systemSetSoftwareFormat = nullptr;
    };
}

struct CVehicleSoundManager::Impl
{
    struct LoadedBank
    {
        FmodStudioBank* bank = nullptr;
        BankMetadata    metadata;
        unsigned int    references = 0;
    };

    struct VehicleState
    {
        const SoundDefinition*   definition = nullptr;
        LoadedBank*              loadedBank = nullptr;
        FmodStudioEventInstance* engineEvent = nullptr;
        float                    rpm = 1000.0f;
        float                    previousThrottle = 0.0f;
        float                    smoothedThrottle = 0.0f;
        float                    turboBoost = 0.0f;
        int                      committedGear = -1;
        int                      pendingGear = -1;
        bool                     interior = false;
        bool                     nativeAudioMuted = false;
        short                    nativeEngineOnBank = -1;
        short                    nativeEngineOffBank = -1;
        unsigned long long       gearChangeTick = 0;
        unsigned long long       lastBackfireTick = 0;
        unsigned long long       afterfireUntilTick = 0;
        unsigned long long       nextTelemetryTick = 0;
    };

    struct BankAsset
    {
        SString bankPath;
        SString metadataPath;
    };

    explicit Impl(CClientManager* manager) : clientManager(manager), runtimeRoot(CalcMTASAPath(PathJoin("MTA", "vehicle-sounds", "runtime"))) {}

    ~Impl() { Deactivate(); }

    void DoPulse()
    {
        // Servers opt in by loading a resource-owned vehicle-audio configuration. Without one this is the only work performed per vehicle pulse: no FMOD,
        // filesystem scan, listener update or streamed-vehicle iteration occurs.
        if (!owner)
            return;
        if (!clientManager->GetResourceManager()->Exists(owner))
        {
            Deactivate();
            return;
        }

        if (!ready)
        {
            const unsigned long long now = GetTickCount64_();
            if (now < nextInitializationTick || !Initialize())
                return;
            nextInitializationTick = now + 5000;
        }

        UpdateListener();

        std::set<CClientVehicle*> seen;
        CClientVehicleManager*    vehicleManager = clientManager->GetVehicleManager();
        UpdateCompetitiveMix(vehicleManager);
        for (auto iterator = vehicleManager->StreamedBegin(); iterator != vehicleManager->StreamedEnd(); ++iterator)
        {
            CClientVehicle* vehicle = *iterator;
            if (!vehicle)
                continue;
            const unsigned int audioMode = GetVehicleAudioMode(*vehicle);
            if (!audioMode)
                continue;
            const auto definition = definitions.find(vehicle->GetModel());
            if (definition == definitions.end())
                continue;

            seen.insert(vehicle);
            UpdateVehicle(*vehicle, definition->second, audioMode);
        }

        for (auto iterator = vehicles.begin(); iterator != vehicles.end();)
        {
            if (seen.count(iterator->first) != 0)
            {
                ++iterator;
                continue;
            }

            CClientVehicle* vehicle = iterator->first;
            VehicleState&   state = iterator->second;
            const bool      vehicleStillExists = vehicleManager->Exists(vehicle);
            StopVehicle(vehicleStillExists ? vehicle : nullptr, state);
            iterator = vehicles.erase(iterator);
        }

        // Only the server-owned vehicle audio feature creates attached transient effects. Keep their emitter update out of the global client pulse so
        // servers that never opt in do not pay for an additional effect-list traversal.
        clientManager->GetEffectManager()->DoPulse();
        backend.Update();
    }

    bool LoadConfig(CResource* resource, const SString& path)
    {
        if (!resource || path.empty() || (owner && owner != resource))
            return false;

        std::map<unsigned int, SoundDefinition> newDefinitions;
        if (!LoadDefinitions(path, newDefinitions))
            return false;

        Deactivate();
        owner = resource;
        configPath = path;
        contentRoot = std::filesystem::path(path.c_str()).parent_path().string().c_str();
        definitions = std::move(newDefinitions);
        IndexBanks();
        g_pCore->GetConsole()->Printf("Vehicle audio: server resource registered %u HD vehicle-audio definitions",
                                      static_cast<unsigned int>(definitions.size()));
        return true;
    }

    bool ReloadConfig(CResource* resource)
    {
        if (!owner || owner != resource || configPath.empty())
            return false;

        std::map<unsigned int, SoundDefinition> newDefinitions;
        if (!LoadDefinitions(configPath, newDefinitions))
            return false;

        StopAll();
        definitions = std::move(newDefinitions);
        bankAssets.clear();
        IndexBanks();
        nextInitializationTick = 0;
        g_pCore->GetConsole()->Printf("Vehicle audio: server resource refreshed %u definitions and %u downloaded banks",
                                      static_cast<unsigned int>(definitions.size()), static_cast<unsigned int>(bankAssets.size()));
        return true;
    }

    bool UnloadConfig(CResource* resource)
    {
        if (!owner || owner != resource)
            return false;
        Deactivate();
        return true;
    }

    bool Initialize()
    {
        if (!owner || definitions.empty())
            return false;
        nextInitializationTick = GetTickCount64_() + 5000;
        if (!backend.Initialize(runtimeRoot, contentRoot))
        {
            g_pCore->GetConsole()->Printf("Vehicle audio: FMOD runtime or server base banks could not be initialized");
            return false;
        }

        ready = true;
        g_pCore->GetConsole()->Printf("Vehicle audio: initialized for server resource with %u downloaded banks", static_cast<unsigned int>(bankAssets.size()));
        return true;
    }

    void Deactivate()
    {
        RestoreCompetitiveNativeDuck();
        StopAll();
        backend.Shutdown();
        if (telemetry.is_open())
            telemetry.close();
        ready = false;
        nextInitializationTick = 0;
        owner = nullptr;
        configPath.clear();
        contentRoot.clear();
        definitions.clear();
        bankAssets.clear();
        competitiveLocalVehicle = nullptr;
        competitiveRivalVehicle = nullptr;
        competitiveMixAmount = 0.0f;
        competitiveMixTick = 0;
    }

    void InitializeTelemetry()
    {
        const SString   telemetryPath = CalcMTASAPath(PathJoin("MTA", "logs", "vehicle-audio.csv"));
        std::error_code error;
        std::filesystem::create_directories(std::filesystem::path(telemetryPath.c_str()).parent_path(), error);
        telemetry.open(telemetryPath.c_str(), std::ios::out | std::ios::trunc);
        if (!telemetry)
        {
            g_pCore->GetConsole()->Printf("Vehicle audio: could not create telemetry at %s", telemetryPath.c_str());
            return;
        }

        telemetry << "tick_ms,model,speed_kmh,raw_gear,pending_gear,committed_gear,gear_max_kmh,gear_ratio,raw_throttle,audio_throttle,shift_phase,"
                     "shift_elapsed_ms,shift_duration_ms,target_rpm,rpm,gear_commit,backfire,backfire_reason\n";
        telemetry.flush();
        g_pCore->GetConsole()->Printf("Vehicle audio: telemetry enabled at %s", telemetryPath.c_str());
    }

    void LogTelemetry(CClientVehicle& vehicle, VehicleState& state, unsigned long long now, float speedKmh, int rawGear, float gearMaximum, float gearRatio,
                      float rawThrottle, float audioThrottle, const char* shiftPhase, unsigned long long shiftElapsed, unsigned int shiftDuration,
                      float targetRpm, bool gearCommitted, bool backfire, const char* backfireReason)
    {
        if (!telemetry || (now < state.nextTelemetryTick && !gearCommitted && !backfire))
            return;

        state.nextTelemetryTick = now + 100;
        telemetry << now << ',' << vehicle.GetModel() << ',' << std::fixed << std::setprecision(2) << speedKmh << ',' << rawGear << ',' << state.pendingGear
                  << ',' << state.committedGear << ',' << gearMaximum << ',' << gearRatio << ',' << rawThrottle << ',' << audioThrottle << ',' << shiftPhase
                  << ',' << shiftElapsed << ',' << shiftDuration << ',' << targetRpm << ',' << state.rpm << ',' << (gearCommitted ? 1 : 0) << ','
                  << (backfire ? 1 : 0) << ',' << backfireReason << '\n';
        telemetry.flush();
    }

    bool TriggerBackfire(VehicleState& state, unsigned long long now, unsigned int cooldownMs)
    {
        // The reference behavior uses one strict RPM/cooldown gate for release, afterfire and free-rev limiter events. Keeping it centralized prevents the
        // visual and FMOD backfire states from drifting apart again.
        if (state.rpm <= 5000.0f || now <= state.lastBackfireTick + cooldownMs)
            return false;
        state.lastBackfireTick = now;
        return true;
    }

    unsigned int GetVehicleAudioMode(CClientVehicle& vehicle) const
    {
        static const CStringName key("neon:vehicleAudio");
        CLuaArgument*            value = vehicle.GetCustomData(key, false);
        if (!value)
            return 0;

        if (value->GetType() == LUA_TBOOLEAN)
            return value->GetBoolean() ? 2 : 0;
        if (value->GetType() == LUA_TNUMBER)
            return std::clamp(static_cast<unsigned int>(std::max(0.0, value->GetNumber())), 0u, 3u);
        if (value->GetType() == LUA_TSTRING)
        {
            const std::string mode = ToLower(value->GetString());
            if (mode == "full" || mode == "flames" || mode == "2" || mode == "true")
                return 2;
            if (mode == "sound" || mode == "1")
                return 1;
            if (mode == "silent-local" || mode == "3")
                return 3;
        }
        return 0;
    }

    bool UsesCompetitiveMix(CClientVehicle& vehicle) const
    {
        static const CStringName key("neon:vehicleAudioCompetitive");
        CLuaArgument*            value = vehicle.GetCustomData(key, false);
        if (!value)
            return false;

        if (value->GetType() == LUA_TBOOLEAN)
            return value->GetBoolean();
        if (value->GetType() == LUA_TNUMBER)
            return value->GetNumber() != 0.0;
        if (value->GetType() == LUA_TSTRING)
        {
            const std::string enabled = ToLower(value->GetString());
            return enabled == "true" || enabled == "1" || enabled == "on";
        }
        return false;
    }

    void UpdateCompetitiveMix(CClientVehicleManager* vehicleManager)
    {
        CClientPlayer*  localPlayer = clientManager->GetPlayerManager()->GetLocalPlayer();
        CClientVehicle* localVehicle = localPlayer ? localPlayer->GetOccupiedVehicle() : nullptr;
        CClientVehicle* rivalVehicle = nullptr;
        float           targetAmount = 0.0f;

        // The showcase mix is deliberately asymmetric: a non-VIP driver hears a nearby VIP rival more clearly. VIP-vs-VIP keeps the authored
        // HD engine-audio balance unchanged instead of making both clients fight their own mix.
        if (localVehicle && UsesCompetitiveMix(*localVehicle) && !GetVehicleAudioMode(*localVehicle))
        {
            CVector localPosition;
            localVehicle->GetPosition(localPosition);
            float closestDistanceSquared = COMPETITIVE_MIX_FAR_DISTANCE * COMPETITIVE_MIX_FAR_DISTANCE;

            for (auto iterator = vehicleManager->StreamedBegin(); iterator != vehicleManager->StreamedEnd(); ++iterator)
            {
                CClientVehicle* candidate = *iterator;
                if (!candidate || candidate == localVehicle || !UsesCompetitiveMix(*candidate) || !GetVehicleAudioMode(*candidate) ||
                    definitions.count(candidate->GetModel()) == 0 || candidate->GetDimension() != localVehicle->GetDimension() ||
                    candidate->GetInterior() != localVehicle->GetInterior())
                    continue;

                CVector candidatePosition;
                candidate->GetPosition(candidatePosition);
                const float distanceSquared = (candidatePosition - localPosition).LengthSquared();
                if (distanceSquared > closestDistanceSquared)
                    continue;

                closestDistanceSquared = distanceSquared;
                rivalVehicle = candidate;
            }

            if (rivalVehicle)
            {
                const float distance = std::sqrt(closestDistanceSquared);
                targetAmount =
                    1.0f - std::clamp((distance - COMPETITIVE_MIX_NEAR_DISTANCE) / (COMPETITIVE_MIX_FAR_DISTANCE - COMPETITIVE_MIX_NEAR_DISTANCE), 0.0f, 1.0f);
            }
        }

        competitiveLocalVehicle = localVehicle;
        competitiveRivalVehicle = rivalVehicle;

        const unsigned long long now = GetTickCount64_();
        if (!competitiveMixTick)
        {
            competitiveMixTick = now;
            competitiveMixAmount = 0.0f;
            return;
        }

        const float elapsedMs = static_cast<float>(std::min<unsigned long long>(now - competitiveMixTick, 1000));
        competitiveMixTick = now;
        const float duration = targetAmount > competitiveMixAmount ? COMPETITIVE_MIX_ATTACK_MS : COMPETITIVE_MIX_RELEASE_MS;
        const float maximumChange = elapsedMs / duration;
        if (targetAmount > competitiveMixAmount)
            competitiveMixAmount = std::min(targetAmount, competitiveMixAmount + maximumChange);
        else
            competitiveMixAmount = std::max(targetAmount, competitiveMixAmount - maximumChange);

        UpdateCompetitiveNativeDuck();
    }

    float GetCompetitiveEngineGain(CClientVehicle& vehicle) const
    {
        if (&vehicle == competitiveRivalVehicle)
            return 1.0f + (COMPETITIVE_RIVAL_ENGINE_GAIN - 1.0f) * competitiveMixAmount;
        return 1.0f;
    }

    float GetCompetitiveBackfireGain(CClientVehicle& vehicle) const
    {
        if (&vehicle == competitiveRivalVehicle)
            return 1.0f + (COMPETITIVE_RIVAL_BACKFIRE_GAIN - 1.0f) * competitiveMixAmount;
        return 1.0f;
    }

    void RestoreCompetitiveNativeDuck()
    {
        if (!competitiveDuckedVehicle)
            return;

        CClientVehicleManager* vehicleManager = clientManager ? clientManager->GetVehicleManager() : nullptr;
        if (vehicleManager && vehicleManager->Exists(competitiveDuckedVehicle))
            competitiveDuckedVehicle->SetLiveNativeEngineVolumeOffset(competitiveOriginalEngineVolumeOffset);
        competitiveDuckedVehicle = nullptr;
        competitiveOriginalEngineVolumeOffset = 0.0f;
    }

    void UpdateCompetitiveNativeDuck()
    {
        CClientVehicle* target = competitiveRivalVehicle && competitiveLocalVehicle ? competitiveLocalVehicle : nullptr;
        if (target != competitiveDuckedVehicle)
        {
            RestoreCompetitiveNativeDuck();
            float originalOffset = 0.0f;
            if (target && target->GetLiveNativeEngineVolumeOffset(originalOffset))
            {
                competitiveDuckedVehicle = target;
                competitiveOriginalEngineVolumeOffset = originalOffset;
            }
        }

        if (competitiveDuckedVehicle)
            competitiveDuckedVehicle->SetLiveNativeEngineVolumeOffset(competitiveOriginalEngineVolumeOffset +
                                                                      COMPETITIVE_LOCAL_ENGINE_VOLUME_OFFSET * competitiveMixAmount);
    }

    void IndexBanks()
    {
        const std::filesystem::path banksRoot(PathJoin(contentRoot, "banks").c_str());
        std::error_code             error;
        if (!std::filesystem::is_directory(banksRoot, error))
            return;

        for (std::filesystem::recursive_directory_iterator iterator(banksRoot, std::filesystem::directory_options::skip_permission_denied, error), end;
             iterator != end; iterator.increment(error))
        {
            if (error)
            {
                error.clear();
                continue;
            }
            if (!iterator->is_regular_file(error) || ToLower(iterator->path().extension().string()) != ".bank")
                continue;

            const std::filesystem::path metadataPath = iterator->path().parent_path() / (iterator->path().stem().string() + ".ini");
            if (!std::filesystem::is_regular_file(metadataPath, error))
                continue;

            const std::string key = ToLower(iterator->path().stem().string());
            bankAssets.try_emplace(key, BankAsset{iterator->path().string().c_str(), metadataPath.string().c_str()});
        }
    }

    void UpdateListener()
    {
        CMatrix cameraMatrix;
        if (!clientManager->GetCamera()->GetMatrix(cameraMatrix))
            return;

        CVector velocity;
        if (CClientPlayer* localPlayer = clientManager->GetPlayerManager()->GetLocalPlayer())
            localPlayer->GetMoveSpeed(velocity);
        backend.SetListener(ToFmodAttributes(cameraMatrix, velocity));
    }

    LoadedBank* AcquireBank(const SoundDefinition& definition)
    {
        auto existing = banks.find(definition.bankName);
        if (existing != banks.end())
        {
            ++existing->second.references;
            return &existing->second;
        }

        const auto asset = bankAssets.find(ToLower(definition.bankName));
        if (asset == bankAssets.end())
        {
            g_pCore->GetConsole()->Printf("Vehicle audio: bank '%s' was not found", definition.bankName.c_str());
            return nullptr;
        }

        LoadedBank loaded;
        if (!LoadBankMetadata(asset->second.metadataPath, loaded.metadata))
        {
            g_pCore->GetConsole()->Printf("Vehicle audio: invalid metadata %s", asset->second.metadataPath.c_str());
            return nullptr;
        }
        loaded.bank = backend.LoadBank(asset->second.bankPath);
        if (!loaded.bank)
        {
            g_pCore->GetConsole()->Printf("Vehicle audio: failed to load bank %s", asset->second.bankPath.c_str());
            return nullptr;
        }
        loaded.references = 1;
        return &banks.emplace(definition.bankName, std::move(loaded)).first->second;
    }

    void ReleaseBank(const SoundDefinition& definition, LoadedBank*& loadedBank)
    {
        if (!loadedBank)
            return;
        auto iterator = banks.find(definition.bankName);
        if (iterator != banks.end() && --iterator->second.references == 0)
        {
            backend.UnloadBank(iterator->second.bank);
            banks.erase(iterator);
        }
        loadedBank = nullptr;
    }

    bool EnsureEngineEvent(CClientVehicle& vehicle, VehicleState& state, bool interior)
    {
        if (state.engineEvent && state.interior == interior)
            return true;
        backend.StopAndRelease(state.engineEvent);
        state.interior = interior;

        const BankMetadata& metadata = state.loadedBank->metadata;
        const std::string&  eventPath = interior && !metadata.engineInteriorEvent.empty() ? metadata.engineInteriorEvent : metadata.engineExteriorEvent;
        state.engineEvent = backend.CreateEvent(eventPath);
        if (!state.engineEvent)
            return false;

        backend.Start(state.engineEvent);
        MuteNativeAudio(vehicle, state);
        return true;
    }

    void MuteNativeAudio(CClientVehicle& vehicle, VehicleState& state)
    {
        if (state.nativeAudioMuted)
            return;

        const CVehicleAudioSettingsEntry& currentSettings = vehicle.GetAudioSettings();
        state.nativeEngineOnBank = currentSettings.GetEngineOnSoundBankID();
        state.nativeEngineOffBank = currentSettings.GetEngineOffSoundBankID();
        CVehicleAudioSettingsEntry& replacementSettings = vehicle.GetOrCreateAudioSettings();
        replacementSettings.SetEngineOnSoundBankID(-1);
        replacementSettings.SetEngineOffSoundBankID(-1);
        vehicle.ApplyAudioSettings();
        state.nativeAudioMuted = true;
    }

    void RestoreNativeAudio(CClientVehicle& vehicle, VehicleState& state)
    {
        if (!state.nativeAudioMuted)
            return;
        CVehicleAudioSettingsEntry& settings = vehicle.GetOrCreateAudioSettings();
        settings.SetEngineOnSoundBankID(state.nativeEngineOnBank);
        settings.SetEngineOffSoundBankID(state.nativeEngineOffBank);
        vehicle.ApplyAudioSettings();
        state.nativeAudioMuted = false;
    }

    void UpdateVehicle(CClientVehicle& vehicle, const SoundDefinition& definition, unsigned int audioMode)
    {
        auto          insertion = vehicles.emplace(&vehicle, VehicleState{});
        VehicleState& state = insertion.first->second;
        if (insertion.second)
        {
            state.definition = &definition;
            state.rpm = definition.minimumRpm;
            state.loadedBank = AcquireBank(definition);
            if (!state.loadedBank)
            {
                vehicles.erase(insertion.first);
                return;
            }
        }

        if (!vehicle.IsEngineOn() || vehicle.IsBlown())
        {
            backend.StopAndRelease(state.engineEvent);
            RestoreNativeAudio(vehicle, state);
            state.rpm = definition.minimumRpm;
            state.previousThrottle = 0.0f;
            state.smoothedThrottle = 0.0f;
            state.committedGear = -1;
            state.pendingGear = -1;
            state.afterfireUntilTick = 0;
            return;
        }

        CClientPlayer* localPlayer = clientManager->GetPlayerManager()->GetLocalPlayer();
        const bool     interior = localPlayer && localPlayer->GetOccupiedVehicle() == &vehicle;
        const bool     localDriver = interior && localPlayer->GetOccupiedVehicleSeat() == 0;
        const bool     localOutputMuted = localDriver && audioMode == 3;
        if (!EnsureEngineEvent(vehicle, state, interior))
            return;

        // Mode 3 keeps HD engine-audio simulation and event generation alive for recording/network observers, while the controlling client hears GTA's native
        // engine instead. Other clients resolve the same vehicle as a normal full-output source because they are not its local driver.
        if (localOutputMuted)
            RestoreNativeAudio(vehicle, state);
        else
            MuteNativeAudio(vehicle, state);

        CVector velocity;
        vehicle.GetMoveSpeed(velocity);
        CMatrix matrix;
        if (!vehicle.GetMatrix(matrix))
            return;

        const CHandlingEntry*    handling = vehicle.GetHandlingData();
        const float              maximumSpeed = handling ? std::max(1.0f, handling->GetMaxVelocity()) : 200.0f;
        const int                numberOfGears = handling ? std::max(1, static_cast<int>(handling->GetNumberOfGears())) : 5;
        const int                rawGear = std::clamp(vehicle.GetCurrentGear(), 0, numberOfGears);
        const float              speedKmh = velocity.DotProduct(&matrix.vFront) * 180.0f;
        const float              rawThrottle = std::clamp(std::abs(vehicle.GetGasPedal()), 0.0f, 1.0f);
        const float              timeStep = std::max(0.0f, g_pGame->GetTimeStep());
        const unsigned long long now = GetTickCount64_();

        if (state.committedGear < 0)
        {
            state.committedGear = rawGear;
            state.pendingGear = rawGear;
            state.gearChangeTick = 0;
        }
        else if (rawGear != state.pendingGear)
        {
            state.pendingGear = rawGear;
            state.gearChangeTick = now;
        }

        const float              shiftScale = state.pendingGear <= 1 ? 0.6f : state.pendingGear == 2 ? 0.8f : 1.0f;
        const unsigned int       shiftDuration = static_cast<unsigned int>(definition.gearTimeMs * shiftScale);
        const unsigned long long shiftElapsed = state.gearChangeTick ? now - state.gearChangeTick : shiftDuration;
        const bool               shiftPending = state.pendingGear != state.committedGear;
        const bool               shiftCut = shiftPending && shiftElapsed < shiftDuration / 2;
        bool                     gearCommitted = false;
        if (shiftPending && (!shiftDuration || shiftElapsed >= shiftDuration / 2))
        {
            state.committedGear = state.pendingGear;
            gearCommitted = true;
        }

        const char* shiftPhase = shiftCut ? "cut" : shiftElapsed < shiftDuration ? "recover" : "steady";
        if (shiftCut)
            state.smoothedThrottle = 0.0f;
        else
        {
            const float throttleStep = 0.05f * timeStep;
            if (rawThrottle >= state.smoothedThrottle)
                state.smoothedThrottle = std::min(rawThrottle, state.smoothedThrottle + throttleStep);
            else
                state.smoothedThrottle = std::max(rawThrottle, state.smoothedThrottle - throttleStep);
        }
        const float audioThrottle = state.smoothedThrottle;

        const float gearMaximum = CalculateGearMaximum(maximumSpeed, numberOfGears, state.committedGear);
        float       gearRevsRatio = std::abs(speedKmh) / gearMaximum;
        if (state.committedGear == 0)
            gearRevsRatio *= 0.5f;
        float targetRpm = state.committedGear == 0 ? definition.minimumRpm + (definition.gearRpm - definition.minimumRpm) * gearRevsRatio
                                                   : definition.gearRpm * gearRevsRatio;
        targetRpm = std::clamp(targetRpm, definition.minimumRpm, definition.maximumRpm);

        const float adjustment = (shiftCut ? definition.rpmDown * 0.5f : targetRpm >= state.rpm ? definition.rpmUp : definition.rpmDown) * timeStep;
        if (shiftCut)
            state.rpm = std::max(definition.minimumRpm, state.rpm - adjustment);
        else if (targetRpm >= state.rpm)
            state.rpm = std::min(targetRpm, state.rpm + adjustment);
        else
            state.rpm = std::max(targetRpm, state.rpm - adjustment);

        if ((definition.flags & 4) != 0)
        {
            const float boostTarget =
                audioThrottle * std::clamp((state.rpm - definition.minimumRpm) / (definition.maximumRpm - definition.minimumRpm), 0.0f, 1.0f);
            const float response = std::clamp(timeStep * 0.08f, 0.0f, 1.0f);
            state.turboBoost += (boostTarget - state.turboBoost) * response;
        }
        else
            state.turboBoost = 0.0f;

        const Fmod3DAttributes attributes = ToFmodAttributes(matrix, velocity);
        const BankMetadata&    metadata = state.loadedBank->metadata;
        const float            masterVolume = g_pCore->GetCVars()->GetValue<float>("mastervolume", 1.0f);
        float                  sfxVolume = g_pCore->GetCVars()->GetValue<float>("sfxvolume", 1.0f);
        if (g_pCore->IsWindowMinimized() &&
            (g_pCore->GetCVars()->GetValue<bool>("mute_master_when_minimized") || g_pCore->GetCVars()->GetValue<bool>("mute_sfx_when_minimized")))
            sfxVolume = 0.0f;
        const float eventVolume = interior ? metadata.engineInteriorVolume : metadata.engineExteriorVolume;
        const float competitiveGain = GetCompetitiveEngineGain(vehicle);

        backend.Set3DAttributes(state.engineEvent, attributes);
        backend.SetVolume(state.engineEvent, localOutputMuted ? 0.0f : definition.volume * eventVolume * competitiveGain * masterVolume * sfxVolume);
        backend.SetParameter(state.engineEvent, metadata.rpmParameter, state.rpm);
        backend.SetParameter(state.engineEvent, metadata.throttleParameter, audioThrottle * 2.0f - 1.0f);
        backend.SetParameter(state.engineEvent, metadata.gearParameter, static_cast<float>(state.committedGear));
        backend.SetParameter(state.engineEvent, metadata.turboBoostParameter, state.turboBoost);

        bool        backfire = false;
        bool        highBackfire = false;
        const char* backfireReason = "none";

        if (localDriver && audioMode >= 1 && definition.backfireTimeMs > 0 && (definition.flags & 1) != 0)
        {
            if (audioThrottle == 0.0f)
            {
                if (audioThrottle != state.previousThrottle && TriggerBackfire(state, now, definition.backfireTimeMs))
                {
                    backfire = true;
                    backfireReason = "release";
                    // The original requests RandomInt(1, 2), whose upper bound is exclusive, so the divisor is always one.
                    state.afterfireUntilTick = now + definition.backfireTimeMs;
                }

                if (state.afterfireUntilTick && now > state.gearChangeTick + 500 && now < state.afterfireUntilTick &&
                    TriggerBackfire(state, now, RandomInt(0, definition.backfireTimeMs)))
                {
                    backfire = true;
                    highBackfire = true;
                    backfireReason = "afterfire";
                }
            }
            else
                state.afterfireUntilTick = 0;
        }

        if (gearCommitted)
        {
            const std::string& gearEvent = interior && !metadata.gearInteriorEvent.empty() ? metadata.gearInteriorEvent : metadata.gearExteriorEvent;
            const float        gearVolume = interior ? metadata.gearInteriorVolume : metadata.gearExteriorVolume;
            if (!localOutputMuted)
                backend.PlayOneShot(gearEvent, attributes, definition.volume * gearVolume * competitiveGain * masterVolume * sfxVolume, metadata, state.rpm,
                                    audioThrottle, 1.0f, state.turboBoost);
        }

        if (interior)
            LogTelemetry(vehicle, state, now, speedKmh, rawGear, gearMaximum, gearRevsRatio, rawThrottle, audioThrottle, shiftPhase, shiftElapsed,
                         shiftDuration, targetRpm, gearCommitted, backfire, backfireReason);

        state.previousThrottle = audioThrottle;
        if (backfire)
        {
            PlayBackfire(vehicle, state, highBackfire ? 2u : 1u, audioMode);

            // This synchronous Lua event must remain the final operation that can reference state or definition: its handler is allowed to unload or
            // reload the resource-owned subsystem reentrantly.
            CLuaArguments arguments;
            arguments.PushNumber(highBackfire ? 2.0 : 1.0);
            vehicle.CallEvent("onClientVehicleAudioBackfire", arguments, false);
        }
    }

    bool PlayBackfire(CClientVehicle& vehicle, VehicleState& state, unsigned int mode, unsigned int audioMode)
    {
        if ((mode != 1 && mode != 2) || !state.definition || !state.loadedBank || audioMode < 1 || (state.definition->flags & 1) == 0)
            return false;

        CMatrix matrix;
        CVector velocity;
        if (!vehicle.GetMatrix(matrix))
            return false;
        vehicle.GetMoveSpeed(velocity);

        CClientPlayer* localPlayer = clientManager->GetPlayerManager()->GetLocalPlayer();
        const bool     interior = localPlayer && localPlayer->GetOccupiedVehicle() == &vehicle;
        const bool     localOutputMuted = audioMode == 3 && interior && localPlayer->GetOccupiedVehicleSeat() == 0;
        const auto&    definition = *state.definition;
        const auto&    metadata = state.loadedBank->metadata;
        const auto     attributes = ToFmodAttributes(matrix, velocity);
        const float    masterVolume = g_pCore->GetCVars()->GetValue<float>("mastervolume", 1.0f);
        float          sfxVolume = g_pCore->GetCVars()->GetValue<float>("sfxvolume", 1.0f);
        if (g_pCore->IsWindowMinimized() &&
            (g_pCore->GetCVars()->GetValue<bool>("mute_master_when_minimized") || g_pCore->GetCVars()->GetValue<bool>("mute_sfx_when_minimized")))
            sfxVolume = 0.0f;

        const std::string& eventPath = interior && !metadata.backfireInteriorEvent.empty() ? metadata.backfireInteriorEvent : metadata.backfireExteriorEvent;
        const float        eventVolume = interior ? metadata.backfireInteriorVolume : metadata.backfireExteriorVolume;
        if (!localOutputMuted)
            backend.PlayOneShot(eventPath, attributes, definition.volume * eventVolume * GetCompetitiveBackfireGain(vehicle) * masterVolume * sfxVolume,
                                metadata, state.rpm, state.smoothedThrottle, static_cast<float>(mode), state.turboBoost);

        if (audioMode >= 2 && (definition.flags & 2) != 0)
            SpawnBackfireEffects(vehicle, mode == 2);
        return true;
    }

    CClientEffect* CreateBackfireEffect(CClientVehicle& vehicle, const CVector& localPosition, bool highBackfire)
    {
        CMatrix vehicleMatrix;
        if (!vehicle.GetMatrix(vehicleMatrix))
            return nullptr;

        const CVector         worldPosition = vehicleMatrix.TransformVector(localPosition);
        CClientEffectManager* effectManager = clientManager->GetEffectManager();
        CClientEffect*        effect = nullptr;
        if (highBackfire)
            effect = effectManager->Create("backfire_high", worldPosition, INVALID_ELEMENT_ID, false);
        if (!effect)
            effect = effectManager->Create("backfire", worldPosition, INVALID_ELEMENT_ID, false);
        if (!effect)
            effect = effectManager->Create("gunflash", worldPosition, INVALID_ELEMENT_ID, false);
        if (!effect)
            return nullptr;

        CMatrix attachedMatrix;
        // Store VehFuncs' raw RenderWare 180-degree X rotation in right/up/at field order. CClientEffect converts it through MTA's public matrix convention
        // after composing it with the live vehicle matrix.
        attachedMatrix.vRight = CVector(1.0f, 0.0f, 0.0f);
        attachedMatrix.vFront = CVector(0.0f, -1.0f, 0.0f);
        attachedMatrix.vUp = CVector(0.0f, 0.0f, -1.0f);
        attachedMatrix.vPos = localPosition;
        effect->SetAttachedMatrixOffset(attachedMatrix);
        effect->AttachTo(&vehicle);
        effect->DoAttaching();

        // The vanilla gunflash fallback advertises a 20-metre cull distance. That is appropriate for firearms but makes a relayed vehicle backfire
        // disappear long before an opponent is close enough to appreciate it.
        effect->SetDrawDistance(120.0f);

        // Keep the original GTA/VehFuncs particle texture, but make each burst substantially denser so it remains readable during a race. Mode 2 keeps a
        // clearly stronger silhouette without adding a second rendering path or any idle-frame work.
        const float minimumDensity = highBackfire ? 1.4f : 0.55f;
        const float densityRange = highBackfire ? 0.4f : 0.25f;
        const float random = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
        effect->SetEffectDensity(minimumDensity + densityRange * random);
        return effect;
    }

    void SpawnBackfireEffects(CClientVehicle& vehicle, bool highBackfire)
    {
        // The temporary MTA effect remains attached until GTA finishes PlayAndKill. This mirrors VehFuncs' local exhaust transform without retaining a raw
        // pointer to a native vehicle matrix that can disappear during streaming.

        const std::array<VehicleDummies, 2> exhausts{VehicleDummies::EXHAUST, VehicleDummies::EXHAUST_SECONDARY};
        const CHandlingEntry*               handling = vehicle.GetHandlingData();
        const bool                          doubleExhaust = handling && (handling->GetModelFlags() & MODELFLAGS_DOUBLE_EXHAUST) != 0;
        CVector                             firstExhaust;
        for (std::size_t index = 0; index < exhausts.size(); ++index)
        {
            if (index == 1 && !doubleExhaust)
                continue;

            CVector localPosition;
            if (!vehicle.GetDummyPosition(exhausts[index], localPosition))
                continue;
            if (localPosition == CVector())
                continue;
            if (index == 0)
                firstExhaust = localPosition;
            else if (localPosition == firstExhaust)
                continue;

            CreateBackfireEffect(vehicle, localPosition, highBackfire);
        }
    }

    void StopVehicle(CClientVehicle* vehicle, VehicleState& state)
    {
        backend.StopAndRelease(state.engineEvent);
        if (vehicle)
            RestoreNativeAudio(*vehicle, state);
        if (state.definition)
            ReleaseBank(*state.definition, state.loadedBank);
    }

    bool PlayBackfire(CResource* resource, CClientVehicle* vehicle, unsigned int mode)
    {
        if (!owner || owner != resource || !vehicle || (mode != 1 && mode != 2))
            return false;
        const auto state = vehicles.find(vehicle);
        if (state == vehicles.end())
            return false;
        return PlayBackfire(*vehicle, state->second, mode, GetVehicleAudioMode(*vehicle));
    }

    void StopAll()
    {
        CClientVehicleManager* vehicleManager = clientManager ? clientManager->GetVehicleManager() : nullptr;
        for (auto& [vehicle, state] : vehicles)
        {
            const bool exists = vehicleManager && vehicleManager->Exists(vehicle);
            StopVehicle(exists ? vehicle : nullptr, state);
        }
        vehicles.clear();
        for (auto& [name, bank] : banks)
            backend.UnloadBank(bank.bank);
        banks.clear();
    }

    CClientManager*                         clientManager = nullptr;
    CResource*                              owner = nullptr;
    SString                                 runtimeRoot;
    SString                                 contentRoot;
    SString                                 configPath;
    bool                                    ready = false;
    unsigned long long                      nextInitializationTick = 0;
    FmodBackend                             backend;
    std::map<unsigned int, SoundDefinition> definitions;
    std::map<std::string, BankAsset>        bankAssets;
    std::map<std::string, LoadedBank>       banks;
    std::map<CClientVehicle*, VehicleState> vehicles;
    CClientVehicle*                         competitiveLocalVehicle = nullptr;
    CClientVehicle*                         competitiveRivalVehicle = nullptr;
    CClientVehicle*                         competitiveDuckedVehicle = nullptr;
    float                                   competitiveOriginalEngineVolumeOffset = 0.0f;
    float                                   competitiveMixAmount = 0.0f;
    unsigned long long                      competitiveMixTick = 0;
    std::ofstream                           telemetry;
};

CVehicleSoundManager::CVehicleSoundManager(CClientManager* clientManager) : m_impl(std::make_unique<Impl>(clientManager))
{
}

CVehicleSoundManager::~CVehicleSoundManager() = default;

void CVehicleSoundManager::DoPulse()
{
    m_impl->DoPulse();
}

bool CVehicleSoundManager::IsReplacingAudio(const CClientVehicle* vehicle) const
{
    const auto iterator = m_impl->vehicles.find(const_cast<CClientVehicle*>(vehicle));
    return iterator != m_impl->vehicles.end() && iterator->second.engineEvent != nullptr;
}

bool CVehicleSoundManager::LoadServerConfig(CResource* owner, const SString& configPath)
{
    return m_impl->LoadConfig(owner, configPath);
}

bool CVehicleSoundManager::ReloadServerConfig(CResource* owner)
{
    return m_impl->ReloadConfig(owner);
}

bool CVehicleSoundManager::UnloadServerConfig(CResource* owner)
{
    return m_impl->UnloadConfig(owner);
}

bool CVehicleSoundManager::PlayBackfire(CResource* owner, CClientVehicle* vehicle, unsigned int mode)
{
    return m_impl->PlayBackfire(owner, vehicle, mode);
}
