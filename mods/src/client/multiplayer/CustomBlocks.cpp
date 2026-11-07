#include "client/multiplayer/CustomBlocks.hpp"

#include "client/multiplayer/NbtJson.hpp"
#include "client/resources/ServerBlocks.hpp"
#include "client/resources/BlockBoxDefinition.hpp"
#include "client/resources/BlockOffsetDefinition.hpp"
#include "world/level/block/BlockPalette.hpp"
#include "world/level/lighting/LightRegistry.hpp"
#include "world/level/chunk/ChunkCodec.hpp"

#include "util/Paths.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <iostream>
#include <map>

namespace perlin {
    namespace {
        using State = BlockPalette::StateValue;

        int32_t ResolvePersistent(const PersistentBlockState& block) {
            std::vector<std::pair<std::string, State>> states;
            states.reserve(block.states.size());
            for (const auto& e : block.states) {
                State v;
                v.kind = e.kind == 0 ? State::Bool : e.kind == 1 ? State::Int : State::String;
                v.i = e.number;
                v.s = e.text;
                states.emplace_back(e.key, std::move(v));
            }
            std::sort(states.begin(), states.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });
            std::string name = block.name;
            if (!name.empty() && !name.contains(':'))
                name = "minecraft:" + name;
            const int32_t hash = BlockPalette::HashState(name, states);
            BlockPalette::Instance().LearnState(hash, name, std::move(states));
            return hash;
        }

        int ParseLevel(const ui::Json* v, const char* key, bool fraction) {
            if (!v)
                return -1;
            const ui::Json* n = v;
            if (v->IsObject())
                n = v->Find(key);
            // network nbt bytes arrive as bools that keep their value ("lightLevel": false is a 0 filter, not absent);
            // a byte is a level already, only floats are 0..1 fractions
            if (!n || !(n->IsNumber() || n->IsBool()))
                return -1;
            double d = n->IsBool() ? n->number : n->NumberOr(0);
            if (fraction && !n->IsBool() && d <= 1.0)
                d *= 15.0;
            return std::clamp(static_cast<int>(std::lround(d)), 0, 15);
        }

        struct Prop {
            std::string name;
            std::vector<State> values;
        };

        State ToState(const ui::Json& v) {
            State s;
            if (v.IsString()) {
                s.kind = State::String;
                s.s = v.string;
            } else if (v.IsBool()) {
                s.kind = State::Bool;
                s.i = v.boolean ? 1 : 0;
            } else {
                s.kind = State::Int;
                s.i = static_cast<int32_t>(v.NumberOr(0));
            }
            return s;
        }
    }

    namespace {
        const ui::Json* FindKey(const ui::Json& node, const std::string& key, int depth = 0) {
            if (depth > 6 || !node.IsObject())
                return nullptr;
            if (const ui::Json* hit = node.Find(key))
                return hit;
            for (const auto& [k, v] : node.object)
                if (const ui::Json* hit = FindKey(v, key, depth + 1))
                    return hit;
            return nullptr;
        }

        std::string IconKey(const ui::Json* icon) {
            if (!icon)
                return {};
            if (icon->IsString())
                return icon->string;
            if (!icon->IsObject())
                return {};
            if (const ui::Json* textures = icon->Find("textures"); textures && textures->IsObject())
                if (const ui::Json* d = textures->Find("default"); d && d->IsString())
                    return d->string;
            if (const ui::Json* t = icon->Find("texture"); t && t->IsString())
                return t->string;
            return {};
        }
    }

    size_t RegisterCustomItems(const std::vector<proto::ItemstatesEntry>& items) {
        ServerBlocks::Instance().ClearItems();
        size_t count = 0;
        for (const proto::ItemstatesEntry& item : items) {
            if (item.name.starts_with("minecraft:") || item.nbt.bytes.empty())
                continue;
            ui::Json nbt;
            if (!NetworkNbtToJson(item.nbt.bytes, nbt) || !nbt.IsObject())
                continue;
            CustomItemDef def;
            def.name = item.name;
            def.icon = IconKey(FindKey(nbt, "minecraft:icon"));
            if (def.icon.empty())
                def.icon = IconKey(FindKey(nbt, "icon"));
            if (const ui::Json* dn = FindKey(nbt, "minecraft:display_name")) {
                if (dn->IsString())
                    def.displayName = dn->string;
                else if (const ui::Json* v = dn->Find("value"); v && v->IsString())
                    def.displayName = v->string;
            }
            if (const ui::Json* wearable = FindKey(nbt, "minecraft:wearable")) {
                const ui::Json* slot = wearable->IsObject() ? wearable->Find("slot") : nullptr;
                const std::string where = slot && slot->IsString() ? slot->string : std::string();
                static const char* const kSlots[] = {"slot.armor.head", "slot.armor.chest", "slot.armor.legs",
                                                     "slot.armor.feet"};
                for (int i = 0; i < 4; i++)
                    if (where == kSlots[i])
                        def.armorSlot = i;
            }
            // use_modifiers in seconds, the older item_properties use_duration in ticks; food without either eats in 32
            if (const ui::Json* mods = FindKey(nbt, "minecraft:use_modifiers"); mods && mods->IsObject())
                if (const ui::Json* d = mods->Find("use_duration"); d && d->IsNumber())
                    def.useTicks = static_cast<int>(d->NumberOr(0) * 20.0);
            if (def.useTicks == 0)
                if (const ui::Json* d = FindKey(nbt, "use_duration"); d && d->IsNumber())
                    def.useTicks = static_cast<int>(d->NumberOr(0));
            if (def.useTicks == 0 && FindKey(nbt, "minecraft:food"))
                def.useTicks = 32;
            ServerBlocks::Instance().SetItem(std::move(def));
            count++;
        }
        return count;
    }

    void InstallPersistentChunkResolver() {
        SetPersistentResolver(&ResolvePersistent);
    }

    size_t RegisterCustomBlocks(const std::vector<proto::BlockPropertiesEntry>& blocks) {
        auto& palette = BlockPalette::Instance();
        size_t states = 0;
        palette.TruncateSequence();
        std::vector<BlockPalette::CustomRun> runs;
        std::string customDump = "{\n";
        ServerBlocks::Instance().Reset();
        LightRegistry::Instance().ClearCustom();
        for (const proto::BlockPropertiesEntry& entry : blocks) {
            ui::Json nbt;
            if (entry.name.empty() || !NetworkNbtToJson(entry.state.bytes, nbt) || !nbt.IsObject())
                continue;
            std::vector<Prop> axes;
            {
                static const std::pair<const char*, std::vector<const char*>> kTraitStates[] = {
                    {"block_face", {"down", "up", "north", "south", "west", "east"}},
                    {"vertical_half", {"bottom", "top"}},
                    {"cardinal_direction", {"south", "west", "north", "east"}},
                    {"facing_direction", {"down", "up", "north", "south", "west", "east"}},
                };
                static const char* kConnections[] = {"connection_north", "connection_east",
                                                     "connection_south", "connection_west"};
                const auto key = [](const std::string& n) {
                    return n.starts_with("minecraft:") ? n.substr(10) : n;
                };
                std::vector<std::string> enabled;
                const auto collect = [&](const std::string& traitName, const ui::Json& trait) {
                    const ui::Json* flags = trait.IsObject() ? trait.Find("enabled_states") : nullptr;
                    if (!flags)
                        return;
                    std::string own = traitName;
                    if (own.empty())
                        if (const ui::Json* n = trait.Find("name"); n && n->IsString())
                            own = n->string;
                    if (flags->IsNumber()) {
                        if (key(own) == "connection" && (static_cast<int64_t>(flags->NumberOr(0)) & 1))
                            enabled.push_back("cardinal_connections");
                    } else if (flags->IsArray()) {
                        for (const ui::Json& v : flags->array)
                            if (v.IsString())
                                enabled.push_back(key(v.string));
                    } else if (flags->IsObject()) {
                        for (const auto& [k, v] : flags->object)
                            if ((v.IsNumber() && v.NumberOr(0) != 0) || (v.IsBool() && v.boolean))
                                enabled.push_back(key(k));
                    }
                };
                if (const ui::Json* traits = nbt.Find("traits")) {
                    if (traits->IsArray())
                        for (const ui::Json& t : traits->array)
                            collect(std::string(), t);
                    else if (traits->IsObject())
                        for (const auto& [k, t] : traits->object)
                            collect(k, t);
                }
                const auto has = [&](const std::string& n) {
                    return std::find(enabled.begin(), enabled.end(), n) != enabled.end();
                };
                for (const auto& [name, values] : kTraitStates)
                    if (has(name)) {
                        Prop prop;
                        prop.name = std::string("minecraft:") + name;
                        for (const char* v : values) {
                            State st;
                            st.kind = State::String;
                            st.s = v;
                            prop.values.push_back(st);
                        }
                        axes.push_back(std::move(prop));
                    }
                const bool cardinal = has("cardinal_connections");
                for (const char* dir : kConnections)
                    if (cardinal || has(dir)) {
                        Prop prop;
                        prop.name = std::string("minecraft:") + dir;
                        for (int b = 0; b < 2; b++) {
                            State st;
                            st.kind = State::Bool;
                            st.i = b;
                            prop.values.push_back(st);
                        }
                        axes.push_back(std::move(prop));
                    }
            }
            if (const ui::Json* props = nbt.Find("properties"); props && props->IsArray())
                for (const ui::Json& p : props->array) {
                    const ui::Json* name = p.Find("name");
                    const ui::Json* values = p.Find("enum");
                    if (!name || !name->IsString() || !values || !values->IsArray() || values->array.empty())
                        continue;
                    Prop prop;
                    prop.name = name->string;
                    for (const ui::Json& v : values->array)
                        prop.values.push_back(ToState(v));
                    axes.push_back(std::move(prop));
                }
            std::vector<size_t> idx(axes.size(), 0);
            size_t emitted = 0;
            BlockPalette::CustomRun run;
            run.name = entry.name;
            for (;;) {
                std::vector<std::pair<std::string, State>> st;
                st.reserve(axes.size());
                for (size_t i = 0; i < axes.size(); i++)
                    st.emplace_back(axes[i].name, axes[i].values[idx[i]]);
                std::sort(st.begin(), st.end(),
                          [](const auto& a, const auto& b) { return a.first < b.first; });
                const int32_t stateHash = BlockPalette::HashState(entry.name, st);
                palette.LearnState(stateHash, entry.name, st);
                run.hashes.push_back(stateHash);
                states++;
                if (++emitted >= 65536)
                    break;
                size_t d = 0;
                while (d < idx.size() && ++idx[d] == axes[d].values.size())
                    idx[d++] = 0;
                if (d == idx.size())
                    break;
            }
            runs.push_back(std::move(run));

            CustomBlockDef def;
            def.name = entry.name;
            def.json = nbt.Dump();
            customDump += "\"" + entry.name + "\": " + def.json + ",\n";
            CustomBlockCollision collision;
            int emission = -1, dampening = -1;
            if (const ui::Json* components = nbt.Find("components"); components && components->IsObject()) {
                collision.base = ParseBlockBoxComponent(components->Find("minecraft:collision_box"));
                collision.selection = ParseBlockBoxComponent(components->Find("minecraft:selection_box"));
                collision.randomOffset =
                    ParseBlockOffsetDefinition(components->Find("minecraft:random_offset"));
                if (const ui::Json* f = components->Find("minecraft:friction")) {
                    const ui::Json* v = f->IsObject() ? f->Find("value") : f;
                    if (v && v->IsNumber())
                        collision.friction = std::clamp(1.0f - static_cast<float>(v->number), 0.1f, 1.0f);
                }
                int level = ParseLevel(components->Find("minecraft:light_emission"), "emission", false);
                if (level < 0)
                    level = ParseLevel(components->Find("minecraft:block_light_emission"), "emission", true);
                emission = level;
                int damp = ParseLevel(components->Find("minecraft:light_dampening"), "lightLevel", false);
                if (damp < 0)
                    damp = ParseLevel(components->Find("minecraft:block_light_filter"), "lightLevel", false);
                dampening = damp;
                if (const ui::Json* mi = components->Find("minecraft:material_instances");
                    mi && mi->IsObject()) {
                    const ui::Json* mats = mi->Find("materials");
                    const ui::Json* map = mats && mats->IsObject() ? mats : mi;
                    for (const auto& [face, v] : map->object) {
                        if (!v.IsObject())
                            continue;
                        if (const ui::Json* tex = v.Find("texture"); tex && tex->IsString())
                            def.faceTexture[face] = tex->string;
                        if (const ui::Json* rm = v.Find("render_method");
                            rm && rm->IsString() && rm->string != "opaque")
                            def.cutout = true;
                    }
                }
            }
            if (const ui::Json* perms = nbt.Find("permutations"); perms && perms->IsArray())
                for (const ui::Json& perm : perms->array) {
                    const ui::Json* cond = perm.Find("condition");
                    const ui::Json* comps = perm.Find("components");
                    if (!cond || !cond->IsString() || !comps || !comps->IsObject())
                        continue;
                    CollisionSet set = ParseBlockBoxComponent(comps->Find("minecraft:collision_box"));
                    if (set.defined)
                        collision.permutations.emplace_back(cond->string, std::move(set));
                    CollisionSet selection = ParseBlockBoxComponent(comps->Find("minecraft:selection_box"));
                    if (selection.defined)
                        collision.selectionPermutations.emplace_back(cond->string, std::move(selection));
                    if (auto offset = ParseBlockOffsetDefinition(comps->Find("minecraft:random_offset")))
                        collision.randomOffsetPermutations.emplace_back(cond->string, *offset);
                }
            if (collision.base.defined || !collision.permutations.empty() || collision.selection.defined ||
                !collision.selectionPermutations.empty() || collision.friction >= 0.0f ||
                collision.randomOffset || !collision.randomOffsetPermutations.empty())
                ServerBlocks::Instance().SetCollision(entry.name, std::move(collision));
            if (emission >= 0 || dampening >= 0)
                LightRegistry::Instance().SetCustom(entry.name, emission, dampening);
            ServerBlocks::Instance().Add(std::move(def));
        }
        palette.InsertCustomRuns(std::move(runs));
        // raw server custom block defs, for offline geometry/material checks
        if (customDump.size() > 3) {
            customDump.resize(customDump.size() - 2);
            std::error_code ec;
            std::filesystem::create_directories(paths::DataDir() + "/ui_generated", ec);
            std::ofstream(paths::DataDir() + "/ui_generated/custom_blocks.json",
                          std::ios::binary | std::ios::trunc)
                << customDump << "\n}\n";
        }
        return states;
    }
}
