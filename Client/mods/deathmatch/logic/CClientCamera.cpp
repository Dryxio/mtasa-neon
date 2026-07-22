/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *               (Shared logic for modifications)
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/shared_logic/CClientCamera.cpp
 *  PURPOSE:     Camera entity class
 *
 *****************************************************************************/

#include <StdInc.h>
#include <game/CCam.h>
#include <array>
#include <cmath>
#include <utility>

#define PI_2 6.283185307179586476925286766559f

// Camera FOV constants
constexpr const std::uintptr_t VAR_CurrentCameraFOV = 0x8D5038;  // CCamera::CurrentFOV

namespace
{
    constexpr float       kPi = 3.1415926535897932384626433832795f;
    constexpr float       kMatrixTolerance = 1e-4f;
    constexpr float       kAngleTolerance = 1e-4f;
    constexpr float       kRollLookupStep = 15.0f;
    constexpr std::size_t kRollLookupSize = static_cast<std::size_t>(360.0f / kRollLookupStep);

    struct RollLookupEntry
    {
        float degrees;
        float cosValue;
        float sinValue;
    };

    const std::array<RollLookupEntry, kRollLookupSize> kRollLookup = []()
    {
        std::array<RollLookupEntry, kRollLookupSize> table = {};
        for (std::size_t i = 0; i < kRollLookupSize; ++i)
        {
            float degrees = static_cast<float>(i) * kRollLookupStep;
            float radians = ConvertDegreesToRadiansNoWrap(degrees);
            table[i] = {degrees, std::cos(radians), std::sin(radians)};
        }
        return table;
    }();

    inline float WrapDegrees(float angle) noexcept
    {
        angle -= 360.0f * std::floor((angle + 180.0f) / 360.0f);
        if (angle <= -180.0f)
            angle += 360.0f;
        else if (angle > 180.0f)
            angle -= 360.0f;
        return angle;
    }

    inline bool AlmostEqual(float lhs, float rhs, float epsilon = kMatrixTolerance) noexcept
    {
        return std::fabs(lhs - rhs) <= epsilon;
    }

    inline bool VectorsClose(const CVector& a, const CVector& b, float epsilon = kMatrixTolerance) noexcept
    {
        return AlmostEqual(a.fX, b.fX, epsilon) && AlmostEqual(a.fY, b.fY, epsilon) && AlmostEqual(a.fZ, b.fZ, epsilon);
    }

    inline bool MatricesClose(const CMatrix& a, const CMatrix& b, float epsilon = kMatrixTolerance) noexcept
    {
        return VectorsClose(a.vFront, b.vFront, epsilon) && VectorsClose(a.vUp, b.vUp, epsilon) && VectorsClose(a.vPos, b.vPos, epsilon);
    }

    inline std::pair<float, float> GetRollSinCos(float rollDegrees) noexcept
    {
        float normalized = WrapDegrees(rollDegrees);
        float index = normalized / kRollLookupStep;
        float rounded = std::round(index);
        if (std::fabs(index - rounded) <= kAngleTolerance / kRollLookupStep)
        {
            int wrapped = static_cast<int>(std::lround(rounded)) % static_cast<int>(kRollLookupSize);
            if (wrapped < 0)
                wrapped += static_cast<int>(kRollLookupSize);
            std::size_t tableIndex = static_cast<std::size_t>(wrapped);
            const auto& entry = kRollLookup[tableIndex];
            return {entry.cosValue, entry.sinValue};
        }

        float radians = ConvertDegreesToRadiansNoWrap(normalized);
        return {std::cos(radians), std::sin(radians)};
    }

    inline bool RequiresOrthonormalization(const CMatrix& matrix) noexcept
    {
        constexpr float lengthTarget = 1.0f;
        const bool      frontValid = AlmostEqual(matrix.vFront.LengthSquared(), lengthTarget, 1e-3f);
        const bool      upValid = AlmostEqual(matrix.vUp.LengthSquared(), lengthTarget, 1e-3f);
        const bool      orthogonal = AlmostEqual(matrix.vFront.DotProduct(&matrix.vUp), 0.0f, 1e-3f);
        return !(frontValid && upValid && orthogonal);
    }

    inline void EnsureOrthonormal(CMatrix& matrix)
    {
        if (RequiresOrthonormalization(matrix))
            matrix.OrthoNormalize(CMatrix::AXIS_FRONT, CMatrix::AXIS_UP);
    }

    inline CMatrix& ScratchMatrix() noexcept
    {
        static thread_local CMatrix s_matrix;
        return s_matrix;
    }
}

CClientCamera::CClientCamera(CClientManager* pManager) : ClassInit(this), CClientEntity(INVALID_ELEMENT_ID)
{
    CClientEntityRefManager::AddEntityRefs(ENTITY_REF_DEBUG(this, "CClientCamera"), &m_pFocusedPlayer, &m_pFocusedEntity, NULL);

    // Init
    m_pManager = pManager;
    m_pPlayerManager = m_pManager->GetPlayerManager();

    m_pFocusedPlayer = NULL;
    m_pFocusedEntity = NULL;
    m_pFocusedGameEntity = NULL;
    m_bInvalidated = false;
    m_bFixed = false;
    m_fRoll = 0.0f;
    m_fFOV = DEFAULT_FOV;
    SetTypeName("camera");

    m_pCamera = g_pGame ? g_pGame->GetCamera() : nullptr;

    // Hook handler for the fixed camera
    if (g_pMultiplayer)
        g_pMultiplayer->SetProcessCamHandler(CClientCamera::StaticProcessFixedCamera);

    m_FixedCameraMode = EFixedCameraMode::ROTATION;
    m_hasCachedPulseMatrix = false;
    m_hasCachedPulseEuler = false;
    m_lastCenterOfWorldPos = CVector();
    m_lastCenterOfWorldRot = 0.0f;
    m_hasCenterOfWorld = false;
}

CClientCamera::~CClientCamera()
{
    AbortScriptCamera();

    // We need to be ingame
    if (g_pGame && g_pGame->GetSystemState() == SystemState::GS_PLAYING_GAME)
    {
        // Restore the camera to the local player
        SetFocusToLocalPlayerImpl();
    }
    CClientEntityRefManager::RemoveEntityRefs(0, &m_pFocusedPlayer, &m_pFocusedEntity, NULL);
}

void CClientCamera::DoPulse()
{
    InvalidateCachedTransforms();

    // Native script cameras bypass MTA's fixed-camera matrix, but GTA still
    // streams the scene around the camera rather than the distant player. Keep
    // MTA's streaming override on the native camera for the full lease so a
    // one-shot LOAD_SCENE is not immediately displaced by player-centred LODs.
    if (m_pScriptCameraOwner && !m_bFileCutsceneLease)
    {
        const CMatrix& scriptCameraMatrix = AcquirePulseMatrix();
        UpdateCenterOfWorldFromMatrix(scriptCameraMatrix);
        m_matFixedMatrix = scriptCameraMatrix;
        return;
    }

    // If we're fixed, force the target vector
    if (m_bFixed)
    {
        UpdateCenterOfWorldFromFixedMatrix();
    }
    else
    {
        // If we aren't invalidated
        if (!m_bInvalidated)
        {
            // If we got the camera behind a player but no focused entity
            if (m_pFocusedPlayer)
            {
                if (m_pFocusedEntity)
                {
                    // Is the focused entity a vehicle, but the player doesn't have any occupied?
                    CClientVehicle* pVehicle = m_pFocusedPlayer->GetOccupiedVehicle();
                    if (m_pFocusedEntity->GetType() == CCLIENTVEHICLE)
                    {
                        if (!pVehicle)
                        {
                            SetFocus(m_pFocusedPlayer, MODE_CAM_ON_A_STRING);
                        }
                    }
                    else if (pVehicle)
                    {
                        SetFocus(m_pFocusedPlayer, MODE_CAM_ON_A_STRING);
                    }
                }
                else
                {
                    SetFocus(m_pFocusedPlayer, MODE_CAM_ON_A_STRING);
                }
            }

            // Make sure the world center is where the camera is
            if (m_pFocusedGameEntity)
            {
                // Make sure we have the proper rotation for what we're spectating
                float fRotation = 0;
                if (m_pFocusedEntity)
                {
                    eClientEntityType eType = m_pFocusedEntity->GetType();
                    if (eType == CCLIENTVEHICLE)
                    {
                        CVector vecVehicleRotation;
                        static_cast<CClientVehicle*>((CClientEntity*)m_pFocusedEntity)->GetRotationRadians(vecVehicleRotation);
                        fRotation = vecVehicleRotation.fZ * 3.14159f / 180;
                    }
                    else if (eType == CCLIENTPED || eType == CCLIENTPLAYER)
                    {
                        fRotation = static_cast<CClientPed*>((CClientEntity*)m_pFocusedEntity)->GetCurrentRotation();
                    }
                }

                // Set the new world center/rotation
                SetCenterOfWorldCached(m_pFocusedGameEntity->GetPosition(), fRotation);
            }
            else
            {
                SetCenterOfWorldCached(NULL, 0.0f);
            }
        }

        // Save this so position or rotation is preserved when changing to fixed mode
        m_matFixedMatrix = AcquirePulseMatrix();
    }
}

bool CClientCamera::GetMatrix(CMatrix& Matrix) const
{
    if (m_bFixed)
        Matrix = m_matFixedMatrix;
    else
        Matrix = AcquirePulseMatrix();
    return true;
}

bool CClientCamera::SetMatrix(const CMatrix& Matrix)
{
    // Switch to fixed mode if required
    if (!IsInFixedMode())
        ToggleCameraFixedMode(true);

    m_matFixedMatrix = Matrix;
    EnsureOrthonormal(m_matFixedMatrix);
    m_FixedCameraMode = EFixedCameraMode::MATRIX;
    UpdateCenterOfWorldFromFixedMatrix();
    InvalidateCachedTransforms();
    return true;
}

void CClientCamera::GetPosition(CVector& vecPosition) const
{
    CMatrix matTemp;
    GetMatrix(matTemp);
    vecPosition = matTemp.vPos;
}

void CClientCamera::SetPosition(const CVector& vecPosition)
{
    if (!std::isfinite(vecPosition.fX) || !std::isfinite(vecPosition.fY) || !std::isfinite(vecPosition.fZ))
        return;

    // Switch to fixed mode if required
    if (!IsInFixedMode())
        ToggleCameraFixedMode(true);

    // Update cached matrix position and world center lazily
    m_matFixedMatrix.vPos = vecPosition;
    UpdateCenterOfWorldFromFixedMatrix();
    InvalidateCachedTransforms();
}

void CClientCamera::GetRotationDegrees(CVector& vecRotation) const
{
    CVector eulerRadians(0.0f, 0.0f, 0.0f);

    if (m_bFixed)
    {
        if (g_pMultiplayer)
        {
            CMatrix matrix = m_matFixedMatrix;
            EnsureOrthonormal(matrix);
            g_pMultiplayer->ConvertMatrixToEulerAngles(matrix, eulerRadians.fX, eulerRadians.fY, eulerRadians.fZ);
        }
    }
    else
    {
        const CMatrix& matrix = AcquirePulseMatrix();
        eulerRadians = AcquirePulseEuler(matrix);
    }

    vecRotation = CVector(PI_2, PI_2, PI_2) - eulerRadians;
    ConvertRadiansToDegrees(vecRotation);
    // srsly, f knows, just pretend you never saw this line
    // vecRotation.fY = 360.0f - vecRotation.fY;    // Removed as caused problems with: Camera.rotation = Camera.rotation
}

void CClientCamera::SetRotationRadians(const CVector& vecRotation)
{
    if (!std::isfinite(vecRotation.fX) || !std::isfinite(vecRotation.fY) || !std::isfinite(vecRotation.fZ))
        return;

    if (!m_pCamera)
        return;

    if (!g_pMultiplayer)
        return;

    // Switch to fixed mode if required
    if (!IsInFixedMode())
        ToggleCameraFixedMode(true);

    CVector vecUseRotation = CVector(PI_2, PI_2, PI_2) - vecRotation;
    m_FixedCameraMode = EFixedCameraMode::ROTATION;

    // Set rotational part of fixed matrix
    CMatrix newMatrix;
    g_pMultiplayer->ConvertEulerAnglesToMatrix(newMatrix, vecUseRotation.fX, vecUseRotation.fY, vecUseRotation.fZ);
    m_matFixedMatrix.vUp = newMatrix.vUp;
    m_matFixedMatrix.vFront = newMatrix.vFront;
    m_matFixedMatrix.vRight = newMatrix.vRight;
    EnsureOrthonormal(m_matFixedMatrix);
    UpdateCenterOfWorldFromFixedMatrix();
    InvalidateCachedTransforms();
}

void CClientCamera::GetFixedTarget(CVector& vecTarget, float* pfRoll) const
{
    if (m_bFixed && m_FixedCameraMode == EFixedCameraMode::TARGET)
    {
        // Use supplied target vector and roll if was set
        if (pfRoll)
            *pfRoll = m_fRoll;
        vecTarget = m_vecFixedTarget;
    }
    else
    {
        if (pfRoll)
            *pfRoll = 0;
        CMatrix matTemp;
        GetMatrix(matTemp);
        vecTarget = matTemp.vPos + matTemp.vFront;
    }
}

void CClientCamera::SetFixedTarget(const CVector& vecPosition, float fRoll)
{
    if (!std::isfinite(vecPosition.fX) || !std::isfinite(vecPosition.fY) || !std::isfinite(vecPosition.fZ))
        return;

    // Switch to fixed mode if required
    if (!IsInFixedMode())
        ToggleCameraFixedMode(true);

    float normalizedRoll = 0.0f;
    if (std::isfinite(fRoll))
    {
        constexpr float fullCircle = 360.0f;
        normalizedRoll = std::remainder(fRoll, fullCircle);
        if (normalizedRoll < 0.0f)
            normalizedRoll += fullCircle;
    }

    // Store the target so it can be updated from our hook
    m_vecFixedTarget = vecPosition;
    m_fRoll = normalizedRoll;
    m_FixedCameraMode = EFixedCameraMode::TARGET;

    // Update fixed matrix
    // Calculate the front vector, target - position. If its length is 0 we'll get problems
    // (i.e. if position and target are the same), so make a new vector then looking horizontally
    CVector vecFront = m_vecFixedTarget - m_matFixedMatrix.vPos;
    if (vecFront.Length() < FLOAT_EPSILON)
        vecFront = CVector(0.0, 1.0f, 0.0f);
    else
        vecFront.Normalize();

    // Grab the right vector
    CVector vecRight = CVector(vecFront.fY, -vecFront.fX, 0);
    if (vecRight.Length() < FLOAT_EPSILON)
        vecRight = CVector(1.0f, 0.0f, 0.0f);
    else
        vecRight.Normalize();

    // Calculate the up vector from this
    CVector vecUp = vecRight;
    vecUp.CrossProduct(&vecFront);
    vecUp.Normalize();

    // Apply roll if needed around the front axis
    if (m_fRoll != 0.0f)
    {
        auto [cosRoll, sinRoll] = GetRollSinCos(m_fRoll);
        const CVector originalUp = vecUp;
        vecUp = originalUp * cosRoll - vecRight * sinRoll;
        vecRight = originalUp * sinRoll + vecRight * cosRoll;
    }

    // Set rotational part of fixed matrix
    m_matFixedMatrix.vFront = vecFront;
    m_matFixedMatrix.vUp = vecUp;
    m_matFixedMatrix.vRight = vecRight;
    EnsureOrthonormal(m_matFixedMatrix);
    UpdateCenterOfWorldFromFixedMatrix();
    InvalidateCachedTransforms();
}

//
// Make player 'orbit camera' rotate to face this point
//
void CClientCamera::SetOrbitTarget(const CVector& vecPosition)
{
    if (m_pCamera)
    {
        CClientEntity* pCameraTarget = GetTargetEntity();

        if (pCameraTarget != nullptr)
        {
            CVector vecTargetPosition;
            pCameraTarget->GetPosition(vecTargetPosition);
            if (pCameraTarget->GetType() == CCLIENTPLAYER)
                vecTargetPosition.fZ += 0.6f;

            CVector vecDirection = vecPosition - vecTargetPosition;
            if (vecDirection.Length() <= FLOAT_EPSILON)
                return;
            vecDirection.Normalize();

            float fAngleHorz = -atan2(vecDirection.fX, vecDirection.fY) - PI / 2;
            float fAngleVert = asin(vecDirection.fZ);

            CCam* pCam = m_pCamera->GetCam(m_pCamera->GetActiveCam());
            if (!pCam)
                return;

            pCam->SetDirection(fAngleHorz, fAngleVert);
        }
    }
}

void CClientCamera::AttachTo(CElement* pElement)
{
    if (pElement)
    {
        // Switch to fixed mode if required
        if (!IsInFixedMode())
            ToggleCameraFixedMode(true);
    }

    CClientEntity::AttachTo(pElement);
}

void CClientCamera::FadeIn(float fTime)
{
    if (!m_pCamera)
        return;
    m_pCamera->Fade(fTime, FADE_IN);
}

void CClientCamera::FadeOut(float fTime, unsigned char ucRed, unsigned char ucGreen, unsigned char ucBlue)
{
    if (!m_pCamera)
        return;
    m_pCamera->SetFadeColor(ucRed, ucGreen, ucBlue);
    m_pCamera->Fade(fTime, FADE_OUT);
}

void CClientCamera::SetFocus(CClientEntity* pEntity, eCamMode eMode, bool bSmoothTransition)
{
    if (!m_pCamera)
        return;

    // Should we attach to an entity, or set the camera to free?
    if (pEntity)
    {
        if (pEntity->IsBeingDeleted())
        {
            SetFocusToLocalPlayer();
            return;
        }

        eClientEntityType eType = pEntity->GetType();

        // Remove stream reference from the previous target
        if (m_pFocusedEntity && m_pFocusedEntity->IsStreamingCompatibleClass())
            static_cast<CClientStreamElement*>((CClientEntity*)m_pFocusedEntity)->RemoveStreamReference();

        // Add stream reference for our new target
        if (pEntity && pEntity->IsStreamingCompatibleClass())
            static_cast<CClientStreamElement*>(pEntity)->AddStreamReference();

        CEntity* pGameEntity = pEntity->GetGameEntity();
        if (pGameEntity)
        {
            // Set the switch style
            int iSwitchStyle = 2;
            if (bSmoothTransition)
            {
                iSwitchStyle = 1;
            }

            // Hacky, used to follow peds
            if (eMode == MODE_CAM_ON_A_STRING && (eType == CCLIENTPED || eType == CCLIENTPLAYER))
                eMode = MODE_FOLLOWPED;

            // Do it
            m_pCamera->TakeControl(pGameEntity, eMode, iSwitchStyle);

            // Store
            m_pFocusedEntity = pEntity;
            m_pFocusedGameEntity = pGameEntity;
            m_bFixed = false;
        }
    }
    else
    {
        // ## TODO ## : Don't attach to anything (free camera controlled by SetPosition and SetRotation)
        // Restore the camera to the local player at the moment
        SetFocusToLocalPlayer();
    }

    m_fRoll = 0.0f;
    m_fFOV = DEFAULT_FOV;
    InvalidateCachedTransforms();
}

void CClientCamera::SetFocus(CClientPlayer* pPlayer, eCamMode eMode, bool bSmoothTransition)
{
    if (!m_pCamera)
        return;

    // Should we attach to a player, or set the camera to free?
    if (pPlayer)
    {
        if (pPlayer->IsBeingDeleted())
        {
            SetFocus(reinterpret_cast<CClientEntity*>(NULL), MODE_NONE, bSmoothTransition);
            return;
        }

        // Attach the camera to the player vehicle if in a vehicle. To the player model otherwise
        CClientVehicle* pVehicle = pPlayer->GetOccupiedVehicle();
        if (pVehicle)
        {
            SetFocus(pVehicle, eMode, bSmoothTransition);
        }
        else
        {
            // Cast it so we don't call ourselves...
            SetFocus(static_cast<CClientEntity*>(pPlayer), eMode, bSmoothTransition);
        }
    }
    else
    {
        SetFocus(reinterpret_cast<CClientEntity*>(NULL), MODE_NONE, bSmoothTransition);
    }

    // Store the player we focused
    m_pFocusedPlayer = pPlayer;
    m_fRoll = 0.0f;
    m_fFOV = DEFAULT_FOV;
    m_bFixed = false;
    InvalidateCachedTransforms();
}

void CClientCamera::SetFocus(CVector* vecTarget, bool bSmoothTransition)
{
    if (!vecTarget)
        return;

    CVector sanitizedTarget = *vecTarget;

    if (!std::isfinite(sanitizedTarget.fX) || !std::isfinite(sanitizedTarget.fY) || !std::isfinite(sanitizedTarget.fZ))
        return;

    if (!m_pCamera)
        return;

    *vecTarget = sanitizedTarget;

    if (m_pFocusedEntity && m_pFocusedEntity->IsStreamingCompatibleClass())
        static_cast<CClientStreamElement*>((CClientEntity*)m_pFocusedEntity)->RemoveStreamReference();
    m_pFocusedEntity = nullptr;
    m_pFocusedGameEntity = nullptr;
    m_pFocusedPlayer = nullptr;
    m_bInvalidated = false;

    int iSwitchStyle = 2;
    if (bSmoothTransition)
        iSwitchStyle = 1;

    m_pCamera->TakeControl(vecTarget, iSwitchStyle);
    m_bFixed = true;
    InvalidateCachedTransforms();
}

void CClientCamera::SetFocusToLocalPlayer()
{
    // Restore the camera
    SetFocusToLocalPlayerImpl();

    Reset();
}

void CClientCamera::Reset()
{
    // Remove stream reference from the previous target
    if (m_pFocusedEntity && m_pFocusedEntity->IsStreamingCompatibleClass())
        static_cast<CClientStreamElement*>((CClientEntity*)m_pFocusedEntity)->RemoveStreamReference();

    m_pFocusedPlayer = NULL;
    m_pFocusedEntity = NULL;
    m_pFocusedGameEntity = NULL;
    m_bFixed = false;
    m_fRoll = 0.0f;
    m_fFOV = DEFAULT_FOV;
    SetCenterOfWorldCached(NULL, 0.0f);
    InvalidateCachedTransforms();
}

std::uint32_t CClientCamera::AcquireScriptCamera(CResource* owner, bool inhibitControls)
{
    if (!owner || m_pScriptCameraOwner || !m_pCamera || !g_pGame || g_pGame->GetSystemState() != SystemState::GS_PLAYING_GAME ||
        g_pGame->IsFileCutsceneActive())
        return 0;

    m_bScriptCameraPreviousFixed = m_bFixed;
    GetMatrix(m_matScriptCameraPrevious);
    m_fScriptCameraPreviousFOV = m_fFOV;
    m_bScriptCameraPreviousWidescreen = m_pCamera->GetWidescreen();
    m_bScriptCameraPreviousNativeNearClipEnabled = m_pCamera->GetScriptNearClip(m_fScriptCameraPreviousNativeNearClip);
    m_fScriptCameraPreviousMtaNearClip = g_pMultiplayer ? g_pMultiplayer->GetNearClipDistance() : 0.3f;

    // The MTA fixed-camera hook writes its matrix every frame. Returning to the
    // gameplay camera first ensures GTA's native vector processors own it for
    // the complete duration of the lease.
    SetFocusToLocalPlayer();
    m_pCamera->ResetScriptCameraComponents();
    m_pCamera->SetScriptCameraPersist(false, false);
    if (g_pMultiplayer)
        g_pMultiplayer->SetNearClipDistance(0.3f);

    m_bScriptCameraControlsInhibited = inhibitControls && g_pCore && g_pCore->GetKeyBinds();
    if (m_bScriptCameraControlsInhibited)
        g_pCore->GetKeyBinds()->AcquireGameplayControlInhibit();

    ++m_uiScriptCameraToken;
    if (m_uiScriptCameraToken == 0)
        ++m_uiScriptCameraToken;
    m_pScriptCameraOwner = owner;
    m_bFileCutsceneLease = false;
    m_bFileCutsceneStarted = false;
    m_bFileCutsceneVisibleAreaSet = false;
    return m_uiScriptCameraToken;
}

bool CClientCamera::BeginFileCutscene(CResource* owner, std::uint32_t token, const char* name, std::optional<unsigned int> visibleArea)
{
    CWorld* world = g_pGame ? g_pGame->GetWorld() : nullptr;
    if (!HasScriptCameraLease(owner, token) || m_bFileCutsceneLease || !g_pGame || (visibleArea && (!world || *visibleArea > 255)) ||
        !g_pGame->LoadFileCutscene(name))
        return false;

    if (visibleArea)
    {
        // SET_AREA_VISIBLE is independent of the player's interior in the
        // mission SCM. Keep that world-only state inside the native cutscene
        // lease so resource teardown and camera preemption restore it too.
        m_uiFileCutscenePreviousVisibleArea = world->GetCurrentArea();
        world->SetCurrentArea(*visibleArea);
        m_bFileCutsceneVisibleAreaSet = true;
    }

    // The same lease that snapshots camera and controls must also own the
    // CCutsceneMgr teardown. This prevents a server camera takeover or
    // resource stop from restoring gameplay while native playback continues.
    m_bFileCutsceneLease = true;
    m_bFileCutsceneStarted = false;
    return true;
}

bool CClientCamera::IsFileCutsceneLeaseActive(const CResource* owner, std::uint32_t token) const
{
    return m_bFileCutsceneLease && HasScriptCameraLease(owner, token);
}

bool CClientCamera::IsFileCutsceneLoaded(const CResource* owner, std::uint32_t token) const
{
    return IsFileCutsceneLeaseActive(owner, token) && g_pGame && g_pGame->IsFileCutsceneLoaded();
}

bool CClientCamera::StartFileCutscene(CResource* owner, std::uint32_t token)
{
    if (!IsFileCutsceneLoaded(owner, token) || m_bFileCutsceneStarted || !g_pGame->StartFileCutscene())
        return false;

    m_bFileCutsceneStarted = true;
    return true;
}

bool CClientCamera::HasFileCutsceneFinished(const CResource* owner, std::uint32_t token) const
{
    // Native HasCutsceneFinished returns true when no DAT is present, including
    // before start. Require this lease's accepted start before exposing it.
    return IsFileCutsceneLeaseActive(owner, token) && m_bFileCutsceneStarted && g_pGame && g_pGame->HasFileCutsceneFinished();
}

bool CClientCamera::IsFileCutsceneSkipInputPressed(const CResource* owner, std::uint32_t token) const
{
    return IsFileCutsceneLeaseActive(owner, token) && m_bFileCutsceneStarted && g_pGame && g_pGame->IsFileCutsceneSkipInputPressed();
}

bool CClientCamera::WasFileCutsceneSkipped(const CResource* owner, std::uint32_t token) const
{
    return IsFileCutsceneLeaseActive(owner, token) && m_bFileCutsceneStarted && g_pGame && g_pGame->WasFileCutsceneSkipped();
}

bool CClientCamera::SkipFileCutscene(CResource* owner, std::uint32_t token)
{
    return IsFileCutsceneLeaseActive(owner, token) && m_bFileCutsceneStarted && g_pGame && g_pGame->SkipFileCutscene();
}

bool CClientCamera::ReleaseScriptCamera(CResource* owner, std::uint32_t token, bool preserveFade)
{
    if (!HasScriptCameraLease(owner, token))
        return false;

    RestoreScriptCameraLease(preserveFade);
    return true;
}

void CClientCamera::ReleaseScriptCamera(CResource* owner)
{
    if (owner && owner == m_pScriptCameraOwner)
        RestoreScriptCameraLease();
}

void CClientCamera::AbortScriptCamera()
{
    if (m_pScriptCameraOwner)
        RestoreScriptCameraLease();
}

bool CClientCamera::HasScriptCameraLease(const CResource* owner, std::uint32_t token) const
{
    return owner && owner == m_pScriptCameraOwner && token != 0 && token == m_uiScriptCameraToken;
}

void CClientCamera::RestoreScriptCameraLease(bool preserveFade)
{
    if (!m_pScriptCameraOwner)
        return;

    if (m_bFileCutsceneLease && g_pGame)
        g_pGame->DeleteFileCutscene();
    if (m_bFileCutsceneVisibleAreaSet && g_pGame && g_pGame->GetWorld())
        g_pGame->GetWorld()->SetCurrentArea(m_uiFileCutscenePreviousVisibleArea);
    m_bFileCutsceneLease = false;
    m_bFileCutsceneStarted = false;
    m_bFileCutsceneVisibleAreaSet = false;

    // Clear ownership before restoring legacy MTA camera state so any internal
    // setter reached during restoration cannot observe a stale active lease.
    m_pScriptCameraOwner = nullptr;

    if (m_pCamera)
    {
        m_pCamera->ResetScriptCameraComponents();
        m_pCamera->SetScriptCameraPersist(false, false);
        if (m_bScriptCameraPreviousNativeNearClipEnabled)
            m_pCamera->SetScriptNearClip(m_fScriptCameraPreviousNativeNearClip);
        else
            m_pCamera->ClearScriptNearClip();
        m_pCamera->SetScriptWidescreen(m_bScriptCameraPreviousWidescreen);
        m_pCamera->RestoreWithJumpCut();
    }

    if (g_pMultiplayer)
        g_pMultiplayer->SetNearClipDistance(m_fScriptCameraPreviousMtaNearClip);

    // Successful scene transitions may intentionally hand control back while
    // the screen is black, then fade into gameplay. Safety-driven cleanup uses
    // the default path so an abort can never strand the client behind a fade.
    if (!preserveFade)
        FadeIn(0.0f);
    if (m_bScriptCameraPreviousFixed)
    {
        SetMatrix(m_matScriptCameraPrevious);
        SetFOV(m_fScriptCameraPreviousFOV);
    }
    else
    {
        SetFocusToLocalPlayer();
    }

    if (m_bScriptCameraControlsInhibited && g_pCore && g_pCore->GetKeyBinds())
        g_pCore->GetKeyBinds()->ReleaseGameplayControlInhibit();
    m_bScriptCameraControlsInhibited = false;
}

void CClientCamera::SetFocusToLocalPlayerImpl()
{
    // Restore the camera
    if (!m_pCamera)
        return;
    m_pCamera->RestoreWithJumpCut();
    SetCenterOfWorldCached(NULL, 0.0f);
    InvalidateCachedTransforms();
}

void CClientCamera::UnreferenceEntity(CClientEntity* pEntity)
{
    if (m_pFocusedEntity == pEntity)
    {
        SetFocusToLocalPlayerImpl();

        // Remove stream reference from the previous target
        if (m_pFocusedEntity && m_pFocusedEntity->IsStreamingCompatibleClass())
            static_cast<CClientStreamElement*>((CClientEntity*)m_pFocusedEntity)->RemoveStreamReference();

        m_pFocusedEntity = NULL;
        m_pFocusedGameEntity = NULL;
        m_bInvalidated = false;
    }
}

void CClientCamera::UnreferencePlayer(CClientPlayer* pPlayer)
{
    if (m_pFocusedPlayer == pPlayer)
    {
        SetFocusToLocalPlayerImpl();
        m_pFocusedPlayer = NULL;
    }
}

void CClientCamera::InvalidateEntity(CClientEntity* pEntity)
{
    if (!m_bInvalidated)
    {
        if (m_pFocusedEntity && m_pFocusedEntity == pEntity)
        {
            SetFocusToLocalPlayerImpl();
            m_bInvalidated = true;
        }
    }
}

void CClientCamera::RestoreEntity(CClientEntity* pEntity)
{
    if (m_bInvalidated)
    {
        if (m_pFocusedEntity && m_pFocusedEntity == pEntity)
        {
            SetFocus(pEntity, MODE_CAM_ON_A_STRING);
            m_bInvalidated = false;
        }
    }
}

void CClientCamera::SetCameraVehicleViewMode(eVehicleCamMode eMode)
{
    if (!m_pCamera)
        return;
    m_pCamera->SetCameraVehicleViewMode(static_cast<BYTE>(eMode));
}

void CClientCamera::SetCameraPedViewMode(ePedCamMode eMode)
{
    if (!m_pCamera)
        return;
    m_pCamera->SetCameraPedViewMode(static_cast<BYTE>(eMode));
}

eVehicleCamMode CClientCamera::GetCameraVehicleViewMode()
{
    if (!m_pCamera)
        return eVehicleCamMode::BUMPER;
    return (eVehicleCamMode)m_pCamera->GetCameraVehicleViewMode();
}

ePedCamMode CClientCamera::GetCameraPedViewMode()
{
    if (!m_pCamera)
        return ePedCamMode::CLOSE;
    return (ePedCamMode)m_pCamera->GetCameraPedViewMode();
}

void CClientCamera::SetCameraClip(bool bObjects, bool bVehicles)
{
    if (!m_pCamera)
        return;
    m_pCamera->SetCameraClip(bObjects, bVehicles);
}

void CClientCamera::ResetCameraClip()
{
    if (!m_pCamera)
        return;
    m_pCamera->ResetCameraClip();
}

void CClientCamera::GetCameraClip(bool& bObjects, bool& bVehicles)
{
    if (!m_pCamera)
    {
        bObjects = true;
        bVehicles = true;
        return;
    }
    m_pCamera->GetCameraClip(bObjects, bVehicles);
}

void CClientCamera::ToggleCameraFixedMode(bool bEnabled)
{
    if (bEnabled)
    {
        CClientPlayer*  pLocalPlayer = m_pManager->GetPlayerManager()->GetLocalPlayer();
        CClientVehicle* pLocalVehicle = NULL;

        // Get the local vehicle, if any
        if (pLocalPlayer)
            pLocalVehicle = pLocalPlayer->GetOccupiedVehicle();

        // Use the local vehicle, otherwise use the local player
        if (pLocalVehicle)
            SetFocus(pLocalVehicle, MODE_FIXED, false);
        else
            SetFocus(pLocalPlayer, MODE_FIXED, false);

        // Set the target position
        SetFocus(&m_matFixedMatrix.vPos, false);
    }
    else
    {
        SetCenterOfWorldCached(NULL, 0.0f);
        SetFocusToLocalPlayer();

        m_fRoll = 0.0f;
        m_fFOV = DEFAULT_FOV;
    }

    InvalidateCachedTransforms();
}

void CClientCamera::SetTargetEntity(CClientEntity* pEntity)
{
    if (m_bFixed)
        return;

    if (!m_pCamera)
        return;

    CCam* const pCam = m_pCamera->GetCam(m_pCamera->GetActiveCam());

    if (!pCam)
        return;

    if (pEntity && pEntity->IsBeingDeleted())
        pEntity = nullptr;

    CEntity* pGameEntity = pEntity ? pEntity->GetGameEntity() : nullptr;
    if (pEntity && !pGameEntity)
    {
        pEntity = nullptr;
        pGameEntity = nullptr;
    }

    pCam->SetTargetEntity(pGameEntity);
}

CClientEntity* CClientCamera::GetTargetEntity()
{
    CClientEntity* pReturn = NULL;
    if (m_pCamera)
    {
        CCam* pCam = m_pCamera->GetCam(m_pCamera->GetActiveCam());
        if (!pCam)
            return nullptr;

        CEntity* pEntity = pCam->GetTargetEntity();
        if (pEntity && g_pGame)
        {
            CPools* pPools = g_pGame->GetPools();
            if (pPools)
            {
                CEntitySAInterface* pInterface = pEntity->GetInterface();
                if (pInterface)
                    pReturn = pPools->GetClientEntity(reinterpret_cast<DWORD*>(pInterface));
            }
        }
    }
    return pReturn;
}

bool CClientCamera::StaticProcessFixedCamera(CCam* pCam)
{
    if (!pCam)
        return false;

    if (!g_pClientGame)
        return false;

    CClientManager* manager = g_pClientGame->GetManager();
    if (!manager)
        return false;

    CClientCamera* camera = manager->GetCamera();
    return camera ? camera->ProcessFixedCamera(pCam) : false;
}

bool CClientCamera::ProcessFixedCamera(CCam* pCam)
{
    assert(pCam);
    // The purpose of this handler function is changing the Source, Front and Up vectors in CCam
    // when called by GTA. This is called when we are in fixed camera mode.
    // Make sure we actually want to apply our custom camera position/lookat
    // (this handler could also be called from cinematic mode)
    if (!m_bFixed)
        return false;

    // Update our position/rotation if we're attached
    DoAttaching();

    SetGtaMatrix(m_matFixedMatrix, pCam);

    // Set the zoom
    float appliedFOV = std::isfinite(m_fFOV) ? m_fFOV : DEFAULT_FOV;
    pCam->SetFOV(appliedFOV);

    return true;
}

//
// Return matrix being used by GTA right now
//
CMatrix CClientCamera::GetGtaMatrix() const
{
    if (!m_pCamera)
        return CMatrix();

    if (IsInCameraTransition())
        return GetInterpolatedCameraMatrix();

    CCam* pCam = m_pCamera->GetCam(m_pCamera->GetActiveCam());
    if (!pCam)
        return CMatrix();

    CMatrix matResult;
    m_pCamera->GetMatrix(&matResult);
    matResult.vFront = *pCam->GetFront();
    matResult.vUp = *pCam->GetUp();
    matResult.vPos = *pCam->GetSource();
    matResult.vRight = -matResult.vRight;  // Camera has this the other way
    matResult.OrthoNormalize(CMatrix::AXIS_FRONT, CMatrix::AXIS_UP);
    return matResult;
}

//
// Set matrix to be used by GTA right now
//
void CClientCamera::SetGtaMatrix(const CMatrix& matInNew, CCam* pCam) const
{
    if (!m_pCamera)
        return;

    auto isFiniteVector = [](const CVector& vec) { return std::isfinite(vec.fX) && std::isfinite(vec.fY) && std::isfinite(vec.fZ); };

    if (!isFiniteVector(matInNew.vFront) || !isFiniteVector(matInNew.vUp) || !isFiniteVector(matInNew.vRight) || !isFiniteVector(matInNew.vPos))
        return;

    CCam* targetCam = pCam ? pCam : m_pCamera->GetCam(m_pCamera->GetActiveCam());
    if (!targetCam)
        return;

    CMatrix matNew = matInNew;
    EnsureOrthonormal(matNew);
    matNew.vRight = -matNew.vRight;  // Camera has this the other way
    m_pCamera->SetMatrix(&matNew);
    *targetCam->GetUp() = matNew.vUp;
    *targetCam->GetFront() = matNew.vFront;
    *targetCam->GetSource() = matNew.vPos;
}

void CClientCamera::InvalidateCachedTransforms() const
{
    m_hasCachedPulseMatrix = false;
    m_hasCachedPulseEuler = false;
}

void CClientCamera::SetCenterOfWorldCached(const CVector* pPosition, float fRotationRadians)
{
    if (!g_pMultiplayer)
        return;

    if (!pPosition)
    {
        if (m_hasCenterOfWorld)
        {
            g_pMultiplayer->SetCenterOfWorld(NULL, NULL, NULL);
            m_hasCenterOfWorld = false;
            m_lastCenterOfWorldPos = CVector();
            m_lastCenterOfWorldRot = 0.0f;
        }
        return;
    }

    if (!std::isfinite(pPosition->fX) || !std::isfinite(pPosition->fY) || !std::isfinite(pPosition->fZ))
        return;

    const float rotation = std::isfinite(fRotationRadians) ? fRotationRadians : 0.0f;

    if (!m_hasCenterOfWorld || !VectorsClose(m_lastCenterOfWorldPos, *pPosition) || !AlmostEqual(m_lastCenterOfWorldRot, rotation, kAngleTolerance))
    {
        m_lastCenterOfWorldPos = *pPosition;
        m_lastCenterOfWorldRot = rotation;
        g_pMultiplayer->SetCenterOfWorld(NULL, &m_lastCenterOfWorldPos, m_lastCenterOfWorldRot);
        m_hasCenterOfWorld = true;
    }
}

void CClientCamera::UpdateCenterOfWorldFromMatrix(const CMatrix& matrix)
{
    if (!g_pMultiplayer)
        return;

    if (!std::isfinite(matrix.vPos.fX) || !std::isfinite(matrix.vPos.fY) || !std::isfinite(matrix.vPos.fZ))
        return;

    CMatrix normalized = matrix;
    EnsureOrthonormal(normalized);

    CVector eulerAngles;
    g_pMultiplayer->ConvertMatrixToEulerAngles(normalized, eulerAngles.fX, eulerAngles.fY, eulerAngles.fZ);

    SetCenterOfWorldCached(&matrix.vPos, kPi - eulerAngles.fZ);
}

void CClientCamera::UpdateCenterOfWorldFromFixedMatrix()
{
    UpdateCenterOfWorldFromMatrix(m_matFixedMatrix);
}

const CMatrix& CClientCamera::AcquirePulseMatrix() const
{
    if (!m_hasCachedPulseMatrix)
    {
        m_cachedPulseMatrix = GetGtaMatrix();
        m_hasCachedPulseMatrix = true;
    }
    return m_cachedPulseMatrix;
}

const CVector& CClientCamera::AcquirePulseEuler(const CMatrix& matrix) const
{
    if (!m_hasCachedPulseEuler)
    {
        if (!g_pMultiplayer)
        {
            m_cachedPulseEuler = CVector();
            m_hasCachedPulseEuler = true;
            return m_cachedPulseEuler;
        }
        CMatrix temp = matrix;
        EnsureOrthonormal(temp);
        g_pMultiplayer->ConvertMatrixToEulerAngles(temp, m_cachedPulseEuler.fX, m_cachedPulseEuler.fY, m_cachedPulseEuler.fZ);
        m_hasCachedPulseEuler = true;
    }
    return m_cachedPulseEuler;
}

void CClientCamera::ShakeCamera(float radius, float x, float y, float z) noexcept
{
    if (!m_pCamera)
        return;
    m_pCamera->ShakeCamera(radius, x, y, z);
}

void CClientCamera::ResetShakeCamera() noexcept
{
    if (!m_pCamera)
        return;
    m_pCamera->ResetShakeCamera();
}

bool CClientCamera::IsInCameraTransition() const
{
    return m_pCamera ? m_pCamera->IsInTransition() : false;
}

CMatrix CClientCamera::GetInterpolatedCameraMatrix() const
{
    CMatrix matrix;
    if (m_pCamera && m_pCamera->GetTransitionMatrix(matrix))
        return matrix;

    return CMatrix();
}

float CClientCamera::GetAccurateFOV() const
{
    if (!m_pCamera)
        return DEFAULT_FOV;

    if (IsInCameraTransition())
        return m_pCamera->GetTransitionFOV();

    return *(float*)VAR_CurrentCameraFOV;
}
