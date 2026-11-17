#include <waila.cpp>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

#include "font_widths.inc"

namespace {
    std::string scenario = "stone";
    std::string out_path = "preview.txt";
    std::map<std::string, std::string> setting_values;
    std::vector<uint8_t> submitted;
    int64_t clock_ms = 1000;

    float font_width(const std::string& text) {
        float w = 0;
        bool code = false;
        for (size_t i = 0; i < text.size(); i++) {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            if (c == 0xC2 && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0xA7) {
                i += 2;
                code = true;
                continue;
            }
            code = false;
            w += c < 128 ? kGlyphWidth[c] : 6;
        }
        (void)code;
        return w;
    }

    std::string loaded_manifest_defaults() {
        std::ifstream in(std::string(PREVIEW_MANIFEST));
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    const std::map<std::string, std::string> kNames = {
        {"minecraft:stone", "Stone"},
        {"minecraft:wheat", "Wheat Seeds"},
        {"minecraft:chest", "Chest"},
        {"minecraft:noteblock", "Note Block"},
        {"minecraft:cow", "Cow"},
        {"minecraft:horse", "Horse"},
        {"minecraft:bedrock", "Bedrock"},
        {"minecraft:diamond", "Diamond"},
        {"minecraft:iron_ingot", "Iron Ingot"},
        {"minecraft:apple", "Apple"},
        {"minecraft:cobblestone", "Cobblestone"},
        {"minecraft:oak_log", "Oak Log"},
        {"minecraft:dirt", "Dirt"},
    };

    abi::BlockProps props_for(const std::string& name) {
        abi::BlockProps p;
        p.known = true;
        const auto it = kNames.find(name);
        p.displayName = it == kNames.end() ? name : it->second;
        if (name == "minecraft:stone" || name == "minecraft:cobblestone") {
            p.hardness = 1.5f;
            p.tool = abi::Tool::Pickaxe;
            p.harvestLevel = 1;
        } else if (name == "minecraft:bedrock")
            p.hardness = -1;
        else if (name == "minecraft:chest" || name == "minecraft:oak_log") {
            p.hardness = 2.0f;
            p.tool = abi::Tool::Axe;
        } else if (name == "minecraft:dirt") {
            p.hardness = 0.5f;
            p.tool = abi::Tool::Shovel;
        } else
            p.hardness = 0.0f;
        return p;
    }
}

extern "C" int32_t perlin_host_call(uint32_t op, const void* in, uint32_t inLen, void* out, uint32_t outCap) {
    const auto o = static_cast<Op>(op);
    abi::Reader r(static_cast<const uint8_t*>(in), inLen);
    abi::Writer w;
    switch (o) {
    case Op::SettingList: {
        auto manifest = json::parse(loaded_manifest_defaults());
        std::vector<abi::SettingEntry> list;
        if (manifest)
            for (const json::Value& s : (*manifest)["settings"].array()) {
                const std::string id(s["id"].string());
                if (id.empty())
                    continue;
                const json::Value& d = s["default"];
                std::string value = d.is_bool()     ? (d.boolean() ? "true" : "false")
                                    : d.is_string() ? std::string(d.string())
                                                    : std::format("{}", d.number());
                if (setting_values.count(id))
                    value = setting_values[id];
                list.push_back({id, value});
            }
        w.write(list);
        break;
    }
    case Op::PlayerState: {
        abi::PlayerState p;
        p.connected = true;
        p.x = 0, p.y = 64, p.z = 0;
        w.write(p);
        break;
    }
    case Op::Inventory: {
        abi::Inventory inv;
        inv.slots[0].name = scenario == "bedrock" ? "minecraft:wooden_pickaxe" : "minecraft:wooden_pickaxe";
        inv.slots[0].count = 1;
        w.write(inv);
        break;
    }
    case Op::Aim: {
        abi::AimState a;
        if (scenario == "cow" || scenario == "horse") {
            a.kind = abi::AimKind::Entity;
            a.entity = 7;
            a.entityType = scenario == "cow" ? "minecraft:cow" : "minecraft:horse";
            a.distance = 2.5;
        } else {
            a.kind = abi::AimKind::Block;
            a.block.hit = true;
            a.block.x = 10, a.block.y = 64, a.block.z = -3;
            a.block.block = scenario == "wheat"       ? "minecraft:wheat"
                            : scenario == "chest"     ? "minecraft:chest"
                            : scenario == "noteblock" ? "minecraft:noteblock"
                            : scenario == "bedrock"   ? "minecraft:bedrock"
                                                      : "minecraft:stone";
            a.distance = 3.2;
        }
        w.write(a);
        break;
    }
    case Op::Block: {
        abi::BlockInfo b;
        b.loaded = true;
        const std::string n = scenario == "wheat"       ? "minecraft:wheat"
                              : scenario == "chest"     ? "minecraft:chest"
                              : scenario == "noteblock" ? "minecraft:noteblock"
                              : scenario == "bedrock"   ? "minecraft:bedrock"
                                                        : "minecraft:stone";
        b.name = n;
        if (scenario == "wheat")
            b.states.emplace_back("growth", "3");
        w.write(b);
        break;
    }
    case Op::BlockProps: {
        const std::string name = r.str();
        w.write(props_for(name));
        break;
    }
    case Op::BlockEntityData: {
        abi::BlockEntityInfo be;
        if (scenario == "chest") {
            be.present = true;
            be.id = "Chest";
            be.nbt =
                R"({"id":"Chest","Items":[{"Name":"minecraft:diamond","Count":12,"Slot":0},{"Name":"minecraft:iron_ingot","Count":64,"Slot":1},{"Name":"minecraft:apple","Count":3,"Slot":4},{"Name":"minecraft:cobblestone","Count":1,"Slot":5}]})";
        } else if (scenario == "noteblock") {
            be.present = true;
            be.nbt = R"({"id":"Music","note":9})";
        }
        w.write(be);
        break;
    }
    case Op::EntityById: {
        abi::Entity e;
        e.runtimeId = 7;
        e.uniqueId = 7;
        e.type = scenario == "horse" ? "minecraft:horse" : "minecraft:cow";
        e.health = scenario == "horse" ? 28.0f : 7.0f;
        e.maxHealth = scenario == "horse" ? 30.0f : 10.0f;
        if (scenario == "horse") {
            e.flags.set(abi::EntityFlag::Tamed);
            e.flags.set(abi::EntityFlag::Saddled);
            e.attributes.push_back({"minecraft:movement", 0, 0.2f, 1});
            e.attributes.push_back({"minecraft:horse.jump_strength", 0, 0.7f, 2});
            e.ownerUid = 99;
        } else {
            e.flags.set(abi::EntityFlag::Baby);
        }
        w.write(e);
        break;
    }
    case Op::PlayerList: {
        std::vector<abi::PlayerEntry> list;
        list.push_back({"uuid", "Steve", 99, abi::PermissionLevel::Unknown});
        w.write(list);
        break;
    }
    case Op::DisplayName: {
        (void)r.get<uint8_t>();
        const std::string id = r.str();
        const auto it = kNames.find(id);
        const std::string name = it == kNames.end() ? id : it->second;
        w.raw(name.data(), name.size());
        break;
    }
    case Op::UiMeasure: {
        const std::string text = r.str();
        const float scale = r.get<float>();
        w.put(font_width(text) * scale);
        break;
    }
    case Op::UiSubmit: {
        r.str();
        r.get<int32_t>();
        const auto bytes = r.bytes();
        submitted.assign(bytes.begin(), bytes.end());
        break;
    }
    case Op::Clock:
        w.put<int64_t>(clock_ms);
        break;
    case Op::InputDown:
        w.boolean(false);
        break;
    case Op::BreakProgress: {
        abi::BreakState b;
        b.active = scenario == "stone";
        b.x = 10, b.y = 64, b.z = -3;
        b.progress = 0.4f;
        w.write(b);
        break;
    }
    default:
        break;
    }
    if (w.Size() > outCap)
        return -(abi::kNeedMoreBase + static_cast<int32_t>(w.Size()));
    std::memcpy(out, w.Data(), w.Size());
    return static_cast<int32_t>(w.Size());
}

int main(int argc, char** argv) {
    if (argc > 1)
        scenario = argv[1];
    if (argc > 2)
        out_path = argv[2];
    for (int i = 3; i + 1 < argc; i += 2)
        setting_values[argv[i]] = argv[i + 1];

    perlin_event(static_cast<uint32_t>(abi::Event::Load), nullptr, 0);
    abi::Writer w;
    w.write(abi::EventFrame{0.016f, 60.0f, 320, 180});
    for (int i = 0; i < 12; i++) {
        clock_ms += 40;
        perlin_event(static_cast<uint32_t>(abi::Event::Frame), w.Data(), static_cast<uint32_t>(w.Size()));
    }

    std::ofstream out(out_path);
    abi::Reader r(submitted.data(), submitted.size());
    while (r.Remaining() > 0 && r.Ok()) {
        const auto kind = static_cast<abi::Draw>(r.get<uint8_t>());
        if (kind == abi::Draw::Rect) {
            const float x = r.get<float>(), y = r.get<float>(), ww = r.get<float>(), h = r.get<float>();
            const uint32_t c = r.get<uint32_t>();
            out << "rect " << x << ' ' << y << ' ' << ww << ' ' << h << ' ' << c << '\n';
        } else if (kind == abi::Draw::Gradient) {
            const float x = r.get<float>(), y = r.get<float>(), ww = r.get<float>(), h = r.get<float>();
            const uint32_t a = r.get<uint32_t>(), b = r.get<uint32_t>();
            out << "gradient " << x << ' ' << y << ' ' << ww << ' ' << h << ' ' << a << ' ' << b << '\n';
        } else if (kind == abi::Draw::Border) {
            r.get<float>(), r.get<float>(), r.get<float>(), r.get<float>(), r.get<uint32_t>(), r.get<float>();
        } else if (kind == abi::Draw::Line) {
            const float x0 = r.get<float>(), y0 = r.get<float>(), x1 = r.get<float>(), y1 = r.get<float>();
            const uint32_t c = r.get<uint32_t>();
            const float t = r.get<float>();
            out << "line " << x0 << ' ' << y0 << ' ' << x1 << ' ' << y1 << ' ' << c << ' ' << t << '\n';
        } else if (kind == abi::Draw::Text) {
            const float x = r.get<float>(), y = r.get<float>();
            const uint32_t c = r.get<uint32_t>();
            const float s = r.get<float>();
            r.get<uint8_t>(), r.get<uint8_t>();
            out << "text " << x << ' ' << y << ' ' << c << ' ' << s << ' ' << r.str() << '\n';
        } else if (kind == abi::Draw::Item) {
            const float x = r.get<float>(), y = r.get<float>(), s = r.get<float>();
            out << "item " << x << ' ' << y << ' ' << s << ' ' << r.str() << '\n';
        } else if (kind == abi::Draw::Image) {
            const float x = r.get<float>(), y = r.get<float>(), ww = r.get<float>(), h = r.get<float>();
            r.get<uint32_t>();
            out << "image " << x << ' ' << y << ' ' << ww << ' ' << h << ' ' << r.str() << '\n';
        } else {
            break;
        }
    }
    std::printf("%s: %zu bytes submitted\n", scenario.c_str(), submitted.size());
    return 0;
}
