/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CTasksSAStory.cpp
 *  PURPOSE:     Verified story-task factories appended to the public task API
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CTaskManagementSystemSA.h"
#include "CTasksSA.h"
#include "CVehicleSA.h"
#include "TaskSA.h"
#include <game/CEntity.h>

namespace
{
    constexpr DWORD FUNC_CTaskComplexCarDriveMission__Constructor = 0x63CC30;

    class CTaskComplexCarDriveMissionStorySAInterface : public CTaskComplexSAInterface
    {
    public:
        unsigned char m_nativeFields[0x20];
    };
    static_assert(sizeof(CTaskComplexCarDriveMissionStorySAInterface) == 0x2C, "Invalid CTaskComplexCarDriveMission interface size");

    class CTaskComplexCarDriveMissionStorySA : public virtual CTaskComplexSA
    {
    public:
        CTaskComplexCarDriveMissionStorySA(CVehicle* pVehicle, CEntity* pTarget, int iMission, int iDrivingStyle, float fSpeed)
        {
            auto* pVehicleSA = dynamic_cast<CVehicleSA*>(pVehicle);
            if (!pVehicleSA || !pTarget || !pTarget->GetInterface())
                return;

            CreateTaskInterface(sizeof(CTaskComplexCarDriveMissionStorySAInterface));
            if (!IsValid())
                return;

            const DWORD dwThis = reinterpret_cast<DWORD>(GetInterface());
            const DWORD dwVehicle = reinterpret_cast<DWORD>(pVehicleSA->GetInterface());
            const DWORD dwTarget = reinterpret_cast<DWORD>(pTarget->GetInterface());
            const DWORD dwFunc = FUNC_CTaskComplexCarDriveMission__Constructor;

            // Compact GTA:SA 1.0, SHA-256
            // 72ae59e44c761389e354a50dc6215e964fe771121e2f4b1877273a493ceecc9b.
            // 0x63CC30 is __thiscall(vehicle, target, mission, style, speed).
            // Calling GTA preserves its safe target reference, autopilot setup,
            // clone/destructor lifecycle and restoration of the previous mission.
            // clang-format off
            __asm
            {
                mov     ecx, dwThis
                push    fSpeed
                push    iDrivingStyle
                push    iMission
                push    dwTarget
                push    dwVehicle
                call    dwFunc
            }
            // clang-format on
        }
    };
}

CTaskComplex* CTasksSA::CreateTaskComplexCarDriveMission(CVehicle* pVehicle, CEntity* pTarget, int iMission, int iDrivingStyle, float fSpeed)
{
    auto* pTask = NewTask<CTaskComplexCarDriveMissionStorySA>(pVehicle, pTarget, iMission, iDrivingStyle, fSpeed);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}
