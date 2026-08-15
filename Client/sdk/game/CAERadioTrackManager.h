/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        sdk/game/CAERadioTrackManager.h
 *  PURPOSE:     Radio track audio entity interface
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

struct SRadioPlaybackState
{
    static constexpr unsigned int QUEUE_SIZE = 5;

    bool          radioOn{};
    unsigned char stationId{};
    int           mode{};
    int           currentTrackId{-1};
    int           currentTrackType{6};
    int           currentTrackIndex{-1};
    int           playTimeMs{-1};
    int           trackLengthMs{-1};
    unsigned char trackFlags{};
    int           trackQueue[QUEUE_SIZE]{-1, -1, -1, -1, -1};
    signed char   trackTypes[QUEUE_SIZE]{6, 6, 6, 6, 6};
    signed char   trackIndexes[QUEUE_SIZE]{-1, -1, -1, -1, -1};
};

class CAERadioTrackManager
{
public:
    virtual BYTE  GetCurrentRadioStationID() = 0;
    virtual BYTE  IsVehicleRadioActive() = 0;
    virtual char* GetRadioStationName(BYTE bStationID) = 0;
    virtual bool  IsRadioOn() = 0;
    virtual void  SetBassSetting(DWORD dwBass) = 0;
    virtual void  Reset() = 0;
    virtual void  StartRadio(BYTE bStationID, BYTE bUnknown) = 0;
    virtual bool  IsStationLoading() const = 0;
    virtual bool  GetPlaybackState(SRadioPlaybackState& state) const = 0;
    virtual bool  SetPlaybackState(const SRadioPlaybackState& state) = 0;
};
