#include "client/resources/BlockOffsetDefinition.hpp"

#include <cmath>
#include <limits>

namespace perlin {
    std::optional<physics::bds::BlockRandomOffset> ParseBlockOffsetDefinition(const ui::Json* value) {
        if (!value || !value->IsObject())
            return std::nullopt;
        std::array<physics::bds::BlockOffsetAxis, 3> description{};
        constexpr const char* names[] = {"x", "y", "z"};
        for (size_t i = 0; i < description.size(); ++i) {
            const auto* axis = value->Find(names[i]);
            if (!axis)
                continue;
            if (!axis->IsObject())
                return std::nullopt;
            if (const auto* range = axis->Find("range")) {
                if (!range->IsObject())
                    return std::nullopt;
                const auto number = [](const ui::Json* field, float& out) {
                    if (!field)
                        return true;
                    constexpr double maximum = std::numeric_limits<float>::max();
                    if (!field->IsNumber() || !std::isfinite(field->number) || field->number < -maximum ||
                        field->number > maximum)
                        return false;
                    out = static_cast<float>(field->number);
                    return std::isfinite(out);
                };
                if (!number(range->Find("min"), description[i].minimum) ||
                    !number(range->Find("max"), description[i].maximum) ||
                    description[i].minimum > description[i].maximum)
                    return std::nullopt;
            }
            if (const auto* steps = axis->Find("steps")) {
                if (!steps->IsNumber() || !std::isfinite(steps->number) || steps->number < 0 ||
                    steps->number > std::numeric_limits<uint32_t>::max() ||
                    std::floor(steps->number) != steps->number)
                    return std::nullopt;
                description[i].steps = static_cast<uint32_t>(steps->number);
            }
        }
        return physics::bds::BlockOffsetFromDescription(description);
    }
}
