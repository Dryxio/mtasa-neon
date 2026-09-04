/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CCoronasSA.cpp
 *  PURPOSE:     Header file for corona entity manager class
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <game/Common.h>
#include <game/CCoronas.h>

class CRegisteredCorona;
class CRegisteredCoronaSA;
class CRegisteredCoronaSAInterface;
class CEntitySAInterface;
struct SFileObjectInstance;
struct RwTexture;

#define FUNC_DoSunAndMoon 0x6FC5A0

#define MAX_CORONAS           4096
#define MAX_CORONA_TEXTURES   9
#define ARRAY_CORONAS         0xC3E058  // also in CRegisteredCoronasSA.h
#define ARRAY_CORONA_TEXTURES 0xC3E000

class CCoronasSA : public CCoronas
{
private:
    CRegisteredCoronaSA* Coronas[MAX_CORONAS]{};
    unsigned char        m_ucCoronaReflectionsEnabled;

    static CRegisteredCoronaSAInterface* GetCoronaArray();
    static void                          RelocateCoronaArray();

public:
    CCoronasSA();
    ~CCoronasSA();

    CRegisteredCorona* GetCorona(DWORD ID);
    CRegisteredCorona* CreateCorona(DWORD Identifier, CVector* position);
    CRegisteredCorona* FindFreeCorona();
    CRegisteredCorona* FindCorona(DWORD Identifier);
    RwTexture*         GetTexture(CoronaType type);

    void DisableSunAndMoon(bool bDisabled);

    void          SetCoronaReflectionsEnabled(unsigned char ucEnabled);
    unsigned char GetCoronaReflectionsEnabled();

    void               SetDistantLightsEnabled(bool enabled) override;
    bool               GetDistantLightsEnabled() const override;
    bool               SetDistantLightsDrawDistance(float distance) override;
    bool               SetDistantLightsCoronaRadiusMultiplier(float multiplier) override;
    void               RebuildDistantLights() override;
    void               DoPulseDistantLights() override;
    SDistantLightStats GetDistantLightStats() const override;
    void               RenderDistantLights() override;
    void               SetDistantLightSearchlightsEnabled(bool enabled) override;
    bool               SetDistantLightSettings(const SDistantLightSettings& settings) override;
    void               SetDistantLightsAutomaticDrawDistance(bool enabled) override;

    // GTA creates every static IPL entity once while calculating the world
    // bounds, before streaming most of them back out. Preserve only the raw
    // instance while Project2DFX has never been enabled; the expensive DAT
    // lookup and light catalogue are built lazily on the first activation.
    void CaptureDistantLight(const SFileObjectInstance& instance, CEntitySAInterface* entity);
};
