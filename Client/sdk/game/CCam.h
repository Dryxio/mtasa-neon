/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        sdk/game/CCam.h
 *  PURPOSE:     Camera entity interface
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

class CEntity;
class CVector;

class CCam
{
public:
    virtual CVector*     GetFront() const = 0;
    virtual CVector*     GetUp() const = 0;
    virtual CVector*     GetSource() const = 0;
    virtual CVector*     GetFixedModeSource() const = 0;
    virtual CVector*     GetFixedModeVector() const = 0;
    virtual CVector*     GetTargetHistoryPos() const = 0;
    virtual unsigned int GetMode() const = 0;
    virtual float        GetFOV() const = 0;
    virtual void         SetFOV(float fFOV) = 0;
    virtual void         GetDirection(float& fHorizontal, float& fVertical) = 0;
    virtual void         SetDirection(float fHorizontal, float fVertical) = 0;
    virtual CEntity*     GetTargetEntity() const = 0;
    virtual void         SetTargetEntity(CEntity* pEntity) = 0;

    // Local gameplay aim in degrees. Unlike SetDirection, these reject camera
    // states that would overwrite the requested angle and enforce native limits.
    // Append new slots so existing camera consumers retain their ABI layout.
    virtual bool GetAimDirection(float& horizontal, float& vertical) = 0;
    virtual bool SetAimDirection(float horizontal, float vertical) = 0;
};
