#pragma once
#include "client/attachable/Animation.hpp"
#include "client/attachable/Geometry.hpp"
#include "client/attachable/Molang.hpp"
#include "client/entity/Materials.hpp"

#include <array>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace perlin::entity {
    using ExprPtr = std::shared_ptr<molang::Expr>;

    struct ClientEntityDef {
        std::string identifier;
        std::map<std::string, std::string> textures, geometries, animations, controllers;
        std::map<std::string, std::string> materials;
        // particle_effects: short name used by animations and controllers -> particle effect identifier
        std::map<std::string, std::string> particleEffects;
        std::vector<ExprPtr> initialize, preAnimation;
        struct Animate {
            std::string name;
            ExprPtr weight;
        };
        std::vector<Animate> animate;
        ExprPtr scale;
        struct Controller {
            std::string id;
            ExprPtr condition;
        };
        std::vector<Controller> renderControllers;
        bool ok = false;
    };

    struct RenderControllerDef {
        ExprPtr geometry;
        std::vector<ExprPtr> textures;
        std::map<std::string, std::vector<molang::Value>> arrays;
        struct Part {
            std::string pattern;
            ExprPtr visible;
        };
        std::vector<Part> parts;
        struct MaterialRule {
            std::string pattern;
            ExprPtr value;
        };
        std::vector<MaterialRule> materials;
        ExprPtr color[4];
        bool hasColor = false;
        bool translucent = false;
        // ignore_lighting: drawn at full brightness whatever the world light where the actor stands
        bool ignoreLighting = false;
        float lightMultiplier = 1.0f;
        // light_color_multiplier written as molang (a pulsing glow): evaluated per frame, replaces the number
        ExprPtr lightMultiplierExpr;
        // overlay_color: a colour laid over the texture by its alpha; is_hurt_color and on_fire_color replace the
        // game's own red hurt flash and fire tint (alpha 0 turns them off)
        ExprPtr overlay[4], hurtColor[4], fireColor[4];
        bool hasOverlay = false, hasHurtColor = false, hasFireColor = false;
        // uv_anim: texture coordinates become uv * scale + offset, both molang (scrolling force fields, water)
        ExprPtr uvOffset[2], uvScale[2];
        bool hasUvAnim = false;
    };

    class EntityAssets {
    public:
        void Configure(const std::string& root);
        void SyncOverlays();
        bool Loaded() const { return loaded; }
        const ClientEntityDef* Entity(const std::string& identifier) const;
        const RenderControllerDef* RenderController(const std::string& id) const;
        bool CollisionBox(const std::string& identifier, float& width, float& height) const;
        MaterialLibrary materialLibrary;
        attachable::AnimationLibrary animations;
        attachable::GeometryLibrary geometries;
        size_t EntityCount() const { return entities.size(); }
        const std::string& Root() const { return root; }

    public:
        bool Seat(const std::string& identifier, float& x, float& y, float& z) const;

    private:
        void LoadAll();
        void LoadDirectory(const std::string& dir, bool overlay);
        uint32_t overlayGeneration = 0;
        size_t overlaysLoaded = 0;
        std::string root;
        bool loaded = false;
        std::unordered_map<std::string, ClientEntityDef> entities;
        std::unordered_map<std::string, RenderControllerDef> controllers;
        std::unordered_map<std::string, std::pair<float, float>> collisionBoxes;
        std::unordered_map<std::string, std::array<float, 3>> seats;
    };

    std::string LowerCase(std::string s);
    std::string AfterDot(const std::string& ref);
}
