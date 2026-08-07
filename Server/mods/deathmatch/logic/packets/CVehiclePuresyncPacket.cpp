/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/packets/CVehiclePuresyncPacket.cpp
 *  PURPOSE:     Vehicle pure synchronization packet class
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CVehiclePuresyncPacket.h"
#include "CVehicleResyncPacket.h"
#include "CVehicleManager.h"
#include "CGame.h"
#include "CTrainTrackManager.h"
#include "CTickRateSettings.h"
#include "CWeaponNames.h"
#include "Utils.h"
#include "lua/CLuaFunctionParseHelpers.h"
#include "net/SyncStructures.h"

extern CGame* g_pGame;

namespace
{
    bool IsFiniteVector(const CVector& vector)
    {
        return std::isfinite(vector.fX) && std::isfinite(vector.fY) && std::isfinite(vector.fZ);
    }

    float RotationDifference(float first, float second)
    {
        const float difference = std::fmod(std::abs(first - second), 360.0f);
        return std::min(difference, 360.0f - difference);
    }
}  // namespace

CVehiclePuresyncPacket::CVehiclePuresyncPacket(CPlayer* pPlayer)
{
    m_pSourceElement = pPlayer;
}

//
// NOTE: Any changes to this function will require similar changes to CSimVehiclePuresyncPacket::Read()
//
bool CVehiclePuresyncPacket::Read(NetBitStreamInterface& BitStream)
{
    // Got a player to read?
    if (m_pSourceElement)
    {
        CPlayer* pSourcePlayer = static_cast<CPlayer*>(m_pSourceElement);

        // Player is in a vehicle?
        CVehicle* pVehicle = pSourcePlayer->GetOccupiedVehicle();
        if (pVehicle)
        {
            const bool      bVehicleFrozen = pVehicle->IsFrozen();
            const ElementID vehicleID = pVehicle->GetID();
            bool            bRejectVehicleState = bVehicleFrozen;
            bool            bNeedsVehicleResync = false;
            bool            bSignificantFrozenAttempt = false;
            const CVector   vecAuthoritativePosition = pVehicle->GetPosition();

            // Read out the time context
            unsigned char ucTimeContext = 0;
            if (!BitStream.Read(ucTimeContext))
                return false;

            // Only read this packet if it matches the current time context that
            // player is in.
            if (!pSourcePlayer->CanUpdateSync(ucTimeContext))
            {
                return false;
            }

            // Read out the keysync data
            CControllerState ControllerState;
            if (!ReadFullKeysync(ControllerState, BitStream))
                return false;

            // Read out the remote model
            int iRemoteModelID = 0;
            BitStream.Read(iRemoteModelID);

            eVehicleType vehicleType = pVehicle->GetVehicleType();
            eVehicleType remoteVehicleType = CVehicleManager::GetVehicleType(static_cast<unsigned short>(iRemoteModelID));

            // Read out its position
            SPositionSync position(false);
            if (!BitStream.Read(&position))
                return false;

            bool         bHasTrainSync = false;
            float        fTrainPosition = 0.0f;
            bool         bTrainDirection = false;
            CTrainTrack* pTrainTrack = nullptr;
            float        fTrainSpeed = 0.0f;

            // If the remote vehicle is a train, we want to read special train-specific data
            if (remoteVehicleType == VEHICLE_TRAIN)
            {
                if (!BitStream.Read(fTrainPosition) || !BitStream.ReadBit(bTrainDirection) || !BitStream.Read(fTrainSpeed))
                    return false;

                // TODO(qaisjp, feature/custom-train-tracks): this needs to be changed to an ElementID when the time is right (in a backwards compatible manner)
                // Note: here we use a uchar, in the CTT branch this is a uint. Just don't forget that, it might be important
                uchar trackIndex;
                if (!BitStream.Read(trackIndex))
                    return false;
                pTrainTrack = g_pGame->GetTrainTrackManager()->GetDefaultTrackByIndex(trackIndex);
                bHasTrainSync = true;
            }

            // Read the camera orientation
            CVector vecCamPosition, vecCamFwd;
            ReadCameraOrientation(position.data.vecPosition, BitStream, vecCamPosition, vecCamFwd);
            pSourcePlayer->SetCameraOrientation(vecCamPosition, vecCamFwd);

            // Jax: don't allow any outdated packets through
            SOccupiedSeatSync seat;
            if (!BitStream.Read(&seat))
                return false;
            if (seat.data.ucSeat != pSourcePlayer->GetOccupiedVehicleSeat())
            {
                // Mis-matching seats can happen when we warp into a different one,
                // which will screw up the whole packet
                return false;
            }

            // Read out the vehicle matrix only if he's the driver
            unsigned int uiSeat = pSourcePlayer->GetOccupiedVehicleSeat();
            if (uiSeat == 0)
            {
                // Read out the vehicle rotation in degrees
                SRotationDegreesSync rotation;
                if (!BitStream.Read(&rotation))
                    return false;

                // Move speed vector
                SVelocitySync velocity;
                if (!BitStream.Read(&velocity))
                    return false;

                // Turn speed vector
                SVelocitySync turnSpeed;
                if (!BitStream.Read(&turnSpeed))
                    return false;
                if (!IsFiniteVector(velocity.data.vecVelocity) || !IsFiniteVector(turnSpeed.data.vecVelocity))
                    return false;

                // Health
                SVehicleHealthSync health;
                if (!BitStream.Read(&health))
                    return false;

                const float fPreviousHealth = pVehicle->GetLastSyncedHealth();
                const float fCurrentHealth = pVehicle->GetHealth();
                const float fHealth = health.data.fValue;

                if (bVehicleFrozen)
                {
                    CVector vecAuthoritativeRotation;
                    pVehicle->GetRotationDegrees(vecAuthoritativeRotation);
                    const float rotationDifference = std::max({RotationDifference(rotation.data.vecRotation.fX, vecAuthoritativeRotation.fX),
                                                               RotationDifference(rotation.data.vecRotation.fY, vecAuthoritativeRotation.fY),
                                                               RotationDifference(rotation.data.vecRotation.fZ, vecAuthoritativeRotation.fZ)});
                    // Frozen clients still emit small physics jitter. Only report motion
                    // large enough to represent a useful local unfreeze attempt.
                    bSignificantFrozenAttempt = DistanceBetweenPoints3D(vecAuthoritativePosition, position.data.vecPosition) > 0.5f ||
                                                rotationDifference > 3.0f || (velocity.data.vecVelocity - pVehicle->GetVelocity()).Length() > 0.03f ||
                                                (turnSpeed.data.vecVelocity - pVehicle->GetTurnSpeed()).Length() > 0.05f || fHealth > fCurrentHealth + 0.5f;
                    bNeedsVehicleResync = bSignificantFrozenAttempt;
                }
                else if (DistanceBetweenPoints3D(vecAuthoritativePosition, position.data.vecPosition) >= g_TickRateSettings.playerTeleportAlert)
                {
                    CLuaArguments arguments;
                    arguments.PushElement(pVehicle);
                    arguments.PushNumber(vecAuthoritativePosition.fX);
                    arguments.PushNumber(vecAuthoritativePosition.fY);
                    arguments.PushNumber(vecAuthoritativePosition.fZ);
                    arguments.PushNumber(position.data.vecPosition.fX);
                    arguments.PushNumber(position.data.vecPosition.fY);
                    arguments.PushNumber(position.data.vecPosition.fZ);
                    const bool bAccepted = pSourcePlayer->CallEvent("onPlayerVehicleTeleport", arguments, nullptr);
                    pVehicle = GetElementFromId<CVehicle>(vehicleID);
                    if (!pVehicle || pSourcePlayer->GetOccupiedVehicle() != pVehicle)
                        return false;

                    if (!bAccepted || pVehicle->IsFrozen())
                    {
                        bRejectVehicleState = true;
                        bNeedsVehicleResync = true;
                    }
                }

                bool bApplyVehicleHealth = !bRejectVehicleState;
                if (bApplyVehicleHealth && fHealth > fCurrentHealth + 0.5f)
                {
                    CLuaArguments arguments;
                    arguments.PushElement(pVehicle);
                    arguments.PushNumber(fCurrentHealth);
                    arguments.PushNumber(fHealth);
                    const bool bAccepted = pSourcePlayer->CallEvent("onPlayerVehicleHealthSyncIncrease", arguments, nullptr);
                    pVehicle = GetElementFromId<CVehicle>(vehicleID);
                    if (!pVehicle || pSourcePlayer->GetOccupiedVehicle() != pVehicle)
                        return false;

                    if (!bAccepted || pVehicle->IsFrozen())
                    {
                        bApplyVehicleHealth = false;
                        bNeedsVehicleResync = true;
                    }
                    if (pVehicle->IsFrozen())
                        bRejectVehicleState = true;
                }

                if (!bRejectVehicleState)
                {
                    pVehicle->SetPosition(position.data.vecPosition);
                    pVehicle->SetRotationDegrees(rotation.data.vecRotation);
                    pVehicle->SetVelocity(velocity.data.vecVelocity);
                    pSourcePlayer->SetPosition(position.data.vecPosition);
                    pSourcePlayer->SetVelocity(velocity.data.vecVelocity);
                    pVehicle->SetTurnSpeed(turnSpeed.data.vecVelocity);

                    // Train data is parsed before the occupied seat. Defer its
                    // application until the same authorization decision as the
                    // rest of the driver-controlled vehicle state.
                    if (bHasTrainSync && vehicleType == VEHICLE_TRAIN)
                    {
                        pVehicle->SetTrainPosition(fTrainPosition);
                        pVehicle->SetTrainDirection(bTrainDirection);
                        pVehicle->SetTrainTrack(pTrainTrack);
                        pVehicle->SetTrainSpeed(fTrainSpeed);
                    }
                }
                else
                {
                    pSourcePlayer->SetPosition(vecAuthoritativePosition);
                    pSourcePlayer->SetVelocity(pVehicle->GetVelocity());
                }

                // Less than last time?
                if (bApplyVehicleHealth && fHealth < fPreviousHealth)
                {
                    // Grab the delta health
                    float fDeltaHealth = fPreviousHealth - fHealth;

                    if (fDeltaHealth > 0.0f)
                    {
                        // Call the onVehicleDamage event
                        CLuaArguments Arguments;
                        Arguments.PushNumber(fDeltaHealth);
                        pVehicle->CallEvent("onVehicleDamage", Arguments);
                    }
                }
                if (bApplyVehicleHealth)
                {
                    pVehicle->SetHealth(fHealth);
                    // Stops sync + fixVehicle/setElementHealth conflicts triggering onVehicleDamage by having a separate stored float keeping track of ONLY
                    // what comes in via sync
                    // - Caz
                    pVehicle->SetLastSyncedHealth(fHealth);
                }

                // Trailer chain
                CVehicle* pTowedByVehicle = pVehicle;
                ElementID TrailerID;
                bool      bHasTrailer;
                if (!BitStream.ReadBit(bHasTrailer))
                    return false;

                while (bHasTrailer)
                {
                    BitStream.Read(TrailerID);
                    CVehicle* pTrailer = GetElementFromId<CVehicle>(TrailerID);

                    // Read out the trailer position and rotation
                    SPositionSync trailerPosition(false);
                    if (!BitStream.Read(&trailerPosition))
                        return false;

                    SRotationDegreesSync trailerRotation;
                    if (!BitStream.Read(&trailerRotation))
                        return false;

                    // Rejected driver state must still be consumed completely
                    // so the following player fields remain aligned, but it may
                    // not move or relink any trailer named by the client.
                    if (bRejectVehicleState)
                    {
                        if (!BitStream.ReadBit(bHasTrailer))
                            return false;
                        continue;
                    }

                    // If we found the trailer
                    if (pTrailer)
                    {
                        // Set its position and rotation
                        pTrailer->SetPosition(trailerPosition.data.vecPosition);
                        pTrailer->SetRotationDegrees(trailerRotation.data.vecRotation);

                        // Is this a new trailer, attached?
                        CVehicle* pCurrentTrailer = pTowedByVehicle->GetTowedVehicle();
                        if (pCurrentTrailer != pTrailer)
                        {
                            // If theres a trailer already attached
                            if (pCurrentTrailer)
                            {
                                pTowedByVehicle->SetTowedVehicle(NULL);
                                pCurrentTrailer->SetTowedByVehicle(NULL);

                                // Tell everyone to detach them
                                CVehicleTrailerPacket DetachPacket(pTowedByVehicle, pCurrentTrailer, false);
                                g_pGame->GetPlayerManager()->BroadcastOnlyJoined(DetachPacket);

                                // Execute the attach trailer script function
                                CLuaArguments Arguments;
                                Arguments.PushElement(pTowedByVehicle);
                                pCurrentTrailer->CallEvent("onTrailerDetach", Arguments);
                            }

                            // If something else is towing this trailer
                            CVehicle* pCurrentVehicle = pTrailer->GetTowedByVehicle();
                            if (pCurrentVehicle)
                            {
                                pCurrentVehicle->SetTowedVehicle(NULL);
                                pTrailer->SetTowedByVehicle(NULL);

                                // Tell everyone to detach them
                                CVehicleTrailerPacket DetachPacket(pCurrentVehicle, pTrailer, false);
                                g_pGame->GetPlayerManager()->BroadcastOnlyJoined(DetachPacket);

                                // Execute the attach trailer script function
                                CLuaArguments Arguments;
                                Arguments.PushElement(pCurrentVehicle);
                                pTrailer->CallEvent("onTrailerDetach", Arguments);
                            }

                            pTowedByVehicle->SetTowedVehicle(pTrailer);
                            pTrailer->SetTowedByVehicle(pTowedByVehicle);

                            // Execute the attach trailer script function
                            CLuaArguments Arguments;
                            Arguments.PushElement(pTowedByVehicle);
                            bool bContinue = pTrailer->CallEvent("onTrailerAttach", Arguments);

                            // Attach or detach trailers depending on the event outcome
                            CVehicleTrailerPacket TrailerPacket(pTowedByVehicle, pTrailer, bContinue);
                            g_pGame->GetPlayerManager()->BroadcastOnlyJoined(TrailerPacket);
                        }
                    }
                    else
                        break;

                    pTowedByVehicle = pTrailer;

                    if (BitStream.ReadBit(bHasTrailer) == false)
                        return false;
                }

                // If there was a trailer before
                CVehicle* pCurrentTrailer = !bRejectVehicleState ? pTowedByVehicle->GetTowedVehicle() : nullptr;
                if (pCurrentTrailer)
                {
                    pTowedByVehicle->SetTowedVehicle(NULL);
                    pCurrentTrailer->SetTowedByVehicle(NULL);

                    // Tell everyone else to detach them
                    CVehicleTrailerPacket DetachPacket(pTowedByVehicle, pCurrentTrailer, false);
                    g_pGame->GetPlayerManager()->BroadcastOnlyJoined(DetachPacket);

                    // Execute the detach trailer script function
                    CLuaArguments Arguments;
                    Arguments.PushElement(pTowedByVehicle);
                    pCurrentTrailer->CallEvent("onTrailerDetach", Arguments);
                }
            }
            else
            {
                pSourcePlayer->SetPosition(bVehicleFrozen ? vecAuthoritativePosition : position.data.vecPosition);
            }

            if (BitStream.ReadBit())
            {
                ElementID DamagerID;
                if (!BitStream.Read(DamagerID))
                    return false;

                SWeaponTypeSync weaponType;
                if (!BitStream.Read(&weaponType))
                    return false;

                SBodypartSync bodyPart;
                if (!BitStream.Read(&bodyPart))
                    return false;

                pSourcePlayer->SetDamageInfo(DamagerID, weaponType.data.ucWeaponType, static_cast<unsigned char>(bodyPart.data.uiBodypart));
            }

            // Player health
            SPlayerHealthSync health;
            if (!BitStream.Read(&health))
                return false;
            float fHealth = health.data.fValue;

            float fOldHealth = pSourcePlayer->GetHealth();
            float fHealthLoss = fOldHealth - fHealth;
            bool  bApplyPlayerHealth = true;

            if (fHealth > fOldHealth + 1.0f)
            {
                CLuaArguments arguments;
                arguments.PushNumber(fOldHealth);
                arguments.PushNumber(fHealth);
                const bool bAccepted = pSourcePlayer->CallEvent("onPlayerHealthSyncIncrease", arguments, nullptr);
                pVehicle = GetElementFromId<CVehicle>(vehicleID);
                if (!pVehicle || pSourcePlayer->GetOccupiedVehicle() != pVehicle)
                    return false;

                if (!bAccepted)
                {
                    bApplyPlayerHealth = false;
                    bNeedsVehicleResync = true;
                }
                if (pVehicle->IsFrozen())
                {
                    bRejectVehicleState = true;
                    bNeedsVehicleResync = true;
                }
            }

            // Less than last packet's frame?
            if (bApplyPlayerHealth && fHealth < fOldHealth && fHealthLoss > 0)
            {
                // Call the onPlayerDamage event
                CLuaArguments Arguments;

                CElement* pDamageSource = CElementIDs::GetElement(pSourcePlayer->GetPlayerAttacker());
                if (pDamageSource)
                    Arguments.PushElement(pDamageSource);
                else
                    Arguments.PushNil();
                Arguments.PushNumber(pSourcePlayer->GetAttackWeapon());
                Arguments.PushNumber(pSourcePlayer->GetAttackBodyPart());
                Arguments.PushNumber(fHealthLoss);
                pSourcePlayer->CallEvent("onPlayerDamage", Arguments);
            }
            if (bApplyPlayerHealth)
                pSourcePlayer->SetHealth(fHealth);

            // Armor
            SPlayerArmorSync armor;
            if (!BitStream.Read(&armor))
                return false;
            float fArmor = armor.data.fValue;

            float fOldArmor = pSourcePlayer->GetArmor();
            float fArmorLoss = fOldArmor - fArmor;
            bool  bApplyPlayerArmor = true;

            if (fArmor > fOldArmor + 1.0f)
            {
                CLuaArguments arguments;
                arguments.PushNumber(fOldArmor);
                arguments.PushNumber(fArmor);
                const bool bAccepted = pSourcePlayer->CallEvent("onPlayerArmorSyncIncrease", arguments, nullptr);
                pVehicle = GetElementFromId<CVehicle>(vehicleID);
                if (!pVehicle || pSourcePlayer->GetOccupiedVehicle() != pVehicle)
                    return false;

                if (!bAccepted)
                {
                    bApplyPlayerArmor = false;
                    bNeedsVehicleResync = true;
                }
                if (pVehicle->IsFrozen())
                {
                    bRejectVehicleState = true;
                    bNeedsVehicleResync = true;
                }
            }

            // Less than last packet's frame?
            if (bApplyPlayerArmor && fArmor < fOldArmor && fArmorLoss > 0)
            {
                // Call the onPlayerDamage event
                CLuaArguments Arguments;

                CElement* pDamageSource = CElementIDs::GetElement(pSourcePlayer->GetPlayerAttacker());
                if (pDamageSource)
                    Arguments.PushElement(pDamageSource);
                else
                    Arguments.PushNil();
                Arguments.PushNumber(pSourcePlayer->GetAttackWeapon());
                Arguments.PushNumber(pSourcePlayer->GetAttackBodyPart());
                Arguments.PushNumber(fArmorLoss);
                pSourcePlayer->CallEvent("onPlayerDamage", Arguments);
            }
            if (bApplyPlayerArmor)
                pSourcePlayer->SetArmor(fArmor);

            // Flags
            SVehiclePuresyncFlags flags;
            if (!BitStream.Read(&flags))
                return false;

            pSourcePlayer->SetWearingGoggles(flags.data.bIsWearingGoggles);
            pSourcePlayer->SetDoingGangDriveby(flags.data.bIsDoingGangDriveby);

            // Weapon sync
            if (flags.data.bHasAWeapon)
            {
                SWeaponSlotSync slot;
                if (!BitStream.Read(&slot))
                    return false;

                pSourcePlayer->SetWeaponSlot(slot.data.uiSlot);

                if (flags.data.bIsDoingGangDriveby && CWeaponNames::DoesSlotHaveAmmo(slot.data.uiSlot))
                {
                    float fWeaponRange = pSourcePlayer->GetWeaponRangeFromSlot(slot.data.uiSlot);

                    // Read the ammo states
                    SWeaponAmmoSync ammo(pSourcePlayer->GetWeaponType(), true, true);
                    if (!BitStream.Read(&ammo))
                        return false;
                    pSourcePlayer->SetWeaponAmmoInClip(ammo.data.usAmmoInClip);
                    pSourcePlayer->SetWeaponTotalAmmo(ammo.data.usTotalAmmo);

                    // Read aim data
                    SWeaponAimSync aim(fWeaponRange, true);
                    if (!BitStream.Read(&aim))
                        return false;
                    pSourcePlayer->SetAimDirection(aim.data.fArm);
                    pSourcePlayer->SetSniperSourceVector(aim.data.vecOrigin);
                    pSourcePlayer->SetTargettingVector(aim.data.vecTarget);

                    // Read the driveby direction
                    SDrivebyDirectionSync driveby;
                    if (!BitStream.Read(&driveby))
                        return false;
                    pSourcePlayer->SetDriveByDirection(driveby.data.ucDirection);
                }
            }
            else
                pSourcePlayer->SetWeaponSlot(0);

            // Vehicle specific data if he's the driver
            if (uiSeat == 0)
            {
                ReadVehicleSpecific(pVehicle, BitStream, iRemoteModelID, !bRejectVehicleState);

                // Set vehicle specific stuff if he's the driver
                if (!bRejectVehicleState)
                {
                    pVehicle->SetSirenActive(flags.data.bIsSirenOrAlarmActive);
                    pVehicle->SetSmokeTrailEnabled(flags.data.bIsSmokeTrailEnabled);
                    pVehicle->SetLandingGearDown(flags.data.bIsLandingGearDown);
                    pVehicle->SetOnGround(flags.data.bIsOnGround);
                    pVehicle->SetInWater(flags.data.bIsInWater);
                    pVehicle->SetDerailed(flags.data.bIsDerailed);
                    pVehicle->SetHeliSearchLightVisible(flags.data.bIsHeliSearchLightVisible);
                }
            }

            // Read the vehicle_look_left and vehicle_look_right control states
            // if it's an aircraft.
            if (flags.data.bIsAircraft)
            {
                ControllerState.LeftShoulder2 = BitStream.ReadBit() * 255;
                ControllerState.RightShoulder2 = BitStream.ReadBit() * 255;
            }

            // A locally-unfrozen or teleported vehicle must not smuggle throttle
            // or steering into the authoritative server controller state.
            if (!bRejectVehicleState)
                pSourcePlayer->GetPad()->NewControllerState(ControllerState);
            else
                pSourcePlayer->GetPad()->NewControllerState(CControllerState());

            const bool bReportedOnFire = BitStream.ReadBit();
            if (!bRejectVehicleState)
                pVehicle->SetOnFire(bReportedOnFire);

            if (bNeedsVehicleResync)
            {
                if (CVehicle* pCurrentVehicle = GetElementFromId<CVehicle>(vehicleID))
                    pSourcePlayer->Send(CVehicleResyncPacket(pCurrentVehicle));
            }

            if (bSignificantFrozenAttempt)
            {
                CLuaArguments arguments;
                arguments.PushElement(pVehicle);
                arguments.PushString("frozen_transform");
                pSourcePlayer->CallEvent("onPlayerInvalidVehicleSync", arguments, nullptr);
            }

            // Success
            return true;
        }
    }

    return false;
}

//
// NOTE: Any changes to this function will require similar changes to CSimVehiclePuresyncPacket::Write()
//
bool CVehiclePuresyncPacket::Write(NetBitStreamInterface& BitStream) const
{
    // Got a player to send?
    if (m_pSourceElement)
    {
        CPlayer* pSourcePlayer = static_cast<CPlayer*>(m_pSourceElement);

        // Player is in a vehicle and is the driver?
        CVehicle* pVehicle = pSourcePlayer->GetOccupiedVehicle();
        if (pVehicle)
        {
            // Player ID
            ElementID PlayerID = pSourcePlayer->GetID();
            BitStream.Write(PlayerID);

            // Write the time context of that player
            BitStream.Write(pSourcePlayer->GetSyncTimeContext());

            // Write his ping divided with 2 plus a small number so the client can find out when this packet was sent
            const unsigned int   uiPing = pSourcePlayer->GetPing();
            const unsigned short usLatency = uiPing <= 0xFFFF ? static_cast<unsigned short>(uiPing) : 0xFFFF;
            BitStream.WriteCompressed(usLatency);

            // Write the keysync data
            const CControllerState& ControllerState = pSourcePlayer->GetPad()->GetCurrentControllerState();
            WriteFullKeysync(ControllerState, BitStream);

            // Write the serverside model (#8800)
            BitStream.Write((int)pVehicle->GetModel());

            // Write the vehicle matrix only if he's the driver
            CVector      vecTemp;
            unsigned int uiSeat = pSourcePlayer->GetOccupiedVehicleSeat();
            if (uiSeat == 0)
            {
                // Vehicle position
                SPositionSync position(false);
                position.data.vecPosition = pVehicle->GetPosition();
                BitStream.Write(&position);

                // If the remote vehicle is a train, we want to read special train-specific data
                if (pVehicle->GetVehicleType() == VEHICLE_TRAIN)
                {
                    // Train specific data
                    float fPosition = pVehicle->GetTrainPosition();
                    bool  bDirection = pVehicle->GetTrainDirection();
                    float fSpeed = pVehicle->GetTrainSpeed();

                    BitStream.Write(fPosition);
                    BitStream.WriteBit(bDirection);
                    BitStream.Write(fSpeed);

                    // Push the train track information
                    const auto trainTrack = pVehicle->GetTrainTrack();
                    if (!trainTrack || pVehicle->IsDerailed())
                    {
                        // NOTE(qaisjp, feature/custom-train-tracks): when can a train both be on a track AND derailed?
                        // I suppose it's possible for some weirdness here. We should make sure that whenever we set the train track,
                        // we set that the train is NOT derailed; and that whenever we derail the track, we set the train track to nil.
                        // Note: here we use a uchar, in the CTT branch this is a uint. Just don't forget that, it might be important
                        BitStream.Write((uchar)0);
                    }
                    else if (trainTrack && trainTrack->IsDefault())
                    {
                        BitStream.Write(trainTrack->GetDefaultTrackId());
                    }
                    else
                    {
                        // TODO(qaisjp, feature/custom-train-tracks): implement behaviour for non-default tracks
                        assert(0 && "It is impossible for custom train tracks to exist right now, so this should never be reached.");
                    }
                }

                // Vehicle rotation
                SRotationDegreesSync rotation;
                pVehicle->GetRotationDegrees(rotation.data.vecRotation);
                BitStream.Write(&rotation);

                // Move speed vector
                SVelocitySync velocity;
                velocity.data.vecVelocity = pVehicle->GetVelocity();
                BitStream.Write(&velocity);

                // Turn speed vector
                SVelocitySync turnSpeed;
                turnSpeed.data.vecVelocity = pVehicle->GetTurnSpeed();
                BitStream.Write(&turnSpeed);

                // Health
                SVehicleHealthSync health;
                health.data.fValue = pVehicle->GetHealth();
                BitStream.Write(&health);

                // Write the trailer chain
                CVehicle* pTrailer = pVehicle->GetTowedVehicle();
                while (pTrailer)
                {
                    BitStream.WriteBit(true);
                    BitStream.Write(pTrailer->GetID());

                    // Write the position and rotation
                    CVector vecTrailerPosition, vecTrailerRotationDegrees;

                    // Write the matrix
                    vecTrailerPosition = pTrailer->GetPosition();
                    pTrailer->GetRotationDegrees(vecTrailerRotationDegrees);

                    SPositionSync trailerPosition(false);
                    trailerPosition.data.vecPosition = vecTrailerPosition;
                    BitStream.Write(&trailerPosition);

                    SRotationDegreesSync trailerRotation;
                    trailerRotation.data.vecRotation = vecTrailerRotationDegrees;
                    BitStream.Write(&trailerRotation);

                    // Get the next towed vehicle
                    pTrailer = pTrailer->GetTowedVehicle();
                }

                // End of our trailer chain
                BitStream.WriteBit(false);
            }

            // Player health and armor
            SPlayerHealthSync health;
            health.data.fValue = pSourcePlayer->GetHealth();
            BitStream.Write(&health);

            SPlayerArmorSync armor;
            armor.data.fValue = pSourcePlayer->GetArmor();
            BitStream.Write(&armor);

            // Weapon
            unsigned char ucWeaponType = pSourcePlayer->GetWeaponType();

            // Flags
            SVehiclePuresyncFlags flags;
            flags.data.bIsWearingGoggles = pSourcePlayer->IsWearingGoggles();
            flags.data.bIsDoingGangDriveby = pSourcePlayer->IsDoingGangDriveby();
            flags.data.bIsSirenOrAlarmActive = pVehicle->IsSirenActive();
            flags.data.bIsSmokeTrailEnabled = pVehicle->IsSmokeTrailEnabled();
            flags.data.bIsLandingGearDown = pVehicle->IsLandingGearDown();
            flags.data.bIsOnGround = pVehicle->IsOnGround();
            flags.data.bIsInWater = pVehicle->IsInWater();
            flags.data.bIsDerailed = pVehicle->IsDerailed();
            flags.data.bIsAircraft = (pVehicle->GetVehicleType() == VEHICLE_PLANE || pVehicle->GetVehicleType() == VEHICLE_HELI);
            flags.data.bHasAWeapon = (ucWeaponType != 0);
            flags.data.bIsHeliSearchLightVisible = pVehicle->IsHeliSearchLightVisible();
            BitStream.Write(&flags);

            // Write the weapon stuff
            if (flags.data.bHasAWeapon)
            {
                // Write the weapon slot
                SWeaponSlotSync slot;
                slot.data.uiSlot = pSourcePlayer->GetWeaponSlot();
                BitStream.Write(&slot);

                if (flags.data.bIsDoingGangDriveby && CWeaponNames::DoesSlotHaveAmmo(slot.data.uiSlot))
                {
                    // Write the ammo states
                    SWeaponAmmoSync ammo(ucWeaponType, true, true);
                    ammo.data.usAmmoInClip = pSourcePlayer->GetWeaponAmmoInClip();
                    ammo.data.usTotalAmmo = pSourcePlayer->GetWeaponTotalAmmo();
                    BitStream.Write(&ammo);

                    // Sync aim data
                    SWeaponAimSync aim(0.0f, true);
                    aim.data.vecOrigin = pSourcePlayer->GetSniperSourceVector();
                    pSourcePlayer->GetTargettingVector(aim.data.vecTarget);
                    aim.data.fArm = pSourcePlayer->GetAimDirection();
                    BitStream.Write(&aim);

                    // Sync driveby direction
                    SDrivebyDirectionSync driveby;
                    driveby.data.ucDirection = pSourcePlayer->GetDriveByDirection();
                    BitStream.Write(&driveby);
                }
            }

            // Vehicle specific data only if he's the driver
            if (uiSeat == 0)
            {
                WriteVehicleSpecific(pVehicle, BitStream);
            }

            // Write vehicle_look_left and vehicle_look_right control states when
            // it's an aircraft.
            if (flags.data.bIsAircraft)
            {
                BitStream.WriteBit(ControllerState.LeftShoulder2 != 0);
                BitStream.WriteBit(ControllerState.RightShoulder2 != 0);
            }

            // Write parts state
            SVehicleDamageSyncMethodeB damage;
            // Check where we are in the cycle
            uchar ucMode = (pVehicle->m_uiDamageInfoSendPhase & 3);
            damage.data.bSyncDoors = (ucMode == 0);
            damage.data.bSyncWheels = (ucMode == 1);
            damage.data.bSyncPanels = (ucMode == 2);
            damage.data.bSyncLights = (ucMode == 3);
            damage.data.doors.data.ucStates = pVehicle->m_ucDoorStates;
            damage.data.wheels.data.ucStates = pVehicle->m_ucWheelStates;
            damage.data.panels.data.ucStates = pVehicle->m_ucPanelStates;
            damage.data.lights.data.ucStates = pVehicle->m_ucLightStates;
            BitStream.Write(&damage);

            BitStream.WriteBit(pVehicle->IsOnFire());

            // Success
            return true;
        }
    }

    return false;
}

void CVehiclePuresyncPacket::ReadVehicleSpecific(CVehicle* pVehicle, NetBitStreamInterface& BitStream, int iRemoteModel, bool bApply)
{
    // Turret data
    unsigned short usModel = pVehicle->GetModel();
    if (CVehicleManager::HasTurret(iRemoteModel))
    {
        // Read out the turret position
        SVehicleTurretSync vehicle;
        if (!BitStream.Read(&vehicle))
            return;

        // Set the data
        if (bApply && CVehicleManager::HasTurret(usModel))
            pVehicle->SetTurretPosition(vehicle.data.fTurretX, vehicle.data.fTurretY);
    }

    // Adjustable property value
    if (CVehicleManager::HasAdjustableProperty(iRemoteModel))
    {
        unsigned short usAdjustableProperty;
        if (BitStream.Read(usAdjustableProperty) && bApply && CVehicleManager::HasAdjustableProperty(usModel))
        {
            pVehicle->SetAdjustableProperty(usAdjustableProperty);
        }
    }

    // Door angles.
    if (CVehicleManager::HasDoors(static_cast<unsigned short>(iRemoteModel)))
    {
        SDoorOpenRatioSync door;

        for (unsigned char i = 2; i < 6; ++i)
        {
            if (!BitStream.Read(&door))
                return;

            if (bApply && CVehicleManager::HasDoors(usModel))
                pVehicle->SetDoorOpenRatio(i, door.data.fRatio);
        }
    }
}

void CVehiclePuresyncPacket::WriteVehicleSpecific(CVehicle* pVehicle, NetBitStreamInterface& BitStream) const
{
    // Turret states
    unsigned short usModel = pVehicle->GetModel();
    if (CVehicleManager::HasTurret(usModel))
    {
        SVehicleTurretSync vehicle;
        pVehicle->GetTurretPosition(vehicle.data.fTurretX, vehicle.data.fTurretY);

        BitStream.Write(&vehicle);
    }

    // Adjustable property value
    if (CVehicleManager::HasAdjustableProperty(usModel))
    {
        BitStream.Write(pVehicle->GetAdjustableProperty());
    }

    // Door angles.
    if (CVehicleManager::HasDoors(usModel))
    {
        SDoorOpenRatioSync door;
        for (unsigned char i = 2; i < 6; ++i)
        {
            door.data.fRatio = pVehicle->GetDoorOpenRatio(i);
            BitStream.Write(&door);
        }
    }
}
