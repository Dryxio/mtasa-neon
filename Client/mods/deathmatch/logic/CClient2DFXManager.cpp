/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.x / Neon
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/mods/deathmatch/logic/CClient2DFXManager.cpp
 *  PURPOSE:     Resource-owned model 2DFX state and native integration
 *
 *****************************************************************************/
#include "StdInc.h"
#include "CClient2DFXManager.h"
#include "CClientDummy.h"
#include "CResource.h"
#include "../../../game_sa/CEntitySA.h"
#include "../../../game_sa/CModelInfoSA.h"
#include "../../../game_sa/HookSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace
{
constexpr std::uintptr_t ADDRESS_2DFX_STORE = 0xB4C2D8;
constexpr std::uintptr_t ADDRESS_2DFX_PLUGIN_OFFSET = 0xC3A1E0;
constexpr std::uintptr_t ADDRESS_GET_2DFX = 0x4C4C70;
constexpr std::uintptr_t ADDRESS_GET_2DFX_HOOK = 0x4C4CDC;
constexpr std::uintptr_t ADDRESS_POST_CREATE_EFFECTS_HOOK = 0x533BAE;
constexpr std::uintptr_t ADDRESS_FX = 0xA9AE00;
constexpr std::uintptr_t ADDRESS_FX_DESTROY_ENTITY = 0x4A1280;
constexpr std::uintptr_t ADDRESS_ESCALATORS = 0xC6E9A8;
constexpr std::uintptr_t ADDRESS_ESCALATOR_SWITCH_OFF = 0x717860;
constexpr std::uintptr_t ADDRESS_TXD_PUSH = 0x7316A0;
constexpr std::uintptr_t ADDRESS_TXD_POP = 0x7316B0;
constexpr std::uintptr_t ADDRESS_TXD_FIND = 0x731850;
constexpr std::uintptr_t ADDRESS_TXD_SET = 0x7319C0;
constexpr std::uintptr_t ADDRESS_RW_TEXTURE_READ = 0x7F3AC0;
constexpr std::uintptr_t ADDRESS_RW_TEXTURE_DESTROY = 0x7F3820;
constexpr std::uintptr_t ADDRESS_RW_FRAME_DESTROY = 0x7F05A0;
constexpr std::uintptr_t ADDRESS_RP_ATOMIC_SET_FRAME = 0x74BF20;
constexpr std::uintptr_t ADDRESS_RP_ATOMIC_DESTROY = 0x749DC0;

constexpr std::size_t MAX_NATIVE_EFFECTS = 255;
constexpr std::size_t PARTICLE_NAME_CAPACITY = 24;
constexpr std::size_t ROADSIGN_LINE_CAPACITY = 16;

std::uint32_t PackColor(const RwColor& color)
{
    return (static_cast<std::uint32_t>(color.a) << 24) | (static_cast<std::uint32_t>(color.r) << 16) |
           (static_cast<std::uint32_t>(color.g) << 8) | static_cast<std::uint32_t>(color.b);
}

RwColor UnpackColor(std::uint32_t color)
{
    return RwColor{static_cast<std::uint8_t>((color >> 16) & 0xFF), static_cast<std::uint8_t>((color >> 8) & 0xFF),
                   static_cast<std::uint8_t>(color & 0xFF), static_cast<std::uint8_t>((color >> 24) & 0xFF)};
}

std::string BoundedString(const char* value, std::size_t capacity)
{
    if (!value)
        return {};
    return std::string(value, strnlen(value, capacity));
}

bool CopyParticleName(char (&destination)[PARTICLE_NAME_CAPACITY], const std::string& value)
{
    if (value.size() >= PARTICLE_NAME_CAPACITY)
        return false;
    std::memset(destination, 0, sizeof(destination));
    std::memcpy(destination, value.data(), value.size());
    return true;
}

bool CopyRoadsignLine(char* destination, const std::string& value)
{
    if (!destination || value.size() > ROADSIGN_LINE_CAPACITY)
        return false;
    std::memset(destination, 0, ROADSIGN_LINE_CAPACITY);
    std::memcpy(destination, value.data(), value.size());
    return true;
}

RwTexture* LoadParticleTexture(const std::string& name)
{
    if (name.empty())
        return nullptr;

    using PushTxd = void(__cdecl*)();
    using PopTxd = void(__cdecl*)();
    using FindTxd = std::int32_t(__cdecl*)(const char*);
    using SetTxd = void(__cdecl*)(std::int32_t);
    using ReadTexture = RwTexture*(__cdecl*)(const char*, const char*);

    reinterpret_cast<PushTxd>(ADDRESS_TXD_PUSH)();
    const std::int32_t txd = reinterpret_cast<FindTxd>(ADDRESS_TXD_FIND)("particle");
    if (txd >= 0)
        reinterpret_cast<SetTxd>(ADDRESS_TXD_SET)(txd);
    RwTexture* texture = reinterpret_cast<ReadTexture>(ADDRESS_RW_TEXTURE_READ)(name.c_str(), nullptr);
    reinterpret_cast<PopTxd>(ADDRESS_TXD_POP)();
    return texture;
}

void DestroyTexture(RwTexture*& texture)
{
    if (!texture)
        return;
    reinterpret_cast<void(__cdecl*)(RwTexture*)>(ADDRESS_RW_TEXTURE_DESTROY)(texture);
    texture = nullptr;
}

bool ReplaceTexture(RwTexture*& texture, const std::string& name)
{
    if (texture && BoundedString(texture->name, RW_TEXTURE_NAME_LENGTH) == name)
        return true;
    if (!texture && name.empty())
        return true;

    // Load first. A bad texture name must not invalidate the live effect.
    RwTexture* replacement = name.empty() ? nullptr : LoadParticleTexture(name);
    if (!name.empty() && !replacement)
        return false;

    RwTexture* previous = texture;
    texture = replacement;
    if (previous)
        reinterpret_cast<void(__cdecl*)(RwTexture*)>(ADDRESS_RW_TEXTURE_DESTROY)(previous);
    return true;
}
}

struct C2DFXNativeLight
{
    RwColor color;
    float coronaFarClip;
    float pointLightRange;
    float coronaSize;
    float shadowSize;
    std::uint16_t flags;
    e2dCoronaFlashType flashType;
    bool coronaReflection;
    std::uint8_t flareType;
    std::uint8_t shadowMultiplier;
    std::int8_t shadowDistance;
    std::int8_t offsetX;
    std::int8_t offsetY;
    std::int8_t offsetZ;
    std::uint8_t padding[2];
    RwTexture* coronaTexture;
    RwTexture* shadowTexture;
    std::int32_t field28;
    std::int32_t field2C;
};
static_assert(sizeof(C2DFXNativeLight) == 0x30, "Unexpected GTA 2DFX light layout");

struct C2DFXNativeParticle
{
    char name[PARTICLE_NAME_CAPACITY];
};
static_assert(sizeof(C2DFXNativeParticle) == 0x18, "Unexpected GTA 2DFX particle layout");

struct C2DFXNativeRoadsign
{
    RwV2d size;
    RwV3d rotation;
    std::uint8_t flags;
    std::uint8_t padding[3];
    char* text;
    RpAtomic* atomic;
};
static_assert(sizeof(C2DFXNativeRoadsign) == 0x20, "Unexpected GTA 2DFX roadsign layout");

struct C2DFXNativeEscalator
{
    RwV3d bottom;
    RwV3d top;
    RwV3d end;
    std::uint8_t direction;
    std::uint8_t padding[3];
};
static_assert(sizeof(C2DFXNativeEscalator) == 0x28, "Unexpected GTA 2DFX escalator layout");

union C2DFXNativePayload
{
    C2DFXNativeLight light;
    C2DFXNativeParticle particle;
    C2DFXNativeRoadsign roadsign;
    C2DFXNativeEscalator escalator;
    std::uint8_t raw[0x30];
};

struct C2DFXNativeEffect
{
    CVector position;
    e2dEffectType type;
    std::uint8_t padding[3];
    C2DFXNativePayload payload;
};
static_assert(sizeof(C2DFXNativeEffect) == 0x40, "Unexpected GTA C2dEffect layout");

namespace
{
struct C2DFXNativeStore
{
    std::uint32_t count;
    C2DFXNativeEffect effects[100];
};

struct C2DFXPluginData
{
    std::uint32_t count;
    C2DFXNativeEffect effects[1];
};

struct CEscalatorNative
{
    RwV3d start;
    RwV3d bottom;
    RwV3d top;
    RwV3d end;
    std::uint8_t matrix[72];
    bool exists;
    bool objectCreated;
    bool moveDown;
    std::uint8_t padding7B;
    std::int32_t intermediatePlanes;
    std::uint32_t bottomPlanes;
    std::uint32_t topPlanes;
    std::uint8_t padding88[8];
    RwSphere bounding;
    float currentPosition;
    CEntitySAInterface* entity;
    void* objects[42];
};
static_assert(sizeof(CEscalatorNative) == 0x150, "Unexpected GTA escalator layout");

C2DFXNativeEffect g_removedEffect{};

C2DFXNativeEffect* NativeGetEffect(CBaseModelInfoSAInterface* modelInfo, std::uint32_t index)
{
    if (!modelInfo)
        return nullptr;
    using Get2DFX = C2DFXNativeEffect*(__thiscall*)(CBaseModelInfoSAInterface*, std::uint32_t);
    return reinterpret_cast<Get2DFX>(ADDRESS_GET_2DFX)(modelInfo, index);
}

void DestroyRoadsignAtomic(C2DFXNativeEffect* effect)
{
    if (!effect || effect->type != e2dEffectType::ROADSIGN || !effect->payload.roadsign.atomic)
        return;

    RpAtomic* atomic = effect->payload.roadsign.atomic;
    RwFrame* frame = RpAtomicGetFrame(atomic);
    if (frame)
    {
        reinterpret_cast<void(__cdecl*)(RpAtomic*, RwFrame*)>(ADDRESS_RP_ATOMIC_SET_FRAME)(atomic, nullptr);
        reinterpret_cast<void(__cdecl*)(RwFrame*)>(ADDRESS_RW_FRAME_DESTROY)(frame);
    }
    reinterpret_cast<void(__cdecl*)(RpAtomic*)>(ADDRESS_RP_ATOMIC_DESTROY)(atomic);
    effect->payload.roadsign.atomic = nullptr;
}

void SetRoadsignColor(C2DFXNativeEffect* effect, std::uint32_t packedColor)
{
    if (!effect || effect->type != e2dEffectType::ROADSIGN || !effect->payload.roadsign.atomic)
        return;
    RpGeometry* geometry = effect->payload.roadsign.atomic->geometry;
    if (!geometry)
        return;

    const RwColor color = UnpackColor(packedColor);
    for (std::int32_t index = 0; index < geometry->materials.entries; ++index)
    {
        RpMaterial* material = geometry->materials.materials[index];
        if (material)
            material->color = color;
    }
}

void DestroyParticleFxForModel(std::uint32_t model)
{
    auto* fx = reinterpret_cast<std::uint8_t*>(ADDRESS_FX);
    void* node = *reinterpret_cast<void**>(fx + 0x44);
    std::vector<CEntitySAInterface*> entities;

    while (node)
    {
        void* previous = *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(node) + 0x4);
        CEntitySAInterface* entity = *reinterpret_cast<CEntitySAInterface**>(reinterpret_cast<std::uint8_t*>(node) + 0xC);
        if (entity && entity->m_nModelIndex == model && std::find(entities.begin(), entities.end(), entity) == entities.end())
            entities.push_back(entity);
        node = previous;
    }

    using DestroyEntityFx = void(__thiscall*)(void*, CEntitySAInterface*);
    for (CEntitySAInterface* entity : entities)
        reinterpret_cast<DestroyEntityFx>(ADDRESS_FX_DESTROY_ENTITY)(reinterpret_cast<void*>(ADDRESS_FX), entity);
}

void DisableEscalatorsForModel(std::uint32_t model)
{
    auto* escalators = reinterpret_cast<CEscalatorNative*>(ADDRESS_ESCALATORS);
    using SwitchOff = void(__thiscall*)(CEscalatorNative*);
    for (std::size_t index = 0; index < 32; ++index)
    {
        auto& escalator = escalators[index];
        if (!escalator.exists || !escalator.entity || escalator.entity->m_nModelIndex != model)
            continue;
        reinterpret_cast<SwitchOff>(ADDRESS_ESCALATOR_SWITCH_OFF)(&escalator);
        escalator.exists = false;
    }
}

bool ReadEffectData(C2DFXNativeEffect* effect, S2DFXData& out)
{
    if (!effect)
        return false;
    out = {};
    out.position = effect->position;

    switch (effect->type)
    {
        case e2dEffectType::LIGHT:
        {
            const auto& light = effect->payload.light;
            out.drawDistance = light.coronaFarClip;
            out.lightRange = light.pointLightRange;
            out.coronaSize = light.coronaSize;
            out.shadowSize = light.shadowSize;
            out.shadowMultiplier = light.shadowMultiplier;
            out.shadowDistance = light.shadowDistance;
            out.flashType = light.flashType;
            out.coronaReflection = light.coronaReflection;
            out.flareType = light.flareType;
            out.offset = CVector(static_cast<float>(light.offsetX), static_cast<float>(light.offsetY), static_cast<float>(light.offsetZ));
            out.color = PackColor(light.color);
            out.flags = light.flags;
            out.coronaName = light.coronaTexture ? BoundedString(light.coronaTexture->name, RW_TEXTURE_NAME_LENGTH) : std::string{};
            out.shadowName = light.shadowTexture ? BoundedString(light.shadowTexture->name, RW_TEXTURE_NAME_LENGTH) : std::string{};
            return true;
        }
        case e2dEffectType::PARTICLE:
            out.particleName = BoundedString(effect->payload.particle.name, PARTICLE_NAME_CAPACITY);
            return true;
        case e2dEffectType::ROADSIGN:
        {
            const auto& roadsign = effect->payload.roadsign;
            out.size = CVector(roadsign.size.x, roadsign.size.y, 0.0f);
            out.rotation = CVector(roadsign.rotation.x, roadsign.rotation.y, roadsign.rotation.z);
            out.flags = roadsign.flags;
            if (roadsign.text)
            {
                for (std::size_t line = 0; line < 4; ++line)
                    out.text[line] = BoundedString(roadsign.text + line * ROADSIGN_LINE_CAPACITY, ROADSIGN_LINE_CAPACITY);
            }
            if (roadsign.atomic && roadsign.atomic->geometry && roadsign.atomic->geometry->materials.entries > 0 && roadsign.atomic->geometry->materials.materials[0])
                out.color = PackColor(roadsign.atomic->geometry->materials.materials[0]->color);
            return true;
        }
        case e2dEffectType::ESCALATOR:
            out.bottom = CVector(effect->payload.escalator.bottom.x, effect->payload.escalator.bottom.y, effect->payload.escalator.bottom.z);
            out.top = CVector(effect->payload.escalator.top.x, effect->payload.escalator.top.y, effect->payload.escalator.top.z);
            out.end = CVector(effect->payload.escalator.end.x, effect->payload.escalator.end.y, effect->payload.escalator.end.z);
            out.direction = effect->payload.escalator.direction;
            return true;
        case e2dEffectType::SUN_GLARE:
            return true;
        default:
            return false;
    }
}

bool ApplyEffectData(C2DFXNativeEffect* effect, const S2DFXData& data)
{
    if (!effect)
        return false;
    effect->position = data.position;

    switch (effect->type)
    {
        case e2dEffectType::LIGHT:
        {
            auto& light = effect->payload.light;
            if (!ReplaceTexture(light.coronaTexture, data.coronaName))
                return false;
            if (!ReplaceTexture(light.shadowTexture, data.shadowName))
                return false;
            light.color = UnpackColor(data.color);
            light.coronaFarClip = data.drawDistance;
            light.pointLightRange = data.lightRange;
            light.coronaSize = data.coronaSize;
            light.shadowSize = data.shadowSize;
            light.flags = data.flags;
            light.flashType = data.flashType;
            light.coronaReflection = data.coronaReflection;
            light.flareType = data.flareType;
            light.shadowMultiplier = data.shadowMultiplier;
            light.shadowDistance = data.shadowDistance;
            light.offsetX = static_cast<std::int8_t>(data.offset.fX);
            light.offsetY = static_cast<std::int8_t>(data.offset.fY);
            light.offsetZ = static_cast<std::int8_t>(data.offset.fZ);
            return true;
        }
        case e2dEffectType::PARTICLE:
            return CopyParticleName(effect->payload.particle.name, data.particleName);
        case e2dEffectType::ROADSIGN:
        {
            auto& roadsign = effect->payload.roadsign;
            if (!roadsign.text)
                return false;
            roadsign.size = RwV2d{data.size.fX, data.size.fY};
            roadsign.rotation = RwV3d{data.rotation.fX, data.rotation.fY, data.rotation.fZ};
            roadsign.flags = static_cast<std::uint8_t>(data.flags & 0xFF);
            for (std::size_t line = 0; line < 4; ++line)
            {
                if (!CopyRoadsignLine(roadsign.text + line * ROADSIGN_LINE_CAPACITY, data.text[line]))
                    return false;
            }
            SetRoadsignColor(effect, data.color);
            return true;
        }
        case e2dEffectType::ESCALATOR:
            effect->payload.escalator.bottom = RwV3d{data.bottom.fX, data.bottom.fY, data.bottom.fZ};
            effect->payload.escalator.top = RwV3d{data.top.fX, data.top.fY, data.top.fZ};
            effect->payload.escalator.end = RwV3d{data.end.fX, data.end.fY, data.end.fZ};
            effect->payload.escalator.direction = data.direction;
            return true;
        case e2dEffectType::SUN_GLARE:
            return true;
        default:
            return false;
    }
}

void DestroyCustomResources(C2DFXNativeEffect* effect)
{
    if (!effect)
        return;
    if (effect->type == e2dEffectType::LIGHT)
    {
        DestroyTexture(effect->payload.light.coronaTexture);
        DestroyTexture(effect->payload.light.shadowTexture);
    }
    else if (effect->type == e2dEffectType::ROADSIGN)
        DestroyRoadsignAtomic(effect);
}

void PrepareVisualChange(std::uint32_t model, C2DFXNativeEffect* effect)
{
    if (!effect)
        return;
    switch (effect->type)
    {
        case e2dEffectType::PARTICLE:
            DestroyParticleFxForModel(model);
            break;
        case e2dEffectType::ROADSIGN:
            DestroyRoadsignAtomic(effect);
            break;
        case e2dEffectType::ESCALATOR:
            DisableEscalatorsForModel(model);
            break;
        default:
            break;
    }
}

void RestreamModel(std::uint32_t model)
{
    if (g_pClientGame)
        g_pClientGame->RestreamModel(static_cast<std::uint16_t>(model));
}

class C2DFXResourceTracker final : public CClientDummy
{
public:
    C2DFXResourceTracker(CClientManager* manager, CResource* owner) : CClientDummy(manager, INVALID_ELEMENT_ID, "2dfxtracker"), m_owner(owner) {}
    ~C2DFXResourceTracker() { CClient2DFXManager::GetSingleton().ReleaseResource(m_owner); }

private:
    CResource* m_owner{};
};
}

struct CClient2DFXManager::SImpl
{
    struct SOverride
    {
        CResource* owner{};
        S2DFXData data{};
        std::uint64_t sequence{};
    };

    struct SOriginal
    {
        S2DFXData baseline{};
        S2DFXData effective{};
        bool captured{};
        std::map<e2dEffectProperty, std::vector<SOverride>> overrides;
        std::unordered_set<CResource*> removers;
    };

    struct SCustom
    {
        CResource* owner{};
        std::unique_ptr<C2DFXNativeEffect> effect;
        std::array<char, 64> roadsignText{};
        S2DFXData baseline{};
        S2DFXData current{};
    };

    struct SModel
    {
        std::uint32_t model{};
        CBaseModelInfoSAInterface* native{};
        std::unordered_map<std::uint32_t, SOriginal> originals;
        std::vector<std::unique_ptr<SCustom>> customs;
    };

    CClientManager* manager{};
    bool hooksInstalled{};
    std::uint64_t nextSequence{1};
    std::unordered_map<CBaseModelInfoSAInterface*, SModel> models;
    std::unordered_set<CResource*> trackedResources;
};

CClient2DFXManager::~CClient2DFXManager() = default;

namespace
{
CBaseModelInfoSAInterface* GetModelInterface(std::uint32_t model)
{
    CModelInfo* info = g_pGame ? g_pGame->GetModelInfo(model) : nullptr;
    return info ? info->GetInterface() : nullptr;
}

CClient2DFXManager::SImpl::SModel* FindModel(CClient2DFXManager::SImpl* impl, std::uint32_t model)
{
    if (!impl)
        return nullptr;
    CBaseModelInfoSAInterface* native = GetModelInterface(model);
    if (!native)
        return nullptr;
    auto iterator = impl->models.find(native);
    return iterator == impl->models.end() ? nullptr : &iterator->second;
}

CClient2DFXManager::SImpl::SModel& EnsureModel(CClient2DFXManager::SImpl* impl, std::uint32_t model)
{
    CBaseModelInfoSAInterface* native = GetModelInterface(model);
    auto [iterator, inserted] = impl->models.try_emplace(native);
    if (inserted)
    {
        iterator->second.model = model;
        iterator->second.native = native;
    }
    return iterator->second;
}

std::uint32_t NativeCount(const CClient2DFXManager::SImpl::SModel& state)
{
    const std::uint32_t total = state.native ? state.native->ucNumOf2DEffects : 0;
    const std::uint32_t custom = static_cast<std::uint32_t>(state.customs.size());
    return total >= custom ? total - custom : 0;
}

bool IsRemoved(const CClient2DFXManager::SImpl::SModel& model, std::uint32_t index)
{
    auto iterator = model.originals.find(index);
    return iterator != model.originals.end() && !iterator->second.removers.empty();
}

bool EnsureTracked(CClient2DFXManager::SImpl* impl, CResource* owner)
{
    if (!impl || !impl->manager || !owner)
        return false;
    if (impl->trackedResources.contains(owner))
        return true;

    auto* tracker = new C2DFXResourceTracker(impl->manager, owner);
    tracker->SetParent(owner->GetResourceDynamicEntity());
    if (CElementGroup* group = owner->GetElementGroup())
        group->Add(tracker);
    impl->trackedResources.insert(owner);
    return true;
}

bool CaptureOriginal(CClient2DFXManager::SImpl::SModel& model, std::uint32_t index)
{
    auto& original = model.originals[index];
    if (original.captured)
        return true;
    C2DFXNativeEffect* effect = NativeGetEffect(model.native, index);
    if (!effect || effect->type == e2dEffectType::UNKNOWN)
        return false;
    if (!ReadEffectData(effect, original.baseline))
        return false;
    original.effective = original.baseline;
    original.captured = true;
    return true;
}

S2DFXData BuildEffective(const CClient2DFXManager::SImpl::SOriginal& original)
{
    S2DFXData effective = original.baseline;
    for (const auto& [property, stack] : original.overrides)
    {
        if (!stack.empty())
            CClient2DFXManager::CopyProperty(effective, stack.back().data, property);
    }
    return effective;
}

bool ApplyOriginal(CClient2DFXManager::SImpl::SModel& model, std::uint32_t index)
{
    auto iterator = model.originals.find(index);
    if (iterator == model.originals.end() || !iterator->second.captured)
        return true;
    auto& original = iterator->second;
    original.effective = BuildEffective(original);
    if (!original.removers.empty())
        return true;
    C2DFXNativeEffect* effect = NativeGetEffect(model.native, index);
    return effect && effect->type != e2dEffectType::UNKNOWN && ApplyEffectData(effect, original.effective);
}

C2DFXNativeEffect* __cdecl Resolve2DFXHook(CBaseModelInfoSAInterface* modelInfo, void* geometry, std::uint32_t pluginCount, std::uint32_t index)
{
    return CClient2DFXManager::GetSingleton().ResolveNativeEffect(modelInfo, geometry, pluginCount, index);
}

void __declspec(naked) HookGet2DFX()
{
    __asm
    {
        push esi
        push eax
        push edi
        push ebx
        call Resolve2DFXHook
        add esp, 16
        pop edi
        pop esi
        pop ebp
        pop ebx
        retn 4
    }
    MTA_VERIFY_HOOK_LOCAL_SIZE;
}

void __cdecl NotifyEntityEffectsCreated(CEntitySAInterface* entity)
{
    CClient2DFXManager::GetSingleton().OnEntityEffectsCreated(entity);
}

void __declspec(naked) HookPostCreateEffects()
{
    __asm
    {
        push ebp
        call NotifyEntityEffectsCreated
        add esp, 4
        pop edi
        pop ebp
        pop ebx
        add esp, 0C0h
        retn
    }
    MTA_VERIFY_HOOK_LOCAL_SIZE;
}
}

CClient2DFXManager& CClient2DFXManager::GetSingleton()
{
    static CClient2DFXManager manager;
    return manager;
}

void CClient2DFXManager::Initialize(CClientManager* manager)
{
    if (!m_impl)
        m_impl = std::make_unique<SImpl>();
    m_impl->manager = manager;
    if (!m_impl->hooksInstalled)
    {
        HookInstall(ADDRESS_GET_2DFX_HOOK, HookGet2DFX, 10);
        HookInstall(ADDRESS_POST_CREATE_EFFECTS_HOOK, HookPostCreateEffects, 10);
        g_removedEffect = {};
        g_removedEffect.type = e2dEffectType::UNKNOWN;
        m_impl->hooksInstalled = true;
    }
}

bool CClient2DFXManager::IsValidModel(std::uint32_t model) const
{
    return model <= std::numeric_limits<std::uint16_t>::max() && GetModelInterface(model) != nullptr;
}

bool CClient2DFXManager::IsModelLoaded(std::uint32_t model) const
{
    CModelInfo* info = g_pGame ? g_pGame->GetModelInfo(model) : nullptr;
    return info && info->GetInterface() && info->IsLoaded();
}

std::uint32_t CClient2DFXManager::GetCount(std::uint32_t model, bool includeCustom) const
{
    CBaseModelInfoSAInterface* native = GetModelInterface(model);
    if (!native)
        return 0;
    if (!m_impl)
        return native->ucNumOf2DEffects;
    auto iterator = m_impl->models.find(native);
    if (iterator == m_impl->models.end() || includeCustom)
        return native->ucNumOf2DEffects;
    return NativeCount(iterator->second);
}

e2dEffectType CClient2DFXManager::GetType(std::uint32_t model, std::uint32_t index) const
{
    CBaseModelInfoSAInterface* native = GetModelInterface(model);
    if (!native || index >= GetCount(model, true))
        return e2dEffectType::NONE;
    C2DFXNativeEffect* effect = NativeGetEffect(native, index);
    return effect ? effect->type : e2dEffectType::NONE;
}

bool CClient2DFXManager::GetData(std::uint32_t model, std::uint32_t index, S2DFXData& out) const
{
    CBaseModelInfoSAInterface* native = GetModelInterface(model);
    if (!native || index >= GetCount(model, true))
        return false;
    C2DFXNativeEffect* effect = NativeGetEffect(native, index);
    if (!effect || effect->type == e2dEffectType::UNKNOWN)
        return false;
    if (!ReadEffectData(effect, out))
        return false;

    if (!m_impl)
        return true;
    auto modelIt = m_impl->models.find(native);
    if (modelIt == m_impl->models.end())
        return true;

    const std::uint32_t nativeCount = NativeCount(modelIt->second);
    if (index < nativeCount)
    {
        auto originalIt = modelIt->second.originals.find(index);
        if (originalIt != modelIt->second.originals.end() && originalIt->second.captured)
            out.color = originalIt->second.effective.color;
    }
    else
    {
        const std::size_t customIndex = index - nativeCount;
        if (customIndex < modelIt->second.customs.size())
            out = modelIt->second.customs[customIndex]->current;
    }
    return true;
}

bool CClient2DFXManager::Add(CResource* owner, std::uint32_t model, e2dEffectType type, const S2DFXData& data)
{
    if (!m_impl || !EnsureTracked(m_impl.get(), owner) || !IsModelLoaded(model))
        return false;
    if (type != e2dEffectType::LIGHT && type != e2dEffectType::PARTICLE && type != e2dEffectType::SUN_GLARE && type != e2dEffectType::ROADSIGN &&
        type != e2dEffectType::ESCALATOR)
        return false;

    auto& state = EnsureModel(m_impl.get(), model);
    if (!state.native || state.native->ucNumOf2DEffects >= MAX_NATIVE_EFFECTS)
        return false;

    auto custom = std::make_unique<SImpl::SCustom>();
    custom->owner = owner;
    custom->effect = std::make_unique<C2DFXNativeEffect>();
    custom->effect->type = type;
    custom->baseline = data;
    custom->current = data;
    if (type == e2dEffectType::ROADSIGN)
        custom->effect->payload.roadsign.text = custom->roadsignText.data();

    if (!ApplyEffectData(custom->effect.get(), data))
    {
        DestroyCustomResources(custom->effect.get());
        return false;
    }

    state.customs.push_back(std::move(custom));
    ++state.native->ucNumOf2DEffects;
    if (type != e2dEffectType::LIGHT && type != e2dEffectType::SUN_GLARE)
        RestreamModel(model);
    return true;
}

bool CClient2DFXManager::Remove(CResource* owner, std::uint32_t model, std::uint32_t index)
{
    if (!m_impl || !EnsureTracked(m_impl.get(), owner))
        return false;
    SImpl::SModel* state = FindModel(m_impl.get(), model);
    if (!state)
    {
        if (!IsModelLoaded(model))
            return false;
        state = &EnsureModel(m_impl.get(), model);
    }

    const std::uint32_t nativeCount = NativeCount(*state);
    if (index >= state->native->ucNumOf2DEffects)
        return false;

    if (index >= nativeCount)
    {
        const std::size_t customIndex = index - nativeCount;
        if (customIndex >= state->customs.size() || state->customs[customIndex]->owner != owner)
            return false;
        C2DFXNativeEffect* effect = state->customs[customIndex]->effect.get();
        const e2dEffectType type = effect->type;
        PrepareVisualChange(model, effect);
        DestroyCustomResources(effect);
        state->customs.erase(state->customs.begin() + customIndex);
        --state->native->ucNumOf2DEffects;
        if (type != e2dEffectType::LIGHT && type != e2dEffectType::SUN_GLARE)
            RestreamModel(model);
        return true;
    }

    if (!CaptureOriginal(*state, index))
        return false;
    auto& original = state->originals[index];
    if (original.removers.contains(owner))
        return true;
    C2DFXNativeEffect* effect = NativeGetEffect(state->native, index);
    PrepareVisualChange(model, effect);
    original.removers.insert(owner);
    RestreamModel(model);
    return true;
}

bool CClient2DFXManager::Restore(CResource* owner, std::uint32_t model, std::uint32_t index)
{
    if (!m_impl || !owner)
        return false;
    SImpl::SModel* state = FindModel(m_impl.get(), model);
    if (!state || index >= NativeCount(*state))
        return false;
    auto iterator = state->originals.find(index);
    if (iterator == state->originals.end() || !iterator->second.removers.contains(owner))
        return false;
    iterator->second.removers.erase(owner);
    ApplyOriginal(*state, index);
    RestreamModel(model);
    return true;
}

bool CClient2DFXManager::SetProperty(CResource* owner, std::uint32_t model, std::uint32_t index, e2dEffectProperty property, const S2DFXData& value)
{
    if (!m_impl || !EnsureTracked(m_impl.get(), owner) || !IsPropertyValid(GetType(model, index), property))
        return false;
    auto* state = FindModel(m_impl.get(), model);
    if (!state)
        state = &EnsureModel(m_impl.get(), model);
    const std::uint32_t nativeCount = NativeCount(*state);
    if (index >= state->native->ucNumOf2DEffects)
        return false;

    if (index >= nativeCount)
    {
        const std::size_t customIndex = index - nativeCount;
        if (customIndex >= state->customs.size() || state->customs[customIndex]->owner != owner)
            return false;
        auto& custom = *state->customs[customIndex];
        S2DFXData next = custom.current;
        CopyProperty(next, value, property);
        C2DFXNativeEffect* effect = custom.effect.get();
        const S2DFXData previous = custom.current;
        if (effect->type != e2dEffectType::LIGHT && effect->type != e2dEffectType::SUN_GLARE)
            PrepareVisualChange(model, effect);
        if (!ApplyEffectData(effect, next))
        {
            ApplyEffectData(effect, previous);
            if (effect->type != e2dEffectType::LIGHT && effect->type != e2dEffectType::SUN_GLARE)
                RestreamModel(model);
            return false;
        }
        custom.current = std::move(next);
        if (effect->type != e2dEffectType::LIGHT && effect->type != e2dEffectType::SUN_GLARE)
            RestreamModel(model);
        return true;
    }

    if (!CaptureOriginal(*state, index) || IsRemoved(*state, index))
        return false;
    auto& original = state->originals[index];
    auto& stack = original.overrides[property];
    const auto previousStack = stack;
    stack.erase(std::remove_if(stack.begin(), stack.end(), [owner](const SImpl::SOverride& entry) { return entry.owner == owner; }), stack.end());
    stack.push_back(SImpl::SOverride{owner, value, m_impl->nextSequence++});

    C2DFXNativeEffect* effect = NativeGetEffect(state->native, index);
    const e2dEffectType type = effect ? effect->type : e2dEffectType::UNKNOWN;
    if (type != e2dEffectType::LIGHT && type != e2dEffectType::SUN_GLARE)
        PrepareVisualChange(model, effect);
    if (!ApplyOriginal(*state, index))
    {
        stack = previousStack;
        ApplyOriginal(*state, index);
        if (type != e2dEffectType::LIGHT && type != e2dEffectType::SUN_GLARE)
            RestreamModel(model);
        return false;
    }
    if (type != e2dEffectType::LIGHT && type != e2dEffectType::SUN_GLARE)
        RestreamModel(model);
    return true;
}

bool CClient2DFXManager::ResetProperty(CResource* owner, std::uint32_t model, std::uint32_t index, e2dEffectProperty property)
{
    if (!m_impl || !owner)
        return false;
    auto* state = FindModel(m_impl.get(), model);
    if (!state)
        return false;
    const std::uint32_t nativeCount = NativeCount(*state);
    if (index >= state->native->ucNumOf2DEffects)
        return false;

    if (index >= nativeCount)
    {
        const std::size_t customIndex = index - nativeCount;
        if (customIndex >= state->customs.size() || state->customs[customIndex]->owner != owner)
            return false;
        auto& custom = *state->customs[customIndex];
        if (!IsPropertyValid(custom.effect->type, property))
            return false;
        S2DFXData next = custom.current;
        CopyProperty(next, custom.baseline, property);
        if (custom.effect->type != e2dEffectType::LIGHT && custom.effect->type != e2dEffectType::SUN_GLARE)
            PrepareVisualChange(model, custom.effect.get());
        if (!ApplyEffectData(custom.effect.get(), next))
            return false;
        custom.current = std::move(next);
        if (custom.effect->type != e2dEffectType::LIGHT && custom.effect->type != e2dEffectType::SUN_GLARE)
            RestreamModel(model);
        return true;
    }

    auto originalIt = state->originals.find(index);
    if (originalIt == state->originals.end() || IsRemoved(*state, index))
        return false;
    auto stackIt = originalIt->second.overrides.find(property);
    if (stackIt == originalIt->second.overrides.end())
        return false;
    auto& stack = stackIt->second;
    const std::size_t oldSize = stack.size();
    stack.erase(std::remove_if(stack.begin(), stack.end(), [owner](const SImpl::SOverride& entry) { return entry.owner == owner; }), stack.end());
    if (stack.size() == oldSize)
        return false;

    C2DFXNativeEffect* effect = NativeGetEffect(state->native, index);
    const e2dEffectType type = effect ? effect->type : e2dEffectType::UNKNOWN;
    if (type != e2dEffectType::LIGHT && type != e2dEffectType::SUN_GLARE)
        PrepareVisualChange(model, effect);
    const bool result = ApplyOriginal(*state, index);
    if (type != e2dEffectType::LIGHT && type != e2dEffectType::SUN_GLARE)
        RestreamModel(model);
    return result;
}

bool CClient2DFXManager::ResetModel(CResource* owner, std::uint32_t model)
{
    if (!m_impl || !owner)
        return false;
    auto* state = FindModel(m_impl.get(), model);
    if (!state)
        return false;

    bool changed = false;
    DestroyParticleFxForModel(model);
    DisableEscalatorsForModel(model);
    for (std::uint32_t index = 0; index < state->native->ucNumOf2DEffects; ++index)
        DestroyRoadsignAtomic(NativeGetEffect(state->native, index));

    for (auto iterator = state->customs.begin(); iterator != state->customs.end();)
    {
        if ((*iterator)->owner == owner)
        {
            DestroyCustomResources((*iterator)->effect.get());
            iterator = state->customs.erase(iterator);
            --state->native->ucNumOf2DEffects;
            changed = true;
        }
        else
            ++iterator;
    }

    for (auto& [index, original] : state->originals)
    {
        changed = original.removers.erase(owner) > 0 || changed;
        for (auto& [property, stack] : original.overrides)
        {
            const std::size_t oldSize = stack.size();
            stack.erase(std::remove_if(stack.begin(), stack.end(), [owner](const SImpl::SOverride& entry) { return entry.owner == owner; }), stack.end());
            changed = stack.size() != oldSize || changed;
        }
        ApplyOriginal(*state, index);
    }

    if (changed)
        RestreamModel(model);
    return changed;
}

void CClient2DFXManager::ReleaseResource(CResource* owner)
{
    if (!m_impl || !owner)
        return;
    std::vector<std::uint32_t> models;
    models.reserve(m_impl->models.size());
    for (const auto& [native, state] : m_impl->models)
        models.push_back(state.model);
    for (std::uint32_t model : models)
        ResetModel(owner, model);
    m_impl->trackedResources.erase(owner);
}

C2DFXNativeEffect* CClient2DFXManager::ResolveNativeEffect(void* modelInfoPointer, void* geometry, std::uint32_t pluginCount, std::uint32_t index)
{
    auto* modelInfo = static_cast<CBaseModelInfoSAInterface*>(modelInfoPointer);
    if (!modelInfo)
        return nullptr;

    SImpl::SModel* state = nullptr;
    if (m_impl)
    {
        auto iterator = m_impl->models.find(modelInfo);
        if (iterator != m_impl->models.end())
            state = &iterator->second;
    }

    const std::uint32_t customCount = state ? static_cast<std::uint32_t>(state->customs.size()) : 0;
    const std::uint32_t totalCount = modelInfo->ucNumOf2DEffects;
    const std::uint32_t nativeCount = totalCount >= customCount ? totalCount - customCount : 0;
    pluginCount = std::min(pluginCount, nativeCount);
    const std::uint32_t storedCount = nativeCount - pluginCount;

    C2DFXNativeEffect* result = nullptr;
    if (index < storedCount)
    {
        if (modelInfo->s2DEffectIndex < 0)
            return nullptr;
        auto* store = reinterpret_cast<C2DFXNativeStore*>(ADDRESS_2DFX_STORE);
        result = &store->effects[index + modelInfo->s2DEffectIndex];
    }
    else if (index < nativeCount)
    {
        if (!geometry)
            return nullptr;
        const std::int32_t offset = *reinterpret_cast<std::int32_t*>(ADDRESS_2DFX_PLUGIN_OFFSET);
        auto** pluginPointer = reinterpret_cast<C2DFXPluginData**>(reinterpret_cast<std::uint8_t*>(geometry) + offset);
        C2DFXPluginData* plugin = pluginPointer ? *pluginPointer : nullptr;
        if (!plugin || index - storedCount >= plugin->count)
            return nullptr;
        result = &plugin->effects[index - storedCount];
    }
    else if (state && index < totalCount)
    {
        const std::size_t customIndex = index - nativeCount;
        if (customIndex < state->customs.size())
            result = state->customs[customIndex]->effect.get();
    }

    if (!state || index >= nativeCount)
        return result;

    auto originalIt = state->originals.find(index);
    if (originalIt == state->originals.end())
        return result;
    if (!originalIt->second.removers.empty())
        return &g_removedEffect;
    if (result && originalIt->second.captured)
        ApplyEffectData(result, originalIt->second.effective);
    return result;
}

void CClient2DFXManager::OnEntityEffectsCreated(void* entityPointer)
{
    if (!m_impl || !entityPointer)
        return;
    auto* entity = static_cast<CEntitySAInterface*>(entityPointer);
    CBaseModelInfoSAInterface* native = GetModelInterface(entity->m_nModelIndex);
    if (!native)
        return;
    auto modelIt = m_impl->models.find(native);
    if (modelIt == m_impl->models.end())
        return;
    auto& model = modelIt->second;
    const std::uint32_t nativeCount = NativeCount(model);

    for (const auto& [index, original] : model.originals)
    {
        if (index >= nativeCount || !original.captured || !original.removers.empty())
            continue;
        C2DFXNativeEffect* effect = NativeGetEffect(native, index);
        if (effect && effect->type == e2dEffectType::ROADSIGN)
            SetRoadsignColor(effect, original.effective.color);
    }
    for (const auto& custom : model.customs)
    {
        if (custom->effect && custom->effect->type == e2dEffectType::ROADSIGN)
            SetRoadsignColor(custom->effect.get(), custom->current.color);
    }
}

bool CClient2DFXManager::IsPropertyValid(e2dEffectType type, e2dEffectProperty property)
{
    if (property == e2dEffectProperty::POSITION)
        return type != e2dEffectType::NONE && type != e2dEffectType::UNKNOWN;
    switch (type)
    {
        case e2dEffectType::LIGHT:
            return property >= e2dEffectProperty::FAR_CLIP_DISTANCE && property <= e2dEffectProperty::FLAGS;
        case e2dEffectType::PARTICLE:
            return property == e2dEffectProperty::PARTICLE_NAME;
        case e2dEffectType::ROADSIGN:
            return property == e2dEffectProperty::COLOR || property == e2dEffectProperty::FLAGS || property == e2dEffectProperty::SIZE ||
                   property == e2dEffectProperty::ROTATION || (property >= e2dEffectProperty::TEXT_1 && property <= e2dEffectProperty::TEXT_4);
        case e2dEffectType::ESCALATOR:
            return property >= e2dEffectProperty::BOTTOM && property <= e2dEffectProperty::DIRECTION;
        default:
            return false;
    }
}

void CClient2DFXManager::CopyProperty(S2DFXData& destination, const S2DFXData& source, e2dEffectProperty property)
{
    switch (property)
    {
        case e2dEffectProperty::POSITION: destination.position = source.position; break;
        case e2dEffectProperty::FAR_CLIP_DISTANCE: destination.drawDistance = source.drawDistance; break;
        case e2dEffectProperty::LIGHT_RANGE: destination.lightRange = source.lightRange; break;
        case e2dEffectProperty::CORONA_SIZE: destination.coronaSize = source.coronaSize; break;
        case e2dEffectProperty::SHADOW_SIZE: destination.shadowSize = source.shadowSize; break;
        case e2dEffectProperty::SHADOW_MULT: destination.shadowMultiplier = source.shadowMultiplier; break;
        case e2dEffectProperty::FLASH_TYPE: destination.flashType = source.flashType; break;
        case e2dEffectProperty::CORONA_REFLECTION: destination.coronaReflection = source.coronaReflection; break;
        case e2dEffectProperty::FLARE_TYPE: destination.flareType = source.flareType; break;
        case e2dEffectProperty::SHADOW_DISTANCE: destination.shadowDistance = source.shadowDistance; break;
        case e2dEffectProperty::OFFSET: destination.offset = source.offset; break;
        case e2dEffectProperty::COLOR: destination.color = source.color; break;
        case e2dEffectProperty::CORONA_NAME: destination.coronaName = source.coronaName; break;
        case e2dEffectProperty::SHADOW_NAME: destination.shadowName = source.shadowName; break;
        case e2dEffectProperty::FLAGS: destination.flags = source.flags; break;
        case e2dEffectProperty::PARTICLE_NAME: destination.particleName = source.particleName; break;
        case e2dEffectProperty::SIZE: destination.size = source.size; break;
        case e2dEffectProperty::ROTATION: destination.rotation = source.rotation; break;
        case e2dEffectProperty::TEXT_1: destination.text[0] = source.text[0]; break;
        case e2dEffectProperty::TEXT_2: destination.text[1] = source.text[1]; break;
        case e2dEffectProperty::TEXT_3: destination.text[2] = source.text[2]; break;
        case e2dEffectProperty::TEXT_4: destination.text[3] = source.text[3]; break;
        case e2dEffectProperty::BOTTOM: destination.bottom = source.bottom; break;
        case e2dEffectProperty::TOP: destination.top = source.top; break;
        case e2dEffectProperty::END: destination.end = source.end; break;
        case e2dEffectProperty::DIRECTION: destination.direction = source.direction; break;
    }
}
