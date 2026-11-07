// Generated from barrel_roll.json by perlin_settings.py. Do not edit.
#pragma once
#include <perlin/settings.hpp>

namespace settings {
    enum class ActivationBehaviour { Vanilla, Hybrid, HybridToggle };

    inline constexpr perlin::Toggle mod_enabled{"mod_enabled"};
    inline constexpr perlin::Toggle switch_roll_and_yaw{"switch_roll_and_yaw"};
    inline constexpr perlin::Toggle momentum_based_mouse{"momentum_based_mouse"};
    inline constexpr perlin::Number momentum_mouse_deadzone{"momentum_mouse_deadzone"};
    inline constexpr perlin::Choice<ActivationBehaviour> activation_behaviour{"activation_behaviour"};
    inline constexpr perlin::Toggle invert_pitch{"invert_pitch"};
    inline constexpr perlin::Toggle disable_when_submerged{"disable_when_submerged"};
    inline constexpr perlin::Toggle enable_banking{"enable_banking"};
    inline constexpr perlin::Number banking_strength{"banking_strength"};
    inline constexpr perlin::Toggle simulate_control_surface_efficacy{"simulate_control_surface_efficacy"};
    inline constexpr perlin::Toggle automatic_righting{"automatic_righting"};
    inline constexpr perlin::Number righting_strength{"righting_strength"};
    inline constexpr perlin::Toggle enable_thrust{"enable_thrust"};
    inline constexpr perlin::Number max_thrust{"max_thrust"};
    inline constexpr perlin::Number thrust_acceleration{"thrust_acceleration"};
    inline constexpr perlin::Toggle smoothing_enabled{"smoothing_enabled"};
    inline constexpr perlin::Number smoothing_pitch{"smoothing_pitch"};
    inline constexpr perlin::Number smoothing_yaw{"smoothing_yaw"};
    inline constexpr perlin::Number smoothing_roll{"smoothing_roll"};
    inline constexpr perlin::Number sensitivity_pitch{"sensitivity_pitch"};
    inline constexpr perlin::Number sensitivity_yaw{"sensitivity_yaw"};
    inline constexpr perlin::Number sensitivity_roll{"sensitivity_roll"};
}
