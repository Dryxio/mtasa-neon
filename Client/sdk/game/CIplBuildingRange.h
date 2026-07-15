/*****************************************************************************/
/*
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game/CIplBuildingRange.h
 *  PURPOSE:     GTA IPL building range helpers
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

struct SIplBuildingRange
{
    std::int16_t min;
    std::int16_t max;
};

constexpr SIplBuildingRange ClampIplBuildingRange(SIplBuildingRange range, std::size_t buildingPoolSize)
{
    constexpr SIplBuildingRange emptyRange{std::numeric_limits<std::int16_t>::max(), std::numeric_limits<std::int16_t>::min()};

    // GTA sign-extends both endpoints and loops while min <= max. Canonicalize
    // negative or malformed metadata to max < min so native removal stays empty.
    if (buildingPoolSize == 0 || range.min < 0 || range.max < range.min)
        return emptyRange;

    const std::size_t lastPoolIndex = std::min(buildingPoolSize - 1, static_cast<std::size_t>(std::numeric_limits<std::int16_t>::max()));
    if (static_cast<std::size_t>(range.min) > lastPoolIndex)
        return emptyRange;

    if (static_cast<std::size_t>(range.max) > lastPoolIndex)
        range.max = static_cast<std::int16_t>(lastPoolIndex);

    return range;
}
