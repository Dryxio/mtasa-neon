/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        sdk/game/TaskGoTo.h
 *  PURPOSE:     Go to task interface
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include "Task.h"

class CNodeAddress
{
public:
    short sRegion;
    short sIndex;

    CNodeAddress() { sRegion = -1; }
};

enum
{
    WANDER_TYPE_STANDARD = 0,
    WANDER_TYPE_COP,
    WANDER_TYPE_MEDIC,
    WANDER_TYPE_CRIMINAL,
    WANDER_TYPE_GANG,
    WANDER_TYPE_SHOP,
    WANDER_TYPE_FLEE,
    WANDER_TYPE_PROSTITUTE
};

#define NO_WANDER_TYPE 9999

// Common movement command stored by GTA's active point-navigation subtasks.
// Reading this task is important for script commands because GTA installs a
// clone through CEventScriptCommand and may no longer expose the constructor's
// original wrapper as the active parent.
class CTaskSimpleGoTo : public virtual CTaskSimple
{
public:
    virtual ~CTaskSimpleGoTo() {};

    virtual int GetMoveState() const = 0;
};

class CTaskComplexWander : public virtual CTaskComplex
{
public:
    virtual ~CTaskComplexWander() {};

    virtual CNodeAddress* GetNextNode() = 0;
    virtual CNodeAddress* GetLastNode() = 0;

    virtual int GetWanderType() = 0;

    // The live ped move state can be rewritten while GTA processes or blends
    // a subtask. Observer presentation needs the durable command stored by the
    // Wander parent so a requested walk is never serialized as a run.
    virtual int GetMoveState() const = 0;
};

class CTaskComplexWanderStandard : public virtual CTaskComplexWander
{
public:
    virtual ~CTaskComplexWanderStandard() {};
};

// Moves a ped to a world-space point and leaves it standing at the destination.
// This public interface deliberately hides the GTA-specific timed/base variants so
// callers can use one durable task contract while game_sa selects the native class.
class CTaskComplexGoToPointAndStandStill : public virtual CTaskComplex
{
public:
    virtual ~CTaskComplexGoToPointAndStandStill() {};

    // The live ped move state can temporarily differ from the command while
    // GTA blends or slows the actor. Presentation sync needs the command's
    // intent so remote clients select the same locomotion animation.
    virtual int GetMoveState() const = 0;
};

class CTaskComplexSeekEntityRadiusAngleOffset : public virtual CTaskComplex
{
public:
    virtual ~CTaskComplexSeekEntityRadiusAngleOffset() {};
};
