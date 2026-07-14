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

constexpr std::uint16_t SERVER_MODEL_ID_MIN = 30000;

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
