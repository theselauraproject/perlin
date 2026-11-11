// Generated from minimap.json by perlin_settings.py. Do not edit.
#pragma once
#include <perlin/settings.hpp>

namespace settings {
    enum class Corner { TopRight, TopLeft, BottomRight, BottomLeft };
    enum class CaveMode { Auto, Off, On };

    inline constexpr perlin::Toggle enabled{"enabled"};
    inline constexpr perlin::Choice<Corner> corner{"corner"};
    inline constexpr perlin::Integer size{"size"};
    inline constexpr perlin::Number zoom{"zoom"};
    inline constexpr perlin::Toggle rotate{"rotate"};
    inline constexpr perlin::Number opacity{"opacity"};
    inline constexpr perlin::Number relief{"relief"};
    inline constexpr perlin::Toggle sun_shadows{"sun_shadows"};
    inline constexpr perlin::Toggle water_depth{"water_depth"};
    inline constexpr perlin::Toggle day_night{"day_night"};
    inline constexpr perlin::Toggle grid{"grid"};
    inline constexpr perlin::Choice<CaveMode> cave_mode{"cave_mode"};
    inline constexpr perlin::Integer cave_zoom{"cave_zoom"};
    inline constexpr perlin::Toggle waypoints{"waypoints"};
    inline constexpr perlin::Toggle death_waypoint{"death_waypoint"};
    inline constexpr perlin::Button clear_waypoints{"clear_waypoints"};
}
