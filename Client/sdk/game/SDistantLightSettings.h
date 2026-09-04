// Project2DFX curve controls shared by the game, settings UI and headless tests.
#pragma once
#include <cmath>

struct SDistantLightSettings
{
    bool  automaticDistance = true;
    bool  growWithDistance = true;
    float nearAlpha = 0.5f;
    float reachFullAlpha = 150.0f;
    float boostStart = 150.0f;
    float farAlphaBoost = 4.0f;

    bool IsValid() const
    {
        return std::isfinite(nearAlpha) && nearAlpha >= 0 && nearAlpha <= 1 && std::isfinite(reachFullAlpha) && reachFullAlpha >= 1 && reachFullAlpha <= 2000 &&
               std::isfinite(boostStart) && boostStart >= 0 && boostStart <= 5000 && std::isfinite(farAlphaBoost) && farAlphaBoost >= 1 && farAlphaBoost <= 8;
    }
};
