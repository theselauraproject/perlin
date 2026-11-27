#pragma once

#include <array>
#include <string_view>

namespace perlin {
    inline int CameraEaseIndex(std::string_view name, int protocol = 0) {
        static constexpr std::array<std::string_view, 33> names{
            "linear", "spring", "in_quad", "out_quad", "in_out_quad", "in_cubic", "out_cubic",
            "in_out_cubic", "in_quart", "out_quart", "in_out_quart", "in_quint", "out_quint",
            "in_out_quint", "in_sine", "out_sine", "in_out_sine", "in_expo", "out_expo",
            "in_out_expo", "in_circ", "out_circ", "in_out_circ", "in_bounce", "out_bounce",
            "in_out_bounce", "in_back", "out_back", "in_out_back", "in_elastic", "out_elastic",
            "in_out_elastic", "inverse_lerp"};
        for (size_t i = 0; i < names.size(); ++i) {
            if (name == names[i])
                return static_cast<int>(i);
            // The matched 2193 FOV reader lowercases its 32 registered labels.
            // Leave Perlin's existing unknown-label and extra-curve policy alone.
            if (protocol != 2193 || i >= 32 || name.size() != names[i].size())
                continue;
            bool equal = true;
            for (size_t j = 0; j < name.size(); ++j) {
                char value = name[j];
                if (value >= 'A' && value <= 'Z')
                    value = static_cast<char>(value + ('a' - 'A'));
                if (value != names[i][j]) {
                    equal = false;
                    break;
                }
            }
            if (equal)
                return static_cast<int>(i);
        }
        return 0;
    }
}
