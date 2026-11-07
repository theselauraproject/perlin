#include "client/resources/BlockBoxDefinition.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace perlin {
    namespace {
        bool Enabled(const ui::Json& value) {
            if (value.IsBool())
                return value.boolean;
            if (value.IsNumber())
                return std::isfinite(value.number) && value.number != 0;
            return true;
        }
        bool Box(const ui::Json& value, CollisionBox& out) {
            if (!value.IsObject())
                return false;
            if (const auto* enabled = value.Find("enabled"); enabled && !Enabled(*enabled))
                return false;
            constexpr const char* keys[] = {"minX", "minY", "minZ", "maxX", "maxY", "maxZ"};
            bool corners = false;
            for (const char* key : keys)
                corners |= value.Find(key) != nullptr;
            if (corners) {
                for (int i = 0; i < 6; ++i) {
                    const auto* coordinate = value.Find(keys[i]);
                    if (!coordinate || !coordinate->IsNumber() || !std::isfinite(coordinate->number))
                        return false;
                    out[i] = std::clamp(static_cast<float>(coordinate->number / 16.0), 0.0f, 1.0f);
                }
            } else {
                float origin[] = {-8, 0, -8}, size[] = {16, 16, 16};
                const auto triple = [&](const char* key, float* result) {
                    const auto* array = value.Find(key);
                    if (!array)
                        return true;
                    if (!array->IsArray() || array->array.size() != 3)
                        return false;
                    for (int i = 0; i < 3; ++i) {
                        if (!array->array[i].IsNumber() || !std::isfinite(array->array[i].number) ||
                            std::abs(array->array[i].number) > std::numeric_limits<float>::max())
                            return false;
                        result[i] = static_cast<float>(array->array[i].number);
                    }
                    return true;
                };
                if (!triple("origin", origin) || !triple("size", size))
                    return false;
                for (int i = 0; i < 3; ++i) {
                    const float offset = i == 1 ? 0 : 8;
                    out[i] = (origin[i] + offset) / 16;
                    out[i + 3] = (origin[i] + offset + size[i]) / 16;
                }
            }
            for (float coordinate : out)
                if (!std::isfinite(coordinate))
                    return false;
            return out[3] > out[0] && out[4] > out[1] && out[5] > out[2];
        }
    }
    CollisionSet ParseBlockBoxComponent(const ui::Json* component) {
        CollisionSet result;
        if (!component)
            return result;
        result.defined = true;
        if (!Enabled(*component))
            return result;
        if (component->IsBool() || component->IsNumber()) {
            result.boxes.push_back({0, 0, 0, 1, 1, 1});
            return result;
        }
        if (const auto* enabled = component->Find("enabled"); enabled && !Enabled(*enabled))
            return result;
        const auto add = [&](const ui::Json& value) {
            CollisionBox box;
            if (Box(value, box))
                result.boxes.push_back(box);
        };
        if (component->IsArray()) {
            for (const auto& value : component->array)
                add(value);
        } else if (const auto* boxes = component->Find("boxes")) {
            if (!boxes->IsArray())
                return result;
            for (const auto& value : boxes->array)
                add(value);
        } else
            add(*component);
        return result;
    }
}
