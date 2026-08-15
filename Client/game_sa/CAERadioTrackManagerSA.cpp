/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CAERadioTrackManagerSA.cpp
 *  PURPOSE:     Audio entity radio track manager
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CAERadioTrackManagerSA.h"

namespace
{
    int GetHardwareInt(DWORD function)
    {
        int value = -1;
        __asm
        {
            mov ecx, CLASS_CAEAudioHardware
            call function
            mov value, eax
        }
        return value;
    }

    void StopHardwareTrack()
    {
        DWORD function = FUNC_CAEAudioHardware_StopTrack;
        __asm
        {
            mov ecx, CLASS_CAEAudioHardware
            call function
        }
    }
}

BYTE CAERadioTrackManagerSA::GetCurrentRadioStationID()
{
    DWORD dwFunc = FUNC_GetCurrentRadioStationID;
    BYTE  bReturn = 0;
    // clang-format off
    __asm
    {
        mov     ecx, CLASS_CAERadioTrackManager
        call    dwFunc
        mov     bReturn, al
    }
    // clang-format on

    return bReturn;
}

BYTE CAERadioTrackManagerSA::IsVehicleRadioActive()
{
    DWORD dwFunc = FUNC_IsVehicleRadioActive;
    BYTE  bReturn = 0;
    // clang-format off
    __asm
    {
        mov     ecx, CLASS_CAERadioTrackManager
        call    dwFunc
        mov     bReturn, al
    }
    // clang-format on

    return bReturn;
}

char* CAERadioTrackManagerSA::GetRadioStationName(BYTE bStationID)
{
    DWORD dwFunc = FUNC_GetRadioStationName;
    char* cReturn = 0;
    DWORD dwStationID = bStationID;
    // clang-format off
    __asm
    {
        mov     ecx, CLASS_CAERadioTrackManager
        push    dwStationID
        call    dwFunc
        mov     cReturn, eax
    }
    // clang-format on

    return cReturn;
}

bool CAERadioTrackManagerSA::IsRadioOn()
{
    DWORD dwFunc = FUNC_IsRadioOn;
    bool  bReturn = false;
    // clang-format off
    __asm
    {
        mov     ecx, CLASS_CAERadioTrackManager
        call    dwFunc
        mov     bReturn, al
    }
    // clang-format on

    return bReturn;
}

void CAERadioTrackManagerSA::SetBassSetting(DWORD dwBass)
{
    DWORD dwFunc = FUNC_SetBassSetting;
    // clang-format off
    __asm
    {
        mov     ecx, CLASS_CAERadioTrackManager
        push    0x3F800000 // 1.0f
        push    dwBass
        call    dwFunc
    }
    // clang-format on
}

void CAERadioTrackManagerSA::Reset()
{
    DWORD dwFunc = FUNC_Reset;
    // clang-format off
    __asm
    {
        mov     ecx, CLASS_CAERadioTrackManager
        call    dwFunc
    }
    // clang-format on
}

void CAERadioTrackManagerSA::StartRadio(BYTE bStationID, BYTE bUnknown)
{
    DWORD dwFunc = FUNC_StartRadio;
    DWORD dwStationID = bStationID;
    DWORD dwUnknown = bUnknown;
    // clang-format off
    __asm
    {
        mov     ecx, CLASS_CAERadioTrackManager
        push    0
        push    0
        push    dwUnknown
        push    dwStationID
        call    dwFunc
    }
    // clang-format on
}

bool CAERadioTrackManagerSA::IsStationLoading() const
{
    CAERadioTrackManagerSAInterface* trackInterface = GetInterface();
    return (trackInterface->stationsListed || trackInterface->stationsListDown);
}

bool CAERadioTrackManagerSA::GetPlaybackState(SRadioPlaybackState& state) const
{
    const auto* trackInterface = GetInterface();
    if (!trackInterface)
        return false;

    const auto& settings = trackInterface->activeSettings;
    state.radioOn = trackInterface->trackMode != eRadioTrackMode::RADIO_STOPPED || trackInterface->isInitialised || trackInterface->stationsListed ||
                    trackInterface->stationsListDown;
    state.stationId = settings.currentRadioStation;
    state.mode = static_cast<int>(trackInterface->trackMode);
    state.currentTrackId = GetHardwareInt(FUNC_CAEAudioHardware_GetPlayingTrackID);
    state.currentTrackType = settings.currentTrackType;
    state.currentTrackIndex = settings.currentTrackIndex;
    state.playTimeMs = GetHardwareInt(FUNC_CAEAudioHardware_GetTrackPlayTime);
    state.trackLengthMs = GetHardwareInt(FUNC_CAEAudioHardware_GetTrackLengthMs);
    state.trackFlags = settings.trackFlags;

    for (unsigned int i = 0; i < SRadioPlaybackState::QUEUE_SIZE; ++i)
    {
        state.trackQueue[i] = settings.trackQueue[i];
        state.trackTypes[i] = static_cast<signed char>(settings.trackTypes[i]);
        state.trackIndexes[i] = settings.trackIndexes[i];
    }

    return true;
}

bool CAERadioTrackManagerSA::SetPlaybackState(const SRadioPlaybackState& state)
{
    if (!state.radioOn || state.stationId >= 13 || state.currentTrackId < 0 || state.playTimeMs < 0)
        return false;

    auto* trackInterface = GetInterface();
    if (!trackInterface)
        return false;

    auto settings = trackInterface->activeSettings;
    settings.currentRadioStation = state.stationId;
    settings.currentTrackId = state.currentTrackId;
    settings.prevTrackId = -1;
    settings.trackPlayTime = state.playTimeMs;
    settings.trackLengthInMS = state.trackLengthMs;
    settings.trackFlags = state.trackFlags;
    settings.currentTrackType = static_cast<std::uint8_t>(state.currentTrackType);
    settings.currentTrackIndex = static_cast<std::int8_t>(state.currentTrackIndex);
    settings.prevTrackType = 6;
    settings.prevTrackIndex = -1;

    for (unsigned int i = 0; i < SRadioPlaybackState::QUEUE_SIZE; ++i)
    {
        settings.trackQueue[i] = state.trackQueue[i];
        settings.trackTypes[i] = static_cast<std::uint8_t>(state.trackTypes[i]);
        settings.trackIndexes[i] = state.trackIndexes[i];
    }

    // The first queued stream is what RADIO_STARTING passes to CAEAudioHardware::PlayTrack.
    // Canonicalise it to the supplied current clip so applying a snapshot cannot accidentally
    // resume the previous queue head during a transition.
    settings.trackQueue[0] = state.currentTrackId;
    settings.trackTypes[0] = static_cast<std::uint8_t>(state.currentTrackType);
    settings.trackIndexes[0] = static_cast<std::int8_t>(state.currentTrackIndex);

    // CAEStreamThread keeps the existing decoder when the track id is unchanged. Queueing a
    // stop first makes its next service pass rebuild the decoder and honour the requested seek.
    StopHardwareTrack();

    trackInterface->requestedSettings = settings;
    trackInterface->activeSettings = settings;
    trackInterface->trackMode = eRadioTrackMode::RADIO_STARTING;
    trackInterface->isInitialised = false;
    trackInterface->radioStationMenuRequest = -1;
    trackInterface->radioStationScriptRequest = -1;
    return true;
}
