#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "client/renderer/block/BlockGeometry.hpp"
#include "client/renderer/block/BlockModels.hpp"

namespace perlin {

    struct BlockAppearance {
        int top = 0;
        int side = 0;
        int bottom = 0;
        std::vector<int> varTop, varSide, varBottom;
        int sideTile[4] = {0, 0, 0, 0};
        float overlayFrac = 0;
        int overlayTile = 0;
        float tintR = 1, tintG = 1, tintB = 1;
        bool tintTop = false;
        bool tintSide = false;
        bool tintBottom = false;
        std::string carriedTex;
        bool useCarriedColor = false;
        bool hasCarriedRGB = false;
        float carriedR = 1, carriedG = 1, carriedB = 1;
        enum class Shape { Cube, Cross, Flat, Wall };
        Shape shape = Shape::Cube;
        bool cutout = false;
        std::string texTop, texSide, texBottom;
    };

    class BlockLibrary {
    public:
        static int TilePx() { return tilePxValue; }
        static constexpr int kAtlasTiles = 128;
        static int MaxTiles() { return maxTilesValue; }
        static void SetMaxTiles(int tiles) {
            maxTilesValue = std::clamp(tiles, 256, kAtlasTiles * kAtlasTiles);
        }

        void SetTexturePack(bool active);
        bool EnsureLoaded(const std::string& perlinPackDir, const std::string& vanillaRpRoot);
        void Reset();
        bool Ready() const { return ready; }
        void SetFancy(bool value) { fancy = value; }
        bool Complete() const { return complete; }
        // budgetMs > 0: stop after that long and leave the rest queued
        bool ApplyCustom(const std::string& vanillaRpRoot, double budgetMs = 0);

        enum TechnicalBits { kBarrier = 1, kLightBlock = 2, kStructureVoid = 4 };
        void SetTechnicalVisible(int mask) const;
        int TechnicalVisible() const { return technicalMask.load(std::memory_order_relaxed); }
        const BlockAppearance& Look(int32_t localId) const;
        const BlockAppearance* LookByName(const std::string& name) const;
        bool TilePixels(int tile, std::vector<uint8_t>& rgba) const;
        const BlockModels& Models() const { return models; }
        int GlassMode() const { return glassMode.load(std::memory_order_relaxed); }
        void SetGlassMode(int mode) { glassMode.store(mode, std::memory_order_relaxed); }
        bool NeedsGlassRebuild(int) const { return false; }
        int BetterGrass() const { return betterGrass.load(std::memory_order_relaxed); }
        void SetBetterGrass(int mode) { betterGrass.store(mode, std::memory_order_relaxed); }
        bool IsGlassBase(int tile) const { return glassTiles.find(tile) != glassTiles.end(); }
        int GlassTile(int baseTile, int mode, int mask) const;
        static float GlassInset() { return 1.0f / 16.0f + 0.5f / static_cast<float>(tilePxValue); }
        BlockGeometry& Geometry() const { return geometry; }

        const std::vector<uint8_t>& AtlasPixels() const { return atlas; }
        int TilesLoaded() const { return nextTile; }
        static void TileUV(int tile, float& u0, float& v0, float& u1, float& v1);

        struct TileAlpha {
            bool holes = false;
            bool translucent = false;
            bool centerHollow = false;
            float saturation = 0;
            float frame = 0;
            float holeFrac = 0;
        };
        TileAlpha AlphaOf(int tile) const;
        float SaturationOf(int tile) const;
        std::string TilePathOf(int tile) const;
        int TileContaining(const std::string& part) const;
        std::vector<uint8_t> FoliageFlags() const;
        void PollAnimation(uint64_t step, const std::function<void(int tile, const uint8_t* rgba)>& emit);

    private:
        int FetchTile(const std::string& pngRelPath, const std::string& vanillaRpRoot);
        int FetchOrSynth(const std::string& pngRelPath, const std::string& vanillaRpRoot,
                         const std::string& label, const std::unordered_map<std::string, std::string>& stems);
        int SynthTile(const std::string& label);
        bool fetchingGeometry = false;

        BlockModels models;
        mutable BlockGeometry geometry;
        bool modelsTried = false;
        bool geometryTried = false;
        bool ready = false;
        bool fancy = true;
        bool complete = false;
        int fetchFails = 0;
        int lastFileMisses = -1;
        int stallStreak = 0;
        int nextTile = 1;
        std::vector<int> synthTiles;
        std::unordered_map<std::string, BlockAppearance> appearances;
        std::vector<uint8_t> atlas;
        std::unordered_map<std::string, int> tileByPng;
        std::unordered_map<int, TileAlpha> alphaByTile;
        std::unordered_set<int> overlayMaskTiles;
        std::unordered_map<int, std::string> pathByTile;
        struct AnimSrc {
            std::vector<uint8_t> pixels;
            int frames = 0;
        };
        std::unordered_map<int, AnimSrc> animByTile;
        int NewTile();
        void BuildGlassVariants();
        bool glassBuilt = false;
        bool glassLean = false;
        int glassBuiltMode = 0;
        std::atomic<int> glassMode{0};
        mutable std::atomic<int> technicalMask{0};
        std::atomic<int> betterGrass{0};
        std::unordered_map<int, std::vector<int>> glassTiles;
        inline static int tilePxValue = 16;
        inline static int maxTilesValue = 2048;
        std::unordered_map<uint64_t, int> tileByHash;
        std::unordered_map<std::string, std::array<float, 3>> baseTints;
        mutable std::unordered_map<int32_t, bool> unknownLogged;
        mutable std::vector<const BlockAppearance*> localLook;
        mutable std::mutex lookMutex;
        mutable std::shared_mutex alphaMutex;
    };

}
