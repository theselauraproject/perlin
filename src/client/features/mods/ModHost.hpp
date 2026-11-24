#pragma once
#include "client/systems/camera/Camera.hpp"
#include "client/systems/model/EntityTracker.hpp"
#include "client/systems/config/Options.hpp"
#include "client/systems/input/Input.hpp"
#include "client/systems/input/InputManager.hpp"
#include "client/systems/audio/SoundSystem.hpp"
#include "client/systems/particle/ParticleSystem.hpp"
#include "client/systems/player/LocalPlayer.hpp"
#include "client/systems/renderer/LevelRenderer.hpp"
#include "client/systems/gfx/RenderContext.hpp"
#include "client/ui/ChatLog.hpp"
#include "util/TaskGroup.hpp"
#include "client/systems/audio/Capture.hpp"
#include "client/systems/entity/EntityRenderer.hpp"
#include "client/systems/hand/HandRenderer.hpp"
#include "client/systems/mods/ModManager.hpp"
#include "client/systems/mods/ModUiRequest.hpp"
#include "client/systems/multiplayer/BedrockSession.hpp"
#include "client/systems/renderer/Fog.hpp"
#include "client/ui/Hud.hpp"
#include "util/ModSocket.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <glm/glm.hpp>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace perlin::gpu {
    class ModGpuHub;
}

namespace perlin {
    class BlockPalette;
    struct LocalBreakState {
        bool active = false;
        int x = 0, y = 0, z = 0;
        float progress = 0.0f;
    };

    class ModHost {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        struct ScreenFlags {
            bool death = false, inventory = false, workbench = false, pause = false, disconnect = false,
                 editor = false;
            int pauseView = 0;
        };
        struct FrameStats {
            int fps = 0;
            bool debugOn = false;
        };
        class Window {
        public:
            virtual ~Window() = default;
            virtual bool Available() const = 0;
            virtual void Quit() = 0;
            virtual void SetClipboard(const std::string& text) = 0;
            virtual std::string GetClipboard() = 0;
            virtual void OpenUrl(const std::string& url) = 0;
            virtual void SetTitle(const std::string& title) = 0;
            virtual void ReleaseCursor() = 0;
            virtual bool Focused() = 0;
            virtual bool Minimized() = 0;
        };

        struct Deps {
            mods::ModManager& manager;
            Window& window;
            mods::UiRequest& uiRequest;
            gpu::ModGpuHub& gpuHub;
            Camera& camera;
            Options& options;
            ui::HudState& hudState;
            std::unique_ptr<LevelRenderer>& levelRenderer;
            HandRenderer& handRenderer;
            Input& input;
            InputManager& inputManager;
            entity::EntityTracker& previewEntities;
            LocalPlayer& player;
            ui::ChatLog& chat;
            entity::EntityRenderer& entityRenderer;
            audio::SoundSystem& sounds;
            audio::AudioEngine& audioEngine;
            particle::ParticleSystem& particles;
            std::unique_ptr<RenderContext>& renderer;
            std::unique_ptr<BedrockSession>& session;
            TaskGroup& tasks;
            float& rainLevel;
            float& thunderLevel;
            std::function<ScreenFlags()> screenFlags;
            std::function<FrameStats()> frameStats;
            std::function<uint64_t()> hudTicks;
            std::function<bool()> replayHidesHud;
            std::function<bool()> replayPlaying;
            std::function<void()> reloadResources;
            std::function<void()> takeScreenshot;
            std::function<void()> applyTexturePacks;
            std::function<void()> applyShaderPack;
            std::function<void(const std::string&)> joinServer;
            std::function<LocalBreakState()> localBreak;
        };

        explicit ModHost(Deps d) : deps(std::move(d)) {}

        Deps deps;

        void SetupMods();
        void SetupModPlayerApi();
        void SetupModWorldApi();
        void RegisterBlockOps();
        void RegisterTerrainOps();
        void RegisterEntityOps();
        void SetupModClientApi();
        void SetupModInteractionApi();
        void SetupModActions();
        void SetupModAdvanced();
        void SetupModHooks();
        void RegisterHudHooks();
        void RegisterRenderHooks();
        void RegisterScreenHooks();
        void RegisterWorldHooks();
        void ToggleMod(const std::string& name);
        void PumpMods(BedrockSession* live, Level* level, ui::Hud* hudPtr, float dt);
        void TickMods(BedrockSession* live);
        bool InterceptOutgoing(BedrockSession* live, const std::string& text);
        bool FilterIncomingChat(const std::string& line);
        void UpdateModLayer(ui::Hud& hud);
        void ApplyModHud(ui::HudState& s);
        void NoteModSound(const std::string& name, double x, double y, double z, float volume, float pitch);
        void MergePluginCommands(BedrockSession* live);
        void StartModHttp(const std::string& plugin, uint32_t id, std::string method, std::string url,
                          int timeoutMs, std::vector<std::pair<std::string, std::string>> headers,
                          std::vector<uint8_t> body, std::string savePath = {});
        abi::Screen CurrentScreen() const;
        void ApplyModCamera(float& fov, glm::mat4& viewEffect);
        void DropModHooks(const std::string& plugin);
        void ApplyModDebugLines(std::vector<std::string>& left, std::vector<std::string>& right);
        void ApplyModFog(FogState& fog);

        bool ModClientTime(double& worldTicks);
        bool ModClientWeather(float& rain, float& thunder);
        HandSettings ModHand();

        std::optional<uint32_t> ModOutline();
        void DrawModTint(int fbW, int fbH);
        int ModFrameLimit(int fps) const;
        void PumpModHooks(BedrockSession* live);
        void SetupModExtra();
        void SetupModRev2();
        void PumpModRev2();
        void DropModRev2(const std::string& plugin);
        void RebuildBlockRemap();
        BlockPalette& ModPalette();
        bool DetachCamera(float dt, glm::vec3 eye, PerspectiveMode& mode, bool& showSelf);
        bool DetachTakesLook();
        bool CameraDetachActive();
        bool DetachedPlayerView(glm::vec3& eye, float& yaw, float& pitch);
        void DetachLook(float dyaw, float dpitch);
        void PrepareModBones(entity::EntityRenderer::FrameIn& in);
        void PumpModExtra(BedrockSession* live);
        void DropModExtra(const std::string& plugin);
        void ApplyModSlotOverlays(ui::HudState& state);
        void ApplyModSkins(std::vector<entity::Entity>& entities);
        void PrepareModRender(entity::EntityRenderer::FrameIn& in);
        bool DispatchModInteract(abi::InteractKind kind, int x, int y, int z, int face, uint64_t entity);
        bool DispatchModSlotClick(int slot, bool right, bool shift);
        bool DispatchModToast(const std::string& title, const std::string& body);
        void PumpModInput(mods::ModManager& mm);
        void DrawModGfx(int fbW, int fbH, bool hudBand);

        BedrockSession* modLive = nullptr;
        // Content the server announced over the mod channel (revision 3), for ContentList.
        std::vector<abi::ContentEntry> modContent;
        void PumpModChannel(BedrockSession* live, mods::ModManager& mm);
        Level* modLevel = nullptr;
        ui::Hud* modHud = nullptr;
        float modDt = 0.0f;
        int modGuiW = 0, modGuiH = 0;
        std::mutex modMutex;
        struct ModAdvanced {
            std::mutex mutex;
            abi::CameraControlState camera;
            TimePoint cameraUntil;
            abi::ModelPose pose;
            bool lookTaken = false;
            glm::dvec3 lookDir{0.0, 0.0, -1.0};
            TimePoint poseUntil;
            struct DrawSet {
                TimePoint until;
                std::vector<entity::EntityRenderer::FrameIn::ModLine> lines;
            };
            std::map<std::string, DrawSet> lines;
            std::map<std::string, std::pair<TimePoint, std::vector<uint32_t>>> drops;
            std::map<std::string, std::pair<TimePoint, std::vector<uint32_t>>> outDrops;
            std::array<float, 16> viewProjection{};
            std::vector<entity::EntityRenderer::FrameIn::ModLine> frameLines;
        } modAdv;
        struct ModRev2 {
            std::mutex mutex;
            std::string detachPlugin;
            abi::CameraDetachState detach;
            TimePoint detachUntil;
            bool detachActive = false;
            glm::dvec3 detachPos{0.0};
            glm::vec3 playerEye{0.0f};
            float playerYaw = 0.0f, playerPitch = 0.0f;
            float detachYaw = 0.0f, detachPitch = 0.0f;
            std::map<std::string, abi::BlockRemapData> remaps;
            size_t remapPaletteCount = 0;
            abi::BlockRemapStateData remapState;
            struct BoneSet {
                TimePoint until;
                std::vector<entity::EntityRenderer::FrameIn::ModBone> bones;
            };
            std::map<std::pair<std::string, uint64_t>, BoneSet> bones;
            std::unordered_map<uint64_t, std::vector<entity::EntityRenderer::FrameIn::ModBone>> frameBones;
            std::map<std::string, std::pair<TimePoint, abi::AnimationStyleData>> styles;
        } modRev2;
        struct ModHooks {
            using TimePoint = std::chrono::steady_clock::time_point;
            std::mutex mutex;
            std::map<std::string, std::map<abi::HudElement, abi::HudElementState>> hud;
            std::map<std::string, abi::DebugLinesData> debug;
            std::pair<TimePoint, abi::FogOverride> fog;
            std::pair<TimePoint, abi::ScreenTintData> tint;
            std::pair<TimePoint, abi::ClientTimeData> time;
            std::pair<TimePoint, abi::ClientWeatherData> weather;
            std::pair<TimePoint, abi::BlockOutlineData> outline;
            std::pair<TimePoint, abi::ViewModelData> viewModel;
            struct AttachedMesh {
                TimePoint until;
                abi::EntityMeshData data;
            };
            std::map<std::string, AttachedMesh> attachedMeshes;
            struct EntityRule {
                TimePoint until;
                abi::EntityOverride rule;
            };
            std::map<std::string, std::map<std::string, EntityRule>> entities;
            struct Mesh {
                TimePoint until;
                abi::WorldMeshData data;
            };
            std::map<std::string, Mesh> meshes;
            std::map<std::string, abi::FrameLimitData> frame;
            struct Button {
                std::string plugin;
                abi::ScreenButtonDef def;
            };
            std::vector<Button> buttons;
            std::string customOwner, customTitle;
            bool customOpen = false;
            std::vector<std::string> tooltipLines;
            std::set<std::string> loadingHidden;
            std::map<std::string, abi::SkinOverrideData> skins;
            uint64_t skinSerial = 0;
            std::map<std::string, abi::AmbientSoundData> ambient;
            std::vector<std::pair<std::string, abi::EventServerPing>> pings;
            std::atomic<uint32_t> nextPing{1};
            std::vector<std::pair<std::string, std::string>> packs;
            std::vector<abi::ParticleRequest> particles;
            bool windowFocused = true, windowMinimized = false;
        } modHooks;
        struct ModExtra {
            struct Overlay {
                TimePoint until;
                bool forever = true;
                abi::SlotOverlayData data;
            };
            std::mutex mutex;
            std::map<std::string, std::unique_ptr<util::SocketSet>> sockets;
            std::map<std::string, Overlay> overlays;
            std::map<std::string, std::set<uint32_t>> streams;
            audio::Capture capture;
            std::string micOwner;
            uint32_t micRate = 16000, micFrame = 320;
            std::vector<int16_t> micPending;
            std::string pendingJoin;
            std::atomic<float> pingMs{-1.0f};
            std::atomic<bool> pinging{false};
            TimePoint nextPing, rateAt, sweepAt;
            uint64_t rateIn = 0, rateOut = 0, rateBytesIn = 0, rateBytesOut = 0;
            float perSecond[4] = {0, 0, 0, 0};
        } modExtra;
        std::vector<entity::EntityRenderer::FrameIn::ModMesh> modFrameMeshes;
        std::vector<entity::EntityRenderer::FrameIn::AttachedMesh> modFrameAttached;
        struct ScreenButtonView {
            abi::ScreenButtonDef def;
            bool hover = false;
        };
        std::vector<ScreenButtonView> modScreenButtons;
        bool modLayerOnScreen = false;
        std::string modLayerButtons;
        std::unordered_map<uint64_t, entity::EntityRenderer::FrameIn::ModStyle> modFrameStyles;
        std::unordered_map<std::string, entity::EntityRenderer::FrameIn::ModStyle> modFrameTypeStyles;
        std::map<std::string, std::shared_ptr<const entity::SkinData>> modSkinCache;
        uint64_t modSkinCacheSerial = 0;
        std::vector<std::pair<std::string, abi::EventHttp>> modHttp;
        std::atomic<int> modHttpActive{0};
        std::vector<abi::EventSound> modSounds;
        std::map<std::string, bool> modKeyDown;
        std::atomic<bool> modWantPackets{false};
        bool modWasConnected = false, modWasDead = false;
        bool modGrabMouseWas = false, modGrabKeyboardWas = false;
        std::atomic<int> modFbW{0}, modFbH{0};
        std::mutex modNativeMutex;
        abi::NativeHandles modNative;
        bool modNativeReady = false;
        struct ModGfxTexture {
            uint32_t gpu = 0;
            uint64_t version = 0;
            int width = 0, height = 0;
        };
        std::unordered_map<std::string, ModGfxTexture> modGfxTextures;
        struct ModGfxDraw {
            uint32_t mesh = 0;
            uint32_t texture = 0;
            float clip[4] = {0, 0, 0, 0};
        };
        struct ModGfxCached {
            uint64_t serial = 0;
            const void* data = nullptr;
            bool hudBand = false;
            bool seen = false;
            std::vector<ModGfxDraw> draws;
        };
        std::unordered_map<std::string, ModGfxCached> modGfxCache;
        int modLastDim = 0, modLastSlot = -1;
        uint32_t modLastHurt = 0, modTitleSerial = 0;
        abi::Screen modLastScreen = abi::Screen::None;
        std::unordered_set<uint64_t> modEntityIds;
        bool modEntitySeeded = false;
        std::string modToastText, modTitleText, modSubtitleText;
        TimePoint modToastUntil, modTitleStart;
        int modTitleFadeIn = 500, modTitleStay = 3500, modTitleFadeOut = 1000;
        uint64_t modLayerVersion = 0;
        int modLayerW = 0, modLayerH = 0;
        bool modLayerShown = false;
        std::string modCommandSig;
        uint32_t modCommandsVersion = 0;
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> modPackets;
        mods::OpScope scope{deps.manager};
    };
}
