/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CAERadioTrackManagerSA.cpp
 *  PURPOSE:     Header file for audio entity radio track manager class
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <game/CAERadioTrackManager.h>

#define FUNC_GetCurrentRadioStationID 0x4E83F0
#define FUNC_IsVehicleRadioActive     0x4E9800
#define FUNC_GetRadioStationName      0x4E9E10
#define FUNC_IsRadioOn                0x4E8350
#define FUNC_SetBassSetting           0x4E82F0
#define FUNC_Reset                    0x4E7F80
#define FUNC_StartRadio               0x4EB3C0

#define FUNC_CAEAudioHardware_StopTrack         0x4D8F50
#define FUNC_CAEAudioHardware_GetTrackPlayTime  0x4D8F60
#define FUNC_CAEAudioHardware_GetTrackLengthMs  0x4D8F70
#define FUNC_CAEAudioHardware_GetActiveTrackID  0x4D8F80
#define FUNC_CAEAudioHardware_GetPlayingTrackID 0x4D8F90

#define CLASS_CAERadioTrackManager 0x8CB6F8
#define CLASS_CAEAudioHardware     0xB5F8B8

constexpr std::size_t RADIO_STATION_COUNT = 14;

enum class eRadioTrackMode
{
    RADIO_STARTING,
    RADIO_WAITING_TO_PLAY,
    RADIO_PLAYING,
    RADIO_STOPPING,
    RADIO_STOPPING_SILENCED,
    RADIO_STOPPING_CHANNELS_STOPPED,
    RADIO_WAITING_TO_STOP,
    RADIO_STOPPED
};

struct tRadioSettings
{
    std::int32_t trackQueue[5];
    std::int32_t currentTrackId;
    std::int32_t prevTrackId;
    std::int32_t trackPlayTime;
    std::int32_t trackLengthInMS;
    std::uint8_t trackFlags;
    std::uint8_t currentRadioStation;
    std::uint8_t bassSet;
    std::uint8_t field_27;
    float        bassGain;
    std::uint8_t trackTypes[5];
    std::uint8_t currentTrackType;
    std::uint8_t prevTrackType;
    std::int8_t  trackIndexes[5];
    std::int8_t  currentTrackIndex;
    std::int8_t  prevTrackIndex;
};
static_assert(sizeof(tRadioSettings) == 0x3C, "Invalid size of tRadioSettings struct!");
static_assert(offsetof(tRadioSettings, currentTrackId) == 0x14, "Invalid current track offset!");
static_assert(offsetof(tRadioSettings, trackPlayTime) == 0x1C, "Invalid play time offset!");
static_assert(offsetof(tRadioSettings, currentRadioStation) == 0x25, "Invalid station offset!");
static_assert(offsetof(tRadioSettings, trackTypes) == 0x2C, "Invalid track type offset!");

struct tRadioState
{
    std::int32_t elapsed[3];
    std::int32_t timeInPauseModeInMS;
    std::int32_t timeInMS;
    std::int32_t trackPlayTime;
    std::int32_t trackQueue[3];
    std::uint8_t trackTypes[3];
    std::uint8_t gameMonthDay;
    std::uint8_t gameClockHours;
};
static_assert(sizeof(tRadioState) == 0x2C, "Invalid size of tRadioState struct!");

class CAERadioTrackManagerSAInterface
{
public:
    bool            isInitialised;
    bool            displayStationName;
    std::uint8_t    field_2;
    bool            enableInPauseMode;
    bool            bassEnhance;
    bool            pauseMode;
    bool            retuneJustStarted;
    bool            autoSelect;
    std::uint8_t    tracksInARow[RADIO_STATION_COUNT];
    std::uint8_t    gameMonthDay;
    std::uint8_t    gameClockHours;
    std::int32_t    listenItems[RADIO_STATION_COUNT];
    std::uint32_t   timeRadioStationReturned;
    std::uint32_t   timeToDisplayRadioName;
    std::uint32_t   savedTimeInMS;
    std::uint32_t   retuneStartedTime;
    std::uint32_t   field_60;
    std::int32_t    hwClientHandle;
    eRadioTrackMode trackMode;
    std::int32_t    stationsListed;
    std::int32_t    stationsListDown;
    std::int32_t    savedRadioStationId;
    std::int32_t    radioStationMenuRequest;
    std::int32_t    radioStationScriptRequest;
    float           volume1;
    float           volume2;
    tRadioSettings  requestedSettings;
    tRadioSettings  activeSettings;
    tRadioState     radioState[RADIO_STATION_COUNT];
    std::uint32_t   field_368;
    std::uint8_t    userTrackPlayMode;
    std::uint8_t    field_36D[3];
};
static_assert(sizeof(CAERadioTrackManagerSAInterface) == 0x370, "Invalid size of CAERadioTrackManagerSAInterface class!");
static_assert(offsetof(CAERadioTrackManagerSAInterface, requestedSettings) == 0x88, "Invalid requested settings offset!");
static_assert(offsetof(CAERadioTrackManagerSAInterface, activeSettings) == 0xC4, "Invalid active settings offset!");
static_assert(offsetof(CAERadioTrackManagerSAInterface, radioState) == 0x100, "Invalid radio state offset!");
static_assert(offsetof(CAERadioTrackManagerSAInterface, field_368) == 0x368, "Invalid tail offset!");

class CAERadioTrackManagerSA : public CAERadioTrackManager
{
public:
    CAERadioTrackManagerSAInterface* GetInterface() const noexcept { return reinterpret_cast<CAERadioTrackManagerSAInterface*>(CLASS_CAERadioTrackManager); }

    BYTE  GetCurrentRadioStationID();
    BYTE  IsVehicleRadioActive();
    char* GetRadioStationName(BYTE bStationID);
    bool  IsRadioOn();
    void  SetBassSetting(DWORD dwBass);
    void  Reset();
    void  StartRadio(BYTE bStationID, BYTE bUnknown);
    bool  IsStationLoading() const;
    bool  GetPlaybackState(SRadioPlaybackState& state) const override;
    bool  SetPlaybackState(const SRadioPlaybackState& state) override;
};
