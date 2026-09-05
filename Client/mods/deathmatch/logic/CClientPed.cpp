/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *               (Shared logic for modifications)
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/shared_logic/CClientPed.cpp
 *  PURPOSE:     Ped entity class
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CNativeAITelemetry.h"
#include <game/CAEVehicleAudioEntity.h>
#include <game/CAnimBlendAssocGroup.h>
#include <game/CAnimManager.h>
#include <game/CCam.h>
#include <game/CCarEnterExit.h>
#include <game/CColStore.h>
#include <game/CColPoint.h>
#include <game/CFx.h>
#include <game/CPedIntelligence.h>
#include <game/CPedSound.h>
#include <game/CPointLights.h>
#include <game/CStreaming.h>
#include <game/CTaskManager.h>
#include <game/CTasks.h>
#include <game/CVisibilityPlugins.h>
#include <game/CWeapon.h>
#include <game/CWeaponStat.h>
#include <game/CWeaponStatManager.h>
#include <game/TaskBasic.h>
#include <game/TaskCar.h>
#include <game/TaskCarAccessories.h>
#include <game/TaskIK.h>
#include <game/TaskJumpFall.h>
#include <game/TaskPhysicalResponse.h>
#include <game/TaskAttack.h>
#include <game/TaskGoTo.h>
#include <game/TaskSimpleSwim.h>
#include "enums/VehicleType.h"
#include <limits>
#include <unordered_map>

using std::list;
using std::vector;

extern CClientGame* g_pClientGame;

#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif

#define INVALID_VALUE 0xFFFFFFFF

#define PED_INTERPOLATION_WARP_THRESHOLD           5  // Minimal threshold
#define PED_INTERPOLATION_WARP_THRESHOLD_FOR_SPEED 5  // Units to increment the threshold per speed unit

namespace
{
    constexpr unsigned long REMOTE_PED_STREAM_IN_TRANSFORM_FENCE_TIMEOUT = 1500;
    constexpr unsigned long REMOTE_PED_STREAM_IN_TRANSFORM_FENCE_MINIMUM = 100;
    constexpr unsigned long NATIVE_COLLISION_RESIDENCY_PROBE_INTERVAL = 100;
    constexpr float         NATIVE_COLLISION_GROUND_PROBE_ABOVE = 0.25f;
    constexpr float         NATIVE_COLLISION_BASE_TOLERANCE_BELOW = 0.35f;
    constexpr float         NATIVE_COLLISION_BASE_TOLERANCE_ABOVE = 0.75f;

    bool IsRemotePedStreamInTransformFenceTraceEnabled()
    {
        static const bool enabled = FileExists(SharedUtil::CalcMTASAPath("mta\\logs\\remote-ped-stream-fence-trace.enable"));
        return enabled;
    }

    bool IsNativeCollisionResidencyTraceEnabled()
    {
        static const bool enabled = FileExists(SharedUtil::CalcMTASAPath("mta\\logs\\native-collision-residency-trace.enable"));
        return enabled;
    }
}

enum eAnimGroups
{
    ANIM_GROUP_GOGGLES = 32,
    ANIM_GROUP_STEALTH_KN = 87,
};

enum eAnimIDs
{
    ANIM_ID_WALK_CIVI = 0,
    ANIM_ID_RUN_CIVI,
    ANIM_ID_SPRINT_PANIC,
    ANIM_ID_IDLE_STANCE,
    ANIM_ID_WEAPON_CROUCH = 55,
    ANIM_ID_GOGGLES_ON = 224,
    ANIM_ID_STEALTH_AIM = 347,
};

#define STEALTH_KILL_RANGE 2.5f

struct SBodyPartName
{
    const char* szName;
};

static const SFixedArray<SBodyPartName, 10> BodyPartNames = {
    {{"Unknown"}, {"Unknown"}, {"Unknown"}, {"Torso"}, {"Ass"}, {"Left Arm"}, {"Right Arm"}, {"Left Leg"}, {"Right Leg"}, {"Head"}}};

namespace
{
    constexpr unsigned long NATIVE_TASK_LOCOMOTION_TRACE_HEARTBEAT = 1000;

    enum class ENativeTaskLocomotionTraceChannel
    {
        PRODUCER,
        RECEIVE,
        APPLY,
    };

    struct SNativeTaskLocomotionTraceChannelState
    {
        SString       signature;
        unsigned long lastLoggedAt{};
        bool          initialized{};
    };

    struct SNativeTaskLocomotionTraceState
    {
        SNativeTaskLocomotionTraceChannelState producer;
        SNativeTaskLocomotionTraceChannelState receive;
        SNativeTaskLocomotionTraceChannelState apply;
    };

    std::unordered_map<unsigned int, SNativeTaskLocomotionTraceState>        g_nativeTaskLocomotionTraceStates;
    std::unordered_map<unsigned int, SNativeTaskLocomotionTraceChannelState> g_nativeTaskLocomotionVelocityTraceStates;
    std::unordered_map<unsigned int, SNativeTaskLocomotionTraceChannelState> g_nativeTaskAnimationProducerTraceStates;

    bool IsNativeTaskLocomotionTraceEnabled()
    {
        // This diagnostic is deliberately opt-in because these paths execute
        // every frame for streamed peds. The marker makes field captures easy
        // to enable without adding a permanent client setting or log volume.
        static const bool enabled = FileExists(CalcMTASAPath("mta\\logs\\native-task-locomotion-trace.enable"));
        static bool       announced = false;
        if (enabled && !announced && g_pCore)
        {
            announced = true;
            g_pCore->GetConsole()->Printf("[native-task-locomotion][enabled] profile=%s pid=%u heartbeat=%lu", g_pCore->IsSecondaryClient() ? "cl2" : "primary",
                                          static_cast<unsigned int>(GetCurrentProcessId()), NATIVE_TASK_LOCOMOTION_TRACE_HEARTBEAT);
        }
        return enabled;
    }

    const char* GetNativeTaskLocomotionMoveStateName(PedMoveState::Enum moveState)
    {
        switch (moveState)
        {
            case PedMoveState::PEDMOVE_NONE:
                return "none";
            case PedMoveState::PEDMOVE_STILL:
                return "still";
            case PedMoveState::PEDMOVE_TURN_L:
                return "turn_left";
            case PedMoveState::PEDMOVE_TURN_R:
                return "turn_right";
            case PedMoveState::PEDMOVE_WALK:
                return "walk";
            case PedMoveState::PEDMOVE_JOG:
                return "jog";
            case PedMoveState::PEDMOVE_RUN:
                return "run";
            case PedMoveState::PEDMOVE_SPRINT:
                return "sprint";
            default:
                return "unknown";
        }
    }

    const char* GetNativeTaskLocomotionModeName(unsigned int mode)
    {
        switch (mode)
        {
            case SNativeTaskLocomotionSync::NONE:
                return "none";
            case SNativeTaskLocomotionSync::WALK:
                return "walk";
            case SNativeTaskLocomotionSync::RUN:
                return "run";
            case SNativeTaskLocomotionSync::SPRINT:
                return "sprint";
            default:
                return "invalid";
        }
    }

    SNativeTaskLocomotionTraceChannelState& GetNativeTaskLocomotionTraceChannel(CClientPed* pPed, ENativeTaskLocomotionTraceChannel channel)
    {
        SNativeTaskLocomotionTraceState& state = g_nativeTaskLocomotionTraceStates[pPed->GetID().Value()];
        switch (channel)
        {
            case ENativeTaskLocomotionTraceChannel::PRODUCER:
                return state.producer;
            case ENativeTaskLocomotionTraceChannel::RECEIVE:
                return state.receive;
            default:
                return state.apply;
        }
    }

    bool ShouldTraceNativeTaskLocomotion(CClientPed* pPed, ENativeTaskLocomotionTraceChannel channel, const SString& signature)
    {
        // Native walking-style peds use the same presentation channel as
        // mission actors. Including them keeps the opt-in trace useful for
        // ambient AI without coupling diagnostics to a particular resource.
        if (!pPed || (!pPed->IsMissionActor() && !pPed->IsUsingNativeWalkingStyle()) || !IsNativeTaskLocomotionTraceEnabled())
            return false;

        const unsigned long now = CClientTime::GetTime();
        auto&               state = GetNativeTaskLocomotionTraceChannel(pPed, channel);
        if (state.initialized && state.signature == signature && now - state.lastLoggedAt < NATIVE_TASK_LOCOMOTION_TRACE_HEARTBEAT)
            return false;

        state.signature = signature;
        state.lastLoggedAt = now;
        state.initialized = true;
        return true;
    }

    CTask* GetNativeTaskLocomotionPrimaryTask(CClientPed* pPed)
    {
        CTaskManager* pTaskManager = pPed->GetTaskManager();
        return pTaskManager ? pTaskManager->GetTask(TASK_PRIORITY_PRIMARY) : nullptr;
    }

    CTask* GetNativeTaskLocomotionSimplestTask(CClientPed* pPed)
    {
        CTaskManager* pTaskManager = pPed->GetTaskManager();
        return pTaskManager ? pTaskManager->GetSimplestActiveTask() : nullptr;
    }

    CTask* GetDeepestNativeSubTask(CTask* pTask)
    {
        // GTA's group hand-signal task is complex while the live animation is
        // owned by its SimpleAnim leaf. Keep traversal bounded so diagnostics
        // never turn a damaged native task chain into an unbounded walk.
        for (unsigned int depth = 0; pTask && depth < 16; ++depth)
        {
            CTask* pSubTask = pTask->GetSubTask();
            if (!pSubTask || pSubTask == pTask)
                break;
            pTask = pSubTask;
        }
        return pTask;
    }

    bool IsLiveGangTalkAnimation(CTask* pTask)
    {
        if (!pTask || pTask->GetTaskType() != TASK_SIMPLE_ANIM)
            return false;

        unsigned short animGroup = 0;
        unsigned short animId = 0;
        float          progress = 0.0f;
        float          speed = 0.0f;
        float          blendAmount = 0.0f;
        if (!pTask->GetPresentationAnimation(animGroup, animId, progress, speed, blendAmount))
            return false;

        return animGroup == static_cast<unsigned short>(eAnimGroup::ANIM_GROUP_GANGS) &&
               animId >= static_cast<unsigned short>(eAnimID::ANIM_ID_PRTIAL_GNGTLKA) && animId <= static_cast<unsigned short>(eAnimID::ANIM_ID_PRTIAL_GNGTLKH);
    }

    bool IsNativeTaskPartialAnimation(const SNativeTaskAnimationPresentationSync& presentation)
    {
        if (!SNativeTaskAnimationPresentationSync::IsAnimationMode(presentation.data.uiMode))
            return false;

        const auto group = static_cast<eAnimGroup>(presentation.data.usAnimGroup);
        if (group == eAnimGroup::ANIM_GROUP_HANDSIGNAL || group == eAnimGroup::ANIM_GROUP_HANDSIGNALL)
            return true;

        return group == eAnimGroup::ANIM_GROUP_GANGS && presentation.data.usAnimId >= static_cast<unsigned short>(eAnimID::ANIM_ID_PRTIAL_GNGTLKA) &&
               presentation.data.usAnimId <= static_cast<unsigned short>(eAnimID::ANIM_ID_PRTIAL_GNGTLKH);
    }

    const char* GetNamedAnimPresentationValidationName(ENamedAnimPresentationValidation validation)
    {
        switch (validation)
        {
            case ENamedAnimPresentationValidation::VALID:
                return "valid";
            case ENamedAnimPresentationValidation::NO_ASSOCIATION:
                return "no_association";
            case ENamedAnimPresentationValidation::NO_HIERARCHY:
                return "no_hierarchy";
            case ENamedAnimPresentationValidation::INVALID_TOTAL_TIME:
                return "invalid_total_time";
            case ENamedAnimPresentationValidation::INVALID_ANIM_GROUP:
                return "invalid_anim_group";
            case ENamedAnimPresentationValidation::INVALID_ANIM_ID:
                return "invalid_anim_id";
            case ENamedAnimPresentationValidation::INVALID_CURRENT_TIME:
                return "invalid_current_time";
            case ENamedAnimPresentationValidation::INVALID_SPEED:
                return "invalid_speed";
            case ENamedAnimPresentationValidation::NON_POSITIVE_SPEED:
                return "non_positive_speed";
            case ENamedAnimPresentationValidation::INVALID_BLEND_AMOUNT:
                return "invalid_blend_amount";
            case ENamedAnimPresentationValidation::INACTIVE_BLEND_AMOUNT:
                return "inactive_blend_amount";
            default:
                return "unknown";
        }
    }

    void TraceNativeTaskAnimationProducer(CClientPed* pPed, const char* reason, const char* selection, CTask* pPresentationTask,
                                          const SNamedAnimPresentationDiagnostic* pNamedDiagnostic = nullptr)
    {
        if (!pPed || (!pPed->IsMissionActor() && !pPed->IsUsingNativeWalkingStyle()) || !IsNativeTaskLocomotionTraceEnabled())
            return;

        CTask*              pFightTask = pPed->GetGamePlayer() ? pPed->GetGamePlayer()->GetPedIntelligence()->GetFightTask() : nullptr;
        CTask*              pSimplestTask = GetNativeTaskLocomotionSimplestTask(pPed);
        CTask*              pPartialTask = pPed->GetTaskManager() ? pPed->GetTaskManager()->GetTaskSecondary(TASK_SECONDARY_PARTIAL_ANIM) : nullptr;
        CTask*              pPartialLeaf = GetDeepestNativeSubTask(pPartialTask);
        const int           fightType = pFightTask ? static_cast<int>(pFightTask->GetTaskType()) : -1;
        const int           simplestType = pSimplestTask ? static_cast<int>(pSimplestTask->GetTaskType()) : -1;
        const int           partialType = pPartialTask ? static_cast<int>(pPartialTask->GetTaskType()) : -1;
        const int           partialLeafType = pPartialLeaf ? static_cast<int>(pPartialLeaf->GetTaskType()) : -1;
        const int           selectedType = pPresentationTask ? static_cast<int>(pPresentationTask->GetTaskType()) : -1;
        const bool          managedGroup = pPed->GetGamePlayer() && pPed->GetGamePlayer()->IsNativeAmbientGroupActive();
        const auto          validation = pNamedDiagnostic ? pNamedDiagnostic->validation : ENamedAnimPresentationValidation::VALID;
        const SString       signature("%s:%s:%d:%d:%d:%d:%d:%d:%d:%d", reason, selection, selectedType, fightType, simplestType, partialType, partialLeafType,
                                      managedGroup, pPed->HasSyncedAnim(), static_cast<int>(validation));
        const unsigned long now = CClientTime::GetTime();
        auto&               state = g_nativeTaskAnimationProducerTraceStates[pPed->GetID().Value()];
        if (state.initialized && state.signature == signature && now - state.lastLoggedAt < NATIVE_TASK_LOCOMOTION_TRACE_HEARTBEAT)
            return;

        state.signature = signature;
        state.lastLoggedAt = now;
        state.initialized = true;

        const auto* pNamedTask =
            pPresentationTask && selectedType == TASK_SIMPLE_NAMED_ANIM ? dynamic_cast<const CTaskSimpleRunNamedAnim*>(pPresentationTask) : nullptr;
        g_pCore->GetConsole()->Printf(
            "[native-task-animation][producer] profile=%s pid=%u ped=%u model=%lu reason=%s selection=%s syncedAnim=%d occupied=%d entering=%d "
            "exiting=%d dead=%d managedGroup=%d fight=%s(%d) simplest=%s(%d) partial=%s(%d) partialLeaf=%s(%d) selected=%s(%d) "
            "named=%s/%s validation=%s association=%p hierarchy=%p group=%d anim=%d total=%.4f current=%.4f speed=%.4f blend=%.4f",
            g_pCore->IsSecondaryClient() ? "cl2" : "primary", static_cast<unsigned int>(GetCurrentProcessId()), pPed->GetID().Value(), pPed->GetModel(), reason,
            selection, pPed->HasSyncedAnim(), pPed->GetRealOccupiedVehicle() != nullptr, pPed->IsGettingIntoVehicle(), pPed->IsGettingOutOfVehicle(),
            pPed->IsDead(), managedGroup, pFightTask ? pFightTask->GetTaskName() : "none", fightType, pSimplestTask ? pSimplestTask->GetTaskName() : "none",
            simplestType, pPartialTask ? pPartialTask->GetTaskName() : "none", partialType, pPartialLeaf ? pPartialLeaf->GetTaskName() : "none",
            partialLeafType, pPresentationTask ? pPresentationTask->GetTaskName() : "none", selectedType,
            pNamedTask && pNamedTask->GetGroupName() ? pNamedTask->GetGroupName() : "none",
            pNamedTask && pNamedTask->GetAnimName() ? pNamedTask->GetAnimName() : "none", GetNamedAnimPresentationValidationName(validation),
            pNamedDiagnostic ? pNamedDiagnostic->association : nullptr, pNamedDiagnostic ? pNamedDiagnostic->hierarchy : nullptr,
            pNamedDiagnostic ? pNamedDiagnostic->animGroup : -1, pNamedDiagnostic ? pNamedDiagnostic->animId : -1,
            pNamedDiagnostic ? pNamedDiagnostic->totalTime : 0.0f, pNamedDiagnostic ? pNamedDiagnostic->currentTime : 0.0f,
            pNamedDiagnostic ? pNamedDiagnostic->speed : 0.0f, pNamedDiagnostic ? pNamedDiagnostic->blendAmount : 0.0f);
    }

    PedMoveState::Enum GetNativeTaskLocomotionMoveState(CClientPed* pPed)
    {
        return pPed->GetGamePlayer() ? pPed->GetGamePlayer()->GetMoveState() : PedMoveState::PEDMOVE_NONE;
    }

    std::optional<PedMoveState::Enum> GetNativeTaskLocomotionParentMoveState(CTask* pTask)
    {
        for (CTask* pCurrent = pTask; pCurrent; pCurrent = pCurrent->GetParent())
        {
            if (pCurrent->GetTaskType() == TASK_COMPLEX_WANDER)
            {
                auto* pWander = dynamic_cast<CTaskComplexWander*>(pCurrent);
                if (!pWander)
                    return std::nullopt;

                return static_cast<PedMoveState::Enum>(pWander->GetMoveState());
            }

            if (pCurrent->GetTaskType() == TASK_COMPLEX_GO_TO_POINT_AND_STAND_STILL)
            {
                auto* pGoTo = dynamic_cast<CTaskComplexGoToPointAndStandStill*>(pCurrent);
                if (!pGoTo)
                    return std::nullopt;

                return static_cast<PedMoveState::Enum>(pGoTo->GetMoveState());
            }

            if (pCurrent->GetTaskType() == TASK_COMPLEX_FOLLOW_NODE_ROUTE)
            {
                auto* pNavigate = dynamic_cast<CTaskComplexFollowNodeRoute*>(pCurrent);
                if (!pNavigate)
                    return std::nullopt;

                return static_cast<PedMoveState::Enum>(pNavigate->GetMoveState());
            }
        }
        return std::nullopt;
    }

    bool HasWalkAlongsidePedAncestor(CTask* pTask)
    {
        for (CTask* current = pTask; current; current = current->GetParent())
        {
            if (current->GetTaskType() == TASK_COMPLEX_WALK_ALONGSIDE_PED)
                return true;
        }
        return false;
    }

    std::optional<PedMoveState::Enum> GetNativeTaskLocomotionCommandMoveState(CTask* pTask)
    {
        // WalkAlongside keeps its durable Seek/GoTo parent at SPRINT so a
        // distant partner can catch up. Retail SelectMoveState independently
        // downshifts the live SimpleGoTo child to WALK below five metres.
        // Script peds are CPlayerPed wrappers, so treating the parent intent as
        // the effective command makes the owner synthesize sprint input and
        // defeats that stock distance controller.
        if (HasWalkAlongsidePedAncestor(pTask) && pTask &&
            (pTask->GetTaskType() == TASK_SIMPLE_GO_TO_POINT || pTask->GetTaskType() == TASK_SIMPLE_GO_TO_POINT_FINE))
        {
            auto* pSimpleGoTo = dynamic_cast<CTaskSimpleGoTo*>(pTask);
            if (pSimpleGoTo)
                return static_cast<PedMoveState::Enum>(pSimpleGoTo->GetMoveState());
        }

        // Preserve the durable complex command first. Story go-to tasks rely
        // on that intent while GTA blends or slows their active subtask.
        if (const auto parentMoveState = GetNativeTaskLocomotionParentMoveState(pTask))
            return parentMoveState;

        // Event-response tasks such as pedestrian avoidance can temporarily
        // own Wander's active SimpleGoTo without linking its parent chain back
        // to the primary Wander. That subtask still carries the exact movement
        // command GTA is processing and must win over the transient ped state.
        if (pTask && (pTask->GetTaskType() == TASK_SIMPLE_GO_TO_POINT || pTask->GetTaskType() == TASK_SIMPLE_GO_TO_POINT_FINE))
        {
            auto* pSimpleGoTo = dynamic_cast<CTaskSimpleGoTo*>(pTask);
            if (pSimpleGoTo)
                return static_cast<PedMoveState::Enum>(pSimpleGoTo->GetMoveState());
        }

        return std::nullopt;
    }

    bool HasGangFollowerAncestor(CTask* pTask)
    {
        for (CTask* current = pTask; current; current = current->GetParent())
        {
            if (current->GetTaskType() == TASK_COMPLEX_GANG_FOLLOWER)
                return true;
        }
        return false;
    }

    bool HasNativeGangFollowerWalkSpeedOverride(CClientPed* pPed, CTask* pTask)
    {
        return pPed && pPed->GetGamePlayer() && pPed->GetGamePlayer()->IsNativeAmbientGroupActive() && HasGangFollowerAncestor(pTask) &&
               pPed->GetGamePlayer()->IsMoveAnimationSpeedSetByTask();
    }

    void TraceNativeTaskLocomotionProducer(CClientPed* pPed, const char* reason, const SNativeTaskLocomotionSync& locomotion,
                                           const CControllerState& controllerState, const CVector& velocity)
    {
        CTask*                   pPrimaryTask = GetNativeTaskLocomotionPrimaryTask(pPed);
        CTask*                   pSimplestTask = GetNativeTaskLocomotionSimplestTask(pPed);
        const PedMoveState::Enum moveState = GetNativeTaskLocomotionMoveState(pPed);
        const auto               commandMoveState = GetNativeTaskLocomotionCommandMoveState(pSimplestTask);
        const auto               parentMoveState = GetNativeTaskLocomotionParentMoveState(pSimplestTask);
        const bool               walkAlongsideChild =
            commandMoveState && HasWalkAlongsidePedAncestor(pSimplestTask) && pSimplestTask &&
            (pSimplestTask->GetTaskType() == TASK_SIMPLE_GO_TO_POINT || pSimplestTask->GetTaskType() == TASK_SIMPLE_GO_TO_POINT_FINE);
        const char* commandSource =
            walkAlongsideChild ? "walk-alongside-child" : (parentMoveState ? "complex-parent" : (commandMoveState ? "simple-go-to" : "live-ped"));
        CTask*        pSimplestParentTask = pSimplestTask ? pSimplestTask->GetParent() : nullptr;
        const int     primaryType = pPrimaryTask ? static_cast<int>(pPrimaryTask->GetTaskType()) : -1;
        const int     simplestType = pSimplestTask ? static_cast<int>(pSimplestTask->GetTaskType()) : -1;
        const int     simplestParentType = pSimplestParentTask ? static_cast<int>(pSimplestParentTask->GetTaskType()) : -1;
        const SString signature("%s:%u:%d:%d:%d:%d:%d:%d:%d", reason, locomotion.data.uiMode, static_cast<int>(moveState),
                                commandMoveState ? static_cast<int>(*commandMoveState) : -1, parentMoveState ? static_cast<int>(*parentMoveState) : -1,
                                primaryType, simplestType, simplestParentType, controllerState.LeftStickX != 0 || controllerState.LeftStickY != 0);
        if (!ShouldTraceNativeTaskLocomotion(pPed, ENativeTaskLocomotionTraceChannel::PRODUCER, signature))
            return;

        g_pCore->GetConsole()->Printf(
            "[native-task-locomotion][producer] profile=%s pid=%u ped=%u model=%lu reason=%s mode=%s stick=(%d,%d) controller=(%d,%d,w=%u,x=%u) "
            "move=%s command=%s commandSource=%s parentCommand=%s velocity=(%.4f,%.4f,%.4f) primary=%s(%d) simplest=%s(%d) simplestParent=%s(%d)",
            g_pCore->IsSecondaryClient() ? "cl2" : "primary", static_cast<unsigned int>(GetCurrentProcessId()), pPed->GetID().Value(), pPed->GetModel(), reason,
            GetNativeTaskLocomotionModeName(locomotion.data.uiMode), locomotion.data.sLeftStickX, locomotion.data.sLeftStickY, controllerState.LeftStickX,
            controllerState.LeftStickY, static_cast<unsigned int>(controllerState.m_bPedWalk), static_cast<unsigned int>(controllerState.ButtonCross),
            GetNativeTaskLocomotionMoveStateName(moveState), commandMoveState ? GetNativeTaskLocomotionMoveStateName(*commandMoveState) : "unavailable",
            commandSource, parentMoveState ? GetNativeTaskLocomotionMoveStateName(*parentMoveState) : "unavailable", velocity.fX, velocity.fY, velocity.fZ,
            pPrimaryTask ? pPrimaryTask->GetTaskName() : "none", primaryType, pSimplestTask ? pSimplestTask->GetTaskName() : "none", simplestType,
            pSimplestParentTask ? pSimplestParentTask->GetTaskName() : "none", simplestParentType);
    }

    void TraceNativeTaskLocomotionReceive(CClientPed* pPed, const SNativeTaskLocomotionSync& locomotion, const char* source)
    {
        const SString signature("%s:%u", source, locomotion.data.uiMode);
        if (!ShouldTraceNativeTaskLocomotion(pPed, ENativeTaskLocomotionTraceChannel::RECEIVE, signature))
            return;

        g_pCore->GetConsole()->Printf("[native-task-locomotion][receive] profile=%s pid=%u ped=%u model=%lu source=%s mode=%s stick=(%d,%d) syncing=%d",
                                      g_pCore->IsSecondaryClient() ? "cl2" : "primary", static_cast<unsigned int>(GetCurrentProcessId()), pPed->GetID().Value(),
                                      pPed->GetModel(), source, GetNativeTaskLocomotionModeName(locomotion.data.uiMode), locomotion.data.sLeftStickX,
                                      locomotion.data.sLeftStickY, pPed->IsSyncing());
    }

    void TraceNativeTaskLocomotionApply(CClientPed* pPed, const char* reason, const SNativeTaskLocomotionSync& locomotion, unsigned long sampleAge,
                                        unsigned long presentationLease, const CControllerState& baseState, const CControllerState& appliedState)
    {
        CTask*                   pPrimaryTask = GetNativeTaskLocomotionPrimaryTask(pPed);
        CTask*                   pSimplestTask = GetNativeTaskLocomotionSimplestTask(pPed);
        const PedMoveState::Enum moveState = GetNativeTaskLocomotionMoveState(pPed);
        const int                primaryType = pPrimaryTask ? static_cast<int>(pPrimaryTask->GetTaskType()) : -1;
        const int                simplestType = pSimplestTask ? static_cast<int>(pSimplestTask->GetTaskType()) : -1;
        const SString signature("%s:%u:%d:%d:%d:%d", reason, locomotion.data.uiMode, static_cast<int>(moveState), primaryType, simplestType, pPed->IsSyncing());
        if (!ShouldTraceNativeTaskLocomotion(pPed, ENativeTaskLocomotionTraceChannel::APPLY, signature))
            return;

        g_pCore->GetConsole()->Printf(
            "[native-task-locomotion][apply] profile=%s pid=%u ped=%u model=%lu reason=%s mode=%s sampleAge=%lu lease=%lu syncing=%d "
            "base=(%d,%d,w=%u,x=%u) applied=(%d,%d,w=%u,x=%u) move=%s primary=%s(%d) simplest=%s(%d)",
            g_pCore->IsSecondaryClient() ? "cl2" : "primary", static_cast<unsigned int>(GetCurrentProcessId()), pPed->GetID().Value(), pPed->GetModel(), reason,
            GetNativeTaskLocomotionModeName(locomotion.data.uiMode), sampleAge, presentationLease, pPed->IsSyncing(), baseState.LeftStickX,
            baseState.LeftStickY, static_cast<unsigned int>(baseState.m_bPedWalk), static_cast<unsigned int>(baseState.ButtonCross), appliedState.LeftStickX,
            appliedState.LeftStickY, static_cast<unsigned int>(appliedState.m_bPedWalk), static_cast<unsigned int>(appliedState.ButtonCross),
            GetNativeTaskLocomotionMoveStateName(moveState), pPrimaryTask ? pPrimaryTask->GetTaskName() : "none", primaryType,
            pSimplestTask ? pSimplestTask->GetTaskName() : "none", simplestType);
    }

    void TraceNativeTaskLocomotionVelocityLimit(CClientPed* pPed, const CVector& authoritativeVelocity, const CVector& localVelocity,
                                                const CVector& limitedVelocity, unsigned long sampleAge)
    {
        if (!pPed || (!pPed->IsMissionActor() && !pPed->IsUsingNativeWalkingStyle()) || !IsNativeTaskLocomotionTraceEnabled())
            return;

        const float   authoritativeSpeed = std::sqrt(authoritativeVelocity.fX * authoritativeVelocity.fX + authoritativeVelocity.fY * authoritativeVelocity.fY);
        const float   localSpeed = std::sqrt(localVelocity.fX * localVelocity.fX + localVelocity.fY * localVelocity.fY);
        const float   limitedSpeed = std::sqrt(limitedVelocity.fX * limitedVelocity.fX + limitedVelocity.fY * limitedVelocity.fY);
        const SString signature("%.3f:%.3f:%.3f", authoritativeSpeed, localSpeed, limitedSpeed);
        const unsigned long now = CClientTime::GetTime();
        auto&               state = g_nativeTaskLocomotionVelocityTraceStates[pPed->GetID().Value()];
        if (state.initialized && state.signature == signature && now - state.lastLoggedAt < NATIVE_TASK_LOCOMOTION_TRACE_HEARTBEAT)
            return;

        state.signature = signature;
        state.lastLoggedAt = now;
        state.initialized = true;
        g_pCore->GetConsole()->Printf(
            "[native-task-locomotion][velocity-limit] profile=%s pid=%u ped=%u model=%lu sampleAge=%lu authoritative=(%.4f,%.4f,%.4f) "
            "local=(%.4f,%.4f,%.4f) limited=(%.4f,%.4f,%.4f)",
            g_pCore->IsSecondaryClient() ? "cl2" : "primary", static_cast<unsigned int>(GetCurrentProcessId()), pPed->GetID().Value(), pPed->GetModel(),
            sampleAge, authoritativeVelocity.fX, authoritativeVelocity.fY, authoritativeVelocity.fZ, localVelocity.fX, localVelocity.fY, localVelocity.fZ,
            limitedVelocity.fX, limitedVelocity.fY, limitedVelocity.fZ);
    }

    CWeaponInfo* GetNativeTaskWeaponPresentationInfo(CClientPed* ped, eWeaponType weaponType)
    {
        if (!ped || weaponType < WEAPONTYPE_UNARMED || weaponType >= WEAPONTYPE_LAST_WEAPONTYPE)
            return nullptr;

        CWeapon* weapon = ped->GetWeapon(ped->GetCurrentWeaponSlot());
        if (!weapon || weapon->GetType() != weaponType)
            return nullptr;

        eWeaponSkill skill = WEAPONSKILL_STD;
        if (weaponType >= WEAPONTYPE_PISTOL && weaponType <= WEAPONTYPE_TEC9)
        {
            const float level = ped->GetStat(g_pGame->GetStats()->GetSkillStatIndex(weaponType));
            skill = g_pGame->GetWeaponStatManager()->GetWeaponSkillFromSkillLevel(weaponType, level);
        }
        return weapon->GetInfo(skill);
    }

    bool IsNativeTaskWeaponPresentationSupported(CClientPed* ped, eWeaponType weaponType, bool allowSpray)
    {
        CWeaponInfo* info = GetNativeTaskWeaponPresentationInfo(ped, weaponType);
        if (!info)
            return false;

        // Use the live weapon-property fire type, not a weapon-ID list: Lua can
        // alter weapon properties, and projectile/area-effect weapons carry
        // world authority that a presentation-only peer must never inherit.
        return info->GetFireType() == FIRETYPE_INSTANT_HIT || (allowSpray && weaponType == WEAPONTYPE_SPRAYCAN && info->GetFireType() == FIRETYPE_AREA_EFFECT);
    }
}  // namespace

CClientPed::CClientPed(CClientManager* pManager, unsigned long ulModelID, ElementID ID)
    : ClassInit(this), CClientStreamElement(pManager->GetPlayerStreamer(), ID), CAntiCheatModule(pManager->GetAntiCheat())
{
    SetTypeName("ped");

    // Init
    Init(pManager, ulModelID, false);

    // Add it to our ped manager
    pManager->GetPedManager()->AddToList(this);
}

CClientPed::CClientPed(CClientManager* pManager, unsigned long ulModelID, ElementID ID, bool bIsLocalPlayer)
    : ClassInit(this), CClientStreamElement(pManager->GetPlayerStreamer(), ID), CAntiCheatModule(pManager->GetAntiCheat())
{
    // Init
    Init(pManager, ulModelID, bIsLocalPlayer);

    // Add it to our ped manager
    pManager->GetPedManager()->AddToList(this);
}

void CClientPed::Init(CClientManager* pManager, unsigned long ulModelID, bool bIsLocalPlayer)
{
    CClientEntityRefManager::AddEntityRefs(ENTITY_REF_DEBUG(this, "CClientPed"), &m_pOccupiedVehicle, &m_pOccupyingVehicle, &m_pTargetedEntity,
                                           &m_pCurrentContactEntity, &m_pBulletImpactEntity, &m_interp.pTargetOriginSource, NULL);

    // Init members
    m_pManager = pManager;

    // Store the model we're going to use
    m_ulModel = ulModelID;
    m_usLogicalModel = 0xFFFF;
    m_pModelInfo = g_pGame->GetModelInfo(ulModelID);

    m_bIsLocalPlayer = bIsLocalPlayer;
    m_pPad = g_pGame->GetPad();

    m_pRequester = pManager->GetModelRequestManager();

    m_bTaskToBeRestoredOnAnimEnd = false;
    m_eTaskTypeToBeRestoredOnAnimEnd = TASK_SIMPLE_PLAYER_ON_FOOT;
    m_bisNextAnimationCustom = false;
    m_bisCurrentAnimationCustom = false;
    m_strCustomIFPBlockName = "Default";
    m_strCustomIFPAnimationName = "Default";
    m_u32CustomBlockNameHash = 0;
    m_u32CustomAnimationNameHash = 0;
    m_iVehicleInOutState = VEHICLE_INOUT_NONE;
    m_pPlayerPed = NULL;
    m_pTaskManager = NULL;
    m_pOccupiedVehicle = NULL;
    m_pOccupyingVehicle = NULL;
    // m_uiOccupyingSeat = 0;
    m_uiOccupiedVehicleSeat = 0xFF;
    m_bHealthLocked = false;
    m_bDontChangeRadio = false;
    m_armorLocked = false;
    m_ulLastOnScreenTime = 0;
    m_pLoadedModelInfo = NULL;
    m_pOutOfVehicleWeaponSlot = WEAPONSLOT_MAX;  // WEAPONSLOT_MAX = invalid
    m_bRadioOn = false;
    m_ucRadioChannel = 1;
    m_fBeginAimX = 0.0f;
    m_fBeginAimY = 0.0f;
    m_fTargetAimX = 0.0f;
    m_fTargetAimY = 0.0f;
    m_ulBeginAimTime = 0;
    m_ulTargetAimTime = 0;
    m_ulBeginRotationTime = 0;
    m_ulEndRotationTime = 0;
    m_fBeginRotation = 0.0f;
    m_fTargetRotationA = 0.0f;
    m_fBeginCameraRotation = 0.0f;
    m_fTargetCameraRotation = 0.0f;
    m_ulBeginTarget = 0;
    m_ulEndTarget = 0;
    m_bForceGettingIn = false;
    m_bForceGettingOut = false;
    m_ucLeavingDoor = 0xFF;
    m_bDucked = false;
    m_bWearingGoggles = false;
    m_bVisible = true;
    m_bUsesCollision = true;
    m_fHealth = 100.0f;
    m_armor = 0.0f;
    m_bDead = false;
    m_bWorldIgnored = false;
    m_fCurrentRotation = 0.0f;
    m_fMoveSpeed = 0.0f;
    m_bCanBeKnockedOffBike = true;
    m_bBleeding = false;
    RemoveAllWeapons();  // Set all our weapon values to unarmed
    m_bHasJetPack = false;
    m_FightingStyle = STYLE_GRAB_KICK;
    m_MoveAnim = MOVE_DEFAULT;
    m_ucAlpha = 255;
    m_fTargetRotation = 0.0f;
    m_bTargetAkimboUp = false;
    m_bIsChoking = false;
    m_ulLastTimeBeganAiming = 0;
    m_ulLastTimeEndedAiming = 0;
    m_ulLastTimeBeganCrouch = 0;
    m_ulLastTimeBeganStand = 0;          // Standing after crouching
    m_ulLastTimeMovedWhileCrouched = 0;  // Moved while crouching
    m_bRecreatingModel = false;
    m_pCurrentContactEntity = NULL;
    m_bSunbathing = false;
    m_bDestroyingSatchels = false;
    m_bDoingGangDriveby = false;
    m_bProcessingWeaponFireEvent = false;
    m_bDeferredGangDrivebyAbort = false;

    m_pAnimationBlock = NULL;
    m_bRequestedAnimation = false;
    m_bHeadless = false;
    m_bFrozen = false;
    m_bFrozenWaitingForGroundToLoad = false;
    m_fGroundCheckTolerance = 0.f;
    m_fObjectsAroundTolerance = 0.f;
    m_iLoadAllModelsCounter = 0;
    m_bIsOnFire = false;
    m_bIsInWater = false;
    m_LastSyncedData = new SLastSyncedPedData;
    m_bSpeechEnabled = true;
    m_bStealthAiming = false;
    m_fLighting = 0.0f;
    m_bBulletImpactData = false;
    m_ucEnteringDoor = 0xFF;
    m_ucLeavingDoor = 0xFF;

    m_ulLastVehicleInOutTime = 0;
    m_bIsGettingOutOfVehicle = false;
    m_bIsGettingIntoVehicle = false;
    m_bIsGettingJacked = false;
    m_bIsJackingVehicle = false;
    m_bNoNewVehicleTask = false;
    m_VehicleInOutID = INVALID_ELEMENT_ID;
    m_NoNewVehicleTaskReasonID = INVALID_ELEMENT_ID;
    m_pGettingJackedBy = NULL;
    m_ucVehicleInOutSeat = 0xFF;
    m_bIsSyncing = false;

    // Time based interpolation
    m_interp.pTargetOriginSource = NULL;
    m_interp.bHadOriginSource = false;
    m_interp.pos.ulFinishTime = 0;

    m_pClothes = new CClientPlayerClothes(this);

    // Our default clothes
    m_pClothes->DefaultClothes(false);

    // Movement state names for Lua
    m_MovementStateNames[MOVEMENTSTATE_STAND] = "stand";
    m_MovementStateNames[MOVEMENTSTATE_WALK] = "walk";
    m_MovementStateNames[MOVEMENTSTATE_POWERWALK] = "powerwalk";
    m_MovementStateNames[MOVEMENTSTATE_JOG] = "jog";
    m_MovementStateNames[MOVEMENTSTATE_SPRINT] = "sprint";
    m_MovementStateNames[MOVEMENTSTATE_CROUCH] = "crouch";
    m_MovementStateNames[MOVEMENTSTATE_CRAWL] = "crawl";
    m_MovementStateNames[MOVEMENTSTATE_ROLL] = "roll";
    m_MovementStateNames[MOVEMENTSTATE_JUMP] = "jump";
    m_MovementStateNames[MOVEMENTSTATE_FALL] = "fall";
    m_MovementStateNames[MOVEMENTSTATE_CLIMB] = "climb";
    m_MovementStateNames[MOVEMENTSTATE_SWIM] = "swim";
    m_MovementStateNames[MOVEMENTSTATE_WALK_TO_POINT] = "walk_to_point";
    m_MovementStateNames[MOVEMENTSTATE_ASCENT_JETPACK] = "ascent_jetpack";
    m_MovementStateNames[MOVEMENTSTATE_DESCENT_JETPACK] = "descent_jetpack";
    m_MovementStateNames[MOVEMENTSTATE_JETPACK] = "jetpack_flying";
    m_MovementStateNames[MOVEMENTSTATE_HANGING] = "hanging";

    // Create the player model
    if (m_bIsLocalPlayer)
    {
        // Init shotsync data stuff to 0
        m_remoteDataStorage = NULL;
        m_shotSyncData = g_pMultiplayer->GetLocalShotSyncData();
        m_currentControllerState = NULL;
        m_rawControllerState = CControllerState();
        m_lastControllerState = NULL;
        m_stats = NULL;

        // Init the local player
        _CreateLocalModel();

        // Init default analog control states
        CClientPad::InitAnalogControlStates();

        // Give full health, no armor, no weapons and put him at a safe location
        SetHealth(GetMaxHealth());
        SetArmor(0);
        RemoveAllWeapons();
        SetPosition(CVector(2488.562f, -1662.40f, 23.335f));
    }
    else
    {
        // Add our shotsync data
        m_remoteDataStorage = g_pMultiplayer->CreateRemoteDataStorage();
        m_remoteDataStorage->SetProcessPlayerWeapon(true);
        m_shotSyncData = m_remoteDataStorage->ShotSyncData();
        m_currentControllerState = m_remoteDataStorage->CurrentControllerState();
        m_rawControllerState = CControllerState();
        m_lastControllerState = m_remoteDataStorage->LastControllerState();
        m_stats = m_remoteDataStorage->Stats();
        // ### remember if you want to set Int flags, subtract STATS_OFFSET from the enum ID ###

        SetStat(MAX_HEALTH, 569.0f);  // Default max_health stat

        SetArmor(0.0f);
    }

    g_pClientGame->InsertPedPointerToSet(this);
    m_clientModel = pManager->GetModelManager()->FindModelByID(m_ulModel);
}

CClientPed::~CClientPed()
{
    // _DestroyModel owns the normal release. This covers streamed-out and
    // exceptional teardown paths as well, without leaving a stale residence.
    ReleaseNativeCollisionResidency("destructor");
    dassert(m_nativeCollisionResidency == 0);

    // A hack to destroy custom animation by playing a default internal animation.
    // When IFP is unloaded by leaving the server, the pointer to its animations might
    // still be somewhere in use, and a crash can occur by calling its members.
    // So we switch to internal GTA animation to avoid the crash.
    CStaticFunctionDefinitions::SetPedAnimation(*this, "ped", "idle_stance", -1, 250, true, false, false, false);

    g_pClientGame->RemovePedPointerFromSet(this);

    // Remove from the ped manager
    m_pManager->GetPedManager()->RemoveFromList(this);

    // Unreference us from stuff
    m_pManager->UnreferenceEntity(this);

    // Remove our linked contact entity
    if (m_pCurrentContactEntity)
    {
        m_pCurrentContactEntity->RemoveContact(this);
        m_pCurrentContactEntity = NULL;
    }

    // Make sure we're not requesting any model
    m_pRequester->Cancel(this, false);

    // Detach us from eventual entities
    AttachTo(NULL);

    // Remove all our projectiles
    RemoveAllProjectiles();

    // If this is the local player, give the player full health and put him at a safe location
    if (m_bIsLocalPlayer)
    {
        SetHealth(GetMaxHealth());
        SetPosition(CVector(2488.562f, -1662.40f, 23.335f));
        SetInterior(0);
        SetDimension(0);
        SetVoice("PED_TYPE_PLAYER", "VOICE_PLY_CR");
        m_pClothes->DefaultClothes(true);
        SetCanBeKnockedOffBike(true);
        SetHeadless(false);
        SetBleeding(false);
    }
    else
    {
        // Remove our shotsync data
        g_pMultiplayer->RemoveRemoteDataStorage(m_pPlayerPed);
        g_pMultiplayer->DestroyRemoteDataStorage(m_remoteDataStorage);
        m_remoteDataStorage = NULL;

        CClientVehicle* pVehicle = GetOccupiedVehicle();
        if (m_pPlayerPed && pVehicle && GetOccupiedVehicleSeat() == 0)
        {
            if (g_pClientGame->GetLocalPlayer() && g_pClientGame->GetLocalPlayer()->GetOccupiedVehicle() == pVehicle)
            {
                CVehicle* pGameVehicle = pVehicle->GetGameVehicle();
                if (pGameVehicle)
                {
                    // Driver from local player vehicle is being destroyed
                    pGameVehicle->GetVehicleAudioEntity()->JustGotOutOfVehicleAsDriver();
                }
            }
        }
    }

    // We have a player model?
    if (m_pPlayerPed)
    {
        // Do we have the in_water task? #3973: Peds destroyed in water leave water circles
        CTask* pTask = m_pTaskManager->GetTask(TASK_PRIORITY_EVENT_RESPONSE_NONTEMP);
        if (pTask && pTask->GetTaskType() == TASK_COMPLEX_IN_WATER)
        {
            // Kill the task immediately
            pTask->MakeAbortable(m_pPlayerPed, ABORT_PRIORITY_IMMEDIATE, NULL);
        }

        // Destroy the player model
        if (m_bIsLocalPlayer)
        {
            _DestroyLocalModel();
        }
        else
        {
            _DestroyModel();
        }
    }

    // Delete our clothes
    delete m_pClothes;
    m_pClothes = NULL;

    delete m_LastSyncedData;

    // Remove us from any occupied vehicle
    CClientVehicle::UnpairPedAndVehicle(this);

    // Delete delayed sync data
    list<SDelayedSyncData*>::iterator iter = m_SyncBuffer.begin();
    for (; iter != m_SyncBuffer.end(); iter++)
    {
        delete *iter;
    }

    m_SyncBuffer.clear();

    if (m_interp.pTargetOriginSource)
    {
        m_interp.pTargetOriginSource->RemoveOriginSourceUser(this);
        m_interp.pTargetOriginSource = NULL;
    }

    g_pClientGame->GetPedSync()->RemovePed(this);

    CClientEntityRefManager::RemoveEntityRefs(0, &m_pOccupiedVehicle, &m_pOccupyingVehicle, &m_pTargetedEntity, &m_pCurrentContactEntity,
                                              &m_pBulletImpactEntity, &m_interp.pTargetOriginSource, NULL);
    m_clientModel = nullptr;
}

void CClientPed::SetStat(unsigned short usStat, float fValue)
{
    if (m_bIsLocalPlayer)
    {
        if (usStat < MAX_INT_FLOAT_STATS)
            g_pGame->GetStats()->SetStatValue(usStat, fValue);
        g_pMultiplayer->SetLocalStatValue(usStat, fValue);
    }
    else
    {
        if (usStat < MAX_FLOAT_STATS)
            m_stats->StatTypesFloat[usStat] = fValue;
        else if (usStat >= STATS_OFFSET && usStat < MAX_INT_FLOAT_STATS)
            m_stats->StatTypesInt[usStat - STATS_OFFSET] = (int)fValue;
    }
}

float CClientPed::GetStat(unsigned short usStat)
{
    if (m_bIsLocalPlayer)
    {
        if (g_pGame->GetPedContext() == NULL)
        {
            return g_pGame->GetStats()->GetStatValue(usStat);
        }
        else
        {
            return g_pMultiplayer->GetLocalStatValue(usStat);
        }
    }
    else
    {
        if (usStat < MAX_FLOAT_STATS)
            return m_stats->StatTypesFloat[usStat];
        else if (usStat >= STATS_OFFSET && usStat < MAX_INT_FLOAT_STATS)
            return (float)m_stats->StatTypesInt[usStat - STATS_OFFSET];
        else
            return 0.0f;
    }
}

void CClientPed::ResetStats()
{
    // stats
    for (unsigned short us = 0; us <= NUM_PLAYER_STATS; us++)
    {
        if (us == MAX_HEALTH)
        {
            SetStat(us, 569.0f);
        }
        else
        {
            SetStat(us, 0.0f);
        }
    }
}

bool CClientPed::GetMatrix(CMatrix& Matrix) const
{
    // Are we frozen?
    if (IsFrozen())
    {
        Matrix = m_matFrozen;
    }
    else if (m_pPlayerPed)
    {
        m_pPlayerPed->GetMatrix(&Matrix);
    }
    else
    {
        Matrix = m_Matrix;
    }

    return true;
}

bool CClientPed::SetMatrix(const CMatrix& Matrix)
{
    if (m_pPlayerPed)
    {
        m_pPlayerPed->SetMatrix(const_cast<CMatrix*>(&Matrix));
    }

    // Update rotation
    CVector vecRotation = Matrix.GetRotation();
    SetCurrentRotation(vecRotation.fZ);
    if (!IS_PLAYER(this))
        SetCameraRotation(-vecRotation.fZ);

    if (m_Matrix.vPos != Matrix.vPos)
    {
        UpdateStreamPosition(Matrix.vPos);
    }
    m_Matrix = Matrix;
    m_matFrozen = Matrix;

    return true;
}

void CClientPed::GetPosition(CVector& vecPosition) const
{
    CClientVehicle* pVehicle = const_cast<CClientPed*>(this)->GetRealOccupiedVehicle();

    // Are we frozen?
    if (IsFrozen())
    {
        vecPosition = m_matFrozen.vPos;
    }
    // Streamed in?
    else if (m_pPlayerPed)
    {
        vecPosition = *m_pPlayerPed->GetPosition();
    }
    // In a vehicle?
    else if (pVehicle)
    {
        pVehicle->GetPosition(vecPosition);
    }
    // Attached to an entity?
    else if (m_pAttachedToEntity)
    {
        m_pAttachedToEntity->GetPosition(vecPosition);
        vecPosition += m_vecAttachedPosition;
    }
    // None of the above?
    else
    {
        vecPosition = m_Matrix.vPos;
    }
}

void CClientPed::SetPosition(const CVector& vecPosition, bool bResetInterpolation, bool bAllowGroundLoadFreeze)
{
    // We have a player ped?
    if (m_pPlayerPed)
    {
        // Don't set the actual position if we're in a vehicle
        if (!GetRealOccupiedVehicle())
        {
            // Is this the local player?
            if (m_bIsLocalPlayer)
            {
                // If move is big enough, do ground checks
                float DistanceMoved = (m_Matrix.vPos - vecPosition).Length();
                if (DistanceMoved > 50 && !IsFrozen() && bAllowGroundLoadFreeze)
                    SetFrozenWaitingForGroundToLoad(true);
            }

            // Set the real position
            m_pPlayerPed->SetPosition(const_cast<CVector*>(&vecPosition));
        }
    }

    // Have we moved to a different position?
    if (m_Matrix.vPos != vecPosition)
    {
        // Store our new position
        m_Matrix.vPos = vecPosition;
        m_matFrozen.vPos = vecPosition;

        // Update our streaming position
        UpdateStreamPosition(vecPosition);
    }

    if (bResetInterpolation)
        RemoveTargetPosition();
}

void CClientPed::SetInterior(unsigned char ucInterior)
{
    CEntity* pEntity = GetGameEntity();
    if (pEntity)
    {
        pEntity->SetAreaCode(ucInterior);
    }

    // If local player
    if (m_bIsLocalPlayer)
    {
        // If our camera is in the same world as the player, move it
        if (g_pGame->GetWorld()->GetCurrentArea() == m_ucInterior)
        {
            g_pGame->GetWorld()->SetCurrentArea(ucInterior);
        }
    }

    CClientEntity::SetInterior(ucInterior);
}

void CClientPed::Teleport(const CVector& vecPosition)
{
    // We have a player ped?
    if (m_pPlayerPed)
    {
        // Don't set the actual position if we're in a vehicle
        if (!GetRealOccupiedVehicle())
        {
            // Set it only if we're not in a vehicle or not working on getting in/out
            if (!m_pOccupiedVehicle || GetVehicleInOutState() != VEHICLE_INOUT_GETTING_OUT)
            {
                // Is this the local player?
                if (m_bIsLocalPlayer)
                {
                    // If move is big enough, do ground checks
                    float DistanceMoved = (m_Matrix.vPos - vecPosition).Length();
                    if (DistanceMoved > 50 && !IsFrozen())
                        SetFrozenWaitingForGroundToLoad(true);
                }

                // Player has jetpack?
                bool hasJetpack = HasJetPack();

                // Set the real position
                m_pPlayerPed->Teleport(vecPosition.fX, vecPosition.fY, vecPosition.fZ);

                // Restore jetpack
                SetHasJetPack(hasJetpack);
            }
        }
    }

    // Have we moved to a different position?
    if (m_Matrix.vPos != vecPosition)
    {
        // Store our new position
        m_Matrix.vPos = vecPosition;
        m_matFrozen.vPos = vecPosition;

        // Update our streaming position
        UpdateStreamPosition(vecPosition);
    }
}

void CClientPed::GetRotationDegrees(CVector& vecRotation) const
{
    // Grab our rotations in radians
    GetRotationRadians(vecRotation);

    // Convert it to degrees
    vecRotation.fX = vecRotation.fX * 180.0f / 3.1415926535897932384626433832795f;
    vecRotation.fY = vecRotation.fY * 180.0f / 3.1415926535897932384626433832795f;
    vecRotation.fZ = vecRotation.fZ * 180.0f / 3.1415926535897932384626433832795f;
}

void CClientPed::GetRotationRadians(CVector& vecRotation) const
{
    CMatrix matTemp;
    GetMatrix(matTemp);
    g_pMultiplayer->ConvertMatrixToEulerAngles(matTemp, vecRotation.fX, vecRotation.fY, vecRotation.fZ);
}

void CClientPed::SetRotationDegrees(const CVector& vecRotation)
{
    // Convert from degrees to radians
    CVector vecTemp;
    vecTemp.fX = vecRotation.fX * 3.1415926535897932384626433832795f / 180.0f;
    vecTemp.fY = vecRotation.fY * 3.1415926535897932384626433832795f / 180.0f;
    vecTemp.fZ = vecRotation.fZ * 3.1415926535897932384626433832795f / 180.0f;

    // Set the rotation as radians
    SetRotationRadians(vecTemp);

    // HACK: set again the z rotation to work on ground
    SetCurrentRotation(vecTemp.fZ);
    if (!IS_PLAYER(this))
        SetCameraRotation(vecTemp.fZ);  // This is incorrect and kept for backward compatibility
}

void CClientPed::SetRotationRadians(const CVector& vecRotation)
{
    // Grab the matrix, apply the rotation to it and set it again
    CMatrix matTemp;
    GetMatrix(matTemp);
    g_pMultiplayer->ConvertEulerAnglesToMatrix(matTemp, vecRotation.fX, vecRotation.fY,
                                               vecRotation.fZ);  // This is incorrect and kept for backward compatibility
    SetMatrix(matTemp);
}

//
//
// New rotation functions which fixes inv rotate when in air
// Also fixes camera rotation for peds
//
//
void CClientPed::GetRotationDegreesNew(CVector& vecRotation) const
{
    // Grab our rotations in radians and convert it to degrees
    GetRotationRadiansNew(vecRotation);
    ConvertRadiansToDegreesNoWrap(vecRotation);
}

void CClientPed::GetRotationRadiansNew(CVector& vecRotation) const
{
    CMatrix matTemp;
    GetMatrix(matTemp);
    g_pMultiplayer->ConvertMatrixToEulerAngles(matTemp, vecRotation.fX, vecRotation.fY, vecRotation.fZ);
    vecRotation.fZ = -vecRotation.fZ;
}

void CClientPed::SetRotationDegreesNew(const CVector& vecRotation)
{
    // Convert from degrees to radians and set the rotation
    CVector vecTemp = vecRotation;
    ConvertDegreesToRadiansNoWrap(vecTemp);
    SetRotationRadiansNew(vecTemp);
}

void CClientPed::SetRotationRadiansNew(const CVector& vecRotation)
{
    // Grab the matrix, apply the rotation to it and set it again

    // For in air
    CMatrix matTemp;
    GetMatrix(matTemp);
    g_pMultiplayer->ConvertEulerAnglesToMatrix(matTemp, vecRotation.fX, vecRotation.fY, -vecRotation.fZ);
    SetMatrix(matTemp);

    // For on ground
    SetCurrentRotation(vecRotation.fZ);
    if (!IS_PLAYER(this))
        SetCameraRotation(-vecRotation.fZ);
}

void CClientPed::SetCurrentRotationNew(float fRotation)
{
    SetRotationRadiansNew(CVector(0, 0, fRotation));
}

void CClientPed::Spawn(const CVector& vecPosition, float fRotation, unsigned short usModel, unsigned char ucInterior, unsigned short usLogicalModel)
{
    // Remove us from our car
    RemoveFromVehicle();
    SetVehicleInOutState(VEHICLE_INOUT_NONE);

    // Wait for ground
    if (m_bIsLocalPlayer)
    {
        SetFrozenWaitingForGroundToLoad(true);
        m_iLoadAllModelsCounter = 10;
    }

    // Remove any animation
    KillAnimation();

    // Give him the correct model
    SetModel(usModel, false, usLogicalModel);

    // Detach from any entities
    AttachTo(NULL);

    // Restore our health before any resurrection calls (::SetHealth/SetInitialState)
    // So we don't get recreated more than once
    if (m_pPlayerPed)
    {
        m_pPlayerPed->SetInitialState();
        m_fHealth = GetMaxHealth();
        m_pPlayerPed->SetHealth(m_fHealth);
        m_bUsesCollision = true;
        m_pPlayerPed->SetLanding(false);
    }
    else
    {
        // Remote ped health/armor was locked during Kill, so make sure it's unlocked
        UnlockHealth();
        UnlockArmor();
    }

    // Set some states
    SetFrozen(false);
    Teleport(vecPosition);
    SetCurrentRotationNew(fRotation);
    SetHealth(GetMaxHealth());
    RemoveAllWeapons();
    SetArmor(0);
    ResetInterpolation();
    SetHasJetPack(false);
    SetMoveSpeed(CVector());
    SetInterior(ucInterior);
    SetFootBloodEnabled(false);
    SetIsDead(false);
}

void CClientPed::ResetInterpolation()
{
    m_ulBeginRotationTime = 0;
    m_ulEndRotationTime = 0;
    m_ulBeginAimTime = 0;
    m_ulTargetAimTime = 0;

    RemoveTargetPosition();
}

float CClientPed::GetCameraRotation()
{
    // Local player
    if (m_bIsLocalPlayer)
    {
        return g_pGame->GetCamera()->GetCameraRotation();
    }
    else
    {
        // Get the keypad
        return m_remoteDataStorage->GetCameraRotation();
    }
}

void CClientPed::SetCameraRotation(float fRotation)
{
    // Local player
    if (m_bIsLocalPlayer)
    {
        CCam* pCam = g_pGame->GetCamera()->GetCam(g_pGame->GetCamera()->GetActiveCam());
        float fOldHorizontal, fOldVertical;
        pCam->GetDirection(fOldHorizontal, fOldVertical);
        pCam->SetDirection(fRotation - PI / 2, fOldVertical);
    }
    else
    {
        // Get the keypad
        m_remoteDataStorage->SetCameraRotation(fRotation);
    }
}

void CClientPed::GetMoveSpeed(CVector& vecMoveSpeed) const
{
    if (m_pPlayerPed)
    {
        m_pPlayerPed->GetMoveSpeed(&vecMoveSpeed);
    }
    else
    {
        vecMoveSpeed = m_vecMoveSpeed;
    }
}

void CClientPed::SetMoveSpeed(const CVector& vecMoveSpeed)
{
    if (m_pPlayerPed)
    {
        m_pPlayerPed->SetMoveSpeed(vecMoveSpeed);
    }
    m_vecMoveSpeed = vecMoveSpeed;
}

void CClientPed::GetTurnSpeed(CVector& vecTurnSpeed) const
{
    if (m_pPlayerPed)
    {
        m_pPlayerPed->GetTurnSpeed(&vecTurnSpeed);
    }
    else
    {
        vecTurnSpeed = m_vecTurnSpeed;
    }
}

void CClientPed::SetTurnSpeed(const CVector& vecTurnSpeed)
{
    if (m_pPlayerPed)
    {
        m_pPlayerPed->SetTurnSpeed(const_cast<CVector*>(&vecTurnSpeed));
    }
    m_vecTurnSpeed = vecTurnSpeed;
}

void CClientPed::GetControllerState(CControllerState& ControllerState)
{
    // Local player
    if (m_bIsLocalPlayer)
    {
        m_pPad->GetCurrentControllerState(&ControllerState);

        // Fix for GTA bug allowing to drive bikes and boats with engine off (3437)
        CClientVehicle* pVehicle = GetRealOccupiedVehicle();
        if (pVehicle && !pVehicle->IsEngineOn())
        {
            ControllerState.ButtonCross = 0;
        }
        // Fix for GTA bug allowing vehicles to be controlled while dead.
        if (pVehicle && IsDying())
        {
            // camera
            ControllerState.LeftStickX = 0;
            ControllerState.LeftStickY = 0;
            ControllerState.RightStickX = 0;
            ControllerState.RightStickY = 0;
            // brake
            ControllerState.ButtonSquare = 0;
            // enter/exit
            ControllerState.ButtonTriangle = 0;
            // nos/fire
            ControllerState.ButtonCircle = 0;
            // accelerate
            ControllerState.ButtonCross = 0;
            // handbrake/missiles
            ControllerState.RightShoulder1 = 0;
            if (pVehicle && pVehicle->GetVehicleType() == CLIENTVEHICLE_PLANE)
            {
                // rudders
                ControllerState.RightShoulder2 = 0;
                ControllerState.LeftShoulder2 = 0;
            }
            // Horn/sirens/police spotlight/hover
            ControllerState.ShockButtonL = 0;
            // raise/lower landing gear
            ControllerState.ShockButtonR = 0;
        }
    }
    else
    {
        ControllerState = *m_currentControllerState;
    }
}

void CClientPed::GetLastControllerState(CControllerState& ControllerState)
{
    // Local player
    if (m_bIsLocalPlayer)
    {
        m_pPad->GetLastControllerState(&ControllerState);
    }
    else
    {
        ControllerState = *m_lastControllerState;
    }
}

void CClientPed::SetControllerState(const CControllerState& ControllerState)
{
    // Local player
    if (m_bIsLocalPlayer)
    {
        // Put the current keystate in the old keystate
        CControllerState csOld;
        m_pPad->GetCurrentControllerState(&csOld);
        m_pPad->SetLastControllerState(&csOld);

        // Set the new current keystate
        m_pPad->SetCurrentControllerState(const_cast<CControllerState*>(&ControllerState));
    }
    else
    {
        // A fresh network/resource controller state supersedes the base that
        // was captured before the presentation overlay was applied.
        m_nativeTaskLocomotionPresentationApplied = false;

        // Put the current into the old keystates
        memcpy(m_lastControllerState, m_currentControllerState, sizeof(CControllerState));

        // Set the new current keystate
        memcpy(m_currentControllerState, &ControllerState, sizeof(CControllerState));
    }
}

SNativeTaskLocomotionSync CClientPed::GetNativeTaskLocomotion()
{
    SNativeTaskLocomotionSync locomotion;
    CControllerState          controllerState{};
    CVector                   velocity{};

    if (!m_pPlayerPed)
    {
        TraceNativeTaskLocomotionProducer(this, "no_game_ped", locomotion, controllerState, velocity);
        return locomotion;
    }
    if (GetRealOccupiedVehicle())
    {
        TraceNativeTaskLocomotionProducer(this, "occupied", locomotion, controllerState, velocity);
        return locomotion;
    }
    if (IsGettingIntoVehicle())
    {
        TraceNativeTaskLocomotionProducer(this, "entering_vehicle", locomotion, controllerState, velocity);
        return locomotion;
    }
    if (IsGettingOutOfVehicle())
    {
        TraceNativeTaskLocomotionProducer(this, "exiting_vehicle", locomotion, controllerState, velocity);
        return locomotion;
    }
    if (IsDucked())
    {
        TraceNativeTaskLocomotionProducer(this, "ducked", locomotion, controllerState, velocity);
        return locomotion;
    }
    if (IsDead())
    {
        TraceNativeTaskLocomotionProducer(this, "dead", locomotion, controllerState, velocity);
        return locomotion;
    }
    if (HasSyncedAnim())
    {
        TraceNativeTaskLocomotionProducer(this, "synced_anim", locomotion, controllerState, velocity);
        return locomotion;
    }

    GetControllerState(controllerState);
    if (controllerState.LeftStickX != 0 || controllerState.LeftStickY != 0)
    {
        TraceNativeTaskLocomotionProducer(this, "controller_input", locomotion, controllerState, velocity);
        return locomotion;
    }

    CTask* pTask = m_pTaskManager->GetSimplestActiveTask();
    if (!pTask)
    {
        TraceNativeTaskLocomotionProducer(this, "no_task", locomotion, controllerState, velocity);
        return locomotion;
    }
    if (pTask->GetTaskType() != TASK_SIMPLE_GO_TO_POINT && pTask->GetTaskType() != TASK_SIMPLE_GO_TO_POINT_FINE)
    {
        TraceNativeTaskLocomotionProducer(this, "unsupported_task", locomotion, controllerState, velocity);
        return locomotion;
    }

    const PedMoveState::Enum liveMoveState = m_pPlayerPed->GetMoveState();
    PedMoveState::Enum       presentationMoveState = GetNativeTaskLocomotionCommandMoveState(pTask).value_or(liveMoveState);

    // GangFollower can retain a sprint seek command while its ControlSubTask
    // deliberately drives the WALK association and marks the animation speed
    // as task-owned. Mirror the animation GTA actually renders; authoritative
    // velocity remains synchronized independently, so this changes only the
    // observer's gait and never slows the owner simulation.
    if (HasNativeGangFollowerWalkSpeedOverride(this, pTask))
        presentationMoveState = PedMoveState::PEDMOVE_WALK;

    // Script peds use CPlayerPed internally, so GTA can still reject a task's
    // sprint request because of its current surface or animation set. Mirror
    // that authoritative fallback instead of making observers sprint alone.
    if (presentationMoveState == PedMoveState::PEDMOVE_SPRINT)
    {
        switch (liveMoveState)
        {
            case PedMoveState::PEDMOVE_WALK:
            case PedMoveState::PEDMOVE_JOG:
            case PedMoveState::PEDMOVE_RUN:
                presentationMoveState = liveMoveState;
                break;
            default:
                break;
        }
    }
    switch (presentationMoveState)
    {
        case PedMoveState::PEDMOVE_WALK:
            locomotion.data.uiMode = SNativeTaskLocomotionSync::WALK;
            break;
        case PedMoveState::PEDMOVE_JOG:
        case PedMoveState::PEDMOVE_RUN:
            locomotion.data.uiMode = SNativeTaskLocomotionSync::RUN;
            break;
        case PedMoveState::PEDMOVE_SPRINT:
            locomotion.data.uiMode = SNativeTaskLocomotionSync::SPRINT;
            break;
        default:
            TraceNativeTaskLocomotionProducer(this, "unsupported_move_state", locomotion, controllerState, velocity);
            return locomotion;
    }

    // GTA divides pad magnitude by 60 and classifies exactly 1.0 as running.
    // Keep walking strictly below that boundary. Remote translation is capped
    // separately after ProcessControl, so this input remains presentation-only.
    const float inputMagnitude =
        locomotion.data.uiMode == SNativeTaskLocomotionSync::WALK ? 59.0f : (locomotion.data.uiMode == SNativeTaskLocomotionSync::RUN ? 108.0f : 128.0f);

    const auto applyInputDirection = [&locomotion, inputMagnitude](float directionX, float directionY)
    {
        const float directionLength = std::sqrt(directionX * directionX + directionY * directionY);
        if (directionLength <= 0.0001f || inputMagnitude < 0.5f)
        {
            locomotion.data.sLeftStickX = 0;
            locomotion.data.sLeftStickY = 0;
            return;
        }

        const float directionScale = inputMagnitude / directionLength;
        locomotion.data.sLeftStickX = static_cast<short>(std::lround(std::clamp(directionX * directionScale, -inputMagnitude, inputMagnitude)));
        locomotion.data.sLeftStickY = static_cast<short>(std::lround(std::clamp(directionY * directionScale, -inputMagnitude, inputMagnitude)));
    };

    GetMoveSpeed(velocity);
    if (velocity.fX * velocity.fX + velocity.fY * velocity.fY < 0.0001f)
    {
        if (m_LastSyncedData && !m_LastSyncedData->nativeTaskLocomotionResetPending &&
            m_LastSyncedData->nativeTaskLocomotion.data.uiMode == locomotion.data.uiMode)
        {
            // GO_TO_POINT is the durable semantic signal that GTA is still
            // driving locomotion. Slow walking styles and blocked path steps
            // can report almost no instantaneous velocity for longer than a
            // network tick; clearing on that sample makes observers alternate
            // between the native gait and an idle/default gait. Keep the last
            // direction until GTA replaces the task with its real idle,
            // animation, damage response, or another unsupported state.
            applyInputDirection(m_LastSyncedData->nativeTaskLocomotion.data.sLeftStickX, m_LastSyncedData->nativeTaskLocomotion.data.sLeftStickY);
            TraceNativeTaskLocomotionProducer(this, "task_intent_hold", locomotion, controllerState, velocity);
            return locomotion;
        }

        locomotion = {};
        TraceNativeTaskLocomotionProducer(this, "stationary", locomotion, controllerState, velocity);
        return locomotion;
    }

    // GTA rotates on-foot pad input by the camera orientation. Encode the
    // authoritative velocity in that same input space so remote actors can
    // strafe or backpedal without fighting their synchronized world heading.
    const float desiredHeading = atan2(-velocity.fX, velocity.fY);
    const float inputHeading = desiredHeading + GetCameraRotation();
    applyInputDirection(-sin(inputHeading), -cos(inputHeading));
    TraceNativeTaskLocomotionProducer(this, "emitted", locomotion, controllerState, velocity);
    return locomotion;
}

void CClientPed::ApplyNativeTaskLocomotion(CControllerState& controllerState, const SNativeTaskLocomotionSync& locomotion)
{
    controllerState.LeftStickX = locomotion.data.sLeftStickX;
    controllerState.LeftStickY = locomotion.data.sLeftStickY;
    controllerState.m_bPedWalk = locomotion.data.uiMode == SNativeTaskLocomotionSync::WALK ? 255 : 0;
    controllerState.ButtonCross = locomotion.data.uiMode == SNativeTaskLocomotionSync::SPRINT ? 255 : 0;
}

void CClientPed::SetNativeTaskLocomotionPresentation(const SNativeTaskLocomotionSync& locomotion, const char* source)
{
    m_nativeTaskLocomotionPresentation = locomotion;
    m_nativeTaskLocomotionPresentationReceivedAt = CClientTime::GetTime();
    TraceNativeTaskLocomotionReceive(this, locomotion, source ? source : "unknown");
}

void CClientPed::SetNativeTaskLocomotionAuthoritativeVelocity(const CVector& velocity)
{
    if (!std::isfinite(velocity.fX) || !std::isfinite(velocity.fY) || !std::isfinite(velocity.fZ))
    {
        m_nativeTaskLocomotionAuthoritativeVelocityValid = false;
        return;
    }

    m_nativeTaskLocomotionAuthoritativeVelocity = velocity;
    m_nativeTaskLocomotionAuthoritativeVelocityReceivedAt = CClientTime::GetTime();
    m_nativeTaskLocomotionAuthoritativeVelocityValid = true;
}

void CClientPed::ApplyNativeTaskLocomotionVelocityLimit()
{
    if (GetType() != CCLIENTPED || m_bIsLocalPlayer || m_bIsSyncing || !m_pPlayerPed || !m_nativeTaskLocomotionAuthoritativeVelocityValid ||
        m_nativeTaskLocomotionPresentation.data.uiMode == SNativeTaskLocomotionSync::NONE ||
        (m_nativeTaskAnimationPresentationActive && !IsNativeTaskPartialAnimation(m_nativeTaskAnimationPresentation)) || GetRealOccupiedVehicle() ||
        IsGettingIntoVehicle() || IsGettingOutOfVehicle() || IsDucked() || IsDead() || HasSyncedAnim())
    {
        return;
    }

    const unsigned long presentationLease = std::max(500UL, static_cast<unsigned long>(g_TickRateSettings.iPedSync) * 3);
    const unsigned long now = CClientTime::GetTime();
    const unsigned long sampleAge = now - m_nativeTaskLocomotionAuthoritativeVelocityReceivedAt;
    const unsigned long presentationAge = now - m_nativeTaskLocomotionPresentationReceivedAt;
    if (sampleAge > presentationLease || presentationAge > presentationLease)
        return;

    CVector localVelocity;
    m_pPlayerPed->GetMoveSpeed(&localVelocity);
    const float localSpeedSquared = localVelocity.fX * localVelocity.fX + localVelocity.fY * localVelocity.fY;
    const float authoritativeSpeedSquared = m_nativeTaskLocomotionAuthoritativeVelocity.fX * m_nativeTaskLocomotionAuthoritativeVelocity.fX +
                                            m_nativeTaskLocomotionAuthoritativeVelocity.fY * m_nativeTaskLocomotionAuthoritativeVelocity.fY;
    if (!std::isfinite(localSpeedSquared) || !std::isfinite(authoritativeSpeedSquared) || localSpeedSquared <= authoritativeSpeedSquared + 0.000001f)
        return;

    CVector limitedVelocity = localVelocity;
    if (authoritativeSpeedSquared <= 0.000001f)
    {
        limitedVelocity.fX = 0.0f;
        limitedVelocity.fY = 0.0f;
    }
    else
    {
        const float scale = std::sqrt(authoritativeSpeedSquared / localSpeedSquared);
        limitedVelocity.fX *= scale;
        limitedVelocity.fY *= scale;
    }

    // Locomotion input is presentation-only for non-syncers. Preserve local
    // collision direction and vertical physics, but never let its root motion
    // outrun the last authoritative XY speed received from the syncer.
    m_pPlayerPed->SetMoveSpeed(limitedVelocity);
    TraceNativeTaskLocomotionVelocityLimit(this, m_nativeTaskLocomotionAuthoritativeVelocity, localVelocity, limitedVelocity, sampleAge);
}

SNativeTaskWeaponPresentationSync CClientPed::GetNativeTaskWeaponPresentation()
{
    SNativeTaskWeaponPresentationSync presentation;
    if (!m_pPlayerPed || IsGettingIntoVehicle() || IsGettingOutOfVehicle() || IsDead() || HasSyncedAnim())
    {
        return presentation;
    }

    if (GetRealOccupiedVehicle())
    {
        CTask* primaryTask = m_pTaskManager ? m_pTaskManager->GetTask(TASK_PRIORITY_PRIMARY) : nullptr;
        auto*  driveByTask =
            primaryTask && primaryTask->GetTaskType() == TASK_SIMPLE_GANG_DRIVEBY ? dynamic_cast<CTaskSimpleGangDriveBy*>(primaryTask) : nullptr;
        if (!driveByTask || !driveByTask->GetPresentation(presentation.data.vecTarget, presentation.data.fAbortRange, presentation.data.ucFrequencyPercentage,
                                                          presentation.data.ucDriveByStyle, presentation.data.bSeatRHS))
        {
            return presentation;
        }

        const eWeaponType weaponType = GetCurrentWeaponType();
        if (!IsNativeTaskWeaponPresentationSupported(this, weaponType, false))
            return presentation;

        presentation.data.uiMode = SNativeTaskWeaponPresentationSync::DRIVEBY;
        presentation.data.ucWeaponType = static_cast<unsigned char>(weaponType);
        presentation.data.usBurstLength = 1;
        presentation.data.ucShootingRate = m_pPlayerPed->GetWeaponShootingRate();
        return presentation;
    }

    const eWeaponType weaponType = GetCurrentWeaponType();
    if (!IsNativeTaskWeaponPresentationSupported(this, weaponType, true))
        return presentation;

    CTaskSimpleUseGun* useGun = m_pPlayerPed->GetPedIntelligence()->GetTaskUseGun();
    if (!useGun)
        return presentation;

    const signed char command = useGun->GetCurrentCommand();
    if (command != GCOMMAND_FIRE && command != GCOMMAND_FIREBURST && !useGun->GetIsFiring())
        return presentation;

    CVector target;
    if (!useGun->GetPresentationTarget(target))
        return presentation;

    presentation.data.uiMode = SNativeTaskWeaponPresentationSync::FIRE;
    presentation.data.ucWeaponType = static_cast<unsigned char>(weaponType);
    presentation.data.usBurstLength = static_cast<unsigned short>(std::clamp<short>(useGun->GetBurstLength(), 1, 32767));
    presentation.data.ucShootingRate = m_pPlayerPed->GetWeaponShootingRate();
    presentation.data.vecTarget = target;
    return presentation;
}

void CClientPed::SetNativeTaskWeaponPresentation(const SNativeTaskWeaponPresentationSync& presentation, const char* source)
{
    const bool taskShapeChanged = presentation.data.uiMode != m_nativeTaskWeaponPresentation.data.uiMode ||
                                  presentation.data.ucWeaponType != m_nativeTaskWeaponPresentation.data.ucWeaponType ||
                                  presentation.data.usBurstLength != m_nativeTaskWeaponPresentation.data.usBurstLength ||
                                  presentation.data.ucShootingRate != m_nativeTaskWeaponPresentation.data.ucShootingRate ||
                                  presentation.data.fAbortRange != m_nativeTaskWeaponPresentation.data.fAbortRange ||
                                  presentation.data.ucFrequencyPercentage != m_nativeTaskWeaponPresentation.data.ucFrequencyPercentage ||
                                  presentation.data.ucDriveByStyle != m_nativeTaskWeaponPresentation.data.ucDriveByStyle ||
                                  presentation.data.bSeatRHS != m_nativeTaskWeaponPresentation.data.bSeatRHS;
    const bool targetChanged = presentation.data.vecTarget != m_nativeTaskWeaponPresentation.data.vecTarget;
    // Target coordinates are updated in place during the pulse. Rebuild only
    // when the task shape changes; restarting on ordinary target motion cuts a
    // continuous native burst into viewer-side aim/fire pops.
    const bool changed = taskShapeChanged || targetChanged;
    if (taskShapeChanged && m_nativeTaskWeaponPresentationActive)
        ClearNativeTaskWeaponPresentation("sample_changed");

    m_nativeTaskWeaponPresentation = presentation;
    m_nativeTaskWeaponPresentationReceivedAt = CClientTime::GetTime();
    if (presentation.data.uiMode != SNativeTaskWeaponPresentationSync::NONE)
        ClearNativeTaskAnimationPresentation("weapon_presentation");

    if (changed && IsNativeTaskLocomotionTraceEnabled() && IsMissionActor())
    {
        g_pCore->GetConsole()->Printf(
            "[native-task-weapon][receive] profile=%s pid=%u ped=%u source=%s mode=%u weapon=%u burst=%u rate=%u "
            "target=(%.3f,%.3f,%.3f) abort=%.1f frequency=%u style=%u rhs=%u",
            g_pCore->IsSecondaryClient() ? "cl2" : "primary", GetCurrentProcessId(), GetID().Value(), source ? source : "unknown", presentation.data.uiMode,
            presentation.data.ucWeaponType, presentation.data.usBurstLength, presentation.data.ucShootingRate, presentation.data.vecTarget.fX,
            presentation.data.vecTarget.fY, presentation.data.vecTarget.fZ, presentation.data.fAbortRange, presentation.data.ucFrequencyPercentage,
            presentation.data.ucDriveByStyle, presentation.data.bSeatRHS);
    }

    if (presentation.data.uiMode == SNativeTaskWeaponPresentationSync::NONE || m_bIsLocalPlayer || m_bIsSyncing || HasSyncedAnim())
        ClearNativeTaskWeaponPresentation("inactive");
}

void CClientPed::ClearNativeTaskWeaponPresentation(const char* reason)
{
    if (!HasNativeTaskWeaponPresentationState())
        return;

    if (m_pTaskManager && m_pPlayerPed)
    {
        CTask*     primaryTask = m_pTaskManager->GetTask(TASK_PRIORITY_PRIMARY);
        CTask*     attackTask = m_pTaskManager->GetTaskSecondary(TASK_SECONDARY_ATTACK);
        const bool ownsPrimaryTask = primaryTask && primaryTask->GetInterface() == m_nativeTaskWeaponPresentationPrimaryTask;
        const bool ownsAttackTask = attackTask && attackTask->GetInterface() == m_nativeTaskWeaponPresentationAttackTask;
        if (ownsAttackTask)
        {
            // GTA's task abort owns presentation cleanup: UseGun removes its
            // upper-body stance and arm IK, while GangDriveBy fades its
            // vehicle association. Merely detaching these viewer-owned tasks
            // can leave that visual state stuck after the authoritative peer
            // has stopped firing.
            attackTask->MakeAbortable(m_pPlayerPed, ABORT_PRIORITY_IMMEDIATE, nullptr);
        }
        // GunControl's abort reaches through the ped intelligence and mutates
        // whichever UseGun task is currently installed. The exact owned
        // secondary was already aborted above; only GangDriveBy has primary-
        // task animation state that still needs native cleanup here.
        if (ownsPrimaryTask && primaryTask->GetTaskType() == TASK_SIMPLE_GANG_DRIVEBY)
            primaryTask->MakeAbortable(m_pPlayerPed, ABORT_PRIORITY_IMMEDIATE, nullptr);

        if (ownsPrimaryTask)
            m_pTaskManager->RemoveTask(TASK_PRIORITY_PRIMARY);
        attackTask = m_pTaskManager->GetTaskSecondary(TASK_SECONDARY_ATTACK);
        if (attackTask && attackTask->GetInterface() == m_nativeTaskWeaponPresentationAttackTask)
            m_pTaskManager->RemoveTaskSecondary(TASK_SECONDARY_ATTACK);
    }

    if (m_nativeTaskWeaponPresentationPreviousShootingRate && m_pPlayerPed)
        m_pPlayerPed->SetWeaponShootingRate(*m_nativeTaskWeaponPresentationPreviousShootingRate);
    m_nativeTaskWeaponPresentationPreviousShootingRate.reset();

    if (m_nativeTaskWeaponPresentationActive && IsNativeTaskLocomotionTraceEnabled() && IsMissionActor())
    {
        g_pCore->GetConsole()->Printf("[native-task-weapon][clear] profile=%s pid=%u ped=%u reason=%s visualShots=%u",
                                      g_pCore->IsSecondaryClient() ? "cl2" : "primary", GetCurrentProcessId(), GetID().Value(), reason ? reason : "unknown",
                                      m_nativeTaskWeaponPresentationFireCount);
    }

    m_nativeTaskWeaponPresentationActive = false;
    m_nativeTaskWeaponPresentationFireCount = 0;
    m_nativeTaskWeaponPresentationPrimaryTask = nullptr;
    m_nativeTaskWeaponPresentationAttackTask = nullptr;
    m_nativeTaskWeaponPresentationPreviousAttackTask = nullptr;
    m_nativeTaskWeaponPresentation = {};
    m_nativeTaskWeaponPresentationAppliedTarget = {};
}

bool CClientPed::HasNativeTaskWeaponPresentationState() const noexcept
{
    return m_nativeTaskWeaponPresentationActive || m_nativeTaskWeaponPresentation.data.uiMode != SNativeTaskWeaponPresentationSync::NONE ||
           m_nativeTaskWeaponPresentationFireCount != 0 || m_nativeTaskWeaponPresentationPrimaryTask || m_nativeTaskWeaponPresentationAttackTask ||
           m_nativeTaskWeaponPresentationPreviousAttackTask || m_nativeTaskWeaponPresentationPreviousShootingRate.has_value();
}

bool CClientPed::PresentNativeTaskWeaponShot()
{
    if (!m_nativeTaskWeaponPresentationActive || !m_pPlayerPed || !m_pTaskManager)
        return false;

    CTask* primaryTask = m_pTaskManager->GetTask(TASK_PRIORITY_PRIMARY);
    if (!primaryTask || primaryTask->GetInterface() != m_nativeTaskWeaponPresentationPrimaryTask)
        return false;

    const eWeaponType weaponType = GetCurrentWeaponType();
    const eWeaponType presentedWeapon = static_cast<eWeaponType>(m_nativeTaskWeaponPresentation.data.ucWeaponType);
    CWeaponInfo*      weaponInfo = GetNativeTaskWeaponPresentationInfo(this, weaponType);
    if (m_nativeTaskWeaponPresentation.data.uiMode == SNativeTaskWeaponPresentationSync::FIRE && presentedWeapon == WEAPONTYPE_SPRAYCAN &&
        weaponType == presentedWeapon && weaponInfo && weaponInfo->GetFireType() == FIRETYPE_AREA_EFFECT)
    {
        // Spray presentation deliberately keeps GTA's native area FX. Damage
        // and tag progress are rejected by the presentation guards downstream.
        return false;
    }

    // This exact viewer-owned task must fail closed. If a live weapon property
    // or equipped weapon changes before the next presentation pulse, cancel
    // GTA's real fire even though there is no safe visual shot to emit.
    if (weaponType != presentedWeapon || !weaponInfo || weaponInfo->GetFireType() != FIRETYPE_INSTANT_HIT)
        return true;

    CVector target = m_nativeTaskWeaponPresentation.data.vecTarget;
    if (!std::isfinite(target.fX) || !std::isfinite(target.fY) || !std::isfinite(target.fZ))
        return true;

    CVector muzzle = *weaponInfo->GetFireOffset();
    bool    leftHand = false;
    if (CTaskSimpleUseGun* useGun = m_pPlayerPed->GetPedIntelligence()->GetTaskUseGun())
        leftHand = useGun->IsPresentationFiringLeftHand();
    GetTransformedBonePosition(leftHand ? BONE_LEFTWRIST : BONE_RIGHTWRIST, muzzle);

    CVector shotDirection = target - muzzle;
    if (shotDirection.LengthSquared() < 0.000001f)
    {
        CMatrix matrix;
        GetMatrix(matrix);
        shotDirection = matrix.vFront;
    }
    else
        shotDirection.Normalize();

    float shellBackOffset = 0.0f;
    float shellSize = 0.0f;
    switch (weaponType)
    {
        case WEAPONTYPE_PISTOL:
        case WEAPONTYPE_PISTOL_SILENCED:
        case WEAPONTYPE_DESERT_EAGLE:
        case WEAPONTYPE_COUNTRYRIFLE:
        case WEAPONTYPE_SNIPERRIFLE:
            shellBackOffset = 0.2f;
            shellSize = 0.25f;
            break;
        case WEAPONTYPE_SHOTGUN:
        case WEAPONTYPE_SAWNOFF_SHOTGUN:
        case WEAPONTYPE_SPAS12_SHOTGUN:
            shellBackOffset = 0.3f;
            shellSize = 0.45f;
            break;
        case WEAPONTYPE_MICRO_UZI:
        case WEAPONTYPE_MP5:
        case WEAPONTYPE_TEC9:
            shellBackOffset = 0.2f;
            shellSize = 0.3f;
            break;
        case WEAPONTYPE_AK47:
        case WEAPONTYPE_M4:
        case WEAPONTYPE_MINIGUN:
            shellBackOffset = 0.65f;
            shellSize = 0.25f;
            break;
        default:
            break;
    }

    if (shellSize > 0.0f)
    {
        g_pGame->GetPointLights()->AddLight(PLTYPE_POINTLIGHT, muzzle, CVector(), 3.0f, SColorRGBA(220, 255, 0, 0), 0, false, nullptr);
        CVector shellPosition = muzzle - shotDirection * shellBackOffset;
        g_pGame->GetFx()->TriggerGunshot(m_pPlayerPed, muzzle, shellPosition, true);
        m_pPlayerPed->DoGunFlash(250, leftHand);

        if (CWeapon* weapon = GetWeapon(GetCurrentWeaponSlot()))
        {
            CMatrix matrix;
            GetMatrix(matrix);
            weapon->AddGunshell(m_pPlayerPed, shellPosition, CVector2D(matrix.vRight.fX, matrix.vRight.fY), shellSize);
        }
    }

    m_pPlayerPed->AddWeaponAudioEvent(weaponType == WEAPONTYPE_MINIGUN ? EPedWeaponAudioEventType::FIRE_MINIGUN_AMMO : EPedWeaponAudioEventType::FIRE);
    NotifyNativeTaskWeaponPresentationFire();
    return true;
}

void CClientPed::NotifyNativeTaskWeaponPresentationFire()
{
    if (!m_nativeTaskWeaponPresentationActive)
        return;

    ++m_nativeTaskWeaponPresentationFireCount;
    if (IsNativeTaskLocomotionTraceEnabled() && IsMissionActor() &&
        (m_nativeTaskWeaponPresentationFireCount == 1 || m_nativeTaskWeaponPresentationFireCount % 10 == 0))
    {
        // Script weapon-fire events remain suppressed for presentation clones;
        // this bounded trace proves that GTA actually emitted audiovisual
        // shots without exposing them as a second gameplay action.
        g_pCore->GetConsole()->Printf("[native-task-weapon][fire] profile=%s pid=%u ped=%u mode=%u weapon=%u visualShots=%u",
                                      g_pCore->IsSecondaryClient() ? "cl2" : "primary", GetCurrentProcessId(), GetID().Value(),
                                      m_nativeTaskWeaponPresentation.data.uiMode, m_nativeTaskWeaponPresentation.data.ucWeaponType,
                                      m_nativeTaskWeaponPresentationFireCount);
    }
}

void CClientPed::UpdateNativeTaskWeaponPresentation()
{
    // Most streamed peds never receive this optional presentation. Avoid task
    // manager lookups until there is viewer-owned state that actually needs to
    // be applied, validated, or released.
    if (!HasNativeTaskWeaponPresentationState())
        return;

    const unsigned long presentationLease = std::max(500UL, static_cast<unsigned long>(g_TickRateSettings.iPedSync) * 3);
    const unsigned long sampleAge = CClientTime::GetTime() - m_nativeTaskWeaponPresentationReceivedAt;
    const bool          occupied = GetRealOccupiedVehicle() != nullptr;
    const bool          invalidVehicleState = (m_nativeTaskWeaponPresentation.data.uiMode == SNativeTaskWeaponPresentationSync::FIRE && occupied) ||
                                     (m_nativeTaskWeaponPresentation.data.uiMode == SNativeTaskWeaponPresentationSync::DRIVEBY && !occupied);
    if (m_bIsLocalPlayer || m_bIsSyncing || HasSyncedAnim() || IsDead() || invalidVehicleState ||
        m_nativeTaskWeaponPresentation.data.uiMode == SNativeTaskWeaponPresentationSync::NONE || sampleAge > presentationLease)
    {
        ClearNativeTaskWeaponPresentation(sampleAge > presentationLease ? "lease_expired" : "invalid_state");
        return;
    }

    const eWeaponType presentedWeapon = static_cast<eWeaponType>(m_nativeTaskWeaponPresentation.data.ucWeaponType);
    const bool        allowSpray = m_nativeTaskWeaponPresentation.data.uiMode == SNativeTaskWeaponPresentationSync::FIRE;
    if (presentedWeapon != GetCurrentWeaponType() || !IsNativeTaskWeaponPresentationSupported(this, presentedWeapon, allowSpray))
    {
        ClearNativeTaskWeaponPresentation("unsupported_weapon");
        return;
    }

    const CVector& target = m_nativeTaskWeaponPresentation.data.vecTarget;
    if (m_nativeTaskWeaponPresentation.data.usBurstLength < 1 || m_nativeTaskWeaponPresentation.data.usBurstLength > 32767 || !std::isfinite(target.fX) ||
        !std::isfinite(target.fY) || !std::isfinite(target.fZ))
    {
        ClearNativeTaskWeaponPresentation("invalid_payload");
        return;
    }

    if (m_nativeTaskWeaponPresentation.data.uiMode == SNativeTaskWeaponPresentationSync::DRIVEBY &&
        (!std::isfinite(m_nativeTaskWeaponPresentation.data.fAbortRange) || m_nativeTaskWeaponPresentation.data.fAbortRange < 0.0f ||
         m_nativeTaskWeaponPresentation.data.fAbortRange > 100000.0f || m_nativeTaskWeaponPresentation.data.ucFrequencyPercentage > 100 ||
         m_nativeTaskWeaponPresentation.data.ucDriveByStyle > SNativeTaskWeaponPresentationSync::MAX_DRIVEBY_STYLE))
    {
        ClearNativeTaskWeaponPresentation("invalid_payload");
        return;
    }

    if (m_nativeTaskWeaponPresentationActive)
    {
        CTask* primaryTask = m_pTaskManager ? m_pTaskManager->GetTask(TASK_PRIORITY_PRIMARY) : nullptr;
        if (!primaryTask || primaryTask->GetInterface() != m_nativeTaskWeaponPresentationPrimaryTask)
        {
            ClearNativeTaskWeaponPresentation("task_lost");
            return;
        }

        if (m_nativeTaskWeaponPresentation.data.uiMode == SNativeTaskWeaponPresentationSync::DRIVEBY)
        {
            auto* driveByTask = primaryTask->GetTaskType() == TASK_SIMPLE_GANG_DRIVEBY ? dynamic_cast<CTaskSimpleGangDriveBy*>(primaryTask) : nullptr;
            if (!driveByTask)
            {
                ClearNativeTaskWeaponPresentation("task_lost");
                return;
            }
            driveByTask->SetPresentationTarget(m_nativeTaskWeaponPresentation.data.vecTarget);
        }
        else
        {
            auto* gunControlTask = primaryTask->GetTaskType() == TASK_SIMPLE_GUN_CTRL ? dynamic_cast<CTaskSimpleGunControl*>(primaryTask) : nullptr;
            if (!gunControlTask)
            {
                ClearNativeTaskWeaponPresentation("task_lost");
                return;
            }

            // GunControl owns the long-lived firing schedule while UseGun owns
            // the current aim. Update both coordinate snapshots so a moving
            // authoritative target stays aligned without restarting the burst.
            gunControlTask->SetPresentationTarget(m_nativeTaskWeaponPresentation.data.vecTarget);
            if (CTaskSimpleUseGun* useGun = m_pPlayerPed->GetPedIntelligence()->GetTaskUseGun())
            {
                useGun->SetPresentationTarget(m_nativeTaskWeaponPresentation.data.vecTarget);
                if (!m_nativeTaskWeaponPresentationAttackTask && useGun->GetInterface() != m_nativeTaskWeaponPresentationPreviousAttackTask)
                    m_nativeTaskWeaponPresentationAttackTask = useGun->GetInterface();
            }
        }
        m_nativeTaskWeaponPresentationAppliedTarget = m_nativeTaskWeaponPresentation.data.vecTarget;
        return;
    }

    const bool validFire = m_nativeTaskWeaponPresentation.data.uiMode == SNativeTaskWeaponPresentationSync::FIRE &&
                           IsNativeTaskWeaponPresentationSupported(this, presentedWeapon, true) && presentedWeapon == GetCurrentWeaponType();
    const bool validDriveBy =
        m_nativeTaskWeaponPresentation.data.uiMode == SNativeTaskWeaponPresentationSync::DRIVEBY && occupied &&
        m_nativeTaskWeaponPresentation.data.ucWeaponType == GetCurrentWeaponType() && IsNativeTaskWeaponPresentationSupported(this, presentedWeapon, false) &&
        std::isfinite(m_nativeTaskWeaponPresentation.data.fAbortRange) && m_nativeTaskWeaponPresentation.data.fAbortRange >= 0.0f &&
        m_nativeTaskWeaponPresentation.data.fAbortRange <= 100000.0f && m_nativeTaskWeaponPresentation.data.ucFrequencyPercentage <= 100 &&
        m_nativeTaskWeaponPresentation.data.ucDriveByStyle <= SNativeTaskWeaponPresentationSync::MAX_DRIVEBY_STYLE;
    if ((!validFire && !validDriveBy) || m_nativeTaskWeaponPresentation.data.usBurstLength < 1 || m_nativeTaskWeaponPresentation.data.usBurstLength > 32767 ||
        !std::isfinite(target.fX) || !std::isfinite(target.fY) || !std::isfinite(target.fZ) || !m_pPlayerPed)
    {
        ClearNativeTaskWeaponPresentation("unsupported_weapon");
        return;
    }

    m_nativeTaskWeaponPresentationPreviousShootingRate = m_pPlayerPed->GetWeaponShootingRate();
    m_pPlayerPed->SetWeaponShootingRate(m_nativeTaskWeaponPresentation.data.ucShootingRate);
    CTask* task = nullptr;
    if (validDriveBy)
    {
        auto* driveByTask = g_pGame->GetTasks()->CreateTaskSimpleGangDriveBy(
            nullptr, &target, m_nativeTaskWeaponPresentation.data.fAbortRange, static_cast<char>(m_nativeTaskWeaponPresentation.data.ucFrequencyPercentage),
            static_cast<char>(m_nativeTaskWeaponPresentation.data.ucDriveByStyle), m_nativeTaskWeaponPresentation.data.bSeatRHS);
        if (driveByTask)
            driveByTask->SetFromScriptCommand(true);
        task = driveByTask;
    }
    else
    {
        task = g_pGame->GetTasks()->CreateTaskSimpleGunControl(nullptr, &target, nullptr, static_cast<char>(GCOMMAND_FIREBURST),
                                                               static_cast<short>(m_nativeTaskWeaponPresentation.data.usBurstLength), 60000);
    }
    if (!task)
    {
        ClearNativeTaskWeaponPresentation("task_refused");
        return;
    }

    CTask* previousAttackTask = m_pTaskManager->GetTaskSecondary(TASK_SECONDARY_ATTACK);
    m_nativeTaskWeaponPresentationPreviousAttackTask = previousAttackTask ? previousAttackTask->GetInterface() : nullptr;
    m_nativeTaskWeaponPresentationPrimaryTask = task->GetInterface();
    task->SetAsPedTask(m_pPlayerPed, TASK_PRIORITY_PRIMARY, true);

    CTask* installedTask = m_pTaskManager->GetTask(TASK_PRIORITY_PRIMARY);
    if (!installedTask || installedTask->GetInterface() != m_nativeTaskWeaponPresentationPrimaryTask)
    {
        m_nativeTaskWeaponPresentationPrimaryTask = nullptr;
        ClearNativeTaskWeaponPresentation("task_refused");
        return;
    }

    m_nativeTaskWeaponPresentationAppliedTarget = target;
    m_nativeTaskWeaponPresentationActive = true;
    if (IsNativeTaskLocomotionTraceEnabled() && IsMissionActor())
    {
        g_pCore->GetConsole()->Printf("[native-task-weapon][apply] profile=%s pid=%u ped=%u mode=%u weapon=%u burst=%u rate=%u target=(%.3f,%.3f,%.3f)",
                                      g_pCore->IsSecondaryClient() ? "cl2" : "primary", GetCurrentProcessId(), GetID().Value(),
                                      m_nativeTaskWeaponPresentation.data.uiMode, m_nativeTaskWeaponPresentation.data.ucWeaponType,
                                      m_nativeTaskWeaponPresentation.data.usBurstLength, m_nativeTaskWeaponPresentation.data.ucShootingRate, target.fX,
                                      target.fY, target.fZ);
    }
}

SNativeTaskAnimationPresentationResult CClientPed::GetNativeTaskAnimationPresentation()
{
    SNativeTaskAnimationPresentationResult result;
    SNativeTaskAnimationPresentationSync&  presentation = result.presentation;
    if (!m_pPlayerPed)
    {
        TraceNativeTaskAnimationProducer(this, "no_game_ped", "none", nullptr);
        return result;
    }
    if (GetRealOccupiedVehicle())
    {
        TraceNativeTaskAnimationProducer(this, "occupied", "none", nullptr);
        return result;
    }
    if (IsGettingIntoVehicle())
    {
        TraceNativeTaskAnimationProducer(this, "entering_vehicle", "none", nullptr);
        return result;
    }
    if (IsGettingOutOfVehicle())
    {
        TraceNativeTaskAnimationProducer(this, "exiting_vehicle", "none", nullptr);
        return result;
    }
    if (IsDead())
    {
        TraceNativeTaskAnimationProducer(this, "dead", "none", nullptr);
        return result;
    }
    if (HasSyncedAnim())
    {
        TraceNativeTaskAnimationProducer(this, "synced_animation", "none", nullptr);
        return result;
    }

    CTask*          presentationTask = nullptr;
    const char*     selection = "none";
    bool            physicalPresentation = false;
    bool            spatialBurstPresentation = false;
    bool            airbornePresentation = false;
    bool            climbingPresentation = false;
    bool            climbStateReady = false;
    SClimbTaskState climbState;
    const auto      selectSpatialTransientTask = [&](CTask* task)
    {
        if (!task)
            return false;

        switch (task->GetTaskType())
        {
            case TASK_SIMPLE_JUMP:
                presentationTask = task;
                physicalPresentation = true;
                spatialBurstPresentation = true;
                selection = "jump_launch";
                return true;
            case TASK_SIMPLE_IN_AIR:
                presentationTask = task;
                physicalPresentation = true;
                spatialBurstPresentation = true;
                airbornePresentation = true;
                selection = "in_air";
                return true;
            case TASK_SIMPLE_LAND:
                presentationTask = task;
                physicalPresentation = true;
                spatialBurstPresentation = true;
                selection = "land";
                return true;
            case TASK_SIMPLE_HIT_HEAD:
                presentationTask = task;
                physicalPresentation = true;
                spatialBurstPresentation = true;
                selection = "hit_head";
                return true;
            case TASK_SIMPLE_CLIMB:
            {
                auto* climbTask = dynamic_cast<CTaskSimpleClimb*>(task);
                if (!climbTask)
                    return false;

                presentationTask = task;
                physicalPresentation = true;
                spatialBurstPresentation = true;
                climbingPresentation = true;
                climbStateReady = climbTask->GetClimbTaskState(climbState);
                selection = "climb";
                return true;
            }
            case TASK_SIMPLE_EVASIVE_STEP:
                presentationTask = task;
                selection = "evasive_step";
                spatialBurstPresentation = true;
                return true;
            case TASK_SIMPLE_EVASIVE_DIVE:
                presentationTask = task;
                physicalPresentation = true;
                spatialBurstPresentation = true;
                selection = "evasive_dive";
                return true;
            case TASK_SIMPLE_FALL:
                presentationTask = task;
                physicalPresentation = true;
                spatialBurstPresentation = true;
                selection = "fall";
                return true;
            case TASK_SIMPLE_GET_UP:
                presentationTask = task;
                physicalPresentation = true;
                spatialBurstPresentation = true;
                selection = "get_up";
                return true;
            case TASK_SIMPLE_SHAKE_FIST:
                presentationTask = task;
                selection = "shake_fist";
                return true;
            default:
                return false;
        }
    };
    if (m_pTaskManager)
    {
        CTask* physicalTask = m_pTaskManager->GetSimplestTask(TASK_PRIORITY_PHYSICAL_RESPONSE);
        if (physicalTask && (physicalTask->GetTaskType() == TASK_SIMPLE_FALL || physicalTask->GetTaskType() == TASK_SIMPLE_GET_UP))
        {
            // The physical-response slot must win over a concurrent fight or
            // ambient pose. Only its selected animation is mirrored; health,
            // collision and event authority remain on the syncer.
            presentationTask = physicalTask;
            physicalPresentation = true;
            selection = physicalTask->GetTaskType() == TASK_SIMPLE_FALL ? "fall" : "get_up";
        }
    }
    if (!presentationTask && m_pTaskManager)
    {
        // Vehicle-danger responses normally live in one of the event slots,
        // and HIT_PED_WITH_CAR nests FALL/GET_UP there instead of in GTA's
        // physical-response slot. Inspect those slots before unrelated fight
        // or primary tasks so the owner publishes the animation that is
        // actually controlling the ped.
        for (const int priority : {TASK_PRIORITY_EVENT_RESPONSE_TEMP, TASK_PRIORITY_EVENT_RESPONSE_NONTEMP})
        {
            CTask* eventTask = m_pTaskManager->GetSimplestTask(priority);
            if (selectSpatialTransientTask(eventTask))
                break;
        }
    }
    if (!presentationTask && m_pTaskManager)
    {
        // A script-command jump occupies the ordinary primary slot. Select
        // that physical chain before a secondary fight task so combat pose
        // presentation can never hide the ped's airborne semantic or burst.
        selectSpatialTransientTask(m_pTaskManager->GetSimplestTask(TASK_PRIORITY_PRIMARY));
    }
    if (!presentationTask)
    {
        presentationTask = m_pPlayerPed->GetPedIntelligence()->GetFightTask();
        if (presentationTask)
            selection = "fight";
    }
    if (!presentationTask && m_pTaskManager && m_pPlayerPed->IsNativeAmbientGroupActive())
    {
        CTask* partialTask = GetDeepestNativeSubTask(m_pTaskManager->GetTaskSecondary(TASK_SECONDARY_PARTIAL_ANIM));
        if (partialTask && partialTask->GetTaskType() == TASK_SIMPLE_HANDSIGNAL_ANIM)
        {
            // The syncer remains the only client running the complex group
            // task. Observers receive just the live body association selected
            // by GTA; hand objects and social decisions remain owner-only.
            presentationTask = partialTask;
            selection = "group_hand_signal";
        }
        else if (IsLiveGangTalkAnimation(partialTask))
        {
            // TASK_SIMPLE_ANIM is also used by props and pass-object flows.
            // Restrict this checkpoint to GTA's eight native gang-talk
            // partials so unrelated secondary gameplay is never mirrored.
            presentationTask = partialTask;
            selection = "group_gang_talk";
        }
    }
    if (!presentationTask && m_pTaskManager)
    {
        CTask* simplestTask = m_pTaskManager->GetSimplestActiveTask();
        if (!selectSpatialTransientTask(simplestTask) && simplestTask &&
            (simplestTask->GetTaskType() == TASK_SIMPLE_CHAT || simplestTask->GetTaskType() == TASK_SIMPLE_NAMED_ANIM ||
             simplestTask->GetTaskType() == TASK_SIMPLE_COWER || simplestTask->GetTaskType() == TASK_SIMPLE_HANDS_UP ||
             simplestTask->GetTaskType() == TASK_SIMPLE_DUCK))
        {
            presentationTask = simplestTask;
            switch (simplestTask->GetTaskType())
            {
                case TASK_SIMPLE_NAMED_ANIM:
                    selection = "named";
                    break;
                case TASK_SIMPLE_CHAT:
                    selection = "chat";
                    break;
                case TASK_SIMPLE_COWER:
                    selection = "cower";
                    break;
                case TASK_SIMPLE_HANDS_UP:
                    selection = "hands_up";
                    break;
                default:
                    selection = "duck";
                    break;
            }
        }
    }

    if (!presentationTask)
    {
        // EVASIVE_DIVE deliberately inserts a native pause between the dive
        // and GET_UP. Keep the terminal frame and the spatial burst alive
        // across that gap instead of exposing the observer's upright base
        // pose. FindActiveTaskByType walks the active task chain, so this does
        // not affect unrelated pauses.
        if (m_pTaskManager && m_pTaskManager->FindActiveTaskByType(TASK_COMPLEX_EVASIVE_DIVE_AND_GET_UP))
        {
            result.state = eNativeTaskAnimationPresentationState::HOLD_LAST_PHYSICAL_FRAME;
            result.spatialBurst = true;
            TraceNativeTaskAnimationProducer(this, "hold_evasive_dive", "evasive_dive_pause", nullptr);
            return result;
        }
        TraceNativeTaskAnimationProducer(this, "no_supported_task", selection, nullptr);
        return result;
    }

    SNamedAnimPresentationDiagnostic        animationDiagnostic;
    const SNamedAnimPresentationDiagnostic* pAnimationDiagnostic = nullptr;
    if ((IsMissionActor() || IsUsingNativeWalkingStyle()) && IsNativeTaskLocomotionTraceEnabled())
    {
        if (auto* pSimpleAnimTask = dynamic_cast<CTaskSimpleAnim*>(presentationTask))
        {
            pSimpleAnimTask->GetPresentationDiagnostic(animationDiagnostic);
            pAnimationDiagnostic = &animationDiagnostic;
        }
        else if (presentationTask->GetTaskType() == TASK_SIMPLE_NAMED_ANIM || presentationTask->GetTaskType() == TASK_SIMPLE_ANIM ||
                 presentationTask->GetTaskType() == TASK_SIMPLE_HANDSIGNAL_ANIM)
            TraceNativeTaskAnimationProducer(this, "simple_anim_task_cast_failed", selection, presentationTask);
    }

    if (!presentationTask || (climbingPresentation && !climbStateReady) ||
        !presentationTask->GetPresentationAnimation(presentation.data.usAnimGroup, presentation.data.usAnimId, presentation.data.fProgress,
                                                    presentation.data.fSpeed, presentation.data.fBlendAmount))
    {
        if (physicalPresentation)
            result.state = eNativeTaskAnimationPresentationState::HOLD_LAST_PHYSICAL_FRAME;
        result.airborne = airbornePresentation;
        result.climbing = climbingPresentation;
        result.spatialBurst = spatialBurstPresentation || physicalPresentation;
        TraceNativeTaskAnimationProducer(this, physicalPresentation ? "hold_last_physical_frame" : "task_rejected", selection, presentationTask,
                                         pAnimationDiagnostic);
        return result;
    }

    presentation.data.uiMode = SNativeTaskAnimationPresentationSync::ANIMATION;
    CVector rotation;
    // Presentation headings use the ordinary ped convention, which is the
    // inverse of the legacy matrix Euler Z returned by GetRotationRadians.
    // Sample through the corrected API so viewers reproduce visible facing.
    GetRotationRadiansNew(rotation);
    presentation.data.fHeading = rotation.fZ;
    if (climbingPresentation)
    {
        presentation.data.vecClimbHandhold = climbState.handhold;
        presentation.data.vecClimbWorldHandhold = climbState.worldHandhold;
        presentation.data.vecClimbAnchorPosition = climbState.anchorPosition;
        presentation.data.fClimbHeading = climbState.handholdHeading;
        presentation.data.usClimbAnchorModel = climbState.anchorModel;
        presentation.data.ucClimbAnchorType = climbState.anchorType;
        presentation.data.ucClimbSurfaceType = climbState.surfaceType;
        presentation.data.ucClimbAnimationPhase = static_cast<unsigned char>(climbState.animationPhase);
        presentation.data.ucClimbPositionPhase = static_cast<unsigned char>(climbState.positionPhase);
        presentation.data.usClimbGetToPositionCounter = climbState.getToPositionCounter;
        presentation.data.bForceClimb = climbState.forceClimb;
        presentation.data.bInvalidClimb = climbState.invalidClimb;
        presentation.data.bClimbChangePosition = climbState.changePosition;
        presentation.data.bClimbAnimationPlaying = climbState.animationPlaying;
    }
    result.state = eNativeTaskAnimationPresentationState::READY;
    result.physical = physicalPresentation;
    result.airborne = airbornePresentation;
    result.climbing = climbingPresentation;
    result.spatialBurst = spatialBurstPresentation || physicalPresentation;
    TraceNativeTaskAnimationProducer(this, "emitted", selection, presentationTask, pAnimationDiagnostic);
    return result;
}

void CClientPed::SetNativeTaskAnimationPresentation(const SNativeTaskAnimationPresentationSync& presentation, const char* source)
{
    const unsigned int previousMode = m_nativeTaskAnimationPresentation.data.uiMode;
    const bool         leavingClimb = previousMode == SNativeTaskAnimationPresentationSync::CLIMB_ANIMATION &&
                              presentation.data.uiMode != SNativeTaskAnimationPresentationSync::CLIMB_ANIMATION;
    const bool shapeChanged = presentation.data.uiMode != m_nativeTaskAnimationPresentation.data.uiMode ||
                              presentation.data.usAnimGroup != m_nativeTaskAnimationPresentation.data.usAnimGroup ||
                              presentation.data.usAnimId != m_nativeTaskAnimationPresentation.data.usAnimId;
    // Native tasks can loop or restart while reusing the same animation
    // association. Rewinding that association in place preserves both cases;
    // fading and recreating it at every progress wrap causes a visible pop.
    const bool changed = shapeChanged || presentation.data.fProgress != m_nativeTaskAnimationPresentation.data.fProgress ||
                         presentation.data.fSpeed != m_nativeTaskAnimationPresentation.data.fSpeed ||
                         presentation.data.fBlendAmount != m_nativeTaskAnimationPresentation.data.fBlendAmount ||
                         presentation.data.fHeading != m_nativeTaskAnimationPresentation.data.fHeading;
    const bool                  partialAnimation = IsNativeTaskPartialAnimation(presentation);
    const char*                 incomingBlockName = partialAnimation ? g_pGame->GetAnimManager()->GetAnimBlockName(presentation.data.usAnimGroup) : nullptr;
    auto                        incomingBlock = incomingBlockName ? g_pGame->GetAnimManager()->GetAnimationBlock(incomingBlockName) : nullptr;
    std::unique_ptr<CAnimBlock> retainedBlock;
    if (shapeChanged && incomingBlock && m_nativeTaskAnimationPresentationBlock &&
        incomingBlock->GetIndex() == m_nativeTaskAnimationPresentationBlock->GetIndex())
    {
        // A new social clip can reuse the same IFP. Preserve the existing ref
        // through association cleanup instead of unload/request churn between
        // consecutive gang-talk or hand-signal samples.
        retainedBlock = std::move(m_nativeTaskAnimationPresentationBlock);
    }
    if (shapeChanged && m_nativeTaskAnimationPresentationActive)
        ClearNativeTaskAnimationPresentation("sample_changed");
    if (retainedBlock)
        m_nativeTaskAnimationPresentationBlock = std::move(retainedBlock);

    m_nativeTaskAnimationPresentation = presentation;
    m_nativeTaskAnimationPresentationReceivedAt = CClientTime::GetTime();
    if (partialAnimation)
    {
        if (incomingBlock && (!m_nativeTaskAnimationPresentationBlock || m_nativeTaskAnimationPresentationBlock->GetIndex() != incomingBlock->GetIndex()))
        {
            if (m_nativeTaskAnimationPresentationBlock)
                g_pGame->GetAnimManager()->RemoveAnimBlockRef(m_nativeTaskAnimationPresentationBlock->GetIndex());

            // The native hand-signal task streams its IFP only on the owner.
            // Retain the same block on observers while presentation is live;
            // loading it never constructs the task or any hand objects.
            if (incomingBlock->IsLoaded())
                incomingBlock->AddRef();
            else
                incomingBlock->Request(NON_BLOCKING);
            m_nativeTaskAnimationPresentationBlock = std::move(incomingBlock);
        }
    }
    else if (m_nativeTaskAnimationPresentationBlock)
    {
        g_pGame->GetAnimManager()->RemoveAnimBlockRef(m_nativeTaskAnimationPresentationBlock->GetIndex());
        m_nativeTaskAnimationPresentationBlock.reset();
    }
    const bool physicalAnchor = SNativeTaskAnimationPresentationSync::IsPhysicalMode(presentation.data.uiMode);
    if (!m_bIsLocalPlayer && !m_bIsSyncing && !HasSyncedAnim() && m_pPlayerPed)
    {
        if (physicalAnchor)
        {
            m_pPlayerPed->SetNativeTaskAirbornePresentationState(true, true);
            m_nativeTaskAirbornePresentationActive = true;
        }
        else if (m_nativeTaskAirbornePresentationActive && (leavingClimb || IsOnGround(true)))
        {
            // LAND can arrive while an airborne body is still fractionally
            // above the floor, so that fence normally waits for contact. A
            // completed climb can end on a ledge above terrain, however; its
            // observer fence must be released on the semantic CLIMB edge.
            m_pPlayerPed->SetNativeTaskAirbornePresentationState(false, false);
            m_nativeTaskAirbornePresentationActive = false;
        }
    }
    if (presentation.data.uiMode != SNativeTaskAnimationPresentationSync::NONE)
        ClearNativeTaskWeaponPresentation("animation_presentation");

    if (changed && IsNativeTaskLocomotionTraceEnabled() && (IsMissionActor() || IsUsingNativeWalkingStyle()))
    {
        g_pCore->GetConsole()->Printf(
            "[native-task-animation][receive] profile=%s pid=%u ped=%u source=%s mode=%u group=%u anim=%u progress=%.3f speed=%.3f blend=%.3f heading=%.3f",
            g_pCore->IsSecondaryClient() ? "cl2" : "primary", GetCurrentProcessId(), GetID().Value(), source ? source : "unknown", presentation.data.uiMode,
            presentation.data.usAnimGroup, presentation.data.usAnimId, presentation.data.fProgress, presentation.data.fSpeed, presentation.data.fBlendAmount,
            presentation.data.fHeading);
    }

    if (presentation.data.uiMode == SNativeTaskAnimationPresentationSync::NONE || m_bIsLocalPlayer || m_bIsSyncing || HasSyncedAnim())
        ClearNativeTaskAnimationPresentation("inactive");
}

void CClientPed::SetNativeTaskPhysicalTakeoverState(const SNativeTaskAnimationPresentationSync& presentation)
{
    const bool physical = SNativeTaskAnimationPresentationSync::IsPhysicalMode(presentation.data.uiMode);
    m_nativeTaskPhysicalTakeover = physical ? presentation : SNativeTaskAnimationPresentationSync{};
    m_nativeTaskPhysicalTakeoverPending = physical;
    m_nativeTaskPhysicalTakeoverStartedAt = physical ? CClientTime::GetTime() : 0;
    if (m_pPlayerPed)
        m_pPlayerPed->SetNativeTaskAirbornePresentationState(physical, false);
}

void CClientPed::ClearNativeTaskAnimationPresentation(const char* reason)
{
    const bool preserveAirborneObserverFence = m_nativeTaskAirbornePresentationActive && m_pPlayerPed && !m_bIsLocalPlayer && !m_bIsSyncing && IsStreamedIn() &&
                                               !HasSyncedAnim() && !GetRealOccupiedVehicle() && !IsDead() &&
                                               m_nativeTaskAnimationPresentation.data.uiMode != SNativeTaskAnimationPresentationSync::CLIMB_ANIMATION &&
                                               !IsOnGround(true);
    if (m_nativeTaskAnimationPresentationActive && m_pPlayerPed)
    {
        auto animation = g_pGame->GetAnimManager()->RpAnimBlendClumpGetAssociation(m_pPlayerPed->GetRpClump(), m_nativeTaskAnimationPresentationAppliedAnimId);
        if (animation && animation->GetInterface() == m_nativeTaskAnimationPresentationAppliedAssociation &&
            static_cast<unsigned short>(animation->GetAnimGroup()) == m_nativeTaskAnimationPresentationAppliedGroup)
        {
            // Presentation associations have no native task destructor to add
            // GTA's blend-auto-remove flag. Without it, a zero-blend HANDSUP
            // association survives an ownership handoff and prevents the new
            // owner's native RunTimedAnim task from ever starting.
            animation->GetInterface()->m_bBlendAutoRemove = true;
            animation->SetBlendDelta(-8.0f);
        }

        // Native tasks can leave their matrix-authored facing in place after
        // the animation ends. Commit only the last heading that was actually
        // validated and applied; never restore the stale CPed rotation or an
        // unvalidated incoming sample during cleanup.
        if (m_nativeTaskAnimationPresentationAppliedHeading.has_value())
            ApplyNativeTaskAnimationPresentationHeading(m_nativeTaskAnimationPresentationAppliedHeading.value());
    }

    if (m_nativeTaskAnimationPresentationActive && IsNativeTaskLocomotionTraceEnabled() && (IsMissionActor() || IsUsingNativeWalkingStyle()))
    {
        g_pCore->GetConsole()->Printf("[native-task-animation][clear] profile=%s pid=%u ped=%u reason=%s", g_pCore->IsSecondaryClient() ? "cl2" : "primary",
                                      GetCurrentProcessId(), GetID().Value(), reason ? reason : "unknown");
    }

    m_nativeTaskAnimationPresentationActive = false;
    m_nativeTaskAnimationPresentationAppliedGroup = 0;
    m_nativeTaskAnimationPresentationAppliedAnimId = 0;
    m_nativeTaskAnimationPresentationAppliedAssociation = nullptr;
    m_nativeTaskAnimationPresentationAppliedHeading.reset();
    m_nativeTaskAnimationPresentation = {};
    if (m_nativeTaskAnimationPresentationBlock)
    {
        g_pGame->GetAnimManager()->RemoveAnimBlockRef(m_nativeTaskAnimationPresentationBlock->GetIndex());
        m_nativeTaskAnimationPresentationBlock.reset();
    }
    if (m_nativeTaskAirbornePresentationActive && !preserveAirborneObserverFence && m_pPlayerPed)
        m_pPlayerPed->SetNativeTaskAirbornePresentationState(false, false);
    if (!preserveAirborneObserverFence)
        m_nativeTaskAirbornePresentationActive = false;
}

void CClientPed::ApplyNativeTaskAnimationPresentationHeading(float fHeading)
{
    if (!m_pPlayerPed)
        return;

    // GTA can rotate a native task's rendered matrix without updating these
    // fields. Align current, target, and the active interpolation endpoints so
    // the later Interpolate() pulse has zero rotational delta while camera
    // interpolation remains intact.
    SetCurrentRotation(fHeading);
    m_fBeginRotation = fHeading;
    m_fTargetRotationA = fHeading;

    CMatrix matrix;
    GetMatrix(matrix);

    CVector matrixRotation;
    g_pMultiplayer->ConvertMatrixToEulerAngles(matrix, matrixRotation.fX, matrixRotation.fY, matrixRotation.fZ);
    // Ped headings use the inverse of the legacy matrix Euler Z convention.
    // Write the visible matrix directly for this frame and keep frozen-ped
    // caches consistent with the committed current/target rotation.
    g_pMultiplayer->ConvertEulerAnglesToMatrix(matrix, matrixRotation.fX, matrixRotation.fY, -fHeading);
    m_pPlayerPed->SetMatrix(&matrix);
    m_Matrix = matrix;
    m_matFrozen = matrix;
    m_nativeTaskAnimationPresentationAppliedHeading = fHeading;
}

void CClientPed::UpdateNativeTaskAnimationPresentation()
{
    if (m_nativeTaskAirbornePresentationActive && m_pPlayerPed && !m_bIsSyncing &&
        !SNativeTaskAnimationPresentationSync::IsPhysicalMode(m_nativeTaskAnimationPresentation.data.uiMode) && IsOnGround(true))
    {
        m_pPlayerPed->SetNativeTaskAirbornePresentationState(false, false);
        m_nativeTaskAirbornePresentationActive = false;
    }

    if (m_bIsSyncing && m_nativeTaskPhysicalTakeoverPending)
    {
        const unsigned long takeoverAge = CClientTime::GetTime() - m_nativeTaskPhysicalTakeoverStartedAt;
        if (m_nativeTaskPhysicalTakeover.data.uiMode == SNativeTaskAnimationPresentationSync::CLIMB_ANIMATION)
        {
            CTask* activeClimb = m_pTaskManager ? m_pTaskManager->FindActiveTaskByType(TASK_SIMPLE_CLIMB) : nullptr;
            auto*  climbTask = dynamic_cast<CTaskSimpleClimb*>(activeClimb);
            if (climbTask && climbTask->ApplyTakeoverAnimationProgress(m_nativeTaskPhysicalTakeover.data.fProgress))
            {
                // The task now owns the anchor, physical flags and remaining
                // native phase transitions. The synchronized progress is
                // applied once so normal GTA time advancement can continue.
                m_nativeTaskPhysicalTakeover = {};
                m_nativeTaskPhysicalTakeoverPending = false;
                m_nativeTaskPhysicalTakeoverStartedAt = 0;
            }
            else if (takeoverAge >= 2000)
            {
                SetNativeTaskPhysicalTakeoverState({});
            }
        }
        else if (m_pTaskManager && m_pTaskManager->FindActiveTaskByType(TASK_COMPLEX_IN_AIR_AND_LAND))
        {
            // The seeded native task has now created its active airborne
            // chain. It owns the physical flags through landing from here.
            m_nativeTaskPhysicalTakeover = {};
            m_nativeTaskPhysicalTakeoverPending = false;
            m_nativeTaskPhysicalTakeoverStartedAt = 0;
        }
        else if (takeoverAge >= 2000 || (takeoverAge >= 250 && IsOnGround(true)))
        {
            // A stale server bit or failed task seed must not suspend the ped
            // forever. Two seconds covers the complete ordinary jump arc.
            SetNativeTaskPhysicalTakeoverState({});
        }
    }

    const unsigned long presentationLease = std::max(500UL, static_cast<unsigned long>(g_TickRateSettings.iPedSync) * 3);
    const unsigned long sampleAge = CClientTime::GetTime() - m_nativeTaskAnimationPresentationReceivedAt;
    if (m_bIsLocalPlayer || m_bIsSyncing || HasSyncedAnim() || GetRealOccupiedVehicle() || IsDead() ||
        m_nativeTaskAnimationPresentation.data.uiMode == SNativeTaskAnimationPresentationSync::NONE || sampleAge > presentationLease)
    {
        ClearNativeTaskAnimationPresentation(sampleAge > presentationLease ? "lease_expired" : "invalid_state");
        return;
    }

    const bool presentationAnimationValid =
        g_pGame->GetAnimManager()->IsValidGroup(m_nativeTaskAnimationPresentation.data.usAnimGroup) &&
        g_pGame->GetAnimManager()->IsValidAnim(m_nativeTaskAnimationPresentation.data.usAnimGroup, m_nativeTaskAnimationPresentation.data.usAnimId);
    if (!SNativeTaskAnimationPresentationSync::IsAnimationMode(m_nativeTaskAnimationPresentation.data.uiMode) ||
        !std::isfinite(m_nativeTaskAnimationPresentation.data.fProgress) || m_nativeTaskAnimationPresentation.data.fProgress < 0.0f ||
        m_nativeTaskAnimationPresentation.data.fProgress > 1.0f || !std::isfinite(m_nativeTaskAnimationPresentation.data.fSpeed) ||
        m_nativeTaskAnimationPresentation.data.fSpeed <= 0.0f || m_nativeTaskAnimationPresentation.data.fSpeed > 16.0f || !m_pPlayerPed ||
        !std::isfinite(m_nativeTaskAnimationPresentation.data.fBlendAmount) || m_nativeTaskAnimationPresentation.data.fBlendAmount < 0.0f ||
        m_nativeTaskAnimationPresentation.data.fBlendAmount > 1.0f || !std::isfinite(m_nativeTaskAnimationPresentation.data.fHeading) ||
        m_nativeTaskAnimationPresentation.data.fHeading < -6.2831855f || m_nativeTaskAnimationPresentation.data.fHeading > 6.2831855f ||
        (!presentationAnimationValid && !m_nativeTaskAnimationPresentationBlock))
    {
        ClearNativeTaskAnimationPresentation("invalid_sample");
        return;
    }
    if (!presentationAnimationValid)
    {
        // A non-blocking observer-side IFP request may need a few frames.
        // Keep the validated sample until the block creates its association
        // group; the ordinary presentation lease still bounds this wait.
        return;
    }

    std::unique_ptr<CAnimBlendAssociation> animation;
    if (m_nativeTaskAnimationPresentationActive)
    {
        animation = g_pGame->GetAnimManager()->RpAnimBlendClumpGetAssociation(m_pPlayerPed->GetRpClump(), m_nativeTaskAnimationPresentationAppliedAnimId);
        if (!animation || animation->GetInterface() != m_nativeTaskAnimationPresentationAppliedAssociation ||
            static_cast<unsigned short>(animation->GetAnimGroup()) != m_nativeTaskAnimationPresentationAppliedGroup)
        {
            m_nativeTaskAnimationPresentationActive = false;
        }
    }

    if (!m_nativeTaskAnimationPresentationActive)
    {
        animation = BlendAnimation(m_nativeTaskAnimationPresentation.data.usAnimGroup, m_nativeTaskAnimationPresentation.data.usAnimId, 8.0f);
        if (!animation)
        {
            ClearNativeTaskAnimationPresentation("blend_refused");
            return;
        }

        m_nativeTaskAnimationPresentationAppliedGroup = m_nativeTaskAnimationPresentation.data.usAnimGroup;
        m_nativeTaskAnimationPresentationAppliedAnimId = m_nativeTaskAnimationPresentation.data.usAnimId;
        m_nativeTaskAnimationPresentationAppliedAssociation = animation->GetInterface();
        m_nativeTaskAnimationPresentationActive = true;
        if (IsNativeTaskLocomotionTraceEnabled() && (IsMissionActor() || IsUsingNativeWalkingStyle()))
        {
            g_pCore->GetConsole()->Printf("[native-task-animation][apply] profile=%s pid=%u ped=%u group=%u anim=%u",
                                          g_pCore->IsSecondaryClient() ? "cl2" : "primary", GetCurrentProcessId(), GetID().Value(),
                                          m_nativeTaskAnimationPresentation.data.usAnimGroup, m_nativeTaskAnimationPresentation.data.usAnimId);
        }
    }

    const float animationLength = animation->GetLength();
    const float compensatedProgress = animationLength > 0.0f
                                          ? m_nativeTaskAnimationPresentation.data.fProgress +
                                                static_cast<float>(sampleAge) / 1000.0f * m_nativeTaskAnimationPresentation.data.fSpeed / animationLength
                                          : m_nativeTaskAnimationPresentation.data.fProgress;
    // Fight idle and shuffle associations loop. Clamping their compensated
    // progress at 1 freezes the observer until the next snapshot wraps back
    // near zero, producing the visible stop/teleport cadence. Preserve the
    // native wrap for looped clips and clamp only one-shot strikes.
    const float safeCompensatedProgress = std::isfinite(compensatedProgress) ? compensatedProgress : m_nativeTaskAnimationPresentation.data.fProgress;
    const float playbackProgress = animation->IsLooped() ? std::fmod(safeCompensatedProgress, 1.0f) : std::clamp(safeCompensatedProgress, 0.0f, 1.0f);
    animation->SetCurrentProgress(playbackProgress);
    animation->SetCurrentSpeed(m_nativeTaskAnimationPresentation.data.fSpeed);
    animation->SetBlendAmount(m_nativeTaskAnimationPresentation.data.fBlendAmount);
    // Some native tasks, notably PartnerChat, rotate the rendered matrix
    // without updating CPed's ordinary current-rotation field. Mirror that
    // visible facing and retain its final validated value through cleanup.
    ApplyNativeTaskAnimationPresentationHeading(m_nativeTaskAnimationPresentation.data.fHeading);
}

void CClientPed::RemoveNativeTaskLocomotionPresentation(CControllerState& controllerState)
{
    if (!m_nativeTaskLocomotionPresentationApplied)
        return;

    controllerState.LeftStickX = m_nativeTaskLocomotionBaseControllerState.LeftStickX;
    controllerState.LeftStickY = m_nativeTaskLocomotionBaseControllerState.LeftStickY;
    controllerState.m_bPedWalk = m_nativeTaskLocomotionBaseControllerState.m_bPedWalk;
    controllerState.ButtonCross = m_nativeTaskLocomotionBaseControllerState.ButtonCross;
    m_nativeTaskLocomotionPresentationApplied = false;
}

void CClientPed::ApplyNativeTaskOwnerLocomotionAssist(CControllerState& controllerState)
{
    if (!m_bIsSyncing || !m_pPlayerPed || !m_pTaskManager || !m_pPlayerPed->IsNativeAmbientWanderEventProfileActive() || GetRealOccupiedVehicle() ||
        IsGettingIntoVehicle() || IsGettingOutOfVehicle() || IsDucked() || IsDead() || HasSyncedAnim() || controllerState.LeftStickX != 0 ||
        controllerState.LeftStickY != 0)
    {
        return;
    }

    CTask* pTask = m_pTaskManager->GetSimplestActiveTask();
    if (!pTask || pTask->GetTaskType() != TASK_SIMPLE_GO_TO_POINT)
        return;

    // GangFollower owns both its catch-up and formation-speed transitions.
    // Its durable seek command remains SPRINT while it regulates a nearby
    // member down to walking speed; layering CPlayerPed pad sprint over that
    // controller causes a persistent overshoot/stop loop. Other native sprint
    // tasks, including group flee reactions, still receive the wrapper assist.
    if (HasGangFollowerAncestor(pTask))
        return;

    // GTA's non-player go-to branch can sprint directly. MTA script peds are
    // CPlayerPed wrappers, so their equivalent branch requires pad sprint
    // input. Supply it only when another native task owns that intent.
    if (GetNativeTaskLocomotionCommandMoveState(pTask) == PedMoveState::PEDMOVE_SPRINT)
        controllerState.ButtonCross = 255;
}

void CClientPed::ApplyNativeTaskLocomotionPresentation(CControllerState& controllerState)
{
    // Ped and player sync intervals are server-configurable up to four
    // seconds. Keep the presentation through two missed unreliable updates,
    // while retaining a short floor for the default player sync rate.
    const unsigned long    syncInterval = static_cast<unsigned long>(GetType() == CCLIENTPED ? g_TickRateSettings.iPedSync : g_TickRateSettings.iPureSync);
    const unsigned long    presentationLease = std::max(500UL, syncInterval * 3);
    const unsigned long    sampleAge = CClientTime::GetTime() - m_nativeTaskLocomotionPresentationReceivedAt;
    const CControllerState baseState = controllerState;

    if (m_bIsLocalPlayer)
    {
        TraceNativeTaskLocomotionApply(this, "local_player", m_nativeTaskLocomotionPresentation, sampleAge, presentationLease, baseState, controllerState);
        return;
    }
    if (m_bIsSyncing)
    {
        TraceNativeTaskLocomotionApply(this, "syncing", m_nativeTaskLocomotionPresentation, sampleAge, presentationLease, baseState, controllerState);
        return;
    }
    if (m_nativeTaskLocomotionPresentation.data.uiMode == SNativeTaskLocomotionSync::NONE)
    {
        TraceNativeTaskLocomotionApply(this, "no_sample", m_nativeTaskLocomotionPresentation, sampleAge, presentationLease, baseState, controllerState);
        return;
    }

    // Never let a delayed on-foot presentation sample leak into a different
    // gameplay state. Clearing it here also prevents a quick transition back
    // to on foot from reviving the stale input for the remainder of its lease.
    const char* invalidState = nullptr;
    if (GetRealOccupiedVehicle())
        invalidState = "occupied";
    else if (IsGettingIntoVehicle())
        invalidState = "entering_vehicle";
    else if (IsGettingOutOfVehicle())
        invalidState = "exiting_vehicle";
    else if (IsDucked())
        invalidState = "ducked";
    else if (IsDead())
        invalidState = "dead";
    else if (HasSyncedAnim())
        invalidState = "synced_anim";
    else if (m_nativeTaskAnimationPresentationActive && !IsNativeTaskPartialAnimation(m_nativeTaskAnimationPresentation))
        invalidState = "native_task_animation";

    if (invalidState)
    {
        TraceNativeTaskLocomotionApply(this, invalidState, m_nativeTaskLocomotionPresentation, sampleAge, presentationLease, baseState, controllerState);
        m_nativeTaskLocomotionPresentation = {};
        return;
    }

    if (sampleAge > presentationLease)
    {
        TraceNativeTaskLocomotionApply(this, "lease_expired", m_nativeTaskLocomotionPresentation, sampleAge, presentationLease, baseState, controllerState);
        return;
    }

    m_nativeTaskLocomotionBaseControllerState.LeftStickX = controllerState.LeftStickX;
    m_nativeTaskLocomotionBaseControllerState.LeftStickY = controllerState.LeftStickY;
    m_nativeTaskLocomotionBaseControllerState.m_bPedWalk = controllerState.m_bPedWalk;
    m_nativeTaskLocomotionBaseControllerState.ButtonCross = controllerState.ButtonCross;
    ApplyNativeTaskLocomotion(controllerState, m_nativeTaskLocomotionPresentation);
    m_nativeTaskLocomotionPresentationApplied = true;
    TraceNativeTaskLocomotionApply(this, "applied", m_nativeTaskLocomotionPresentation, sampleAge, presentationLease, baseState, controllerState);
}

void CClientPed::AddKeysync(unsigned long ulDelay, const CControllerState& ControllerState, bool bDucking)
{
    if (!m_bIsLocalPlayer)
    {
        SDelayedSyncData* pData = new SDelayedSyncData;
        pData->ulTime = CClientTime::GetTime() + ulDelay;
        pData->ucType = DELAYEDSYNC_KEYSYNC;
        pData->State = ControllerState;
        pData->bDucking = bDucking;

        m_SyncBuffer.push_back(pData);

        if (!IsStreamedIn())
            UpdateKeysync(true);
    }
}

void CClientPed::AddChangeWeapon(unsigned long ulDelay, eWeaponSlot slot, unsigned short usWeaponAmmo)
{
    if (!m_bIsLocalPlayer)
    {
        SDelayedSyncData* pData = new SDelayedSyncData;
        pData->ulTime = CClientTime::GetTime() + ulDelay;
        pData->ucType = DELAYEDSYNC_CHANGEWEAPON;
        pData->slot = slot;
        pData->usWeaponAmmo = usWeaponAmmo;

        m_SyncBuffer.push_back(pData);

        if (!IsStreamedIn())
            UpdateKeysync(true);
    }
}

void CClientPed::AddMoveSpeed(unsigned long ulDelay, const CVector& vecMoveSpeed)
{
    if (!m_bIsLocalPlayer)
    {
        SDelayedSyncData* pData = new SDelayedSyncData;
        pData->ulTime = CClientTime::GetTime() + ulDelay;
        pData->ucType = DELAYEDSYNC_MOVESPEED;
        pData->vecTarget = vecMoveSpeed;

        m_SyncBuffer.push_back(pData);

        if (!IsStreamedIn())
            UpdateKeysync(true);
    }
}

void CClientPed::SetTargetTarget(unsigned long ulDelay, const CVector& vecSource, const CVector& vecTarget)
{
    if (!m_bIsLocalPlayer)
    {
        m_ulBeginTarget = CClientTime::GetTime();
        m_ulEndTarget = m_ulBeginTarget + ulDelay;
        m_vecBeginSource = m_shotSyncData->m_vecShotOrigin;
        m_vecBeginTarget = m_shotSyncData->m_vecShotTarget;
        m_vecTargetSource = vecSource;
        m_vecTargetTarget = vecTarget;

        // Grab the radius of the target circle
        float fRadius = DistanceBetweenPoints3D(m_vecTargetSource, m_vecTargetTarget);

        // Grab the angle of the source vector and the angle of the target vector relative to the source vector that applies
        m_vecBeginTargetAngle.fX = acos(Clamp(-1.0f, (m_vecBeginTarget.fX - m_vecBeginSource.fX) / fRadius, 1.0f));
        m_vecBeginTargetAngle.fY = acos(Clamp(-1.0f, (m_vecBeginTarget.fY - m_vecBeginSource.fY) / fRadius, 1.0f));
        m_vecBeginTargetAngle.fZ = acos(Clamp(-1.0f, (m_vecBeginTarget.fZ - m_vecBeginSource.fZ) / fRadius, 1.0f));
        m_vecTargetTargetAngle.fX = acos(Clamp(-1.0f, (m_vecTargetTarget.fX - m_vecTargetSource.fX) / fRadius, 1.0f));
        m_vecTargetTargetAngle.fY = acos(Clamp(-1.0f, (m_vecTargetTarget.fY - m_vecTargetSource.fY) / fRadius, 1.0f));
        m_vecTargetTargetAngle.fZ = acos(Clamp(-1.0f, (m_vecTargetTarget.fZ - m_vecTargetSource.fZ) / fRadius, 1.0f));

        // Grab the angle to interpolate and make sure it's below pi and above -pi (shortest path of interpolation)
        m_vecTargetInterpolateAngle = m_vecTargetTargetAngle - m_vecBeginTargetAngle;

        if (m_vecTargetInterpolateAngle.fX >= PI)
            m_vecTargetInterpolateAngle.fX -= 2 * PI;
        else if (m_vecTargetInterpolateAngle.fX <= -PI)
            m_vecTargetInterpolateAngle.fX += 2 * PI;

        if (m_vecTargetInterpolateAngle.fY >= PI)
            m_vecTargetInterpolateAngle.fY -= 2 * PI;
        else if (m_vecTargetInterpolateAngle.fY <= -PI)
            m_vecTargetInterpolateAngle.fY += 2 * PI;

        if (m_vecTargetInterpolateAngle.fZ >= PI)
            m_vecTargetInterpolateAngle.fZ -= 2 * PI;
        else if (m_vecTargetInterpolateAngle.fZ <= -PI)
            m_vecTargetInterpolateAngle.fZ += 2 * PI;
    }
}

bool CClientPed::SetModel(unsigned long ulModel, bool bTemp, unsigned short usLogicalModel)
{
    // Valid model?
    if (CClientPlayerManager::IsValidModel(ulModel))
    {
        // Different model from what we have now?
        if (m_ulModel != ulModel)
        {
            if (m_bisCurrentAnimationCustom)
            {
                m_bisNextAnimationCustom = true;
            }

            if (bTemp)
            {
                m_ulStoredModel = m_ulModel;
                m_usStoredLogicalModel = m_usLogicalModel;
            }

            // Set the model we're changing to
            m_usLogicalModel = usLogicalModel;
            m_ulModel = ulModel;
            m_pModelInfo = g_pGame->GetModelInfo(ulModel);
            UpdateSpatialData();

            // Are we loaded?
            if (m_pPlayerPed)
            {
                // Request the model
                if (m_pRequester->Request(static_cast<unsigned short>(ulModel), this))
                {
                    m_pModelInfo->MakeCustomModel();
                    // Change the model immediately if it was loaded
                    _ChangeModel();
                }
            }
        }
        else
        {
            // Logical identity can change while a slot-exhausted client keeps
            // using the same parent runtime model.
            m_usLogicalModel = usLogicalModel;
        }

        return true;
    }

    return false;
}

void CClientPed::PrepareForModelFree(unsigned short usRuntimeModel)
{
    if (!m_pPlayerPed || m_pPlayerPed->GetModelIndex() != usRuntimeModel)
        return;

    // Remote ped model changes normally recreate the native ped on the next
    // pulse. A server FREE can follow SET_ELEMENT_MODEL in the same packet
    // batch, so destroy the old native ped while its model-info slot still
    // exists. It will stream back in with the already selected parent model.
    if (!m_bIsLocalPlayer)
    {
        StreamOutForABit();
        m_shouldRecreate = false;
        return;
    }

    // Local player changes are normally immediate. This also covers the rare
    // case where the model request callback has not run before the FREE RPC.
    _ChangeModel();
}

bool CClientPed::GetCanBeKnockedOffBike()
{
    if (m_pPlayerPed)
    {
        return !m_pPlayerPed->GetCantBeKnockedOffBike();
    }
    return m_bCanBeKnockedOffBike;
}

void CClientPed::SetCanBeKnockedOffBike(bool bCanBeKnockedOffBike)
{
    if (m_pPlayerPed)
    {
        m_pPlayerPed->SetCantBeKnockedOffBike((bCanBeKnockedOffBike) ? BIKE_KNOCK_OFF_DEFAULT : BIKE_KNOCK_OFF_NEVER);
    }
    m_bCanBeKnockedOffBike = bCanBeKnockedOffBike;
}

CVector* CClientPed::GetBonePosition(eBone bone, CVector& vecPosition) const
{
    if (m_pPlayerPed)
    {
        return m_pPlayerPed->GetBonePosition(bone, &vecPosition);
    }

    return NULL;
}

CVector* CClientPed::GetTransformedBonePosition(eBone bone, CVector& vecPosition) const
{
    if (m_pPlayerPed)
    {
        return m_pPlayerPed->GetTransformedBonePosition(bone, &vecPosition);
    }

    return NULL;
}

CClientVehicle* CClientPed::GetRealOccupiedVehicle()
{
    if (m_pPlayerPed)
    {
        // Grab the player in the vehicle using the game interface
        CVehicle* pGameVehicle = m_pPlayerPed->GetVehicle();
        if (pGameVehicle)
        {
            // Return the CClientVehicle for it
            return (CClientVehicle*)pGameVehicle->GetStoredPointer();
        }
    }

    // No occupied vehicle
    return NULL;
}

CClientVehicle* CClientPed::GetClosestEnterableVehicle(bool bGetPositionFromClosestDoor, bool bCheckDriverDoor, bool bCheckPassengerDoors,
                                                       bool bCheckStreamedOutVehicles, unsigned int* uiClosestDoor, CVector* pClosestDoorPosition,
                                                       float fWithinRange, bool localVehicles)
{
    if (bGetPositionFromClosestDoor)
    {
        if (!m_pPlayerPed || (!bCheckDriverDoor && !bCheckPassengerDoors))
            return NULL;
    }

    CClientVehicle* pVehicle = NULL;
    int             iClosestDoor = 0;
    CVector         vecClosestDoorPosition;

    CVector vecPosition;
    GetPosition(vecPosition);

    float                                   fClosestDistance = 0.0f;
    CVector                                 vecVehiclePosition;
    CClientVehicle*                         pTempVehicle = NULL;
    vector<CClientVehicle*>::const_iterator iter, listEnd;
    if (bCheckStreamedOutVehicles)
    {
        iter = m_pManager->GetVehicleManager()->IterBegin();
        listEnd = m_pManager->GetVehicleManager()->IterEnd();
    }
    else
    {
        iter = m_pManager->GetVehicleManager()->StreamedBegin();
        listEnd = m_pManager->GetVehicleManager()->StreamedEnd();
    }
    for (; iter != listEnd; iter++)
    {
        pTempVehicle = *iter;

        if (pTempVehicle->IsLocalEntity() != localVehicles)
            continue;

        CVehicle* pGameVehicle = pTempVehicle->GetGameVehicle();

        if (!pGameVehicle && bGetPositionFromClosestDoor)
            continue;

        // Should we take the position from the closest door instead of center of vehicle
        if (bGetPositionFromClosestDoor && static_cast<VehicleType>(pTempVehicle->GetModel()) != VehicleType::VT_RCBARON)
        {
            // Get the closest front-door
            CVector vecFrontPos;
            int     iFrontDoor = 0;
            g_pGame->GetCarEnterExit()->GetNearestCarDoor(m_pPlayerPed, pGameVehicle, &vecFrontPos, &iFrontDoor);

            // Get the closest passenger-door
            CVector vecPassengerPos;
            int     iPassengerDoor;
            g_pGame->GetCarEnterExit()->GetNearestCarPassengerDoor(m_pPlayerPed, pGameVehicle, &vecPassengerPos, &iPassengerDoor, false, false, false);

            if (bCheckDriverDoor && !bCheckPassengerDoors)
            {
                iClosestDoor = iFrontDoor;
                vecClosestDoorPosition = vecVehiclePosition = vecFrontPos;
            }
            else
            {
                iClosestDoor = iPassengerDoor;
                vecClosestDoorPosition = vecVehiclePosition = vecPassengerPos;
            }
            if (bCheckDriverDoor)
            {
                // If they're different, find the closest
                if (iFrontDoor != iPassengerDoor && iPassengerDoor < 2)
                {
                    float fDistanceFromFront = DistanceBetweenPoints3D(vecPosition, vecFrontPos);
                    float fDistanceFromPassenger = DistanceBetweenPoints3D(vecPosition, vecPassengerPos);
                    if (fDistanceFromPassenger < fDistanceFromFront)
                    {
                        iClosestDoor = iPassengerDoor;
                        vecClosestDoorPosition = vecVehiclePosition = vecPassengerPos;
                    }
                }
            }
        }
        else
        {
            pTempVehicle->GetPosition(vecVehiclePosition);
        }

        float fDistance = DistanceBetweenPoints3D(vecPosition, vecVehiclePosition);
        if (fDistance <= fWithinRange)
        {
            if (pVehicle == NULL || fDistance < fClosestDistance)
            {
                pVehicle = pTempVehicle;
                fClosestDistance = fDistance;

                if (uiClosestDoor)
                {
                    // Get the actual door id
                    switch (iClosestDoor)
                    {
                        case 10:
                            iClosestDoor = 0;
                            break;
                        case 8:
                            iClosestDoor = 1;
                            break;
                        case 11:
                            iClosestDoor = 2;
                            break;
                        case 9:
                            iClosestDoor = 3;
                            break;
                    }
                    *uiClosestDoor = static_cast<unsigned int>(iClosestDoor);
                }
                if (pClosestDoorPosition)
                    *pClosestDoorPosition = vecClosestDoorPosition;
            }
        }
    }

    return pVehicle;
}

bool CClientPed::GetClosestDoor(CClientVehicle* pVehicle, bool bCheckDriverDoor, bool bCheckPassengerDoors, unsigned int& uiClosestDoor,
                                CVector* pClosestDoorPosition)
{
    if (!bCheckDriverDoor && !bCheckPassengerDoors)
        return false;

    int     iClosestDoor;
    CVector vecClosestDoorPosition;

    CVector vecPosition;
    GetPosition(vecPosition);

    CVehicle* pGameVehicle = pVehicle->GetGameVehicle();
    if (pGameVehicle)
    {
        if (m_pPlayerPed)
        {
            // Get the closest front-door
            CVector vecFrontPos;
            int     iFrontDoor;
            g_pGame->GetCarEnterExit()->GetNearestCarDoor(m_pPlayerPed, pGameVehicle, &vecFrontPos, &iFrontDoor);

            // Get the closest passenger-door
            CVector vecPassengerPos;
            int     iPassengerDoor;
            g_pGame->GetCarEnterExit()->GetNearestCarPassengerDoor(m_pPlayerPed, pGameVehicle, &vecPassengerPos, &iPassengerDoor, false, false, false);

            if (bCheckDriverDoor && !bCheckPassengerDoors)
            {
                iClosestDoor = iFrontDoor;
                vecClosestDoorPosition = vecFrontPos;
            }
            else
            {
                iClosestDoor = iPassengerDoor;
                vecClosestDoorPosition = vecPassengerPos;
            }
            if (bCheckDriverDoor)
            {
                // If they're different, find the closest
                if (iFrontDoor != iPassengerDoor)
                {
                    float fDistanceFromFront = DistanceBetweenPoints3D(vecPosition, vecFrontPos);
                    float fDistanceFromPassenger = DistanceBetweenPoints3D(vecPosition, vecPassengerPos);
                    if (fDistanceFromPassenger < fDistanceFromFront)
                    {
                        iClosestDoor = iPassengerDoor;
                        vecClosestDoorPosition = vecPassengerPos;
                    }
                }
            }
            // Get the actual door id
            switch (iClosestDoor)
            {
                case 10:
                    uiClosestDoor = 0;
                    break;
                case 8:
                    uiClosestDoor = 1;
                    break;
                case 11:
                    uiClosestDoor = 2;
                    break;
                case 9:
                    uiClosestDoor = 3;
                    break;
            }
        }
        if (pClosestDoorPosition)
            *pClosestDoorPosition = vecClosestDoorPosition;

        return true;
    }
    return false;
}

void CClientPed::GetOutOfVehicle(unsigned char ucDoor)
{
    if (ucDoor != 0xFF)
        m_ucLeavingDoor = ucDoor + 2;
    else
        m_ucLeavingDoor = 0xFF;
    m_bForceGettingOut = true;

    // Get the current vehicle you're in
    CClientVehicle* pVehicle = GetRealOccupiedVehicle();
    if (pVehicle)
    {
        // m_pOccupyingVehicle = pVehicle;
        if (m_pPlayerPed)
        {
            CVehicle* pGameVehicle = pVehicle->m_pVehicle;

            if (pGameVehicle)
            {
                CTaskComplexLeaveCar* pOutTask = g_pGame->GetTasks()->CreateTaskComplexLeaveCar(pGameVehicle, m_ucLeavingDoor);
                if (pOutTask)
                {
                    pOutTask->SetAsPedTask(m_pPlayerPed, TASK_PRIORITY_PRIMARY, true);

                    // Turn off the radio if local player
                    if (m_bIsLocalPlayer)
                    {
                        StopRadio();
                    }
                }
            }

            if (m_ucLeavingDoor != 0xFF)
                pVehicle->AllowDoorRatioSetting(m_ucLeavingDoor, false);
        }
    }

    ResetInterpolation();
    ResetToOutOfVehicleWeapon();
}

void CClientPed::GetIntoVehicle(CClientVehicle* pVehicle, unsigned int uiSeat, unsigned char ucDoor)
{
    // TODO: add checks to ensure we don't try to use the wrong seats for bikes etc
    // Eventually remove us from a previous vehicle
    RemoveFromVehicle();

    // Do it
    _GetIntoVehicle(pVehicle, uiSeat, ucDoor);
    m_uiOccupiedVehicleSeat = uiSeat;
    m_ucEnteringDoor = ucDoor;
    m_bForceGettingIn = true;
}

void CClientPed::WarpIntoVehicle(CClientVehicle* pVehicle, unsigned int uiSeat)
{
    // Ensure vehicle model is loaded
    CModelInfo* pModelInfo = pVehicle->GetModelInfo();
    if (g_pGame->IsASyncLoadingEnabled() && !pModelInfo->IsLoaded())
    {
        if (pVehicle->IsStreamedIn())
        {
            pModelInfo->Request(BLOCKING, "CClientPed::WarpIntoVehicle");
        }
    }

    // Wrong seat or undefined passengers count?
    if ((uiSeat > 0 && uiSeat > pVehicle->m_ucMaxPassengers) || (uiSeat > 0 && pVehicle->m_ucMaxPassengers == 255))
        return;

    // Transfer WaitingForGroundToLoad state to vehicle
    if (m_bIsLocalPlayer)
    {
        if (IsFrozenWaitingForGroundToLoad())
        {
            SetFrozenWaitingForGroundToLoad(false);
            pVehicle->SetFrozenWaitingForGroundToLoad(true, true);
        }
        CVector vecPosition;
        GetPosition(vecPosition);
        CVector vecVehiclePosition;
        pVehicle->GetPosition(vecVehiclePosition);
        float fDist = (vecPosition - vecVehiclePosition).Length();
        if (fDist > 50 && !pVehicle->IsFrozen())
        {
            pVehicle->SetFrozenWaitingForGroundToLoad(true, true);
        }
    }

    // Remove some tasks so we don't get any weird results
    SetChoking(false);
    SetHasJetPack(false);
    SetSunbathing(false);
    KillAnimation();

    if (m_pPlayerPed)
    {
        m_pPlayerPed->SetLanding(false);

        // Fall tasks
        KillTask(TASK_PRIORITY_EVENT_RESPONSE_TEMP);
        // Swim tasks
        KillTask(TASK_PRIORITY_EVENT_RESPONSE_NONTEMP);
        // Jump & vehicle enter/exit & custom animation tasks
        m_pTaskManager->RemoveTask(TASK_PRIORITY_PRIMARY);

        KillTaskSecondary(TASK_SECONDARY_ATTACK);

        // check we aren't in the fall and get up task
        CTask* pTaskPhysicalResponse = m_pTaskManager->GetTask(TASK_PRIORITY_PHYSICAL_RESPONSE);
        // check our physical response task
        if (pTaskPhysicalResponse && strcmp(pTaskPhysicalResponse->GetTaskName(), "TASK_COMPLEX_FALL_AND_GET_UP") == 0)
        {
            m_pTaskManager->RemoveTask(TASK_PRIORITY_PHYSICAL_RESPONSE);
        }
    }

    CClientVehicle* pPrevVehicle = GetRealOccupiedVehicle();
    // Eventually remove us from a previous vehicle
    RemoveFromVehicle();
    // m_uiOccupyingSeat = uiSeat;
    m_bForceGettingIn = false;
    m_bForceGettingOut = false;
    m_ucLeavingDoor = 0xFF;

    // Store our current seat
    if (m_pPlayerPed)
        m_pPlayerPed->SetOccupiedSeat((unsigned char)uiSeat);

    // Driverseat
    if (uiSeat == 0)
    {
        // Force the vehicle we're warping into to be streamed in
        // if the local player is entering it. This is so we don't
        // get screwed up with camera not following and similar issues.
        if (m_bIsLocalPlayer)
        {
            pVehicle->AddStreamReference();
            pVehicle->SetSwingingDoorsAllowed(true);
        }

        // Warp the player into the car's driverseat
        CVehicle* pGameVehicle = pVehicle->m_pVehicle;
        if (pGameVehicle)
        {
            // Warp him in
            InternalWarpIntoVehicle(pGameVehicle);

            if (m_bIsLocalPlayer || (g_pClientGame->GetLocalPlayer() && g_pClientGame->GetLocalPlayer()->GetOccupiedVehicle() == pVehicle))
            {
                // Tell vehicle audio we have driver
                pGameVehicle->GetVehicleAudioEntity()->JustGotInVehicleAsDriver();
            }

            // Make sure our camera is fixed on the new vehicle
            if (m_bIsLocalPlayer && pPrevVehicle && m_pManager->GetCamera()->GetTargetEntity() == pPrevVehicle)
                m_pManager->GetCamera()->SetTargetEntity(pVehicle);
        }

        // Update the vehicle and us so we know we've occupied it
        CClientVehicle::SetPedOccupiedVehicle(this, pVehicle, 0, 0xFF);
    }
    else
    {
        // Passenger seat
        unsigned char ucSeat = CClientVehicleManager::ConvertIndexToGameSeat(pVehicle->m_usModel, static_cast<unsigned char>(uiSeat));
        if (ucSeat != 0 && ucSeat != 0xFF)
        {
            if (m_pPlayerPed)
            {
                // Force the vehicle we're warping into to be streamed in
                // if the local player is entering it. This is so we don't
                // get screwed up with camera not following and similar issues.
                if (m_bIsLocalPlayer)
                {
                    pVehicle->AddStreamReference();
                }

                // Warp the player into the car's driverseat
                CVehicle* pGameVehicle = pVehicle->m_pVehicle;
                if (pGameVehicle)
                {
                    // Reset whatever task
                    m_pTaskManager->RemoveTask(TASK_PRIORITY_PRIMARY);

                    // Create a task to warp the player in and execute it
                    CTaskSimpleCarSetPedInAsPassenger* pInTask = g_pGame->GetTasks()->CreateTaskSimpleCarSetPedInAsPassenger(pGameVehicle, ucSeat);
                    if (pInTask)
                    {
                        pInTask->SetIsWarpingPedIntoCar();
                        pInTask->ProcessPed(m_pPlayerPed);
                        pInTask->Destroy();
                    }

                    if (m_bIsLocalPlayer && pVehicle->IsDriven())
                    {
                        // Tell vehicle audio we have driver
                        pGameVehicle->GetVehicleAudioEntity()->JustGotInVehicleAsDriver();
                    }

                    // Make sure our camera is fixed on the new vehicle
                    if (m_bIsLocalPlayer && pPrevVehicle && m_pManager->GetCamera()->GetTargetEntity() == pPrevVehicle)
                        m_pManager->GetCamera()->SetTargetEntity(pVehicle);
                }
            }

            // Update us so we know we've occupied it
            CClientVehicle::SetPedOccupiedVehicle(this, pVehicle, uiSeat, 0xFF);
        }
        else
            return;
    }

    // Turn on the radio if local player and it's not already on.
    if (m_bIsLocalPlayer)
    {
        CVehicle* pGameVehicle = pVehicle->m_pVehicle;
        if (pGameVehicle)
        {
            pGameVehicle->GetVehicleAudioEntity()->TurnOnRadioForVehicle();
        }
        StartRadio();
    }

    RemoveTargetPosition();

    if (!pVehicle->IsStreamedIn() || !m_pPlayerPed)
        SetWarpInToVehicleRequired(true);

    // Make peds stream in when they warp to a vehicle
    CVector vecInVehiclePosition;
    GetPosition(vecInVehiclePosition);
    UpdateStreamPosition(vecInVehiclePosition);
    if (pVehicle->IsStreamedIn() && !m_pPlayerPed)
        StreamIn(true);
}

void CClientPed::ResetToOutOfVehicleWeapon()
{
    if (m_pOutOfVehicleWeaponSlot != WEAPONSLOT_MAX)
    {
        // Jax: I think this should be left up to scripting
        // SetCurrentWeaponSlot ( m_pOutOfVehicleWeaponSlot );
        m_pOutOfVehicleWeaponSlot = WEAPONSLOT_MAX;
    }
}

CClientVehicle* CClientPed::RemoveFromVehicle(bool bSkipWarpIfGettingOut)
{
    SetWarpInToVehicleRequired(false);
    SetDoingGangDriveby(false);

    // Reset any enter/exit tasks
    if (IsEnteringVehicle())
    {
        m_pTaskManager->RemoveTask(TASK_PRIORITY_DEFAULT);
    }

    // Get the current vehicle you're in
    CClientVehicle* pVehicle = GetRealOccupiedVehicle();
    if (!pVehicle)
    {
        pVehicle = GetOccupiedVehicle();
        if (!pVehicle)
        {
            pVehicle = m_pOccupyingVehicle;
        }
    }

    if (pVehicle)
    {
        pVehicle->SetSwingingDoorsAllowed(false);

        // Warp the player out of the vehicle
        CVehicle* pGameVehicle = pVehicle->m_pVehicle;
        if (pGameVehicle)
        {
            // Did he really was in vehicle and is there driver?
            if (pVehicle != m_pOccupyingVehicle && pVehicle->GetOccupant())
            {
                // Local player left vehicle or got abandoned by remote driver
                if ((m_bIsLocalPlayer ||
                     (m_uiOccupiedVehicleSeat == 0 && (g_pClientGame->GetLocalPlayer() && g_pClientGame->GetLocalPlayer()->GetOccupiedVehicle() == pVehicle))))
                {
                    // Tell vehicle audio the driver left
                    pGameVehicle->GetVehicleAudioEntity()->JustGotOutOfVehicleAsDriver();
                }
            }

            // If vehicle was deleted during exit, don't skip warp. Fixes player getting stuck and going invisible.
            if (pVehicle->IsBeingDeleted())
                bSkipWarpIfGettingOut = false;

            // Jax: this should be safe, doesn't remove the player if he's getting dragged out already (fix for getting stuck on back after being jacked)
            if (!bSkipWarpIfGettingOut || (!IsGettingOutOfVehicle()))
            {
                // Warp the player out
                InternalRemoveFromVehicle(pGameVehicle);
            }
        }

        CClientVehicle::UnpairPedAndVehicle(this);

        if (m_bIsLocalPlayer)
        {
            pVehicle->RemoveStreamReference();
        }
    }

    // Reset the interpolation so we won't move from the last known spot to where we exit
    ResetInterpolation();

    // Local player?
    if (m_bIsLocalPlayer)
    {
        // Stop the radio
        StopRadio();
    }

    // And in our class
    CClientVehicle::UnpairPedAndVehicle(this);
    assert(m_pOccupiedVehicle == NULL);
    assert(m_pOccupyingVehicle == NULL);
    m_uiOccupiedVehicleSeat = 0xFF;

    m_bForceGettingIn = false;
    m_bForceGettingOut = false;
    m_ucLeavingDoor = 0xFF;

    return pVehicle;
}

bool CClientPed::IsVisible()
{
    if (m_pPlayerPed)
    {
        return m_pPlayerPed->IsVisible();
    }
    return m_bVisible;
}

void CClientPed::SetVisible(bool bVisible)
{
    m_bVisible = bVisible;
    UpdateAlphaAndVisibility();
}

bool CClientPed::GetUsesCollision()
{
    /*
    if ( m_pPlayerPed )
    {
        return m_pPlayerPed->GetUsesCollision ();
    }*/
    return m_bUsesCollision;
}

void CClientPed::SetUsesCollision(bool bUsesCollision)
{
    // The game changes this bool frequently, so we shouldn't set it every frame
    if (bUsesCollision != m_bUsesCollision)
    {
        if (m_pPlayerPed)
        {
            m_pPlayerPed->SetUsesCollision(bUsesCollision);
        }
        m_bUsesCollision = bUsesCollision;
    }
}

float CClientPed::GetMaxHealth()
{
    // Grab his player health stat
    float fStat = GetStat(MAX_HEALTH);

    // Do a linear interpolation to get how much health this would allow
    // Assumes: 100 health = 569 stat, 176 health = 1000 stat.
    float fMaxHealth = fStat * 0.176f;

    // Return the max health. Make sure it can't be below 1
    if (fMaxHealth < 1.0f)
        fMaxHealth = 1.0f;
    return fMaxHealth;
}

float CClientPed::GetHealth()
{
    if (m_bHealthLocked)
        return m_fHealth;

    if (m_pPlayerPed)
    {
        return m_pPlayerPed->GetHealth();
    }
    return m_fHealth;
}

void CClientPed::SetHealth(float fHealth)
{
    // If our health is locked, dont allow any change
    if (m_bHealthLocked)
        return;

    if (fHealth < 0.0f)
        fHealth = 0.0f;

    InternalSetHealth(fHealth);
    m_fHealth = fHealth;

    if (m_fHealth > 0.0f)
        m_bDead = false;
}

void CClientPed::InternalSetHealth(float fHealth)
{
    if (m_pPlayerPed)
    {
        // If the player is dead, call grab the current vehicle he's in, respawn and put back into the vehicle
        if (m_pPlayerPed->GetHealth() <= 0.0f && fHealth > 0.0f)
        {
            // Grab the vehicle and eventually warp out of it
            CClientVehicle* pVehicle = GetOccupiedVehicle();
            unsigned int    uiVehicleSeat = m_uiOccupiedVehicleSeat;
            RemoveFromVehicle();

            // If it's the local player, call respawn
            if (m_bIsLocalPlayer)
            {
                Respawn(NULL, false, true);
            }
            else
            {
                // Ped is alive again (Fix #414)
                UnlockHealth();
                UnlockArmor();
                SetIsDead(false);

                // Recreate the player
                ReCreateModel();
            }

            // If the vehicle existed, warp the player back in
            if (pVehicle)
            {
                WarpIntoVehicle(pVehicle, uiVehicleSeat);
            }
        }

        // Recheck we have a ped, ReCreateModel might destroy it
        if (m_pPlayerPed)
        {
            // Set the new health
            m_pPlayerPed->SetHealth(fHealth);
        }
    }
}

float CClientPed::GetArmor() const noexcept
{
    if (m_armorLocked)
        return m_armor;

    if (m_pPlayerPed)
        return m_pPlayerPed->GetArmor();

    return m_armor;
}

void CClientPed::SetArmor(float armor) noexcept
{
    if (m_armorLocked)
        return;

    armor = std::clamp(armor, 0.0f, 100.0f);

    if (m_pPlayerPed)
        m_pPlayerPed->SetArmor(armor);

    m_armor = armor;
}

void CClientPed::LockHealth(float fHealth)
{
    m_bHealthLocked = true;
    m_fHealth = fHealth;
}

void CClientPed::LockArmor(float armor) noexcept
{
    m_armorLocked = true;
    m_armor = armor;
}

float CClientPed::GetOxygenLevel()
{
    if (m_pPlayerPed)
    {
        return m_pPlayerPed->GetOxygenLevel();
    }
    return -1.0f;
}

void CClientPed::SetOxygenLevel(float fOxygen)
{
    if (m_pPlayerPed)
    {
        m_pPlayerPed->SetOxygenLevel(fOxygen);
    }
}

bool CClientPed::IsDying()
{
    if (m_pPlayerPed)
    {
        CTask* pTask = m_pTaskManager->GetTask(TASK_PRIORITY_EVENT_RESPONSE_NONTEMP);
        if (pTask)
        {
            if (pTask->GetTaskType() == TASK_SIMPLE_DIE || pTask->GetTaskType() == TASK_SIMPLE_DROWN || pTask->GetTaskType() == TASK_SIMPLE_DIE_IN_CAR ||
                pTask->GetTaskType() == TASK_COMPLEX_DIE_IN_CAR || pTask->GetTaskType() == TASK_SIMPLE_DROWN_IN_CAR || pTask->GetTaskType() == TASK_COMPLEX_DIE)
            {
                return true;
            }
        }
    }
    return false;
}

bool CClientPed::IsDead()
{
    if (m_pPlayerPed)
    {
        CTask* pTask = m_pTaskManager->GetTask(TASK_PRIORITY_EVENT_RESPONSE_NONTEMP);

        if (pTask)
            return pTask->GetTaskType() == TASK_SIMPLE_DEAD;
    }

    return m_bDead;
}

void CClientPed::BeHit(CClientPed* pClientPedAttacker, ePedPieceTypes hitBodyPart, int hitBodySide, int weaponId)
{
    CPlayerPed* pPedAttacker = pClientPedAttacker->GetGamePlayer();
    if (m_pPlayerPed && !IsDead() && !IsDying() && pPedAttacker)
    {
        CTask* pTask = g_pGame->GetTasks()->CreateTaskSimpleBeHit(pPedAttacker, hitBodyPart, hitBodySide, weaponId);
        if (pTask)
        {
            pTask->SetAsPedTask(m_pPlayerPed, TASK_PRIORITY_PHYSICAL_RESPONSE);
        }
    }
}

void CClientPed::Kill(eWeaponType weaponType, unsigned char ucBodypart, bool bStealth, bool bSetDirectlyDead, AssocGroupId animGroup, AnimationId animID)
{
    // Don't change task if already dead or dying
    if (m_pPlayerPed && !IsDead() && !IsDying())
    {
        // Do we have the in_water task?
        CTask* pTask = m_pTaskManager->GetTask(TASK_PRIORITY_EVENT_RESPONSE_NONTEMP);
        if (pTask && pTask->GetTaskType() == TASK_COMPLEX_IN_WATER)
        {
            // Kill the task
            pTask->MakeAbortable(m_pPlayerPed, ABORT_PRIORITY_URGENT, NULL);
        }

        m_pPlayerPed->SetLanding(false);

        // Make sure to remove the jetpack task before setting death tasks (Issue #7860)
        if (HasJetPack())
        {
            SetHasJetPack(false);
        }

        if (bSetDirectlyDead)
        {
            // TODO: Avoid the animation, try to make it go directly to the last animation frame.
            pTask = g_pGame->GetTasks()->CreateTaskSimpleDead(GetTickCount32(), true);
            if (pTask)
            {
                pTask->SetAsPedTask(m_pPlayerPed, TASK_PRIORITY_DEFAULT);
            }
        }
        else if (bStealth)
        {
            pTask = g_pGame->GetTasks()->CreateTaskSimpleStealthKill(false, m_pPlayerPed, 87);
            if (pTask)
            {
                pTask->SetAsPedTask(m_pPlayerPed, TASK_PRIORITY_PRIMARY);
            }
        }
        else
        {
            pTask = g_pGame->GetTasks()->CreateTaskComplexDie(weaponType, animGroup, animID, 4.0f, 1.0f);
            if (pTask)
            {
                pTask->SetAsPedTask(m_pPlayerPed, TASK_PRIORITY_EVENT_RESPONSE_NONTEMP);
            }
        }
    }
    if (m_bIsLocalPlayer)
    {
        SetHealth(0.0f);
        SetArmor(0.0f);
    }
    else
    {
        LockHealth(0.0f);
        LockArmor(0.0f);
    }

    // Silently remove the ped satchels
    DestroySatchelCharges(false, true);

    // Stop pressing buttons
    SetControllerState(CControllerState());

    // Remove goggles #9477
    if (IsWearingGoggles())
        SetWearingGoggles(false, false);

    m_bDead = true;
}

void CClientPed::StealthKill(CClientPed* pPed)
{
    if (m_pPlayerPed)
    {
        CPlayerPed* pPlayerPed = pPed->GetGamePlayer();
        if (pPlayerPed)
        {
            CTask* pTask = g_pGame->GetTasks()->CreateTaskSimpleStealthKill(true, pPlayerPed, 87);
            if (pTask)
            {
                pTask->SetAsPedTask(m_pPlayerPed, TASK_PRIORITY_PRIMARY);
            }
        }
    }
}

void CClientPed::SetFrozen(bool bFrozen)
{
    if (m_bFrozen != bFrozen)
    {
        m_bFrozen = bFrozen;

        if (bFrozen)
        {
            if (m_pTaskManager)
            {
                // Fix #366: Can only run forward bug
                m_pPlayerPed->SetLanding(false);

                // Let them have a jetpack (#9522)
                if (!HasJetPack())
                    m_pTaskManager->RemoveTask(TASK_PRIORITY_PRIMARY);

                m_pTaskManager->RemoveTask(TASK_PRIORITY_EVENT_RESPONSE_TEMP);
                m_pTaskManager->RemoveTask(TASK_PRIORITY_EVENT_RESPONSE_NONTEMP);

                // Let's let them choke too
                if (!IsChoking())
                    m_pTaskManager->RemoveTask(TASK_PRIORITY_PHYSICAL_RESPONSE);
            }

            // Always use the client's cached matrix, it's already updated by SetCurrentRotation
            m_matFrozen = m_Matrix;
        }

        // Wrapper freezes, collision holds and observer fencing share GTA's
        // bDontApplySpeed bit. Recompute the union so releasing one reason can
        // never accidentally release (or strand) another.
        ApplyPhysicalFreezeState();
    }
}

bool CClientPed::IsFrozenWaitingForGroundToLoad() const
{
    return m_bFrozenWaitingForGroundToLoad;
}

void CClientPed::SetFrozenWaitingForGroundToLoad(bool bFrozen)
{
    // Currently only for local player
    dassert(m_bIsLocalPlayer);

    if (!g_pGame->IsASyncLoadingEnabled(true))
        return;

    if (m_bFrozenWaitingForGroundToLoad != bFrozen)
    {
        m_bFrozenWaitingForGroundToLoad = bFrozen;

        if (bFrozen)
        {
            // Set auto unsuspend time in case changes prevent second call
            g_pGame->SuspendASyncLoading(true, 5000);

            m_fGroundCheckTolerance = 0.f;
            m_fObjectsAroundTolerance = -1.f;
            /*
                        if ( m_pTaskManager )
                        {
                            m_pTaskManager->RemoveTask ( TASK_PRIORITY_PRIMARY );
                            m_pTaskManager->RemoveTask ( TASK_PRIORITY_EVENT_RESPONSE_TEMP );
                            m_pTaskManager->RemoveTask ( TASK_PRIORITY_EVENT_RESPONSE_NONTEMP );
                            m_pTaskManager->RemoveTask ( TASK_PRIORITY_PHYSICAL_RESPONSE );
                        }
            */
            if (m_pPlayerPed)
            {
                m_pPlayerPed->GetMatrix(&m_matFrozen);
            }
            else
            {
                m_matFrozen = m_Matrix;
            }
        }
        else
        {
            g_pGame->SuspendASyncLoading(false);
        }
    }
}

CWeapon* CClientPed::GiveWeapon(eWeaponType weaponType, unsigned int uiAmmo, bool bSetAsCurrent)
{
    CWeapon* pWeapon = NULL;
    if (m_pPlayerPed)
    {
        // Grab our current ammo in clip
        pWeapon = GetWeapon(weaponType);
        unsigned int uiPreviousAmmoTotal = 0, uiPreviousAmmoInClip = 0;
        eWeaponSkill weaponSkill = WEAPONSKILL_STD;
        eWeaponType  previousWeaponType = eWeaponType::WEAPONTYPE_ANYWEAPON;
        if (pWeapon)
        {
            uiPreviousAmmoTotal = pWeapon->GetAmmoTotal();
            uiPreviousAmmoInClip = pWeapon->GetAmmoInClip();
            previousWeaponType = pWeapon->GetType();
        }

        if (weaponType >= WEAPONTYPE_PISTOL && weaponType <= WEAPONTYPE_TEC9)
        {
            float fSkill = GetStat(g_pGame->GetStats()->GetSkillStatIndex(weaponType));
            weaponSkill = g_pGame->GetWeaponStatManager()->GetWeaponSkillFromSkillLevel(weaponType, fSkill);
        }

        pWeapon = m_pPlayerPed->GiveWeapon(weaponType, uiAmmo, weaponSkill);

        // Restore clip ammo?
        if (uiPreviousAmmoInClip)
        {
            unsigned int uiTotalAmmo;
            eWeaponSlot  slot = pWeapon->GetSlot();
            if (pWeapon->GetType() != previousWeaponType)
            {
                // Emulate GTA's behaviour of setting ammo to keep in sync
                if (slot <= 1 || slot >= 10)
                {
                    // Melee Weapons
                    uiTotalAmmo = 1;
                }
                else if (slot >= 3 && slot <= 5)
                {
                    // slot 3,4,5 share the ammo, also if it's the currently used weapon add
                    uiTotalAmmo = uiPreviousAmmoTotal + uiAmmo;
                }
                else
                {
                    // Other slots replace the ammo
                    uiTotalAmmo = uiAmmo;
                }
            }
            else
            {
                // same weapon so always add
                uiTotalAmmo = uiPreviousAmmoTotal + uiAmmo;
            }
            // If we have less ammo in total than in our clip, update it accordingly
            if (uiPreviousAmmoInClip > uiTotalAmmo)
                uiPreviousAmmoInClip = uiTotalAmmo;

            pWeapon->SetAmmoTotal(uiTotalAmmo);
            pWeapon->SetAmmoInClip(uiPreviousAmmoInClip);
        }

        if (bSetAsCurrent)
            pWeapon->SetAsCurrentWeapon();
    }

    CWeaponInfo* pInfo = NULL;
    if (weaponType >= eWeaponType::WEAPONTYPE_PISTOL && weaponType <= WEAPONTYPE_TEC9)
    {
        float        fStat = GetStat(g_pGame->GetStats()->GetSkillStatIndex(weaponType));
        eWeaponSkill weaponSkill = g_pGame->GetWeaponStatManager()->GetWeaponSkillFromSkillLevel(weaponType, fStat);
        pInfo = g_pGame->GetWeaponInfo(weaponType, weaponSkill);
    }
    else
    {
        pInfo = g_pGame->GetWeaponInfo(weaponType);
    }
    if (pInfo)
    {
        eWeaponSlot slot = pInfo->GetSlot();
        m_WeaponTypes[slot] = weaponType;

        if (bSetAsCurrent)
            m_CurrentWeaponSlot = slot;
    }

    return pWeapon;
}

bool CClientPed::SetCurrentWeaponSlot(eWeaponSlot weaponSlot)
{
    if (weaponSlot < WEAPONSLOT_MAX)
    {
        if (weaponSlot != GetCurrentWeaponSlot())
        {
            if (m_pPlayerPed)
            {
                if (weaponSlot == WEAPONSLOT_TYPE_UNARMED)
                {
                    eWeaponSlot currentSlot = GetCurrentWeaponSlot();
                    CWeapon*    oldWeapon = GetWeapon(currentSlot);
                    DWORD       ammoInClip = oldWeapon->GetAmmoInClip();
                    DWORD       ammoInTotal = oldWeapon->GetAmmoTotal();
                    eWeaponType weaponType = oldWeapon->GetType();

                    bool isGoggles = currentSlot == WEAPONSLOT_TYPE_PARACHUTE && (weaponType == WEAPONTYPE_NIGHTVISION || weaponType == WEAPONTYPE_INFRARED);
                    if (!isGoggles)
                    {
                        RemoveWeapon(oldWeapon->GetType());
                    }

                    m_pPlayerPed->SetCurrentWeaponSlot(WEAPONSLOT_TYPE_UNARMED);

                    if (!isGoggles)
                    {
                        CWeapon* newWeapon = GiveWeapon(weaponType, ammoInTotal);
                        newWeapon->SetAmmoInClip(ammoInClip);
                        newWeapon->SetAmmoTotal(ammoInTotal);
                    }

                    // Don't allow doing gang driveby while unarmed
                    if (IsDoingGangDriveby())
                        SetDoingGangDriveby(false);
                    m_CurrentWeaponSlot = weaponSlot;
                    return true;
                }
                else
                {
                    // Make sure we have a weapon and some ammo on this slot
                    CWeapon* pWeapon = GetWeapon(weaponSlot);
                    if (pWeapon && pWeapon->GetAmmoTotal())
                    {
                        m_pPlayerPed->SetCurrentWeaponSlot(weaponSlot);
                        m_CurrentWeaponSlot = weaponSlot;
                        return true;
                    }
                }
            }
            else
            {
                m_CurrentWeaponSlot = weaponSlot;
                return true;
            }
        }
    }
    return false;
}

eWeaponSlot CClientPed::GetCurrentWeaponSlot()
{
    if (m_pPlayerPed)
    {
        return m_pPlayerPed->GetCurrentWeaponSlot();
    }
    return m_CurrentWeaponSlot;
}

eWeaponType CClientPed::GetCurrentWeaponType()
{
    if (m_pPlayerPed)
    {
        CWeapon* pWeapon = GetWeapon(GetCurrentWeaponSlot());
        if (pWeapon)
        {
            return pWeapon->GetType();
        }
    }
    return WEAPONTYPE_UNARMED;
}

bool CClientPed::IsCurrentWeaponUsingBulletSync()
{
    eWeaponType weaponType = GetCurrentWeaponType();
    return g_pClientGame->GetWeaponTypeUsesBulletSync(weaponType);
}

CWeapon* CClientPed::GetWeapon()
{
    if (m_pPlayerPed)
    {
        return m_pPlayerPed->GetWeapon(m_pPlayerPed->GetCurrentWeaponSlot());
    }
    return NULL;
}

eWeaponType CClientPed::GetWeaponType(eWeaponSlot slot)
{
    if (slot >= WEAPONSLOT_MAX)
        return WEAPONTYPE_UNARMED;

    if (m_pPlayerPed)
    {
        CWeapon* pWeapon = GetWeapon(slot);
        if (pWeapon)
        {
            return pWeapon->GetType();
        }
        return WEAPONTYPE_UNARMED;
    }
    return m_WeaponTypes[slot];
}

void CClientPed::RemoveWeapon(eWeaponType weaponType)
{
    if (m_pPlayerPed)
    {
        m_pPlayerPed->ClearWeapon(weaponType);
        m_pPlayerPed->RemoveWeaponModel(weaponType);

        // Set whatever weapon to 0 ammo so we don't keep it anymore
        CWeapon* pWeapon = GetWeapon(weaponType);
        if (pWeapon)
        {
            pWeapon->SetType(WEAPONTYPE_UNARMED);
            pWeapon->SetAmmoInClip(0);
            pWeapon->SetAmmoTotal(0);
            pWeapon->Remove();
        }
    }
    for (int i = 0; i < (int)WEAPONSLOT_MAX; i++)
    {
        if (m_WeaponTypes[i] == weaponType)
        {
            m_WeaponTypes[i] = WEAPONTYPE_UNARMED;
            m_usWeaponAmmo[i] = 0;
            if (m_CurrentWeaponSlot == (eWeaponSlot)i)
            {
                m_CurrentWeaponSlot = WEAPONSLOT_TYPE_UNARMED;
            }
        }
    }
}

void CClientPed::RemoveAllWeapons()
{
    if (m_bIsLocalPlayer)
    {
        g_pClientGame->ResetAmmoInClip();
        g_pMultiplayer->SetNightVisionEnabled(false, true);
        g_pMultiplayer->SetThermalVisionEnabled(false, true);
    }
    if (m_pPlayerPed)
    {
        m_pPlayerPed->ClearWeapons();
    }

    for (int i = 0; i < (int)WEAPONSLOT_MAX; i++)
    {
        m_WeaponTypes[i] = WEAPONTYPE_UNARMED;
        m_usWeaponAmmo[i] = 0;
    }
    m_CurrentWeaponSlot = WEAPONSLOT_TYPE_UNARMED;
}

CWeapon* CClientPed::GetWeapon(eWeaponSlot weaponSlot)
{
    if (weaponSlot < WEAPONSLOT_MAX)
    {
        if (m_pPlayerPed)
        {
            return m_pPlayerPed->GetWeapon(weaponSlot);
        }
    }
    return NULL;
}

CWeapon* CClientPed::GetWeapon(eWeaponType weaponType)
{
    if (weaponType < WEAPONTYPE_LAST_WEAPONTYPE)
    {
        if (m_pPlayerPed)
        {
            return m_pPlayerPed->GetWeapon(weaponType);
        }
    }
    return NULL;
}

bool CClientPed::HasWeapon(eWeaponType weaponType)
{
    if (m_pPlayerPed)
    {
        CWeapon* pWeapon = GetWeapon(weaponType);
        if (pWeapon)
            return true;
    }
    else
    {
        for (int i = 0; i < (int)WEAPONSLOT_MAX; i++)
        {
            if (m_WeaponTypes[i] == weaponType)
            {
                return true;
            }
        }
    }

    return false;
}

//
// Check and attempt to fix weapons for remote players
//
void CClientPed::ValidateRemoteWeapons()
{
    // Must be streamed in remote player
    if (!m_pPlayerPed || IsLocalPlayer() || GetType() != CCLIENTPLAYER)
        return;

    // Check everything matches
    bool bMismatch = false;
    for (uint i = 0; i < WEAPONSLOT_MAX; i++)
    {
        eWeaponType slotWeaponType = GetWeaponType((eWeaponSlot)i);
        if (m_WeaponTypes[i] != slotWeaponType)
        {
            SString strPlayerName = ((CClientPlayer*)this)->GetNick();
            AddReportLog(5430, SString("Mismatch in slot %d  Wanted type:%d  Got type:%d (%s)", i, m_WeaponTypes[i], slotWeaponType, *strPlayerName), 30);
            bMismatch = true;
        }
    }

    if (!bMismatch)
    {
        // All fine. Save current slot
        m_CurrentWeaponSlot = m_pPlayerPed->GetCurrentWeaponSlot();
        return;
    }

    // Fix wrongness
    for (uint i = 0; i < WEAPONSLOT_MAX; i++)
    {
        if (m_WeaponTypes[i] != WEAPONTYPE_UNARMED)
        {
            bool bSetAsCurrent = (i == m_CurrentWeaponSlot);
            GiveWeapon(m_WeaponTypes[i], m_usWeaponAmmo[i], bSetAsCurrent);
        }
    }
    m_pPlayerPed->SetCurrentWeaponSlot(m_CurrentWeaponSlot);
}

eMovementState CClientPed::GetMovementState()
{
    // Do we have a player, and are we on foot? (streamed in)
    if (m_pPlayerPed && !GetRealOccupiedVehicle())
    {
        CControllerState cs;
        GetControllerState(cs);

        // Get his current task(s)
        CTask* pActiveTask = GetTaskManager()->GetActiveTask();
        CTask* pSimplestTask = GetTaskManager()->GetSimplestActiveTask();
        if (!pActiveTask || !pSimplestTask)
            return MOVEMENTSTATE_UNKNOWN;

        const char* szComplexTaskName = pActiveTask->GetTaskName();
        const char* szSimpleTaskName = pSimplestTask->GetTaskName();

        // Check tasks
        if (strcmp(szSimpleTaskName, "TASK_SIMPLE_CLIMB") == 0)  // Is he climbing?
        {
            CTaskSimpleClimb* climbingTask = dynamic_cast<CTaskSimpleClimb*>(GetTaskManager()->GetSimplestActiveTask());
            if (climbingTask && climbingTask->GetHeightForPos() == eClimbHeights::CLIMB_GRAB)
                return MOVEMENTSTATE_HANGING;

            return MOVEMENTSTATE_CLIMB;
        }
        else if (strcmp(szComplexTaskName, "TASK_COMPLEX_JUMP") == 0)  // Is he jumping?
            return MOVEMENTSTATE_JUMP;
        else if (strcmp(szSimpleTaskName, "TASK_SIMPLE_GO_TO_POINT") == 0)  // Entering vehicle (walking to the doors)?
            return MOVEMENTSTATE_WALK_TO_POINT;
        else if (strcmp(szSimpleTaskName, "TASK_SIMPLE_SWIM") == 0)  // Is he swimming?
            return MOVEMENTSTATE_SWIM;
        else if (strcmp(szSimpleTaskName, "TASK_SIMPLE_JETPACK") == 0)  // Is he flying?
        {
            if (cs.ButtonCross != 0)
                return MOVEMENTSTATE_ASCENT_JETPACK;
            else if (cs.ButtonSquare != 0)
                return MOVEMENTSTATE_DESCENT_JETPACK;
            else
                return MOVEMENTSTATE_JETPACK;
        }

        // Check movement state
        if (!IsOnGround() && !GetContactEntity() && !m_pPlayerPed->IsStandingOnEntity() && !m_pPlayerPed->IsInWater() &&
            (strcmp(szSimpleTaskName, "TASK_SIMPLE_IN_AIR") == 0 || strcmp(szSimpleTaskName, "TASK_SIMPLE_FALL") == 0))  // Is he falling?
            return MOVEMENTSTATE_FALL;

        // Sometimes it returns 'fall' or 'walk', so it's better to return false instead
        if (IsEnteringVehicle() || IsLeavingVehicle())
            return MOVEMENTSTATE_UNKNOWN;

        if (!IsDucked())
        {
            // Use the effective controller state. It includes both the local
            // scripted pad and the observer-side native-task presentation.
            const bool walking = CClientPad::GetControlState("walk", cs, true);

            switch (m_pPlayerPed->GetMoveState())
            {
                case PedMoveState::PEDMOVE_STILL:
                    return MOVEMENTSTATE_STAND;
                case PedMoveState::PEDMOVE_WALK:
                    return (cs.LeftStickX == 0 && cs.LeftStickY == 0) ? MOVEMENTSTATE_STAND : MOVEMENTSTATE_WALK;
                case PedMoveState::PEDMOVE_SPRINT:
                    return MOVEMENTSTATE_SPRINT;
                case PedMoveState::PEDMOVE_RUN:
                    return walking ? MOVEMENTSTATE_WALK : MOVEMENTSTATE_JOG;  // FileEX: It should be MOVEMENTSTATE_RUN, but we're keeping JOG for backward
                                                                              // compatibility (PEDMOVE_JOG is unused in SA)
            }
        }
        else
        {
            // Is he moving the controller at all?
            if (cs.LeftStickX == 0 && cs.LeftStickY == 0)
                return MOVEMENTSTATE_CROUCH;
            else
                return (cs.LeftStickX != 0 && cs.RightShoulder1 != 0) ? MOVEMENTSTATE_ROLL : MOVEMENTSTATE_CRAWL;
        }
    }

    return MOVEMENTSTATE_UNKNOWN;
}

bool CClientPed::GetMovementState(std::string& strStateName)
{
    eMovementState eCurrentMoveState = GetMovementState();
    if (eCurrentMoveState == MOVEMENTSTATE_UNKNOWN)
        return false;

    strStateName = m_MovementStateNames[eCurrentMoveState];
    return true;
}

CTask* CClientPed::GetCurrentPrimaryTask()
{
    if (m_pPlayerPed)
    {
        return m_pTaskManager->GetTask(TASK_PRIORITY_PRIMARY);
    }

    return NULL;
}

bool CClientPed::IsSimplestTask(int iTaskType)
{
    if (m_pPlayerPed)
    {
        CTask* pTask = m_pTaskManager->GetSimplestActiveTask();
        if (pTask)
        {
            return (pTask->GetTaskType() == iTaskType);
        }
    }
    return false;
}

bool CClientPed::HasTask(int iTaskType, int iTaskPriority, bool bPrimary)
{
    if (m_pPlayerPed)
    {
        int    iNumTasks = (bPrimary) ? TASK_PRIORITY_MAX : TASK_SECONDARY_MAX;
        CTask* pTask = NULL;
        if (iTaskPriority >= 0 && iTaskPriority < iNumTasks)
        {
            pTask = (bPrimary) ? m_pTaskManager->GetTask(iTaskPriority) : m_pTaskManager->GetTaskSecondary(iTaskPriority);
            if (pTask && pTask->GetTaskType() == iTaskType)
            {
                return true;
            }
        }
        else
        {
            for (int i = 0; i < TASK_PRIORITY_MAX; i++)
            {
                pTask = m_pTaskManager->GetTask(i);
                if (pTask && pTask->GetTaskType() == iTaskType)
                {
                    return true;
                }
            }
            for (int i = 0; i < TASK_SECONDARY_MAX; i++)
            {
                pTask = m_pTaskManager->GetTaskSecondary(i);
                if (pTask && pTask->GetTaskType() == iTaskType)
                {
                    return true;
                }
            }
        }
    }
    return false;
}

bool CClientPed::SetTask(CTask* pTask, int iTaskPriority)
{
    if (m_pTaskManager)
    {
        pTask->SetAsPedTask(m_pPlayerPed, iTaskPriority);
        return true;
    }

    return false;
}

bool CClientPed::SetTaskSecondary(CTask* pTask, int iTaskPriority)
{
    if (m_pTaskManager)
    {
        pTask->SetAsSecondaryPedTask(m_pPlayerPed, iTaskPriority);
        return true;
    }

    return false;
}

bool CClientPed::KillTask(int iTaskPriority, bool bGracefully)
{
    if (m_pTaskManager)
    {
        CTask* pTask = m_pTaskManager->GetTask(iTaskPriority);
        if (pTask)
        {
            if (bGracefully)
            {
                pTask->MakeAbortable(m_pPlayerPed, ABORT_PRIORITY_IMMEDIATE, NULL);
                pTask->Destroy();
            }
            m_pTaskManager->RemoveTask(iTaskPriority);
            return true;
        }
    }
    return false;
}

bool CClientPed::KillTaskSecondary(int iTaskPriority, bool bGracefully)
{
    if (m_pTaskManager)
    {
        CTask* pTask = m_pTaskManager->GetTaskSecondary(iTaskPriority);
        if (pTask)
        {
            if (bGracefully)
            {
                pTask->MakeAbortable(m_pPlayerPed, ABORT_PRIORITY_IMMEDIATE, NULL);
                pTask->Destroy();
            }
            m_pTaskManager->RemoveTaskSecondary(iTaskPriority);
            return true;
        }
    }
    return false;
}

CVector CClientPed::GetAim() const
{
    if (m_shotSyncData)
        return CVector(m_shotSyncData->m_fArmDirectionX, m_shotSyncData->m_fArmDirectionY, 0);
    return CVector();
}

void CClientPed::SetAim(float fArmDirectionX, float fArmDirectionY, eVehicleAimDirection cInVehicleAimAnim)
{
    if (!m_bIsLocalPlayer)
    {
        m_ulBeginAimTime = 0;
        m_ulTargetAimTime = 0;
        m_shotSyncData->m_fArmDirectionX = fArmDirectionX;
        m_shotSyncData->m_fArmDirectionY = fArmDirectionY;
        m_shotSyncData->m_cInVehicleAimDirection = cInVehicleAimAnim;
    }
}

void CClientPed::SetAimInterpolated(unsigned long ulDelay, float fArmDirectionX, float fArmDirectionY, bool bAkimboAimUp,
                                    eVehicleAimDirection cInVehicleAimAnim)
{
    if (!m_bIsLocalPlayer)
    {
        // Force the old akimbo up thing
        m_remoteDataStorage->SetAkimboTargetUp(m_bTargetAkimboUp);

        // Set the new data
        m_ulBeginAimTime = CClientTime::GetTime();
        m_ulTargetAimTime = m_ulBeginAimTime + ulDelay;
        m_bTargetAkimboUp = bAkimboAimUp;
        m_fBeginAimX = m_shotSyncData->m_fArmDirectionX;
        m_fBeginAimY = m_shotSyncData->m_fArmDirectionY;
        m_fTargetAimX = fArmDirectionX;
        m_fTargetAimY = fArmDirectionY;
        m_shotSyncData->m_cInVehicleAimDirection = cInVehicleAimAnim;
    }
}

void CClientPed::SetAimingData(unsigned long ulDelay, const CVector& vecTargetPosition, float fArmDirectionX, float fArmDirectionY,
                               eVehicleAimDirection cInVehicleAimAnim, CVector* pSource, bool bInterpolateAim)
{
    if (!m_bIsLocalPlayer)
    {
        if (bInterpolateAim)
        {
            m_ulBeginAimTime = CClientTime::GetTime();
            m_ulTargetAimTime = m_ulBeginAimTime + ulDelay;
            m_fBeginAimX = m_shotSyncData->m_fArmDirectionX;
            m_fBeginAimY = m_shotSyncData->m_fArmDirectionY;
            m_fTargetAimX = fArmDirectionX;
            m_fTargetAimY = fArmDirectionY;
        }
        else
        {
            m_ulBeginAimTime = 0;
            m_ulTargetAimTime = 0;
            m_shotSyncData->m_fArmDirectionX = fArmDirectionX;
            m_shotSyncData->m_fArmDirectionY = fArmDirectionY;
        }

        m_shotSyncData->m_vecShotTarget = vecTargetPosition;
        m_shotSyncData->m_cInVehicleAimDirection = cInVehicleAimAnim;

        m_shotSyncData->m_bUseOrigin = pSource != NULL;
        if (pSource)
        {
            m_shotSyncData->m_vecShotOrigin = *pSource;
        }
    }
}

void CClientPed::WorldIgnore(bool bIgnore)
{
    if (bIgnore)
    {
        if (m_pPlayerPed)
        {
            g_pGame->GetWorld()->IgnoreEntity(m_pPlayerPed);
        }
    }
    else
    {
        g_pGame->GetWorld()->IgnoreEntity(NULL);
    }
    m_bWorldIgnored = bIgnore;
}

void CClientPed::StreamedInPulse(bool bDoStandardPulses)
{
    // ControllerState checks and fixes are done at the same same as everything else unless using alt pulse order
    bool bDoControllerStateFixPulse = g_pClientGame->IsUsingAlternatePulseOrder() ? !bDoStandardPulses : bDoStandardPulses;

    if (!bDoStandardPulses)
    {
        if (bDoControllerStateFixPulse)
        {
            // ControllerState checks and fixes only
            CControllerState Current;
            GetControllerState(Current);
            RemoveNativeTaskLocomotionPresentation(Current);
            m_rawControllerState = Current;

            ApplyControllerStateFixes(Current);
            if (GetType() != CCLIENTPED)
                ApplyNativeTaskLocomotionPresentation(Current);

            if (m_bIsLocalPlayer)
                m_pPad->SetCurrentControllerState(&Current);
            else
                memcpy(m_currentControllerState, &Current, sizeof(CControllerState));
        }
        return;
    }

    // Re-create ped
    if (m_shouldRecreate)
        ReCreateGameEntity();

    // Grab some vars here, saves getting them twice
    CClientVehicle* pVehicle = GetOccupiedVehicle();

    // Do we have a player? (streamed in)
    if (m_pPlayerPed)
    {
        // If it's local entity, update in/out vehicle state
        if (IsLocalEntity())
            UpdateVehicleInOut();

        // Handle waiting for the ground to load
        if (IsFrozenWaitingForGroundToLoad())
            HandleWaitingForGroundToLoad();

        UpdateNativeAmbientOwnerCollisionFence();
        UpdateRemoteStreamInTransformFence();

        // Bodge to get things loaded quicker on spawn
        if (m_iLoadAllModelsCounter)
        {
            m_iLoadAllModelsCounter--;
            if (GetModelInfo())
                g_pGame->GetStreaming()->LoadAllRequestedModels(false, "CClientPed::StreamedInPulse - m_iLoadAllModelsCounter");
        }

        if (m_bPendingRebuildPlayer)
            ProcessRebuildPlayer(true);

        UpdateNativeTaskWeaponPresentation();
        UpdateNativeTaskAnimationPresentation();
        UpdateRemoteReplicaPhysicsFence();

        if (RefreshNativeCollisionResidency())
            ApplyNativeEventProfileState();

        // Run any gang driveby abort deferred by SetDoingGangDriveby(false).
        if (m_bDeferredGangDrivebyAbort)
        {
            m_bDeferredGangDrivebyAbort = false;
            CTask* primaryTask = m_pTaskManager->GetTask(TASK_PRIORITY_PRIMARY);
            if (primaryTask && primaryTask->GetTaskType() == TASK_SIMPLE_GANG_DRIVEBY)
                primaryTask->MakeAbortable(m_pPlayerPed, ABORT_PRIORITY_URGENT, NULL);
        }

        CControllerState Current;
        GetControllerState(Current);
        RemoveNativeTaskLocomotionPresentation(Current);
        m_rawControllerState = Current;

        if (bDoControllerStateFixPulse)
            ApplyControllerStateFixes(Current);
        if (GetType() != CCLIENTPED)
            ApplyNativeTaskLocomotionPresentation(Current);

        // Set the controller state we might've changed something in
        // We can't use SetControllerState as it will update the previous
        // controller state aswell and that will screw up with impulse buttons
        // like weapon switching.
        if (m_bIsLocalPlayer)
        {
            m_pPad->SetCurrentControllerState(&Current);
        }
        else
        {
            memcpy(m_currentControllerState, &Current, sizeof(CControllerState));
        }

        // Are we frozen and not in a vehicle
        if (IsFrozen() && !pVehicle)
        {
            CVector vecTemp;
            m_pPlayerPed->SetMatrix(&m_matFrozen);
            m_pPlayerPed->SetMoveSpeed(vecTemp);
        }

        // Is our health locked?
        if (m_bHealthLocked)
        {
            InternalSetHealth(m_fHealth);
        }

        // Is our armor locked?
        if (m_armorLocked)
        {
            m_pPlayerPed->SetArmor(m_armor);
        }

        // In a vehicle?
        if (pVehicle)
        {
            // Jax: this stops the game removing weapons in vehicles
            CWeapon* pCurrentWeapon = GetWeapon();
            if (pCurrentWeapon)
            {
                pCurrentWeapon->SetAsCurrentWeapon();
            }

            // Remove any contact entity we have saved (we won't have one in a vehicle)
            if (m_pCurrentContactEntity)
            {
                m_pCurrentContactEntity->RemoveContact(this);
                m_pCurrentContactEntity = NULL;
            }
        }
        // Not in a vehicle?
        else
        {
            // Not the local player?
            if (!m_bIsLocalPlayer)
            {
                // Force the ped in/out? Only if remote player or ped we dont sync
                if (m_bForceGettingIn && !IsSyncing())
                {
                    // Are we entering a vehicle and it's a different vehicle from the one we've entered?
                    if (m_pOccupyingVehicle)
                    {
                        // Are we not already getting in?
                        CTask* pTask = GetCurrentPrimaryTask();
                        if (pTask)
                        {
                            int iTaskType = pTask->GetTaskType();
                            if (iTaskType != TASK_COMPLEX_ENTER_CAR_AS_DRIVER && iTaskType != TASK_COMPLEX_ENTER_CAR_AS_PASSENGER)
                            {
                                _GetIntoVehicle(m_pOccupyingVehicle, m_uiOccupiedVehicleSeat, m_ucEnteringDoor);
                            }
                        }
                        else
                        {
                            _GetIntoVehicle(m_pOccupyingVehicle, m_uiOccupiedVehicleSeat, m_ucEnteringDoor);
                        }
                    }
                }
                // Force him to get out of the vehicle as this tasks can sometimes cancel. This also
                // applies to the local player and can cause problem #2870. Only if remote player or ped we dont sync
                if (m_bForceGettingOut && !IsSyncing())
                {
                    // Are we out of the car? If not, continue forcing.
                    if (GetRealOccupiedVehicle())
                    {
                        // Are we not already getting out?
                        CTask* pTask = GetCurrentPrimaryTask();
                        if (!pTask || pTask->GetTaskType() != TASK_COMPLEX_LEAVE_CAR)
                        {
                            GetOutOfVehicle(m_ucLeavingDoor);
                        }
                    }
                    else
                    {
                        m_bForceGettingOut = false;
                        m_ucLeavingDoor = 0xFF;
                    }
                }

                Interpolate();
                UpdateKeysync();
            }

            // Store contact entities for quick-access (relies on playerManager being pulsed before vehicleManager + objectManager)
            CClientEntity* pContactEntity = NULL;
            pContactEntity = GetContactEntity();
            // Is the current contact-entity different to our stored one?
            if (pContactEntity != m_pCurrentContactEntity)
            {
                if (m_pCurrentContactEntity)
                {
                    m_pCurrentContactEntity->RemoveContact(this);
                }
                if (pContactEntity)
                {
                    pContactEntity->AddContact(this);
                }
                m_pCurrentContactEntity = pContactEntity;
            }
        }

        // Are we a CClientPed and not a CClientPlayer
        if (GetType() == CCLIENTPED)
        {
            // Update our controller state to match our scripted pad
            m_Pad.DoPulse(this);

            // Scripted peds rebuild their controller state above, after the
            // common controller-state pulse. Apply remote native-task
            // locomotion last so the scripted pad cannot erase this
            // presentation-only input before GTA processes the ped.
            CControllerState Current;
            GetControllerState(Current);
            m_rawControllerState = Current;
            ApplyNativeTaskOwnerLocomotionAssist(Current);
            ApplyNativeTaskLocomotionPresentation(Current);
            memcpy(m_currentControllerState, &Current, sizeof(CControllerState));
        }

        // Are we waiting on an unloaded anim-block?
        if (m_bRequestedAnimation && m_pAnimationBlock)
        {
            // Is it loaded now?
            if (m_pAnimationBlock->IsLoaded())
            {
                if (m_bisCurrentAnimationCustom)
                {
                    m_bisNextAnimationCustom = true;
                }

                m_bRequestedAnimation = false;

                RunAnimationFromCache();
            }
        }

        // Are we need to update anim speed & progress?
        // We need to do it here because the anim starts on the next frame after calling RunNamedAnimation
        if (m_pAnimationBlock && m_AnimationCache.progressWaitForStreamIn && IsAnimationInProgress())
            UpdateAnimationProgressAndSpeed();

        UpdateAlphaAndVisibility();

        // Grab our current position
        CVector vecPosition = *m_pPlayerPed->GetPosition();
        // Have we moved?
        if (vecPosition != m_Matrix.vPos)
        {
            // Store our new position
            m_Matrix.vPos = vecPosition;
            m_matFrozen.vPos = vecPosition;

            // Update our streaming position
            UpdateStreamPosition(vecPosition);
        }
        // Fix for unloading ped models which are currently streamed in (DO NOT REMOVE or players will not reset to the default models!)
        if (m_ulStoredModel > 0 && m_ulModel == 0)
        {
            // Make sure the scripter hasn't fixed this himself as well by changing from CJ back. (unlikely but who knows).
            if (m_ulModel == 0)
            {
                // Give him back his previous model
                SetModel(m_ulStoredModel, false, m_usStoredLogicalModel);
            }
            // Reset the stored model
            m_ulStoredModel = 0;
            m_usStoredLogicalModel = 0xFFFF;
        }
        // Fix for unloading weapon models which are currently streamed in (DO NOT REMOVE or weapons will not reset to the default models!)
        while (!m_RestoreWeaponList.empty())
        {
            // Fixme: Scripted weapon gets/sets may be incorrect when this code is being used.
            const SRestoreWeaponItem item = m_RestoreWeaponList.front();
            m_RestoreWeaponList.pop_front();

            // Give our Weapon back after deleting to reload the model
            CWeapon* pWeapon = GiveWeapon(item.eWeaponID, item.dwAmmo, item.bCurrentWeapon);

            // Reset our states
            pWeapon->SetAmmoInClip(item.dwClipAmmo);
            if (item.bCurrentWeapon)
                pWeapon->SetAsCurrentWeapon();
        }

        ValidateRemoteWeapons();
    }
}

//
// Do checks and modifications of controller state
//
void CClientPed::ApplyControllerStateFixes(CControllerState& Current)
{
    CClientVehicle* pVehicle = GetOccupiedVehicle();

    if (m_bIsLocalPlayer)
    {
        // Check if the ped got in fire without the script control
        m_bIsOnFire = m_pPlayerPed->IsOnFire();

        // Do our stealth aiming stuff
        SetStealthAiming(ShouldBeStealthAiming());

        // Process our scripted control settings
        bool bOnFoot = pVehicle ? false : true;
        CClientPad::ProcessAllToggledControls(Current, bOnFoot);
        CClientPad::ProcessSetAnalogControlState(Current, bOnFoot);
    }

    // Is the player stealth aiming?
    if (m_bStealthAiming)
    {
        // Grab our current anim
        std::unique_ptr<CAnimBlendAssociation> pAssoc = GetFirstAnimation();
        if (pAssoc)
        {
            // Check we're not doing any important animations
            eAnimID animId = pAssoc->GetAnimID();
            if (animId == eAnimID::ANIM_ID_WALK || animId == eAnimID::ANIM_ID_RUN || animId == eAnimID::ANIM_ID_IDLE ||
                animId == eAnimID::ANIM_ID_WEAPON_CROUCH || animId == eAnimID::ANIM_ID_KILL_PARTIAL)
            {
                // Are our knife anims loaded?
                std::unique_ptr<CAnimBlock> pBlock = g_pGame->GetAnimManager()->GetAnimationBlock("KNIFE");
                if (pBlock->IsLoaded())
                {
                    // Force the animation
                    BlendAnimation(ANIM_GROUP_STEALTH_KN, ANIM_ID_STEALTH_AIM, 8.0f);
                }
            }
        }
    }

    // Is the player choking?
    if (m_bIsChoking)
    {
        // Grab the choking task
        CTask* pTask = m_pTaskManager->GetTask(TASK_PRIORITY_PHYSICAL_RESPONSE);
        if (pTask && pTask->GetTaskType() == TASK_SIMPLE_CHOKING)
        {
            // Update the task so he keeps on choking until we make him stop
            CTaskSimpleChoking* pTaskChoking = dynamic_cast<CTaskSimpleChoking*>(pTask);
            pTaskChoking->UpdateChoke(m_pPlayerPed, NULL, true);
        }
    }

    unsigned long ulNow = CClientTime::GetTime();
    // MS checks must take into account the gamespeed
    float fSpeedRatio = (1.0f / g_pGame->GetGameSpeed());

    // Remember when we started standing from crouching
    if (m_bWasDucked && m_bWasDucked != IsDucked())
    {
        m_ulLastTimeBeganStand = ulNow;
        m_bWasDucked = false;
    }

    // Remember when we start aiming if we're aiming.
    CTask* pTask = m_pTaskManager->GetTaskSecondary(TASK_SECONDARY_ATTACK);
    if (pTask && pTask->GetTaskType() == TASK_SIMPLE_USE_GUN)
    {
        if (m_ulLastTimeBeganAiming == 0)
            m_ulLastTimeBeganAiming = ulNow;

        if (m_ulLastTimeBeganStand >= ulNow - 200.0f * fSpeedRatio)
        {
            if (!g_pClientGame->IsGlitchEnabled(CClientGame::GLITCH_FASTMOVE))
            {
                // Disable movement keys.  This stops an exploit where players can run
                // with guns shortly after standing
                Current.LeftStickX = 0;
                Current.LeftStickY = 0;
            }
        }

        // Fix to disable the quick cutting of the post deagle shooting animation
        // If we're USE_GUN, but aren't pressing the fire or aim keys we must be
        // in a post-fire state where the player is preparing to move back to
        // a normal stance.  This can normally be cut using the crouch key, so block it
        if (!g_pClientGame->IsGlitchEnabled(CClientGame::GLITCH_CROUCHBUG))
        {
            if (Current.RightShoulder1 == 0 && Current.LeftShoulder1 == 0 && Current.ButtonCircle == 0)
            {
                Current.ShockButtonL = 0;
                // The above checks can be dodged by pressing one of the keys quickly enough, so use a hard
                // timer as well.
                m_ulLastTimeEndedAiming = ulNow;
            }
            // We carry on blocking the crouch key for 600ms after someone has ended aiming
            else if (m_ulLastTimeEndedAiming != 0 && m_ulLastTimeEndedAiming >= ulNow - 600.0f * fSpeedRatio)
            {
                Current.ShockButtonL = 0;
            }
        }
    }
    else
    {
        m_ulLastTimeBeganAiming = 0;
        // If we have the aim button pressed but aren't aiming, we're probably sprinting
        // If we're sprinting with an MP5,Deagle,Fire Extinguisher,Spray can, we shouldnt be able to shoot
        // These weapons are weapons you can run with, but can't run with while aiming
        // This fixes a weapon desync bug involving aiming and sprinting packets arriving simultaneously
        eWeaponType iCurrentWeapon = GetCurrentWeaponType();
        if (Current.RightShoulder1 != 0 &&
            (iCurrentWeapon == 29 || iCurrentWeapon == 24 || iCurrentWeapon == 23 || iCurrentWeapon == 41 || iCurrentWeapon == 42))
        {
            Current.ButtonCircle = 0;
            Current.LeftShoulder1 = 0;
        }
    }

    // Remember when we start the crouching if we're crouching.
    pTask = m_pTaskManager->GetTaskSecondary(TASK_SECONDARY_DUCK);
    if (pTask && pTask->GetTaskType() == TASK_SIMPLE_DUCK)
    {
        m_bWasDucked = true;
        if (m_ulLastTimeBeganCrouch == 0)
            m_ulLastTimeBeganCrouch = ulNow;
        // No longer aiming if we're in the process of crouching
        m_ulLastTimeBeganAiming = 0;
    }
    else
    {
        m_bWasDucked = false;
        m_ulLastTimeBeganCrouch = 0;
    }

    // If we started crouching less than some time ago, make sure we can't jump or sprint.
    // This fixes the exploit both locally and remotely that enables players to abort
    // the crouching animation and shoot quickly with slow shooting weapons. Also fixes
    // the exploit making you able to get crouched without being able to move and shoot
    // with infinite ammo for remote players.
    if (m_ulLastTimeBeganCrouch != 0)
    {
        if (m_ulLastTimeBeganCrouch >= ulNow - 600.0f * fSpeedRatio)
        {
            if (!g_pClientGame->IsGlitchEnabled(CClientGame::GLITCH_FASTFIRE))
            {
                Current.ButtonSquare = 0;
                Current.ButtonCross = 0;
                // Keep fire disabled until the crouch transition has finished. Allowing it here is
                // required for the vanilla fast-fire animation cut exposed by the fastfire glitch.
                Current.ButtonCircle = 0;
                Current.LeftShoulder1 = 0;
            }
            if (!g_pClientGame->IsGlitchEnabled(CClientGame::GLITCH_CROUCHBUG) && m_ulLastTimeBeganCrouch >= ulNow - 400.0f * fSpeedRatio)
            {
                // Disable double crouching (another anim cut). The crouchbug glitch deliberately
                // restores GTA's original second-crouch input during this transition.
                if (g_pClientGame->IsUsingAlternatePulseOrder())
                    Current.ShockButtonL = 255;  // Do this differently if we have changed the pulse order
                else
                    Current.ShockButtonL = 0;
            }
        }
    }
    // If we just started aiming, make sure they dont try and crouch
    else if ((m_ulLastTimeBeganAiming != 0 && m_ulLastTimeBeganAiming >= ulNow - 300.0f * fSpeedRatio) || (ulNow - m_ulLastTimeFired) <= 300.0f * fSpeedRatio)
    {
        if (!g_pClientGame->IsGlitchEnabled(CClientGame::GLITCH_FASTFIRE))
        {
            Current.ShockButtonL = 0;
        }
    }

    // Stop speed advantage by tapping sprint button
    pTask = m_pTaskManager->GetSimplestActiveTask();
    if (pTask && pTask->GetTaskType() == TASK_SIMPLE_PLAYER_ON_FOOT)
    {
        bool bSprintButtonDown = (Current.ButtonCross != 0);

        // Pressed sprint?
        if (bSprintButtonDown && (bSprintButtonDown != m_bWasSprintButtonDown))
        {
            // Check if too soon since since last press
            if ((ulNow - m_ulLastTimeSprintPressed) < 300.0f * fSpeedRatio)
            {
                // On second successive quick press, delay next release
                m_ulBlockSprintReleaseTime = ulNow;
            }
            m_ulLastTimeSprintPressed = ulNow;
        }
        m_bWasSprintButtonDown = bSprintButtonDown;

        // If required, delay sprint button release
        if ((ulNow - m_ulBlockSprintReleaseTime) < 300.0f * fSpeedRatio)
        {
            if (g_pClientGame->GetMiscGameSettings().bAllowFastSprintFix)
                if (!g_pClientGame->IsGlitchEnabled(CClientGame::GLITCH_FASTSPRINT))
                    Current.ButtonCross = 255;
        }
    }

    // Are we working on entering a vehicle?
    pTask = m_pTaskManager->GetTask(TASK_PRIORITY_PRIMARY);
    if (pTask)
    {
        // Entering as driver or passenger?
        int iTaskType = pTask->GetTaskType();
        if (iTaskType == TASK_COMPLEX_ENTER_CAR_AS_DRIVER || iTaskType == TASK_COMPLEX_ENTER_CAR_AS_PASSENGER || iTaskType == TASK_COMPLEX_ENTER_BOAT_AS_DRIVER)
        {
            // Don't allow the aiming key (RightShoulder1)
            // This fixes bug allowing you to run around in aim mode while
            // entering a vehicle both locally and remotely.
            Current.RightShoulder1 = 0;
        }
    }
    // Fix for reloading aborting
    if (GetWeapon()->GetState() == WEAPONSTATE_RELOADING)
    {
        // Disable changing weapons
        Current.DPadUp = 0;
        Current.DPadDown = 0;
        // Disable vehicle entry
        Current.ButtonTriangle = 0;
        // Disable jumping
        Current.ButtonSquare = 0;
        // if we are ducked disable movement (otherwise it will abort reloading)
        if (IsDucked())
        {
            Current.LeftStickX = 0;
            Current.LeftStickY = 0;
        }
    }
    // Fix for crouching the end of animation aborting reload
    CControllerState Previous;
    GetLastControllerState(Previous);
    if (IsDucked() && (Current.LeftStickX == 0 || Current.LeftStickY == 0))
    {
        if (Previous.LeftStickY != 0 || Previous.LeftStickX != 0)
            m_ulLastTimeMovedWhileCrouched = ulNow;
    }
    // Is this the local player?
    if (m_bIsLocalPlayer)
    {
        // * Fix for weapons continuing to fire without any ammo (only needs to be applied locally)
        // Do we have a weapon?
        CWeapon* pWeapon = GetWeapon();
        if (pWeapon)
        {
            // Weapon wielding slot?
            eWeaponSlot slot = pWeapon->GetSlot();
            if (slot != WEAPONSLOT_TYPE_UNARMED && slot != WEAPONSLOT_TYPE_MELEE)
            {
                eWeaponType eWeapon = pWeapon->GetType();
                // No Ammo left?
                float        fSkill = GetStat(g_pGame->GetStats()->GetSkillStatIndex(eWeapon));
                CWeaponStat* pWeaponStat = g_pGame->GetWeaponStatManager()->GetWeaponStatsFromSkillLevel(eWeapon, fSkill);
                if ((pWeapon->GetAmmoInClip() == 0 && pWeaponStat->GetMaximumClipAmmo() > 0) || pWeapon->GetAmmoTotal() == 0)
                {
                    // Make sure our fire key isn't pressed
                    Current.ButtonCircle = 0;
                    Current.LeftShoulder1 = 0;
                }
            }
        }

        // * Fix for warp glitches when sprinting and blocking simultaneously
        // This is applied locally, and prevents you using the backwards key while sprint-blocking
        CTask* pTask = m_pTaskManager->GetSimplestActiveTask();
        if ((pTask && pTask->GetTaskType() == TASK_SIMPLE_PLAYER_ON_FOOT) && (GetWeapon()->GetSlot() == WEAPONSLOT_TYPE_UNARMED) &&
            (Current.RightShoulder1 != 0) && (Current.ButtonSquare != 0) && (Current.ButtonCross != 0))
        {  // We are block jogging
            if (Current.LeftStickY > 0)
                // We're pressing target+jump+sprint+backwards.  Using the backwards key in this situation is prone to bugs, swap it with forwards
                Current.LeftStickY = -Current.LeftStickY;
            else if (Current.LeftStickY == 0)
                // We're pressing target+jump+sprint
                // This causes some sliding, so let's disable this glitchy animation
                Current.ButtonCross = 0;
        }

        // * Check for entering a vehicle whilst using a gun
        pTask = m_pTaskManager->GetTask(TASK_PRIORITY_PRIMARY);
        if (pTask)
        {
            int iTaskType = pTask->GetTaskType();
            if (iTaskType == TASK_COMPLEX_ENTER_CAR_AS_DRIVER || iTaskType == TASK_COMPLEX_ENTER_CAR_AS_PASSENGER)
            {
                if (IsUsingGun())
                {
                    pTask->MakeAbortable(m_pPlayerPed, ABORT_PRIORITY_URGENT, NULL);
                }
            }
        }

        // Make sure crouching
        pTask = m_pTaskManager->GetTaskSecondary(TASK_SECONDARY_DUCK);
        if (pTask && pTask->GetTaskType() == TASK_SIMPLE_DUCK)
        {
            // Check for left/right
            if (Current.LeftStickX != 0)
                m_ulLastTimePressedLeftOrRight = ulNow;

            // If crouching and aiming, don't allow uncrouch button if just pressed left/right, or just aimed
            pTask = m_pTaskManager->GetTaskSecondary(TASK_SECONDARY_ATTACK);
            if ((pTask && pTask->GetTaskType() == TASK_SIMPLE_USE_GUN) || (Current.RightShoulder1 != 0))
                m_ulLastTimeUseGunCrouched = ulNow;

            // Maybe cancel crouch/sprint/jump to prevent quickstand
            if ((ulNow - m_ulLastTimePressedLeftOrRight < 500.f * fSpeedRatio) && (ulNow - m_ulLastTimeUseGunCrouched < 500.f * fSpeedRatio))
            {
                if (!g_pClientGame->IsGlitchEnabled(CClientGame::GLITCH_QUICKSTAND))
                {
                    Current.ShockButtonL = 0;
                    Current.ButtonCross = 0;
                    Current.ButtonSquare = 0;
                }
            }
        }
    }
    else
    {
        // If we are a normal ped
        if (GetType() == eClientEntityType::CCLIENTPED)
        {
            // Do we have a weapon?
            CWeapon* pWeapon = GetWeapon();
            if (pWeapon)
            {
                // Weapon wielding slot?
                eWeaponSlot slot = pWeapon->GetSlot();
                if (slot != WEAPONSLOT_TYPE_UNARMED && slot != WEAPONSLOT_TYPE_MELEE)
                {
                    eWeaponType eWeapon = pWeapon->GetType();
                    // No Ammo left?
                    float        fSkill = GetStat(g_pGame->GetStats()->GetSkillStatIndex(eWeapon));
                    CWeaponStat* pWeaponStat = g_pGame->GetWeaponStatManager()->GetWeaponStatsFromSkillLevel(eWeapon, fSkill);
                    if ((pWeapon->GetAmmoInClip() == 0 && pWeaponStat->GetMaximumClipAmmo() > 0) && pWeapon->GetAmmoTotal() == 0)
                    {
                        pTask = m_pTaskManager->GetTaskSecondary(TASK_SECONDARY_ATTACK);
                        if (pTask && pTask->GetTaskType() == TASK_SIMPLE_USE_GUN)
                        {
                            // disable the fire control state.
                            m_Pad.SetControlState("fire", false);
                        }
                    }
                }
            }
        }
    }
}

float CClientPed::GetCurrentRotation()
{
    if (IsFrozen())
    {
        CVector vecRotation = m_matFrozen.GetRotation();
        return vecRotation.fZ;
    }

    if (m_pPlayerPed)
    {
        return m_pPlayerPed->GetCurrentRotation();
    }
    return m_fCurrentRotation;
}

void CClientPed::SetCurrentRotation(float fRotation, bool bIncludeTarget)
{
    if (m_pPlayerPed)
    {
        m_pPlayerPed->SetCurrentRotation(fRotation);
        m_fCurrentRotation = fRotation;
        if (bIncludeTarget)
        {
            m_pPlayerPed->SetTargetRotation(fRotation);
            m_fTargetRotation = fRotation;
        }
    }
    else
    {
        // The ped model is still not loaded
        m_fCurrentRotation = fRotation;
        if (bIncludeTarget)
            m_fTargetRotation = fRotation;
    }

    // Always update m_Matrix rotation so m_matFrozen gets correct value in SetFrozen
    CVector vecRotation = m_Matrix.GetRotation();
    vecRotation.fZ = fRotation;
    m_Matrix.SetRotation(vecRotation);

    // Ordinary GTA processing consumes fCurrentRotation and updates the live
    // matrix. Remote replicas are physically frozen to prevent observer-only
    // gravity while collision is unavailable, so that consumption never
    // happens. Apply only the interpolated orientation while that fence is
    // active; m_Matrix already carries the authoritative network position.
    if (m_pPlayerPed && m_remoteReplicaPhysicsFenceActive)
        m_pPlayerPed->SetMatrix(&m_Matrix);
}

void CClientPed::SetTargetRotation(float fRotation)
{
    if (m_pPlayerPed)
    {
        m_pPlayerPed->SetTargetRotation(fRotation);
    }
    m_fTargetRotation = fRotation;
    SetCurrentRotation(fRotation);
}

void CClientPed::SetTargetRotation(unsigned long ulDelay, std::optional<float> rotation, std::optional<float> cameraRotation)
{
    m_ulBeginRotationTime = CClientTime::GetTime();
    m_ulEndRotationTime = m_ulBeginRotationTime + ulDelay;
    if (rotation.has_value())
    {
        m_fBeginRotation = (m_pPlayerPed) ? m_pPlayerPed->GetCurrentRotation() : m_fCurrentRotation;
        m_fTargetRotationA = rotation.value();
        if (m_remoteStreamInFenceActive)
        {
            m_remoteStreamInFenceCurrentRotation = rotation.value();
            m_remoteStreamInFenceTargetRotation = rotation.value();
            CVector fenceRotation = m_remoteStreamInFenceMatrix.GetRotation();
            fenceRotation.fZ = rotation.value();
            m_remoteStreamInFenceMatrix.SetRotation(fenceRotation);
        }
    }
    if (cameraRotation.has_value())
    {
        m_fBeginCameraRotation = GetCameraRotation();
        m_fTargetCameraRotation = cameraRotation.value();
    }
}

void CClientPed::SetNativeAIRotationTelemetryNetworkSample(const SNativeAITelemetryPacket& packet, unsigned long receivedAt, unsigned long receiveInterval,
                                                           unsigned long spatialSyncRate, bool headingApplied) noexcept
{
    if (!packet.hasHeading)
        return;

    m_nativeAIRotationNetworkSample.localSequence = packet.localSequence;
    m_nativeAIRotationNetworkSample.sampleKey = packet.sampleKey;
    m_nativeAIRotationNetworkSample.receivedAt = receivedAt;
    m_nativeAIRotationNetworkSample.receiveInterval = receiveInterval;
    m_nativeAIRotationNetworkSample.spatialSyncRate = spatialSyncRate;
    m_nativeAIRotationNetworkSample.heading = packet.heading;
    m_nativeAIRotationNetworkSample.valid = true;
    m_nativeAIRotationNetworkSample.applied = headingApplied;
}

void CClientPed::RecordNativeAIRotationTelemetryPostProcess() noexcept
{
    if (!CNativeAITelemetry::IsEnabled(ENativeAITelemetryCategory::PRESENTATION) || !m_pPlayerPed || m_bIsLocalPlayer || GetType() != CCLIENTPED)
        return;

    SString    runId;
    bool       ambientTraffic = false;
    const bool harnessActor = GetCustomDataString(CStringName("neon:nativeAIRunId"), runId, false);
    GetCustomDataBool(CStringName("neon:ambientPedTraffic"), ambientTraffic, false);
    if (!harnessActor && !ambientTraffic)
        return;

    const unsigned long now = CClientTime::GetTime();
    if (m_nativeAIRotationTelemetryNextSampleAt != 0 && now < m_nativeAIRotationTelemetryNextSampleAt)
        return;
    // Twenty samples per second retain frame-after-apply evidence while
    // leaving ample room under the shared JSONL writer's rate limit.
    m_nativeAIRotationTelemetryNextSampleAt = now + 50;

    CVector matrixRotation;
    GetRotationRadiansNew(matrixRotation);

    SNativeAIRotationTelemetry rotation;
    rotation.currentHeading = GetCurrentRotation();
    rotation.targetHeading = m_pPlayerPed->GetTargetRotation();
    rotation.matrixHeading = matrixRotation.fZ;
    rotation.interpolationBeginHeading = m_fBeginRotation;
    rotation.interpolationTargetHeading = m_fTargetRotationA;
    rotation.interpolationActive = m_ulBeginRotationTime != 0 && now < m_ulEndRotationTime;
    rotation.interpolationBeginMs = m_ulBeginRotationTime;
    rotation.interpolationEndMs = m_ulEndRotationTime;
    rotation.hasNetworkSample = m_nativeAIRotationNetworkSample.valid;
    if (rotation.hasNetworkSample)
    {
        rotation.lastReceiveSequence = m_nativeAIRotationNetworkSample.localSequence;
        rotation.lastReceiveSampleKey = m_nativeAIRotationNetworkSample.sampleKey;
        rotation.lastReceiveAtMs = m_nativeAIRotationNetworkSample.receivedAt;
        rotation.sampleAgeMs = now - m_nativeAIRotationNetworkSample.receivedAt;
        rotation.receiveIntervalMs = m_nativeAIRotationNetworkSample.receiveInterval;
        rotation.spatialSyncRateMs = m_nativeAIRotationNetworkSample.spatialSyncRate;
        rotation.networkSampleHeading = m_nativeAIRotationNetworkSample.heading;
        rotation.networkHeadingApplied = m_nativeAIRotationNetworkSample.applied;
    }
    rotation.remoteStreamInFence = m_remoteStreamInFenceActive;
    rotation.remoteReplicaPhysicsFence = m_remoteReplicaPhysicsFenceActive;
    rotation.nativeCollisionAuthorityFence = m_nativeCollisionAuthorityFence.active;
    rotation.ownerCollisionFence = m_nativeAmbientOwnerCollisionFence.active;
    rotation.animationPresentationActive = m_nativeTaskAnimationPresentationActive;
    rotation.animationMode = m_nativeTaskAnimationPresentation.data.uiMode;
    rotation.animationGroup = m_nativeTaskAnimationPresentation.data.usAnimGroup;
    rotation.animationId = m_nativeTaskAnimationPresentation.data.usAnimId;
    CNativeAITelemetry::RecordPedRotationEvent("rotation_post_process", this, rotation);
}

// Temporary
#include "../mods/deathmatch/logic/CClientGame.h"
#include <enums/VehicleType.h>
extern CClientGame* g_pClientGame;

void CClientPed::Interpolate()
{
    // Grab the current time
    unsigned long ulCurrentTime = CClientTime::GetTime();

    // Do we have interpolation data for aiming?
    if (m_ulBeginAimTime != 0)
    {
        // We're not at the end of the interpolation?
        if (ulCurrentTime < m_ulTargetAimTime)
        {
            // Interpolate the aiming
            float fDeltaTime = float(m_ulTargetAimTime - m_ulBeginAimTime);
            float fDeltaX = m_fTargetAimX - m_fBeginAimX;
            float fDeltaY = m_fTargetAimY - m_fBeginAimY;
            float fProgress = float(ulCurrentTime - m_ulBeginAimTime);
            m_shotSyncData->m_fArmDirectionY = m_fBeginAimY + (fDeltaY / fDeltaTime * fProgress);

            // Hack for the wrap-around (the edge seems varying for different weapons...)
            if (fDeltaX > 5.0f || fDeltaX < -5.0f)
            {
                m_shotSyncData->m_fArmDirectionX = m_fTargetAimX;
            }
            else
            {
                m_shotSyncData->m_fArmDirectionX = m_fBeginAimX + (fDeltaX / fDeltaTime * fProgress);
            }
        }
        else
        {
            // Force the aiming to the final target position
            m_shotSyncData->m_fArmDirectionX = m_fTargetAimX;
            m_shotSyncData->m_fArmDirectionY = m_fTargetAimY;
            m_ulBeginAimTime = 0;

            // Force the hands to the correct "up" position for akimbos
            m_remoteDataStorage->SetAkimboTargetUp(m_bTargetAkimboUp);
        }
    }

    // Don't interpolate rotation and position if we're working on getting in/out of a vehicle, or are attached to something
    if (!m_bForceGettingIn && !m_bForceGettingOut && !m_pOccupiedVehicle && !m_pAttachedToEntity && !m_remoteStreamInFenceActive)
    {
        // We have interpolation data for rotation?
        if (m_ulBeginRotationTime != 0)
        {
            // We're not at the end?
            if (ulCurrentTime < m_ulEndRotationTime)
            {
                const float fDelta = GetOffsetRadians(m_fBeginRotation, m_fTargetRotationA);

                // Hack for the wrap-around (the edge seems to be varying...)
                if (fDelta < -M_PI || fDelta > M_PI)
                {
                    SetCurrentRotation(m_fTargetRotationA);
                    SetCameraRotation(m_fTargetCameraRotation);
                }
                else
                {
                    // Interpolate the player rotation
                    const float fDeltaTime = float(m_ulEndRotationTime - m_ulBeginRotationTime);
                    const float fCameraDelta = GetOffsetRadians(m_fBeginCameraRotation, m_fTargetCameraRotation);
                    const float fProgress = float(ulCurrentTime - m_ulBeginRotationTime);
                    const float fNewRotation = m_fBeginRotation + fDelta * (fProgress / fDeltaTime);
                    const float fNewCameraRotation = m_fBeginCameraRotation + fCameraDelta * (fProgress / fDeltaTime);

                    SetCurrentRotation(fNewRotation);
                    SetCameraRotation(fNewCameraRotation);
                }
            }
            else
            {
                // Set the rotation to the final target
                SetCurrentRotation(m_fTargetRotationA);
                SetCameraRotation(m_fTargetCameraRotation);
                m_ulBeginRotationTime = 0;
            }
        }

        UpdateTargetPosition();
    }

    // Interpolate the source and target vector for aiming
    if (m_ulBeginTarget != 0)
    {
        // We're not at the end?
        if (ulCurrentTime < m_ulEndTarget)
        {
            // Grab the amount of time and how much of it we've progressed
            float fDeltaTime = float(m_ulEndTarget - m_ulBeginTarget);
            float fProgress = float(ulCurrentTime - m_ulBeginTarget);
            float fTime = fProgress / fDeltaTime;

            // Grab the delta source vector and interpolate it with the time
            CVector vecDelta = m_vecTargetSource - m_vecBeginSource;
            m_shotSyncData->m_vecShotOrigin = m_vecBeginSource + (vecDelta * fTime);

            // Grab the radius of the target circle
            float fRadius = DistanceBetweenPoints3D(m_vecTargetSource, m_vecTargetTarget);

            // Interpolate it with the time
            CVector vecInterpolateAngle = m_vecTargetInterpolateAngle * fTime;

            // Convert the angles back to the interpolated target position
            CVector vecInterpolated;
            vecInterpolated.fX = cos(m_vecBeginTargetAngle.fX + vecInterpolateAngle.fX) * fRadius + m_shotSyncData->m_vecShotOrigin.fX;
            vecInterpolated.fY = cos(m_vecBeginTargetAngle.fY + vecInterpolateAngle.fY) * fRadius + m_shotSyncData->m_vecShotOrigin.fY;
            vecInterpolated.fZ = cos(m_vecBeginTargetAngle.fZ + vecInterpolateAngle.fZ) * fRadius + m_shotSyncData->m_vecShotOrigin.fZ;

            // Set it
            m_shotSyncData->m_vecShotTarget = vecInterpolated;

            // Also set this as the target position for akimbo guns
            m_remoteDataStorage->SetAkimboTarget(vecInterpolated);
        }
        else
        {
            m_shotSyncData->m_vecShotOrigin = m_vecTargetSource;
            m_shotSyncData->m_vecShotTarget = m_vecTargetTarget;
            m_ulBeginTarget = 0;

            // Also set this as the target position for akimbo guns
            m_remoteDataStorage->SetAkimboTarget(m_vecTargetTarget);
        }
    }
    // Make sure we're using our origin vector
    m_shotSyncData->m_bUseOrigin = true;
}

void CClientPed::UpdateKeysync(bool bCleanup)
{
    // TODO: we should ignore any 'old' keysyncs and set only the latest

    // Got any keysyncs to apply?
    if (m_SyncBuffer.size() > 0)
    {
        // Time to apply it?
        unsigned long ulCurrentTime = 0;
        if (!bCleanup)
            ulCurrentTime = CClientTime::GetTime();

        // Get the sync data at the front
        SDelayedSyncData* pData = m_SyncBuffer.front();

        // Is the front data valid
        if (pData)
        {
            // Check the front data's time (if this isn't valid, nothing else will be either so just leave it in the buffer)
            if (bCleanup || ulCurrentTime >= pData->ulTime)
            {
                // Loop through until one of the conditions are caught
                do
                {
                    // Remove it from the list straight away so we don't end up picking it up again
                    m_SyncBuffer.pop_front();

                    switch (pData->ucType)
                    {
                        case DELAYEDSYNC_KEYSYNC:
                        {
                            SetControllerState(pData->State);
                            Duck(pData->bDucking);
                            break;
                        }
                        case DELAYEDSYNC_CHANGEWEAPON:
                        {
                            if (pData->slot > WEAPONSLOT_TYPE_UNARMED)
                            {
                                // Grab the current weapon the player has
                                CWeapon*    pPlayerWeapon = GetWeapon();
                                eWeaponSlot eCurrentSlot = pData->slot;
                                if (!pPlayerWeapon || pPlayerWeapon->GetSlot() != eCurrentSlot || GetRealOccupiedVehicle())
                                {
                                    CWeapon* pSlotWeapon = GetWeapon(eCurrentSlot);
                                    if (pSlotWeapon)
                                    {
                                        pPlayerWeapon = GiveWeapon(pSlotWeapon->GetType(), pData->usWeaponAmmo, true);
                                    }
                                }

                                // Give it unlimited ammo, set the ammo in clip and weapon state
                                if (pPlayerWeapon)
                                {
                                    pPlayerWeapon->SetAmmoTotal(9999);
                                    // r1154 - Commented out below as it was causing reload animation desync (Issue #4503). Although it must have been there for
                                    // a reason...
                                    if (/*pData->usWeaponAmmo < pPlayerWeapon->GetAmmoInClip () &&*/ pPlayerWeapon->GetState() != WEAPONSTATE_RELOADING)
                                        pPlayerWeapon->SetAmmoInClip(pData->usWeaponAmmo);
                                }
                            }
                            else
                            {
                                SetCurrentWeaponSlot(WEAPONSLOT_TYPE_UNARMED);
                            }
                            break;
                        }
                        case DELAYEDSYNC_MOVESPEED:
                        {
                            SetMoveSpeed(pData->vecTarget);
                            break;
                        }
                    }

                    // Delete the data
                    delete pData;

                    // Reset the current sync data pointer
                    pData = NULL;

                    // Loop through until we have a new valid sync data, or we don't have any data left to process
                    while (pData == NULL && m_SyncBuffer.size() > 0)
                    {
                        // Get the next sync data at the front
                        pData = m_SyncBuffer.front();

                        // Check to see if the data is invalid
                        if (!pData)
                        {
                            // It is, so remove it from the list
                            m_SyncBuffer.pop_front();
                        }
                    }
                } while (pData && (bCleanup || ulCurrentTime >= pData->ulTime));
            }
        }
    }
}

void CClientPed::_CreateModel()
{
    // Replace the loaded model info with the model we're going to load and
    // add a reference to it.
    m_pLoadedModelInfo = m_pModelInfo;
    m_pLoadedModelInfo->ModelAddRef(BLOCKING, "CClientPed::_CreateModel");

    // Create the new ped
    m_pPlayerPed = dynamic_cast<CPlayerPed*>(g_pGame->GetPools()->AddPed(this, m_ulModel));
    if (m_pPlayerPed)
    {
        // Put our pointer in the stored data and update the remote data with the new model pointer
        m_pPlayerPed->SetStoredPointer(this);

        // MTA constructs both players and script peds as CPlayerPed. Remember
        // the element identity separately so the one IsPlayer check inside
        // GTA's choking task can retain ordinary CPed behaviour for NPCs.
        m_pPlayerPed->SetNativeChokingUsesNonPlayerBehavior(GetType() == CCLIENTPED);
        // Script peds are also CPlayerPed wrappers, but native jump tasks must
        // use GTA's CPed force, climb, voice and landing paths. The multiplayer
        // hook applies this bit only at audited jump/fall callsites.
        m_pPlayerPed->SetNativeJumpUsesNonPlayerBehavior(GetType() == CCLIENTPED);

        // Script peds are reconstructed as CPlayerPed instances whenever they
        // stream in. Reapply their persisted actor classification before GTA
        // starts evaluating ambient task transitions on the new instance.
        ApplyMissionActorState();
        ApplyStoryProtectionState();
        ApplySuffersCriticalHitsState();
        ApplyCanBeDraggedOutState();
        ApplyOnlyDamagedByPlayerState();
        ApplyStayInSamePlaceState();
        ApplyNeverTargetedState();
        ApplyScriptPhysicalProofsState();

        g_pMultiplayer->AddRemoteDataStorage(m_pPlayerPed, m_remoteDataStorage);

        // Grab the task manager
        m_pTaskManager = m_pPlayerPed->GetPedIntelligence()->GetTaskManager();

        // Validate
        m_pManager->RestoreEntity(this);

        // Jump straight to the target position if we have one
        if (HasTargetPosition())
        {
            CVector vecPosition = m_interp.pos.vecTarget;
            if (m_interp.pTargetOriginSource)
            {
                CVector vecOrigin;
                m_interp.pTargetOriginSource->GetPosition(vecOrigin);
                vecPosition += vecOrigin;
            }
            m_Matrix.vPos = vecPosition;
        }

        const bool remoteNonSyncer = GetType() == CCLIENTPED && !m_bIsLocalPlayer && !m_bIsSyncing;
        if (remoteNonSyncer)
        {
            // The create RPC is the only network truth available before the
            // first PED_SYNC. Seed each missing component independently: an
            // earlier position-only RPC must not leave heading or velocity at
            // their zero-initialized defaults.
            if (!m_remoteAuthoritativeTransform.positionValid)
            {
                m_remoteAuthoritativeTransform.position = m_Matrix.vPos;
                m_remoteAuthoritativeTransform.positionValid = true;
                m_remoteAuthoritativeTransform.restoreAllowed = true;
            }
            if (!m_remoteAuthoritativeTransform.moveSpeedValid)
            {
                m_remoteAuthoritativeTransform.moveSpeed = m_vecMoveSpeed;
                m_remoteAuthoritativeTransform.moveSpeedValid = true;
            }
            if (!m_remoteAuthoritativeTransform.rotationValid)
            {
                m_remoteAuthoritativeTransform.rotation = m_fCurrentRotation;
                m_remoteAuthoritativeTransform.rotationValid = true;
            }
        }

        const bool syncedAnimationOwnsTransform = HasSyncedAnim() && GetAnimationCache().bUpdatePosition;
        const bool restoreRemoteAuthoritativeTransform =
            remoteNonSyncer && m_remoteAuthoritativeTransform.restoreAllowed && !GetOccupiedVehicle() && !GetOccupyingVehicle() && !IsGettingIntoVehicle() &&
            !IsGettingOutOfVehicle() && !GetAttachedTo() && !syncedAnimationOwnsTransform &&
            !SNativeTaskAnimationPresentationSync::IsPhysicalMode(m_nativeTaskAnimationPresentation.data.uiMode) && !m_nativeTaskAirbornePresentationActive;
        if (restoreRemoteAuthoritativeTransform)
        {
            if (m_remoteAuthoritativeTransform.positionValid)
                m_Matrix.vPos = m_remoteAuthoritativeTransform.position;
            if (m_remoteAuthoritativeTransform.rotationValid)
            {
                m_fCurrentRotation = m_remoteAuthoritativeTransform.rotation;
                m_fTargetRotation = m_remoteAuthoritativeTransform.rotation;
                CVector rotation = m_Matrix.GetRotation();
                rotation.fZ = m_remoteAuthoritativeTransform.rotation;
                m_Matrix.SetRotation(rotation);
            }
            if (m_remoteAuthoritativeTransform.moveSpeedValid)
                m_vecMoveSpeed = m_remoteAuthoritativeTransform.moveSpeed;
        }

        // Restore any settings
        m_pPlayerPed->SetLanding(false);
        m_pPlayerPed->SetMatrix(&m_Matrix);
        m_pPlayerPed->SetCurrentRotation(m_fCurrentRotation);
        m_pPlayerPed->SetTargetRotation(m_fTargetRotation);
        m_pPlayerPed->SetMoveSpeed(m_vecMoveSpeed);
        m_pPlayerPed->SetTurnSpeed(&m_vecTurnSpeed);
        // Collision ownership must only be evaluated after the reconstructed
        // CPlayerPed has received its authoritative transform. Evaluating it
        // earlier arms the safety fence around GTA's temporary creation
        // origin and then pins the ped at (0, 0, 0).
        ApplyNativeEventProfileState();
        Duck(m_bDucked);
        SetWearingGoggles(m_bWearingGoggles);
        m_pPlayerPed->SetVisible(m_bVisible);
        m_pPlayerPed->SetUsesCollision(m_bUsesCollision);
        m_pPlayerPed->SetHealth(m_fHealth);
        m_pPlayerPed->SetArmor(m_armor);
        m_pPlayerPed->SetLighting(m_fLighting);
        WorldIgnore(m_bWorldIgnored);

        // PED_SYNC continues updating the element cache while a remote ped is
        // streamed out. Hold that authoritative transform while GTA loads the
        // local collision sector; otherwise a stationary owner has no delta
        // to resend and this new game instance can fall for several frames.
        ArmRemoteStreamInTransformFence();

        // Set remote players to not fall off bikes locally, let them decide
        if (m_bIsLocalPlayer)
            SetCanBeKnockedOffBike(m_bCanBeKnockedOffBike);
        else
            SetCanBeKnockedOffBike(false);

        // Restore their weapons
        for (int i = 0; i < (int)WEAPONSLOT_MAX; i++)
        {
            if (m_WeaponTypes[i] != WEAPONTYPE_UNARMED)
            {
                bool bSetAsCurrent = (i == m_CurrentWeaponSlot);
                GiveWeapon(m_WeaponTypes[i], m_usWeaponAmmo[i], bSetAsCurrent);
            }
        }

        m_pPlayerPed->SetCurrentWeaponSlot(m_CurrentWeaponSlot);
        m_pPlayerPed->SetFightingStyle(m_FightingStyle, 6);
        m_pPlayerPed->SetMoveAnim(m_bUseNativeWalkingStyle ? MOVE_NATIVE : m_MoveAnim);
        SetHasJetPack(m_bHasJetPack);
        SetInterior(m_ucInterior);
        SetAlpha(m_ucAlpha);
        SetChoking(m_bIsChoking);
        SetSunbathing(m_bSunbathing, false);
        SetHeadless(m_bHeadless);
        SetOnFire(m_bIsOnFire);
        SetSpeechEnabled(m_bSpeechEnabled);
        SetBleeding(m_bBleeding);

        // Rebuild the player if it's CJ. So we get the clothes.
        RebuildModel();

        // Reattach to an entity + any entities attached to this
        ReattachEntities();

        // Warp it into a vehicle, if necessary
        if (m_pOccupiedVehicle)
            WarpIntoVehicle(m_pOccupiedVehicle, m_uiOccupiedVehicleSeat);

        // Are we dead?
        if (m_fHealth == 0.0f)
        {
            Kill(WEAPONTYPE_UNARMED, 0, false, true);
        }

        // Are we still playing a animation?
        if (m_pAnimationBlock && IsAnimationInProgress())
        {
            if (m_bisCurrentAnimationCustom)
            {
                m_bisNextAnimationCustom = true;
            }

            RunAnimationFromCache();
        }

        // Set the voice that corresponds to our model
        short sVoiceType, sVoiceID;
        m_pModelInfo->GetVoice(&sVoiceType, &sVoiceID);
        SetVoice(sVoiceType, sVoiceID);

        // Tell the streamer we created the player
        NotifyCreate();
    }
    else
    {
        // Remove the reference again
        m_pLoadedModelInfo->RemoveRef();
        m_pLoadedModelInfo = NULL;

        // Tell the streamed we were unable to create
        NotifyUnableToCreate();
    }
}

void CClientPed::_CreateLocalModel()
{
    // Init the local player and grab the pointers
    g_pGame->InitLocalPlayer(this);
    m_pPlayerPed = dynamic_cast<CPlayerPed*>(g_pGame->GetPools()->GetPedFromRef((DWORD)1));

    if (m_pPlayerPed)
    {
        m_pTaskManager = m_pPlayerPed->GetPedIntelligence()->GetTaskManager();

        // Put our pointer in its stored pointer
        m_pPlayerPed->SetStoredPointer(this);

        // Add a reference to the model we're using
        m_pLoadedModelInfo = m_pModelInfo;
        m_pLoadedModelInfo->ModelAddRef(BLOCKING, "CClientPed::_CreateLocalModel");

        // Make sure we are CJ
        if (m_pPlayerPed->GetModelIndex() != m_ulModel)
        {
            m_pPlayerPed->SetModelIndex(m_ulModel);
        }

        m_pPlayerPed->SetLanding(false);

        // Give him the default fighting style
        m_pPlayerPed->SetFightingStyle(m_FightingStyle, 6);
        m_pPlayerPed->SetMoveAnim(m_bUseNativeWalkingStyle ? MOVE_NATIVE : m_MoveAnim);
        SetHasJetPack(m_bHasJetPack);

        // Rebuild him so he gets his clothes
        RebuildModel();

        // Validate
        m_pManager->RestoreEntity(this);

        // Tell the streamer we created the player
        NotifyCreate();
    }
}

void CClientPed::ArmRemoteStreamInTransformFence()
{
    const bool syncedAnimationOwnsTransform = HasSyncedAnim() && GetAnimationCache().bUpdatePosition;
    if (!m_pPlayerPed || GetType() != CCLIENTPED || m_bIsLocalPlayer || m_bIsSyncing || IsFrozen() || !m_remoteAuthoritativeTransform.restoreAllowed ||
        GetOccupiedVehicle() || GetOccupyingVehicle() || IsGettingIntoVehicle() || IsGettingOutOfVehicle() || GetAttachedTo() || syncedAnimationOwnsTransform ||
        SNativeTaskAnimationPresentationSync::IsPhysicalMode(m_nativeTaskAnimationPresentation.data.uiMode) || m_nativeTaskAirbornePresentationActive)
    {
        return;
    }

    CVector localPosition;
    GetPosition(localPosition);
    m_remoteStreamInFenceMatrix = m_Matrix;
    m_remoteStreamInFenceMatrix.vPos = m_remoteAuthoritativeTransform.position;
    m_remoteStreamInFenceCurrentRotation = m_remoteAuthoritativeTransform.rotation;
    m_remoteStreamInFenceTargetRotation = m_remoteAuthoritativeTransform.rotation;
    CVector fenceRotation = m_remoteStreamInFenceMatrix.GetRotation();
    fenceRotation.fZ = m_remoteAuthoritativeTransform.rotation;
    m_remoteStreamInFenceMatrix.SetRotation(fenceRotation);
    m_remoteStreamInFenceStartedAt = CClientTime::GetTime();
    m_remoteStreamInFenceNextProbeAt = m_remoteStreamInFenceStartedAt;
    m_remoteStreamInFencePreviousStaticWaitingForCollision = m_pPlayerPed->IsStaticWaitingForCollision();
    m_remoteStreamInFenceActive = true;
    m_pPlayerPed->SetStaticWaitingForCollision(true);
    m_pPlayerPed->SetFrozen(true);
    m_pPlayerPed->SetMatrix(&m_remoteStreamInFenceMatrix);
    m_pPlayerPed->SetCurrentRotation(m_remoteStreamInFenceCurrentRotation);
    m_pPlayerPed->SetTargetRotation(m_remoteStreamInFenceTargetRotation);
    m_pPlayerPed->SetMoveSpeed(m_remoteAuthoritativeTransform.moveSpeedValid ? m_remoteAuthoritativeTransform.moveSpeed : CVector());

    if (CColStore* collisionStore = g_pGame->GetCollisionStore())
        collisionStore->RequestCollision(m_remoteStreamInFenceMatrix.vPos, m_ucInterior);

    if (IsRemotePedStreamInTransformFenceTraceEnabled())
    {
        g_pCore->GetConsole()->Printf(
            "[remote-ped-stream-fence][arm] ped=%u model=%lu source=authoritative pos=(%.3f,%.3f,%.3f) local=(%.3f,%.3f,%.3f) heading=%.3f", GetID().Value(),
            GetModel(), m_remoteStreamInFenceMatrix.vPos.fX, m_remoteStreamInFenceMatrix.vPos.fY, m_remoteStreamInFenceMatrix.vPos.fZ, localPosition.fX,
            localPosition.fY, localPosition.fZ, m_remoteStreamInFenceCurrentRotation);
    }
}

void CClientPed::UpdateRemoteStreamInTransformFence()
{
    if (!m_remoteStreamInFenceActive)
    {
        // Collision can unload while a remote ped is still inside the entity
        // streaming radius. Catch that transition before GTA integrates
        // gravity and before StreamOut can persist a local observer fall.
        const unsigned long now = CClientTime::GetTime();
        const bool          syncedAnimationOwnsTransform = HasSyncedAnim() && GetAnimationCache().bUpdatePosition;
        if (!m_pPlayerPed || GetType() != CCLIENTPED || m_bIsLocalPlayer || m_bIsSyncing || IsFrozen() || !m_remoteAuthoritativeTransform.restoreAllowed ||
            GetOccupiedVehicle() || GetOccupyingVehicle() || IsGettingIntoVehicle() || IsGettingOutOfVehicle() || GetAttachedTo() ||
            syncedAnimationOwnsTransform || SNativeTaskAnimationPresentationSync::IsPhysicalMode(m_nativeTaskAnimationPresentation.data.uiMode) ||
            m_nativeTaskAirbornePresentationActive || !m_remoteAuthoritativeTransform.positionValid || now < m_remoteStreamInFenceRetryAt ||
            now < m_remoteStreamInFenceNextProbeAt)
        {
            return;
        }

        m_remoteStreamInFenceNextProbeAt = now + REMOTE_PED_STREAM_IN_TRANSFORM_FENCE_MINIMUM;
        CColStore* collisionStore = g_pGame->GetCollisionStore();
        if (collisionStore && !collisionStore->HasCollisionLoaded(m_remoteAuthoritativeTransform.position, m_ucInterior))
            ArmRemoteStreamInTransformFence();
        return;
    }

    const bool syncedAnimationOwnsTransform = HasSyncedAnim() && GetAnimationCache().bUpdatePosition;
    if (!m_pPlayerPed || m_bIsLocalPlayer || m_bIsSyncing || IsFrozen() || !m_remoteAuthoritativeTransform.restoreAllowed || GetOccupiedVehicle() ||
        GetOccupyingVehicle() || IsGettingIntoVehicle() || IsGettingOutOfVehicle() || GetAttachedTo() || syncedAnimationOwnsTransform ||
        SNativeTaskAnimationPresentationSync::IsPhysicalMode(m_nativeTaskAnimationPresentation.data.uiMode) || m_nativeTaskAirbornePresentationActive)
    {
        ClearRemoteStreamInTransformFence("state_changed");
        return;
    }

    const unsigned long elapsed = CClientTime::GetTime() - m_remoteStreamInFenceStartedAt;
    CColStore*          collisionStore = g_pGame->GetCollisionStore();
    const bool          collisionReady = collisionStore && collisionStore->HasCollisionLoaded(m_remoteStreamInFenceMatrix.vPos, m_ucInterior);
    if (!collisionStore || (elapsed >= REMOTE_PED_STREAM_IN_TRANSFORM_FENCE_MINIMUM && collisionReady) ||
        elapsed >= REMOTE_PED_STREAM_IN_TRANSFORM_FENCE_TIMEOUT)
    {
        ClearRemoteStreamInTransformFence(elapsed >= REMOTE_PED_STREAM_IN_TRANSFORM_FENCE_TIMEOUT ? "timeout" : "collision_ready", true);
        return;
    }

    // Do not use CClientPed::SetFrozen here: it removes native primary and
    // response tasks. The physical-only freeze guarantees that an entity
    // already present in GTA's moving list cannot integrate gravity, while
    // the native collision-waiting flag covers later moving-list insertion.
    // Preserve velocity for the eventual release.
    m_pPlayerPed->SetMatrix(&m_remoteStreamInFenceMatrix);
    m_pPlayerPed->SetCurrentRotation(m_remoteStreamInFenceCurrentRotation);
    m_pPlayerPed->SetTargetRotation(m_remoteStreamInFenceTargetRotation);
}

void CClientPed::ClearRemoteStreamInTransformFence(const char* reason, bool commitAuthoritativeTransform)
{
    if (!m_remoteStreamInFenceActive)
        return;

    const unsigned long elapsed = CClientTime::GetTime() - m_remoteStreamInFenceStartedAt;
    if (m_pPlayerPed)
    {
        if (commitAuthoritativeTransform)
        {
            // A PED_SYNC received while the fence was active may have refreshed
            // the target after the previous pulse. Commit that exact final
            // target before unfreezing, then discard interpolation timers whose
            // errors were measured from an older local transform.
            m_Matrix = m_remoteStreamInFenceMatrix;
            m_fCurrentRotation = m_remoteStreamInFenceCurrentRotation;
            m_fTargetRotation = m_remoteStreamInFenceTargetRotation;
            m_pPlayerPed->SetMatrix(&m_Matrix);
            m_pPlayerPed->SetCurrentRotation(m_fCurrentRotation);
            m_pPlayerPed->SetTargetRotation(m_fTargetRotation);
            if (m_remoteAuthoritativeTransform.moveSpeedValid)
            {
                m_vecMoveSpeed = m_remoteAuthoritativeTransform.moveSpeed;
                m_pPlayerPed->SetMoveSpeed(m_vecMoveSpeed);
            }
        }
        RemoveTargetPosition();
        m_ulBeginRotationTime = 0;
        m_pPlayerPed->SetStaticWaitingForCollision(m_remoteStreamInFencePreviousStaticWaitingForCollision);
        ApplyPhysicalFreezeState();
        // GTA's stock mission collision gate puts a physical back on the
        // moving list after clearing bStaticWaitingForCollision. The fence can
        // be armed after construction, so AddToMovingList is intentionally a
        // safe no-op when the ped was already linked and a required resume
        // when construction left it out of the list.
        if (!m_remoteStreamInFencePreviousStaticWaitingForCollision && !m_pPlayerPed->IsStatic())
            m_pPlayerPed->AddToMovingList();
    }
    m_remoteStreamInFenceActive = false;
    m_remoteStreamInFenceRetryAt = CClientTime::GetTime() + REMOTE_PED_STREAM_IN_TRANSFORM_FENCE_MINIMUM;

    if (IsRemotePedStreamInTransformFenceTraceEnabled())
    {
        g_pCore->GetConsole()->Printf("[remote-ped-stream-fence][release] ped=%u model=%lu reason=%s elapsed=%lu", GetID().Value(), GetModel(),
                                      reason ? reason : "unknown", elapsed);
    }
}

void CClientPed::UpdateNativeAmbientOwnerCollisionFence()
{
    auto&      state = m_nativeAmbientOwnerCollisionFence;
    const auto clearUnloadedCollisionAirborneTask = [this]()
    {
        if (!m_pTaskManager || !m_pPlayerPed)
            return;
        for (const int priority :
             {TASK_PRIORITY_PHYSICAL_RESPONSE, TASK_PRIORITY_EVENT_RESPONSE_TEMP, TASK_PRIORITY_EVENT_RESPONSE_NONTEMP, TASK_PRIORITY_PRIMARY})
        {
            if (m_pTaskManager->FindTaskByType(priority, TASK_COMPLEX_IN_AIR_AND_LAND))
                KillTask(priority, true);
        }
        m_pPlayerPed->SetNativeTaskAirbornePresentationState(false, false);
        m_nativeTaskAirbornePresentationActive = false;
        m_nativeTaskPhysicalTakeover = {};
        m_nativeTaskPhysicalTakeoverPending = false;
        m_nativeTaskPhysicalTakeoverStartedAt = 0;
    };

    const bool syncedAnimationOwnsTransform = HasSyncedAnim() && GetAnimationCache().bUpdatePosition;
    const bool transformOwnedElsewhere =
        GetOccupiedVehicle() || GetOccupyingVehicle() || IsGettingIntoVehicle() || IsGettingOutOfVehicle() || GetAttachedTo() || syncedAnimationOwnsTransform;
    if (state.active)
    {
        // A revoke releases the ambient profile before MTA flips the syncer.
        // Keep holding during that short gap; only the ownership transition,
        // collision recovery, or an explicit transform owner may release it.
        if (!m_pPlayerPed || GetType() != CCLIENTPED || m_bIsLocalPlayer || !m_bIsSyncing || IsFrozen() || transformOwnedElsewhere)
        {
            ClearNativeAmbientOwnerCollisionFence("state_changed");
            return;
        }

        const unsigned long now = CClientTime::GetTime();
        if (now >= state.nextProbeAt)
        {
            state.nextProbeAt = now + REMOTE_PED_STREAM_IN_TRANSFORM_FENCE_MINIMUM;
            CColStore* collisionStore = g_pGame->GetCollisionStore();
            if (!collisionStore || collisionStore->HasCollisionLoaded(state.safeMatrix.vPos, m_ucInterior))
            {
                ClearNativeAmbientOwnerCollisionFence("collision_ready");
                return;
            }
            collisionStore->RequestCollision(state.safeMatrix.vPos, m_ucInterior);
        }

        // The server must never receive a gravity-integrated position from an
        // owner whose local collision sector has disappeared. Keep the last
        // collision-backed transform authoritative until the immediate
        // handoff completes or the sector is available again.
        clearUnloadedCollisionAirborneTask();
        m_Matrix = state.safeMatrix;
        m_fCurrentRotation = state.safeCurrentRotation;
        m_fTargetRotation = state.safeTargetRotation;
        m_vecMoveSpeed = {};
        m_pPlayerPed->SetMatrix(&m_Matrix);
        m_pPlayerPed->SetCurrentRotation(m_fCurrentRotation);
        m_pPlayerPed->SetTargetRotation(m_fTargetRotation);
        m_pPlayerPed->SetMoveSpeed(m_vecMoveSpeed);
        return;
    }

    const bool ambientOwner =
        m_pPlayerPed && GetType() == CCLIENTPED && !m_bIsLocalPlayer && m_bIsSyncing && m_pPlayerPed->IsNativeAmbientWanderEventProfileActive();
    if (!ambientOwner || IsFrozen() || transformOwnedElsewhere)
    {
        // A vehicle, attachment, or root-motion animation can legitimately
        // move the ped without this on-foot collision contract. Rebase after
        // that state ends instead of restoring a stale snapshot.
        state.snapshotValid = false;
        return;
    }

    CMatrix liveMatrix;
    m_pPlayerPed->GetMatrix(&liveMatrix);
    if (!state.snapshotValid)
    {
        // StartSync/create RPC has already installed this transform. Seed it
        // even if collision is currently absent so a new owner cannot take a
        // first gravity step before the server finishes the handoff.
        state.safeMatrix = liveMatrix;
        state.safeCurrentRotation = m_pPlayerPed->GetCurrentRotation();
        state.safeTargetRotation = m_pPlayerPed->GetTargetRotation();
        m_pPlayerPed->GetMoveSpeed(&state.safeMoveSpeed);
        state.snapshotValid = true;
    }

    const unsigned long now = CClientTime::GetTime();
    if (now < state.nextProbeAt)
        return;
    state.nextProbeAt = now + REMOTE_PED_STREAM_IN_TRANSFORM_FENCE_MINIMUM;

    CColStore* collisionStore = g_pGame->GetCollisionStore();
    if (!collisionStore || collisionStore->HasCollisionLoaded(liveMatrix.vPos, m_ucInterior))
    {
        // Refresh only from a transform backed by loaded world collision. A
        // later missing-COL pulse will therefore have an uncontaminated
        // position even if GTA immediately enters IN_AIR_AND_LAND.
        state.safeMatrix = liveMatrix;
        state.safeCurrentRotation = m_pPlayerPed->GetCurrentRotation();
        state.safeTargetRotation = m_pPlayerPed->GetTargetRotation();
        m_pPlayerPed->GetMoveSpeed(&state.safeMoveSpeed);
        return;
    }

    state.startedAt = now;
    state.previousStaticWaitingForCollision = m_pPlayerPed->IsStaticWaitingForCollision();
    state.active = true;
    clearUnloadedCollisionAirborneTask();
    m_Matrix = state.safeMatrix;
    m_fCurrentRotation = state.safeCurrentRotation;
    m_fTargetRotation = state.safeTargetRotation;
    m_vecMoveSpeed = {};
    m_pPlayerPed->SetStaticWaitingForCollision(true);
    m_pPlayerPed->SetFrozen(true);
    m_pPlayerPed->SetMatrix(&m_Matrix);
    m_pPlayerPed->SetCurrentRotation(m_fCurrentRotation);
    m_pPlayerPed->SetTargetRotation(m_fTargetRotation);
    m_pPlayerPed->SetMoveSpeed(m_vecMoveSpeed);
    collisionStore->RequestCollision(state.safeMatrix.vPos, m_ucInterior);

    CNativeAITelemetry::RecordPedEvent(ENativeAITelemetryCategory::OWNERSHIP, "owner_collision_hold_started", this);
    if (IsRemotePedStreamInTransformFenceTraceEnabled())
    {
        g_pCore->GetConsole()->Printf("[native-ambient-owner-collision][hold] ped=%u model=%lu safe=(%.3f,%.3f,%.3f) local=(%.3f,%.3f,%.3f)", GetID().Value(),
                                      GetModel(), state.safeMatrix.vPos.fX, state.safeMatrix.vPos.fY, state.safeMatrix.vPos.fZ, liveMatrix.vPos.fX,
                                      liveMatrix.vPos.fY, liveMatrix.vPos.fZ);
    }
}

void CClientPed::ClearNativeAmbientOwnerCollisionFence(const char* reason, bool restoreSafeTransform)
{
    auto& state = m_nativeAmbientOwnerCollisionFence;
    if (!state.active)
        return;

    const unsigned long elapsed = CClientTime::GetTime() - state.startedAt;
    if (m_pPlayerPed)
    {
        if (restoreSafeTransform && state.snapshotValid)
        {
            m_Matrix = state.safeMatrix;
            m_fCurrentRotation = state.safeCurrentRotation;
            m_fTargetRotation = state.safeTargetRotation;
            m_vecMoveSpeed = state.safeMoveSpeed;
            m_pPlayerPed->SetMatrix(&m_Matrix);
            m_pPlayerPed->SetCurrentRotation(m_fCurrentRotation);
            m_pPlayerPed->SetTargetRotation(m_fTargetRotation);
            m_pPlayerPed->SetMoveSpeed(m_vecMoveSpeed);
        }
        m_pPlayerPed->SetStaticWaitingForCollision(state.previousStaticWaitingForCollision);
        ApplyPhysicalFreezeState();
        if (!state.previousStaticWaitingForCollision && !m_pPlayerPed->IsStatic())
            m_pPlayerPed->AddToMovingList();
    }
    state.active = false;
    state.nextProbeAt = CClientTime::GetTime() + REMOTE_PED_STREAM_IN_TRANSFORM_FENCE_MINIMUM;

    CNativeAITelemetry::RecordPedEvent(ENativeAITelemetryCategory::OWNERSHIP, "owner_collision_hold_released", this);
    if (IsRemotePedStreamInTransformFenceTraceEnabled())
    {
        g_pCore->GetConsole()->Printf("[native-ambient-owner-collision][release] ped=%u model=%lu reason=%s elapsed=%lu", GetID().Value(), GetModel(),
                                      reason ? reason : "unknown", elapsed);
    }
}

void CClientPed::_DestroyModel()
{
    UpdateNativeCollisionAuthorityFence(false, "destroy_model");
    ReleaseNativeCollisionResidency("destroy_model");
    m_remoteReplicaPhysicsFenceActive = false;
    ClearNativeAmbientOwnerCollisionFence("destroy_model");
    m_nativeAmbientOwnerCollisionFence.snapshotValid = false;
    const bool remoteNonSyncer = GetType() == CCLIENTPED && !m_bIsLocalPlayer && !m_bIsSyncing;
    const bool syncedAnimationOwnsTransform = HasSyncedAnim() && GetAnimationCache().bUpdatePosition;
    const bool preserveRemoteAuthoritativeTransform = remoteNonSyncer && m_remoteAuthoritativeTransform.restoreAllowed && !GetOccupiedVehicle() &&
                                                      !GetOccupyingVehicle() && !IsGettingIntoVehicle() && !IsGettingOutOfVehicle() && !GetAttachedTo() &&
                                                      !syncedAnimationOwnsTransform &&
                                                      !SNativeTaskAnimationPresentationSync::IsPhysicalMode(m_nativeTaskAnimationPresentation.data.uiMode) &&
                                                      !m_nativeTaskAirbornePresentationActive && m_remoteAuthoritativeTransform.positionValid;
    if (remoteNonSyncer && !preserveRemoteAuthoritativeTransform)
    {
        // Vehicle, attachment, root-motion and physical presentation own the
        // live cache for this stream-out. Keep an older on-foot snapshot from
        // overwriting that deliberate state on the next model creation.
        m_remoteAuthoritativeTransform.restoreAllowed = false;
    }
    ClearRemoteStreamInTransformFence("destroy_model", preserveRemoteAuthoritativeTransform);
    // This path also serves model recreation without StreamOut. Release a
    // presentation task while its original GTA ped and saved shooting rate
    // still exist, so the replacement entity can accept the next sample.
    ClearNativeTaskWeaponPresentation("destroy_model");
    if (m_nativeTaskAirbornePresentationActive && m_pPlayerPed)
        m_pPlayerPed->SetNativeTaskAirbornePresentationState(false, false);
    m_nativeTaskAirbornePresentationActive = false;
    m_nativeTaskPhysicalTakeover = {};
    m_nativeTaskPhysicalTakeoverPending = false;
    m_nativeTaskPhysicalTakeoverStartedAt = 0;
    ClearNativeTaskAnimationPresentation("destroy_model");

    // Store ped ammo
    if (GetType() == CCLIENTPED)
    {
        for (uchar i = 0; i < (uchar)WEAPONSLOT_MAX; i++)
        {
            if (m_WeaponTypes[i] != WEAPONTYPE_UNARMED)
            {
                CWeapon* pWeapon = GetWeapon(m_WeaponTypes[i]);
                if (pWeapon)
                {
                    m_usWeaponAmmo[i] = static_cast<ushort>(pWeapon->GetAmmoTotal());
                }
            }
        }
    }

    // Remove our linked contact entity
    if (m_pCurrentContactEntity)
    {
        m_pCurrentContactEntity->RemoveContact(this);
        m_pCurrentContactEntity = NULL;
    }

    // A remote CPlayerPed is presentation only. Never persist its locally
    // simulated fall over the last transform accepted from its network
    // owner; a stationary owner may not send another position after this
    // observer streams the entity back in.
    if (preserveRemoteAuthoritativeTransform)
    {
        m_Matrix.vPos = m_remoteAuthoritativeTransform.position;
        if (m_remoteAuthoritativeTransform.rotationValid)
        {
            m_fCurrentRotation = m_remoteAuthoritativeTransform.rotation;
            m_fTargetRotation = m_remoteAuthoritativeTransform.rotation;
            CVector rotation = m_Matrix.GetRotation();
            rotation.fZ = m_remoteAuthoritativeTransform.rotation;
            m_Matrix.SetRotation(rotation);
        }
        m_vecMoveSpeed = m_remoteAuthoritativeTransform.moveSpeedValid ? m_remoteAuthoritativeTransform.moveSpeed : CVector();
    }
    else
    {
        m_Matrix.vPos = *m_pPlayerPed->GetPosition();
        m_fCurrentRotation = m_pPlayerPed->GetCurrentRotation();
        m_pPlayerPed->GetMoveSpeed(&m_vecMoveSpeed);
    }
    m_pPlayerPed->GetTurnSpeed(&m_vecTurnSpeed);
    m_bDucked = IsDucked();
    m_bWearingGoggles = IsWearingGoggles();
    m_pPlayerPed->SetOnFire(false);
    m_fLighting = m_pPlayerPed->GetLighting();

    /* Eventually remove from vehicle
        MUST use internal-func, to save the the occupied-vehicle (streaming) */
    CClientVehicle* pVehicle = GetRealOccupiedVehicle();
    if (!pVehicle)
    {
        pVehicle = GetOccupiedVehicle();
        if (!pVehicle)
        {
            pVehicle = m_pOccupyingVehicle;
        }
    }
    if (pVehicle)
    {
        CVehicle* pGameVehicle = pVehicle->GetGameVehicle();
        if (pGameVehicle)
        {
            InternalRemoveFromVehicle(pGameVehicle);
        }
    }

    g_pMultiplayer->RemoveRemoteDataStorage(m_pPlayerPed);

    // Invalidate
    m_pManager->InvalidateEntity(this);

    // Remove the ped from the world
    g_pGame->GetPools()->RemovePed(m_pPlayerPed);
    m_pPlayerPed = NULL;
    m_pTaskManager = NULL;

    // Remove the reference to its model
    m_pLoadedModelInfo->RemoveRef();
    m_pLoadedModelInfo = NULL;

    // Any pending rebuild will not be required now
    m_bPendingRebuildPlayer = false;

    NotifyDestroy();
}

void CClientPed::_DestroyLocalModel()
{
    /* Eventually remove from vehicle
        MUST use internal-func, to save the the occupied-vehicle (streaming) */
    CClientVehicle* pVehicle = GetRealOccupiedVehicle();
    if (!pVehicle)
    {
        pVehicle = GetOccupiedVehicle();
        if (!pVehicle)
        {
            pVehicle = m_pOccupyingVehicle;
        }
    }
    if (pVehicle)
    {
        CVehicle* pGameVehicle = pVehicle->GetGameVehicle();
        if (pGameVehicle)
        {
            InternalRemoveFromVehicle(pGameVehicle);
        }

        pVehicle->RemoveStreamReference();
    }

    // Invalidate
    m_pManager->InvalidateEntity(this);

    g_pGame->GetPools()->InvalidateLocalPlayerClientEntity();

    // Make sure we are CJ again
    if (m_pPlayerPed->GetModelIndex() != 0)
    {
        m_pPlayerPed->SetModelIndex(0);
    }

    // Remove reference to our previous model
    m_pLoadedModelInfo->RemoveRef();
    m_pLoadedModelInfo = NULL;

    // NULL our pointers, we don't destroy the local player
    m_pPlayerPed = NULL;
    m_pTaskManager = NULL;
}

void CClientPed::_ChangeModel()
{
    // Different model than before?
    if (m_pPlayerPed->GetModelIndex() != m_ulModel)
    {
        g_pMultiplayer->SetAutomaticVehicleStartupOnPedEnter(false);

        // We need to reset visual stats when changing from CJ model
        if (m_pPlayerPed->GetModelIndex() == 0)
        {
            // Reset visual stats
            SetStat(21, 0.0f);
            SetStat(23, 0.0f);
        }

        // Store attached satchels
        std::vector<SSatchelsData> attachedSatchels;
        m_pPlayerPed->GetAttachedSatchels(attachedSatchels);

        if (m_bIsLocalPlayer)
        {
            // TODO: Create a simple function to save and restore player states and use it
            //       all the places that to context saving/restoring.

            // Save the vehicle he's in
            CClientVehicle* pVehicle = GetOccupiedVehicle();
            unsigned int    uiSeat = GetOccupiedVehicleSeat();
            CVector         vecVehicleVelocity, vecVehicleTurnVelocity;
            float           fVehicleTrainSpeed;

            // Are we leaving it? Don't warp him back into anything
            if (pVehicle)
            {
                // Store the velocity of the vehicle since
                // GTA will set it to zero
                pVehicle->GetMoveSpeed(vecVehicleVelocity);
                pVehicle->GetTurnSpeed(vecVehicleTurnVelocity);
                fVehicleTrainSpeed = pVehicle->GetTrainSpeed();

                // Are we leaving it? Don't warp back into it.
                if (IsLeavingVehicle())
                    pVehicle = NULL;

                // Remove him from the vehicle
                RemoveFromVehicle();
            }

            m_pPlayerPed->GetFightingStyle();

            // Takes care of clothes/task issues
            Respawn(NULL, true, false);

            // Remember the model we had loaded and store the new model we're going to load
            CModelInfo* pLoadedModel = m_pLoadedModelInfo;
            m_pLoadedModelInfo = m_pModelInfo;

            // Add reference to the model
            m_pLoadedModelInfo->ModelAddRef(BLOCKING, "CClientPed::_ChangeModel");

            // Set the new player model and restore the interior
            m_pPlayerPed->SetModelIndex(m_ulModel);

            // Rebuild the player after a skin change
            if (m_ulModel == 0)
            {
                // When the local player changes to CJ, the clothes geometry gets an extra ref from somewhere, causing a memory leak.
                // So make sure clothes geometry is built now...
                m_pClothes->AddAllToModel();
                m_pPlayerPed->RebuildPlayer();
            }

            // Remove reference to the old model we used (Flag extra GTA reference to be removed as well)
            pLoadedModel->RemoveRef(true);
            pLoadedModel = NULL;

            // Warp into it again
            if (pVehicle)
            {
                WarpIntoVehicle(pVehicle, uiSeat);

                // Restore vehicle speed
                pVehicle->SetMoveSpeed(vecVehicleVelocity);
                pVehicle->SetTurnSpeed(vecVehicleTurnVelocity);
                pVehicle->SetTrainSpeed(fVehicleTrainSpeed);
            }
            m_bDontChangeRadio = false;

            // Are we still playing a animation?
            if (m_pAnimationBlock && IsAnimationInProgress())
            {
                if (m_bisCurrentAnimationCustom)
                {
                    m_bisNextAnimationCustom = true;
                }

                RunAnimationFromCache();
            }

            // Set the voice that corresponds to the new model
            short sVoiceType, sVoiceID;
            m_pModelInfo->GetVoice(&sVoiceType, &sVoiceID);
            SetVoice(sVoiceType, sVoiceID);
        }
        else
        {
            // ChrML: Changing the skin in certain cases causes player sliding. So we recreate instead.

            m_shouldRecreate = true;
        }

        // ReAttach satchels
        CClientProjectileManager* pProjectileManager = m_pManager->GetProjectileManager();

        for (const SSatchelsData& satchelData : attachedSatchels)
        {
            CClientProjectile* pSatchel = pProjectileManager->Get((CEntitySAInterface*)satchelData.pProjectileInterface);
            if (!pSatchel || pSatchel->IsBeingDeleted())
                continue;

            pSatchel->SetAttachedOffsets(*satchelData.vecAttachedOffsets, *satchelData.vecAttachedRotation);
            pSatchel->InternalAttachTo(this);
        }

        g_pMultiplayer->SetAutomaticVehicleStartupOnPedEnter(true);
    }
    if (m_clientModel && m_clientModel->GetModelID() != m_ulModel)
        m_clientModel = nullptr;
}

void CClientPed::ReCreateModel()
{
    // We can only recreate if we're not the local player and if we have a player model
    if (!m_bIsLocalPlayer && m_pPlayerPed)
    {
        // Make sure we don't unload then load unneccessarily if the new and the old model were the same
        bool bSameModel = (m_pLoadedModelInfo == m_pModelInfo);
        if (bSameModel)
        {
            m_pLoadedModelInfo->ModelAddRef(BLOCKING, "CClientPed::ReCreateModel");
        }

        m_shouldRecreate = true;

        // Remove the reference we temporarily added again
        if (bSameModel)
        {
            m_pLoadedModelInfo->RemoveRef();
        }
    }
}

void CClientPed::ReCreateGameEntity()
{
    if (!m_shouldRecreate || !m_pPlayerPed)
        return;

    // Destroy current game entity
    _DestroyModel();

    // Create the new game entity
    _CreateModel();

    m_shouldRecreate = false;
}

void CClientPed::ModelRequestCallback(CModelInfo* pModelInfo)
{
    // The model loading may take a while and there's a chance of ped being moved to other dimension.
    if (!IsVisibleInAllDimensions() && GetDimension() != m_pStreamer->GetDimension())
    {
        NotifyUnableToCreate();
        return;
    }

    // If we have a player loaded
    if (m_pPlayerPed)
    {
        // Change its skin
        _ChangeModel();
    }
    else
    {
        // If we don't have a player loaded, load it
        _CreateModel();
    }
}

void CClientPed::RebuildModel(bool bDelayChange)
{
    // We have a player
    if (m_pPlayerPed)
    {
        // We are CJ?
        if (m_ulModel == 0)
        {
            // Adds only the necessary textures
            m_pClothes->RefreshClothes();
            m_pClothes->AddAllToModel();

            m_bPendingRebuildPlayer = true;

            // Apply immediately unless there is a chance more clothes states will change (e.g. via script)
            if (!bDelayChange)
                ProcessRebuildPlayer(false);
        }
    }
}

//
// Process any pending build but avoid rebuilding more than once a frame
//
void CClientPed::ProcessRebuildPlayer(bool bNeedsClothesUpdate)
{
    assert(m_pPlayerPed);

    if (m_bPendingRebuildPlayer && m_uiFrameLastRebuildPlayer != g_pClientGame->GetFrameCount())
    {
        m_bPendingRebuildPlayer = false;
        m_uiFrameLastRebuildPlayer = g_pClientGame->GetFrameCount();

        if (bNeedsClothesUpdate)
            m_pClothes->AddAllToModel();

        if (m_bIsLocalPlayer)
        {
            m_pPlayerPed->RebuildPlayer();
        }
        else
        {
            g_pMultiplayer->RebuildMultiplayerPlayer(m_pPlayerPed);
        }
    }
}

void CClientPed::StreamIn(bool bInstantly)
{
    if (m_bIsLocalPlayer)
    {
        NotifyCreate();
        return;
    }
#if 0
    // We need to create now?
    if ( bInstantly )
    {
        // Request its model blocking
        if ( !m_pPlayerPed && m_pRequester->RequestBlocking ( static_cast < unsigned short > ( m_ulModel ), "CClientVehicle::StreamIn - bInstantly" ) )
        {
            m_pModelInfo->MakeCustomModel ( );
            // If it was loaded, create it immediately.
            _CreateModel ();
        }
        else NotifyUnableToCreate ();
    }
    else
#endif
    {
        // Request it
        if (!m_pPlayerPed && m_pRequester->Request(static_cast<unsigned short>(m_ulModel), this))
        {
            m_pModelInfo->MakeCustomModel();
            // If it was loaded, create it immediately.
            _CreateModel();
        }
        else
            NotifyUnableToCreate();
    }
}

void CClientPed::StreamOut()
{
    // Make sure we have a player ped and that we're not
    // the local player
    if (m_pPlayerPed && !m_bIsLocalPlayer)
    {
        SetNativeTaskLocomotionPresentation({}, "stream_out");
        ClearNativeTaskWeaponPresentation("stream_out");
        ClearNativeTaskAnimationPresentation("stream_out");

        // Destroy us
        _DestroyModel();

        // Make sure no model loading is pending. This would recreate
        // us very soon.
        m_pRequester->Cancel(this, true);
    }
}

void CClientPed::StreamOutWeaponForABit(eWeaponSlot eSlot)
{
    // Get the Weapon
    CWeapon* pWeapon = GetWeapon(eSlot);
    if (pWeapon)
    {
        // Store our states i.e. clip Ammo, Ammo, type and if it's the current weapon
        SRestoreWeaponItem item;
        item.dwClipAmmo = pWeapon->GetAmmoInClip();
        item.dwAmmo = pWeapon->GetAmmoTotal();
        item.eWeaponID = pWeapon->GetType();
        item.bCurrentWeapon = GetCurrentWeaponType() == item.eWeaponID;
        m_RestoreWeaponList.push_back(item);

        // Remove it
        pWeapon->Remove();
    }
}

bool CClientPed::SetMissionActor(bool enabled)
{
    if (GetType() != CCLIENTPED)
        return false;

    if (m_bMissionActor == enabled)
        return true;

    if (!enabled)
    {
        if (m_pPlayerPed && m_missionActorNativeState)
            m_pPlayerPed->RestoreCreatedByState(*m_missionActorNativeState);

        // Script peds normally use MTA's player-weapon synchronization path.
        // Restore that policy when the resource gives up native AI ownership.
        if (m_remoteDataStorage)
            m_remoteDataStorage->SetProcessPlayerWeapon(true);
    }

    m_bMissionActor = enabled;
    m_missionActorNativeState.reset();

    if (m_pPlayerPed)
        ApplyMissionActorState();
    ApplyNativeEventProfileState();

    return true;
}

void CClientPed::ApplyMissionActorState()
{
    m_missionActorNativeState.reset();
    if (!m_pPlayerPed)
        return;

    // Script peds are CPlayerPed instances, so CPed::IsPlayer normally sends
    // their melee task through player-input attack selection. Keep a separate
    // policy bit rather than changing bPedType globally: only the audited
    // CTaskSimpleFight call sites should observe native CPed behaviour.
    m_pPlayerPed->SetNativeFightUsesNonPlayerBehavior(GetType() == CCLIENTPED && m_bMissionActor);

    if (!m_bMissionActor)
        return;

    // SetCharCreatedBy changes perception and decision-maker state in addition
    // to the classification byte. Snapshot all affected values so disabling
    // the policy is a true restoration rather than a partial flag reset.
    m_missionActorNativeState = m_pPlayerPed->GetCreatedByState();
    m_pPlayerPed->SetCreatedBy(PED_CREATED_BY_MISSION);

    // MTA represents script peds as CPlayerPed instances. Its shot-sync hook
    // therefore replaces CWeapon::Fire's explicit target with the replicated
    // player target by default. A mission actor running GTA AI tasks must keep
    // the task-owned target, just like a CPed created by main.scm.
    if (m_remoteDataStorage)
        m_remoteDataStorage->SetProcessPlayerWeapon(false);
}

bool CClientPed::AcquireNativeEventProfile(CResource* owner, unsigned int token, ePedNativeEventProfile profile)
{
    if (!owner || token == 0 || profile == ePedNativeEventProfile::NONE || GetType() != CCLIENTPED || m_nativeEventProfileOwner)
        return false;
    if (profile == ePedNativeEventProfile::MISSION && !m_bMissionActor)
        return false;
    if ((profile == ePedNativeEventProfile::AMBIENT_WANDER || profile == ePedNativeEventProfile::AMBIENT_COP_SAFE) && m_bMissionActor)
        return false;

    // Every client remembers the resource-scoped lease, including before it
    // becomes syncer. Apply gates the native exception to the authoritative
    // generation and teardown can still revoke a surviving server-owned ped.
    m_nativeEventProfileOwner = owner;
    m_uiNativeEventProfileToken = token;
    m_nativeEventProfile = profile;
    ApplyNativeEventProfileState();
    return true;
}

bool CClientPed::ReleaseNativeEventProfile(CResource* owner, unsigned int token, ePedNativeEventProfile profile)
{
    if (!owner || token == 0 || m_nativeEventProfileOwner != owner || m_uiNativeEventProfileToken != token || m_nativeEventProfile != profile)
        return false;

    m_nativeEventProfileOwner = nullptr;
    m_uiNativeEventProfileToken = 0;
    m_nativeEventProfile = ePedNativeEventProfile::NONE;
    ApplyNativeEventProfileState();
    return true;
}

bool CClientPed::IsNativeEventProfileActive(const CResource* owner, unsigned int token, ePedNativeEventProfile profile) const
{
    if (!owner || token == 0 || m_nativeEventProfileOwner != owner || m_uiNativeEventProfileToken != token || m_nativeEventProfile != profile ||
        !m_bIsSyncing || !m_pPlayerPed)
        return false;

    if (profile == ePedNativeEventProfile::MISSION)
        return m_bMissionActor && m_pPlayerPed->IsNativeMissionEventProfileActive();
    if (profile == ePedNativeEventProfile::AMBIENT_WANDER || profile == ePedNativeEventProfile::AMBIENT_COP_SAFE)
        return m_pPlayerPed->IsNativeAmbientWanderEventProfileActive();
    return false;
}

bool CClientPed::AddNativeGunAimedAtEvent(CClientPed* aimingPed)
{
    if (!aimingPed || aimingPed == this || GetType() != CCLIENTPED || !m_bIsSyncing || IsDead() || !m_pPlayerPed || !aimingPed->m_pPlayerPed ||
        !m_pPlayerPed->IsNativeAmbientWanderEventProfileActive())
    {
        return false;
    }

    return m_pPlayerPed->AddNativeGunAimedAtEvent(aimingPed->m_pPlayerPed);
}

bool CClientPed::AddNativeDamageResponseEvent(CClientPed* attackingPed, eWeaponType weaponType, ePedPieceTypes hitZone)
{
    if (!attackingPed || attackingPed == this || GetType() != CCLIENTPED || !m_bIsSyncing || IsDead() || !m_pPlayerPed || !attackingPed->m_pPlayerPed ||
        !m_pPlayerPed->IsNativeAmbientWanderEventProfileActive())
    {
        return false;
    }

    return m_pPlayerPed->AddNativeDamageResponseEvent(attackingPed->m_pPlayerPed, weaponType, hitZone);
}

bool CClientPed::AddNativeDamageEvent(CClientPed* attackingPed, eWeaponType weaponType, ePedPieceTypes hitZone, int damageFactor, unsigned char direction)
{
    // Only the victim's authority may apply a replay. Remote NPC health stays
    // locked just like remote player health; its syncer publishes the result.
    const bool ownsVictim = GetType() == CCLIENTPLAYER ? m_bIsLocalPlayer : GetType() == CCLIENTPED && IsSyncing() && !IsHealthLocked() && !IsArmorLocked();
    if (!attackingPed || attackingPed == this || !ownsVictim || IsDead() || !m_pPlayerPed || attackingPed->GetType() != CCLIENTPED ||
        !attackingPed->m_pPlayerPed)
    {
        return false;
    }

    // The observer normally suppresses damage produced by the attacker's
    // replicated weapon presentation. This call is different: the server has
    // authenticated the owner's native hit and selected this client as
    // its authoritative victim. Scope that distinction to the exact pair for
    // the synchronous AffectsPed/DamageHandler passes, then restore any outer
    // replay context before returning.
    CClientPed* const previousReplayVictim = attackingPed->m_pNativeDamageReplayVictim;
    attackingPed->m_pNativeDamageReplayVictim = this;
    const bool accepted = m_pPlayerPed->AddNativeDamageEvent(attackingPed->m_pPlayerPed, weaponType, hitZone, damageFactor, direction);
    attackingPed->m_pNativeDamageReplayVictim = previousReplayVictim;
    return accepted;
}

void CClientPed::ApplyNativeEventProfileState()
{
    if (!m_pPlayerPed)
        return;

    RefreshNativeCollisionResidency();

    // The lease remains logically owned across stream and syncer generations,
    // but only the authoritative syncer may let GTA consume ambient events.
    const bool leased = m_nativeEventProfileOwner && m_uiNativeEventProfileToken != 0;
    const bool missionActive =
        leased && m_nativeEventProfile == ePedNativeEventProfile::MISSION && m_bMissionActor && m_bIsSyncing && m_nativeCollisionResidencyReady;
    const bool ambientSelected =
        leased && (m_nativeEventProfile == ePedNativeEventProfile::AMBIENT_WANDER || m_nativeEventProfile == ePedNativeEventProfile::AMBIENT_COP_SAFE);
    const bool ambientActive = ambientSelected && !m_bMissionActor && m_bIsSyncing && m_nativeCollisionResidencyReady;
    dassert((!missionActive && !ambientActive) || (m_nativeCollisionResidency != 0 && m_nativeCollisionResidencyReady));

    // Acquiring the lease can race a local scanner event. Purge a response
    // whenever this peer is fenced as an observer, even if the wrapper was not
    // previously marked active.
    if (!ambientActive && (ambientSelected || m_pPlayerPed->IsNativeAmbientWanderEventProfileActive()))
        ClearNativeAmbientWanderResponse();

    // MTA represents script peds with CPlayerPed even when GTA's native AI is
    // authoritative. Ambient owners need the same audited CPed melee branches
    // as mission actors; otherwise FightingControl consumes player-input fight
    // movement and continuously mixes ordinary locomotion into blocks and
    // strikes. Recompute this with the owner-only profile state so handoff and
    // release fence the old peer automatically.
    m_pPlayerPed->SetNativeFightUsesNonPlayerBehavior(GetType() == CCLIENTPED && (m_bMissionActor || ambientActive));
    // FireInstantHit's player shot-sync path replaces the explicit target with
    // replicated player aim. A native ambient owner, like a mission actor,
    // must keep the target selected by CTaskSimpleUseGun. Recompute the policy
    // from current authority so observer/released peers immediately return to
    // ordinary script-ped weapon processing after a handoff.
    if (m_remoteDataStorage)
        m_remoteDataStorage->SetProcessPlayerWeapon(!(m_bMissionActor || ambientActive));
    m_pPlayerPed->SetNativeMissionEventProfileActive(missionActive);
    m_pPlayerPed->SetNativeAmbientWanderEventProfile(ambientSelected, ambientActive);
}

bool CClientPed::RefreshNativeCollisionResidency()
{
    CColStore* collisionStore = g_pGame ? g_pGame->GetCollisionStore() : nullptr;
    bool       markedNativeAgent = false;
    GetCustomDataBool(CStringName("neon:ambientPedTraffic"), markedNativeAgent, false);
    const bool validProfile =
        m_nativeEventProfileOwner && m_uiNativeEventProfileToken != 0 &&
        (((m_nativeEventProfile == ePedNativeEventProfile::AMBIENT_WANDER || m_nativeEventProfile == ePedNativeEventProfile::AMBIENT_COP_SAFE) &&
          !m_bMissionActor) ||
         (m_nativeEventProfile == ePedNativeEventProfile::MISSION && m_bMissionActor));
    const bool physicalTakeover = m_nativeTaskPhysicalTakeoverPending || m_nativeTaskAirbornePresentationActive;
    const bool wantsNativeAuthority =
        m_pPlayerPed && GetType() == CCLIENTPED && !m_bIsLocalPlayer && m_bIsSyncing && (markedNativeAgent || validProfile || physicalTakeover);
    if (!wantsNativeAuthority)
    {
        const bool changed = m_nativeCollisionResidencyReady;
        UpdateNativeCollisionAuthorityFence(false, "state_changed");
        ReleaseNativeCollisionResidency("state_changed");
        return changed;
    }

    // Deny authority first and release it only after the probe below. This is
    // called synchronously by SetSyncing, before the newly authoritative GTA
    // physical can reach its next ProcessControl tick.
    if (!m_nativeCollisionResidencyReady)
        UpdateNativeCollisionAuthorityFence(true, "awaiting_ground_support");

    if (!collisionStore)
    {
        const bool changed = m_nativeCollisionResidencyReady;
        ReleaseNativeCollisionResidency("store_unavailable");
        return changed;
    }

    if (!m_nativeCollisionResidency)
    {
        m_nativeCollisionResidency = collisionStore->AcquireCollisionResidency(m_pPlayerPed, m_ucInterior);
        m_nativeCollisionResidencyNextProbeAt = 0;
        if (m_nativeCollisionResidency)
            CNativeAITelemetry::RecordPedEvent(ENativeAITelemetryCategory::OWNERSHIP, "collision_residency_acquired", this);
    }
    else if (!collisionStore->UpdateCollisionResidency(m_nativeCollisionResidency, m_pPlayerPed, m_ucInterior))
    {
        const bool changed = m_nativeCollisionResidencyReady;
        UpdateNativeCollisionAuthorityFence(true, "update_refused");
        ReleaseNativeCollisionResidency("update_refused");
        return changed;
    }

    if (!m_nativeCollisionResidency)
        return false;

    const unsigned long now = CClientTime::GetTime();
    if (now < m_nativeCollisionResidencyNextProbeAt)
        return false;
    m_nativeCollisionResidencyNextProbeAt = now + NATIVE_COLLISION_RESIDENCY_PROBE_INTERVAL;

    const bool ready = collisionStore->IsCollisionResidencyLoaded(m_nativeCollisionResidency) && HasNativeCollisionGroundSupport();
    const bool changed = ready != m_nativeCollisionResidencyReady;
    if (changed)
    {
        m_nativeCollisionResidencyReady = ready;
        CNativeAITelemetry::RecordPedEvent(ENativeAITelemetryCategory::OWNERSHIP,
                                           ready ? "collision_residency_ground_ready" : "collision_residency_ground_lost", this);
    }
    UpdateNativeCollisionAuthorityFence(!ready, ready ? "ground_ready" : "ground_lost");
    return changed;
}

void CClientPed::ReleaseNativeCollisionResidency(const char* reason)
{
    if (!m_nativeCollisionResidency)
    {
        m_nativeCollisionResidencyReady = false;
        return;
    }

    if (g_pGame)
    {
        if (CColStore* collisionStore = g_pGame->GetCollisionStore())
            collisionStore->ReleaseCollisionResidency(m_nativeCollisionResidency);
    }
    m_nativeCollisionResidency = 0;
    m_nativeCollisionResidencyReady = false;
    m_nativeCollisionResidencyNextProbeAt = 0;
    const SString event = reason ? SString("collision_residency_released_%s", reason) : SString("%s", "collision_residency_released");
    CNativeAITelemetry::RecordPedEvent(ENativeAITelemetryCategory::OWNERSHIP, event, this);
}

bool CClientPed::HasNativeCollisionGroundSupport()
{
    if (!m_pPlayerPed || !g_pGame || !g_pGame->GetWorld())
        return false;

    const CVector position = *m_pPlayerPed->GetPosition();
    if (!std::isfinite(position.fX) || !std::isfinite(position.fY) || !std::isfinite(position.fZ))
        return false;
    const float rawBaseOffset = m_pPlayerPed->GetDistanceFromCentreOfMassToBaseOfModel();
    const float baseOffset = std::isfinite(rawBaseOffset) ? std::clamp(rawBaseOffset, 0.0f, 2.0f) : 1.0f;
    const float minimumRootDelta = std::max(0.0f, baseOffset - NATIVE_COLLISION_BASE_TOLERANCE_BELOW);
    const float maximumRootDelta = baseOffset + NATIVE_COLLISION_BASE_TOLERANCE_ABOVE;
    CVector     start = position;
    CVector     end = position;
    start.fZ += NATIVE_COLLISION_GROUND_PROBE_ABOVE;
    end.fZ -= maximumRootDelta;

    SLineOfSightFlags flags;
    flags.bCheckPeds = false;
    flags.bSeeThroughStuff = false;
    flags.bIgnoreSomeObjectsForCamera = false;
    CColPoint*  collision{};
    const bool  hit = g_pGame->GetWorld()->ProcessLineOfSight(&start, &end, &collision, nullptr, flags);
    const float lineRootDelta = hit && collision ? position.fZ - collision->GetPosition().fZ : std::numeric_limits<float>::infinity();
    CVector     groundProbePosition = position;
    const float groundZ = g_pGame->GetWorld()->FindGroundZFor3DPosition(&groundProbePosition);
    const float groundRootDelta = std::isfinite(groundZ) ? position.fZ - groundZ : std::numeric_limits<float>::infinity();
    const bool  lineSupported = hit && collision && lineRootDelta >= minimumRootDelta && lineRootDelta <= maximumRootDelta;
    const bool  supported = lineSupported;
    const float feetDelta = lineRootDelta - baseOffset;
    if (IsNativeCollisionResidencyTraceEnabled())
    {
        g_pCore->GetConsole()->Printf(
            "[native-collision-residency][ground-probe] ped=%u model=%lu pos=(%.3f,%.3f,%.3f) lineHit=%s lineDelta=%.3f groundZ=%.3f groundDelta=%.3f "
            "baseOffset=%.3f feetDelta=%.3f range=(%.3f,%.3f) source=%s ready=%s",
            GetID().Value(), GetModel(), position.fX, position.fY, position.fZ, hit ? "true" : "false", lineRootDelta, groundZ, groundRootDelta, baseOffset,
            feetDelta, minimumRootDelta, maximumRootDelta, lineSupported ? "line" : "none", supported ? "true" : "false");
    }
    if (collision)
        collision->Destroy();
    return supported;
}

void CClientPed::UpdateNativeCollisionAuthorityFence(bool shouldFence, const char* reason)
{
    auto& fence = m_nativeCollisionAuthorityFence;
    if (!m_pPlayerPed)
    {
        fence.active = false;
        return;
    }

    if (shouldFence && !fence.active)
    {
        m_pPlayerPed->GetMatrix(&fence.safeMatrix);
        fence.safeCurrentRotation = m_pPlayerPed->GetCurrentRotation();
        fence.safeTargetRotation = m_pPlayerPed->GetTargetRotation();
        m_pPlayerPed->GetMoveSpeed(&fence.safeMoveSpeed);
        fence.previousStaticWaitingForCollision = m_pPlayerPed->IsStaticWaitingForCollision();
        fence.active = true;
        m_pPlayerPed->SetStaticWaitingForCollision(true);
        ApplyPhysicalFreezeState();
        CNativeAITelemetry::RecordPedEvent(ENativeAITelemetryCategory::OWNERSHIP, "collision_authority_fenced", this);
    }

    if (shouldFence && fence.active)
    {
        // Native physical tasks can author matrices directly, independently of
        // bDontApplySpeed. Preserve the acquisition transform until nearby
        // support has actually been observed.
        m_Matrix = fence.safeMatrix;
        m_fCurrentRotation = fence.safeCurrentRotation;
        m_fTargetRotation = fence.safeTargetRotation;
        m_vecMoveSpeed = {};
        m_pPlayerPed->SetMatrix(&m_Matrix);
        m_pPlayerPed->SetCurrentRotation(m_fCurrentRotation);
        m_pPlayerPed->SetTargetRotation(m_fTargetRotation);
        m_pPlayerPed->SetMoveSpeed(m_vecMoveSpeed);
        return;
    }

    if (!shouldFence && fence.active)
    {
        m_Matrix = fence.safeMatrix;
        m_fCurrentRotation = fence.safeCurrentRotation;
        m_fTargetRotation = fence.safeTargetRotation;
        m_vecMoveSpeed = fence.safeMoveSpeed;
        m_pPlayerPed->SetMatrix(&m_Matrix);
        m_pPlayerPed->SetCurrentRotation(m_fCurrentRotation);
        m_pPlayerPed->SetTargetRotation(m_fTargetRotation);
        m_pPlayerPed->SetMoveSpeed(m_vecMoveSpeed);
        m_pPlayerPed->SetStaticWaitingForCollision(fence.previousStaticWaitingForCollision);
        fence.active = false;
        ApplyPhysicalFreezeState();
        if (!fence.previousStaticWaitingForCollision && !m_pPlayerPed->IsStatic())
            m_pPlayerPed->AddToMovingList();
        CNativeAITelemetry::RecordPedEvent(ENativeAITelemetryCategory::OWNERSHIP,
                                           reason && strcmp(reason, "ground_ready") == 0 ? "collision_authority_ground_ready" : "collision_authority_released",
                                           this);
    }
}

void CClientPed::ApplyPhysicalFreezeState()
{
    if (m_pPlayerPed)
        m_pPlayerPed->SetFrozen(IsFrozen() || m_remoteStreamInFenceActive || m_nativeAmbientOwnerCollisionFence.active ||
                                m_nativeCollisionAuthorityFence.active || m_remoteReplicaPhysicsFenceActive);
}

void CClientPed::UpdateRemoteReplicaPhysicsFence()
{
    const bool syncedAnimationOwnsTransform = HasSyncedAnim() && GetAnimationCache().bUpdatePosition;
    const bool physicalPresentation =
        SNativeTaskAnimationPresentationSync::IsPhysicalMode(m_nativeTaskAnimationPresentation.data.uiMode) || m_nativeTaskAirbornePresentationActive;
    const bool shouldFence = m_pPlayerPed && GetType() == CCLIENTPED && !m_bIsLocalPlayer && !m_bIsSyncing && !IsFrozen() && !GetOccupiedVehicle() &&
                             !GetOccupyingVehicle() && !IsGettingIntoVehicle() && !IsGettingOutOfVehicle() && !GetAttachedTo() &&
                             !syncedAnimationOwnsTransform && !physicalPresentation;
    if (shouldFence == m_remoteReplicaPhysicsFenceActive)
        return;

    m_remoteReplicaPhysicsFenceActive = shouldFence;
    ApplyPhysicalFreezeState();
    CNativeAITelemetry::RecordPedEvent(ENativeAITelemetryCategory::OWNERSHIP, shouldFence ? "remote_replica_physics_fenced" : "remote_replica_physics_released",
                                       this);
}

void CClientPed::ClearNativeAmbientWanderResponse()
{
    if (!m_pTaskManager || !m_pPlayerPed)
        return;

    CTask*            physicalRoot = m_pTaskManager->GetTask(TASK_PRIORITY_PHYSICAL_RESPONSE);
    CTask*            physicalLeaf = m_pTaskManager->GetSimplestTask(TASK_PRIORITY_PHYSICAL_RESPONSE);
    const eWeaponType lastWeaponDamage = m_pPlayerPed->GetLastWeaponDamage();
    const bool        isVehicleImpactFall = physicalRoot && physicalRoot->GetTaskType() == TASK_COMPLEX_FALL_AND_GET_UP && physicalLeaf &&
                                     (physicalLeaf->GetTaskType() == TASK_SIMPLE_FALL || physicalLeaf->GetTaskType() == TASK_SIMPLE_GET_UP) &&
                                     (lastWeaponDamage == WEAPONTYPE_RAMMEDBYCAR || lastWeaponDamage == WEAPONTYPE_RUNOVERBYCAR);
    if (isVehicleImpactFall)
    {
        // KillPedWithCar puts its fall/get-up chain in the physical slot. Do
        // not let that exact old-owner reaction keep applying movement after
        // a handoff; every other physical response remains in MTA's existing
        // damage pipeline.
        KillTask(TASK_PRIORITY_PHYSICAL_RESPONSE, true);
    }

    // These two slots are entirely GTA event-handler responses. Once this peer
    // is fenced as an ambient observer, letting any previous generation remain
    // would keep old-owner AI alive beside the new authority. The event type can
    // already have advanced by handoff time, so task-type/event-type filtering
    // is not a reliable ownership boundary. Every other physical response,
    // primary wander and the permanent default task are deliberately left
    // untouched.
    for (const int priority : {TASK_PRIORITY_EVENT_RESPONSE_TEMP, TASK_PRIORITY_EVENT_RESPONSE_NONTEMP})
        KillTask(priority, true);
}

bool CClientPed::SetStoryProtected(bool enabled)
{
    if (GetType() != CCLIENTPED)
        return false;

    if (m_bStoryProtected == enabled)
        return true;

    if (!enabled && m_pPlayerPed && m_storyProtectionNativeState)
        m_pPlayerPed->SetStoryProtectionState(*m_storyProtectionNativeState);

    m_bStoryProtected = enabled;
    m_storyProtectionNativeState.reset();

    if (enabled && m_pPlayerPed)
        ApplyStoryProtectionState();

    // Independent scalar policies must win over the grouped protagonist
    // policy regardless of the order in which a resource applies them.
    ApplySuffersCriticalHitsState();
    ApplyCanBeDraggedOutState();
    ApplyOnlyDamagedByPlayerState();
    ApplyNeverTargetedState();

    return true;
}

void CClientPed::ApplyStoryProtectionState()
{
    m_storyProtectionNativeState.reset();
    if (!m_bStoryProtected || !m_pPlayerPed)
        return;

    // Story scripts combine five independent CPed bits. Preserve their prior
    // values so a resource can relinquish this policy without contaminating
    // an actor that it does not own.
    m_storyProtectionNativeState = m_pPlayerPed->GetStoryProtectionState();
    auto state = *m_storyProtectionNativeState;
    state.neverTargeted = true;
    state.noCriticalHits = true;
    state.cannotBeDraggedOut = true;
    state.stayInCarWhenJacked = true;
    state.getOutOfUpsideDownCar = false;
    m_pPlayerPed->SetStoryProtectionState(state);
}

bool CClientPed::SetSuffersCriticalHits(bool suffersCriticalHits)
{
    if (GetType() != CCLIENTPED)
        return false;

    // Keep this bit separate from the grouped protagonist policy. Enemy
    // actors often disable critical hits without becoming untargetable or
    // protected from vehicle jacking.
    m_suffersCriticalHits = suffersCriticalHits;
    ApplySuffersCriticalHitsState();
    return true;
}

void CClientPed::ApplySuffersCriticalHitsState()
{
    if (!m_suffersCriticalHits || !m_pPlayerPed)
        return;

    auto state = m_pPlayerPed->GetStoryProtectionState();
    state.noCriticalHits = !*m_suffersCriticalHits;
    m_pPlayerPed->SetStoryProtectionState(state);
}

bool CClientPed::CanBeDraggedOut() const
{
    if (m_canBeDraggedOut)
        return *m_canBeDraggedOut;

    return !m_pPlayerPed || !m_pPlayerPed->GetStoryProtectionState().cannotBeDraggedOut;
}

bool CClientPed::SetCanBeDraggedOut(bool canBeDraggedOut)
{
    if (GetType() != CCLIENTPED)
        return false;

    m_canBeDraggedOut = canBeDraggedOut;
    ApplyCanBeDraggedOutState();
    return true;
}

void CClientPed::ApplyCanBeDraggedOutState()
{
    if (!m_canBeDraggedOut || !m_pPlayerPed)
        return;

    auto state = m_pPlayerPed->GetStoryProtectionState();
    state.cannotBeDraggedOut = !*m_canBeDraggedOut;
    m_pPlayerPed->SetStoryProtectionState(state);
}

bool CClientPed::IsOnlyDamagedByPlayer() const
{
    if (m_onlyDamagedByPlayer)
        return *m_onlyDamagedByPlayer;

    return m_pPlayerPed && m_pPlayerPed->GetStoryProtectionState().onlyDamagedByPlayer;
}

bool CClientPed::SetOnlyDamagedByPlayer(bool onlyDamagedByPlayer)
{
    if (GetType() != CCLIENTPED)
        return false;

    m_onlyDamagedByPlayer = onlyDamagedByPlayer;
    ApplyOnlyDamagedByPlayerState();
    return true;
}

void CClientPed::ApplyOnlyDamagedByPlayerState()
{
    if (!m_onlyDamagedByPlayer || !m_pPlayerPed)
        return;

    // Opcode 02A9 uses CPhysical::bInvulnerable. GTA's CEventDamage then
    // rejects non-player attackers while deliberately retaining player damage.
    auto state = m_pPlayerPed->GetStoryProtectionState();
    state.onlyDamagedByPlayer = *m_onlyDamagedByPlayer;
    m_pPlayerPed->SetStoryProtectionState(state);
}

bool CClientPed::GetStayInSamePlace() const
{
    if (m_stayInSamePlace)
        return *m_stayInSamePlace;

    return m_pPlayerPed && m_pPlayerPed->GetScriptStayInSamePlace();
}

bool CClientPed::SetStayInSamePlace(bool stayInSamePlace)
{
    if (GetType() != CCLIENTPED)
        return false;

    m_stayInSamePlace = stayInSamePlace;
    ApplyStayInSamePlaceState();
    return true;
}

void CClientPed::ApplyStayInSamePlaceState()
{
    if (m_stayInSamePlace && m_pPlayerPed)
        m_pPlayerPed->SetScriptStayInSamePlace(*m_stayInSamePlace);
}

bool CClientPed::IsNeverTargeted() const
{
    if (m_neverTargeted)
        return *m_neverTargeted;

    return m_pPlayerPed && m_pPlayerPed->GetStoryProtectionState().neverTargeted;
}

bool CClientPed::SetNeverTargeted(bool neverTargeted)
{
    if (GetType() != CCLIENTPED)
        return false;

    m_neverTargeted = neverTargeted;
    ApplyNeverTargetedState();
    return true;
}

void CClientPed::ApplyNeverTargetedState()
{
    if (!m_neverTargeted || !m_pPlayerPed)
        return;

    auto state = m_pPlayerPed->GetStoryProtectionState();
    state.neverTargeted = *m_neverTargeted;
    m_pPlayerPed->SetStoryProtectionState(state);
}

SPhysicalProofs CClientPed::GetScriptPhysicalProofs() const
{
    if (m_pPlayerPed)
        return m_pPlayerPed->GetPhysicalProofs();
    return m_scriptPhysicalProofs.value_or(SPhysicalProofs{});
}

bool CClientPed::SetScriptPhysicalProofs(const SPhysicalProofs& proofs)
{
    if (GetType() != CCLIENTPED)
        return false;

    // Proofs belong to the synchronized ped policy rather than a particular
    // native CPed instance. Retaining the tuple makes opcode 02AB survive the
    // same stream and owner lifecycle as the rest of the mission actor.
    m_scriptPhysicalProofs = proofs;
    ApplyScriptPhysicalProofsState();
    return true;
}

void CClientPed::ApplyScriptPhysicalProofsState()
{
    if (m_scriptPhysicalProofs && m_pPlayerPed)
        m_pPlayerPed->SetPhysicalProofs(*m_scriptPhysicalProofs);
}

void CClientPed::InternalWarpIntoVehicle(CVehicle* pGameVehicle)
{
    if (m_pPlayerPed)
    {
        // Create a task to warp the player in and execute it
        CTaskSimpleCarSetPedInAsDriver* pInTask = g_pGame->GetTasks()->CreateTaskSimpleCarSetPedInAsDriver(pGameVehicle);
        if (pInTask)
        {
            pInTask->SetIsWarpingPedIntoCar();
            pInTask->ProcessPed(m_pPlayerPed);
            pInTask->Destroy();
            SetWarpInToVehicleRequired(false);
        }

        // If we're a remote player
        if (!m_bIsLocalPlayer)
        {
            // Make sure we can't fall off
            SetCanBeKnockedOffBike(false);
        }
    }
}

void CClientPed::InternalRemoveFromVehicle(CVehicle* pGameVehicle)
{
    if (m_pPlayerPed && m_pTaskManager)
    {
        SetWarpInToVehicleRequired(false);

        // Reset whatever task
        m_pTaskManager->RemoveTask(TASK_PRIORITY_PRIMARY);

        // Create a task to warp the player in and execute it
        CTaskSimpleCarSetPedOut* pOutTask = g_pGame->GetTasks()->CreateTaskSimpleCarSetPedOut(pGameVehicle, 1, false);
        if (pOutTask)
        {
            // May seem illogical, but it'll crash without this
            pOutTask->SetKnockedOffBike();

            pOutTask->ProcessPed(m_pPlayerPed);
            pOutTask->SetIsWarpingPedOutOfCar();
            pOutTask->Destroy();
        }

        m_Matrix.vPos = *m_pPlayerPed->GetPosition();

        // Local player?
        if (m_bIsLocalPlayer)
        {
            // Turn off the radio
            StopRadio();
        }
    }
}

bool CClientPed::PerformChecks()
{
    // Must be streamed in
    if (m_pPlayerPed)
    {
        // Is this the local player?
        if (m_bIsLocalPlayer)
        {
            // Is GTA's health/armor less than or equal to our health/armor?
            // The player should not be able to gain any health/armor without us knowing..
            // meaning all health/armor giving must go through SetHealth/SetArmor.
            if ((m_fHealth > 0.0f && m_pPlayerPed->GetHealth() > m_fHealth + FLOAT_EPSILON) ||
                (m_armor < 100.0f && m_pPlayerPed->GetArmor() > m_armor + FLOAT_EPSILON))
            {
                g_pCore->GetConsole()->Printf("healthCheck: %f %f", m_pPlayerPed->GetHealth(), m_fHealth);
                g_pCore->GetConsole()->Printf("armorCheck: %f %f", m_pPlayerPed->GetArmor(), m_armor);
                return false;
            }
            // Perform the checks in CGame
            if (!g_pGame->PerformChecks())
            {
                return false;
            }
        }
    }

    // Player is not a cheater yet
    return true;
}

void CClientPed::StartRadio()
{
    // We use this to avoid radio lags sometimes. Also make sure
    // it's not already on
    if (!m_bDontChangeRadio && !m_bRadioOn)
    {
        // Turn it on if we're not on channel none
        if (m_ucRadioChannel != 0)
            g_pGame->GetAudioEngine()->StartRadio(m_ucRadioChannel);

        m_bRadioOn = true;
    }
}

void CClientPed::StopRadio()
{
    // We use this to avoid radio lags sometimes
    if (!m_bDontChangeRadio)
    {
        // Stop the radio and mark it as off
        g_pGame->GetAudioEngine()->StopRadio();
        m_bRadioOn = false;
    }
}

void CClientPed::Duck(bool bDuck)
{
    if (m_pPlayerPed)
    {
        if (bDuck)
        {
            // Check if he's already ducking
            CTask* pTaskDuck = m_pTaskManager->GetTaskSecondary(TASK_SECONDARY_DUCK);
            if (!pTaskDuck || pTaskDuck->GetTaskType() != TASK_SIMPLE_DUCK)
            {
                // DUCK_TASK_CONTROLLED means we can move around while ducked, I think
                pTaskDuck = g_pGame->GetTasks()->CreateTaskSimpleDuck(DUCK_TASK_CONTROLLED);
                if (pTaskDuck)
                {
                    pTaskDuck->SetAsSecondaryPedTask(m_pPlayerPed, TASK_SECONDARY_DUCK);
                }
            }
        }
        else
        {
            // Reset ducking
            m_ulLastTimeBeganCrouch = 0;
            // Jax: lets give this a whirl (it seems to cancel the task automatically)
            m_pPlayerPed->SetDucking(false);
        }
    }
    m_bDucked = bDuck;
}

bool CClientPed::IsDucked()
{
    if (m_pPlayerPed)
    {
        CTask* pTaskDuck = m_pTaskManager->GetTaskSecondary(TASK_SECONDARY_DUCK);
        if (pTaskDuck)
        {
            return true;
        }
    }

    return m_bDucked;
}

void CClientPed::SetChoking(bool bChoking)
{
    // Remember the choking state
    m_bIsChoking = bChoking;

    // We have a task manager?
    if (m_pTaskManager && m_pPlayerPed)
    {
        // Grab the physical response task. Is he already choking?
        CTask* pTask = m_pTaskManager->GetTask(TASK_PRIORITY_PHYSICAL_RESPONSE);
        if (pTask && pTask->GetTaskType() == TASK_SIMPLE_CHOKING)
        {
            // Make him stop choking if that's what we're supposed to do
            if (!bChoking)
            {
                m_pTaskManager->RemoveTask(TASK_PRIORITY_PHYSICAL_RESPONSE);
            }
        }
        else
        {
            // His not choking. Make him choke if that's what we're supposed to do.
            if (bChoking)
            {
                m_pPlayerPed->SetLanding(false);

                // Remove jetpack now so it doesn't stay on (#9522#c25612)
                if (HasJetPack())
                    SetHasJetPack(false);

                // Let's kill any animation
                KillAnimation();

                // Create the choking task
                CTaskSimpleChoking* pTask = g_pGame->GetTasks()->CreateTaskSimpleChoking(NULL, true);
                if (pTask)
                {
                    pTask->SetAsPedTask(m_pPlayerPed, TASK_PRIORITY_PHYSICAL_RESPONSE);
                }
            }
        }
    }
}

bool CClientPed::IsChoking()
{
    // We have a task manager?
    if (m_pTaskManager)
    {
        // Return whether we have a physical task and it's choking
        CTask* pTask = m_pTaskManager->GetTask(TASK_PRIORITY_PHYSICAL_RESPONSE);
        return (pTask && pTask->GetTaskType() == TASK_SIMPLE_CHOKING);
    }
    else
    {
        // Otherwize remember the state we've stored
        return m_bIsChoking;
    }
}

void CClientPed::SetWearingGoggles(bool bWearing, bool animationEnabled)
{
    if (m_pPlayerPed)
    {
        if (bWearing != IsWearingGoggles())
        {
            // Make him wear goggles
            m_pPlayerPed->SetGogglesState(bWearing);

            // Are our goggle anims loaded?
            if (animationEnabled)
            {
                std::unique_ptr<CAnimBlock> pBlock = g_pGame->GetAnimManager()->GetAnimationBlock("GOGGLES");
                if (pBlock->IsLoaded())
                {
                    BlendAnimation(ANIM_GROUP_GOGGLES, ANIM_ID_GOGGLES_ON, 4.0f);
                }
            }
        }
    }
    m_bWearingGoggles = bWearing;
}

bool CClientPed::IsWearingGoggles(bool bCheckMoving)
{
    if (m_pPlayerPed)
    {
        if (bCheckMoving)
        {
            bool bPuttingOn;
            if (IsMovingGoggles(bPuttingOn))
                return bPuttingOn;
        }

        return m_pPlayerPed->IsWearingGoggles();
    }
    return m_bWearingGoggles;
}

bool CClientPed::IsMovingGoggles(bool& bPuttingOn)
{
    if (m_pPlayerPed)
    {
        CTask* pTask = m_pTaskManager->GetTask(TASK_PRIORITY_PRIMARY);
        if (pTask && pTask->GetTaskType() == TASK_COMPLEX_USE_GOGGLES)
        {
            pTask = pTask->GetSubTask();
            if (pTask)
            {
                if (pTask->GetTaskType() == TASK_SIMPLE_GOGGLES_ON)
                {
                    bPuttingOn = true;
                    return true;
                }
                else if (pTask->GetTaskType() == TASK_SIMPLE_GOGGLES_OFF)
                {
                    bPuttingOn = false;
                    return true;
                }
            }
        }
    }
    return false;
}

void CClientPed::_GetIntoVehicle(CClientVehicle* pVehicle, unsigned int uiSeat, unsigned char ucDoor)
{
    assert(m_pOccupiedVehicle == NULL);
    assert(m_pOccupyingVehicle == NULL || m_pOccupyingVehicle == pVehicle);

    // Check for swimming task and warp to door.
    CTask* pTask = 0;
    if (m_pTaskManager)
        pTask = m_pTaskManager->GetTask(TASK_PRIORITY_EVENT_RESPONSE_NONTEMP);
    auto usVehicleModel = static_cast<VehicleType>(pVehicle->GetModel());
    if (((pTask && pTask->GetTaskType() == TASK_COMPLEX_IN_WATER) || pVehicle->IsOnWater()) &&
        (usVehicleModel == VehicleType::VT_SKIMMER || usVehicleModel == VehicleType::VT_SEASPAR || usVehicleModel == VehicleType::VT_LEVIATHN ||
         usVehicleModel == VehicleType::VT_VORTEX))
    {
        CVector      vecDoorPos;
        unsigned int uiDoor;
        GetClosestDoor(pVehicle, uiSeat == 0, uiSeat != 0, uiDoor, &vecDoorPos);
        Teleport(vecDoorPos);
    }
    // Driverseat
    if (uiSeat == 0)
    {
        if (m_bIsLocalPlayer)
        {
            pVehicle->SetSwingingDoorsAllowed(true);
        }

        if (m_pPlayerPed)
        {
            // Grab the game vehicle. If it exists, begin walking the player into it
            CVehicle* pGameVehicle = pVehicle->m_pVehicle;
            if (pGameVehicle)
            {
                // Create and set the get-in task
                CTaskComplexEnterCarAsDriver* pInTask = g_pGame->GetTasks()->CreateTaskComplexEnterCarAsDriver(pGameVehicle);
                if (pInTask)
                {
                    pInTask->SetTargetDoor(ucDoor);
                    pInTask->SetAsPedTask(m_pPlayerPed, TASK_PRIORITY_PRIMARY, true);
                }
            }
        }

        // Tell the vehicle that we're occupying it
        CClientVehicle::SetPedOccupyingVehicle(this, pVehicle, uiSeat, ucDoor);
    }
    else
    {
        // HACK: Grabs the closest passenger-door for bikes
        eClientVehicleType vehicleType = CClientVehicleManager::GetVehicleType(pVehicle->m_usModel);
        if (vehicleType == CLIENTVEHICLE_BIKE || vehicleType == CLIENTVEHICLE_QUADBIKE)
        {
            unsigned int uiTemp;
            if (GetClosestDoor(pVehicle, false, true, uiTemp))
            {
                uiSeat = uiTemp;
            }
        }

        unsigned char ucSeat = CClientVehicleManager::ConvertIndexToGameSeat(pVehicle->m_usModel, static_cast<unsigned char>(uiSeat));
        if (ucSeat != 0 && ucSeat != 0xFF)
        {
            if (m_pPlayerPed)
            {
                // Grab the game vehicle. If it exists, begin walking the player into it
                CVehicle* pGameVehicle = pVehicle->m_pVehicle;
                if (pGameVehicle)
                {
                    // Create the task for walking him in
                    CTaskComplexEnterCarAsPassenger* pInTask = g_pGame->GetTasks()->CreateTaskComplexEnterCarAsPassenger(pGameVehicle, ucSeat, false);
                    if (pInTask)
                    {
                        pInTask->SetTargetDoor(ucDoor);
                        pInTask->SetAsPedTask(m_pPlayerPed, TASK_PRIORITY_PRIMARY, true);
                    }
                }
            }

            // Tell the vehicle we're occupying it
            CClientVehicle::SetPedOccupyingVehicle(this, pVehicle, uiSeat, ucDoor);
        }
    }
}

bool CClientPed::SetHasJetPack(bool bHasJetPack)
{
    if (m_pPlayerPed)
    {
        if (bHasJetPack)
        {
            if (!IsInVehicle() && !HasJetPack())
            {
                // jumping task
                CTask* pTask = m_pTaskManager->GetTask(TASK_PRIORITY_PRIMARY);
                if (pTask)
                {
                    pTask->MakeAbortable(m_pPlayerPed, ABORT_PRIORITY_IMMEDIATE, NULL);
                    pTask->Destroy();
                    m_pTaskManager->RemoveTask(TASK_PRIORITY_PRIMARY);
                }
                // falling task
                pTask = m_pTaskManager->GetTask(TASK_PRIORITY_EVENT_RESPONSE_TEMP);
                if (pTask)
                {
                    pTask->MakeAbortable(m_pPlayerPed, ABORT_PRIORITY_IMMEDIATE, NULL);
                    pTask->Destroy();
                    m_pTaskManager->RemoveTask(TASK_PRIORITY_EVENT_RESPONSE_TEMP);
                }
                // swimming task
                pTask = m_pTaskManager->GetTask(TASK_PRIORITY_EVENT_RESPONSE_NONTEMP);
                if (pTask)
                {
                    pTask->MakeAbortable(m_pPlayerPed, ABORT_PRIORITY_IMMEDIATE, NULL);
                    pTask->Destroy();
                    m_pTaskManager->RemoveTask(TASK_PRIORITY_EVENT_RESPONSE_NONTEMP);
                }

                // Kill choking state now so it doesn't stay on (#9522#c26644)
                if (IsChoking())
                    SetChoking(false);

                // Kill animation as well
                KillAnimation();

                CTaskSimpleJetPack* pJetPackTask = g_pGame->GetTasks()->CreateTaskSimpleJetpack();
                if (pJetPackTask)
                {
                    pJetPackTask->SetAsPedTask(m_pPlayerPed, TASK_PRIORITY_PRIMARY, true);
                    m_bHasJetPack = true;
                    return true;
                }
            }
            return false;
        }
        else
        {
            CTask* pPrimaryTask = m_pTaskManager->GetSimplestActiveTask();
            if (pPrimaryTask && pPrimaryTask->GetTaskType() == TASK_SIMPLE_JETPACK)
            {
                pPrimaryTask->MakeAbortable(m_pPlayerPed, ABORT_PRIORITY_URGENT, NULL);
            }

            m_bHasJetPack = false;
            return true;
        }
    }
    m_bHasJetPack = bHasJetPack;
    return true;
}

bool CClientPed::HasJetPack()
{
    if (m_pPlayerPed)
    {
        CTask* pPrimaryTask = m_pTaskManager->GetSimplestActiveTask();
        if (pPrimaryTask && pPrimaryTask->GetTaskType() == TASK_SIMPLE_JETPACK)
        {
            auto* jetpackTask = dynamic_cast<CTaskSimpleJetPack*>(pPrimaryTask);
            if (jetpackTask && jetpackTask->IsFinished())
                return false;

            return true;
        }
        return false;
    }
    return m_bHasJetPack;
}

bool CClientPed::IsInWater()
{
    if (m_pPlayerPed)
    {
        CTask* pTask = m_pTaskManager->GetTask(TASK_PRIORITY_EVENT_RESPONSE_NONTEMP);
        if (pTask)
        {
            if (pTask->GetTaskType() == TASK_COMPLEX_IN_WATER)
            {
                return true;
            }
        }
        return false;
    }
    return m_bIsInWater;
}

float CClientPed::GetDistanceFromGround()
{
    CVector vecPosition;
    GetPosition(vecPosition);
    float fGroundLevel = static_cast<float>(g_pGame->GetWorld()->FindGroundZFor3DPosition(&vecPosition));

    return (vecPosition.fZ - fGroundLevel);
}

bool CClientPed::IsOnGround(bool checkVehicles)
{
    CVector vecPosition;
    GetPosition(vecPosition);
    float fGroundLevel = static_cast<float>(g_pGame->GetWorld()->FindGroundZFor3DPosition(&vecPosition));

    if (DefinitelyLessThan(vecPosition.fZ, fGroundLevel))
        return false;

    bool isOnGround = DefinitelyLessThan((vecPosition.fZ - fGroundLevel), 1.0f, 1e-4f) || EssentiallyEqual((vecPosition.fZ - fGroundLevel), 1.0f, 1e-4f);
    if (!isOnGround && checkVehicles && m_pPlayerPed)
        return m_pPlayerPed->IsStandingOnEntity();

    return isOnGround;
}

bool CClientPed::IsClimbing()
{
    if (m_pPlayerPed)
    {
        CTask* pPrimaryTask = m_pTaskManager->GetSimplestActiveTask();
        if (pPrimaryTask && pPrimaryTask->GetTaskType() == TASK_SIMPLE_CLIMB)
        {
            return true;
        }
    }
    return false;
}

void CClientPed::NextRadioChannel()
{
    // Is our radio on?
    if (m_bRadioOn)
    {
        SetCurrentRadioChannel((m_ucRadioChannel + 1) % 13);
    }
}

void CClientPed::PreviousRadioChannel()
{
    // Is our radio on?
    if (m_bRadioOn)
    {
        if (m_ucRadioChannel == 0)
        {
            m_ucRadioChannel = 13;
        }

        SetCurrentRadioChannel(m_ucRadioChannel - 1);
    }
}

bool CClientPed::SetCurrentRadioChannel(unsigned char ucChannel)
{
    // Local player?
    if (m_bIsLocalPlayer && ucChannel <= 12)
    {
        if (m_ucRadioChannel != ucChannel)
        {
            CLuaArguments Arguments;
            Arguments.PushNumber(ucChannel);
            if (!CallEvent("onClientPlayerRadioSwitch", Arguments, true))
            {
                // if we cancel the radio channel setting at 12 then when they go through previous it will get to 0, then the next time it is used set to 13 in
                // preperation to set to 12 but if it is cancelled it stays at 13. Issue 6113 - Caz
                if (m_ucRadioChannel == 13)
                    m_ucRadioChannel = 0;

                return false;
            }
        }

        m_ucRadioChannel = ucChannel;

        g_pGame->GetAudioEngine()->StartRadio(m_ucRadioChannel);
        if (m_ucRadioChannel == 0)
            g_pGame->GetAudioEngine()->StopRadio();

        return true;
    }
    return false;
}

bool CClientPed::GetShotData(CVector* pvecOrigin, CVector* pvecTarget, CVector* pvecGunMuzzle, CVector* pvecFireOffset, float* fAimX, float* fAimY)
{
    CWeapon* pWeapon = GetWeapon(GetCurrentWeaponSlot());
    if (!pWeapon)
        return false;

    unsigned char    ucWeaponType = pWeapon->GetType();
    CClientVehicle*  pVehicle = GetRealOccupiedVehicle();
    CControllerState Controller;
    GetControllerState(Controller);
    float fRotation = GetCurrentRotation();

    // Grab the target range of the current weapon
    float        fSkill = 1000.0f;  //  GetStat ( g_pGame->GetStats ( )->GetSkillStatIndex ( pWeapon->GetType ( ) ) );
    CWeaponStat* pCurrentWeaponInfo = g_pGame->GetWeaponStatManager()->GetWeaponStatsFromSkillLevel(pWeapon->GetType(), fSkill);
    float        fRange = pCurrentWeaponInfo->GetWeaponRange();

    // Grab the gun muzzle position
    CVector vecFireOffset = *pCurrentWeaponInfo->GetFireOffset();
    CVector vecGunMuzzle = vecFireOffset;
    GetTransformedBonePosition(BONE_RIGHTWRIST, vecGunMuzzle);

    CVector vecOrigin, vecTarget;
    if (m_bIsLocalPlayer)
    {
        if (pCurrentWeaponInfo->IsFlagSet(WEAPONTYPE_FIRSTPERSON))
        {
            // Grab the active cam
            CCamera* pCamera = g_pGame->GetCamera();
            CCam*    pActive = pCamera->GetCam(pCamera->GetActiveCam());

            // Find the target position
            CVector vecFront = *pActive->GetFront();
            vecFront.Normalize();
            vecOrigin = *pActive->GetSource();
            // Jax: advance along the line 2 units (seems to decrease the chance of corrupt bullet vectors)
            vecOrigin += (vecFront * 2.0f);
            vecTarget = vecOrigin + (vecFront * fRange);

            // Apply shoot through walls fix
            vecOrigin = AdjustShotOriginForWalls(vecOrigin, vecTarget, 2.5f);
        }
        else
        {
            // Always use the gun muzzle as origin
            vecOrigin = vecGunMuzzle;

            if (false && HasAkimboPointingUpwards())  // Upwards pointing akimbo's
            {
                // Disabled temporarily until we actually get working akimbos
                vecTarget = vecOrigin;
                vecTarget.fZ += fRange;
            }
            else if (Controller.RightShoulder1 == 255)  // First-person weapons, crosshair active: sync the crosshair
            {
                g_pGame->GetCamera()->Find3rdPersonCamTargetVector(fRange, &vecGunMuzzle, &vecOrigin, &vecTarget);
                // Apply shoot through walls fix
                vecOrigin = AdjustShotOriginForWalls(vecOrigin, vecTarget, 0.5f);
            }
            else if (pVehicle)  // Drive-by/vehicle weapons: camera origin as origin, performing collision tests
            {
                CColPoint* pCollision;
                CMatrix    mat;
                bool       bCollision;

                g_pGame->GetCamera()->GetMatrix(&mat);

                CVector vecCameraOrigin = mat.vPos;
                CVector vecTemp = vecCameraOrigin;
                g_pGame->GetCamera()->Find3rdPersonCamTargetVector(fRange, &vecCameraOrigin, &vecTemp, &vecTarget);

                bCollision = g_pGame->GetWorld()->ProcessLineOfSight(&mat.vPos, &vecTarget, &pCollision, NULL);
                if (pCollision)
                {
                    if (bCollision)
                    {
                        CVector vecBullet = pCollision->GetPosition() - vecOrigin;
                        vecBullet.Normalize();
                        vecTarget = vecOrigin + (vecBullet * fRange);
                    }
                    pCollision->Destroy();
                }
            }
            else
            {
                // For shooting without the crosshair showing (just holding the fire button)
                vecOrigin = vecGunMuzzle;

                float   fTemp = 6.283152f - fRotation;
                CVector vecTemp = CVector(sin(fTemp), cos(fTemp), 0.0f);
                vecTarget = CVector(vecOrigin.fX + (vecTemp.fX * fRange), vecOrigin.fY + (vecTemp.fY * fRange), vecOrigin.fZ);
            }
        }
    }
    else  // Always use only the last reported shot data for remote players?
    {
        vecOrigin = m_shotSyncData->m_vecShotOrigin;
        vecTarget = m_shotSyncData->m_vecShotTarget;
    }

    if (pvecOrigin)
        *pvecOrigin = vecOrigin;
    if (pvecTarget)
        *pvecTarget = vecTarget;
    if (pvecFireOffset)
        *pvecFireOffset = vecFireOffset;
    if (pvecGunMuzzle)
        *pvecGunMuzzle = vecGunMuzzle;
    if (fAimX)
        *fAimX = m_shotSyncData->m_fArmDirectionX;
    if (fAimY)
        *fAimY = m_shotSyncData->m_fArmDirectionY;
    return true;
}

//
// Fix firing through walls by pulling back shot origin when next to a wall
//
CVector CClientPed::AdjustShotOriginForWalls(const CVector& vecOrigin, const CVector& vecTarget, float fMaxPullBack)
{
    CVector vecResultOrigin = vecOrigin;

    // Do a short line of sight check from the max pullback position
    CVector vecFront = (vecTarget - vecOrigin);
    vecFront.Normalize();
    CVector vecTempOrigin = vecOrigin - vecFront * fMaxPullBack;
    CVector vecTempTarget = vecOrigin + vecFront * 1;

    g_pGame->GetWorld()->IgnoreEntity(m_pPlayerPed);
    CColPoint* pCollision;
    bool       bCollision = g_pGame->GetWorld()->ProcessLineOfSight(&vecTempOrigin, &vecTempTarget, &pCollision, NULL);
    g_pGame->GetWorld()->IgnoreEntity(NULL);

    if (pCollision)
    {
        if (bCollision)
        {
            float fDist = (pCollision->GetPosition() - vecTempOrigin).Length();

            if (fDist < fMaxPullBack)
            {
                // If wall is hit, move origin back to the wall
                float fFrontMul = fDist - fMaxPullBack;
                vecResultOrigin = vecOrigin + (vecFront * fFrontMul);
            }
        }
        pCollision->Destroy();
    }

    return vecResultOrigin;
}

eFightingStyle CClientPed::GetFightingStyle()
{
    if (m_pPlayerPed)
    {
        return m_pPlayerPed->GetFightingStyle();
    }
    return m_FightingStyle;
}

void CClientPed::SetFightingStyle(eFightingStyle style)
{
    if (m_pPlayerPed)
    {
        m_pPlayerPed->SetFightingStyle(style, 6);
    }
    m_FightingStyle = style;
}

eMoveAnim CClientPed::GetMoveAnim()
{
    // Keep getPedWalkingStyle consistent with the server while the model-native
    // policy is active. The dedicated boolean getter exposes that policy.
    if (m_bUseNativeWalkingStyle)
        return m_MoveAnim;

    if (m_pPlayerPed)
    {
        return m_pPlayerPed->GetMoveAnim();
    }
    return m_MoveAnim;
}

void CClientPed::SetMoveAnim(eMoveAnim iAnim)
{
    if (iAnim == MOVE_NATIVE)
    {
        SetUseNativeWalkingStyle(true);
        return;
    }

    if (!IsValidMoveAnim(iAnim))
        return;

    const bool bWasUsingNativeWalkingStyle = m_bUseNativeWalkingStyle;
    m_bUseNativeWalkingStyle = false;
    if (m_pPlayerPed)
    {
        // Clear the internal marker first. If loading an explicit animation
        // block fails, the engine must not silently remain in native mode.
        if (bWasUsingNativeWalkingStyle)
            m_pPlayerPed->SetMoveAnim(MOVE_DEFAULT);
        m_pPlayerPed->SetMoveAnim(iAnim);
    }
    m_MoveAnim = iAnim;
}

void CClientPed::SetUseNativeWalkingStyle(bool bEnabled)
{
    if (m_bUseNativeWalkingStyle == bEnabled)
        return;

    // Native mode and explicit numeric styles are mutually exclusive. This
    // mirrors the server's last-setter-wins behavior and avoids hidden state.
    m_bUseNativeWalkingStyle = bEnabled;
    m_MoveAnim = MOVE_DEFAULT;

    if (m_pPlayerPed)
        m_pPlayerPed->SetMoveAnim(bEnabled ? MOVE_NATIVE : MOVE_DEFAULT);
}

unsigned int CClientPed::CountProjectiles(eWeaponType weaponType)
{
    if (weaponType == WEAPONTYPE_UNARMED)
        return static_cast<unsigned int>(m_Projectiles.size());

    unsigned int                       uiCount = 0;
    list<CClientProjectile*>::iterator iter = m_Projectiles.begin();
    for (; iter != m_Projectiles.end(); iter++)
    {
        if ((*iter)->GetWeaponType() == weaponType)
        {
            uiCount++;
        }
    }
    return uiCount;
}

void CClientPed::RemoveAllProjectiles()
{
    CClientProjectile*                 pProjectile = NULL;
    list<CClientProjectile*>::iterator iter = m_Projectiles.begin();
    for (; iter != m_Projectiles.end(); iter++)
    {
        pProjectile = *iter;
        pProjectile->m_pCreator = NULL;
        pProjectile->Destroy();
    }
    m_Projectiles.clear();
}

void CClientPed::DestroySatchelCharges(bool bBlow, bool bDestroy)
{
    // Don't allow any recurrance
    if (m_bDestroyingSatchels)
        return;
    m_bDestroyingSatchels = true;

    CClientProjectile* pProjectile = NULL;
    CVector            vecPosition;

    list<CClientProjectile*>::iterator iter = m_Projectiles.begin();
    while (iter != m_Projectiles.end())
    {
        pProjectile = *iter;

        if (pProjectile->GetWeaponType() == WEAPONTYPE_REMOTE_SATCHEL_CHARGE)
        {
            if (bBlow)
            {
                pProjectile->GetPosition(vecPosition);
                CLuaArguments Arguments;
                Arguments.PushNumber(vecPosition.fX);
                Arguments.PushNumber(vecPosition.fY);
                Arguments.PushNumber(vecPosition.fZ);
                Arguments.PushNumber(EXP_TYPE_GRENADE);
                bool bCancelExplosion = !CallEvent("onClientExplosion", Arguments, true);

                if (!bCancelExplosion)
                    m_pManager->GetExplosionManager()->Create(EXP_TYPE_GRENADE, vecPosition, this, true, -1.0f, false, WEAPONTYPE_REMOTE_SATCHEL_CHARGE);
            }
            if (bDestroy)
            {
                pProjectile->Destroy(bBlow);
            }
        }
        iter++;
    }

    m_bDestroyingSatchels = false;
}

bool CClientPed::IsEnteringVehicle()
{
    if (m_pPlayerPed)
    {
        CTask* pTask = GetCurrentPrimaryTask();
        if (pTask)
        {
            switch (pTask->GetTaskType())
            {
                case TASK_COMPLEX_ENTER_CAR_AS_DRIVER:
                {
                    CTask* pSubTask = pTask->GetSubTask();
                    // Peds will have TASK_SIMPLE_CAR_DRIVE_TIMED subtask after entering, so we make an exception
                    if (pSubTask && pSubTask->GetTaskType() == TASK_SIMPLE_CAR_DRIVE_TIMED)
                    {
                        return false;
                    }
                    return true;
                    break;
                }
                case TASK_COMPLEX_ENTER_CAR_AS_PASSENGER:
                {
                    CTask* pSubTask = pTask->GetSubTask();
                    if (pSubTask && pSubTask->GetTaskType() == TASK_SIMPLE_CAR_DRIVE_TIMED)
                    {
                        return false;
                    }
                    return true;
                    break;
                }
                default:
                    break;
            }
        }
    }
    return false;
}

bool CClientPed::IsLeavingVehicle()
{
    return FindLeavingVehicleTaskPriority() != TASK_PRIORITY_MAX;
}

int CClientPed::FindLeavingVehicleTaskPriority()
{
    if (m_pPlayerPed && m_pTaskManager)
    {
        // Scripted exits normally live under PRIMARY, but emergency jump-outs
        // are event-response tasks. Inspect every removable primary slot so
        // both forms enter the same reliable vehicle lifecycle. DEFAULT is
        // deliberately excluded because GTA owns that permanent slot.
        for (int priority = TASK_PRIORITY_PHYSICAL_RESPONSE; priority < TASK_PRIORITY_DEFAULT; ++priority)
        {
            for (CTask* task = m_pTaskManager->GetTask(priority); task; task = task->GetSubTask())
            {
                switch (task->GetTaskType())
                {
                    case TASK_COMPLEX_LEAVE_CAR:
                    case TASK_COMPLEX_LEAVE_CAR_AND_DIE:
                    case TASK_COMPLEX_LEAVE_CAR_AND_FLEE:
                    case TASK_COMPLEX_LEAVE_CAR_AND_WANDER:
                    case TASK_COMPLEX_SCREAM_IN_CAR_THEN_LEAVE:
                    case TASK_SIMPLE_CAR_JUMP_OUT:
                        return priority;
                    default:
                        break;
                }
            }
        }
    }

    return TASK_PRIORITY_MAX;
}

void CClientPed::AbortLeavingVehicleTask()
{
    const int priority = FindLeavingVehicleTaskPriority();
    if (priority != TASK_PRIORITY_MAX)
        KillTask(priority, true);
}

bool CClientPed::IsGettingIntoVehicle()
{
    if (m_pPlayerPed)
    {
        CTask* pTask = GetCurrentPrimaryTask();
        if (pTask)
        {
            if (pTask->GetTaskType() == TASK_COMPLEX_ENTER_CAR_AS_DRIVER || pTask->GetTaskType() == TASK_COMPLEX_ENTER_CAR_AS_PASSENGER)
            {
                CTask* pSubTask = pTask->GetSubTask();
                if (pSubTask)
                {
                    switch (pSubTask->GetTaskType())
                    {
                        case TASK_SIMPLE_CAR_GET_IN:
                        case TASK_SIMPLE_CAR_CLOSE_DOOR_FROM_INSIDE:
                        case TASK_SIMPLE_CAR_SHUFFLE:
                        case TASK_COMPLEX_ENTER_BOAT_AS_DRIVER:
                        {
                            return true;
                            break;
                        }
                        default:
                            break;
                    }
                }
            }
        }
    }
    return false;
}

bool CClientPed::IsGettingOutOfVehicle()
{
    if (m_pPlayerPed)
    {
        CTask* pTask = GetCurrentPrimaryTask();
        if (pTask)
        {
            if (pTask->GetTaskType() == TASK_COMPLEX_LEAVE_CAR)
            {
                CTask* pSubTask = pTask->GetSubTask();
                if (pSubTask)
                {
                    switch (pSubTask->GetTaskType())
                    {
                        case TASK_SIMPLE_CAR_GET_OUT:
                        case TASK_SIMPLE_CAR_JUMP_OUT:
                        case TASK_SIMPLE_CAR_CLOSE_DOOR_FROM_OUTSIDE:
                        {
                            return true;
                            break;
                        }
                        default:
                            break;
                    }
                }
            }
        }
    }
    return false;
}

bool CClientPed::IsGettingJacked()
{
    if (m_pPlayerPed)
    {
        CTask* pTask = GetCurrentPrimaryTask();
        if (pTask)
        {
            switch (pTask->GetTaskType())
            {
                case TASK_COMPLEX_CAR_SLOW_BE_DRAGGED_OUT_AND_STAND_UP:
                case TASK_SIMPLE_BIKE_JACKED:
                {
                    return true;
                    break;
                }
                default:
                    break;
            }
        }
    }
    return false;
}

CClientEntity* CClientPed::GetContactEntity()
{
    CPools* pPools = g_pGame->GetPools();
    if (pPools && m_pPlayerPed)
    {
        CEntity* pEntity = m_pPlayerPed->GetContactEntity();
        if (pEntity)
        {
            CEntitySAInterface* pInterface = pEntity->GetInterface();
            eEntityType         entityType = pInterface ? pEntity->GetEntityType() : ENTITY_TYPE_NOTHING;
            if (entityType == ENTITY_TYPE_VEHICLE || entityType == ENTITY_TYPE_OBJECT)
            {
                return pPools->GetClientEntity((DWORD*)pInterface);
            }
        }
    }
    return nullptr;
}

bool CClientPed::HasAkimboPointingUpwards()
{
    if (m_bIsLocalPlayer)
    {
        if (!GetRealOccupiedVehicle())
        {
            CControllerState csController;
            GetControllerState(csController);
            if (csController.RightShoulder1)
            {
                CWeapon* pWeapon = GetWeapon(GetCurrentWeaponSlot());
                if (pWeapon)
                {
                    unsigned char ucWeaponType = pWeapon->GetType();
                    if (ucWeaponType == 22 || ucWeaponType == 26 || ucWeaponType == 28 || ucWeaponType == 32)
                    {
                        if (!IsDucked() && pWeapon->GetState() != WEAPONSTATE_RELOADING)
                        {
                            CTask* pTask = m_pTaskManager->GetTaskSecondary(TASK_SECONDARY_IK);
                            if (pTask && pTask->GetTaskType() == TASK_SIMPLE_IK_MANAGER)
                            {
                                return false;
                            }
                            return true;
                        }
                    }
                }
            }
        }
    }
    return false;
}

float CClientPed::GetDistanceFromCentreOfMassToBaseOfModel()
{
    if (m_pPlayerPed)
    {
        return m_pPlayerPed->GetDistanceFromCentreOfMassToBaseOfModel();
    }
    return 0.0f;
}

void CClientPed::SetAlpha(unsigned char ucAlpha)
{
    m_ucAlpha = ucAlpha;
    UpdateAlphaAndVisibility();
}

void CClientPed::UpdateAlphaAndVisibility()
{
    if (!m_pPlayerPed)
        return;

    unsigned char ucEffectiveAlpha = m_ucAlpha;
    if (m_ucInterior != g_pGame->GetWorld()->GetCurrentArea())
        ucEffectiveAlpha = 0;

    if (RpClump* pClump = m_pPlayerPed->GetRpClump())
        g_pGame->GetVisibilityPlugins()->SetClumpAlpha(pClump, ucEffectiveAlpha);

    // GTA gates both blob and dynamic ped-shadow creation on bIsVisible, while
    // MTA alpha normally affects only the clump. Keep the logical visibility
    // cached separately so restoring a non-zero alpha restores the ped too.
    const bool bNativeVisible = m_bVisible && ucEffectiveAlpha != 0;
    if (!bNativeVisible)
        m_pPlayerPed->ReleaseRealTimeShadow();
    m_pPlayerPed->SetVisible(bNativeVisible);
}

void CClientPed::Respawn(CVector* pvecPosition, bool bRestoreState, bool bCameraCut)
{
    // We must not call CPed::Respawn for remote players
    if (m_bIsLocalPlayer)
    {
        SetNextAnimationNormal();
        SetFrozenWaitingForGroundToLoad(true);
        if (m_pPlayerPed)
        {
            // Detach us
            CClientEntity* pAttachedTo = GetAttachedTo();
            if (pAttachedTo && pAttachedTo->IsEntityAttached(this))
                InternalAttachTo(NULL);

            // Detach our attached entities
            for (uint i = 0; i < m_AttachedEntities.size(); i++)
            {
                CClientEntity* pEntity = m_AttachedEntities[i];
                pEntity->InternalAttachTo(NULL);
            }
            CVector vecPosition;
            if (!pvecPosition)
            {
                GetPosition(vecPosition);
                pvecPosition = &vecPosition;
            }

            // Jax: save some info incase we want to restore the old state
            CVector vecMoveSpeed;
            GetMoveSpeed(vecMoveSpeed);
            float         fHealth = GetHealth();
            float         fArmor = GetArmor();
            eWeaponSlot   weaponSlot = GetCurrentWeaponSlot();
            float         fCurrentRotation = GetCurrentRotation();
            float         fTargetRotation = m_pPlayerPed->GetTargetRotation();
            unsigned char ucInterior = GetInterior();
            unsigned char ucCameraInterior = static_cast<unsigned char>(g_pGame->GetWorld()->GetCurrentArea());
            bool          bOldNightVision = g_pMultiplayer->IsNightVisionEnabled();
            bool          bOldThermalVision = g_pMultiplayer->IsThermalVisionEnabled();

            // Don't allow any camera movement if we're in fixed mode
            if (m_pManager->GetCamera()->IsInFixedMode())
                bCameraCut = false;

            m_pPlayerPed->Respawn(pvecPosition, bCameraCut);
            SetPosition(*pvecPosition);

            m_pPlayerPed->SetLanding(false);

            // Set it to 0 (Fix #501)
            SetCurrentWeaponSlot(eWeaponSlot::WEAPONSLOT_TYPE_UNARMED);

            if (bRestoreState)
            {
                // Jax: restore all the things we saved
                SetHealth(fHealth);
                SetArmor(fArmor);
                SetCurrentWeaponSlot(weaponSlot);
                SetCurrentRotation(fCurrentRotation);
                m_pPlayerPed->SetTargetRotation(fTargetRotation);
                SetMoveSpeed(vecMoveSpeed);
                SetHasJetPack(m_bHasJetPack);
                SetInterior(ucInterior);
            }
            // Restore the camera's interior whether we're restoring player states or not
            g_pGame->GetWorld()->SetCurrentArea(ucCameraInterior);

            // Reset goggle effect
            g_pMultiplayer->SetNightVisionEnabled(bOldNightVision, false);
            g_pMultiplayer->SetThermalVisionEnabled(bOldThermalVision, false);

            // Reattach us
            if (pAttachedTo && pAttachedTo->IsEntityAttached(this))
                InternalAttachTo(pAttachedTo);

            // Reattach our attached entities
            for (uint i = 0; i < m_AttachedEntities.size(); i++)
            {
                CClientEntity* pEntity = m_AttachedEntities[i];
                pEntity->InternalAttachTo(this);
            }
        }
    }
}

void CClientPed::Say(const ePedSpeechContext& speechId, float probability)
{
    if (!m_pPlayerPed)
        return;

    m_pPlayerPed->Say(speechId, probability);
}

const char* CClientPed::GetBodyPartName(unsigned char ucID)
{
    if (ucID <= 10)
    {
        return BodyPartNames[ucID].szName;
    }

    return "Unknown";
}

void CClientPed::GetTargetPosition(CVector& vecPosition)
{
    vecPosition = m_interp.pos.vecTarget;
    if (m_interp.pTargetOriginSource)
    {
        CVector vecTemp;
        m_interp.pTargetOriginSource->GetPosition(vecTemp);
        vecPosition += vecTemp;
    }
}

void CClientPed::SetTargetPosition(const CVector& vecPosition, unsigned long ulDelay, CClientEntity* pTargetOriginSource)
{
    UpdateTargetPosition();

    // Get the origin of the position if we are in contact with anything
    CVector vecOrigin;
    if (pTargetOriginSource)
        pTargetOriginSource->GetPosition(vecOrigin);

    if (m_remoteStreamInFenceActive)
        m_remoteStreamInFenceMatrix.vPos = vecPosition + vecOrigin;

    UpdateUnderFloorFix(vecPosition, vecOrigin);

    // Update the references to the contact entity
    if (pTargetOriginSource != m_interp.pTargetOriginSource)
    {
        if (m_interp.pTargetOriginSource)
            m_interp.pTargetOriginSource->RemoveOriginSourceUser(this);
        if (pTargetOriginSource)
            pTargetOriginSource->AddOriginSourceUser(this);
        m_interp.pTargetOriginSource = pTargetOriginSource;
    }

    if (m_pPlayerPed)
    {
        // The ped is streamed in
        CVector vecCurrentPosition;
        GetPosition(vecCurrentPosition);
        vecCurrentPosition -= vecOrigin;

        m_interp.pos.vecTarget = vecPosition;
        m_interp.vecOriginSourceLastPosition = vecOrigin;
        m_interp.bHadOriginSource = false;

        // Calculate the relative error
        m_interp.pos.vecError = vecPosition - vecCurrentPosition;

        // Get the interpolation interval
        unsigned long ulTime = CClientTime::GetTime();
        m_interp.pos.ulStartTime = ulTime;
        m_interp.pos.ulFinishTime = ulTime + ulDelay;

        // Initialize the interpolation
        m_interp.pos.fLastAlpha = 0.0f;
    }
    else
    {
        // Set the position straight
        SetPosition(vecPosition + vecOrigin);
    }
}

void CClientPed::UpdateRemoteAuthoritativeTransform(const CVector* pPosition, const float* pRotation, const CVector* pMoveSpeed)
{
    // A non-syncer still runs a local GTA CPlayerPed instance. Keep network
    // truth separate from that instance: unloaded collision can make the
    // observer fall even though the owner remains stationary, and the owner
    // may then have no spatial delta to resend for an arbitrarily long time.
    if (GetType() != CCLIENTPED || m_bIsLocalPlayer || m_bIsSyncing)
        return;

    if (pPosition && std::isfinite(pPosition->fX) && std::isfinite(pPosition->fY) && std::isfinite(pPosition->fZ))
    {
        m_remoteAuthoritativeTransform.position = *pPosition;
        m_remoteAuthoritativeTransform.positionValid = true;
        m_remoteAuthoritativeTransform.restoreAllowed = true;
        if (m_remoteStreamInFenceActive)
            m_remoteStreamInFenceMatrix.vPos = *pPosition;
    }
    if (pRotation && std::isfinite(*pRotation))
    {
        m_remoteAuthoritativeTransform.rotation = *pRotation;
        m_remoteAuthoritativeTransform.rotationValid = true;
        if (m_remoteStreamInFenceActive)
        {
            m_remoteStreamInFenceCurrentRotation = *pRotation;
            m_remoteStreamInFenceTargetRotation = *pRotation;
            CVector fenceRotation = m_remoteStreamInFenceMatrix.GetRotation();
            fenceRotation.fZ = *pRotation;
            m_remoteStreamInFenceMatrix.SetRotation(fenceRotation);
        }
    }
    if (pMoveSpeed && std::isfinite(pMoveSpeed->fX) && std::isfinite(pMoveSpeed->fY) && std::isfinite(pMoveSpeed->fZ))
    {
        m_remoteAuthoritativeTransform.moveSpeed = *pMoveSpeed;
        m_remoteAuthoritativeTransform.moveSpeedValid = true;
        if (m_remoteStreamInFenceActive && m_pPlayerPed)
            m_pPlayerPed->SetMoveSpeed(*pMoveSpeed);
    }
}

void CClientPed::InvalidateRemoteAuthoritativeTransformRestore()
{
    m_remoteAuthoritativeTransform.restoreAllowed = false;
    ClearRemoteStreamInTransformFence("authoritative_transform_suspended");
}

void CClientPed::RemoveTargetPosition()
{
    m_interp.pos.ulFinishTime = 0;
    if (m_interp.pTargetOriginSource)
    {
        m_interp.pTargetOriginSource->RemoveOriginSourceUser(this);
        m_interp.pTargetOriginSource = NULL;
    }
}

void CClientPed::UpdateTargetPosition()
{
    if (HasTargetPosition())
    {
        unsigned long ulCurrentTime = CClientTime::GetTime();

        // Get the origin position if there is any contact
        CVector vecOrigin;
        if (m_interp.pTargetOriginSource)
            m_interp.pTargetOriginSource->GetPosition(vecOrigin);
        else if (m_interp.bHadOriginSource)
            vecOrigin = m_interp.vecOriginSourceLastPosition;

        // Grab our currrent position
        CVector vecCurrentPosition;
        GetPosition(vecCurrentPosition);
        vecCurrentPosition -= vecOrigin;

        // Get the factor of time spent from the interpolation start
        // to the current time.
        float fAlpha = SharedUtil::Unlerp(m_interp.pos.ulStartTime, ulCurrentTime, m_interp.pos.ulFinishTime);

        // Don't let it overcompensate the error
        fAlpha = SharedUtil::Clamp(0.0f, fAlpha, 1.0f);

        // Get the current error portion to compensate
        float fCurrentAlpha = fAlpha - m_interp.pos.fLastAlpha;
        m_interp.pos.fLastAlpha = fAlpha;

        // Apply the error compensation
        CVector vecCompensation = SharedUtil::Lerp(CVector(), fCurrentAlpha, m_interp.pos.vecError);

        // If we finished compensating the error, finish it for the next pulse
        if (fAlpha == 1.0f)
        {
            m_interp.pos.ulFinishTime = 0;
        }

        CVector vecNewPosition = vecCurrentPosition + vecCompensation;

        // Check if the distance to interpolate is too far.
        CVector vecVelocity;
        GetMoveSpeed(vecVelocity);
        float fThreshold =
            (PED_INTERPOLATION_WARP_THRESHOLD + PED_INTERPOLATION_WARP_THRESHOLD_FOR_SPEED * vecVelocity.Length()) * g_pGame->GetGameSpeed() * TICK_RATE / 100;

        // There is a reason to have this condition this way: To prevent NaNs generating new NaNs after interpolating (Comparing with NaNs always results to
        // false).
        if (!((vecCurrentPosition - m_interp.pos.vecTarget).Length() <= fThreshold))
        {
            // Abort all interpolation
            m_interp.pos.ulFinishTime = 0;
            vecNewPosition = m_interp.pos.vecTarget;
        }

        SetPosition(vecNewPosition + vecOrigin, false);
    }
}

// Peds under floor fix hack
void CClientPed::UpdateUnderFloorFix(const CVector& vecTargetPosition, const CVector& vecOrigin)
{
    // Calc remote movement
    CVector vecRemoteMovement = vecTargetPosition - m_vecPrevTargetPosition;
    m_vecPrevTargetPosition = vecTargetPosition;

    // Calc local error
    CVector vecLocalPosition;
    GetPosition(vecLocalPosition);
    vecLocalPosition -= vecOrigin;
    CVector vecLocalError = vecTargetPosition - vecLocalPosition;

    // Small remote movement + local position error = force a warp
    bool bForceLocalZ = false;
    bool bForceLocalXY = false;
    if (abs(vecRemoteMovement.fZ) < 0.01f)
    {
        float fLocalErrorZ = abs(vecLocalError.fZ);
        if (fLocalErrorZ > 0.1f && fLocalErrorZ < 10.f)
        {
            bForceLocalZ = true;
        }
    }

    if (abs(vecRemoteMovement.fX) < 0.01f)
    {
        float fLocalErrorX = abs(vecLocalError.fX);
        if (fLocalErrorX > 0.1f && fLocalErrorX < 10.f)
        {
            bForceLocalXY = true;
        }
    }

    if (abs(vecRemoteMovement.fY) < 0.01f)
    {
        float fLocalErrorY = abs(vecLocalError.fY);
        if (fLocalErrorY > 0.1f && fLocalErrorY < 10.f)
        {
            bForceLocalXY = true;
        }
    }

    // Only force position if needed for at least two consecutive calls
    if (!bForceLocalZ && !bForceLocalXY)
        m_uiForceLocalCounter = 0;
    else if (m_uiForceLocalCounter++ > 1)
    {
        if (bForceLocalZ)
        {
            vecLocalPosition.fZ = vecTargetPosition.fZ;
            CVector vecMoveSpeed;
            GetMoveSpeed(vecMoveSpeed);
            vecMoveSpeed.fZ = 0;
            SetMoveSpeed(vecMoveSpeed);
        }
        if (bForceLocalXY)
        {
            vecLocalPosition.fX = vecTargetPosition.fX;
            vecLocalPosition.fY = vecTargetPosition.fY;
        }
        SetPosition(vecLocalPosition + vecOrigin);
    }
}

CClientEntity* CClientPed::GetTargetedEntity()
{
    CClientEntity* pReturn = NULL;
    if (m_pPlayerPed)
    {
        CEntity* pEntity = m_pPlayerPed->GetTargetedEntity();
        if (pEntity)
        {
            CPools* pPools = g_pGame->GetPools();
            pReturn = pPools->GetClientEntity((DWORD*)pEntity->GetInterface());
        }
    }
    return pReturn;
}

CClientPed* CClientPed::GetTargetedPed()
{
    CClientEntity* pTargetEntity = GetTargetedEntity();
    if (pTargetEntity && IS_PED(pTargetEntity))
    {
        return static_cast<CClientPed*>(pTargetEntity);
    }
    return NULL;
}

void CClientPed::NotifyCreate()
{
    m_pManager->GetPedManager()->OnCreation(this);
    CClientStreamElement::NotifyCreate();
}

void CClientPed::NotifyDestroy()
{
    m_pManager->GetPedManager()->OnDestruction(this);
    UpdateKeysync(true);
}

bool CClientPed::IsSunbathing()
{
    if (m_pPlayerPed)
    {
        CTask* pTask = m_pTaskManager->GetTask(TASK_PRIORITY_PRIMARY);
        if (pTask && pTask->GetTaskType() == TASK_COMPLEX_SUNBATHE)
        {
            return true;
        }
    }
    return m_bSunbathing;
}

void CClientPed::SetSunbathing(bool bSunbathing, bool bStartStanding)
{
    if (m_pPlayerPed)
    {
        CTask* pTask = m_pTaskManager->GetTask(TASK_PRIORITY_PRIMARY);
        if (pTask && pTask->GetTaskType() == TASK_COMPLEX_SUNBATHE)
        {
            if (!bSunbathing)
            {
                CTaskComplexSunbathe* pSunbatheTask = dynamic_cast<CTaskComplexSunbathe*>(pTask);
                CTask*                pNewTask = pSunbatheTask->CreateNextSubTask(m_pPlayerPed);
                if (pNewTask)
                {
                    pSunbatheTask->SetSubTask(pNewTask);
                }
            }
        }
        else
        {
            if (bSunbathing)
            {
                CTaskComplexSunbathe* pTask = g_pGame->GetTasks()->CreateTaskComplexSunbathe(NULL, bStartStanding);
                if (pTask)
                {
                    pTask->SetAsPedTask(m_pPlayerPed, TASK_PRIORITY_PRIMARY);
                }
            }
        }
    }
    m_bSunbathing = bSunbathing;
}

bool CClientPed::LookAt(CVector vecOffset, int iTime, int iBlend, CClientEntity* pEntity)
{
    if (m_pPlayerPed)
    {
        CEntity* pGameEntity = NULL;
        if (pEntity)
            pGameEntity = pEntity->GetGameEntity();
        CTaskSimpleTriggerLookAt* pTask = g_pGame->GetTasks()->CreateTaskSimpleTriggerLookAt(pGameEntity, iTime, 0, vecOffset, false, 0.250000, iBlend);
        if (pTask)
        {
            pTask->SetAsSecondaryPedTask(m_pPlayerPed, TASK_SECONDARY_PARTIAL_ANIM);

            return true;
        }
    }
    return false;
}

bool CClientPed::UseGun(CVector vecTarget, CClientEntity* pEntity)
{
    if (m_pPlayerPed)
    {
        CEntity* pGameEntity = NULL;
        if (pEntity)
            pGameEntity = pEntity->GetGameEntity();
        CTaskSimpleUseGun* pTask = g_pGame->GetTasks()->CreateTaskSimpleUseGun(pGameEntity, vecTarget, 0, 1, false);
        if (pTask)
        {
            pTask->SetAsSecondaryPedTask(m_pPlayerPed, TASK_SECONDARY_PARTIAL_ANIM);

            return true;
        }
    }
    return false;
}

bool CClientPed::IsAttachToable()
{
    // We're not attachable if we're inside a vehicle (that would get messy)
    if (!GetOccupiedVehicle())
    {
        return true;
    }
    return false;
}

bool CClientPed::IsDoingGangDriveby()
{
    if (m_pPlayerPed)
    {
        CTask* pTask = m_pTaskManager->GetTask(TASK_PRIORITY_PRIMARY);
        if (pTask && pTask->GetTaskType() == TASK_SIMPLE_GANG_DRIVEBY)
        {
            return true;
        }
    }
    return m_bDoingGangDriveby;
}

void CClientPed::SetDoingGangDriveby(bool bDriveby)
{
    m_bDoingGangDriveby = bDriveby;

    if (!m_pPlayerPed)
        return;

    CTask* primaryTask = m_pTaskManager->GetTask(TASK_PRIORITY_PRIMARY);

    if (primaryTask && primaryTask->GetTaskType() == TASK_SIMPLE_GANG_DRIVEBY)
    {
        if (!bDriveby)
        {
            if (m_bProcessingWeaponFireEvent)
            {
                // Aborting now would re-enter the task's own native ProcessPed() and crash.
                m_bDeferredGangDrivebyAbort = true;
            }
            else
            {
                primaryTask->MakeAbortable(m_pPlayerPed, ABORT_PRIORITY_URGENT, NULL);
            }
        }
    }
    else if (bDriveby)
    {
        unsigned int seat = GetOccupiedVehicleSeat();
        bool         bRight = (seat % 2 != 0);

        if (CTask* task = g_pGame->GetTasks()->CreateTaskSimpleGangDriveBy(NULL, NULL, 0.0f, 0, 0, bRight); task != nullptr)
        {
            task->SetAsPedTask(m_pPlayerPed, TASK_PRIORITY_PRIMARY);
        }

        uchar ucWindow = -1;

        switch (seat)
        {
            case 0:
                ucWindow = WINDOW_LEFT_FRONT;
                break;
            case 1:
                ucWindow = WINDOW_RIGHT_FRONT;
                break;
            case 2:
                ucWindow = WINDOW_LEFT_BACK;
                break;
            case 3:
                ucWindow = WINDOW_RIGHT_BACK;
                break;
        }

        if (ucWindow != -1)
        {
            if (CClientVehicle* vehicle = GetOccupiedVehicle(); vehicle != nullptr)
                vehicle->SetWindowOpen(ucWindow, true);
        }
    }
}

bool CClientPed::GetRunningAnimationName(SString& strBlockName, SString& strAnimName)
{
    if (IsRunningAnimation())
    {
        if (IsCustomAnimationPlaying())
        {
            strBlockName = GetNextAnimationCustomBlockName();
            strAnimName = GetNextAnimationCustomName();
        }
        else
        {
            strBlockName = GetAnimationBlock()->GetName();
            strAnimName = m_AnimationCache.strName;
        }
        return true;
    }
    return false;
}

bool CClientPed::IsRunningAnimation()
{
    if (m_pPlayerPed)
    {
        CTask* pTask = m_pTaskManager->GetTask(TASK_PRIORITY_PRIMARY);
        if (pTask && pTask->GetTaskType() == TASK_SIMPLE_NAMED_ANIM)
        {
            return true;
        }
        return false;
    }
    return (m_AnimationCache.bLoop || m_AnimationCache.bFreezeLastFrame) && m_pAnimationBlock;
}

bool CClientPed::IsAnimationInProgress()
{
    bool constAnim = m_AnimationCache.bLoop || m_AnimationCache.bFreezeLastFrame;

    if (!m_pAnimationBlock)
        return constAnim;

    float elapsedTime = static_cast<float>(GetTimestamp() - m_AnimationCache.startTime) / 1000.0f;

    auto animBlendHierarchy = g_pGame->GetAnimManager()->GetAnimation(m_AnimationCache.strName.c_str(), m_pAnimationBlock);
    if (!animBlendHierarchy)
        return constAnim;

    return constAnim || elapsedTime < animBlendHierarchy->GetTotalTime();
}

void CClientPed::RunNamedAnimation(std::unique_ptr<CAnimBlock>& pBlock, const char* szAnimName, int iTime, int iBlend, bool bLoop, bool bUpdatePosition,
                                   bool bInterruptible, bool bFreezeLastFrame, bool bRunInSequence, bool bOffsetPed, bool bHoldLastFrame)
{
    /* lil_Toady: this seems to break things
    // Kill any current animation that might be running
    KillAnimation ();
    */

    // Are we streamed in?
    if (m_pPlayerPed)
    {
        if (!pBlock->IsLoaded())
        {
            pBlock->Request(BLOCKING, true);
        }

        if (pBlock->IsLoaded())
        {
            // Fix #366: Can only run forward bug
            m_pPlayerPed->SetLanding(false);

            // Remove jetpack now so it doesn't stay on (#9522#c25612)
            if (HasJetPack())
                SetHasJetPack(false);

            // Let's not choke them any longer
            if (IsChoking())
                SetChoking(false);

            /*
             Saml1er: Setting flags to 0x10 will tell GTA:SA that animation needs to be decompressed.
                      If not, animation will either crash or do some weird things.
            */
            int flags = 0x10;  // Stops jaw fucking up, some speaking flag maybe
            if (bLoop)
                flags |= 0x2;  // flag that triggers the loop (Maccer)
            if (bUpdatePosition)
            {
                // 0x40 enables position updating on Y-coord, 0x80 on X. (Maccer)
                flags |= 0x40;
                flags |= 0x80;
            }

            // Kill any higher priority tasks if we dont want this anim interruptible
            if (!bInterruptible)
            {
                KillTask(TASK_PRIORITY_PHYSICAL_RESPONSE);
                KillTask(TASK_PRIORITY_EVENT_RESPONSE_TEMP);
                KillTask(TASK_PRIORITY_EVENT_RESPONSE_NONTEMP);
            }

            if (!bFreezeLastFrame)
                flags |= 0x08;  // flag determines whether to freeze player when anim ends. Really annoying (Maccer)
            float  fBlendDelta = 1 / std::max((float)iBlend, 1.0f) * 1000;
            CTask* pTask = g_pGame->GetTasks()->CreateTaskSimpleRunNamedAnim(szAnimName, pBlock->GetName(), flags, fBlendDelta, iTime, !bInterruptible,
                                                                             bRunInSequence, bOffsetPed, bHoldLastFrame);
            if (pTask)
            {
                pTask->SetAsPedTask(m_pPlayerPed, TASK_PRIORITY_PRIMARY);
                g_pClientGame->InsertRunNamedAnimTaskToMap(reinterpret_cast<CTaskSimpleRunNamedAnimSAInterface*>(pTask->GetInterface()), this);
            }
        }
        else
        {
            SString strMessage("%s %d (%s)", pBlock->GetName(), pBlock->GetIndex(), szAnimName);
            g_pCore->LogEvent(543, "Blocking anim load fail", "", strMessage);
            AddReportLog(5431, SString("Failed to load animation %s", *strMessage));
            /*
                        // TODO: unload unreferenced blocks later on
                        g_pGame->GetStreaming ()->RequestAnimations ( pBlock->GetIndex (), 8 );
                        m_bRequestedAnimation = true;
            */
        }
    }
    if (pBlock)
    {
        m_pAnimationBlock = g_pGame->GetAnimManager()->GetAnimBlock(pBlock->GetInterface());
    }
    m_AnimationCache.strName = szAnimName;
    m_AnimationCache.iTime = iTime;
    m_AnimationCache.iBlend = iBlend;
    m_AnimationCache.bLoop = bLoop;
    m_AnimationCache.bUpdatePosition = bUpdatePosition;
    m_AnimationCache.bInterruptible = bInterruptible;
    m_AnimationCache.bFreezeLastFrame = bFreezeLastFrame;
}

void CClientPed::KillAnimation()
{
    if (m_pPlayerPed)
    {
        CTask* pTask = m_pTaskManager->GetTask(TASK_PRIORITY_PRIMARY);
        if (pTask)
        {
            int iTaskType = pTask->GetTaskType();
            if (iTaskType == TASK_SIMPLE_NAMED_ANIM || iTaskType == TASK_SIMPLE_ANIM)
            {
                pTask->MakeAbortable(m_pPlayerPed, ABORT_PRIORITY_IMMEDIATE, NULL);
                pTask->Destroy();
                m_pTaskManager->RemoveTask(TASK_PRIORITY_PRIMARY);
            }
        }
    }
    m_pAnimationBlock = NULL;
    m_AnimationCache.strName = "";
    m_bRequestedAnimation = false;
    SetNextAnimationNormal();
}

std::unique_ptr<CAnimBlock> CClientPed::GetAnimationBlock()
{
    if (m_pAnimationBlock)
    {
        return g_pGame->GetAnimManager()->GetAnimBlock(m_pAnimationBlock->GetInterface());
    }
    return nullptr;
}

void CClientPed::RunAnimationFromCache()
{
    if (!m_pAnimationBlock)
        return;

    // Copy our name incase it gets deleted
    std::string animName = m_AnimationCache.strName;

    // Run our animation
    RunNamedAnimation(m_pAnimationBlock, animName.c_str(), m_AnimationCache.iTime, m_AnimationCache.iBlend, m_AnimationCache.bLoop,
                      m_AnimationCache.bUpdatePosition, m_AnimationCache.bInterruptible, m_AnimationCache.bFreezeLastFrame);

    // Set anim progress & speed
    m_AnimationCache.progressWaitForStreamIn = true;
}

void CClientPed::UpdateAnimationProgressAndSpeed()
{
    if (!m_AnimationCache.progressWaitForStreamIn)
        return;

    // Get current anim
    auto animAssoc = g_pGame->GetAnimManager()->RpAnimBlendClumpGetAssociation(GetClump(), m_AnimationCache.strName.c_str());
    if (!animAssoc)
        return;

    float animLength = animAssoc->GetLength();
    float progress = 0.0f;
    float elapsedTime = static_cast<float>(GetTimestamp() - m_AnimationCache.startTime) / 1000.0f;

    if (m_AnimationCache.bFreezeLastFrame)  // time and loop is ignored if freezeLastFrame is true
        progress = (elapsedTime / animLength) * m_AnimationCache.speed;
    else
    {
        if (m_AnimationCache.bLoop)
            progress = std::fmod(elapsedTime * m_AnimationCache.speed, animLength) / animLength;
        else
            // For non-looped animations, limit duration to animLength if time exceeds it
            progress = (elapsedTime / (m_AnimationCache.iTime <= animLength ? m_AnimationCache.iTime : animLength)) * m_AnimationCache.speed;
    }

    animAssoc->SetCurrentProgress(std::clamp(progress, 0.0f, 1.0f));
    animAssoc->SetCurrentSpeed(m_AnimationCache.speed);

    m_AnimationCache.progressWaitForStreamIn = false;
}

void CClientPed::PostWeaponFire()
{
    m_ulLastTimeFired = CClientTime::GetTime();
}

void CClientPed::SetBulletImpactData(CClientEntity* pEntity, const CVector& vecHitPosition)
{
    // Clear old entity if new impact info
    if (!m_bBulletImpactData)
        m_pBulletImpactEntity = NULL;

    m_bBulletImpactData = true;

    // Only update entity if not NULL to prevent losing previous value. (Shotguns cause multiple calls per shot)
    if (pEntity)
        m_pBulletImpactEntity = pEntity;
    m_vecBulletImpactHit = vecHitPosition;
}

bool CClientPed::GetBulletImpactData(CClientEntity** ppEntity, CVector* pvecHitPosition)
{
    if (m_bBulletImpactData)
    {
        if (ppEntity)
            *ppEntity = m_pBulletImpactEntity;
        if (pvecHitPosition)
            *pvecHitPosition = m_vecBulletImpactHit;
        return true;
    }
    else
        return false;
}

bool CClientPed::IsUsingGun()
{
    if (m_pPlayerPed)
    {
        CTask* pTask = m_pTaskManager->GetTaskSecondary(TASK_SECONDARY_ATTACK);
        if (pTask && pTask->GetTaskType() == TASK_SIMPLE_USE_GUN)
        {
            return true;
        }
    }
    return false;
}

void CClientPed::SetHeadless(bool bHeadless)
{
    if (m_pPlayerPed)
    {
        m_pPlayerPed->RemoveBodyPart((bHeadless) ? 2 : 1, 0);
    }
    m_bHeadless = bHeadless;
}

void CClientPed::SetFootBloodEnabled(bool bHasFootBlood)
{
    if (m_pPlayerPed)
    {
        if (bHasFootBlood)
        {
            m_pPlayerPed->SetFootBlood(-1);
        }
        else
        {
            m_pPlayerPed->SetFootBlood(0);
        }
    }
}

bool CClientPed::IsFootBloodEnabled()
{
    if (m_pPlayerPed)
    {
        return (m_pPlayerPed->GetFootBlood() > 0);
    }
    return false;
}

void CClientPed::SetBleeding(bool bBleeding)
{
    if (m_pPlayerPed)
    {
        m_pPlayerPed->SetBleeding(bBleeding);
    }
    m_bBleeding = bBleeding;
}

bool CClientPed::SetOnFire(bool bIsOnFire)
{
    if (m_pPlayerPed)
        return m_pPlayerPed->SetOnFire(bIsOnFire);

    m_bIsOnFire = bIsOnFire;
    return true;
}

void CClientPed::GetVoice(short* psVoiceType, short* psVoiceID)
{
    if (m_pPlayerPed)
        m_pPlayerPed->GetVoice(psVoiceType, psVoiceID);
}

void CClientPed::GetVoice(const char** pszVoiceType, const char** pszVoice)
{
    if (m_pPlayerPed)
        m_pPlayerPed->GetVoice(pszVoiceType, pszVoice);
}

void CClientPed::SetVoice(short sVoiceType, short sVoiceID)
{
    if (m_pPlayerPed)
        m_pPlayerPed->SetVoice(sVoiceType, sVoiceID);
}

void CClientPed::SetVoice(const char* szVoiceType, const char* szVoice)
{
    if (m_pPlayerPed)
        m_pPlayerPed->SetVoice(szVoiceType, szVoice);
}

void CClientPed::ResetVoice()
{
    if (m_pPlayerPed)
        m_pPlayerPed->ResetVoice();
}

bool CClientPed::IsSpeechEnabled()
{
    if (m_pPlayerPed)
    {
        return !m_pPlayerPed->GetPedSound()->IsSpeechDisabled();
    }
    return m_bSpeechEnabled;
}

void CClientPed::SetSpeechEnabled(bool bEnabled)
{
    if (m_pPlayerPed)
    {
        if (bEnabled)
            m_pPlayerPed->GetPedSound()->EnablePedSpeech();
        else
            m_pPlayerPed->GetPedSound()->DisablePedSpeech(true);
    }
    m_bSpeechEnabled = bEnabled;
}

bool CClientPed::CanReloadWeapon() noexcept
{
    const auto       time = CClientTime::GetTime();
    CControllerState state;
    GetControllerState(state);

    const auto weapon = GetWeapon()->GetType();

    if (state.RightShoulder1 || (IsDucked() && (state.LeftStickX != 0 || state.LeftStickY != 0)) || time - m_ulLastTimeMovedWhileCrouched <= 300)
        return false;

    if (weapon < WEAPONTYPE_PISTOL || weapon > WEAPONTYPE_TEC9 || weapon == WEAPONTYPE_SHOTGUN)
        return false;

    return true;
}

bool CClientPed::ReloadWeapon() noexcept
{
    if (!m_pTaskManager)
        return false;

    auto* weapon = GetWeapon();
    auto* task = m_pTaskManager->GetTaskSecondary(TASK_SECONDARY_ATTACK);

    if (!CanReloadWeapon() || (task && task->GetTaskType() == TASK_SIMPLE_USE_GUN))
        return false;

    CLuaArguments args;
    args.PushNumber(weapon->GetType());
    args.PushNumber(weapon->GetAmmoInClip());
    args.PushNumber(weapon->GetAmmoTotal());

    bool result = false;

    if (IS_PLAYER(this))
        result = CallEvent("onClientPlayerWeaponReload", args, true);
    else
        result = CallEvent("onClientPedWeaponReload", args, true);

    if (!result)
        return false;

    weapon->SetState(WEAPONSTATE_RELOADING);
    return true;
}

bool CClientPed::IsReloadingWeapon() noexcept
{
    auto* weapon = GetWeapon();
    return weapon && weapon->GetState() == WEAPONSTATE_RELOADING;
}

bool CClientPed::ShouldBeStealthAiming()
{
    if (m_pPlayerPed)
    {
        // Do we have a knife?
        if (GetCurrentWeaponType() == WEAPONTYPE_KNIFE)
        {
            // Do we have the aim key pressed?
            CKeyBindsInterface* pKeyBinds = g_pCore->GetKeyBinds();
            if (pKeyBinds)
            {
                SBindableGTAControl* pAimControl = pKeyBinds->GetBindableFromControl("aim_weapon");
                if (pAimControl && pAimControl->bState)
                {
                    // We need to be either crouched, walking or standing
                    SBindableGTAControl* pWalkControl = pKeyBinds->GetBindableFromControl("walk");
                    if (m_pPlayerPed->GetMoveState() == PedMoveState::PEDMOVE_STILL || m_pPlayerPed->GetMoveState() == PedMoveState::PEDMOVE_WALK ||
                        pWalkControl && pWalkControl->bState)
                    {
                        // Do we have a target ped?
                        CClientPed* pTargetPed = GetTargetedPed();
                        if (pTargetPed && pTargetPed->GetGamePlayer())
                        {
                            // Are we close enough to the target?
                            CVector vecPos, vecPos_2;
                            GetPosition(vecPos);
                            pTargetPed->GetPosition(vecPos_2);
                            if (DistanceBetweenPoints3D(vecPos, vecPos_2) <= STEALTH_KILL_RANGE)
                            {
                                // Grab our current anim
                                std::unique_ptr<CAnimBlendAssociation> pAssoc = GetFirstAnimation();
                                if (pAssoc)
                                {
                                    // Our game checks for stealth killing
                                    if (m_pPlayerPed->GetPedIntelligence()->TestForStealthKill(pTargetPed->GetGamePlayer(), false))
                                    {
                                        return true;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return false;
}

void CClientPed::SetStealthAiming(bool bAiming)
{
    if (bAiming != m_bStealthAiming)
    {
        // Stop aiming?
        if (!bAiming)
        {
            // Do we have the aiming animation?
            std::unique_ptr<CAnimBlendAssociation> pAssoc = GetAnimation(ANIM_ID_STEALTH_AIM);
            if (pAssoc)
            {
                // Stop our animation
                pAssoc->SetBlendAmount(-2.0f);
            }
        }
        m_bStealthAiming = bAiming;
    }
}

std::unique_ptr<CAnimBlendAssociation> CClientPed::AddAnimation(AssocGroupId group, AnimationId id)
{
    if (m_pPlayerPed)
    {
        return g_pGame->GetAnimManager()->AddAnimation(m_pPlayerPed->GetRpClump(), group, id);
    }
    return nullptr;
}

std::unique_ptr<CAnimBlendAssociation> CClientPed::BlendAnimation(AssocGroupId group, AnimationId id, float fBlendDelta)
{
    if (m_pPlayerPed)
    {
        return g_pGame->GetAnimManager()->BlendAnimation(m_pPlayerPed->GetRpClump(), group, id, fBlendDelta);
    }
    return nullptr;
}

std::unique_ptr<CAnimBlendAssociation> CClientPed::GetAnimation(AnimationId id)
{
    if (m_pPlayerPed)
    {
        return g_pGame->GetAnimManager()->RpAnimBlendClumpGetAssociation(m_pPlayerPed->GetRpClump(), id);
    }
    return nullptr;
}

std::unique_ptr<CAnimBlendAssociation> CClientPed::GetFirstAnimation()
{
    if (m_pPlayerPed)
    {
        return g_pGame->GetAnimManager()->RpAnimBlendClumpGetFirstAssociation(m_pPlayerPed->GetRpClump());
    }
    return nullptr;
}

void CClientPed::SetNextAnimationCustom(const std::shared_ptr<CClientIFP>& pIFP, const SString& strAnimationName)
{
    m_bisNextAnimationCustom = true;
    m_pCustomAnimationIFP = pIFP;
    m_strCustomIFPBlockName = pIFP->GetBlockName();
    m_strCustomIFPAnimationName = strAnimationName;
    m_u32CustomBlockNameHash = pIFP->GetBlockNameHash();
    m_u32CustomAnimationNameHash = HashString(strAnimationName.ToLower());
}

void CClientPed::ReplaceAnimation(std::unique_ptr<CAnimBlendHierarchy>& pInternalAnimHierarchy, const std::shared_ptr<CClientIFP>& pIFP,
                                  CAnimBlendHierarchySAInterface* pCustomAnimHierarchy)
{
    SReplacedAnimation replacedAnimation;
    replacedAnimation.pIFP = pIFP;
    replacedAnimation.pAnimationHierarchy = pCustomAnimHierarchy;
    m_mapOfReplacedAnimations[pInternalAnimHierarchy->GetInterface()] = replacedAnimation;
}

void CClientPed::RestoreAnimation(std::unique_ptr<CAnimBlendHierarchy>& pInternalAnimHierarchy)
{
    CAnimBlendHierarchySAInterface* pInterface = pInternalAnimHierarchy->GetInterface();
    CIFPEngine::EngineApplyAnimation(*this, pInterface, pInterface);
    m_mapOfReplacedAnimations.erase(pInterface);
}

void CClientPed::RestoreAnimations(const std::shared_ptr<CClientIFP>& IFP)
{
    for (auto iter = m_mapOfReplacedAnimations.cbegin(); iter != m_mapOfReplacedAnimations.cend(); /* manual increment */)
    {
        if (std::addressof(*IFP.get()) == std::addressof(*iter->second.pIFP.get()))
        {
            auto pAnimHierarchy = g_pGame->GetAnimManager()->GetAnimBlendHierarchy(iter->first);
            CIFPEngine::EngineApplyAnimation(*this, iter->first, iter->first);
            iter = m_mapOfReplacedAnimations.erase(iter);
        }
        else
        {
            ++iter;
        }
    }
}

void CClientPed::RestoreAnimations(CAnimBlock& animationBlock)
{
    CAnimManager* pAnimationManager = g_pGame->GetAnimManager();
    const size_t  cAnimations = animationBlock.GetAnimationCount();
    for (size_t i = 0; i < cAnimations; i++)
    {
        auto pAnimHierarchyInterface = animationBlock.GetAnimationHierarchyInterface(i);
        CIFPEngine::EngineApplyAnimation(*this, pAnimHierarchyInterface, pAnimHierarchyInterface);
        m_mapOfReplacedAnimations.erase(pAnimHierarchyInterface);
    }
}

void CClientPed::RestoreAllAnimations()
{
    CAnimManager* pAnimationManager = g_pGame->GetAnimManager();
    RpClump*      pClump = GetClump();
    if (pClump)
    {
        auto pAnimAssociation = pAnimationManager->RpAnimBlendClumpGetFirstAssociation(pClump);
        while (pAnimAssociation)
        {
            auto       pAnimNextAssociation = pAnimationManager->RpAnimBlendGetNextAssociation(pAnimAssociation);
            auto       pAnimHierarchy = pAnimAssociation->GetAnimHierarchy();
            eAnimGroup iGroupID = pAnimAssociation->GetAnimGroup();
            eAnimID    iAnimID = pAnimAssociation->GetAnimID();
            if (pAnimHierarchy && iGroupID >= eAnimGroup::ANIM_GROUP_DEFAULT && iAnimID >= eAnimID::ANIM_ID_WALK)
            {
                auto pAnimStaticAssociation = pAnimationManager->GetAnimStaticAssociation(iGroupID, iAnimID);
                if (pAnimStaticAssociation && pAnimHierarchy->IsCustom())
                {
                    auto pAnimHierarchyInterface = pAnimStaticAssociation->GetAnimHierarchyInterface();
                    CIFPEngine::EngineApplyAnimation(*this, pAnimHierarchyInterface, pAnimHierarchyInterface);
                }
            }
            pAnimAssociation = std::move(pAnimNextAssociation);
        }
    }
    m_mapOfReplacedAnimations.clear();
}

SReplacedAnimation* CClientPed::GetReplacedAnimation(CAnimBlendHierarchySAInterface* pInternalHierarchyInterface)
{
    CClientPed::ReplacedAnim_type::iterator it;
    it = m_mapOfReplacedAnimations.find(pInternalHierarchyInterface);
    if (it != m_mapOfReplacedAnimations.end())
    {
        return &it->second;
    }
    return nullptr;
}

std::unique_ptr<CAnimBlendAssociation> CClientPed::GetAnimAssociation(CAnimBlendHierarchySAInterface* pOriginalHierarchyInterface)
{
    RpClump* pClump = GetClump();
    if (!pClump)
    {
        return nullptr;
    }

    auto                            pReplacedAnimation = GetReplacedAnimation(pOriginalHierarchyInterface);
    CAnimBlendHierarchySAInterface* pReplacedInterface = nullptr;
    if (pReplacedAnimation != nullptr)
    {
        pReplacedInterface = pReplacedAnimation->pAnimationHierarchy;
    }

    CAnimManager* pAnimationManager = g_pGame->GetAnimManager();
    auto          pAnimAssociation = pAnimationManager->RpAnimBlendClumpGetFirstAssociation(pClump);
    while (pAnimAssociation)
    {
        auto pAnimNextAssociation = pAnimationManager->RpAnimBlendGetNextAssociation(pAnimAssociation);
        auto pAnimHierarchy = pAnimAssociation->GetAnimHierarchy();
        if (pAnimHierarchy)
        {
            CAnimBlendHierarchySAInterface* pInterface = pAnimHierarchy->GetInterface();
            if (pInterface == pOriginalHierarchyInterface)
            {
                return pAnimAssociation;
            }
            if (pReplacedInterface && pInterface == pReplacedInterface)
            {
                return pAnimAssociation;
            }
        }
        pAnimAssociation = std::move(pAnimNextAssociation);
    }
    return nullptr;
}

CSphere CClientPed::GetWorldBoundingSphere()
{
    CSphere     sphere;
    CModelInfo* pModelInfo = g_pGame->GetModelInfo(GetModel());
    if (pModelInfo)
    {
        CBoundingBox* pBoundingBox = pModelInfo->GetBoundingBox();
        if (pBoundingBox)
        {
            sphere.vecPosition = pBoundingBox->vecBoundOffset;
            sphere.fRadius = pBoundingBox->fRadius;
        }
    }
    sphere.vecPosition += GetStreamPosition();
    return sphere;
}

// Currently, this should only be called for the local player
void CClientPed::HandleWaitingForGroundToLoad()
{
    // Check if near any MTA objects
    bool    bNearObject = false;
    CVector vecPosition;
    GetPosition(vecPosition);
    CClientEntityResult result;
    GetClientSpatialDatabase()->SphereQuery(result, CSphere(vecPosition + CVector(0, 0, -3), 5));
    for (CClientEntityResult::const_iterator it = result.begin(); it != result.end(); ++it)
    {
        if ((*it)->GetType() == CCLIENTOBJECT)
        {
            bNearObject = true;
            break;
        }
    }

    if (!bNearObject)
    {
        // If not near any MTA objects, then don't bother waiting
        SetFrozenWaitingForGroundToLoad(false);
#ifdef ASYNC_LOADING_DEBUG_OUTPUTA
        OutputDebugLine("[AsyncLoading]   FreezeUntilCollisionLoaded - Early stop");
#endif
        return;
    }

    // Reset position
    SetPosition(m_matFrozen.vPos);
    SetMatrix(m_matFrozen);
    SetMoveSpeed(CVector());

    // Load load load
    if (GetModelInfo())
        g_pGame->GetStreaming()->LoadAllRequestedModels(false, "CClientPed::HandleWaitingForGroundToLoad");

    // Start out with a fairly big radius to check, and shrink it down over time
    float fUseRadius = 50.0f * (1.f - std::max(0.f, m_fObjectsAroundTolerance));

    // Gather up some flags
    CClientObjectManager* pObjectManager = g_pClientGame->GetObjectManager();
    bool                  bASync = g_pGame->IsASyncLoadingEnabled();
    bool                  bMTAObjLimit = pObjectManager->IsObjectLimitReached();
    bool                  bHasModel = GetModelInfo() != NULL;
#ifndef ASYNC_LOADING_DEBUG_OUTPUTA
    bool bMTALoaded = pObjectManager->ObjectsAroundPointLoaded(vecPosition, fUseRadius, m_usDimension);
#else
    SString strAround;
    bool    bMTALoaded = pObjectManager->ObjectsAroundPointLoaded(vecPosition, fUseRadius, m_usDimension, &strAround);
#endif

#ifdef ASYNC_LOADING_DEBUG_OUTPUTA
    SString status = SString(
        "%2.2f,%2.2f,%2.2f  bASync:%d   bHasModel:%d   bMTALoaded:%d   bMTAObjLimit:%d   m_fGroundCheckTolerance:%2.2f   m_fObjectsAroundTolerance:%2.2f  "
        "fUseRadius:%2.1f",
        vecPosition.fX, vecPosition.fY, vecPosition.fZ, bASync, bHasModel, bMTALoaded, bMTAObjLimit, m_fGroundCheckTolerance, m_fObjectsAroundTolerance,
        fUseRadius);
#endif

    // See if ground is ready
    if ((!bHasModel || !bMTALoaded) && m_fObjectsAroundTolerance < 1.f)
    {
        m_fGroundCheckTolerance = 0.f;
        m_fObjectsAroundTolerance = std::min(1.f, m_fObjectsAroundTolerance + 0.01f);
#ifdef ASYNC_LOADING_DEBUG_OUTPUTA
        status += ("  FreezeUntilCollisionLoaded - wait");
#endif
    }
    else
    {
        // Models should be loaded, but sometimes the collision is still not ready
        // Do a ground distance check to make sure.
        // Make the check tolerance larger with each passing frame
        m_fGroundCheckTolerance = std::min(1.f, m_fGroundCheckTolerance + 0.01f);
        float fDist = GetDistanceFromGround();
        float fUseDist = fDist * (1.f - m_fGroundCheckTolerance);
        if (fUseDist > -0.2f && fUseDist < 1.5f)
            SetFrozenWaitingForGroundToLoad(false);

#ifdef ASYNC_LOADING_DEBUG_OUTPUTA
        status += (SString("  GetDistanceFromGround:  fDist:%2.2f   fUseDist:%2.2f", fDist, fUseDist));
#endif

        // Stop waiting after 3 frames, if the object limit has not been reached. (bASync should always be false here)
        if (m_fGroundCheckTolerance > 0.03f && !bMTAObjLimit && !bASync)
            SetFrozenWaitingForGroundToLoad(false);
    }

#ifdef ASYNC_LOADING_DEBUG_OUTPUTA
    OutputDebugLine(SStringX("[AsyncLoading] ")++ status);
    g_pCore->GetGraphics()->DrawString(10, 220, -1, 1, status);

    std::vector<SString> lineList;
    strAround.Split("\n", lineList);
    for (unsigned int i = 0; i < lineList.size(); i++)
        g_pCore->GetGraphics()->DrawString(10, 230 + i * 10, -1, 1, lineList[i]);
#endif
}

//
// CClientPed::UpdateStreamPosition
//
// If ped is in vehicle, make his stream position the same as the vehicle.
// This prevents multiple triggering of collision events and makes collision state consistent with the server
// (This function doesn't need to be virtual)
//
void CClientPed::UpdateStreamPosition(const CVector& vecInPosition)
{
    CVector         vecPosition = vecInPosition;
    CClientVehicle* pVehicle = GetOccupiedVehicle();
    if (pVehicle)
    {
        pVehicle->GetPosition(vecPosition);
        // Optimization if position is the same
        if (vecPosition == GetStreamPosition())
            return;
    }
    CClientStreamElement::UpdateStreamPosition(vecPosition);
}

//////////////////////////////////////////////////////////////////
//
// CClientPed::EnterVehicle
//
// Asks server for permission to start entering vehicle
//
//////////////////////////////////////////////////////////////////
bool CClientPed::EnterVehicle(CClientVehicle* pVehicle, bool bPassenger, std::optional<unsigned int> optSeat)
{
    // Are we local player or ped we are syncing
    if (!IsSyncing() && !IsLocalPlayer() && !IsLocalEntity())
        return false;

    // Are we already inside a vehicle
    if (GetOccupiedVehicle())
        return false;

    // We dead or in water?
    if (IsDead())
        return false;

    // Are we already sending an in/out request or not allowed to create a new in/out?
    if (m_bNoNewVehicleTask                        // Are we permitted to even enter a vehicle?
        || m_VehicleInOutID != INVALID_ELEMENT_ID  // Make sure we're not already processing a vehicle enter (would refer to valid ID if we were)
        || m_bIsGettingJacked                      // Make sure we're not currently getting carjacked &&
        || m_bIsGettingIntoVehicle                 // We can't enter a vehicle we're currently entering...
        || m_bIsGettingOutOfVehicle                // We can't enter a vehicle we're currently leaving...
        || CClientTime::GetTime() < m_ulLastVehicleInOutTime + VEHICLE_INOUT_DELAY  // We are trying to enter the vehicle to soon
    )
    {
        return false;
    }

    // Reset the "is jacking" bit
    m_bIsJackingVehicle = false;

    // Streamed?
    if (!m_pPlayerPed)
        return false;

    unsigned int uiDoor = 0;
    // Do we want to enter a specific vehicle?
    if (!pVehicle)
    {
        // Find the closest vehicle and door
        CClientVehicle* pClosestVehicle = GetClosestEnterableVehicle(true, !bPassenger, bPassenger, false, &uiDoor, nullptr, 20.0f, IsLocalEntity());
        if (pClosestVehicle)
        {
            pVehicle = pClosestVehicle;
        }
        else
        {
            return false;
        }
    }
    else
    {
        // Find the closest door
        GetClosestDoor(pVehicle, !bPassenger, bPassenger, uiDoor, nullptr);
    }

    // Dead vehicle?
    if (pVehicle->GetHealth() <= 0.0f)
        return false;

    // Stop if the vehicle is not enterable
    if (!pVehicle->IsEnterable(IsLocalEntity()))
        return false;

    // Stop if the ped is swimming and the vehicle model cannot be entered from water (fixes #1990)
    auto vehicleModel = static_cast<VehicleType>(pVehicle->GetModel());

    if (IsInWater() && !(vehicleModel == VehicleType::VT_SKIMMER || vehicleModel == VehicleType::VT_SEASPAR || vehicleModel == VehicleType::VT_LEVIATHN ||
                         vehicleModel == VehicleType::VT_VORTEX))
        return false;

    // If the Jump task is playing and we are in water - I know right
    // Kill the task.
    CTask* pTask = GetCurrentPrimaryTask();
    if (pTask && pTask->GetTaskType() == TASK_COMPLEX_JUMP)  // Kill jump task - breaks warp in entry and doesn't really matter
    {
        if (pVehicle->IsInWater() || IsInWater())  // Make sure we are about to warp in (this bug only happens when someone jumps into water with a vehicle)
            KillTask(3, true);                     // Kill jump task if we are about to warp in
    }

    // Make sure we don't have any other primary tasks running, otherwise our 'enter-vehicle'
    // task will replace it and fuck it up!
    if (GetCurrentPrimaryTask())
        return false;

    if (IsClimbing()             // Make sure we're not currently climbing
        || HasJetPack()          // Make sure we don't have a jetpack
        || IsUsingGun()          // Make sure we're not using a gun (have the gun task active) - we stop it in UpdatePlayerTasks anyway
        || IsRunningAnimation()  // Make sure we aren't running an animation
    )
    {
        return false;
    }

    // Determine seat - either explicitly specified or auto-determined from door/passenger flag
    unsigned int uiSeat;

    if (optSeat.has_value())
    {
        // Explicit seat specified
        uiSeat = optSeat.value();
        if (!CClientVehicleManager::IsValidSeat(pVehicle->GetModel(), static_cast<unsigned char>(uiSeat)))
            return false;
    }
    else
    {
        // Legacy behavior - auto-determine seat from door/passenger flag
        uiSeat = uiDoor;
        if (bPassenger && uiDoor == 0)
        {
            // We're trying to enter as a passenger, yet our closest door
            // is the driver's door. Force an enter for the passenger seat.
            uiSeat = 1;
        }
        else if (!bPassenger)
        {
            // We want to drive. Force our seat to the driver's seat.
            uiSeat = 0;
        }
    }

    // If the vehicle's a boat, make sure we're standing on it (we need a dif task to enter boats properly)
    if (pVehicle->GetVehicleType() == CLIENTVEHICLE_BOAT && GetContactEntity() != pVehicle)
    {
        return false;
    }

    // Validate camper seat to avoid multiple occupants && desyncronization
    if (vehicleModel == VehicleType::VT_CAMPER && uiSeat > 0 && pVehicle->GetOccupant(uiSeat))
        return false;

    // Call the onClientVehicleStartEnter event for the ped
    // Check if it is cancelled before sending packet
    CLuaArguments Arguments;
    Arguments.PushElement(this);   // player / ped
    Arguments.PushNumber(uiSeat);  // seat
    Arguments.PushNumber(uiDoor);  // door

    if (!pVehicle->CallEvent("onClientVehicleStartEnter", Arguments, true))
    {
        // Event has been cancelled
        return false;
    }

    if (IsLocalEntity())
    {
        // If vehicle is not local, we can't enter it
        if (!pVehicle->IsLocalEntity())
            return false;

        // Set the vehicle id we're about to enter
        m_VehicleInOutID = pVehicle->GetID();
        m_ucVehicleInOutSeat = static_cast<unsigned char>(uiSeat);
        m_bIsJackingVehicle = false;

        // Make ped enter vehicle
        GetIntoVehicle(pVehicle, uiSeat, static_cast<unsigned char>(uiDoor));

        // Remember that this ped is working on entering a vehicle
        SetVehicleInOutState(VEHICLE_INOUT_GETTING_IN);

        pVehicle->CalcAndUpdateCanBeDamagedFlag();
        pVehicle->CalcAndUpdateTyresCanBurstFlag();

        m_bIsGettingIntoVehicle = true;
        m_bIsGettingOutOfVehicle = false;

        return true;
    }

    // Send an in request
    NetBitStreamInterface* pBitStream = g_pNet->AllocateNetBitStream();
    if (!pBitStream)
    {
        return false;
    }

    pBitStream->Write(GetID());

    // Write the vehicle id to it and that we're requesting to get into it
    pBitStream->Write(pVehicle->GetID());
    unsigned char ucAction = static_cast<unsigned char>(CClientGame::VEHICLE_REQUEST_IN);
    unsigned char ucSeat = static_cast<unsigned char>(uiSeat);
    bool          bIsOnWater = pVehicle->IsOnWater();
    unsigned char ucDoor = static_cast<unsigned char>(uiDoor);
    pBitStream->WriteBits(&ucAction, 4);
    pBitStream->WriteBits(&ucSeat, 4);
    pBitStream->WriteBit(bIsOnWater);
    pBitStream->WriteBits(&ucDoor, 3);

    // Send and destroy it
    g_pNet->SendPacket(PACKET_ID_VEHICLE_INOUT, pBitStream, PACKET_PRIORITY_HIGH, PACKET_RELIABILITY_RELIABLE_ORDERED);
    g_pNet->DeallocateNetBitStream(pBitStream);

    // We're now entering a vehicle
    m_bIsGettingIntoVehicle = true;
    m_ulLastVehicleInOutTime = CClientTime::GetTime();

#ifdef MTA_DEBUG
    g_pCore->GetConsole()->Printf("* Sent_InOut: vehicle_request_in");
#endif

    return true;
}

//////////////////////////////////////////////////////////////////
//
// CClientPed::ExitVehicle
//
// Asks server for permission to start exiting vehicle
//
//////////////////////////////////////////////////////////////////
bool CClientPed::ExitVehicle()
{
    // Are we local player or ped we are syncing
    if (!IsSyncing() && !IsLocalPlayer() && !IsLocalEntity())
    {
        return false;
    }

    // Get our occupied vehicle
    CClientVehicle* pOccupiedVehicle = GetOccupiedVehicle();
    if (!pOccupiedVehicle)
    {
        return false;
    }

    // We dead?
    if (IsDead())
    {
        return false;
    }

    // Are we already sending an in/out request or not allowed to create a new in/out?
    if (m_bNoNewVehicleTask                        // Are we permitted to even enter a vehicle?
        || m_VehicleInOutID != INVALID_ELEMENT_ID  // Make sure we're not already processing a vehicle enter (would refer to valid ID if we were)
        || m_bIsGettingJacked                      // Make sure we're not currently getting carjacked &&
        || m_bIsGettingIntoVehicle                 // We can't enter a vehicle we're currently entering...
        || m_bIsGettingOutOfVehicle                // We can't enter a vehicle we're currently leaving...
        || CClientTime::GetTime() < m_ulLastVehicleInOutTime + VEHICLE_INOUT_DELAY  // We are trying to enter the vehicle to soon
    )
    {
        return false;
    }

    // Reset the "is jacking" bit
    m_bIsJackingVehicle = false;

    // Streamed?
    if (!m_pPlayerPed)
    {
        return false;
    }

    const int rawDoor = g_pGame->GetCarEnterExit()->ComputeTargetDoorToExit(m_pPlayerPed, pOccupiedVehicle->GetGameVehicle());
    auto      targetDoor = static_cast<std::int8_t>(rawDoor);

    // If it's a local entity, we can just exit the vehicle
    if (IsLocalEntity())
    {
        // Set the vehicle id and the seat we're about to exit from
        m_VehicleInOutID = pOccupiedVehicle->GetID();
        m_ucVehicleInOutSeat = static_cast<unsigned char>(GetOccupiedVehicleSeat());

        // Call the onClientVehicleStartExit event for the ped
        // Check if it is cancelled before making the ped exit the vehicle
        CLuaArguments arguments;
        arguments.PushElement(this);                 // player / ped
        arguments.PushNumber(m_ucVehicleInOutSeat);  // seat
        arguments.PushNumber(0);                     // door

        if (!pOccupiedVehicle->CallEvent("onClientVehicleStartExit", arguments, true))  // Event has been cancelled
            return false;

        // Make ped exit vehicle
        GetOutOfVehicle(m_ucVehicleInOutSeat);

        // Remember that this ped is working on leaving a vehicle
        SetVehicleInOutState(VEHICLE_INOUT_GETTING_OUT);

        m_bIsGettingIntoVehicle = false;
        m_bIsGettingOutOfVehicle = true;

        return true;
    }

    // We're about to exit a vehicle
    // Send an out request
    NetBitStreamInterface* pBitStream = g_pNet->AllocateNetBitStream();
    if (!pBitStream)
    {
        return false;
    }

    pBitStream->Write(GetID());

    // Write the vehicle id to it and that we're requesting to get out of it
    pBitStream->Write(pOccupiedVehicle->GetID());
    unsigned char ucAction = static_cast<unsigned char>(CClientGame::VEHICLE_REQUEST_OUT);
    pBitStream->WriteBits(&ucAction, 4);

    if (targetDoor >= 2 && targetDoor <= 5)
    {
        targetDoor -= 2;
        pBitStream->WriteBits(&targetDoor, 2);
    }

    // Send and destroy it
    g_pNet->SendPacket(PACKET_ID_VEHICLE_INOUT, pBitStream, PACKET_PRIORITY_HIGH, PACKET_RELIABILITY_RELIABLE_ORDERED);
    g_pNet->DeallocateNetBitStream(pBitStream);

    // We're now exiting a vehicle
    m_bIsGettingOutOfVehicle = true;
    m_ulLastVehicleInOutTime = CClientTime::GetTime();

#ifdef MTA_DEBUG
    g_pCore->GetConsole()->Printf("* Sent_InOut: vehicle_request_out");
#endif

    return true;
}

// ResetVehicleInOut resets enter/exit variables, and is only called for the local player or for peds we're syncing.
void CClientPed::ResetVehicleInOut()
{
    m_ulLastVehicleInOutTime = 0;
    m_bIsGettingOutOfVehicle = false;
    m_bIsGettingIntoVehicle = false;
    m_bIsJackingVehicle = false;
    m_bIsGettingJacked = false;
    m_VehicleInOutID = INVALID_ELEMENT_ID;
    m_ucVehicleInOutSeat = 0xFF;
    m_bNoNewVehicleTask = false;
    m_NoNewVehicleTaskReasonID = INVALID_ELEMENT_ID;
    m_pGettingJackedBy = NULL;
}

//////////////////////////////////////////////////////////////////
//
// CClientPed::UpdateVehicleInOut
//
// Update enter/exit sequence
//
//////////////////////////////////////////////////////////////////
void CClientPed::UpdateVehicleInOut()
{
    // Script-command tasks can make a synchronized ped leave its vehicle
    // without going through CClientPed::ExitVehicle. Enter the ordinary
    // request/confirmation lifecycle as soon as the active native hierarchy
    // exposes a scripted leave or emergency jump-out. Viewers then run GTA's
    // exit task and the server clears the authoritative seat before
    // locomotion presentation resumes.
    if (IsSyncing() && m_VehicleInOutID == INVALID_ELEMENT_ID && !m_bIsGettingIntoVehicle && !m_bIsGettingOutOfVehicle && GetOccupiedVehicle() &&
        GetRealOccupiedVehicle() && IsLeavingVehicle())
    {
        ExitVehicle();
    }

    if (IsLocalEntity())
    {
        // If getting inside vehicle
        if (m_bIsGettingIntoVehicle)
        {
            CClientVehicle* vehicle = GetRealOccupiedVehicle();
            if (!vehicle)
                return;

            // Call the onClientVehicleEnter event for the ped
            // Check if it is cancelled before allowing the ped to enter the vehicle
            CLuaArguments arguments;
            arguments.PushElement(this);                 // player / ped
            arguments.PushNumber(m_ucVehicleInOutSeat);  // seat

            if (!vehicle->CallEvent("onClientVehicleEnter", arguments, true))
            {
                m_bIsGettingIntoVehicle = false;
                RemoveFromVehicle();
                return;
            }

            m_bIsGettingIntoVehicle = false;
            m_VehicleInOutID = INVALID_ELEMENT_ID;
            WarpIntoVehicle(vehicle, m_ucVehicleInOutSeat);
            SetVehicleInOutState(VEHICLE_INOUT_NONE);
        }
        else if (m_bIsGettingOutOfVehicle)
        {
            // If getting out of vehicle
            CClientVehicle* realVehicle = GetRealOccupiedVehicle();
            CClientVehicle* networkVehicle = GetOccupiedVehicle();

            if (realVehicle)
                return;

            // Call the onClientVehicleExit event for the ped
            CLuaArguments arguments;
            arguments.PushElement(this);                 // player / ped
            arguments.PushNumber(m_ucVehicleInOutSeat);  // seat
            networkVehicle->CallEvent("onClientVehicleExit", arguments, true);

            m_bIsGettingOutOfVehicle = false;
            m_VehicleInOutID = INVALID_ELEMENT_ID;
            RemoveFromVehicle();
            SetVehicleInOutState(VEHICLE_INOUT_NONE);
        }

        return;
    }

    // We got told by the server to animate into a certain vehicle?
    if (m_VehicleInOutID != INVALID_ELEMENT_ID)
    {
        // Grab the vehicle we're getting in/out of
        CDeathmatchVehicle* pInOutVehicle = static_cast<CDeathmatchVehicle*>(g_pClientGame->GetVehicleManager()->Get(m_VehicleInOutID));

        // In or out?
        if (m_bIsGettingOutOfVehicle)
        {
            // If we aren't working on leaving the car (he's eiter finished or cancelled/failed leaving)
            if (IsLeavingVehicle())
                return;

            // Are we outside the car?
            CClientVehicle* pVehicle = GetRealOccupiedVehicle();
            if (pVehicle)
            {
                // Warp us out now to keep in sync with the server
                RemoveFromVehicle();
                return;
            }
            // Tell the server that we successfully left the car
            NetBitStreamInterface* pBitStream = g_pNet->AllocateNetBitStream();
            if (pBitStream)
            {
                // Write the ped ID to it
                pBitStream->Write(GetID());

                // Write the car id and the action id (enter complete)
                pBitStream->Write(m_VehicleInOutID);
                unsigned char ucAction = CClientGame::VEHICLE_NOTIFY_OUT;
                pBitStream->WriteBits(&ucAction, 4);

                // Send it and destroy the packet
                g_pNet->SendPacket(PACKET_ID_VEHICLE_INOUT, pBitStream, PACKET_PRIORITY_HIGH, PACKET_RELIABILITY_RELIABLE_ORDERED);
                g_pNet->DeallocateNetBitStream(pBitStream);
            }

            // Warp ourself out (so we're sure the records are correct)
            RemoveFromVehicle();

            if (pInOutVehicle)
            {
                pInOutVehicle->CalcAndUpdateCanBeDamagedFlag();
                pInOutVehicle->CalcAndUpdateTyresCanBurstFlag();
            }

            // Reset the vehicle in out stuff so we're ready for another car entry/leave.
            // Don't allow a new entry/leave until we've gotten the notify return packet
            ElementID ReasonVehicleID = m_VehicleInOutID;
            ResetVehicleInOut();
            m_bNoNewVehicleTask = true;
            m_NoNewVehicleTaskReasonID = ReasonVehicleID;

#ifdef MTA_DEBUG
            g_pCore->GetConsole()->Printf("* Sent_InOut: vehicle_notify_out");
#endif
        }

        // Are we getting into a vehicle?
        else if (m_bIsGettingIntoVehicle)
        {
            // If we aren't working on entering the car (he's either finished or cancelled)
            // Or we are dead (fix for #908) or we are in water (fix for #521)
            if (IsEnteringVehicle() && !IsDead() && !IsInWater())
                return;

            // Is he in a vehicle now?
            CClientVehicle* pVehicle = GetRealOccupiedVehicle();
            if (pVehicle)
            {
                // Tell the server that we successfully entered the car
                NetBitStreamInterface* pBitStream = g_pNet->AllocateNetBitStream();
                if (pBitStream)
                {
                    // Write the ped or player ID to it
                    pBitStream->Write(GetID());

                    // Write the car id and the action id (enter complete)
                    pBitStream->Write(m_VehicleInOutID);
                    unsigned char ucAction;

                    if (m_bIsJackingVehicle)
                    {
                        ucAction = static_cast<unsigned char>(CClientGame::VEHICLE_NOTIFY_JACK);
#ifdef MTA_DEBUG
                        g_pCore->GetConsole()->Printf("* Sent_InOut: vehicle_notify_jack");
#endif
                    }
                    else
                    {
                        ucAction = static_cast<unsigned char>(CClientGame::VEHICLE_NOTIFY_IN);
#ifdef MTA_DEBUG
                        g_pCore->GetConsole()->Printf("* Sent_InOut: vehicle_notify_in");
#endif
                    }
                    pBitStream->WriteBits(&ucAction, 4);

                    // Send it and destroy the packet
                    g_pNet->SendPacket(PACKET_ID_VEHICLE_INOUT, pBitStream, PACKET_PRIORITY_HIGH, PACKET_RELIABILITY_RELIABLE_ORDERED);
                    g_pNet->DeallocateNetBitStream(pBitStream);
                }

                // Warp ourself in (so we're sure the records are correct)
                pVehicle->AllowDoorRatioSetting(m_ucEnteringDoor, true);
                WarpIntoVehicle(pVehicle, m_ucVehicleInOutSeat);

                if (pInOutVehicle)
                {
                    pInOutVehicle->CalcAndUpdateCanBeDamagedFlag();
                    pInOutVehicle->CalcAndUpdateTyresCanBurstFlag();
                }
            }
            else
            {
                // Tell the server that we aborted entered the car
                NetBitStreamInterface* pBitStream = g_pNet->AllocateNetBitStream();
                if (pBitStream)
                {
                    // Write the ped or player ID to it
                    pBitStream->Write(GetID());

                    // Write the car id and the action id (enter complete)
                    pBitStream->Write(m_VehicleInOutID);
                    unsigned char ucAction;
                    if (m_bIsJackingVehicle)
                    {
                        ucAction = static_cast<unsigned char>(CClientGame::VEHICLE_NOTIFY_JACK_ABORT);
                        pBitStream->WriteBits(&ucAction, 4);

                        // Did we start jacking them?
                        bool            bAlreadyStartedJacking = false;
                        CClientVehicle* pVehicle = DynamicCast<CClientVehicle>(CElementIDs::GetElement(m_VehicleInOutID));
                        if (pVehicle)
                        {
                            CClientPed* pJackedPlayer = pVehicle->GetOccupant();
                            if (pJackedPlayer)
                            {
                                // Jax: have we already started to jack the other player?
                                if (pJackedPlayer->IsGettingJacked())
                                {
                                    bAlreadyStartedJacking = true;
                                }
                            }
                            unsigned char ucDoor = m_ucEnteringDoor - 2;
                            pBitStream->WriteBits(&ucDoor, 3);
                            SDoorOpenRatioSync door;
                            door.data.fRatio = pVehicle->GetDoorOpenRatio(m_ucEnteringDoor);
                            pBitStream->Write(&door);
                        }
                        pBitStream->WriteBit(bAlreadyStartedJacking);

#ifdef MTA_DEBUG
                        g_pCore->GetConsole()->Printf("* Sent_InOut: vehicle_notify_jack_abort");
#endif
                    }
                    else
                    {
                        ucAction = static_cast<unsigned char>(CClientGame::VEHICLE_NOTIFY_IN_ABORT);
                        pBitStream->WriteBits(&ucAction, 4);
                        CClientVehicle* pVehicle = DynamicCast<CClientVehicle>(CElementIDs::GetElement(m_VehicleInOutID));
                        if (pVehicle)
                        {
                            unsigned char ucDoor = m_ucEnteringDoor - 2;
                            pBitStream->WriteBits(&ucDoor, 3);
                            SDoorOpenRatioSync door;
                            door.data.fRatio = pVehicle->GetDoorOpenRatio(m_ucEnteringDoor);
                            pBitStream->Write(&door);
                        }

#ifdef MTA_DEBUG
                        g_pCore->GetConsole()->Printf("* Sent_InOut: vehicle_notify_in_abort");
#endif
                    }

                    // Send it and destroy the packet
                    g_pNet->SendPacket(PACKET_ID_VEHICLE_INOUT, pBitStream, PACKET_PRIORITY_HIGH, PACKET_RELIABILITY_RELIABLE_ORDERED);
                    g_pNet->DeallocateNetBitStream(pBitStream);
                }

                // Warp ourself out again (so we're sure the records are correct)
                RemoveFromVehicle();

                if (pInOutVehicle)
                {
                    pInOutVehicle->CalcAndUpdateCanBeDamagedFlag();
                    pInOutVehicle->CalcAndUpdateTyresCanBurstFlag();
                }
            }

            // Reset
            // Don't allow a new entry/leave until we've gotten the notify return packet
            ElementID ReasonID = m_VehicleInOutID;
            ResetVehicleInOut();
            m_bNoNewVehicleTask = true;
            m_NoNewVehicleTaskReasonID = ReasonID;
        }
    }
    else
    {
        // If we aren't streamed, stop here
        if (!m_pPlayerPed)
            return;

        // If we aren't getting jacked
        if (m_bIsGettingJacked)
            return;

        CClientVehicle* pVehicle = GetRealOccupiedVehicle();
        CClientVehicle* pOccupiedVehicle = GetOccupiedVehicle();

        // Jax: this was commented, re-comment if it was there for a reason (..and give the reason!)
        // Are we in a vehicle we aren't supposed to be in?
        if (pVehicle && !pOccupiedVehicle)
        {
            g_pCore->GetConsole()->Print("You shouldn't be in this vehicle");
            RemoveFromVehicle();
        }

        // Are we supposed to be in a vehicle? But aren't?
        if (!pOccupiedVehicle || pVehicle || IsWarpInToVehicleRequired())
            return;

        // Jax: this happens when we try to warp into a streamed out vehicle, including when we use CClientVehicle::StreamInNow
        // ..maybe we need a different way to detect bike falls?

        // Tell the server
        NetBitStreamInterface* pBitStream = g_pNet->AllocateNetBitStream();
        if (!pBitStream)
            return;

        // Write the ped or player ID to it
        pBitStream->Write(GetID());

        // Vehicle id
        pBitStream->Write(pOccupiedVehicle->GetID());
        unsigned char ucAction = static_cast<unsigned char>(CClientGame::VEHICLE_NOTIFY_FELL_OFF);
        pBitStream->WriteBits(&ucAction, 4);

        // Send it and destroy the packet
        g_pNet->SendPacket(PACKET_ID_VEHICLE_INOUT, pBitStream, PACKET_PRIORITY_HIGH, PACKET_RELIABILITY_RELIABLE_ORDERED);
        g_pNet->DeallocateNetBitStream(pBitStream);

        // We're not allowed to enter any vehicle before we get a confirm
        m_bNoNewVehicleTask = true;
        m_NoNewVehicleTaskReasonID = pOccupiedVehicle->GetID();

        // Remove him from the vehicle
        RemoveFromVehicle();

        /*
        // Make it undamagable if we're not syncing it
        CDeathmatchVehicle* pInOutVehicle = static_cast < CDeathmatchVehicle* > ( pOccupiedVehicle );
        if ( pInOutVehicle )
        {
            if ( pInOutVehicle->IsSyncing () )
            {
                pInOutVehicle->SetCanBeDamaged ( true );
                pInOutVehicle->SetTyresCanBurst ( true );
            }
            else
            {
                pInOutVehicle->SetCanBeDamaged ( false );
                pInOutVehicle->SetTyresCanBurst ( false );
            }
        }
        */

#ifdef MTA_DEBUG
        g_pCore->GetConsole()->Printf("* Sent_InOut: vehicle_notify_fell_off");
#endif
    }
}

// Called from CPedSync
void CClientPed::SetSyncing(bool bIsSyncing)
{
    if (m_bIsSyncing != bIsSyncing)
    {
        // A post-render sample must never be correlated with a receive from a
        // previous ownership epoch.
        m_nativeAIRotationNetworkSample = {};
        m_nativeAIRotationTelemetryNextSampleAt = 0;
        if (!bIsSyncing)
        {
            // If the old owner was waiting on unloaded collision, publish and
            // bootstrap the observer from the last collision-backed transform
            // rather than the locally integrated fall it prevented.
            ClearNativeAmbientOwnerCollisionFence("lost_syncer");
            // The final locally-owned state is the observer's authoritative
            // bootstrap until the first spatial packet from the new syncer.
            GetPosition(m_remoteAuthoritativeTransform.position);
            m_remoteAuthoritativeTransform.positionValid = true;
            m_remoteAuthoritativeTransform.rotation = GetCurrentRotation();
            m_remoteAuthoritativeTransform.rotationValid = true;
            GetMoveSpeed(m_remoteAuthoritativeTransform.moveSpeed);
            m_remoteAuthoritativeTransform.moveSpeedValid = true;
            m_remoteAuthoritativeTransform.restoreAllowed = true;
        }
        else
        {
            ClearNativeAmbientOwnerCollisionFence("became_syncer", false);
            // Local authority now owns the live GTA state. Do not let an old
            // observer snapshot reseed a later physical/task transition.
            m_remoteAuthoritativeTransform = {};
            // Packet_PedStartSync installs an immediate heading rather than a
            // new interpolation. Clear the observer's old rotation timer even
            // when no collision fence happened to be active.
            m_ulBeginRotationTime = 0;
            m_ulEndRotationTime = 0;
        }
        m_nativeAmbientOwnerCollisionFence.snapshotValid = false;

        if (!bIsSyncing && m_pTaskManager)
        {
            // The old authority must not keep integrating a native vertical
            // or anchor-authored task after handoff. Remove only the exact
            // jump/in-air/climb chains; unrelated tasks remain untouched.
            bool removedPhysicalTask = false;
            for (const int priority : {TASK_PRIORITY_EVENT_RESPONSE_TEMP, TASK_PRIORITY_EVENT_RESPONSE_NONTEMP, TASK_PRIORITY_PRIMARY})
            {
                if (m_pTaskManager->FindTaskByType(priority, TASK_COMPLEX_JUMP) || m_pTaskManager->FindTaskByType(priority, TASK_COMPLEX_IN_AIR_AND_LAND) ||
                    m_pTaskManager->FindTaskByType(priority, TASK_SIMPLE_CLIMB))
                {
                    KillTask(priority, true);
                    removedPhysicalTask = true;
                }
            }
            if (removedPhysicalTask && m_pPlayerPed)
                m_pPlayerPed->SetNativeTaskAirbornePresentationState(false, false);
        }

        // A start-sync packet can seed this state even when the new owner was
        // not close enough to receive the last presentation snapshot. Keep
        // the physical phase while removing the observer-only association.
        // Do not wait for EVENT_IN_AIR here: its geometry gate is intentionally
        // local and can reject an early handoff while the observer transform
        // is still close to the floor. Airborne resumes directly in GTA's
        // response task; climb resumes from its transferred anchor and phase.
        const bool continuePhysical =
            bIsSyncing && m_nativeTaskPhysicalTakeoverPending && SNativeTaskAnimationPresentationSync::IsPhysicalMode(m_nativeTaskPhysicalTakeover.data.uiMode);
        if (m_nativeTaskAirbornePresentationActive && m_pPlayerPed)
        {
            m_pPlayerPed->SetNativeTaskAirbornePresentationState(false, false);
            m_nativeTaskAirbornePresentationActive = false;
        }
        SetNativeTaskLocomotionPresentation({}, "syncer_transition");
        m_nativeTaskLocomotionAuthoritativeVelocityValid = false;
        ClearNativeTaskWeaponPresentation("syncer_transition");
        ClearNativeTaskAnimationPresentation("syncer_transition");
        if (continuePhysical && m_pPlayerPed)
        {
            m_pPlayerPed->SetNativeTaskAirbornePresentationState(true, false);
            CTask* physicalTask = nullptr;
            if (m_nativeTaskPhysicalTakeover.data.uiMode == SNativeTaskAnimationPresentationSync::CLIMB_ANIMATION)
            {
                SClimbTaskState climbState;
                climbState.handhold = m_nativeTaskPhysicalTakeover.data.vecClimbHandhold;
                climbState.worldHandhold = m_nativeTaskPhysicalTakeover.data.vecClimbWorldHandhold;
                climbState.anchorPosition = m_nativeTaskPhysicalTakeover.data.vecClimbAnchorPosition;
                climbState.handholdHeading = m_nativeTaskPhysicalTakeover.data.fClimbHeading;
                climbState.anchorModel = m_nativeTaskPhysicalTakeover.data.usClimbAnchorModel;
                climbState.anchorType = m_nativeTaskPhysicalTakeover.data.ucClimbAnchorType;
                climbState.surfaceType = m_nativeTaskPhysicalTakeover.data.ucClimbSurfaceType;
                climbState.animationPhase = static_cast<eClimbHeights>(m_nativeTaskPhysicalTakeover.data.ucClimbAnimationPhase);
                climbState.positionPhase = static_cast<eClimbHeights>(m_nativeTaskPhysicalTakeover.data.ucClimbPositionPhase);
                climbState.getToPositionCounter = m_nativeTaskPhysicalTakeover.data.usClimbGetToPositionCounter;
                climbState.animationGroup = m_nativeTaskPhysicalTakeover.data.usAnimGroup;
                climbState.animationId = m_nativeTaskPhysicalTakeover.data.usAnimId;
                climbState.animationProgress = m_nativeTaskPhysicalTakeover.data.fProgress;
                climbState.animationSpeed = m_nativeTaskPhysicalTakeover.data.fSpeed;
                climbState.animationBlendAmount = m_nativeTaskPhysicalTakeover.data.fBlendAmount;
                climbState.forceClimb = m_nativeTaskPhysicalTakeover.data.bForceClimb;
                climbState.invalidClimb = m_nativeTaskPhysicalTakeover.data.bInvalidClimb;
                climbState.changePosition = m_nativeTaskPhysicalTakeover.data.bClimbChangePosition;
                climbState.animationPlaying = m_nativeTaskPhysicalTakeover.data.bClimbAnimationPlaying;

                physicalTask = g_pGame->GetTasks()->CreateTaskSimpleClimbTakeover(m_pPlayerPed, climbState);

                // The matching collision entity can stream a frame later on
                // the new owner. Continue with GTA's native airborne/landing
                // chain instead of leaving the ped physically suspended.
                if (!physicalTask)
                    physicalTask = g_pGame->GetTasks()->CreateTaskComplexInAirAndLand(true, false);
            }
            else
                physicalTask = g_pGame->GetTasks()->CreateTaskComplexInAirAndLand(true, false);

            if (!physicalTask || !SetTask(physicalTask, TASK_PRIORITY_EVENT_RESPONSE_TEMP))
            {
                // Resolution or allocation failure must not leave physical
                // flags latched without a task that can clear them.
                SetNativeTaskPhysicalTakeoverState({});
            }
            else if (m_nativeTaskPhysicalTakeover.data.uiMode == SNativeTaskAnimationPresentationSync::CLIMB_ANIMATION)
            {
                // The climb factory already restored the exact native phase
                // and association synchronously. A delayed progress replay can
                // target the next phase if GTA advances before Update(), so
                // retire only the serialized snapshot and leave task-owned
                // physical flags intact.
                m_nativeTaskPhysicalTakeover = {};
                m_nativeTaskPhysicalTakeoverPending = false;
                m_nativeTaskPhysicalTakeoverStartedAt = 0;
            }
        }
        else if (!bIsSyncing)
        {
            m_nativeTaskPhysicalTakeover = {};
            m_nativeTaskPhysicalTakeoverPending = false;
            m_nativeTaskPhysicalTakeoverStartedAt = 0;
        }
    }
    m_bIsSyncing = bIsSyncing;
    if (bIsSyncing)
    {
        // StartSync installs an immediate position and rotation before flipping
        // this bit. Drop the observer's old interpolation timer so it cannot
        // overwrite that new authoritative heading on the next pulse.
        ClearRemoteStreamInTransformFence("became_syncer");
        m_remoteReplicaPhysicsFenceActive = false;
        ApplyPhysicalFreezeState();
    }
    ApplyNativeEventProfileState();
    if (bIsSyncing && GetType() == CCLIENTPED)
    {
        bool markedNativeAgent = false;
        GetCustomDataBool(CStringName("neon:ambientPedTraffic"), markedNativeAgent, false);
        dassert(!markedNativeAgent || m_nativeCollisionResidencyReady || m_nativeCollisionAuthorityFence.active);
    }
    if (!bIsSyncing)
    {
        // Reset vehicle in/out stuff in case the ped was entering/exiting
        ResetVehicleInOut();
    }
}

void CClientPed::RunClimbingTask()
{
    if (!m_pPlayerPed)
        return;

    CVector climbPos;
    float   climbAngle;
    int     surfaceType;

    CEntitySAInterface* climbEntity = CTaskSimpleClimb::TestForClimb(m_pPlayerPed, climbPos, climbAngle, surfaceType, true);

    // If a ped is in the air, its rotation is inverted (see GetRotationDegressNew, GetRotationRadiansNew)
    if (!IsOnGround() && !climbEntity)
    {
        CVector rot;
        GetRotationDegrees(rot);

        rot.fZ += 180.0f;
        SetRotationDegrees(rot);

        climbEntity = CTaskSimpleClimb::TestForClimb(m_pPlayerPed, climbPos, climbAngle, surfaceType, true);
    }

    if (!climbEntity)
        return;

    CTaskSimpleClimb* climbTask = g_pGame->GetTasks()->CreateTaskSimpleClimb(climbEntity, climbPos, climbAngle, static_cast<unsigned char>(surfaceType),
                                                                             eClimbHeights::CLIMB_GRAB, false);
    if (!climbTask)
        return;

    climbTask->SetAsPedTask(m_pPlayerPed, TASK_PRIORITY_PRIMARY, true);
}

CTaskSimpleSwim* CClientPed::GetSwimmingTask() const
{
    if (!m_pPlayerPed)
        return nullptr;

    CTask* simplestTask = const_cast<CTaskManager*>(GetTaskManager())->GetSimplestActiveTask();
    if (!simplestTask || simplestTask->GetTaskType() != TASK_SIMPLE_SWIM)
        return nullptr;

    auto* swimmingTask = dynamic_cast<CTaskSimpleSwim*>(simplestTask);
    return swimmingTask;
}

void CClientPed::RunSwimTask() const
{
    if (!m_pPlayerPed || GetSwimmingTask())
        return;

    CTaskComplexInWater* inWaterTask = g_pGame->GetTasks()->CreateTaskComplexInWater();
    if (!inWaterTask)
        return;

    // Set physical flags (bTouchingWater, bSubmergedInWater)
    m_pPlayerPed->SetInWaterFlags(true);

    inWaterTask->SetAsPedTask(m_pPlayerPed, TASK_PRIORITY_EVENT_RESPONSE_NONTEMP, true);
}
