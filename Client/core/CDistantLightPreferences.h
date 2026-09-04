#pragma once
#include "CClientVariables.h"
#include <game/SDistantLightSettings.h>

// Keep the native and web settings on the same persisted Project2DFX policy.
inline SDistantLightSettings ReadDistantLightPreferences()
{
    SDistantLightSettings settings;
    settings.automaticDistance = CVARS_GET_VALUE<bool>("distant_lights_automatic_distance");
    settings.growWithDistance = CVARS_GET_VALUE<bool>("distant_lights_grow_with_distance");
    settings.nearAlpha = CVARS_GET_VALUE<float>("distant_lights_near_alpha");
    settings.reachFullAlpha = CVARS_GET_VALUE<float>("distant_lights_reach_full_alpha");
    settings.boostStart = CVARS_GET_VALUE<float>("distant_lights_boost_start");
    settings.farAlphaBoost = CVARS_GET_VALUE<float>("distant_lights_far_alpha_boost");
    return settings;
}

inline void SaveDistantLightPreferences(const SDistantLightSettings& settings)
{
    CVARS_SET("distant_lights_automatic_distance", settings.automaticDistance);
    CVARS_SET("distant_lights_grow_with_distance", settings.growWithDistance);
    CVARS_SET("distant_lights_near_alpha", settings.nearAlpha);
    CVARS_SET("distant_lights_reach_full_alpha", settings.reachFullAlpha);
    CVARS_SET("distant_lights_boost_start", settings.boostStart);
    CVARS_SET("distant_lights_far_alpha_boost", settings.farAlphaBoost);
}
