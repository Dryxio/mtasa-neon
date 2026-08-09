/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/luadefs/CLuaClientDefs.cpp
 *  PURPOSE:     Lua client definitions class
 *
 *  Multi Theft Auto is available from https://multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CLuaClientDefs.h"
#include "lua/CLuaFunctionParser.h"
#include <CResource.h>

void CLuaClientDefs::LoadFunctions()
{
    constexpr static const std::pair<const char*, lua_CFunction> functions[]{{"setTransferBoxVisible", ArgumentParser<SetTransferBoxVisible>},
                                                                             {"isTransferBoxVisible", ArgumentParser<IsTransferBoxVisible>},
                                                                             {"isTransferBoxAlwaysVisible", ArgumentParser<IsTransferBoxAlwaysVisible>},
                                                                             {"showChat", ArgumentParserWarn<false, ShowChat>},
                                                                             {"isChatVisible", ArgumentParserWarn<false, IsChatVisible>},
                                                                             {"isChatInputBlocked", ArgumentParser<IsChatInputBlocked>},
                                                                             {"clearDebugBox", ArgumentParser<ClearDebug>},
                                                                             {"isMTAWindowFocused", ArgumentParser<IsMTAWindowFocused>},
                                                                             {"isCapsLockEnabled", ArgumentParser<IsCapsLockEnabled>},
                                                                             {"getNeonClientSetting", ArgumentParser<GetNeonClientSetting>},
                                                                             {"setNeonClientSetting", ArgumentParser<SetNeonClientSetting>},
                                                                             {"resetNeonClientSettings", ArgumentParser<ResetNeonClientSettings>}};

    for (const auto& [name, func] : functions)
        CLuaCFunctions::AddFunction(name, func);
}

bool CLuaClientDefs::SetTransferBoxVisible(bool visible)
{
    return g_pClientGame->GetTransferBox()->SetClientVisibility(visible);
}

bool CLuaClientDefs::IsTransferBoxVisible()
{
    return g_pClientGame->GetTransferBox()->IsVisible();
}

bool CLuaClientDefs::IsTransferBoxAlwaysVisible()
{
    return g_pClientGame->GetTransferBox()->IsAlwaysVisible();
}

bool CLuaClientDefs::ShowChat(bool bVisible, std::optional<bool> optInputBlocked)
{
    // Keep old behaviour: input is blocked when chat is hidden
    bool bInputBlocked = !bVisible;
    if (optInputBlocked.has_value())
        bInputBlocked = optInputBlocked.value();

    g_pCore->SetChatVisible(bVisible, bInputBlocked);
    return true;
}

bool CLuaClientDefs::IsChatVisible()
{
    return g_pCore->IsChatVisible();
}

bool CLuaClientDefs::IsChatInputBlocked()
{
    return g_pCore->IsChatInputBlocked();
}

bool CLuaClientDefs::ClearDebug()
{
    g_pCore->DebugClear();
    return true;
}

bool CLuaClientDefs::IsMTAWindowFocused()
{
    return m_pClientGame->IsWindowFocused();
}

bool CLuaClientDefs::IsCapsLockEnabled()
{
    return ((::GetKeyState(VK_CAPITAL) & 0x0001) != 0);
}

std::variant<bool, double, std::string> CLuaClientDefs::GetNeonClientSetting(std::string name)
{
    SString type;
    SString value;
    if (!g_pCore->GetNeonClientSetting(name, type, value))
        return false;
    if (type == "boolean")
        return value == "1";
    if (type == "number")
        return std::strtod(value.c_str(), nullptr);
    return std::string(value);
}

bool CLuaClientDefs::SetNeonClientSetting(lua_State* luaVM, std::string name, std::variant<bool, double, std::string> value)
{
    CResource* resource = luaVM ? g_pClientGame->GetResourceManager()->GetResourceFromLuaState(luaVM) : nullptr;
    if (!resource)
        return false;

    SString type;
    SString serialized;
    if (const bool* booleanValue = std::get_if<bool>(&value))
    {
        type = "boolean";
        serialized = *booleanValue ? "1" : "0";
    }
    else if (const double* numberValue = std::get_if<double>(&value))
    {
        type = "number";
        serialized = SString("%.17g", *numberValue);
    }
    else
    {
        type = "string";
        serialized = std::get<std::string>(value);
    }
    return g_pCore->SetNeonClientSettingOverride(resource->GetName(), name, type, serialized);
}

bool CLuaClientDefs::ResetNeonClientSettings(lua_State* luaVM)
{
    CResource* resource = luaVM ? g_pClientGame->GetResourceManager()->GetResourceFromLuaState(luaVM) : nullptr;
    if (!resource)
        return false;

    g_pCore->ClearNeonClientSettingOverrides(resource->GetName());
    return true;
}
