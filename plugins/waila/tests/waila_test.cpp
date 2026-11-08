#include <perlin/perlin.hpp>

#include <mod_settings.hpp>

#include <waila/api.hpp>
#include <waila/layout.hpp>
#include <waila/vanilla.hpp>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

using namespace perlin;

namespace {
    int failures = 0;
#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                      \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

    std::map<std::string, std::string> overrides;
}

extern "C" int32_t perlin_host_call(uint32_t op, const void* in, uint32_t inLen, void* out, uint32_t outCap) {
    abi::Reader r(static_cast<const uint8_t*>(in), inLen);
    abi::Writer w;
    switch (static_cast<Op>(op)) {
    case Op::SettingList: {
        std::ifstream f(MANIFEST);
        std::stringstream ss;
        ss << f.rdbuf();
        std::vector<abi::SettingEntry> list;
        if (auto m = json::parse(ss.str()))
            for (const json::Value& s : (*m)["settings"].array()) {
                const std::string id(s["id"].string());
                if (id.empty())
                    continue;
                const json::Value& d = s["default"];
                std::string v = d.is_bool()     ? (d.boolean() ? "true" : "false")
                                : d.is_string() ? std::string(d.string())
                                                : std::format("{}", d.number());
                if (overrides.contains(id))
                    v = overrides[id];
                list.push_back({id, v});
            }
        w.write(list);
        break;
    }
    case Op::UiMeasure: {
        const std::string t = r.str();
        w.put(static_cast<float>(t.size()) * 6.0f * r.get<float>());
        break;
    }
    default:
        return -4;
    }
    if (w.Size() > outCap)
        return -(abi::kNeedMoreBase + static_cast<int32_t>(w.Size()));
    std::memcpy(out, w.Data(), w.Size());
    return static_cast<int32_t>(w.Size());
}

struct WailaTestPlugin : perlin::Plugin {};
PERLIN_REGISTER(WailaTestPlugin)

namespace {
    game::BlockTarget block_target(const std::string& name,
                                   std::vector<std::pair<std::string, std::string>> states = {}) {
        game::BlockTarget t;
        t.pos = {1, 2, 3};
        t.block.loaded = true;
        t.block.name = name;
        t.block.states = std::move(states);
        t.display_name = std::string(short_name(name));
        return t;
    }

    std::string row_text(const waila::Row& row) {
        std::string out;
        for (const auto& e : row.left)
            if (const auto* t = std::get_if<waila::Text>(&e))
                out += t->text;
        return out;
    }

    std::vector<std::string> all_text(const waila::Tooltip& tip) {
        std::vector<std::string> lines;
        for (const auto& stage : tip.rows)
            for (const waila::Row& r : stage)
                lines.push_back(row_text(r));
        return lines;
    }

    bool has_line(const waila::Tooltip& tip, std::string_view needle) {
        for (const auto& line : all_text(tip))
            if (line.find(needle) != std::string::npos)
                return true;
        return false;
    }

    waila::Context context(std::string held = {}) {
        waila::Context c;
        c.player.connected = true;
        if (!held.empty()) {
            c.inventory.slots[0].name = std::move(held);
            c.inventory.slots[0].count = 1;
        }
        return c;
    }
}

int main() {
    CHECK(waila::glob("*", "minecraft:stone"));
    CHECK(waila::glob("minecraft:*_stairs", "minecraft:oak_stairs"));
    CHECK(!waila::glob("minecraft:*_stairs", "minecraft:oak_slab"));
    CHECK(waila::glob("*chest", "minecraft:ender_chest"));
    CHECK(waila::glob("chest", "mymod:chest"));
    CHECK(!waila::glob("minecraft:chest", "mymod:chest"));
    CHECK(waila::glob("*sign*", "minecraft:standing_sign"));

    {
        const auto v = json::parse(R"({"a":[1,2,{"b":"xé\n"}],"n":-1.5e1,"t":true,"z":null})");
        CHECK(v && (*v)["a"].size() == 3);
        CHECK((*v)["a"][2]["b"].string() == "x\xc3\xa9\n");
        CHECK((*v)["n"].number() == -15.0 && (*v)["t"].boolean() && (*v)["z"].is_null());
        CHECK((*v)["missing"]["deeper"].integer(7) == 7);
        CHECK(!json::parse("{\"a\":"));
    }

    {
        waila::Registry r;
        std::string order;
        const auto mark = [&](char c) {
            return [&order, c](waila::Tooltip&, const waila::BlockContext&) { order += c; };
        };
        r.block(waila::Stage::Tail, waila::always, mark('t'));
        r.block(waila::Stage::Body, waila::always, mark('b'), "*", 5);
        r.block(waila::Stage::Body, waila::always, mark('a'), "*", -5);
        r.block(waila::Stage::Head, waila::always, mark('h'));
        r.block(waila::Stage::Body, waila::always, mark('x'), "minecraft:dirt");
        r.block(waila::Stage::Body, settings::registry_name, mark('r'));
        r.block(waila::Stage::Body, settings::mod_name, mark('m'), "*", 9);
        const auto target = block_target("minecraft:stone");
        waila::Tooltip tip;
        r.run(tip, waila::BlockContext(target, context()));
        CHECK(order == "habmt");
    }

    waila::Registry reg;
    waila::vanilla::install(reg);
    const auto run_block = [&](const game::BlockTarget& t, waila::Context c = context()) {
        waila::Tooltip tip;
        reg.run(tip, waila::BlockContext(t, c));
        return tip;
    };

    {
        auto tip = run_block(block_target("minecraft:wheat", {{"growth", "3"}}));
        CHECK(has_line(tip, "Growth: 42%"));
        CHECK(has_line(tip, "Minecraft"));
        tip = run_block(block_target("minecraft:wheat", {{"growth", "7"}}));
        CHECK(has_line(tip, "Mature"));
    }
    {
        auto tip = run_block(block_target("minecraft:unpowered_repeater", {{"repeater_delay", "2"}}));
        CHECK(has_line(tip, "Delay: 3 ticks"));
        tip = run_block(block_target("minecraft:redstone_wire", {{"redstone_signal", "9"}}));
        CHECK(has_line(tip, "Power: "));
    }
    CHECK(waila::vanilla::note_name(0) == "F#3" && waila::vanilla::note_name(6) == "C4" &&
          waila::vanilla::note_name(24) == "F#5");
    CHECK(waila::vanilla::mod_title("my_server") == "My Server" &&
          waila::vanilla::mod_title("minecraft") == "Minecraft");
    CHECK(waila::vanilla::item_for_block("minecraft:lit_furnace") == "minecraft:furnace" &&
          waila::vanilla::item_for_block("minecraft:stone") == "minecraft:stone");
    CHECK(waila::vanilla::tool_of("minecraft:iron_pickaxe") == Tool::Pickaxe &&
          waila::vanilla::tool_of("minecraft:shears") == Tool::Shears);
    CHECK(waila::vanilla::tier_of("minecraft:wooden_pickaxe") == 1 &&
          waila::vanilla::tier_of("minecraft:netherite_pickaxe") == 4);

    {
        const auto marks = [](const waila::Tooltip& tip) {
            std::vector<bool> out;
            for (const auto& e : tip.rows[0].front().right)
                if (const auto* m = std::get_if<waila::Mark>(&e))
                    out.push_back(m->ok);
            return out;
        };
        auto stone = block_target("minecraft:stone");
        stone.props.known = true;
        stone.props.hardness = 1.5f;
        stone.props.tool = Tool::Pickaxe;
        stone.props.harvestLevel = 1;
        CHECK(marks(run_block(stone, context("minecraft:wooden_pickaxe"))) == std::vector<bool>{true});
        CHECK(marks(run_block(stone, context("minecraft:wooden_axe"))) == std::vector<bool>{false});
        CHECK(marks(run_block(stone, context())) == std::vector<bool>{false});

        auto obsidian = block_target("minecraft:obsidian");
        obsidian.props = stone.props;
        obsidian.props.harvestLevel = 4;
        CHECK(marks(run_block(obsidian, context("minecraft:iron_pickaxe"))) == std::vector<bool>{false});
        CHECK(marks(run_block(obsidian, context("minecraft:diamond_pickaxe"))) == std::vector<bool>{true});

        auto bedrock = block_target("minecraft:bedrock");
        bedrock.props.known = true;
        bedrock.props.hardness = -1;
        {
            const auto tip = run_block(bedrock);
            bool unbreakable = false;
            for (const auto& e : tip.rows[0].front().right)
                if (const auto* t = std::get_if<waila::Text>(&e))
                    unbreakable |= t->text == "Unbreakable";
            CHECK(unbreakable);
        }
    }

    {
        game::EntityTarget t;
        t.entity.type = "minecraft:cow";
        t.entity.health = 7;
        t.entity.maxHealth = 10;
        t.entity.flags.set(EntityFlag::Baby);
        t.display_name = "Cow";
        waila::Tooltip tip;
        reg.run(tip, waila::EntityContext(t, context()));
        CHECK(has_line(tip, "Cow") && has_line(tip, "Baby") && has_line(tip, "Minecraft"));
        bool hearts = false;
        for (const auto& row : tip.rows[1])
            for (const auto& e : row.left)
                hearts |= std::holds_alternative<waila::Hearts>(e);
        CHECK(hearts);

        overrides["health_max_hearts"] = "5";
        t.entity.health = 100;
        t.entity.maxHealth = 100;
        waila::Tooltip big;
        reg.run(big, waila::EntityContext(t, context()));
        CHECK(has_line(big, "100/100"));
    }

    {
        waila::Tooltip tip;
        tip.set_title("Stone");
        tip.set_icon(waila::ItemIcon{"minecraft:stone"});
        tip.line("Minecraft");
        const auto laid = waila::layout(tip, waila::Theme::dark(), true);
        CHECK(laid.width > 40 && laid.height >= 28);
        const auto without_icon = waila::layout(tip, waila::Theme::dark(), false);
        CHECK(without_icon.width < laid.width);
        const std::string before = waila::signature(tip);
        CHECK(before == waila::signature(tip));
        tip.line("more");
        CHECK(waila::signature(tip) != before);
    }

    std::printf(failures ? "waila_test: %d failure(s)\n" : "waila_test: ok\n", failures);
    return failures ? 1 : 0;
}
