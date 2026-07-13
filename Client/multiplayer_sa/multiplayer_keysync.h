/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        multiplayer_sa/multiplayer_keysync.h
 *  PURPOSE:     Multiplayer module keysync methods
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "multiplayersa_init.h"

#pragma once

#include <game/CPlayerPed.h>
#include <game/CStats.h>

struct CColModelSAInterface;
class CColPointSAInterface;

// our stuff
VOID InitKeysyncHooks();

// Dense-entity diagnostics are collected only while timing checkpoints are
// enabled. The world hook owns the frame boundary; the existing collision-list
// hook reports candidates without adding another mid-function GTA hook.
void          EntityPerformanceBeginWorldFrame();
void          EntityPerformanceEndWorldFrame();
void          EntityPerformanceRecordBroadPhaseCandidate(CPhysicalSAInterface* pPhysical, CPhysicalSAInterface* pCandidate);
int32 __cdecl EntityPerformanceProcessColModels(const void* pMatrixA, CColModelSAInterface* pColModelA, const void* pMatrixB, CColModelSAInterface* pColModelB,
                                                CColPointSAInterface* pSpherePoints, CColPointSAInterface* pLinePoints, float* pMaxTouchDistances,
                                                bool returnAllCollisions);

BOOL GetContextSwitchPedID();
BOOL IsNotInLocalContext();

// hooks
VOID HOOK_CPlayerPed__ProcessControl();
VOID HOOK_CAutomobile__ProcessControl();
VOID HOOK_CMonsterTruck__ProcessControl();
VOID HOOK_CTrailer__ProcessControl();
VOID HOOK_CQuadBike__ProcessControl();
VOID HOOK_CPlane__ProcessControl();
VOID HOOK_CBmx__ProcessControl();
VOID HOOK_CTrain__ProcessControl();
VOID HOOK_CBoat__ProcessControl();
VOID HOOK_CVehicle__ProcessUnknown();
VOID HOOK_CBike__ProcessControl();
VOID HOOK_CHeli__ProcessControl();

// gta's stuff
#define VAR_Keystates 0x7DBCB0

#define FUNC_CPlayerPed__ProcessControl   0x60EA90
#define VTBL_CPlayerPed__ProcessControl   0x86D190
#define FUNC_CPlayerPed__ProcessCollision 0x54DFB0
#define VTBL_CPlayerPed__ProcessCollision 0x86D194
#define FUNC_CPlayerPed__PreRender        0x5E8A20
#define VTBL_CPlayerPed__PreRender        0x86D1AC

#define FUNC_CAutomobile__ProcessControl   0x6B1880
#define VTBL_CAutomobile__ProcessControl   0x871148
#define FUNC_CAutomobile__ProcessCollision 0x54DFB0
#define VTBL_CAutomobile__ProcessCollision 0x87114C
#define FUNC_CAutomobile__PreRender        0x6AAB50
#define VTBL_CAutomobile__PreRender        0x871164

#define FUNC_CMonsterTruck__ProcessControl 0x6C8250
#define VTBL_CMonsterTruck__ProcessControl 0x871800

#define FUNC_CTrailer__ProcessControl 0x6CED20
#define VTBL_CTrailer__ProcessControl 0x871C50

#define FUNC_CQuadBike__ProcessControl 0x6CDCC0
#define VTBL_CQuadBike__ProcessControl 0x871B10

#define FUNC_CPlane__ProcessControl 0x6C9260
#define VTBL_CPlane__ProcessControl 0x871970

#define FUNC_CBmx__ProcessControl 0x6BFA30
#define VTBL_CBmx__ProcessControl 0x871550

#define FUNC_CTrain__ProcessControl 0x6F86A0
#define VTBL_CTrain__ProcessControl 0x872398

#define FUNC_CBoat__ProcessControl 0x6F1770
#define VTBL_CBoat__ProcessControl 0x8721C8

#define FUNC_CBike__ProcessControl 0x6B9250
#define VTBL_CBike__ProcessControl 0x871388

#define FUNC_CHeli__ProcessControl 0x6C7050
#define VTBL_CHeli__ProcessControl 0x8716A8

// #define FUNC_CVehicle__ProcessUnknown     0x593030
// #define VTBL_CVehicle__ProcessUnknown     0x69ADB0

#define CODE_SetTargetRotation 0x535851

#define VAR_CameraRotation 0xB6F178
