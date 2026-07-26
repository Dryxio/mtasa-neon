/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        sdk/WorldLimits.h
 *  PURPOSE:     Shared extended-world coordinate contract
 *
 *****************************************************************************/

#pragma once

constexpr float EXTENDED_WORLD_MIN_COORD = -10000.0f;
constexpr float EXTENDED_WORLD_MAX_COORD = 10000.0f;
// GTA sector selection uses the positive endpoint as an exclusive boundary.
constexpr float EXTENDED_WORLD_MAX_ENTITY_COORD = EXTENDED_WORLD_MAX_COORD - 1.0f;
