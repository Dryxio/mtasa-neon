/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/CClientWorldObjectManager.h
 *  PURPOSE:     Discovery and event bridge for GTA-owned dynamic world objects
 *
 *****************************************************************************/

#pragma once

class CEntitySAInterface;
class CObjectSAInterface;

class CClientWorldObjectManager
{
public:
    static void Pulse();
    static void Shutdown();

    // Installed into CMultiplayer's existing global CObject hooks. Non-world
    // objects are forwarded unchanged to CClientGame's normal handlers.
    static bool ObjectDamageHandler(CObjectSAInterface* pObjectInterface, float fLoss, CEntitySAInterface* pAttackerInterface);
    static bool ObjectBreakHandler(CObjectSAInterface* pObjectInterface, CEntitySAInterface* pAttackerInterface);
};
