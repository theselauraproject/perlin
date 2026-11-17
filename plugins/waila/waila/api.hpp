#pragma once
#include <perlin/perlin.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace waila {
    using namespace perlin;

    struct Text {
        std::string text;
        Rgba color = Rgba::white;
    };
    struct ItemIcon {
        std::string id;
        float size = 8.0f;
        int count = 0;
    };
    struct Texture {
        std::string path;
        float width = 9.0f, height = 9.0f;
    };
    struct Bar {
        float fraction = 0.0f;
        Rgba fill = Rgba::green;
        float width = 80.0f, height = 9.0f;
        std::string label;
    };
    struct Hearts {
        float health = 0.0f, max = 20.0f;
        int per_line = 10;
    };
    struct Armor {
        float points = 0.0f;
    };
    struct Mark {
        bool ok = true;
        float size = 5.0f;
        float drop = 0.0f;
    };
    struct Gap {
        float width = 4.0f;
    };
    using Element = std::variant<Text, ItemIcon, Texture, Bar, Hearts, Armor, Mark, Gap>;

    class Row {
    public:
        Row& add(Element element) {
            (pin_right ? right : left).push_back(std::move(element));
            return *this;
        }
        Row& text(std::string value, Rgba color = Rgba::white) { return add(Text{std::move(value), color}); }
        template <class... Args> Row& text(Rgba color, std::format_string<Args...> fmt, Args&&... args) {
            return add(Text{std::format(fmt, std::forward<Args>(args)...), color});
        }
        Row& item(std::string id, float size = 8.0f, int count = 0) {
            return add(ItemIcon{std::move(id), size, count});
        }
        Row& texture(std::string path, float width = 9.0f, float height = 9.0f) {
            return add(Texture{std::move(path), width, height});
        }
        Row& bar(float fraction, Rgba fill, std::string label = {}, float width = 80.0f) {
            return add(Bar{fraction, fill, width, 9.0f, std::move(label)});
        }
        Row& hearts(float health, float max, int per_line = 10) { return add(Hearts{health, max, per_line}); }
        Row& armor(float points) { return add(Armor{points}); }
        Row& mark(bool ok, float size = 5.0f, float drop = 0.0f) { return add(Mark{ok, size, drop}); }
        Row& gap(float width = 4.0f) { return add(Gap{width}); }
        Row& right_aligned() {
            pin_right = true;
            return *this;
        }
        bool empty() const { return left.empty() && right.empty(); }

        std::vector<Element> left, right;

    private:
        bool pin_right = false;
    };

    enum class Stage : uint8_t { Head, Body, Tail };

    class Tooltip {
    public:
        Row& row() { return rows[static_cast<size_t>(stage)].emplace_back(); }
        Row& row(Stage s) { return rows[static_cast<size_t>(s)].emplace_back(); }
        Tooltip& line(std::string value, Rgba color = Rgba::white) {
            row().text(std::move(value), color);
            return *this;
        }
        template <class... Args> Tooltip& line(Rgba color, std::format_string<Args...> fmt, Args&&... args) {
            row().text(std::format(fmt, std::forward<Args>(args)...), color);
            return *this;
        }

        Row& title() {
            auto& head = rows[static_cast<size_t>(Stage::Head)];
            if (head.empty())
                head.emplace_back();
            return head.front();
        }
        Tooltip& set_title(std::string name, Rgba color = Rgba::white) {
            auto& head = rows[static_cast<size_t>(Stage::Head)];
            if (head.empty())
                head.emplace_back();
            head.front().left.clear();
            head.front().left.push_back(Text{std::move(name), color});
            return *this;
        }

        Tooltip& set_icon(ItemIcon item) {
            item.size = 16.0f;
            icon = std::move(item);
            return *this;
        }
        Tooltip& clear_icon() {
            icon.reset();
            return *this;
        }

        Tooltip& hide() {
            hidden = true;
            return *this;
        }

        float breaking = 0.0f;

        std::array<std::vector<Row>, 3> rows;
        std::optional<ItemIcon> icon;
        bool hidden = false;
        Stage stage = Stage::Body;

        bool empty() const {
            for (const auto& list : rows)
                for (const Row& r : list)
                    if (!r.empty())
                        return false;
            return true;
        }
    };

    struct Context {
        PlayerState player;
        Inventory inventory;
        bool details = false;
        int64_t now_ms = 0;

        const perlin::Item& held() const {
            static const perlin::Item none;
            const int slot = inventory.selected;
            return slot >= 0 && slot < static_cast<int>(inventory.slots.size())
                       ? inventory.slots[static_cast<size_t>(slot)]
                       : none;
        }
        bool creative() const {
            return player.gameMode == GameMode::Creative || player.gameMode == GameMode::Spectator;
        }
    };

    struct BlockContext : Context {
        BlockContext(const game::BlockTarget& t, const Context& base) : Context(base), block(t) {}
        const game::BlockTarget& block;
    };

    struct EntityContext : Context {
        EntityContext(const game::EntityTarget& t, const Context& base)
            : Context(base), target(t), entity(t.entity) {}
        const game::EntityTarget& target;
        const Entity& entity;

        float attribute(std::string_view name, float fallback = 0.0f) const {
            for (const Attribute& a : entity.attributes)
                if (a.name == name)
                    return a.value;
            return fallback;
        }
        std::optional<float> property(std::string_view name) const {
            for (const auto& [key, value] : entity.properties)
                if (key == name)
                    return value;
            return std::nullopt;
        }
    };

    class Gate {
    public:
        constexpr Gate() = default;
        constexpr Gate(const Toggle& toggle) : name(toggle.id()) {}
        bool open() const { return name.empty() || perlin::setting<bool>(name); }

    private:
        std::string_view name;
    };
    inline constexpr Gate always{};

    inline bool glob(std::string_view pattern, std::string_view text) {
        if (pattern.empty() || pattern == "*")
            return true;
        if (pattern.find(':') == std::string_view::npos)
            text = short_name(text);
        size_t p = 0, t = 0, star = std::string_view::npos, mark = 0;
        while (t < text.size()) {
            if (p < pattern.size() && pattern[p] == '*') {
                star = p++;
                mark = t;
            } else if (p < pattern.size() && pattern[p] == text[t]) {
                p++;
                t++;
            } else if (star != std::string_view::npos) {
                p = star + 1;
                t = ++mark;
            } else {
                return false;
            }
        }
        while (p < pattern.size() && pattern[p] == '*')
            p++;
        return p == pattern.size();
    }

    template <class Ctx> struct Provider {
        Stage stage = Stage::Body;
        int priority = 0;
        Gate gate;
        std::string pattern;
        std::function<void(Tooltip&, const Ctx&)> run;
    };

    class Registry {
    public:
        using BlockFn = std::function<void(Tooltip&, const BlockContext&)>;
        using EntityFn = std::function<void(Tooltip&, const EntityContext&)>;

        Registry& block(Stage stage, Gate gate, BlockFn fn, std::string_view pattern = "*",
                        int priority = 0) {
            blocks.push_back({stage, priority, gate, std::string(pattern), std::move(fn)});
            return *this;
        }
        Registry& entity(Stage stage, Gate gate, EntityFn fn, std::string_view pattern = "*",
                         int priority = 0) {
            entities.push_back({stage, priority, gate, std::string(pattern), std::move(fn)});
            return *this;
        }

        void run(Tooltip& tip, const BlockContext& ctx) const {
            run_all(blocks, tip, ctx, ctx.block.block.name);
        }
        void run(Tooltip& tip, const EntityContext& ctx) const {
            run_all(entities, tip, ctx, ctx.entity.type);
        }

    private:
        template <class Ctx>
        static void run_all(const std::vector<Provider<Ctx>>& list, Tooltip& tip, const Ctx& ctx,
                            std::string_view id) {
            for (const Stage stage : {Stage::Head, Stage::Body, Stage::Tail}) {
                tip.stage = stage;
                std::vector<const Provider<Ctx>*> ordered;
                for (const Provider<Ctx>& p : list)
                    if (p.stage == stage && glob(p.pattern, id))
                        ordered.push_back(&p);
                std::stable_sort(ordered.begin(), ordered.end(),
                                 [](const auto* a, const auto* b) { return a->priority < b->priority; });
                for (const Provider<Ctx>* p : ordered)
                    if (p->gate.open())
                        p->run(tip, ctx);
            }
        }

        std::vector<Provider<BlockContext>> blocks;
        std::vector<Provider<EntityContext>> entities;
    };
}
