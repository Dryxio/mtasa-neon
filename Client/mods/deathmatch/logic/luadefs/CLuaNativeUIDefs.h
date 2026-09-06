#pragma once
#include "CLuaDefs.h"
class CLuaNativeUIDefs : public CLuaDefs
{
public:
    static void LoadFunctions();
    LUA_DECLARE(CreateNativeUI);
    LUA_DECLARE(UpdateNativeUI);
    LUA_DECLARE(DestroyNativeUI);
    LUA_DECLARE(GetNativeUIState);
    LUA_DECLARE(ReleaseNativeUI);
    LUA_DECLARE(ShowNativeText);
    LUA_DECLARE(ClearNativeText);
};
