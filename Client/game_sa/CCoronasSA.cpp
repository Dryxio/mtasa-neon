/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CCoronasSA.cpp
 *  PURPOSE:     Corona entity manager
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CCoronasSA.h"
#include "CRegisteredCoronaSA.h"

namespace
{
    // GTA stores the corona array in the executable's data segment. Keep the
    // replacement alive for the rest of the process because GTA can render
    // coronas before and after MTA recreates its CGameSA wrapper objects.
    CRegisteredCoronaSAInterface* g_pCoronaArray = reinterpret_cast<CRegisteredCoronaSAInterface*>(ARRAY_CORONAS);

    void PatchCoronaArrayPointer(std::uintptr_t address, const void* value)
    {
        MemPut<DWORD>(address, reinterpret_cast<DWORD>(value));
    }

    BYTE* CoronaField(CRegisteredCoronaSAInterface* corona, std::size_t offset)
    {
        return reinterpret_cast<BYTE*>(corona) + offset;
    }

}  // namespace

CRegisteredCoronaSAInterface* CCoronasSA::GetCoronaArray()
{
    return g_pCoronaArray;
}

void CCoronasSA::RelocateCoronaArray()
{
    static bool bPatched = false;
    if (bPatched)
        return;

    static CRegisteredCoronaSAInterface coronaArray[MAX_CORONAS]{};
    g_pCoronaArray = coronaArray;

    // Every instruction below directly addresses CCoronas::aCoronas in the
    // SA 1.0 US executable. Relocating all field references lets GTA keep its
    // original corona implementation while iterating over MTA's larger array.
    PatchCoronaArrayPointer(0x6FAACF, &coronaArray[0].Identifier);
    PatchCoronaArrayPointer(0x6FAEA0, coronaArray);
    PatchCoronaArrayPointer(0x6FAEB7, coronaArray + MAX_CORONAS);
    PatchCoronaArrayPointer(0x6FAF42, &coronaArray[0].pEntityAttachedTo);
    PatchCoronaArrayPointer(0x6FB648, CoronaField(&coronaArray[0], 0x36));
    PatchCoronaArrayPointer(0x6FB657, CoronaField(&coronaArray[MAX_CORONAS], 0x36));
    PatchCoronaArrayPointer(0x6FB6CF, &coronaArray[0].FadedIntensity);
    PatchCoronaArrayPointer(0x6FB9B8, &coronaArray[MAX_CORONAS].FadedIntensity);

    PatchCoronaArrayPointer(0x6FC2E8, &coronaArray[0].Identifier);
    PatchCoronaArrayPointer(0x6FC318, &coronaArray[0].Identifier);
    PatchCoronaArrayPointer(0x6FC341, &coronaArray[0].FadedIntensity);
    PatchCoronaArrayPointer(0x6FC34A, &coronaArray[0].FadedIntensity);
    PatchCoronaArrayPointer(0x6FC351, CoronaField(&coronaArray[0], 0x34));
    PatchCoronaArrayPointer(0x6FC358, CoronaField(&coronaArray[0], 0x36));
    PatchCoronaArrayPointer(0x6FC365, &coronaArray[0].JustCreated);
    PatchCoronaArrayPointer(0x6FC36B, &coronaArray[0].Identifier);
    PatchCoronaArrayPointer(0x6FC37A, &coronaArray[0].Red);
    PatchCoronaArrayPointer(0x6FC384, &coronaArray[0].Green);
    PatchCoronaArrayPointer(0x6FC38E, &coronaArray[0].Blue);
    PatchCoronaArrayPointer(0x6FC398, &coronaArray[0].Intensity);
    PatchCoronaArrayPointer(0x6FC3A1, &coronaArray[0].Coordinates);
    PatchCoronaArrayPointer(0x6FC3B9, &coronaArray[0].Size);
    PatchCoronaArrayPointer(0x6FC3C3, &coronaArray[0].NormalAngle);
    PatchCoronaArrayPointer(0x6FC3CD, &coronaArray[0].Range);
    PatchCoronaArrayPointer(0x6FC3D7, &coronaArray[0].pTex);
    PatchCoronaArrayPointer(0x6FC3E1, &coronaArray[0].FlareType);
    PatchCoronaArrayPointer(0x6FC3EB, &coronaArray[0].ReflectionType);
    PatchCoronaArrayPointer(0x6FC3F1, CoronaField(&coronaArray[0], 0x34));
    PatchCoronaArrayPointer(0x6FC3FB, &coronaArray[0].RegisteredThisFrame);
    PatchCoronaArrayPointer(0x6FC403, CoronaField(&coronaArray[0], 0x34));
    PatchCoronaArrayPointer(0x6FC40D, &coronaArray[0].PullTowardsCam);
    PatchCoronaArrayPointer(0x6FC417, &coronaArray[0].FadeSpeed);
    PatchCoronaArrayPointer(0x6FC432, CoronaField(&coronaArray[0], 0x36));
    PatchCoronaArrayPointer(0x6FC44A, CoronaField(&coronaArray[0], 0x36));
    PatchCoronaArrayPointer(0x6FC454, CoronaField(&coronaArray[0], 0x36));
    PatchCoronaArrayPointer(0x6FC45A, &coronaArray[0].pEntityAttachedTo);
    PatchCoronaArrayPointer(0x6FC478, &coronaArray[0].FadedIntensity);
    PatchCoronaArrayPointer(0x6FC496, &coronaArray[0].Identifier);
    PatchCoronaArrayPointer(0x6FC4AC, CoronaField(&coronaArray[0], 0x36));
    PatchCoronaArrayPointer(0x6FC4B2, &coronaArray[0].pEntityAttachedTo);
    PatchCoronaArrayPointer(0x6FC538, &coronaArray[0].Identifier);
    PatchCoronaArrayPointer(0x6FC555, &coronaArray[0].Coordinates);
    PatchCoronaArrayPointer(0x6FC56D, &coronaArray[0].NormalAngle);

    // GTA's native RegisterCorona and UpdateCoronaCoors searches deliberately
    // remain limited to the first 64 slots. Those slots service vanilla
    // effects, while MTA allocates scripted coronas from the entire relocated
    // array through CCoronasSA::FindFreeCorona.
    MemPut<DWORD>(0x6FAAD4, MAX_CORONAS);
    MemPut<DWORD>(0x6FAF4A, MAX_CORONAS);

    bPatched = true;
}

CCoronasSA::CCoronasSA()
{
    RelocateCoronaArray();

    for (int i = 0; i < MAX_CORONAS; i++)
    {
        Coronas[i] = new CRegisteredCoronaSA(&GetCoronaArray()[i], i);
    }
}

CCoronasSA::~CCoronasSA()
{
    for (int i = 0; i < MAX_CORONAS; i++)
    {
        delete Coronas[i];
    }
}

CRegisteredCorona* CCoronasSA::GetCorona(DWORD ID)
{
    return (CRegisteredCorona*)Coronas[ID];
}

CRegisteredCorona* CCoronasSA::CreateCorona(DWORD Identifier, CVector* position)
{
    CRegisteredCoronaSA* corona;
    corona = (CRegisteredCoronaSA*)FindCorona(Identifier);

    if (!corona)
        corona = (CRegisteredCoronaSA*)FindFreeCorona();

    if (corona)
    {
        RwTexture* texture = GetTexture(CoronaType::CORONATYPE_SHINYSTAR);
        if (texture)
        {
            corona->Init(Identifier);
            corona->SetPosition(position);
            corona->SetTexture(texture);
            return (CRegisteredCorona*)corona;
        }
    }

    return (CRegisteredCorona*)NULL;
}

CRegisteredCorona* CCoronasSA::FindFreeCorona()
{
    for (int i = 2; i < MAX_CORONAS; i++)
    {
        if (Coronas[i]->GetIdentifier() == 0)
        {
            return Coronas[i];
        }
    }
    return (CRegisteredCorona*)NULL;
}

CRegisteredCorona* CCoronasSA::FindCorona(DWORD Identifier)
{
    for (int i = 0; i < MAX_CORONAS; i++)
    {
        if (Coronas[i]->GetIdentifier() == Identifier)
        {
            return Coronas[i];
        }
    }
    return (CRegisteredCorona*)NULL;
}

RwTexture* CCoronasSA::GetTexture(CoronaType type)
{
    if ((DWORD)type < MAX_CORONA_TEXTURES)
        return (RwTexture*)(*(DWORD*)(ARRAY_CORONA_TEXTURES + static_cast<DWORD>(type) * sizeof(DWORD)));
    else
        return NULL;
}

void CCoronasSA::DisableSunAndMoon(bool bDisabled)
{
    static BYTE byteOriginal = 0;
    if (bDisabled && !byteOriginal)
    {
        byteOriginal = *(BYTE*)FUNC_DoSunAndMoon;
        MemPut<BYTE>(FUNC_DoSunAndMoon, 0xC3);
    }
    else if (!bDisabled && byteOriginal)
    {
        MemPut<BYTE>(FUNC_DoSunAndMoon, byteOriginal);
        byteOriginal = 0;
    }
}

/*
    Enable or disable corona rain reflections.
    ucEnabled:
     0 - disabled
     1 - enabled
     2 - force enabled (render even if there is no rain)
*/
void CCoronasSA::SetCoronaReflectionsEnabled(unsigned char ucEnabled)
{
    m_ucCoronaReflectionsEnabled = ucEnabled;

    if (ucEnabled == 0)
    {
        // Disable corona rain reflections
        // Return out CCoronas::RenderReflections()
        MemPut<BYTE>(0x6FB630, 0xC3);
    }
    else
    {
        // Enable corona rain reflections
        // Re-enable CCoronas::RenderReflections()
        MemPut<BYTE>(0x6FB630, 0xD9);
    }

    if (ucEnabled == 2)
    {
        // Force enable corona reflections (render even if there is no rain)
        // Disable fWetGripScale check
        MemPut<BYTE>(0x6FB645, 0xEB);

        // Patch "fld fWetGripScale" to "fld fOne"
        MemCpy((void*)0x6FB906, "\x24\x86\x85\x00", 4);
    }
    else
    {
        // Restore patched code
        MemPut<BYTE>(0x6FB645, 0x7A);
        MemCpy((void*)0x6FB906, "\x08\x13\xC8\x00", 4);
    }
}

unsigned char CCoronasSA::GetCoronaReflectionsEnabled()
{
    return m_ucCoronaReflectionsEnabled;
}
