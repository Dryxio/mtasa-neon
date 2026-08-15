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
    constexpr int         INVALID_TRACK_ID = -1;
    constexpr signed char TRACK_TYPE_NONE = 6;

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

    void ClearQueueEntry(SRadioPlaybackState& state, unsigned int index)
    {
        state.trackQueue[index] = INVALID_TRACK_ID;
        state.trackTypes[index] = TRACK_TYPE_NONE;
        state.trackIndexes[index] = -1;
    }

    void CanonicalisePlaybackQueue(SRadioPlaybackState& state, bool forcePrepend = false)
    {
        if (state.currentTrackId < 0)
            return;

        int currentIndex = -1;
        if (!forcePrepend)
        {
            for (unsigned int i = 0; i < SRadioPlaybackState::QUEUE_SIZE; ++i)
            {
                if (state.trackQueue[i] == state.currentTrackId)
                {
                    currentIndex = static_cast<int>(i);
                    break;
                }
            }
        }

        if (currentIndex > 0)
        {
            const unsigned int shift = static_cast<unsigned int>(currentIndex);
            const unsigned int remaining = SRadioPlaybackState::QUEUE_SIZE - shift;
            for (unsigned int i = 0; i < remaining; ++i)
            {
                state.trackQueue[i] = state.trackQueue[i + shift];
                state.trackTypes[i] = state.trackTypes[i + shift];
                state.trackIndexes[i] = state.trackIndexes[i + shift];
            }
            for (unsigned int i = remaining; i < SRadioPlaybackState::QUEUE_SIZE; ++i)
                ClearQueueEntry(state, i);
        }
        else if (currentIndex < 0)
        {
            for (unsigned int i = SRadioPlaybackState::QUEUE_SIZE - 1; i > 0; --i)
            {
                state.trackQueue[i] = state.trackQueue[i - 1];
                state.trackTypes[i] = state.trackTypes[i - 1];
                state.trackIndexes[i] = state.trackIndexes[i - 1];
            }
        }

        state.trackQueue[0] = state.currentTrackId;
        state.trackTypes[0] = static_cast<signed char>(state.currentTrackType);
        state.trackIndexes[0] = static_cast<signed char>(state.currentTrackIndex);
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

    const int playingTrackId = GetHardwareInt(FUNC_CAEAudioHardware_GetPlayingTrackID);
    const int activeTrackId = GetHardwareInt(FUNC_CAEAudioHardware_GetActiveTrackID);
    state.currentTrackId = playingTrackId >= 0 ? playingTrackId : activeTrackId;
    state.playTimeMs = GetHardwareInt(FUNC_CAEAudioHardware_GetTrackPlayTime);
    state.trackLengthMs = GetHardwareInt(FUNC_CAEAudioHardware_GetTrackLengthMs);
    state.trackFlags = settings.trackFlags;

    for (unsigned int i = 0; i < SRadioPlaybackState::QUEUE_SIZE; ++i)
    {
        state.trackQueue[i] = settings.trackQueue[i];
        state.trackTypes[i] = static_cast<signed char>(settings.trackTypes[i]);
        state.trackIndexes[i] = settings.trackIndexes[i];
    }

    state.currentTrackType = settings.currentTrackType;
    state.currentTrackIndex = settings.currentTrackIndex;

    bool currentIsPrevious = false;
    if (state.currentTrackId >= 0)
    {
        int matchingQueueIndex = -1;
        for (unsigned int i = 0; i < SRadioPlaybackState::QUEUE_SIZE; ++i)
        {
            if (state.trackQueue[i] == state.currentTrackId)
            {
                matchingQueueIndex = static_cast<int>(i);
                break;
            }
        }

        if (matchingQueueIndex >= 0)
        {
            state.currentTrackType = state.trackTypes[matchingQueueIndex];
            state.currentTrackIndex = state.trackIndexes[matchingQueueIndex];
        }
        else if (settings.prevTrackId == state.currentTrackId)
        {
            state.currentTrackType = settings.prevTrackType;
            state.currentTrackIndex = settings.prevTrackIndex;
            currentIsPrevious = true;
        }

        // The audio thread can switch to queue[1] before the radio manager rotates its queue.
        // Return a coherent snapshot where queue[0] is always the hardware-current track.
        CanonicalisePlaybackQueue(state, currentIsPrevious);
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

    SRadioPlaybackState normalisedState = state;
    CanonicalisePlaybackQueue(normalisedState);

    auto settings = trackInterface->activeSettings;
    settings.currentRadioStation = normalisedState.stationId;
    settings.currentTrackId = normalisedState.currentTrackId;
    settings.prevTrackId = -1;
    settings.trackPlayTime = normalisedState.playTimeMs;
    settings.trackLengthInMS = normalisedState.trackLengthMs;
    settings.trackFlags = normalisedState.trackFlags;
    settings.currentTrackType = static_cast<std::uint8_t>(normalisedState.currentTrackType);
    settings.currentTrackIndex = static_cast<std::int8_t>(normalisedState.currentTrackIndex);
    settings.prevTrackType = TRACK_TYPE_NONE;
    settings.prevTrackIndex = -1;

    for (unsigned int i = 0; i < SRadioPlaybackState::QUEUE_SIZE; ++i)
    {
        settings.trackQueue[i] = normalisedState.trackQueue[i];
        settings.trackTypes[i] = static_cast<std::uint8_t>(normalisedState.trackTypes[i]);
        settings.trackIndexes[i] = normalisedState.trackIndexes[i];
    }

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
