/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        sdk/net/NativeWorldProtocol.h
 *  PURPOSE:     Shared closed native-world protocol constants
 *
 *****************************************************************************/

#pragma once

constexpr unsigned char NATIVE_WORLD_BULLWORTH_FORMAT = 1;
constexpr unsigned char NATIVE_WORLD_BULLWORTH_AUTHORIZATION_VERSION = 1;
constexpr unsigned char NATIVE_WORLD_BULLWORTH_POLICY = 1;
constexpr unsigned char NATIVE_WORLD_STATIC_V1_FORMAT = 2;
constexpr unsigned char NATIVE_WORLD_STATIC_V1_AUTHORIZATION_VERSION = 2;
constexpr unsigned char NATIVE_WORLD_STATIC_V1_POLICY = 2;
constexpr unsigned char NATIVE_WORLD_STATIC_V3_SET_FORMAT = 3;

// Epoch 4 cannot attach a pending authorization captured by the preceding
// 0x3D netcode contract before generic sets and extended Z became mandatory.
constexpr unsigned char NATIVE_WORLD_STATIC_V3_SET_AUTHORIZATION_VERSION = 4;
constexpr unsigned char NATIVE_WORLD_STATIC_V3_SET_POLICY = 4;
constexpr unsigned char NATIVE_WORLD_ONE_SHOT_STARTUP_MODE = 1;
