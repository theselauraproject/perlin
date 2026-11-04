#include "world/level/block/BlockPalette.hpp"
#include "util/Json.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>

using namespace perlin;
static void Check(bool ok, const char* why) {
    if (!ok) {
        std::cerr << why << '\n';
        std::exit(1);
    }
}
int main(int argc, char** argv) {
    Check(argc == 2, "expected palette file");
    std::ifstream file(argv[1]);
    const std::string text((std::istreambuf_iterator<char>(file)), {});
    auto& palette = BlockPalette::Instance();
    Check(palette.LoadServerStatesText(text, "block_states.json"), "palette failed to load");
    palette.SetSequential(true);
    Check(palette.SequentialAir() == 17713, "undeclared blocks shifted Lifeboat air");
    Check(palette.InternRuntime(17713) == 0, "Lifeboat air rendered as a block");

    const auto document = json::Document::Parse(text);
    Check(document.has_value(), "invalid palette JSON");
    std::map<std::string, BlockPalette::CustomRun> declared;
    for (const auto record : document->root().items()) {
        std::vector<std::pair<std::string, BlockPalette::StateValue>> states;
        for (const auto [key, value] : record[size_t{2}].members()) {
            BlockPalette::StateValue state;
            const auto kind = value[size_t{0}].str();
            state.kind = kind == "b"   ? BlockPalette::StateValue::Bool
                         : kind == "i" ? BlockPalette::StateValue::Int
                                       : BlockPalette::StateValue::String;
            if (state.kind == BlockPalette::StateValue::String)
                state.s = value[size_t{1}].str();
            else
                state.i = static_cast<int32_t>(value[size_t{1}].integer());
            states.emplace_back(key, std::move(state));
        }
        Check(BlockPalette::HashState(record[size_t{1}].str(), states) == record[size_t{0}].integer(),
              "palette NBT types disagree with the canonical state hash");
        if (record[size_t{3}].boolean(false)) {
            const auto name = record[size_t{1}].str();
            auto& run = declared[name];
            run.name = name;
            run.hashes.push_back(static_cast<int32_t>(record[size_t{0}].integer()));
        }
    }
    Check(declared.size() == 98, "unexpected 1.26.60 data-driven definitions");
    std::vector<BlockPalette::CustomRun> runs;
    runs.reserve(declared.size());
    for (auto& [name, run] : declared)
        runs.push_back(std::move(run));
    palette.InsertCustomRuns(std::move(runs));
    Check(palette.SequentialAir() == 18894, "declared vanilla definitions did not restore native IDs");
    Check(palette.InternRuntime(18894) == 0, "native air did not resolve");
    palette.TruncateSequence();
    Check(palette.SequentialAir() == 17713, "previous server's definitions survived reconnect");
    std::cout << "conditional block palette passed\n";
}
