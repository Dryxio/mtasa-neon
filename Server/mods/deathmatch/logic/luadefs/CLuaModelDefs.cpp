/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Server/mods/deathmatch/logic/luadefs/CLuaModelDefs.cpp
 *  PURPOSE:     Server custom model Lua definitions
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CLuaModelDefs.h"

#include "CGame.h"
#include "CResource.h"
#include "CServerModelManager.h"
#include "CScriptArgReader.h"

namespace
{
    std::optional<eServerModelType> ParseServerModelType(const SString& name)
    {
        if (name == "object")
            return eServerModelType::OBJECT;
        if (name == "vehicle")
            return eServerModelType::VEHICLE;
        if (name == "ped")
            return eServerModelType::PED;
        return std::nullopt;
    }

    const char* GetServerModelTypeName(eServerModelType type)
    {
        switch (type)
        {
            case eServerModelType::OBJECT:
                return "object";
            case eServerModelType::VEHICLE:
                return "vehicle";
            case eServerModelType::PED:
                return "ped";
        }
        return "unknown";
    }
}  // namespace

void CLuaModelDefs::LoadFunctions()
{
    constexpr static const std::pair<const char*, lua_CFunction> functions[]{
        {"engineRequestModel", EngineRequestModel},
        {"engineFreeModel", EngineFreeModel},
        {"engineGetModelParent", EngineGetModelParent},
        {"engineGetModelName", EngineGetModelName},
        {"engineGetModelType", EngineGetModelType},
        {"engineGetModelIDFromName", EngineGetModelIDFromName},
        {"engineGetModels", EngineGetModels},
        {"engineIsModelAllocated", EngineIsModelAllocated},
        {"engineGetModelAvailableCount", EngineGetModelAvailableCount},
    };

    for (const auto& [name, function] : functions)
        CLuaCFunctions::AddFunction(name, function);
}

int CLuaModelDefs::EngineRequestModel(lua_State* luaVM)
{
    SString       typeName;
    std::uint16_t parent{};
    SString       name;

    CScriptArgReader arguments(luaVM);
    arguments.ReadString(typeName);
    arguments.ReadNumber(parent);
    arguments.ReadString(name, "");

    const std::optional<eServerModelType> type = ParseServerModelType(typeName);
    if (!type)
        arguments.SetCustomError("Expected model type 'object', 'vehicle', or 'ped' at argument 1");

    if (!arguments.HasErrors())
    {
        CResource*           resource = m_pResourceManager->GetResourceFromLuaState(luaVM);
        CServerModelManager* manager = g_pGame->GetServerModelManager();
        if (resource && manager)
        {
            const auto model = manager->Allocate(*resource, *type, parent, name.c_str());
            if (model != CServerModelManager::INVALID_MODEL_ID)
            {
                lua_pushinteger(luaVM, model);
                return 1;
            }

            arguments.SetCustomError("Unable to allocate model: invalid native parent, duplicate/oversized name, resource quota, or registry exhaustion");
        }
    }

    if (arguments.HasErrors())
        m_pScriptDebugging->LogCustom(luaVM, arguments.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaModelDefs::EngineFreeModel(lua_State* luaVM)
{
    std::uint16_t model{};

    CScriptArgReader arguments(luaVM);
    arguments.ReadNumber(model);

    if (!arguments.HasErrors())
    {
        CResource*           resource = m_pResourceManager->GetResourceFromLuaState(luaVM);
        CServerModelManager* manager = g_pGame->GetServerModelManager();
        if (resource && manager && manager->Free(*resource, model))
        {
            lua_pushboolean(luaVM, true);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, arguments.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaModelDefs::EngineGetModelParent(lua_State* luaVM)
{
    std::uint16_t model{};

    CScriptArgReader arguments(luaVM);
    arguments.ReadNumber(model);

    if (!arguments.HasErrors())
    {
        CServerModelManager* manager = g_pGame->GetServerModelManager();
        if (manager)
        {
            const CServerModelManager::Definition* definition = manager->Find(model);
            if (definition)
            {
                lua_pushinteger(luaVM, definition->parent);
                return 1;
            }
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, arguments.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaModelDefs::EngineGetModelName(lua_State* luaVM)
{
    std::uint16_t model{};

    CScriptArgReader arguments(luaVM);
    arguments.ReadNumber(model);

    if (!arguments.HasErrors())
    {
        CServerModelManager* manager = g_pGame->GetServerModelManager();
        if (manager)
        {
            const CServerModelManager::Definition* definition = manager->Find(model);
            if (definition)
            {
                lua_pushstring(luaVM, definition->name.c_str());
                return 1;
            }
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, arguments.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaModelDefs::EngineGetModelType(lua_State* luaVM)
{
    std::uint16_t model{};

    CScriptArgReader arguments(luaVM);
    arguments.ReadNumber(model);

    if (!arguments.HasErrors())
    {
        const CServerModelManager::Definition* definition = g_pGame->GetServerModelManager()->Find(model);
        if (definition)
        {
            lua_pushstring(luaVM, GetServerModelTypeName(definition->type));
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, arguments.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaModelDefs::EngineGetModelIDFromName(lua_State* luaVM)
{
    SString name;

    CScriptArgReader arguments(luaVM);
    arguments.ReadString(name);

    if (!arguments.HasErrors())
    {
        CResource*  resource = m_pResourceManager->GetResourceFromLuaState(luaVM);
        std::string qualifiedName = name.c_str();
        if (resource && !name.Contains(":"))
            qualifiedName = std::string(resource->GetName().c_str()) + ":" + name.c_str();

        const CServerModelManager::Definition* definition = g_pGame->GetServerModelManager()->FindByName(qualifiedName);
        if (definition)
        {
            lua_pushinteger(luaVM, definition->id);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, arguments.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaModelDefs::EngineGetModels(lua_State* luaVM)
{
    std::optional<eServerModelType> type;
    CScriptArgReader                arguments(luaVM);

    if (!arguments.NextIsNone() && !arguments.NextIsNil())
    {
        SString typeName;
        arguments.ReadString(typeName);
        type = ParseServerModelType(typeName);
        if (!type)
            arguments.SetCustomError("Expected model type 'object', 'vehicle', or 'ped' at argument 1");
    }

    if (arguments.HasErrors())
    {
        m_pScriptDebugging->LogCustom(luaVM, arguments.GetFullErrorMessage());
        lua_pushboolean(luaVM, false);
        return 1;
    }

    const auto& definitions = g_pGame->GetServerModelManager()->GetDefinitions();
    lua_createtable(luaVM, static_cast<int>(definitions.size()), 0);
    int index = 1;
    for (const auto& [id, definition] : definitions)
    {
        if (type && definition.type != *type)
            continue;

        lua_pushinteger(luaVM, id);
        lua_rawseti(luaVM, -2, index++);
    }
    return 1;
}

int CLuaModelDefs::EngineIsModelAllocated(lua_State* luaVM)
{
    std::uint16_t model{};

    CScriptArgReader arguments(luaVM);
    arguments.ReadNumber(model);

    if (arguments.HasErrors())
    {
        m_pScriptDebugging->LogCustom(luaVM, arguments.GetFullErrorMessage());
        lua_pushboolean(luaVM, false);
        return 1;
    }

    lua_pushboolean(luaVM, g_pGame->GetServerModelManager()->Find(model) != nullptr);
    return 1;
}

int CLuaModelDefs::EngineGetModelAvailableCount(lua_State* luaVM)
{
    CResource* resource = m_pResourceManager->GetResourceFromLuaState(luaVM);
    lua_pushinteger(luaVM, resource ? static_cast<lua_Integer>(g_pGame->GetServerModelManager()->GetRemainingCapacity(*resource)) : 0);
    return 1;
}
