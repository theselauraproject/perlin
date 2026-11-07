#pragma once
#include "client/attachable/Animation.hpp"
#include "client/attachable/Geometry.hpp"
#include "client/attachable/Molang.hpp"

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace perlin::attachable {
    struct ItemState {
        std::string identifier;
        bool mainHand = true;
        bool firstPerson = true;
        bool enchanted = false;
        bool blocking = false;
        bool bindPose = false;
        bool using_ = false;
        float usedTicks = 0;
        float maxUseTicks = 72000;
        int animationFrame = 0;
        float partial = 0;
        double lifeSeconds = 0;
        std::string mainHandItem, offHandItem;
    };

    struct Part {
        std::string texture;
        std::vector<Quad> quads;
    };

    struct Frame {
        bool valid = false;
        std::vector<Part> parts;
        bool glint = false;
    };

    class Attachables {
    public:
        Attachables();
        ~Attachables();
        Attachables(const Attachables&) = delete;
        Attachables& operator=(const Attachables&) = delete;
        void Configure(const std::string& root);
        bool Has(const std::string& identifier);
        Frame Evaluate(const ItemState& state, int slot);
        bool ArmorInfo(const std::string& identifier, std::string& geometry, std::string& texture);
        void Clear();

    private:
        struct RenderController {
            std::shared_ptr<molang::Expr> geometry;
            std::vector<std::shared_ptr<molang::Expr>> textures;
            std::map<std::string, std::vector<molang::Value>> arrays;
        };
        struct Entry {
            bool loaded = false, ok = false;
            std::string identifier, name;
            std::map<std::string, std::string> textures, geometries, animations;
            std::vector<std::shared_ptr<molang::Expr>> initialize, preAnimation;
            std::vector<std::pair<std::string, std::shared_ptr<molang::Expr>>> animate;
            std::vector<std::string> renderControllers;
        };
        struct Instance;

        Entry* Load(const std::string& identifier);
        void LoadSupport(Entry& entry);
        bool ReadFile(const std::string& rel, std::string& out) const;
        // Server packs name attachable files freely, so they are indexed by the identifier inside each file.
        void RefreshServerIndex();
        std::string serverKey;
        std::unordered_map<std::string, std::string> serverFiles;
        void LoadRenderControllers(const std::string& text);
        const Texture* TextureFor(const std::string& path);

        std::string root;
        std::unordered_map<std::string, Entry> entries;
        AnimationLibrary animations;
        GeometryLibrary geometries;
        std::unordered_map<std::string, RenderController> controllers;
        std::unordered_map<std::string, bool> loadedFiles;
        std::unordered_map<std::string, std::unique_ptr<Instance>> instances;
        std::unordered_map<std::string, std::unique_ptr<Texture>> textureCache;
    };
}
