#include "client/javashaders/BlockStates.hpp"

#include <cstdio>
#include <string>

using namespace perlin::jshader;

static int failures = 0;
static void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        failures++;
    }
}

int main() {

    {
        const JavaBlock b = ToJavaBlock("minecraft:lit_furnace|minecraft:cardinal_direction=north");
        Check(b.name == "furnace", "lit_furnace -> furnace");
        Check(b.Find("lit") && *b.Find("lit") == "true", "lit_furnace lit=true");
        Check(b.Find("facing") && *b.Find("facing") == "north", "cardinal_direction -> facing");
    }
    {
        const JavaBlock b = ToJavaBlock("minecraft:lantern|hanging=1");
        Check(b.name == "lantern" && b.Find("hanging") && *b.Find("hanging") == "true", "lantern hanging");
    }
    {
        const JavaBlock b = ToJavaBlock("minecraft:tall_grass|upper_block_bit=1");
        Check(b.Find("half") && *b.Find("half") == "upper", "tall grass upper half");
    }
    {
        const JavaBlock b = ToJavaBlock("minecraft:snow");
        Check(b.name == "snow_block", "bedrock snow is java snow_block");
        const JavaBlock l = ToJavaBlock("minecraft:snow_layer|height=3");
        Check(l.name == "snow" && l.Find("layers") && *l.Find("layers") == "4", "snow layer height");
    }
    {
        const JavaBlock b = ToJavaBlock("minecraft:oak_double_slab|minecraft:vertical_half=bottom");
        Check(b.name == "oak_slab" && b.Find("type") && *b.Find("type") == "double", "double slab");
        const JavaBlock t = ToJavaBlock("minecraft:oak_slab|minecraft:vertical_half=top");
        Check(t.Find("type") && *t.Find("type") == "top", "top slab");
    }
    {
        const JavaBlock b = ToJavaBlock("minecraft:campfire|extinguished=1");
        Check(b.Find("lit") && *b.Find("lit") == "false", "campfire extinguished");
    }
    {
        const JavaBlock b = ToJavaBlock("minecraft:trial_spawner|trial_spawner_state=2|ominous=1");
        Check(b.Find("trial_spawner_state") && *b.Find("trial_spawner_state") == "active",
              "trial spawner state");
        Check(b.Find("ominous") && *b.Find("ominous") == "true", "ominous");
    }
    {
        const JavaBlock b = ToJavaBlock("minecraft:cauldron|fill_level=6|cauldron_liquid=water");
        Check(b.name == "water_cauldron" && b.Find("level") && *b.Find("level") == "3",
              "full water cauldron");
    }
    {
        const JavaBlock b = ToJavaBlock("minecraft:torch|torch_facing_direction=north");
        Check(b.name == "wall_torch", "wall torch");
    }

    {
        BlockIdMatcher m;
        m.Build({{10, {"minecraft:lantern:hanging=false"}},
                 {11, {"lantern:hanging=true", "minecraft:furnace:lit=true"}},
                 {12, {"furnace", "oak_leaves:waterlogged=false", "stone:nonexistent=1"}},
                 {13, {"oak_leaves"}},
                 {14, {"%leaves"}},
                 {15, {"othermod:thing"}}});
        Check(m.Match(ToJavaBlock("minecraft:lantern|hanging=0")) == 10, "lantern standing");
        Check(m.Match(ToJavaBlock("minecraft:lantern|hanging=1")) == 11, "lantern hanging");
        Check(m.Match(ToJavaBlock("minecraft:lit_furnace")) == 11, "lit furnace");
        Check(m.Match(ToJavaBlock("minecraft:furnace")) == 12, "unlit furnace");
        Check(m.Match(ToJavaBlock("minecraft:oak_leaves")) == 12, "leaves, not waterlogged");
        Check(m.Match(ToJavaBlock("minecraft:birch_leaves")) == 14, "leaves tag");
        Check(m.Match(ToJavaBlock("minecraft:stone")) == 12, "missing property ignored");
        Check(m.Match(ToJavaBlock("minecraft:dirt")) == -1, "unlisted");
    }
    {
        const auto ids = FlatIdMap({{1, {"minecraft:zombie", "skeleton"}}, {2, {"zombie"}}, {3, {"mod:x"}}});
        Check(ids.at("zombie") == 2, "last entity line wins");
        Check(ids.at("skeleton") == 1, "plain name");
        Check(!ids.contains("x"), "other namespace skipped");
        Check(JavaEntityName("minecraft:zombie_pigman") == "zombified_piglin", "entity rename");
        Check(JavaItemName("minecraft:record_cat") == "music_disc_cat", "music disc");
    }
    if (failures == 0)
        std::printf("block_states_selftest: ok\n");
    return failures == 0 ? 0 : 1;
}
