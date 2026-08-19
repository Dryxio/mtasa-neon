/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  PURPOSE:     Managed rope Lua definitions
 *
 *****************************************************************************/
#pragma once

#include "CLuaDefs.h"

class CLuaRopeDefs : public CLuaDefs
{
public:
    static void LoadFunctions();

    LUA_DECLARE(CreateRope);
    LUA_DECLARE(GetRopeType);
    LUA_DECLARE(SetRopeType);
    LUA_DECLARE(GetRopeDuration);
    LUA_DECLARE(SetRopeDuration);
    LUA_DECLARE(GetRopeRemainingTime);
    LUA_DECLARE(SetRopeRemainingTime);
    LUA_DECLARE(GetRopeHolder);
    LUA_DECLARE(SetRopeHolder);
    LUA_DECLARE(GetRopeHolderOffset);
    LUA_DECLARE(SetRopeHolderOffset);
    LUA_DECLARE(GetRopeTopVelocity);
    LUA_DECLARE(SetRopeTopVelocity);
    LUA_DECLARE(GetRopeWinchHeight);
    LUA_DECLARE(SetRopeWinchHeight);
    LUA_DECLARE(GetRopeLength);
    LUA_DECLARE(SetRopeLength);
    LUA_DECLARE(GetRopeFixedNode);
    LUA_DECLARE(SetRopeFixedNode);
    LUA_DECLARE(IsRopeOnGround);
    LUA_DECLARE(SetRopeOnGround);
    LUA_DECLARE(IsRopePhysicsEnabled);
    LUA_DECLARE(SetRopePhysicsEnabled);
    LUA_DECLARE(GetRopeCarriedElement);
    LUA_DECLARE(AttachElementToRope);
    LUA_DECLARE(DetachElementFromRope);
    LUA_DECLARE(GetRopePositionAt);
    LUA_DECLARE(IsRopeActive);
};
