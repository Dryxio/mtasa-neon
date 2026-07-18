/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CNativeBullworthPackSA.h
 *  PURPOSE:     Trusted Bullworth native world-pack policy
 *
 *****************************************************************************/

#pragma once

struct SNativeWorldPackPolicySA;

// Payload identity and inventory are intentionally absent here; the runtime
// reads or derives those values from the pack files.
const SNativeWorldPackPolicySA& GetNativeBullworthPackPolicy();
const SNativeWorldPackPolicySA& GetNativeStaticWorldV1PackPolicy();
const SNativeWorldPackPolicySA* FindNativeWorldPackPolicy(unsigned int format);
