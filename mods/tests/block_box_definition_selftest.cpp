#include "client/resources/BlockBoxDefinition.hpp"
#include <cstdlib>
#include <iostream>
using namespace perlin;
static void Check(bool v, const char* why) {
    if (!v) {
        std::cerr << why << '\n';
        std::exit(1);
    }
}
static CollisionSet Parse(const char* text) {
    ui::Json json;
    std::string error;
    Check(ui::Json::Parse(text, json, error), "bad fixture");
    return ParseBlockBoxComponent(&json);
}
int main() {
    const auto pad =
        Parse(R"({"enabled":1,"boxes":[{"minX":1,"minY":0,"minZ":1,"maxX":15,"maxY":1,"maxZ":15}]})");
    Check(pad.defined && pad.boxes.size() == 1, "network lily pad shape missing");
    Check(pad.boxes[0] == CollisionBox{0.0625f, 0, 0.0625f, 0.9375f, 0.0625f, 0.9375f},
          "network corners treated as origin/size");
    const auto authored = Parse(R"({"origin":[-7,0,-7],"size":[14,1,14]})");
    Check(authored.boxes == pad.boxes, "pack and network encodings disagree");
    const auto dropper = Parse(
        R"({"boxes":[{"minX":0,"minY":0,"minZ":0,"maxX":16,"maxY":4,"maxZ":16},{"minX":4,"minY":4,"minZ":4,"maxX":12,"maxY":12,"maxZ":12}]})");
    Check(dropper.boxes.size() == 2 && dropper.boxes[1][4] == 0.75f,
          "multi-box item dropper collapsed to a cube");
    const auto disabled = Parse(R"({"enabled":0,"boxes":[{"origin":[-8,0,-8],"size":[16,16,16]}]})");
    Check(disabled.defined && disabled.boxes.empty(), "disabled component still has a box");
    Check(Parse(R"({"boxes":[{"minX":0,"maxX":16}]})").boxes.empty(),
          "malformed network box became full cube");
    Check(Parse(R"({"origin":[-8,0,-8],"size":[16,0,16]})").boxes.empty(), "zero-height box has collision");
    Check(Parse(R"({"boxes":[{"minY":0,"maxY":16}]})").boxes.empty(), "partial corners became full cube");
    Check(Parse(R"({"origin":[1e100,0,0]})").boxes.empty(), "unrepresentable origin admitted");
    Check(Parse(R"({"origin":[3e38,0,0],"size":[3e38,16,16]})").boxes.empty(), "overflowing box admitted");
    Check(Parse(R"({"boxes":false})").boxes.empty(), "malformed box collection became full cube");
    Check(!ParseBlockBoxComponent(nullptr).defined, "missing component overrides vanilla");
    std::cout << "custom block boxes passed\n";
}
