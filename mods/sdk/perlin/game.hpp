#pragma once
#include <climits>
#include <array>
#include <charconv>
#include <cmath>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "perlin/rpc.hpp"
#include "perlin/types.hpp"

namespace perlin::game {
    inline Result<PlayerState> player() {
        return detail::call_as<PlayerState>(Op::PlayerState);
    }

    inline Vec3 position(const PlayerState& p) {
        return {p.x, p.y, p.z};
    }
    inline Vec3 velocity(const PlayerState& p) {
        return {p.vx, p.vy, p.vz};
    }
    inline Vec3 eye_position(const PlayerState& p) {
        return {p.x, p.y + p.eyeHeight, p.z};
    }
    inline Vec3 look_direction(const PlayerState& p) {
        const double yaw = p.yaw * std::numbers::pi / 180.0, pitch = p.pitch * std::numbers::pi / 180.0;
        return {-std::sin(yaw) * std::cos(pitch), -std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
    }

    struct Look {
        float yaw = 0, pitch = 0;
    };
    inline Result<Look> look() {
        return detail::call_as<Look>(Op::PlayerLook);
    }

    inline Result<std::vector<Effect>> effects() {
        return detail::call_list<Effect>(Op::PlayerEffects);
    }

    inline Result<Inventory> inventory() {
        return detail::call_as<Inventory>(Op::Inventory);
    }

    inline Result<WorldState> world() {
        return detail::call_as<WorldState>(Op::WorldState);
    }

    inline Result<BlockInfo> block(const BlockPos& pos) {
        abi::Writer w;
        w.write(pos.x, pos.y, pos.z);
        auto info = detail::call_as<abi::BlockInfo>(Op::Block, w);
        if (!info)
            return unexpected(info.error());
        BlockInfo out;
        static_cast<abi::BlockInfo&>(out) = std::move(*info);
        return out;
    }

    inline std::string block_name(const BlockPos& pos) {
        auto b = block(pos);
        return b && b->loaded ? b->name : std::string{};
    }

    struct MapColumn {
        uint32_t rgb = 0;
        int16_t height = 0;
        int16_t top = 0;
        uint8_t depth = 0;
        uint8_t flags = 0;
    };
    inline constexpr uint8_t kMapLoaded = 1, kMapWater = 2, kMapLava = 4, kMapLeaves = 8, kMapSnow = 16,
                             kMapRock = 32;
    struct MapChunk {
        bool loaded = false;
        uint64_t version = 0;
        std::array<MapColumn, 256> columns{};
    };
    inline constexpr int32_t kMapSurface = INT32_MIN;

    inline Result<MapChunk> map_chunk(int chunkX, int chunkZ, int32_t caveY = kMapSurface) {
        abi::Writer w;
        w.put<int32_t>(chunkX).put<int32_t>(chunkZ).put<int32_t>(caveY);
        auto bytes = detail::call(Op::MapChunk, w);
        if (!bytes)
            return unexpected(bytes.error());
        abi::Reader r(bytes->data(), bytes->size());
        MapChunk out;
        out.loaded = r.boolean();
        out.version = r.get<uint64_t>();
        if (out.loaded)
            for (MapColumn& c : out.columns) {
                c.rgb = r.get<uint32_t>();
                c.height = r.get<int16_t>();
                c.top = r.get<int16_t>();
                c.depth = r.get<uint8_t>();
                c.flags = r.get<uint8_t>();
            }
        if (!r.Ok())
            return unexpected(Error{Status::Failed});
        return out;
    }

    inline bool chunk_loaded(int chunkX, int chunkZ) {
        abi::Writer w;
        w.write<int32_t>(chunkX, chunkZ);
        return detail::call_bool(Op::ChunkLoaded, w);
    }

    inline Result<abi::Region> region(const BlockPos& from, const BlockPos& to) {
        abi::Writer w;
        w.write(from.x, from.y, from.z, to.x, to.y, to.z);
        return detail::call_as<abi::Region>(Op::Region, w);
    }

    struct RaycastOptions {
        double max_distance = 6.0;
        bool include_liquids = false;
    };

    inline Result<RayHit> raycast(const Vec3& origin, const Vec3& direction, RaycastOptions options = {}) {
        abi::Writer w;
        w.write(origin.x, origin.y, origin.z, direction.x, direction.y, direction.z, options.max_distance,
                options.include_liquids);
        return detail::call_as<RayHit>(Op::Raycast, w);
    }

    inline Result<AimState> aim() {
        return detail::call_as<AimState>(Op::Aim);
    }

    struct FindOptions {
        std::string name;
        int radius = 8;
        int max_results = 256;
        bool exact = false;
    };

    inline Result<std::vector<abi::FoundBlock>> find_blocks(const BlockPos& center,
                                                            const FindOptions& options) {
        abi::Writer w;
        w.write(center.x, center.y, center.z, static_cast<int32_t>(options.radius),
                static_cast<int32_t>(options.max_results), options.exact, options.name);
        return detail::call_list<abi::FoundBlock>(Op::FindBlocks, w);
    }

    inline Result<BlockProps> block_props(std::string_view name) {
        abi::Writer w;
        w.write(name);
        return detail::call_as<BlockProps>(Op::BlockProps, w);
    }

    inline Result<BlockEntityInfo> block_entity(const BlockPos& pos) {
        abi::Writer w;
        w.write(pos.x, pos.y, pos.z);
        return detail::call_as<BlockEntityInfo>(Op::BlockEntityData, w);
    }

    inline std::string display_name(NameKind kind, std::string_view id, int aux = 0) {
        abi::Writer w;
        w.write(kind, id, static_cast<int32_t>(aux));
        auto bytes = detail::call(Op::DisplayName, w);
        return bytes ? std::string(bytes->begin(), bytes->end()) : std::string(id);
    }

    inline Result<BreakState> break_state() {
        return detail::call_as<BreakState>(Op::BreakProgress);
    }

    inline Result<std::vector<std::string>> block_names() {
        return detail::call_list<std::string>(Op::BlockNames);
    }

    struct EntityQuery {
        double radius = 0;
        std::string type;
        bool players_only = false;
        bool include_items = true;
    };

    inline Result<std::vector<Entity>> entities(const EntityQuery& query = {}) {
        abi::Writer w;
        w.write(query.radius, query.type, query.players_only, query.include_items);
        return detail::call_list<Entity>(Op::Entities, w);
    }

    inline Result<Entity> entity(uint64_t runtimeId) {
        abi::Writer w;
        w.write(runtimeId);
        return detail::call_as<Entity>(Op::EntityById, w);
    }

    inline Result<std::vector<PlayerEntry>> players() {
        return detail::call_list<PlayerEntry>(Op::PlayerList);
    }

    inline Result<ScreenState> screen() {
        return detail::call_as<ScreenState>(Op::Screen);
    }

    inline Result<abi::Meta> meta() {
        return detail::call_as<abi::Meta>(Op::Meta);
    }

    inline Result<abi::Perspective> perspective() {
        return detail::call_as<abi::Perspective>(Op::PerspectiveGet);
    }

    inline Result<std::vector<abi::ChatLine>> chat_history(uint32_t count = 20) {
        abi::Writer w;
        w.write(count);
        return detail::call_list<abi::ChatLine>(Op::ChatHistory, w);
    }

    inline Result<abi::TitleStateData> title_state() {
        return detail::call_as<abi::TitleStateData>(Op::TitleState);
    }

    inline Result<std::vector<abi::PackEntry>> resource_packs() {
        return detail::call_list<abi::PackEntry>(Op::ResourcePacks);
    }

    inline Result<std::vector<abi::ModEntry>> mods() {
        return detail::call_list<abi::ModEntry>(Op::ModList);
    }

    enum class Option : uint8_t {
        maxFps,
        vsync,
        renderDistance,
        fov,
        gamma,
        mouseSensitivity,
        invertMouse,
        fancyGraphics,
        cloudRange,
        guiScale,
        shadows,
        fovEffects,
        motionBlur,
        damageTilt,
        damageTint,
        thirdPersonCrosshair,
        thirdPersonNametag,
        menuBlur,
        betterGrass,
        showHud,
        glass,
        dynamicLights,
        sprintMode,
        sneakMode,
        capePhysics,
        renderer,
    };

    namespace detail_option {
        enum class Kind : uint8_t { Boolean, Integer, Real, Text };
        constexpr Kind kind_of(Option o) {
            switch (o) {
            case Option::vsync:
            case Option::invertMouse:
            case Option::fancyGraphics:
            case Option::shadows:
            case Option::fovEffects:
            case Option::damageTint:
            case Option::thirdPersonCrosshair:
            case Option::thirdPersonNametag:
            case Option::betterGrass:
            case Option::showHud:
                return Kind::Boolean;
            case Option::maxFps:
            case Option::renderDistance:
            case Option::cloudRange:
            case Option::guiScale:
            case Option::damageTilt:
            case Option::menuBlur:
                return Kind::Integer;
            case Option::fov:
            case Option::gamma:
            case Option::mouseSensitivity:
            case Option::motionBlur:
                return Kind::Real;
            default:
                return Kind::Text;
            }
        }
        template <Kind K> struct value_for {
            using type = std::string;
        };
        template <> struct value_for<Kind::Boolean> {
            using type = bool;
        };
        template <> struct value_for<Kind::Integer> {
            using type = int;
        };
        template <> struct value_for<Kind::Real> {
            using type = double;
        };
    }

    template <Option O> using option_t = typename detail_option::value_for<detail_option::kind_of(O)>::type;

    inline Result<std::string> option_text(std::string_view key) {
        abi::Writer w;
        w.write(key);
        auto bytes = detail::call(Op::OptionGet, w);
        if (!bytes)
            return unexpected(bytes.error());
        return std::string(bytes->begin(), bytes->end());
    }

    inline Result<std::string> options_json() {
        auto bytes = detail::call(Op::Options);
        if (!bytes)
            return unexpected(bytes.error());
        return std::string(bytes->begin(), bytes->end());
    }

    template <Option O> Result<option_t<O>> option() {
        auto text = option_text(meta::enum_name(O));
        if (!text)
            return unexpected(text.error());
        using T = option_t<O>;
        if constexpr (std::is_same_v<T, bool>) {
            return *text == "true";
        } else if constexpr (std::is_same_v<T, std::string>) {
            return std::move(*text);
        } else {
            T v{};
            const auto [ptr, ec] = std::from_chars(text->data(), text->data() + text->size(), v);
            if (ec != std::errc{})
                return unexpected(Error{Status::Failed});
            return v;
        }
    }

    struct BlockTarget {
        BlockPos pos;
        Face face = Face::Up;
        double distance = 0;
        BlockInfo block;
        BlockProps props;
        std::string display_name;
        const BlockEntityInfo& entity() const {
            if (!fetched) {
                if (auto e = block_entity(pos))
                    cached = std::move(*e);
                fetched = true;
            }
            return cached;
        }

    private:
        mutable BlockEntityInfo cached;
        mutable bool fetched = false;
    };

    struct EntityTarget {
        Entity entity;
        double distance = 0;
        std::string display_name;
    };

    using Target = std::variant<std::monostate, BlockTarget, EntityTarget>;

    inline Target target() {
        const auto a = aim();
        if (!a)
            return {};
        if (a->kind == AimKind::Block && a->block.hit) {
            BlockTarget t;
            t.pos = {a->block.x, a->block.y, a->block.z};
            t.face = a->block.face;
            t.distance = a->distance;
            if (auto b = block(t.pos))
                t.block = std::move(*b);
            else
                return {};
            if (auto p = block_props(t.block.name))
                t.props = std::move(*p);
            t.display_name = t.props.displayName.empty() ? display_name(NameKind::Block, t.block.name)
                                                         : t.props.displayName;
            return t;
        }
        if (a->kind == AimKind::Entity) {
            EntityTarget t;
            if (auto e = entity(a->entity))
                t.entity = std::move(*e);
            else
                return {};
            t.distance = a->distance;
            t.display_name =
                !t.entity.nameTag.empty() ? t.entity.nameTag : display_name(NameKind::Entity, t.entity.type);
            return t;
        }
        return {};
    }

    inline std::string translate(std::string_view key) {
        abi::Writer w;
        w.write(key);
        auto bytes = detail::call(Op::Translate, w);
        return bytes ? std::string(bytes->begin(), bytes->end()) : std::string(key);
    }

    inline Result<std::vector<std::string>> server_commands() {
        return detail::call_list<std::string>(Op::ServerCommands);
    }

    enum class Control : uint8_t {
        Forward,
        Back,
        Left,
        Right,
        Jump,
        Sneak,
        Sprint,
        Look,
        Attack,
        Use,
        TogglePerspective,
        Screenshot,
        Zoom,
        ToggleHud,
        Chat,
        Command,
        DebugScreen,
        PickItem,
        Drop,
        Inventory,
        Pause,
        PlayerList,
        Hotbar1,
        Hotbar2,
        Hotbar3,
        Hotbar4,
        Hotbar5,
        Hotbar6,
        Hotbar7,
        Hotbar8,
        Hotbar9,
    };

    inline constexpr std::array<std::string_view, meta::enum_count<Control>> kControlIds{
        "key.forward",
        "key.back",
        "key.left",
        "key.right",
        "key.jump",
        "key.sneak",
        "key.sprint",
        "key.look",
        "key.attack",
        "key.use",
        "key.togglePerspective",
        "key.screenshot",
        "key.zoom",
        "key.toggleHud",
        "key.chat",
        "key.command",
        "key.debugScreen",
        "key.pickItem",
        "key.drop",
        "key.inventory",
        "key.pause",
        "key.playerlist",
        "key.hotbar.1",
        "key.hotbar.2",
        "key.hotbar.3",
        "key.hotbar.4",
        "key.hotbar.5",
        "key.hotbar.6",
        "key.hotbar.7",
        "key.hotbar.8",
        "key.hotbar.9"};

    constexpr std::string_view control_id(Control c) {
        return kControlIds[static_cast<size_t>(c)];
    }

    inline Result<std::vector<std::string>> input_actions() {
        return detail::call_list<std::string>(Op::InputActions);
    }

    inline bool input_down(std::string_view action) {
        abi::Writer w;
        w.write(action);
        return detail::call_bool(Op::InputDown, w);
    }
    inline bool input_down(Control control) {
        return input_down(control_id(control));
    }

    struct MousePos {
        float x = 0, y = 0;
        bool captured = false;
    };

    inline Result<MousePos> mouse() {
        return detail::call_as<MousePos>(Op::MousePosition);
    }

    inline int64_t now_ms() {
        auto v = detail::call_as<int64_t>(Op::Clock);
        return v ? *v : 0;
    }
}
