/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.x
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/mods/deathmatch/logic/luadefs/CLuaFoliageDefs.cpp
 *  PURPOSE:     Lua custom foliage definitions
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CLuaFoliageDefs.h"
#include "../CClientFoliage.h"
#include "../CClientFoliageManager.h"

namespace
{
    CClientFoliage* ReadFoliage(CScriptArgReader& argStream)
    {
        CClientEntity* pEntity = nullptr;
        argStream.ReadUserData(pEntity);

        if (argStream.HasErrors() || !pEntity || pEntity->GetTypeHash() != CClientEntity::GetTypeHashFromString("foliage"))
            return nullptr;

        return static_cast<CClientFoliage*>(pEntity);
    }

    void PushVector3(lua_State* luaVM, const CVector& value)
    {
        lua_getglobal(luaVM, "Vector3");
        lua_pushnumber(luaVM, value.fX);
        lua_pushnumber(luaVM, value.fY);
        lua_pushnumber(luaVM, value.fZ);
        lua_call(luaVM, 3, 1);
    }
}

void CLuaFoliageDefs::LoadFunctions()
{
    constexpr static const std::pair<const char*, lua_CFunction> functions[]{
        {"createFoliage", CreateFoliage},             {"getFoliageSurface", GetFoliageSurface},
        {"setFoliageSurface", SetFoliageSurface},     {"getFoliageVertices", GetFoliageVertices},
        {"setFoliageVertices", SetFoliageVertices},   {"getFoliageDensity", GetFoliageDensity},
        {"setFoliageDensity", SetFoliageDensity},
    };

    for (const auto& [name, func] : functions)
        CLuaCFunctions::AddFunction(name, func);
}

void CLuaFoliageDefs::AddClass(lua_State* luaVM)
{
    lua_newclass(luaVM);

    lua_classfunction(luaVM, "create", "createFoliage");
    lua_classfunction(luaVM, "getSurface", "getFoliageSurface");
    lua_classfunction(luaVM, "setSurface", "setFoliageSurface");
    lua_classfunction(luaVM, "getVertices", "getFoliageVertices");
    lua_classfunction(luaVM, "setVertices", "setFoliageVertices");
    lua_classfunction(luaVM, "getDensity", "getFoliageDensity");
    lua_classfunction(luaVM, "setDensity", "setFoliageDensity");

    lua_classvariable(luaVM, "surface", "setFoliageSurface", "getFoliageSurface");
    lua_classvariable(luaVM, "density", "setFoliageDensity", "getFoliageDensity");

    lua_registerclass(luaVM, "Foliage", "Element");
}

int CLuaFoliageDefs::CreateFoliage(lua_State* luaVM)
{
    CVector v1;
    CVector v2;
    CVector v3;
    int     surface;
    float   density;

    CScriptArgReader argStream(luaVM);
    argStream.ReadVector3D(v1);
    argStream.ReadVector3D(v2);
    argStream.ReadVector3D(v3);
    argStream.ReadNumber(surface);
    argStream.ReadNumber(density, 1.0f);

    if (!argStream.HasErrors() && surface >= 0 && surface <= 255)
    {
        CLuaMain*  pLuaMain = m_pLuaManager->GetVirtualMachine(luaVM);
        CResource* pResource = pLuaMain ? pLuaMain->GetResource() : nullptr;
        if (pResource)
        {
            auto& foliageManager = CClientFoliageManager::GetSingleton();
            foliageManager.SetDimension(m_pManager->GetPointLightsManager()->GetDimension());

            CClientFoliage* pFoliage = foliageManager.Create(m_pManager, INVALID_ELEMENT_ID);
            if (pFoliage)
            {
                if (pFoliage->Initialize(v1, v2, v3, static_cast<std::uint8_t>(surface), density))
                {
                    pFoliage->SetParent(pResource->GetResourceDynamicEntity());
                    if (CElementGroup* pGroup = pResource->GetElementGroup())
                        pGroup->Add(pFoliage);

                    lua_pushelement(luaVM, pFoliage);
                    return 1;
                }

                delete pFoliage;
            }
        }
    }
    else if (argStream.HasErrors())
    {
        m_pScriptDebugging->LogCustom(luaVM, SString("Bad argument @ '%s' [%s]", lua_tostring(luaVM, lua_upvalueindex(1)), *argStream.GetErrorMessage()));
    }

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFoliageDefs::GetFoliageSurface(lua_State* luaVM)
{
    CScriptArgReader argStream(luaVM);
    CClientFoliage* pFoliage = ReadFoliage(argStream);

    if (pFoliage)
    {
        lua_pushnumber(luaVM, pFoliage->GetSurface());
        return 1;
    }

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFoliageDefs::SetFoliageSurface(lua_State* luaVM)
{
    CScriptArgReader argStream(luaVM);
    CClientFoliage* pFoliage = ReadFoliage(argStream);
    int             surface = -1;
    argStream.ReadNumber(surface);

    lua_pushboolean(luaVM, pFoliage && !argStream.HasErrors() && surface >= 0 && surface <= 255 && pFoliage->SetSurface(static_cast<std::uint8_t>(surface)));
    return 1;
}

int CLuaFoliageDefs::GetFoliageVertices(lua_State* luaVM)
{
    CScriptArgReader argStream(luaVM);
    CClientFoliage* pFoliage = ReadFoliage(argStream);

    if (pFoliage)
    {
        CVector v1;
        CVector v2;
        CVector v3;
        pFoliage->GetVertices(v1, v2, v3);
        PushVector3(luaVM, v1);
        PushVector3(luaVM, v2);
        PushVector3(luaVM, v3);
        return 3;
    }

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFoliageDefs::SetFoliageVertices(lua_State* luaVM)
{
    CScriptArgReader argStream(luaVM);
    CClientFoliage* pFoliage = ReadFoliage(argStream);
    CVector         v1;
    CVector         v2;
    CVector         v3;
    argStream.ReadVector3D(v1);
    argStream.ReadVector3D(v2);
    argStream.ReadVector3D(v3);

    lua_pushboolean(luaVM, pFoliage && !argStream.HasErrors() && pFoliage->SetVertices(v1, v2, v3));
    return 1;
}

int CLuaFoliageDefs::GetFoliageDensity(lua_State* luaVM)
{
    CScriptArgReader argStream(luaVM);
    CClientFoliage* pFoliage = ReadFoliage(argStream);

    if (pFoliage)
    {
        lua_pushnumber(luaVM, pFoliage->GetDensity());
        return 1;
    }

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFoliageDefs::SetFoliageDensity(lua_State* luaVM)
{
    CScriptArgReader argStream(luaVM);
    CClientFoliage* pFoliage = ReadFoliage(argStream);
    float           density = 0.0f;
    argStream.ReadNumber(density);

    lua_pushboolean(luaVM, pFoliage && !argStream.HasErrors() && pFoliage->SetDensity(density));
    return 1;
}
