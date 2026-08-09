/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CDefinitiveRadarSA.h
 *  PURPOSE:     GTA Definitive Edition-style radar renderer
 *
 *  The rendering model is derived from Radar Trilogy SA by LLIEPLLIEHb:
 *  https://github.com/multimaks2/Radar-in-style-GTA-SA-The-Definitive-Edition
 *  See Shared/data/MTA San Andreas/MTA/radar-definitive/THIRD_PARTY_NOTICE.txt.
 *
 *****************************************************************************/

#pragma once

// Installs a validated CHud::DrawRadar dispatcher. Style 0 forwards to GTA's
// original renderer; style 1 uses the full Definitive render target pipeline.
bool InstallDefinitiveRadarRenderer();
void ShutdownDefinitiveRadarRenderer();

// CHudSA owns component visibility. Keeping that state outside GTA's patched
// first opcode prevents HUD_RADAR toggles from overwriting the dispatcher.
void SetDefinitiveRadarVisible(bool visible);
bool IsDefinitiveRadarVisible();
