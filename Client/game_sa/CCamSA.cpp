/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CCamSA.cpp
 *  PURPOSE:     Camera entity
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CCamSA.h"
#include "CGameSA.h"
#include "CCameraSA.h"
#include "CameraAimMath.h"
#include <game/CPedIntelligence.h>
#include <game/CTaskManager.h>
#include <game/TaskTypes.h>
#include <cmath>
#include <cfloat>

namespace
{
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = 6.28318530717958647692f;

    inline float WrapAngleRad(float angle) noexcept
    {
        angle -= kTwoPi * std::floor((angle + kPi) / kTwoPi);
        if (angle <= -kPi)
            angle += kTwoPi;
        else if (angle > kPi)
            angle -= kTwoPi;
        return angle;
    }
}

extern CGameSA* pGame;

CEntity* CCamSA::GetTargetEntity() const
{
    if (!m_pInterface)
        return nullptr;

    if (!pGame)
        return nullptr;

    CEntitySAInterface* pInterface = m_pInterface->CamTargetEntity;
    if (pInterface)
    {
        CPools* pPools = pGame->GetPools();
        if (pPools)
            return pPools->GetEntity((DWORD*)pInterface);
    }
    return nullptr;
}

void CCamSA::SetTargetEntity(CEntity* pEntity)
{
    if (!m_pInterface)
        return;

    if (pEntity)
    {
        auto pEntityInterface = pEntity->GetInterface();
        if (!pEntityInterface)
            return;

        m_pInterface->CamTargetEntity = pEntityInterface;
    }
    else
    {
        m_pInterface->CamTargetEntity = nullptr;
    }
}

void CCamSA::GetDirection(float& fHorizontal, float& fVertical)
{
    if (!m_pInterface)
    {
        fHorizontal = 0.0f;
        fVertical = 0.0f;
        return;
    }

    const float fHoriz = std::isfinite(m_pInterface->m_fHorizontalAngle) ? WrapAngleRad(m_pInterface->m_fHorizontalAngle) : 0.0f;
    const float fVert = std::isfinite(m_pInterface->m_fVerticalAngle) ? WrapAngleRad(m_pInterface->m_fVerticalAngle) : 0.0f;

    if (!std::isfinite(m_pInterface->m_fHorizontalAngle) || !std::isfinite(m_pInterface->m_fVerticalAngle))
    {
        m_pInterface->m_fHorizontalAngle = fHoriz;
        m_pInterface->m_fVerticalAngle = fVert;
    }

    fHorizontal = fHoriz;
    fVertical = fVert;
}

void CCamSA::SetDirection(float fHorizontal, float fVertical)
{
    if (!m_pInterface)
        return;

    // Validate input float values
    if (!std::isfinite(fHorizontal) || !std::isfinite(fVertical))
        return;

    m_pInterface->m_fHorizontalAngle = WrapAngleRad(fHorizontal);
    m_pInterface->m_fVerticalAngle = WrapAngleRad(fVertical);
}

bool CCamSA::GetAimLimits(float& minimum, float& maximum, bool& runabout) const
{
    if (!m_pInterface || !pGame || m_pInterface->ResetStatics)
        return false;

    auto* camera = static_cast<CCameraSA*>(pGame->GetCamera());
    if (!camera || camera->GetCam(camera->GetActiveCam()) != this || camera->IsInTransition())
        return false;

    auto* ped = dynamic_cast<CPedSA*>(GetTargetEntity());
    if (!ped)
        return false;
    auto* nativePed = ped->GetPedInterface();
    if (!nativePed || nativePed->bPedType != 0 || nativePed->pTargetedObject || nativePed->m_pAttachedEntity || !std::isfinite(nativePed->fHealth) ||
        nativePed->fHealth <= 0.0f)
        return false;

    // These are the final clamps in the retail 1.0 camera processors, not a
    // generic +/-90 degree limit. Reading the native tuning also respects any
    // engine adjustment to the limits. Target lock and attached/turret cameras
    // are excluded because their processors steer back to a separate target.
    runabout = false;
    switch (m_pInterface->Mode)
    {
        case MODE_AIMWEAPON:
        case MODE_AIMWEAPON_FROMCAR:
        {
            if (nativePed->bCurrentWeaponSlot >= WEAPONSLOT_MAX)
                return false;
            auto* weaponInfo = pGame->GetWeaponInfo(nativePed->Weapons[nativePed->bCurrentWeaponSlot].m_eWeaponType);
            // The same native processor has a separate melee orbit profile;
            // this API intentionally controls firearm/free-aim cameras only.
            if (!weaponInfo || weaponInfo->GetFireType() == FIRETYPE_MELEE)
                return false;
            unsigned int profile = nativePed->pedFlags.bInVehicle ? 2 : 0;
            auto*        intelligence = ped->GetPedIntelligence();
            auto*        tasks = intelligence ? intelligence->GetTaskManager() : nullptr;
            if (!profile && tasks && tasks->FindActiveTaskByType(TASK_SIMPLE_JETPACK))
                profile = 1;
            minimum = -*reinterpret_cast<const float*>(0x8CC4D8 + profile * 0x1C);
            maximum = *reinterpret_cast<const float*>(0x8CC4D4 + profile * 0x1C);
            break;
        }
        case MODE_SNIPER:
        case MODE_M16_1STPERSON:
            if (nativePed->pedFlags.bInVehicle || nativePed->pVehicle)
                return false;
            // Process_M16_1stPerson (0x5105C0) applies an additional +/-1.2
            // radian clamp after its initial -85.5/+60 degree limits.
            minimum = std::max(*reinterpret_cast<const float*>(0x8631BC), -*reinterpret_cast<const float*>(0x8CCC90));
            maximum = std::min(*reinterpret_cast<const float*>(0x8630F8), *reinterpret_cast<const float*>(0x8CCC90));
            break;
        case MODE_SNIPER_RUNABOUT:
        case MODE_M16_1STPERSON_RUNABOUT:
            if (nativePed->pedFlags.bInVehicle || nativePed->pVehicle)
                return false;
            runabout = true;
            minimum = *reinterpret_cast<const float*>(0x8630F4);
            maximum = *reinterpret_cast<const float*>(0x8630F8);
            break;
        default:
            return false;
    }

    return std::isfinite(minimum) && std::isfinite(maximum) && minimum <= maximum && minimum >= -CameraAimMath::Pi / 2.0f &&
           maximum <= CameraAimMath::Pi / 2.0f;
}

bool CCamSA::GetAimDirection(float& horizontal, float& vertical)
{
    float minimum, maximum;
    bool  runabout;
    return GetAimLimits(minimum, maximum, runabout) &&
           CameraAimMath::Decode(m_pInterface->m_fHorizontalAngle, m_pInterface->m_fVerticalAngle, runabout, horizontal, vertical);
}

bool CCamSA::SetAimDirection(float horizontal, float vertical)
{
    float minimum, maximum, beta, alpha;
    bool  runabout;
    if (!GetAimLimits(minimum, maximum, runabout) || !CameraAimMath::Encode(horizontal, vertical, minimum, maximum, runabout, beta, alpha))
        return false;

    m_pInterface->m_fHorizontalAngle = beta;
    m_pInterface->m_fVerticalAngle = alpha;
    // AimWeapon and M16_1stPerson integrate these velocities even after mouse
    // motion stops. Clear the old input tail, not the player's input controls.
    m_pInterface->AlphaSpeed = 0.0f;
    m_pInterface->BetaSpeed = 0.0f;
    m_pInterface->m_fAlphaSpeedOverOneFrame = 0.0f;
    m_pInterface->m_fBetaSpeedOverOneFrame = 0.0f;
    // Sniper bump is an angular oscillator, separate from CCamera's visual
    // shake. Leaving it alive would keep changing a scripted recoil angle.
    m_pInterface->m_fCamBumpedHorz = 0.0f;
    m_pInterface->m_fCamBumpedVert = 0.0f;
    m_pInterface->m_nCamBumpedTime = 0;
    return true;
}
