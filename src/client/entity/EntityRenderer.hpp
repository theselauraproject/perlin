#pragma once
#include "client/particle/DataParticles.hpp"
#include "client/Options.hpp"
#include "client/particle/ParticleSystem.hpp"
#include "client/entity/EntityAssets.hpp"
#include "client/entity/EntityTracker.hpp"
#include "client/renderer/RenderContext.hpp"

#include <array>
#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace perlin {
    class HandRenderer;
}

namespace perlin::entity {
    struct BlockEntityDraw {
        enum class Kind { Bed, Sign, Skull };
        int skull = 0;
        std::vector<std::pair<std::string, int>> patterns;
        bool onWall = false;
        float lid = 0.0f;
        Kind kind = Kind::Bed;
        glm::dvec3 origin{0.0};
        float yaw = 0.0f;
        int color = 14;
        bool hanging = false;
        bool wall = false;
        std::string front, back;
        uint32_t frontColor = 0xFF000000u, backColor = 0xFF000000u;
        bool frontGlow = false, backGlow = false;
    };

    class EntityRenderer {
    public:
        EntityRenderer();
        ~EntityRenderer();
        void Initialize(RenderContext& renderer);
        std::pair<float, float> HitBox(const Entity& e) const;

        struct ParticleRequest {
            std::string effect;
            glm::dvec3 at{0.0};
            std::string preScript;
        };
        std::vector<ParticleRequest> TakeParticleRequests() { return std::exchange(particleRequests, {}); }
        glm::dvec3 SeatPosition(const Entity& vehicle, bool playerRider = true) const;

        glm::dvec3 SeatPosition(const Entity& vehicle, bool playerRider, glm::dvec3 pos, float yaw) const;
        bool CollisionBoxOf(const std::string& type, float& width, float& height) const {
            return assets.CollisionBox(type, width, height);
        }
        void SetItemRenderer(HandRenderer* items) { itemRenderer = items; }
        void ResolveSeats(std::vector<Entity>& entities) const;
        void Shutdown();
        void ClearCaches();

        struct FrameIn {
            double time = 0;
            float dt = 0.016f;

            float partialTick = 1.0f;
            glm::dvec3 camera{0.0};
            std::function<glm::vec3(const glm::dvec3&)> light;
            std::function<glm::ivec2(const glm::dvec3&)> lightLevels;
            bool firstPerson = true;
            float maxDistance = 96.0f;
            float cameraYaw = 0, cameraPitch = 0;
            double worldTicks = 0;
            const Frustum* frustum = nullptr;
            std::function<bool(int, int, int)> sectionReached;
            bool outline = false;
            struct Crack {
                int x = 0, y = 0, z = 0, stage = 0;
                std::vector<std::array<float, 6>> boxes;
            };
            std::vector<Crack> cracks;
            std::function<double(double, double, double)> groundY;
            bool chunkBorders = false;
            bool hitboxes = false;
            bool ownNametag = false;
            bool nametagBackground = true;
            bool nametagShadow = false;
            bool nametags = true;
            bool nametagDepth = false;
            SkinParts ownParts;
            glm::vec3 ownPose{0.0f};
            struct ModLine {
                glm::dvec3 a, b;
                uint32_t colour = 0xFFFFFFFFu;
                float width = 1.0f;
            };
            const std::vector<ModLine>* modLines = nullptr;

            struct ModStyle {
                bool hidden = false;
                glm::vec3 scale{1.0f};
                float tint[4] = {1, 1, 1, 1};
                std::string texture;
                int nameTag = 0;
                std::string nameText;
            };
            std::function<const ModStyle*(const Entity&)> modStyle;

            struct ModMesh {
                std::vector<UiVertex> vertices;
                glm::dvec3 origin{0.0};
                uint32_t texture = 0;
                bool depthTest = true;
            };
            const std::vector<ModMesh>* modMeshes = nullptr;

            struct AttachedMesh {
                std::vector<UiVertex> vertices;
                uint64_t entity = 0;
                int anchor = 0;
                uint32_t texture = 0;
                bool depthTest = true;
                bool hideInFirstPerson = true;
            };
            const std::vector<AttachedMesh>* attachedMeshes = nullptr;
            uint32_t outlineColor = 0x66000000u;
            bool skins3d = false;
            const std::vector<particle::Quad>* particles = nullptr;
            const particle::DataParticles* dataParticles = nullptr;

            struct FireSprite {
                glm::dvec3 pos{0.0};
                uint64_t runtimeId = 0;
                float width = 0.6f, height = 1.8f;
                bool soul = false;
            };
            const std::vector<FireSprite>* fire = nullptr;

            bool screenFire = false;
            particle::DataWorld dataWorld;
            std::function<bool(int, std::vector<uint8_t>&)> tilePixels;
            glm::dvec3 playerPos{0.0};
            float pixelScale = 0;
            float lineWidthPx = 2.5f;
            glm::dvec3 outlineLo{0.0}, outlineHi{0.0};
            std::vector<std::pair<glm::dvec3, glm::dvec3>> outlineShape;
            bool blobShadows = true;

            float ownAttackTime = -1.0f;
            bool selfShadow = false;
            int capePhysics = 1;
        };
        void Render(const std::vector<Entity>& entities, const FrameIn& in);
        void RenderBlockEntities(const std::vector<BlockEntityDraw>& draws, const FrameIn& in);

        struct FpState {
            float attackTime = 0, armHeight = 1, offArmHeight = 1;
            bool bob = true, using_ = false, blocking = false;
            float usedTicks = 0;
            glm::mat4 viewEffect{1.0f};
            glm::vec3 light{1.0f};
        };
        void RenderFirstPerson(const Entity& self, const FrameIn& in, const FpState& fp);
        void RenderDoll(const Entity& doll, const FrameIn& in, float cx, float feetY, float pxPerBlock,
                        const int clip[4]);

        size_t Drawn() const { return drawn; }
        size_t Considered() const { return considered; }
        size_t Unrenderable() const { return unrenderable; }
        size_t Culled() const { return culled; }

    private:
        std::vector<ParticleRequest> particleRequests;
        HandRenderer* itemRenderer = nullptr;
        struct View;
        struct Layer {
            std::vector<attachable::Quad> quads;
            std::string texture;
            bool translucent = false;
            float tint[4] = {1, 1, 1, 1};
            bool ignoreLighting = false;
            float lightMultiplier = 1.0f;

            float overlay[4] = {0, 0, 0, 0};
            float hurtColor[4] = {1.0f, 0.0f, 0.0f, 0.3f};
            float fireColor[4] = {0, 0, 0, 0};
            bool hasOverlay = false, hasFireColor = false;

            float uvOffset[2] = {0, 0}, uvScale[2] = {1, 1};
            bool uvAnim = false;
            MaterialInfo material;
            uint32_t maskRgb = 0xFFFFFF;

            bool body = true;
        };
        bool Build(const Entity& e, View& v, const FrameIn& in, std::vector<Layer>& layers, float& scale);
        void RunAnimations(const Entity& e, View& v, const ClientEntityDef& def, attachable::Pose& pose,
                           const FrameIn& in, const attachable::Geometry* bindGeometry);
        void Draw(const Entity& e, const View& v, const std::vector<Layer>& layers, float scale,
                  const FrameIn& in);
        const FpState* fp = nullptr;
        bool shadowOnlyPass = false;
        glm::vec3 modScale{1.0f};
        void DrawModMeshes(const FrameIn& in);
        void DrawAttachedMeshes(const std::vector<Entity>& entities, const FrameIn& in);
        const glm::mat4* fpModel = nullptr;
        const glm::mat4* dollModel = nullptr;
        const int* dollClip = nullptr;
        uint32_t TextureFor(const Entity& e, const std::string& key);
        void Track(const Entity& e, View& v, const FrameIn& in);
        void TickPlayerBody(const Entity& e, View& v, float dt, float prevYaw);
        void DrawShadow(const glm::dvec3& pos, float radius, float strength, const FrameIn& in);
        uint32_t shadowTexture = 0;
        struct Tag {
            std::string text;
            glm::dvec3 at;
            bool sneaking = false;
        };
        std::vector<Tag> tags;
        void DrawNametags(const FrameIn& in);
        uint32_t TextureByPath(const std::string& path);
        void DrawPainting(const Entity& e, const FrameIn& in);
        void DrawParticles(const FrameIn& in);
        int FlipbookFrames(const std::string& name);
        uint32_t particleSheet = 0;
        bool particleSheetTried = false;
        std::unordered_map<int, uint32_t> particleTiles;
        std::unordered_map<std::string, uint32_t> particleTextures;

        std::unordered_map<std::string, int> flipbookFrames;
        uint32_t fontTexture = 0;
        uint32_t fontTextureVersion = 0;

        RenderContext* renderer = nullptr;
        EntityAssets assets;
        std::unordered_map<uint64_t, std::unique_ptr<View>> views;
        std::unordered_map<std::string, uint32_t> textures;
        std::unordered_map<uint64_t, uint32_t> skinTextures, capeTextures, capeModelTextures;
        std::unordered_map<uint64_t, std::weak_ptr<const SkinData>> skinOwners;
        void PruneSkinCaches();
        std::unordered_map<uint64_t, std::unique_ptr<attachable::GeometryLibrary>> skinGeometry;
        std::unordered_map<uint64_t, std::unique_ptr<attachable::Geometry>> voxelGeometry;
        attachable::GeometryLibrary capeGeometries;
        std::vector<uint32_t> pendingMeshes;
        std::array<std::vector<attachable::Quad>, 11> orbGeometry;
        size_t drawn = 0, considered = 0, unrenderable = 0, culled = 0;
        struct Line {
            glm::dvec3 a, b;
            uint32_t colour;
            float width = 1.0f;
            uint32_t colourEnd = 0;
        };
        std::vector<Line> lines;
        void AddBox(const glm::dvec3& lo, const glm::dvec3& hi, uint32_t colour, float width = 1.0f);
        void AddShapeOutline(const std::vector<std::pair<glm::dvec3, glm::dvec3>>& boxes, uint32_t colour);
        void DrawCracks(const FrameIn& in);
        size_t outlineLineStart = 0;
        void DrawLines(const FrameIn& in);
        std::string loadedRoot;
        std::unordered_map<std::string, bool> warned;
    };
}
