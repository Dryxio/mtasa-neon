/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.x / Neon
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/mods/deathmatch/logic/CClient2DFXHookSupport.cpp
 *  PURPOSE:     Hook helpers required by the deathmatch-owned 2DFX hooks
 *
 *****************************************************************************/
#include "StdInc.h"
#include <SharedUtil.MemAccess.h>
#include "../../../game_sa/HookSystem.h"
#include "../../../game_sa/gamesa_init.h"

// HookInstall is header-only in game_sa, but its two write helpers normally
// live in the game_sa project. 2DFX installs its hooks from Client Deathmatch,
// so provide the same small helpers locally instead of linking game_sa objects
// into the deathmatch module.
void MemCpy(void* destination, const void* source, uint amount)
{
    if (!destination || !source || amount == 0)
        return;

    DWORD oldProtection = 0;
    if (!VirtualProtect(destination, amount, PAGE_EXECUTE_READWRITE, &oldProtection))
    {
        dassert(false);
        return;
    }

    std::memcpy(destination, source, amount);
    FlushInstructionCache(GetCurrentProcess(), destination, amount);

    DWORD ignoredProtection = 0;
    const BOOL restored = VirtualProtect(destination, amount, oldProtection, &ignoredProtection);
    dassert(restored != FALSE);
}

BYTE* CreateJump(DWORD from, DWORD to, BYTE* byteArray)
{
    if (!byteArray)
        return nullptr;

    byteArray[0] = 0xE9;
    const DWORD relativeOffset = to - (from + 5);
    std::memcpy(&byteArray[1], &relativeOffset, sizeof(relativeOffset));
    return byteArray;
}
