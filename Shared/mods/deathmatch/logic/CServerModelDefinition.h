/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/CServerModelDefinition.h
 *  PURPOSE:     Shared server model registry definition
 *
 *****************************************************************************/

#pragma once

#include <cstdint>
#include <string>

// FileID 42340 is the final native slot in the compact extended layout.
// Logical server model IDs must live above that complete public namespace so
// clothes aliases, model slots, TXD/COL/IPL IDs, and server models can never
// reinterpret the same uint16 value differently at an API boundary.
constexpr std::uint16_t SERVER_MODEL_ID_MIN = 42341;
constexpr std::uint16_t SERVER_MODEL_ID_MAX = 65534;

enum class eServerModelType : std::uint8_t
{
    OBJECT,
    VEHICLE,
    PED,
};

struct SServerModelDefinition
{
    std::uint16_t    logicalModelId{};
    std::uint16_t    parentModelId{};
    eServerModelType type{eServerModelType::OBJECT};
    std::string      name;
};
