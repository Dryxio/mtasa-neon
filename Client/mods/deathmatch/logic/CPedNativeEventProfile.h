/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/CPedNativeEventProfile.h
 *  PURPOSE:     Resource-scoped native pedestrian event profiles
 *
 *****************************************************************************/

#pragma once

#include <cstdint>

enum class ePedNativeEventProfile : std::uint8_t
{
    NONE,
    MISSION,
    AMBIENT_WANDER,
    AMBIENT_COP_SAFE,
};
