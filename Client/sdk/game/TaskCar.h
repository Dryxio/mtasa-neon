/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        sdk/game/TaskCar.h
 *  PURPOSE:     Car task interface
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include "Task.h"

enum
{
    DOOR_FRONT_LEFT = 0,
    DOOR_FRONT_RIGHT = 8,
    DOOR_REAR_RIGHT = 9,
    DOOR_REAR_LEFT = 11
};

enum eCarDrivingStyle
{
    DRIVING_STYLE_STOP_FOR_CARS = 0,
    DRIVING_STYLE_SLOW_DOWN_FOR_CARS,
    DRIVING_STYLE_AVOID_CARS,
    DRIVING_STYLE_PLOUGH_THROUGH,
    DRIVING_STYLE_STOP_FOR_CARS_IGNORE_LIGHTS,
    DRIVING_STYLE_AVOID_CARS_OBEY_LIGHTS,
    DRIVING_STYLE_AVOID_CARS_STOP_FOR_PEDS_OBEY_LIGHTS,
};

class CTaskComplexEnterCar : public virtual CTaskComplex
{
public:
    virtual ~CTaskComplexEnterCar() {};

    virtual int  GetTargetDoor() = 0;
    virtual void SetTargetDoor(int iDoor) = 0;
    virtual int  GetEnterCarStartTime() = 0;
};

class CTaskComplexEnterCarAsDriver : public virtual CTaskComplexEnterCar
{
public:
    virtual ~CTaskComplexEnterCarAsDriver() {};
};

class CTaskComplexEnterCarAsPassenger : public virtual CTaskComplexEnterCar
{
public:
    virtual ~CTaskComplexEnterCarAsPassenger() {};
};

class CTaskComplexEnterBoatAsDriver : public virtual CTaskComplex
{
public:
    virtual ~CTaskComplexEnterBoatAsDriver() {};
};

class CTaskComplexLeaveCar : public virtual CTaskComplex
{
public:
    virtual ~CTaskComplexLeaveCar() {};

    virtual int GetTargetDoor() = 0;
};

class CTaskComplexCarDriveWander : public virtual CTaskComplex
{
public:
    virtual ~CTaskComplexCarDriveWander() {};
};

class CTaskComplexCarDriveToPoint : public virtual CTaskComplex
{
public:
    virtual ~CTaskComplexCarDriveToPoint() {};
};
