/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CPedIntelligenceSA.h
 *  PURPOSE:     Header file for ped entity AI class
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <cstddef>

#include <game/CPedIntelligence.h>

class CPedSAInterface;
class CTaskManagerSA;

#define FUNC_CPedIntelligence_TestForStealthKill 0x601E00

class CFightManagerInterface
{
public:
    BYTE  Pad1[16];
    BYTE  UnknownState;
    BYTE  Pad2[3];
    float fStrafeState;
    float fForwardBackwardState;
};

class CPedIntelligenceSAInterface
{
public:
    // CEventHandlerHistory @ + 56
    CPedSAInterface*        pPed;
    DWORD                   taskManager;  // +4 (really CTaskManagerSAInterface)
    BYTE                    bPad[16];
    CFightManagerInterface* fightInterface;  // +24
    BYTE                    bPad2[28];
    BYTE                    eventHandler[0x30];
    // CEventGroup is embedded here in the target executable. Keeping the
    // verified span explicit lets narrow native-event bridges use GTA's own
    // clone/admission lifecycle without reconstructing intelligence state.
    BYTE         eventGroup[0x4C];
    std::int32_t decisionMakerType;         // +180
    std::int32_t decisionMakerTypeInGroup;  // +184
    float        hearingRange;              // +188
    float        seeingRange;               // +192
    DWORD        numPedsToScan;             // +196
    float        decisionMakerRadius;       // +200
    BYTE         bPad3[8];
    DWORD        vehicleScanner;  // +212 (really CVehicleScannerSAInterface)
};

static_assert(offsetof(CPedIntelligenceSAInterface, decisionMakerType) == 0xB4, "Invalid ped decision-maker type offset");
static_assert(offsetof(CPedIntelligenceSAInterface, eventHandler) == 0x38, "Invalid ped event-handler offset");
static_assert(offsetof(CPedIntelligenceSAInterface, eventGroup) == 0x68, "Invalid ped event-group offset");
static_assert(offsetof(CPedIntelligenceSAInterface, decisionMakerTypeInGroup) == 0xB8, "Invalid ped group decision-maker type offset");
static_assert(offsetof(CPedIntelligenceSAInterface, hearingRange) == 0xBC, "Invalid ped hearing range offset");
static_assert(offsetof(CPedIntelligenceSAInterface, seeingRange) == 0xC0, "Invalid ped seeing range offset");
static_assert(offsetof(CPedIntelligenceSAInterface, numPedsToScan) == 0xC4, "Invalid ped scan-count offset");
static_assert(offsetof(CPedIntelligenceSAInterface, decisionMakerRadius) == 0xC8, "Invalid ped decision-maker radius offset");

class CPedIntelligenceSA : public CPedIntelligence
{
private:
    CPedIntelligenceSAInterface* internalInterface;
    CPed*                        ped;
    CTaskManagerSA*              TaskManager;

public:
    CPedIntelligenceSA(CPedIntelligenceSAInterface* pedIntelligenceSAInterface, CPed* ped);
    ~CPedIntelligenceSA();
    CPedIntelligenceSAInterface* GetInterface() { return internalInterface; }
    CTaskManager*                GetTaskManager();
    bool                         TestForStealthKill(CPed* pPed, bool bUnk);
    CTaskSAInterface*            SetTaskDuckSecondary(unsigned short nLengthOfDuck);
    CTaskSimpleUseGun*           GetTaskUseGun();
    CTaskSimpleFight*            GetFightTask();
};
