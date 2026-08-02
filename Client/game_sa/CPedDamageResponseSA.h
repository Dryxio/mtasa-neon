/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CPedDamageResponseSA.h
 *  PURPOSE:     Header file for ped damage response
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <game/CPedDamageResponse.h>

class CPedDamageResponseSAInterface
{
public:
    float fDamageHealth;
    float fDamageArmor;
    bool  bHealthZero;
    bool  bForceDeath;
    bool  bDamageCalculated;
    bool  bCheckIfAffectsPed;
};
static_assert(sizeof(CPedDamageResponseSAInterface) == 0xC, "Invalid ped damage response size");

class CPedDamageResponseSA : public CPedDamageResponse
{
public:
    CPedDamageResponseSA(CPedDamageResponseSAInterface* pInterface) { m_pInterface = pInterface; }

    CPedDamageResponseSAInterface* GetInterface() { return m_pInterface; }

    void Calculate(CEntity* pEntity, float fDamage, eWeaponType weaponType, ePedPieceTypes bodyPart, bool b_1, bool bSpeak);

private:
    CPedDamageResponseSAInterface* m_pInterface;
};
