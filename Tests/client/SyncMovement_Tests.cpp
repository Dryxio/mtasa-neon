/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Tests/client/SyncMovement_Tests.cpp
 *  PURPOSE:     Round-trip tests for position, rotation, and velocity
 *               sync structures
 *
 *  Position syncs have two modes: float (raw 32-bit) and quantized
 *  (SFloatSync<15,10> for X/Y, raw float for Z). Rotation syncs
 *  quantize angles into 16-bit ranges. Velocity uses a flag bit to skip
 *  encoding when the vector is zero, and NormVector encoding for direction.
 *
 *****************************************************************************/

#include <gtest/gtest.h>
#include <limits>
#include "MockBitStream.h"
#include <net/SyncStructures.h>

// ============================================================================
// Native task locomotion presentation
// ============================================================================

TEST(SNativeTaskLocomotionSync, RoundTripsEveryMode)
{
    for (unsigned int mode = SNativeTaskLocomotionSync::NONE; mode <= SNativeTaskLocomotionSync::SPRINT; ++mode)
    {
        MockBitStream             bitStream;
        SNativeTaskLocomotionSync source;
        SNativeTaskLocomotionSync decoded;
        source.data.uiMode = mode;
        source.data.sLeftStickX = -64;
        source.data.sLeftStickY = 96;

        bitStream.Write(&source);
        const int expectedBits = SNativeTaskLocomotionSync::BITCOUNT + (mode == SNativeTaskLocomotionSync::NONE ? 0 : 16);
        EXPECT_EQ(expectedBits, bitStream.GetNumberOfBitsUsed());

        bitStream.ResetReadPointer();
        ASSERT_TRUE(bitStream.Read(&decoded));
        EXPECT_EQ(mode, decoded.data.uiMode);
        if (mode == SNativeTaskLocomotionSync::NONE)
        {
            EXPECT_EQ(0, decoded.data.sLeftStickX);
            EXPECT_EQ(0, decoded.data.sLeftStickY);
        }
        else
        {
            EXPECT_NEAR(source.data.sLeftStickX, decoded.data.sLeftStickX, 1);
            EXPECT_NEAR(source.data.sLeftStickY, decoded.data.sLeftStickY, 1);
        }
    }
}

TEST(SNativeTaskLocomotionSync, VersionGatePreservesFollowingFields)
{
    constexpr unsigned char sentinel = 0xA5;

    for (const auto version : {eBitStreamVersion::NativeWorldStaticWorldV3StartupAuthorization, eBitStreamVersion::Latest})
    {
        MockBitStream             bitStream(static_cast<unsigned short>(version));
        SNativeTaskLocomotionSync source;
        source.data.uiMode = SNativeTaskLocomotionSync::RUN;
        source.data.sLeftStickX = -128;
        source.data.sLeftStickY = 64;

        if (bitStream.Can(eBitStreamVersion::NativeTaskLocomotionPresentation))
            bitStream.Write(&source);
        bitStream.Write(sentinel);

        bitStream.ResetReadPointer();
        SNativeTaskLocomotionSync decoded;
        if (bitStream.Can(eBitStreamVersion::NativeTaskLocomotionPresentation))
            ASSERT_TRUE(bitStream.Read(&decoded));

        unsigned char decodedSentinel = 0;
        ASSERT_TRUE(bitStream.Read(decodedSentinel));
        EXPECT_EQ(sentinel, decodedSentinel);
        EXPECT_EQ(version == eBitStreamVersion::Latest ? SNativeTaskLocomotionSync::RUN : SNativeTaskLocomotionSync::NONE, decoded.data.uiMode);
    }
}

TEST(SNativeTaskWeaponPresentationSync, RoundTrip)
{
    MockBitStream                     bitStream(static_cast<unsigned short>(eBitStreamVersion::Latest));
    SNativeTaskWeaponPresentationSync source;
    source.data.uiMode = SNativeTaskWeaponPresentationSync::FIRE;
    source.data.ucWeaponType = 41;
    source.data.usBurstLength = 5;
    source.data.ucShootingRate = 100;
    source.data.vecTarget = CVector(2100.25f, -1649.5f, 14.0f);
    bitStream.Write(&source);

    bitStream.ResetReadPointer();
    SNativeTaskWeaponPresentationSync decoded;
    ASSERT_TRUE(bitStream.Read(&decoded));
    EXPECT_EQ(SNativeTaskWeaponPresentationSync::FIRE, decoded.data.uiMode);
    EXPECT_EQ(41, decoded.data.ucWeaponType);
    EXPECT_EQ(5, decoded.data.usBurstLength);
    EXPECT_EQ(100, decoded.data.ucShootingRate);
    EXPECT_FLOAT_EQ(source.data.vecTarget.fX, decoded.data.vecTarget.fX);
    EXPECT_FLOAT_EQ(source.data.vecTarget.fY, decoded.data.vecTarget.fY);
    EXPECT_FLOAT_EQ(source.data.vecTarget.fZ, decoded.data.vecTarget.fZ);
}

TEST(SNativeTaskWeaponPresentationSync, RejectsInvalidPayloads)
{
    for (const unsigned int invalidMode : {2U, 3U})
    {
        MockBitStream bitStream;
        bitStream.WriteBits(reinterpret_cast<const char*>(&invalidMode), SNativeTaskWeaponPresentationSync::BITCOUNT);
        bitStream.ResetReadPointer();

        SNativeTaskWeaponPresentationSync decoded;
        EXPECT_FALSE(bitStream.Read(&decoded));
    }

    for (const unsigned short invalidBurst : {0U, 32768U})
    {
        MockBitStream bitStream;
        unsigned int  fireMode = SNativeTaskWeaponPresentationSync::FIRE;
        bitStream.WriteBits(reinterpret_cast<const char*>(&fireMode), SNativeTaskWeaponPresentationSync::BITCOUNT);
        bitStream.Write(static_cast<unsigned char>(41));
        bitStream.Write(invalidBurst);
        bitStream.Write(static_cast<unsigned char>(100));
        bitStream.Write(1.0f);
        bitStream.Write(2.0f);
        bitStream.Write(3.0f);
        bitStream.ResetReadPointer();

        SNativeTaskWeaponPresentationSync decoded;
        EXPECT_FALSE(bitStream.Read(&decoded));
    }

    MockBitStream bitStream;
    unsigned int  fireMode = SNativeTaskWeaponPresentationSync::FIRE;
    bitStream.WriteBits(reinterpret_cast<const char*>(&fireMode), SNativeTaskWeaponPresentationSync::BITCOUNT);
    bitStream.Write(static_cast<unsigned char>(41));
    bitStream.Write(static_cast<unsigned short>(5));
    bitStream.Write(static_cast<unsigned char>(100));
    bitStream.Write(std::numeric_limits<float>::quiet_NaN());
    bitStream.Write(2.0f);
    bitStream.Write(3.0f);
    bitStream.ResetReadPointer();

    SNativeTaskWeaponPresentationSync decoded;
    EXPECT_FALSE(bitStream.Read(&decoded));
}

TEST(SNativeTaskWeaponPresentationSync, NonePreservesFollowingFields)
{
    constexpr unsigned char           sentinel = 0xC3;
    MockBitStream                     bitStream;
    SNativeTaskWeaponPresentationSync source;
    bitStream.Write(&source);
    bitStream.Write(sentinel);

    bitStream.ResetReadPointer();
    SNativeTaskWeaponPresentationSync decoded;
    ASSERT_TRUE(bitStream.Read(&decoded));
    EXPECT_EQ(SNativeTaskWeaponPresentationSync::NONE, decoded.data.uiMode);

    unsigned char decodedSentinel = 0;
    ASSERT_TRUE(bitStream.Read(decodedSentinel));
    EXPECT_EQ(sentinel, decodedSentinel);
}

TEST(SNativeTaskWeaponPresentationSync, VersionGatePreservesFollowingFields)
{
    constexpr unsigned char sentinel = 0x5A;
    for (const auto version : {eBitStreamVersion::NativeTaskLocomotionPresentation, eBitStreamVersion::Latest})
    {
        MockBitStream                     bitStream(static_cast<unsigned short>(version));
        SNativeTaskWeaponPresentationSync source;
        source.data.uiMode = SNativeTaskWeaponPresentationSync::FIRE;
        source.data.ucWeaponType = 41;
        source.data.usBurstLength = 5;
        source.data.ucShootingRate = 100;
        source.data.vecTarget = CVector(1.0f, 2.0f, 3.0f);

        if (bitStream.Can(eBitStreamVersion::NativeTaskWeaponPresentation))
            bitStream.Write(&source);
        bitStream.Write(sentinel);

        bitStream.ResetReadPointer();
        SNativeTaskWeaponPresentationSync decoded;
        if (bitStream.Can(eBitStreamVersion::NativeTaskWeaponPresentation))
            ASSERT_TRUE(bitStream.Read(&decoded));

        unsigned char decodedSentinel = 0;
        ASSERT_TRUE(bitStream.Read(decodedSentinel));
        EXPECT_EQ(sentinel, decodedSentinel);
        EXPECT_EQ(version == eBitStreamVersion::Latest ? SNativeTaskWeaponPresentationSync::FIRE : SNativeTaskWeaponPresentationSync::NONE,
                  decoded.data.uiMode);
    }
}

TEST(SNativeTaskAnimationPresentationSync, RoundTrip)
{
    MockBitStream                        bitStream(static_cast<unsigned short>(eBitStreamVersion::Latest));
    SNativeTaskAnimationPresentationSync source;
    source.data.uiMode = SNativeTaskAnimationPresentationSync::ANIMATION;
    source.data.usAnimGroup = 4;
    source.data.usAnimId = 18;
    source.data.fProgress = 0.375f;
    source.data.fSpeed = 1.0f;
    source.data.fBlendAmount = 0.8f;
    bitStream.Write(&source);

    bitStream.ResetReadPointer();
    SNativeTaskAnimationPresentationSync decoded;
    ASSERT_TRUE(bitStream.Read(&decoded));
    EXPECT_EQ(SNativeTaskAnimationPresentationSync::ANIMATION, decoded.data.uiMode);
    EXPECT_EQ(source.data.usAnimGroup, decoded.data.usAnimGroup);
    EXPECT_EQ(source.data.usAnimId, decoded.data.usAnimId);
    EXPECT_FLOAT_EQ(source.data.fProgress, decoded.data.fProgress);
    EXPECT_FLOAT_EQ(source.data.fSpeed, decoded.data.fSpeed);
    EXPECT_FLOAT_EQ(source.data.fBlendAmount, decoded.data.fBlendAmount);
}

TEST(SNativeTaskAnimationPresentationSync, RejectsInvalidPayloads)
{
    for (const auto invalidProgress : {-0.01f, 1.01f, std::numeric_limits<float>::quiet_NaN()})
    {
        MockBitStream bitStream;
        unsigned int  mode = SNativeTaskAnimationPresentationSync::ANIMATION;
        bitStream.WriteBits(reinterpret_cast<const char*>(&mode), SNativeTaskAnimationPresentationSync::BITCOUNT);
        bitStream.Write(static_cast<unsigned short>(4));
        bitStream.Write(static_cast<unsigned short>(18));
        bitStream.Write(invalidProgress);
        bitStream.Write(1.0f);
        bitStream.Write(1.0f);
        bitStream.ResetReadPointer();

        SNativeTaskAnimationPresentationSync decoded;
        EXPECT_FALSE(bitStream.Read(&decoded));
    }

    for (const auto invalidBlend : {-0.01f, 1.01f, std::numeric_limits<float>::quiet_NaN()})
    {
        MockBitStream bitStream;
        unsigned int  mode = SNativeTaskAnimationPresentationSync::ANIMATION;
        bitStream.WriteBits(reinterpret_cast<const char*>(&mode), SNativeTaskAnimationPresentationSync::BITCOUNT);
        bitStream.Write(static_cast<unsigned short>(4));
        bitStream.Write(static_cast<unsigned short>(18));
        bitStream.Write(0.5f);
        bitStream.Write(1.0f);
        bitStream.Write(invalidBlend);
        bitStream.ResetReadPointer();

        SNativeTaskAnimationPresentationSync decoded;
        EXPECT_FALSE(bitStream.Read(&decoded));
    }
}

TEST(SNativeTaskAnimationPresentationSync, NonePreservesFollowingFields)
{
    constexpr unsigned char              sentinel = 0x7B;
    MockBitStream                        bitStream;
    SNativeTaskAnimationPresentationSync source;
    bitStream.Write(&source);
    bitStream.Write(sentinel);

    bitStream.ResetReadPointer();
    SNativeTaskAnimationPresentationSync decoded;
    ASSERT_TRUE(bitStream.Read(&decoded));
    EXPECT_EQ(SNativeTaskAnimationPresentationSync::NONE, decoded.data.uiMode);

    unsigned char decodedSentinel = 0;
    ASSERT_TRUE(bitStream.Read(decodedSentinel));
    EXPECT_EQ(sentinel, decodedSentinel);
}

TEST(SNativeTaskAnimationPresentationSync, VersionGatePreservesFollowingFields)
{
    constexpr unsigned char sentinel = 0x91;
    for (const auto version : {eBitStreamVersion::NativeTaskLocomotionPresentation, eBitStreamVersion::Latest})
    {
        MockBitStream                        bitStream(static_cast<unsigned short>(version));
        SNativeTaskAnimationPresentationSync source;
        source.data.uiMode = SNativeTaskAnimationPresentationSync::ANIMATION;
        source.data.usAnimGroup = 4;
        source.data.usAnimId = 18;
        source.data.fProgress = 0.5f;
        source.data.fSpeed = 1.0f;
        source.data.fBlendAmount = 1.0f;

        if (bitStream.Can(eBitStreamVersion::NativeTaskAnimationPresentation))
            bitStream.Write(&source);
        bitStream.Write(sentinel);

        bitStream.ResetReadPointer();
        SNativeTaskAnimationPresentationSync decoded;
        if (bitStream.Can(eBitStreamVersion::NativeTaskAnimationPresentation))
            ASSERT_TRUE(bitStream.Read(&decoded));

        unsigned char decodedSentinel = 0;
        ASSERT_TRUE(bitStream.Read(decodedSentinel));
        EXPECT_EQ(sentinel, decodedSentinel);
        EXPECT_EQ(version == eBitStreamVersion::Latest ? SNativeTaskAnimationPresentationSync::ANIMATION : SNativeTaskAnimationPresentationSync::NONE,
                  decoded.data.uiMode);
    }
}

// ============================================================================
// Position syncs
// ============================================================================

// Float mode writes all three components as raw 32-bit floats - no precision loss.
TEST(SPositionSync, RoundTrip_Floats)
{
    MockBitStream bs;
    SPositionSync sync(true);
    sync.data.vecPosition.fX = 100.5f;
    sync.data.vecPosition.fY = -200.25f;
    sync.data.vecPosition.fZ = 50.0f;
    sync.Write(bs);
    bs.ResetReadPointer();
    SPositionSync out(true);
    EXPECT_TRUE(out.Read(bs));
    EXPECT_FLOAT_EQ(100.5f, out.data.vecPosition.fX);
    EXPECT_FLOAT_EQ(-200.25f, out.data.vecPosition.fY);
    EXPECT_FLOAT_EQ(50.0f, out.data.vecPosition.fZ);
}

// Quantized mode: X and Y use SFloatSync<15,10> (precision ~0.001),
// Z is still a raw float (vertical position needs full precision for
// building interiors, etc).
TEST(SPositionSync, RoundTrip_Quantized)
{
    MockBitStream bs;
    SPositionSync sync(false);
    sync.data.vecPosition.fX = 500.0f;
    sync.data.vecPosition.fY = -1234.0f;
    sync.data.vecPosition.fZ = 42.0f;
    sync.Write(bs);
    bs.ResetReadPointer();
    SPositionSync out(false);
    EXPECT_TRUE(out.Read(bs));
    EXPECT_NEAR(500.0f, out.data.vecPosition.fX, 0.01f);
    EXPECT_NEAR(-1234.0f, out.data.vecPosition.fY, 0.01f);
    EXPECT_FLOAT_EQ(42.0f, out.data.vecPosition.fZ);
}

TEST(SPositionSync, RoundTrip_ExtendedWorldBoundary)
{
    MockBitStream bs;
    SPositionSync sync(false);
    sync.data.vecPosition.fX = 9500.0f;
    sync.data.vecPosition.fY = -9500.0f;
    sync.data.vecPosition.fZ = 42.0f;
    sync.Write(bs);
    EXPECT_EQ(2 * (POSITION_SYNC_INTEGER_BITS + POSITION_SYNC_FRACTIONAL_BITS) + 32, bs.GetNumberOfBitsUsed());
    bs.ResetReadPointer();
    SPositionSync out(false);
    EXPECT_TRUE(out.Read(bs));
    EXPECT_NEAR(9500.0f, out.data.vecPosition.fX, 0.01f);
    EXPECT_NEAR(-9500.0f, out.data.vecPosition.fY, 0.01f);
}

TEST(SPositionSync, LegacyVersionPreservesOldWireLimit)
{
    MockBitStream bs(static_cast<unsigned short>(eBitStreamVersion::Unk));
    SPositionSync sync(false);
    sync.data.vecPosition.fX = 9500.0f;
    sync.data.vecPosition.fY = -9500.0f;
    sync.data.vecPosition.fZ = 42.0f;
    sync.Write(bs);
    EXPECT_EQ(2 * (14 + POSITION_SYNC_FRACTIONAL_BITS) + 32, bs.GetNumberOfBitsUsed());
    bs.ResetReadPointer();
    SPositionSync out(false);
    EXPECT_TRUE(out.Read(bs));
    EXPECT_NEAR(8191.0f, out.data.vecPosition.fX, 0.01f);
    EXPECT_NEAR(-8192.0f, out.data.vecPosition.fY, 0.01f);
}

// 2D position (float): only X and Y, used for map blips and radar positions.
TEST(SPosition2DSync, RoundTrip_Floats)
{
    MockBitStream   bs;
    SPosition2DSync sync(true);
    sync.data.vecPosition.fX = 300.0f;
    sync.data.vecPosition.fY = -150.5f;
    sync.Write(bs);
    bs.ResetReadPointer();
    SPosition2DSync out(true);
    EXPECT_TRUE(out.Read(bs));
    EXPECT_FLOAT_EQ(300.0f, out.data.vecPosition.fX);
    EXPECT_FLOAT_EQ(-150.5f, out.data.vecPosition.fY);
}

// 2D position (quantized): uses SFloatSync<14,10> for both components.
TEST(SPosition2DSync, RoundTrip_Quantized)
{
    MockBitStream   bs;
    SPosition2DSync sync(false);
    sync.data.vecPosition.fX = 2000.0f;
    sync.data.vecPosition.fY = -3000.0f;
    sync.Write(bs);
    bs.ResetReadPointer();
    SPosition2DSync out(false);
    EXPECT_TRUE(out.Read(bs));
    EXPECT_NEAR(2000.0f, out.data.vecPosition.fX, 0.01f);
    EXPECT_NEAR(-3000.0f, out.data.vecPosition.fY, 0.01f);
}

TEST(SPosition2DSync, RoundTrip_ExtendedWorldBoundary)
{
    MockBitStream   bs;
    SPosition2DSync sync(false);
    sync.data.vecPosition.fX = 9500.0f;
    sync.data.vecPosition.fY = -9500.0f;
    sync.Write(bs);
    bs.ResetReadPointer();
    SPosition2DSync out(false);
    EXPECT_TRUE(out.Read(bs));
    EXPECT_NEAR(9500.0f, out.data.vecPosition.fX, 0.01f);
    EXPECT_NEAR(-9500.0f, out.data.vecPosition.fY, 0.01f);
}

TEST(SPosition2DSync, LegacyVersionPreservesOldWireWidth)
{
    MockBitStream   bs(static_cast<unsigned short>(eBitStreamVersion::Unk));
    SPosition2DSync sync(false);
    sync.data.vecPosition.fX = 2392.977f;
    sync.data.vecPosition.fY = -1467.968f;
    sync.Write(bs);
    EXPECT_EQ(2 * (14 + POSITION_SYNC_FRACTIONAL_BITS), bs.GetNumberOfBitsUsed());
    bs.ResetReadPointer();
    SPosition2DSync out(false);
    EXPECT_TRUE(out.Read(bs));
    EXPECT_NEAR(sync.data.vecPosition.fX, out.data.vecPosition.fX, 0.01f);
    EXPECT_NEAR(sync.data.vecPosition.fY, out.data.vecPosition.fY, 0.01f);
}

// These tests protect the destination-version wire contract and document why
// pre-serializing into a version-zero stream is invalid. The server packet
// callsites are covered end-to-end by world-sync-regression-test.
TEST(VersionedPositionWireInvariant, ColPolygonPointAndIndexUseDestinationVersion)
{
    constexpr unsigned int pointIndex = 3;

    for (const auto version : {eBitStreamVersion::Unk, eBitStreamVersion::Latest})
    {
        MockBitStream   bs(static_cast<unsigned short>(version));
        SPosition2DSync position(false);
        position.data.vecPosition.fX = 2392.977f;
        position.data.vecPosition.fY = -1467.968f;
        bs.Write(&position);
        bs.Write(pointIndex);

        const int componentBits = version == eBitStreamVersion::Unk ? 14 : POSITION_SYNC_INTEGER_BITS;
        EXPECT_EQ(2 * (componentBits + POSITION_SYNC_FRACTIONAL_BITS) + 32, bs.GetNumberOfBitsUsed());

        bs.ResetReadPointer();
        SPosition2DSync decoded(false);
        unsigned int    decodedIndex = 0;
        ASSERT_TRUE(bs.Read(&decoded));
        ASSERT_TRUE(bs.Read(decodedIndex));
        EXPECT_NEAR(position.data.vecPosition.fX, decoded.data.vecPosition.fX, 0.01f);
        EXPECT_NEAR(position.data.vecPosition.fY, decoded.data.vecPosition.fY, 0.01f);
        EXPECT_EQ(pointIndex, decodedIndex);
    }
}

TEST(VersionedPositionWireInvariant, ColPolygonPointWithoutIndexEndsAfterPosition)
{
    for (const auto version : {eBitStreamVersion::Unk, eBitStreamVersion::Latest})
    {
        MockBitStream   bs(static_cast<unsigned short>(version));
        SPosition2DSync position(false);
        position.data.vecPosition.fX = 2392.977f;
        position.data.vecPosition.fY = -1467.968f;
        bs.Write(&position);

        bs.ResetReadPointer();
        SPosition2DSync decoded(false);
        unsigned int    unexpectedIndex = 0;
        ASSERT_TRUE(bs.Read(&decoded));
        EXPECT_FALSE(bs.Read(unexpectedIndex));
    }
}

TEST(VersionedPositionWireInvariant, ObjectPositionPairUsesDestinationVersion)
{
    constexpr unsigned char sentinel = 0xA5;

    for (const auto version : {eBitStreamVersion::Unk, eBitStreamVersion::Latest})
    {
        MockBitStream bs(static_cast<unsigned short>(version));
        SPositionSync source(false);
        source.data.vecPosition = CVector(2392.977f, -1467.968f, 12.5f);
        SPositionSync target(false);
        target.data.vecPosition = CVector(2412.977f, -1467.968f, 12.5f);
        bs.Write(&source);
        bs.Write(&target);
        bs.Write(sentinel);

        const int componentBits = version == eBitStreamVersion::Unk ? 14 : POSITION_SYNC_INTEGER_BITS;
        EXPECT_EQ(2 * (2 * (componentBits + POSITION_SYNC_FRACTIONAL_BITS) + 32) + 8, bs.GetNumberOfBitsUsed());

        bs.ResetReadPointer();
        SPositionSync decodedSource(false);
        SPositionSync decodedTarget(false);
        unsigned char decodedSentinel = 0;
        ASSERT_TRUE(bs.Read(&decodedSource));
        ASSERT_TRUE(bs.Read(&decodedTarget));
        ASSERT_TRUE(bs.Read(decodedSentinel));
        EXPECT_NEAR(source.data.vecPosition.fX, decodedSource.data.vecPosition.fX, 0.01f);
        EXPECT_NEAR(target.data.vecPosition.fX, decodedTarget.data.vecPosition.fX, 0.01f);
        EXPECT_EQ(sentinel, decodedSentinel);
    }
}

TEST(VersionedPositionWireInvariant, ColPolygonRawCopyFromLegacyIntoLatestCannotPreserveTrailingFields)
{
    MockBitStream   legacy(static_cast<unsigned short>(eBitStreamVersion::Unk));
    SPosition2DSync position(false);
    position.data.vecPosition.fX = 2392.977f;
    position.data.vecPosition.fY = -1467.968f;
    legacy.Write(&position);
    legacy.Write(static_cast<unsigned int>(3));

    MockBitStream latest(static_cast<unsigned short>(eBitStreamVersion::Latest));
    latest.WriteBits(reinterpret_cast<const char*>(legacy.GetData()), legacy.GetNumberOfBitsUsed());
    latest.ResetReadPointer();

    SPosition2DSync decoded(false);
    unsigned int    decodedIndex = 0;
    ASSERT_TRUE(latest.Read(&decoded));
    EXPECT_FALSE(latest.Read(decodedIndex));
}

TEST(VersionedPositionWireInvariant, ObjectRawCopyFromLegacyIntoLatestCannotPreserveTrailingFields)
{
    constexpr unsigned char sentinel = 0xA5;

    MockBitStream legacy(static_cast<unsigned short>(eBitStreamVersion::Unk));
    SPositionSync source(false);
    source.data.vecPosition = CVector(2392.977f, -1467.968f, 12.5f);
    SPositionSync target(false);
    target.data.vecPosition = CVector(2412.977f, -1467.968f, 12.5f);
    legacy.Write(&source);
    legacy.Write(&target);
    legacy.Write(sentinel);

    MockBitStream latest(static_cast<unsigned short>(eBitStreamVersion::Latest));
    latest.WriteBits(reinterpret_cast<const char*>(legacy.GetData()), legacy.GetNumberOfBitsUsed());
    constexpr int latestPacketBits = 2 * (2 * (POSITION_SYNC_INTEGER_BITS + POSITION_SYNC_FRACTIONAL_BITS) + 32) + 8;
    EXPECT_LT(latest.GetNumberOfBitsUsed(), latestPacketBits);
    latest.ResetReadPointer();

    SPositionSync decodedSource(false);
    SPositionSync decodedTarget(false);
    unsigned char decodedSentinel = 0;
    const bool    decodedCompletePacket = latest.Read(&decodedSource) && latest.Read(&decodedTarget) && latest.Read(decodedSentinel);
    EXPECT_FALSE(decodedCompletePacket);
}

// The previous DM netcode epoch is rejected before sync. Its last effective
// pre-epoch capability still models the exact 43-bit legacy layout: 16-bit X/Y
// and the historical 11-bit -110..1938 Z interval.
TEST(SLowPrecisionPositionSync, LegacyWireWidthAndZLimit)
{
    MockBitStream             bs(static_cast<unsigned short>(eBitStreamVersion::NativeTaskLocomotionPresentation));
    SLowPrecisionPositionSync sync;
    sync.data.vecPosition.fX = 9500.0f;
    sync.data.vecPosition.fY = -9500.0f;
    sync.data.vecPosition.fZ = 9500.0f;
    sync.Write(bs);
    EXPECT_EQ(43, bs.GetNumberOfBitsUsed());
    bs.ResetReadPointer();
    SLowPrecisionPositionSync out;
    EXPECT_TRUE(out.Read(bs));
    EXPECT_NEAR(9500.0f, out.data.vecPosition.fX, 0.4f);
    EXPECT_NEAR(-9500.0f, out.data.vecPosition.fY, 0.4f);
    EXPECT_NEAR(1938.0f, out.data.vecPosition.fZ, 1.1f);
}

// Peers in the exact new DM netcode epoch use 16 bits for every component, so
// low-precision Z covers the complete extended world with the same ~0.31-unit
// error as X/Y while staying within the net module's 0x3D effective cap.
TEST(SLowPrecisionPositionSync, ExtendedWireWidthAndWorldBoundary)
{
    MockBitStream             bs(static_cast<unsigned short>(eBitStreamVersion::ExtendedWorldLowPrecisionZ));
    SLowPrecisionPositionSync sync;
    sync.data.vecPosition.fX = 9500.0f;
    sync.data.vecPosition.fY = -9500.0f;
    sync.data.vecPosition.fZ = -9500.0f;
    sync.Write(bs);
    EXPECT_EQ(48, bs.GetNumberOfBitsUsed());
    bs.ResetReadPointer();
    SLowPrecisionPositionSync out;
    EXPECT_TRUE(out.Read(bs));
    EXPECT_NEAR(9500.0f, out.data.vecPosition.fX, 0.4f);
    EXPECT_NEAR(-9500.0f, out.data.vecPosition.fY, 0.4f);
    EXPECT_NEAR(-9500.0f, out.data.vecPosition.fZ, 0.4f);
}

// ============================================================================
// Rotation syncs
// ============================================================================

// Degrees (quantized): each axis uses 16-bit over [0, 360). Step ~0.0055 degrees.
TEST(SRotationDegreesSync, RoundTrip_Quantized)
{
    MockBitStream        bs;
    SRotationDegreesSync sync(false);
    sync.data.vecRotation.fX = 90.0f;
    sync.data.vecRotation.fY = 180.0f;
    sync.data.vecRotation.fZ = 270.0f;
    sync.Write(bs);
    bs.ResetReadPointer();
    SRotationDegreesSync out(false);
    EXPECT_TRUE(out.Read(bs));
    EXPECT_NEAR(90.0f, out.data.vecRotation.fX, 0.01f);
    EXPECT_NEAR(180.0f, out.data.vecRotation.fY, 0.01f);
    EXPECT_NEAR(270.0f, out.data.vecRotation.fZ, 0.01f);
}

// Negative rotation: SRotationDegreesSync casts to unsigned short, so
// negative inputs wrap around. For example, -10 degrees → unsigned short
// wraps to a large value, which reads back as ~360 + (-10 * 65536/360)
// mapped to [0, 360). This is the expected behavior for the sync struct.
TEST(SRotationDegreesSync, RoundTrip_NegativeInput)
{
    MockBitStream        bs;
    SRotationDegreesSync sync(false);
    sync.data.vecRotation.fX = -10.0f;
    sync.data.vecRotation.fY = -90.0f;
    sync.data.vecRotation.fZ = -180.0f;
    sync.Write(bs);
    bs.ResetReadPointer();
    SRotationDegreesSync out(false);
    EXPECT_TRUE(out.Read(bs));
    // Negative values wrap into [0, 360) range due to unsigned short cast.
    // -10° → cast to unsigned short → wraps → reads back as ~350°
    EXPECT_NEAR(350.0f, out.data.vecRotation.fX, 0.01f);
    EXPECT_NEAR(270.0f, out.data.vecRotation.fY, 0.01f);
    EXPECT_NEAR(180.0f, out.data.vecRotation.fZ, 0.01f);
}

// Degrees (float): raw 32-bit floats, no precision loss.
TEST(SRotationDegreesSync, RoundTrip_Floats)
{
    MockBitStream        bs;
    SRotationDegreesSync sync(true);
    sync.data.vecRotation.fX = 45.5f;
    sync.data.vecRotation.fY = 123.456f;
    sync.data.vecRotation.fZ = 359.0f;
    sync.Write(bs);
    bs.ResetReadPointer();
    SRotationDegreesSync out(true);
    EXPECT_TRUE(out.Read(bs));
    EXPECT_FLOAT_EQ(45.5f, out.data.vecRotation.fX);
    EXPECT_FLOAT_EQ(123.456f, out.data.vecRotation.fY);
    EXPECT_FLOAT_EQ(359.0f, out.data.vecRotation.fZ);
}

// Radians (quantized): each axis uses 16-bit over [0, 2*PI). Step ~0.0001 radians.
TEST(SRotationRadiansSync, RoundTrip_Quantized)
{
    MockBitStream        bs;
    SRotationRadiansSync sync(false);
    sync.data.vecRotation.fX = 1.0f;
    sync.data.vecRotation.fY = 3.0f;
    sync.data.vecRotation.fZ = 5.0f;
    sync.Write(bs);
    bs.ResetReadPointer();
    SRotationRadiansSync out(false);
    EXPECT_TRUE(out.Read(bs));
    EXPECT_NEAR(1.0f, out.data.vecRotation.fX, 0.001f);
    EXPECT_NEAR(3.0f, out.data.vecRotation.fY, 0.001f);
    EXPECT_NEAR(5.0f, out.data.vecRotation.fZ, 0.001f);
}

// ============================================================================
// Velocity sync
// ============================================================================

// Zero velocity writes only 1 bit (the "has velocity" flag = false).
// This is a bandwidth optimization - stationary entities are common.
TEST(SVelocitySync, RoundTrip_ZeroVelocity)
{
    MockBitStream bs;
    SVelocitySync sync;
    sync.data.vecVelocity.fX = 0.0f;
    sync.data.vecVelocity.fY = 0.0f;
    sync.data.vecVelocity.fZ = 0.0f;
    sync.Write(bs);
    EXPECT_EQ(1, bs.GetNumberOfBitsUsed());
    bs.ResetReadPointer();
    SVelocitySync out;
    EXPECT_TRUE(out.Read(bs));
    EXPECT_FLOAT_EQ(0.0f, out.data.vecVelocity.fX);
    EXPECT_FLOAT_EQ(0.0f, out.data.vecVelocity.fY);
    EXPECT_FLOAT_EQ(0.0f, out.data.vecVelocity.fZ);
}

// Non-zero velocity: direction is NormVector-encoded (16-bit per component),
// magnitude is a raw float. The reconstructed vector = direction * magnitude.
TEST(SVelocitySync, RoundTrip_NonZero)
{
    MockBitStream bs;
    SVelocitySync sync;
    sync.data.vecVelocity.fX = 10.0f;
    sync.data.vecVelocity.fY = 0.0f;
    sync.data.vecVelocity.fZ = 0.0f;
    sync.Write(bs);
    bs.ResetReadPointer();
    SVelocitySync out;
    EXPECT_TRUE(out.Read(bs));
    EXPECT_NEAR(10.0f, out.data.vecVelocity.fX, 0.01f);
    EXPECT_NEAR(0.0f, out.data.vecVelocity.fY, 0.01f);
    EXPECT_NEAR(0.0f, out.data.vecVelocity.fZ, 0.01f);
}

// Diagonal velocity: tests that all three components of the NormVector
// survive encoding with acceptable precision.
TEST(SVelocitySync, RoundTrip_DiagonalVelocity)
{
    MockBitStream bs;
    SVelocitySync sync;
    sync.data.vecVelocity.fX = 5.0f;
    sync.data.vecVelocity.fY = 5.0f;
    sync.data.vecVelocity.fZ = 5.0f;
    sync.Write(bs);
    bs.ResetReadPointer();
    SVelocitySync out;
    EXPECT_TRUE(out.Read(bs));
    EXPECT_NEAR(5.0f, out.data.vecVelocity.fX, 0.05f);
    EXPECT_NEAR(5.0f, out.data.vecVelocity.fY, 0.05f);
    EXPECT_NEAR(5.0f, out.data.vecVelocity.fZ, 0.05f);
}
