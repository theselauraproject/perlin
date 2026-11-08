#pragma once
#include "client/renderer/block/BlockModels.hpp"
#include "util/Json.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace perlin {

    class BlockGeometry {
    public:
        using FetchFn = std::function<int(const std::string& relPath, const std::string& root)>;

        void ScanPack(const std::string& packRoot, const std::string& blocksDir, const FetchFn& fetch);
        void AddCustom(const std::string& name, const std::string& definitionJson,
                       const std::vector<std::string>& packRoots, const std::string& textureRoot,
                       const FetchFn& fetch);

        void CutCustomSheets(const std::string& root, const FetchFn& fetch);
        void Clear();
        bool Empty() const {
            std::shared_lock lock(mutex);
            return blocks.empty();
        }
        size_t Count() const {
            std::shared_lock lock(mutex);
            return blocks.size();
        }

        const ModelEntry* Find(uint16_t localId);

    private:
        struct Cube {
            float origin[3] = {0, 0, 0}, size[3] = {0, 0, 0};
            float pivot[3] = {0, 0, 0}, rot[3] = {0, 0, 0};
            float inflate = 0;
            bool mirror = false, boxUv = false;
            float uvBox[2] = {0, 0};
            struct Face {
                bool has = false;
                float uv[2] = {0, 0}, size[2] = {0, 0};
                int rotation = 0;
                std::string mat;
            } faces[6];
            std::string mat;
        };
        struct Bone {
            std::string name, parent;
            float pivot[3] = {0, 0, 0}, rot[3] = {0, 0, 0};
            std::vector<Cube> cubes;

            struct Poly {
                float pos[4][3] = {};
                float uv[4][2] = {};
            };
            std::vector<Poly> polys;
            bool polyNormalized = true;
        };
        struct Geometry {
            float tw = 16, th = 16;
            std::vector<Bone> bones;
        };
        struct Components {
            bool hasGeometry = false;
            std::string geometry;
            bool hasMaterials = false;
            std::unordered_map<std::string, int> tiles;
            std::unordered_map<std::string, bool> dimming;

            std::unordered_map<std::string, std::string> paths;
            std::unordered_map<std::string, int> cells;
            bool hasTransform = false;
            float rot[3] = {0, 0, 0}, trans[3] = {0, 0, 0}, scale[3] = {1, 1, 1};
        };
        struct Block {
            Components base;
            std::vector<std::pair<std::string, Components>> perms;
        };

        void LoadGeometries(const std::string& dir);
        void CutSheets(Block& block, const std::string& root, const FetchFn& fetch) const;
        static void ReadPolyMesh(json::View mesh, Bone& bone);
        std::vector<ModelQuad> Bake(const Geometry& g, const Components& c) const;

        std::unordered_map<std::string, Geometry> geometries;
        std::unordered_map<std::string, Block> blocks;

        mutable std::shared_mutex mutex;
        std::vector<std::string> loadedPacks;
        std::string terrainKey;
        std::unordered_map<std::string, std::string> customTerrain;
        std::unordered_map<uint16_t, const ModelEntry*> byId;
        std::deque<ModelEntry> storage;
    };

    bool EvaluateBlockCondition(const std::string& condition, uint16_t localId);
}
