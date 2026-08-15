/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  PURPOSE:     Native GTA radio playback state Lua bindings
 *
 *****************************************************************************/

#include "StdInc.h"
#include <game/CAERadioTrackManager.h>

namespace
{
    constexpr int MIN_NATIVE_TRACK_INDEX = -128;
    constexpr int MAX_NATIVE_TRACK_INDEX = 127;

    void PushIntegerField(lua_State* luaVM, const char* name, int value)
    {
        lua_pushinteger(luaVM, value);
        lua_setfield(luaVM, -2, name);
    }

    bool ReadIntegerField(lua_State* luaVM, int tableIndex, const char* name, int& value)
    {
        lua_getfield(luaVM, tableIndex, name);
        if (!lua_isnumber(luaVM, -1))
        {
            lua_pop(luaVM, 1);
            return false;
        }

        value = static_cast<int>(lua_tointeger(luaVM, -1));
        lua_pop(luaVM, 1);
        return true;
    }

    int GetRadioPlaybackState(lua_State* luaVM)
    {
        CAERadioTrackManager* radio = g_pGame ? g_pGame->GetAERadioTrackManager() : nullptr;
        SRadioPlaybackState   state;
        if (!radio || !radio->GetPlaybackState(state))
        {
            lua_pushboolean(luaVM, false);
            return 1;
        }

        lua_newtable(luaVM);
        lua_pushboolean(luaVM, state.radioOn);
        lua_setfield(luaVM, -2, "radioOn");
        PushIntegerField(luaVM, "station", state.stationId);
        PushIntegerField(luaVM, "mode", state.mode);
        PushIntegerField(luaVM, "trackId", state.currentTrackId);
        PushIntegerField(luaVM, "trackType", state.currentTrackType);
        PushIntegerField(luaVM, "trackIndex", state.currentTrackIndex);
        PushIntegerField(luaVM, "position", state.playTimeMs);
        PushIntegerField(luaVM, "length", state.trackLengthMs);
        PushIntegerField(luaVM, "flags", state.trackFlags);

        lua_newtable(luaVM);
        for (unsigned int i = 0; i < SRadioPlaybackState::QUEUE_SIZE; ++i)
        {
            lua_newtable(luaVM);
            PushIntegerField(luaVM, "id", state.trackQueue[i]);
            PushIntegerField(luaVM, "type", state.trackTypes[i]);
            PushIntegerField(luaVM, "index", state.trackIndexes[i]);
            lua_rawseti(luaVM, -2, i + 1);
        }
        lua_setfield(luaVM, -2, "queue");
        return 1;
    }

    int SetRadioPlaybackState(lua_State* luaVM)
    {
        if (!lua_istable(luaVM, 1))
        {
            lua_pushboolean(luaVM, false);
            return 1;
        }

        SRadioPlaybackState state;
        lua_getfield(luaVM, 1, "radioOn");
        state.radioOn = lua_isboolean(luaVM, -1) ? lua_toboolean(luaVM, -1) != 0 : true;
        lua_pop(luaVM, 1);

        int station{}, mode{}, trackId{}, trackType{}, trackIndex{}, position{}, length{}, flags{};
        if (!ReadIntegerField(luaVM, 1, "station", station) || !ReadIntegerField(luaVM, 1, "trackId", trackId) ||
            !ReadIntegerField(luaVM, 1, "trackType", trackType) || !ReadIntegerField(luaVM, 1, "trackIndex", trackIndex) ||
            !ReadIntegerField(luaVM, 1, "position", position) || !ReadIntegerField(luaVM, 1, "length", length) ||
            !ReadIntegerField(luaVM, 1, "flags", flags))
        {
            lua_pushboolean(luaVM, false);
            return 1;
        }
        ReadIntegerField(luaVM, 1, "mode", mode);

        if (station < 0 || station >= 13 || trackId < 0 || trackType < 0 || trackType > 7 || trackIndex < MIN_NATIVE_TRACK_INDEX ||
            trackIndex > MAX_NATIVE_TRACK_INDEX || position < 0 || length < -1 || flags < 0 || flags > 255)
        {
            lua_pushboolean(luaVM, false);
            return 1;
        }

        state.stationId = static_cast<unsigned char>(station);
        state.mode = mode;
        state.currentTrackId = trackId;
        state.currentTrackType = trackType;
        state.currentTrackIndex = trackIndex;
        state.playTimeMs = position;
        state.trackLengthMs = length;
        state.trackFlags = static_cast<unsigned char>(flags);

        lua_getfield(luaVM, 1, "queue");
        if (!lua_istable(luaVM, -1))
        {
            lua_pop(luaVM, 1);
            lua_pushboolean(luaVM, false);
            return 1;
        }

        for (unsigned int i = 0; i < SRadioPlaybackState::QUEUE_SIZE; ++i)
        {
            lua_rawgeti(luaVM, -1, i + 1);
            if (!lua_istable(luaVM, -1))
            {
                lua_pop(luaVM, 2);
                lua_pushboolean(luaVM, false);
                return 1;
            }

            int id{}, type{}, index{};
            if (!ReadIntegerField(luaVM, -1, "id", id) || !ReadIntegerField(luaVM, -1, "type", type) || !ReadIntegerField(luaVM, -1, "index", index) ||
                id < -1 || type < 0 || type > 7 || index < MIN_NATIVE_TRACK_INDEX || index > MAX_NATIVE_TRACK_INDEX)
            {
                lua_pop(luaVM, 2);
                lua_pushboolean(luaVM, false);
                return 1;
            }

            state.trackQueue[i] = id;
            state.trackTypes[i] = static_cast<signed char>(type);
            state.trackIndexes[i] = static_cast<signed char>(index);
            lua_pop(luaVM, 1);
        }
        lua_pop(luaVM, 1);

        CAERadioTrackManager* radio = g_pGame ? g_pGame->GetAERadioTrackManager() : nullptr;
        lua_pushboolean(luaVM, radio && radio->SetPlaybackState(state));
        return 1;
    }
}

void RegisterRadioPlaybackFunctions()
{
    CLuaCFunctions::AddFunction("getRadioPlaybackState", GetRadioPlaybackState);
    CLuaCFunctions::AddFunction("setRadioPlaybackState", SetRadioPlaybackState);
}
