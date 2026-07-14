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

void CLuaModelDefs::LoadFunctions()
{
    constexpr static const std::pair<const char*, lua_CFunction> functions[]{
        {"engineRequestModel", EngineRequestModel},
        {"engineFreeModel", EngineFreeModel},
        {"engineGetModelParent", EngineGetModelParent},
        {"engineGetModelName", EngineGetModelName},
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

    eServerModelType type = eServerModelType::OBJECT;
    if (typeName == "object")
        type = eServerModelType::OBJECT;
    else if (typeName == "vehicle")
        type = eServerModelType::VEHICLE;
    else
        arguments.SetCustomError("Expected model type 'object' or 'vehicle' at argument 1");

    if (!arguments.HasErrors())
    {
        CResource*           resource = m_pResourceManager->GetResourceFromLuaState(luaVM);
        CServerModelManager* manager = g_pGame->GetServerModelManager();
        if (resource && manager)
        {
            const auto model = manager->Allocate(*resource, type, parent, name.c_str());
            if (model != CServerModelManager::INVALID_MODEL_ID)
            {
                lua_pushinteger(luaVM, model);
                return 1;
            }

            arguments.SetCustomError("Unable to allocate model: invalid native parent, duplicate name, or model registry exhausted");
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
