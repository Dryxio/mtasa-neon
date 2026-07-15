/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CNativeBullworthPackSA.h
 *  PURPOSE:     Reviewed Bullworth native world-pack descriptor
 *
 *****************************************************************************/

#pragma once

struct SNativeWorldPackDescriptorSA;

// Bullworth remains a checked-in prototype payload, but all registration
// behavior is driven through this descriptor so the runtime is not tied to a
// city-specific registrar class.
const SNativeWorldPackDescriptorSA& GetNativeBullworthPackDescriptor();
