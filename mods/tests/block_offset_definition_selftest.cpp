#include "client/resources/BlockOffsetDefinition.hpp"
#include "client/resources/ServerBlocks.hpp"

#include <bit>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

using namespace perlin;

int main(int argc, char** argv) {
    if (argc != 2)
        return 1;
    std::ifstream input(argv[1]);
    if (!input)
        return 1;
    int cases = 0;
    auto& registry = ServerBlocks::Instance();
    const auto before = registry.CollisionRevision();
    registry.Reset();
    if (registry.CollisionRevision() == before)
        return 1;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.starts_with("O "))
            continue;
        std::istringstream row(line.substr(2));
        std::array<uint32_t, 15> words{};
        row >> std::hex;
        for (auto& word : words)
            if (!(row >> word))
                return 1;
        ui::Json definition = ui::Json::MakeObject();
        constexpr const char* names[] = {"x", "y", "z"};
        bool representable = true;
        for (int axis = 0; axis < 3; ++axis) {
            const float low = std::bit_cast<float>(words[3 + axis * 3]);
            const float high = std::bit_cast<float>(words[4 + axis * 3]);
            const double descriptionLow = static_cast<double>(low) * 16.0;
            const double descriptionHigh = static_cast<double>(high) * 16.0;
            representable &= low <= high && std::abs(descriptionLow) <= std::numeric_limits<float>::max() &&
                             std::abs(descriptionHigh) <= std::numeric_limits<float>::max();
            auto range = ui::Json::MakeObject();
            range.Set("min", ui::Json(descriptionLow));
            range.Set("max", ui::Json(descriptionHigh));
            auto value = ui::Json::MakeObject();
            value.Set("range", std::move(range));
            value.Set("steps", ui::Json(static_cast<double>(words[5 + axis * 3])));
            definition.Set(names[axis], std::move(value));
        }
        if (!representable)
            continue;
        CustomBlockCollision collision;
        collision.randomOffset = ParseBlockOffsetDefinition(&definition);
        if (!collision.randomOffset)
            return 1;
        const auto revision = registry.CollisionRevision();
        registry.SetCollision("test:random_offset", std::move(collision));
        CustomBlockCollision stored;
        if (registry.CollisionRevision() == revision || !registry.Collision("test:random_offset", stored) ||
            !stored.randomOffset)
            return 1;
        const physics::bds::BlockPosition position{std::bit_cast<int32_t>(words[0]),
                                                   std::bit_cast<int32_t>(words[1]),
                                                   std::bit_cast<int32_t>(words[2])};
        const auto result = physics::bds::GenerateBlockOffset(*stored.randomOffset, position);
        if (std::bit_cast<uint32_t>(result.x) != words[12] ||
            std::bit_cast<uint32_t>(result.y) != words[13] || std::bit_cast<uint32_t>(result.z) != words[14])
            return 1;
        ++cases;
    }
    if (!input.eof() || cases != 959)
        return 1;
    for (const char* text : {"false", "{\"x\":3}", "{\"x\":{\"steps\":-1}}", "{\"x\":{\"steps\":0.5}}",
                             "{\"x\":{\"steps\":4294967296}}", "{\"z\":{\"range\":{\"min\":2,\"max\":1}}}",
                             "{\"x\":{\"range\":{\"min\":-1e99,\"max\":0}}}"}) {
        ui::Json value;
        std::string error;
        if (!ui::Json::Parse(text, value, error) || ParseBlockOffsetDefinition(&value))
            return 1;
    }
    ui::Json defaults = ui::Json::MakeObject();
    if (!ParseBlockOffsetDefinition(&defaults))
        return 1;
    registry.Reset();
    CustomBlockCollision removed;
    if (registry.Collision("test:random_offset", removed))
        return 1;
    std::printf("Canonical block offset loading: %d BDS-derived offset outputs match\n", cases);
    return 0;
}
