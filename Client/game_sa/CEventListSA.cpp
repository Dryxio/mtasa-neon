/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CEventListSA.cpp
 *  PURPOSE:     Event list
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CEventListSA.h"
#include "CEventDamageSA.h"
#include <game/CEntity.h>
#include <CVector.h>
#include <cmath>

CEventDamage* CEventListSA::GetEventDamage(CEventDamageSAInterface* pInterface)
{
    return new CEventDamageSA(pInterface);
}

float CEventListSA::GetSoundLevel(CEntity* source, const CVector& position)
{
    if (!source || !source->GetInterface() || !std::isfinite(position.fX) || !std::isfinite(position.fY) || !std::isfinite(position.fZ))
        return 0.0f;
    // Same native event aggregation as opcode 0855. Filter one source; this is
    // neither the audio mixer nor a global multiplayer suspicion meter.
    using GetGroup = void*(__cdecl*)();
    using GetLevel = float(__thiscall*)(void*, CEntitySAInterface*, const CVector*);
    auto*       group = reinterpret_cast<GetGroup>(0x4ABA50)();
    const float level = group ? reinterpret_cast<GetLevel>(0x4AB900)(group, source->GetInterface(), &position) : 0.0f;
    return std::isfinite(level) && level > 0.0f ? level : 0.0f;
}
