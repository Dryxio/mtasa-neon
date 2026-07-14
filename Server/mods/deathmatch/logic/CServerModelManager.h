/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Server/mods/deathmatch/logic/CServerModelManager.h
 *  PURPOSE:     Server-authoritative custom model registry
 *
 *****************************************************************************/

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <CServerModelDefinition.h>

class CPlayerManager;
class CResource;

class CServerModelManager
{
public:
    using ModelId = std::uint16_t;

    struct Definition
    {
        ModelId          id{};
        ModelId          parent{};
        eServerModelType type{eServerModelType::OBJECT};
        CResource*       owner{};
        std::string      name;
        bool             retiring{};
    };

    using Definitions = std::map<ModelId, Definition>;
    using BeforeFreeCallback = std::function<void(const Definition&)>;

    static constexpr ModelId FIRST_MODEL_ID = SERVER_MODEL_ID_MIN;
    static constexpr ModelId LAST_MODEL_ID = 65534;
    static constexpr ModelId INVALID_MODEL_ID = 65535;

    explicit CServerModelManager(CPlayerManager& playerManager);

    ModelId Allocate(CResource& owner, eServerModelType type, ModelId parent, const std::string& name = {});
    bool    Free(CResource& requester, ModelId model);
    void    FreeAllOwnedBy(CResource& owner);

    const Definition*  Find(ModelId model) const;
    ModelId            ResolveParent(ModelId model) const;
    bool               IsModelOfType(ModelId model, eServerModelType type) const;
    const Definitions& GetDefinitions() const noexcept { return m_definitions; }

    // Model users must be moved back to the native parent before clients release
    // their runtime allocation. CGame installs this hook once entity remapping is
    // available, keeping registry ownership independent from entity internals.
    void SetBeforeFreeCallback(BeforeFreeCallback callback) { m_beforeFreeCallback = std::move(callback); }

private:
    bool IsValidNativeParent(eServerModelType type, ModelId parent) const;
    bool IsNameAvailable(const std::string& name) const;
    bool FreeDefinition(Definitions::iterator definition);
    void BroadcastAllocate(const Definition& definition) const;
    void BroadcastFree(ModelId model) const;

    CPlayerManager&    m_playerManager;
    Definitions        m_definitions;
    ModelId            m_nextModelId{FIRST_MODEL_ID};
    BeforeFreeCallback m_beforeFreeCallback;
};
