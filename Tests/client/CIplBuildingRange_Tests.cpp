/*****************************************************************************/
/*
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Tests/client/CIplBuildingRange_Tests.cpp
 *  PURPOSE:     Google Test suite for GTA IPL building range clamping
 *
 *****************************************************************************/

#include <game/CIplBuildingRange.h>
#include <gtest/gtest.h>

TEST(CIplBuildingRange, PreservesRangeWithinTargetPool)
{
    const auto range = ClampIplBuildingRange({100, 200}, 13000);
    EXPECT_EQ(range.min, 100);
    EXPECT_EQ(range.max, 200);
}

TEST(CIplBuildingRange, TruncatesRangeCrossingTargetPoolEnd)
{
    const auto range = ClampIplBuildingRange({12990, 13019}, 13000);
    EXPECT_EQ(range.min, 12990);
    EXPECT_EQ(range.max, 12999);
}

TEST(CIplBuildingRange, EmptiesRangeStartingPastTargetPoolEnd)
{
    const auto range = ClampIplBuildingRange({13019, 13040}, 13000);
    EXPECT_EQ(range.min, std::numeric_limits<std::int16_t>::max());
    EXPECT_EQ(range.max, std::numeric_limits<std::int16_t>::min());
}

TEST(CIplBuildingRange, CanonicalizesNegativeNativeRange)
{
    const auto range = ClampIplBuildingRange({-1, -1}, 13000);
    EXPECT_EQ(range.min, std::numeric_limits<std::int16_t>::max());
    EXPECT_EQ(range.max, std::numeric_limits<std::int16_t>::min());
}

TEST(CIplBuildingRange, CapsRepresentableIndexAtInt16Max)
{
    const auto range = ClampIplBuildingRange({32000, std::numeric_limits<std::int16_t>::max()}, 50000);
    EXPECT_EQ(range.min, 32000);
    EXPECT_EQ(range.max, std::numeric_limits<std::int16_t>::max());
}
