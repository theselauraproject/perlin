#pragma once
#include "network/FriendWorld.hpp"
#include "client/ui/CommandInfo.hpp"
#include "world/level/Level.hpp"
#include "network/PacketDispatcher.hpp"
#include "protocol/Packets.hpp"
#include "client/multiplayer/SkinLoader.hpp"
#include "client/entity/EntityTracker.hpp"
#include "network/XboxAuth.hpp"
#include "network/Transport.hpp"
#include "util/ThreadPool.hpp"
#include "network/PacketCipher.hpp"
#include "network/PlayerConstants.hpp"

using EC_KEY = struct ec_key_st;
#include "client/multiplayer/PackDownloader.hpp"

#include "client/multiplayer/BlockEntities.hpp"
#include "client/CameraEffects.hpp"
#include <array>
#include <utility>
#include <condition_variable>
#include <deque>
#include <optional>
#include <atomic>
#include <future>
#include <iostream>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace perlin {

    struct SubChunkData;
    class ClientIdentity;

    class BedrockSession {
    public:
        enum class State {
            Disconnected,
            Connecting,
            WaitingNetworkSettings,
            WaitingPlayStatus,
            ResourcePacks,
            InGame,
            Failed,
        };

        using PacketHandler = std::function<void(uint32_t id, const std::vector<uint8_t>& body)>;

        BedrockSession();
        ~BedrockSession();

        BedrockSession(const BedrockSession&) = delete;
        BedrockSession& operator=(const BedrockSession&) = delete;

        const std::string& PlayerName() const { return playerName; }
        void SetPlayerName(const std::string& name) {
            playerName = name.empty() ? std::string("Perlin") : name;
        }
        void SetTransport(const std::string& mode) { transportMode = mode; }
        // a ready transport (the embedded server's) used by the next Connect instead of dialling out
        void SetInjectedTransport(std::unique_ptr<Transport> transport) {
            injectedTransport = std::move(transport);
        }
        void SetFriendLease(std::shared_ptr<friendworld::Lease> lease) { friendLease = std::move(lease); }
        void SetSkin(LoadedSkin loaded) { skin = std::move(loaded); }
        const LoadedSkin& Skin() const { return skin; }
        entity::EntityTracker& Entities() { return entities; }
        // loginKey names the account (its auth file): logins are cached per account until the token expires
        void SetAccount(std::shared_ptr<xbox::Account> a, std::string loginKey = {}) {
            account = std::move(a);
            accountKey = std::move(loginKey);
        }
        // signs in ahead of the join so connecting skips Xbox Live, PlayFab and the Minecraft token
        static void PrewarmLogin(std::shared_ptr<xbox::Account> account, const std::string& loginKey);
        void SetChunkRadius(int r) { chunkRadius = r; }
        // join announcing another protocol / game version than perlin's own (servers that advertise an older one
        // but let newer clients in); 0 = perlin's own
        void SetProtocolOverride(int protocol, std::string version) {
            protocolOverride = protocol;
            versionOverride = std::move(version);
        }
        void RequestChunkRadiusNow() { SendRequestChunkRadius(); }
        void SetNearRadius(int r) { nearRadius = r; }
        int NearRadius() const { return nearRadius; }
        void SetMaxViewDistance(int r) { maxViewDistance = r; }

        bool Connect(const std::string& host, uint16_t port);
        void Disconnect();
        void SetPackDirectory(const std::string& dir) { packs.SetDirectory(dir); }
        std::vector<PackDownloader::PackFile> ServerPackFiles() const { return packs.Files(); }
        bool Transferring() const { return transferring; }
        // the server says its overworld is a flat one: the sky's horizon sits at the world floor instead of sea level
        bool FlatWorld() const { return flatWorld; }

        void Update();

        template <class P> bool Send(const P& packet) {
            std::vector<uint8_t> body;
            if (!net::EncodeChecked(packet, body)) {
                std::cout << "[net] refusing to send malformed " << P::kName << std::endl;
                return false;
            }
            return Transmit(P::kId, body, true);
        }
        template <class P> bool SendUnreliable(const P& packet) {
            std::vector<uint8_t> body;
            if (!net::EncodeChecked(packet, body)) {
                std::cout << "[net] refusing to send malformed " << P::kName << std::endl;
                return false;
            }
            return Transmit(P::kId, body, false);
        }

        template <class P, class Fn> void On(Fn&& fn) { dispatcher.On<P>(std::forward<Fn>(fn)); }
        template <class P, class Fn> void OnPartial(Fn&& fn) {
            dispatcher.OnPartial<P>(std::forward<Fn>(fn));
        }

        struct PlayerPose {
            float x = 0, y = 0, z = 0;
            float yaw = 0, pitch = 0, headYaw = 0;
            bool onGround = false;
            bool known = false;
        };
        PlayerPose GetPose() const { return pose; }
        uint64_t GetRuntimeId() const { return runtimeId; }
        bool SendPlayerAuthInput();
        bool SendPlayerAuthInputBlockAction(uint32_t action, int bx, int by, int bz, int face);
        void EndItemUseOn();
        bool TakeBlockNamesLearned() { return blockNamesLearned.exchange(false); }
        bool ServerAuthoritativeBlockBreaking() const { return serverAuthBreaking.load(); }

        struct ServerForm {
            uint32_t id = 0;
            std::string json;
        };
        struct BossBar {
            int64_t id = 0;
            std::string title;
            float progress = 1.0f;
            uint8_t color = 0;
            uint8_t overlay = 0;
        };
        struct ScoreRow {
            std::string name;
            int32_t score = 0;
            int64_t order = 0;
        };
        struct ScoreboardView {
            bool visible = false;
            std::string title;
            bool descending = true;
            std::vector<ScoreRow> rows;
        };
        struct ServerToast {
            std::string title, message;
        };
        bool TakeServerForm(ServerForm& out);
        bool TakeFormClosed() { return formClosed.exchange(false); }
        bool SendFormResponse(uint32_t id, const std::string* data, bool busy);
        bool SendServerSettingsRequest();
        std::vector<BossBar> BossBars() const;
        ScoreboardView Scoreboard() const;
        bool TakeToast(ServerToast& out);
        uint32_t InputLocks() const { return inputLocks.load() | (immobile.load() ? 4u | 64u : 0u); }
        uint32_t HiddenHud() const { return hiddenHud.load(); }
        std::atomic<bool> serverAuthBreaking{true};

        struct BlockActionData {
            uint32_t action = 0;
            int x = 0, y = 0, z = 0, face = 0;
        };
        struct MoveInput {
            std::vector<uint32_t> flags;
            float moveX = 0, moveZ = 0;
            float yaw = 0, pitch = 0;
            float eyeHeight = 1.62001f;
            float rawX = 0, rawZ = 0;
            float position[3] = {0, 0, 0};
            float delta[3] = {0, 0, 0};
            bool hasDelta = false;
            bool hasBlockAction = false;
            BlockActionData blockAction;
            // client-predicted vehicle: ActorRotation (x, y) and ActorUniqueID; sets InputData 45
            std::optional<std::array<float, 2>> vehicleRotation;
            std::optional<int64_t> predictedVehicle;
        };
        bool SendMoveInput(const MoveInput& input);

        // CorrectPlayerMovePredictionPacket with the vehicle prediction type
        struct VehicleCorrection {
            uint64_t tick = 0;
            float pos[3] = {0, 0, 0};
            float delta[3] = {0, 0, 0};
            float rotation[2] = {0, 0};
            std::optional<float> angularVelocity;
            bool onGround = false;
        };
        bool TakeVehicleCorrection(VehicleCorrection& out);
        // RiderSeatPosition (actor data 56) of the local player, when the server sent one
        std::optional<std::array<float, 3>> OwnSeatOffset() const {
            std::lock_guard lock(attrMutex);
            return ownSeatOffset;
        }
        // SetActorLink type 1: the local player sits in the controlling seat
        bool RidingAsDriver() const { return ridingDriver.load(); }
        // server moves of this actor are ignored while the client predicts it
        void SetPredictedVehicle(uint64_t runtime) { predictedVehicleRuntime = runtime; }

        struct ServerMotion {
            bool poseChanged = false;
            bool teleport = false;
            bool filterTravel = false;
            bool hasVelocity = false;
            bool hasRotation = false;
            float velocity[3] = {0, 0, 0};
            float yaw = 0, pitch = 0;
            bool correction = false;
            bool stampedVelocity = false;
            uint64_t tick = 0;
            bool onGround = false;
            double pos[3] = {0, 0, 0};
        };
        uint64_t LastAuthTick() const { return authTick; }
        uint64_t CorrectionCount() const { return correctionsReceived.load(); }
        struct CommandResult {
            uint64_t received = 0;
            int success = 0;
            std::string output;
        };
        CommandResult LastCommandResult() const {
            std::lock_guard lock(commandResultMutex);
            return commandResult;
        }
        bool TakeServerMotion(ServerMotion& out);
        float MovementSpeedAttribute() const {
            std::lock_guard lock(attrMutex);
            const auto it = ownAttributes.find("minecraft:movement");
            return it == ownAttributes.end() ? 0.1f : it->second;
        }
        // adds or removes the sprint modifier like the game's sprint action, then returns the instance value;
        // a freeze strength swaps the sent "Freeze effect" modifier for ours
        float PredictedMovementSpeed(bool sprinting, std::optional<float> freezeStrength = std::nullopt);
        // own FREEZING_EFFECT_STRENGTH (actor data 120) with the server tick it was set on
        struct FreezeUpdate {
            uint64_t tick = 0;
            float strength = 0;
        };
        std::vector<FreezeUpdate> TakeFreezeUpdates() {
            std::lock_guard lock(attrMutex);
            std::vector<FreezeUpdate> updates;
            updates.swap(freezeUpdates);
            return updates;
        }
        std::optional<proto::PlayerAttributesEntry> MovementAttribute() const {
            std::lock_guard lock(attrMutex);
            return movementAttribute;
        }
        bool ServerSprinting() const {
            std::lock_guard lock(attrMutex);
            return (ownFlagWords[0] & proto::FlagsMetadataFlags1::Sprinting) != 0;
        }
        bool ServerSwimming() const {
            std::lock_guard lock(attrMutex);
            return (ownFlagWords[0] & proto::FlagsMetadataFlags1::Swimming) != 0;
        }
        std::array<float, 2> ActorSize() const {
            std::lock_guard lock(attrMutex);
            return {ownWidth * ownScale, ownHeight * ownScale};
        }
        float UnderwaterMovementSpeedAttribute() const {
            std::lock_guard lock(attrMutex);
            const auto it = ownAttributes.find("minecraft:underwater_movement");
            return it == ownAttributes.end() ? 0.02f : it->second;
        }
        float LavaMovementSpeedAttribute() const {
            std::lock_guard lock(attrMutex);
            const auto it = ownAttributes.find("minecraft:lava_movement");
            return it == ownAttributes.end() ? 0.02f : it->second;
        }
        std::optional<float> AirDragAttribute() const {
            std::lock_guard lock(attrMutex);
            const auto it = ownAttributes.find("minecraft:air_drag_modifier");
            return it == ownAttributes.end() ? std::nullopt : std::optional<float>(it->second);
        }
        void ArmPredictionSync() {
            if (predictionSyncCountdown < 0)
                predictionSyncCountdown = 200;
        }
        bool SendPredictionSync();
        bool Initialized() const { return initializedSent; }
        void NoteBrokenSupport(int x, int y, int z, std::vector<std::pair<int, uint16_t>> above) {
            std::lock_guard lock(supportMutex);
            brokenSupports.push_back({x, y, z, std::move(above), std::chrono::steady_clock::now()});
        }
        void QueueInputFlag(uint32_t flag) {
            std::lock_guard lock(pendingInputMutex);
            pendingInputFlags.push_back(flag);
        }
        void PruneChunks(int centerChunkX, int centerChunkZ, int radiusChunks);
        bool SendLoadingScreen(bool start, std::optional<uint32_t> id = std::nullopt);
        bool SendPlayerAuthInputWithFlags(const std::vector<uint32_t>& flags, float pitchOverride,
                                          float yawOverride, uint32_t action = UINT32_MAX, int bx = 0,
                                          int by = 0, int bz = 0, int face = 1);
        bool SendChat(const std::string& message);
        uint64_t PacketsSent() const { return txPackets; }
        uint64_t PacketsReceived() const { return diagPackets; }
        std::vector<std::string> TakeChat();
        struct SoundRequest {
            enum class Kind { Named, Level, Stop } kind = Kind::Named;
            std::string name;
            float x = 0, y = 0, z = 0;
            float volume = 1, pitch = 1;
            int32_t extra = 0;
            std::string entityType;
            bool baby = false, global = false, stopAll = false;
        };
        std::vector<SoundRequest> TakeSounds();
        struct ParticleEvent {
            int event = 0;
            double x = 0, y = 0, z = 0;
            int32_t data = 0;
            std::string name;
            std::string molang;
        };
        std::vector<ParticleEvent> TakeParticles();
        struct PendingPlace {
            int x = 0, y = 0, z = 0;
            std::chrono::steady_clock::time_point at;
            bool active = false;
            int32_t held = 0;
        };
        bool SendCommand(const std::string& command);
        bool SendSettingsCommand(const std::string& command);
        bool SendSkinUpdate();
        std::string lastSentSkinName;
        // predicted xp orb pickup (TakeItemEntityPacket): dedupe server SoundOrb echo
        uint64_t lastLocalOrbMs = 0;
        float lastOrbX = 0.0f, lastOrbY = 0.0f, lastOrbZ = 0.0f;
        uint32_t CommandsVersion() const {
            std::lock_guard<std::mutex> lk(commandMutex);
            return commandVersion;
        }
        std::vector<ui::CommandInfo> Commands() const {
            std::lock_guard<std::mutex> lk(commandMutex);
            return commands;
        }
        struct TabPlayer {
            std::string uuid, name;
            std::shared_ptr<const entity::SkinData> skin;
            int64_t uid = 0;
            int perm = -1;
        };
        std::vector<TabPlayer> PlayerList() const {
            std::lock_guard<std::mutex> lk(tabMutex);
            return tabPlayers;
        }
        void ReloadChunks();
        bool SendTickSync(uint64_t timestamp);
        struct BlockCrack {
            int x, y, z, stage;
        };
        std::vector<BlockCrack> Cracks();
        void ClearCrack(int x, int y, int z);
        bool SendPlayerAction(uint32_t action, int bx, int by, int bz, int face = 1);
        void RequestRespawn();
        bool DeathPending() const {
            std::lock_guard<std::mutex> lk(deathMutex);
            return deathPending;
        }
        void ClearDeath() {
            std::lock_guard<std::mutex> lk(deathMutex);
            deathPending = false;
        }
        std::string DeathCause() const {
            std::lock_guard<std::mutex> lk(deathMutex);
            return deathCause;
        }
        void SetPose(float x, float y, float z, float yaw, float pitch);
        bool HaveBlockPos() const { return haveBlockPos; }
        bool GetWorldTime(int64_t& outDay, int& outTicksOfDay) const;
        bool GetWorldTicks(double& outTicks) const;
        void SetDayCycle(bool running);
        struct WorldSettings {
            std::map<std::string, int> rules;
            int difficulty = 2;
            int worldGameMode = 0;
            bool commandsEnabled = true;
            bool known = false;
        };
        struct AdventureSettings {
            // showNameTags is kept for completeness only: the game draws name tags whatever it says.
            bool noPvm = false, noMvp = false, immutableWorld = false, showNameTags = true, autoJump = true;
        };
        // OnScreenTextureAnimation: effect id (-1 = none) and arrival time
        static constexpr int kTotemScreenAnimation = 1000; // not an effect id: totem item
        struct ScreenAnimation {
            int effect = -1;
            std::chrono::steady_clock::time_point started;
        };
        ScreenAnimation TakeScreenAnimation() {
            std::lock_guard<std::mutex> lk(worldMutex);
            return std::exchange(screenAnimation, ScreenAnimation{});
        }
        // ShowCredits: server waits for status 1 before respawning us
        bool TakeCreditsRequest() { return creditsRequested.exchange(false); }
        bool SendCreditsFinished();
        AdventureSettings Adventure() const {
            std::lock_guard<std::mutex> lk(worldMutex);
            return adventure;
        }
        WorldSettings World() const {
            std::lock_guard<std::mutex> lk(worldMutex);
            return world;
        }
        int OwnPermission() const {
            std::lock_guard<std::mutex> lk(tabMutex);
            const auto it = permByUid.find(uniqueId);
            return it == permByUid.end() ? -1 : it->second;
        }
        int DimensionId() const { return dimensionId; }
        // chunk column arrived/left for mods; no-op without listeners
        void NoteModChunk(bool loaded, int x, int z) const;
        bool DimensionChanging() const { return dimAckPending.load(); }
        uint32_t HurtCount() const { return hurtCounter.load(); }
        struct HurtSource {
            bool known = false;
            float dx = 0, dz = 0;
            uint32_t serial = 0;
        };
        HurtSource LatestHurtSource() const {
            std::lock_guard lock(hurtMutex);
            return hurtSource;
        }
        float RainTarget() const { return rainTarget; }
        float ThunderTarget() const { return thunderTarget; }
        void NoteTime(int64_t totalOrTod);
        void NoteClock(int64_t time, bool paused);
        void GetBlockPos(int out[3]) const {
            out[0] = lastBlockPos[0];
            out[1] = lastBlockPos[1];
            out[2] = lastBlockPos[2];
        }
        bool GetSpawn(float out[3]) const {
            if (!pose.known)
                return false;
            out[0] = pose.x;
            out[1] = pose.y;
            out[2] = pose.z;
            return true;
        }

        void SetPacketHandler(PacketHandler h) { onPacket = std::move(h); }
        void SetDroppedPackets(std::vector<uint32_t> ids) {
            std::lock_guard<std::mutex> lk(dropMutex);
            dropped.assign(ids.begin(), ids.end());
            dropActive.store(!dropped.empty(), std::memory_order_release);
        }
        std::vector<std::pair<uint32_t, uint32_t>> PacketCounts() {
            std::lock_guard<std::mutex> lk(idCountMutex);
            std::vector<std::pair<uint32_t, uint32_t>> out;
            out.reserve(idCounts.size());
            for (const auto& [id, n] : idCounts)
                out.emplace_back(id, static_cast<uint32_t>(n));
            return out;
        }
        bool SendRaw(uint32_t id, const std::vector<uint8_t>& body) { return Transmit(id, body, true); }
        void SetDroppedOutgoing(std::vector<uint32_t> ids) {
            std::lock_guard<std::mutex> lk(dropMutex);
            droppedOut.assign(ids.begin(), ids.end());
            dropOutActive.store(!droppedOut.empty(), std::memory_order_release);
        }
        void InjectIncoming(uint32_t id, const std::vector<uint8_t>& body) { OnGamePacket(id, body); }
        using RecordTap = std::function<void(int dir, uint32_t id, const std::vector<uint8_t>& body)>;
        void SetRecordTap(RecordTap t) {
            std::lock_guard<std::mutex> lk(tapMutex);
            recordTap = std::move(t);
            tapActive.store(static_cast<bool>(recordTap), std::memory_order_release);
        }
        void BeginReplayMode(const std::string& name, uint64_t localRuntimeId, int protocol) {
            replayMode = true;
            playerName = name;
            runtimeId = localRuntimeId;
            serverProtocol = protocol;
            state = State::InGame;
            netLevel.EnableUndo(static_cast<size_t>(768) << 20);
        }
        void AdoptWorld(BedrockSession& old) {
            old.WaitChunkWorkIdle();
            netLevel.TakeFrom(old.netLevel);
            blockEntities.TakeFrom(old.blockEntities);
        }
        void WaitChunkWorkIdle() {
            for (int i = 0; i < 20000 && !ChunkWorkIdle(); i++)
                std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        void ReplayEpoch(int64_t epoch) {
            if (epoch == netLevel.Epoch())
                return;
            WaitChunkWorkIdle();
            netLevel.SetEpoch(epoch);
        }
        bool ReplayCanRewind(int64_t epoch) const { return netLevel.CanRewindTo(epoch); }
        bool ReplayRewindWorld(int64_t epoch) {
            WaitChunkWorkIdle();
            return netLevel.RewindTo(epoch);
        }
        bool IsReplay() const { return replayMode; }
        struct ReplayClientEvent {
            enum class Kind { Swing, BlockAction, Hotbar } kind = Kind::Swing;
            uint32_t action = 0;
            int x = 0, y = 0, z = 0, face = 0;
            int slot = 0;
        };
        void ReplaySelectSlot(int slot) {
            std::lock_guard lock(statusMutex);
            inventory.selected = std::clamp(slot, 0, 8);
        }
        void InjectClientPacket(uint32_t id, const std::vector<uint8_t>& body);
        void TakeReplayClientEvents(std::vector<ReplayClientEvent>& out) {
            out.insert(out.end(), replayClientEvents.begin(), replayClientEvents.end());
            replayClientEvents.clear();
        }
        bool ChunkWorkIdle() {
            std::lock_guard<std::mutex> lk(chunkMutex);
            return chunkIn.empty() && !chunkBusy;
        }
        std::string ServerAddress() const { return serverAddress; }
        std::string GameVersion() const { return gameVersion; }

        State GetState() const { return state; }
        PackDownloader::Progress PackProgress() const { return packs.GetProgress(); }
        std::string LastError() const { return lastError; }
        struct DisconnectInfo {
            bool happened = false;
            int reason = -1;
            std::string message;
            bool connectionLost = false;
            // version mismatch: 1 = we're too old, 2 = server too old
            int outdated = 0;
            std::string serverVersion;
            int serverProtocol = 0;
        };
        DisconnectInfo GetDisconnectInfo() const {
            std::lock_guard<std::mutex> lk(disconnectMutex);
            return disconnectInfo;
        }
        bool RequestReconnect();
        int ServerProtocol() const { return serverProtocol; }
        bool MayFly() const { return mayFly; }
        bool Instabuild() const { return instabuild; }
        bool IsFlying() const { return isFlying; }
        // bumped by every UpdateAbilities: the game applies the whole set, not only changes
        uint32_t AbilitiesSeq() const { return abilitiesSeq; }
        // Player::setPlayerGameType 0x1402b9ab0 -> base ability layer 0x1402e1980 (5: the world's type);
        // creative keeps flying, survival and adventure drop it; spectator lives in its own layer
        void ApplyGameTypeAbilities(int worldGameType) {
            const int type = gameType == 5 ? worldGameType : gameType;
            if (type == 1) {
                mayFly = true;
                instabuild = true;
                noClip = false;
            } else if (type == 0 || type == 2) {
                mayFly = false;
                instabuild = false;
                isFlying = false;
                noClip = false;
            }
        }
        // entity flag 32 (gliding), last from server for local player
        // actor flag 75 (sleeping): extended flag word
        // local invisibility (actor flag 5) from our SetActorData
        bool Invisible() const {
            std::lock_guard lock(attrMutex);
            return (ownFlagWords[0] & (1ull << 5)) != 0;
        }
        bool Sleeping() const {
            std::lock_guard lock(attrMutex);
            return (ownFlagWords[1] & proto::FlagsMetadataFlags2::Sleeping) != 0 || (ownPlayerFlags & 2) != 0;
        }
        bool SendStopSleeping() { return SendPlayerAction(6, 0, 0, 0, 0); }
        bool ServerGliding() const {
            std::lock_guard<std::mutex> lk(attrMutex);
            return ((ownFlagWords[0] >> 32) & 1ull) != 0;
        }
        bool PushTowardsClosestSpace() const {
            std::lock_guard<std::mutex> lk(attrMutex);
            return (ownFlagWords[1] & proto::FlagsMetadataFlags2::PushTowardsClosestSpace) != 0;
        }
        std::array<uint64_t, 2> OwnActorFlags() const {
            std::lock_guard lock(attrMutex);
            auto flags = ownFlagWords;
            if (!ownFlagWordsKnown[0])
                flags[0] |= proto::FlagsMetadataFlags1::AffectedByGravity;
            return flags;
        }
        bool Spectator() const { return gameType == 6 || gameType == 3 || gameType == 4 || noClip; }
        bool NoClip() const { return noClip || gameType == 6; }
        int GameType() const { return gameType; }
        float FlySpeed() const { return flySpeed; }
        float VFlySpeed() const { return vFlySpeed; }
        float WalkSpeed() const { return walkSpeed > 0 ? walkSpeed : 0.1f; }
        uint64_t CurrentServerTick() const { return serverTick; }
        bool HaveStartPos() const { return haveStartPos; }
        void GetStartPos(float out[3]) const {
            out[0] = startPos[0];
            out[1] = startPos[1];
            out[2] = startPos[2];
        }
        bool ColumnSettled(int cx, int cz) const;
        bool HaveWorldSpawn() const { return haveWorldSpawn; }
        void GetWorldSpawn(int out[3]) const {
            out[0] = worldSpawn[0];
            out[1] = worldSpawn[1];
            out[2] = worldSpawn[2];
        }

        struct ItemStackInfo {
            std::string name;
            int count = 0;
            int aux = 0;
            int damage = 0;
            uint32_t blockRuntimeId = 0;
            bool enchanted = false;
            std::string customName;
            bool hasColor = false;
            int32_t color = 0;
            int nbtTags = 0;
            std::vector<std::pair<int, int>> enchants;
            std::vector<std::string> lore;
            int networkId = 0;
            bool Empty() const { return name.empty() || count <= 0; }
        };
        struct Vitals {
            float health = 20.0f, maxHealth = 20.0f, absorption = 0.0f;
            float hunger = 20.0f, saturation = 5.0f;
            int level = 0;
            float xpProgress = 0.0f;
            int air = 300, maxAir = 300;
            bool poison = false, wither = false, regeneration = false, hungerEffect = false;
            bool onFire = false;
            bool known = false;
        };
        struct InventoryView {
            ItemStackInfo hotbar[9];
            ItemStackInfo main[27];
            ItemStackInfo offhand;
            ItemStackInfo armor[4];
            ItemStackInfo craft[4];
            ItemStackInfo work[9];
            ItemStackInfo output;
            ItemStackInfo cursor;
            int selected = 0;
            ItemStackInfo horse[17];
            bool horseOpen = false;
            int horseChestSlots = 0;
            int64_t horseEntity = 0;
            ItemStackInfo chest[54];
            bool chestOpen = false;
            int chestSlots = 0;
            std::string chestTitle;
        };
        static constexpr int kInvArmor0 = 36, kInvOffhand = 40, kInvCraft0 = 41, kInvOutput = 45,
                             kInvCursor = 46, kInvWork0 = 47, kInvSlots = 56, kInvHorse0 = 56,
                             kInvHorseCount = 17, kInvChest0 = 73, kInvChestCount = 54, kInvTotal = 127;
        static bool IsCraftAddr(int a) {
            return (a >= kInvCraft0 && a < kInvOutput) || (a >= kInvWork0 && a < kInvSlots);
        }
        struct CreativeGroup {
            int category = 0;
            std::string name;
            ItemStackInfo icon;
        };
        struct CreativeItem {
            uint32_t entryId = 0;
            ItemStackInfo info;
            int group = 0;
        };
        struct CreativeCatalog {
            std::vector<CreativeGroup> groups;
            std::vector<CreativeItem> items;
            uint32_t version = 0;
        };
        CreativeCatalog Creative() const;
        uint32_t CreativeVersion() const;
        void InvCreativeTake(uint32_t entryId, int mode);
        void InvDestroyCursor();
        void InvClick(int addr, bool right);
        void InvShiftClick(int addr);
        void InvHotbarKey(int addr, int hotbar);
        void InvDropCursor(bool whole);
        void InvDropSlot(int addr, bool whole);
        void InvReturnCursor();
        void InvPump();
        bool InvBusy() const;
        struct TitleText {
            std::string title, subtitle, actionBar;
            std::chrono::steady_clock::time_point titleAt, actionAt;
            int fadeIn = 10, stay = 70, fadeOut = 20;
            uint32_t serial = 0;
        };
        Vitals GetVitals() const;

        struct LocatorWaypoint {
            std::array<uint8_t, 16> id{};
            bool visible = true;
            bool hasPosition = false;
            double x = 0, y = 0, z = 0;
            int32_t dimension = 0;
            bool hasColor = false;
            int32_t color = 0;
            bool hasActor = false;
            int64_t actorId = 0;
        };
        std::vector<LocatorWaypoint> GetLocatorWaypoints() const {
            std::lock_guard lock(locatorMutex);
            return locatorWaypoints;
        }
        InventoryView GetInventory() const;
        int WaterWalkSpeedEnchant() const;
        int SoulSpeedEnchant() const;
        int SwiftSneakEnchant() const;
        TitleText GetTitleText() const;
        bool SelectHotbarSlot(int slot);
        bool SendSwingArm(const char* source = "build");

        bool PredictPlace(int x, int y, int z, int32_t heldRuntimeId);
        bool PredictBridgePlace(int x, int y, int z, int32_t heldRuntimeId, int face, const float click[3]);
        // repeat: a held build repeat (GameMode::continueBuildBlock), sent with the simulation-tick trigger
        bool SendUseItemOnBlock(int bx, int by, int bz, int face, const float click[3], const float eye[3], int use, const int dest[3], bool repeat = false);
        bool SendUseItemInAir(const float eye[3], bool held);
        bool SendReleaseItem(const float eye[3]);
        bool SendAttackEntity(uint64_t targetRuntimeId, const float eye[3], const float click[3]);
        bool SendInteractEntity(uint64_t targetRuntimeId, const float eye[3], const float click[3]);
        bool SendLeaveVehicle();
        int64_t RidingUid() const { return ridingUid.load(); }
        bool SendPickBlock(int bx, int by, int bz);
        bool SendPickEntity(uint64_t runtimeId);
        bool SendDropSelected(int count);
        bool OpenInventory();
        bool OpenMountInventory();
        bool CloseInventory();
        bool SwapInventorySlots(int a, int b);
        bool DropInventorySlot(int slot, bool whole);
        struct EffectState {
            int id = 0;
            int amplifier = 0;
            int ticks = 0;
            bool ambient = false;
            bool particles = true;
        };
        std::vector<EffectState> GetEffects() const;
        struct EffectUpdate {
            uint64_t tick = 0;
            int id = 0, amplifier = 0;
            bool active = false;
            int32_t duration = 0;
        };
        std::vector<EffectUpdate> TakeEffectUpdates();
        struct MotionUpdate {
            uint64_t tick = 0;
            std::array<double, 3> velocity{};
        };
        struct ServerMovementEffect {
            uint32_t type = 0;
            int32_t duration = 0;
            uint64_t tick = 0;
        };
        std::vector<ServerMovementEffect> TakeMovementEffects();
        bool WorkbenchOpen() const {
            std::lock_guard lock(statusMutex);
            return workbenchOpen;
        }
        bool TakeWorkbenchOpened();
        bool TakeHorseOpened();
        bool TakeChestOpened();
        bool ChestOpen() const {
            std::lock_guard lock(statusMutex);
            return inventory.chestOpen;
        }
        bool HorseOpen() const {
            std::lock_guard lock(statusMutex);
            return inventory.horseOpen;
        }
        struct RecipeEntry {
            int index = 0;
            bool craftable = false;
            ItemStackInfo out;
        };
        std::vector<RecipeEntry> GetRecipes(int side) const;
        void InvFillRecipe(int index);
        void InvSpread(const std::vector<int>& slots, int mode);
        void InvCreativeClone(int addr);
        void InvCreativePlace(int addr);
        uint32_t CreativeEntryFor(const ItemStackInfo& info) const;
        bool InventoryIsOpen() const {
            std::lock_guard lock(statusMutex);
            return inventoryOpen;
        }
        bool TakeServerClosedInventory();

        struct SignEditRequest {
            int x = 0, y = 0, z = 0;
            bool front = true;
        };
        struct MapView {
            int width = 128, height = 128;
            std::vector<uint32_t> pixels;
            bool locked = false;
            uint64_t revision = 0;
        };
        struct NpcDialogueView {
            uint64_t entityId = 0;
            std::string dialogue, screenName, npcName, actionJson;
            bool open = true;
        };
        struct TradeView {
            int windowId = 0;
            std::string title;
            std::vector<uint8_t> offers;
            int64_t villagerUid = 0;
            bool newUi = false;
        };
        CameraEffects& Camera() { return cameraEffects; }
        bool TakeSignRequest(SignEditRequest& out);
        bool SendSignText(int x, int y, int z, bool front, const std::string& text);
        int WindowProperty(int window, int property, int fallback) const;
        bool TakeNpcDialogue(NpcDialogueView& out);
        bool TakeTrade(TradeView& out);
        std::optional<MapView> Map(int64_t id) const;
        Level& NetLevel() { return netLevel; }
        BlockEntityStore& BlockEntities() { return blockEntities; }
        const BlockEntityStore& BlockEntities() const { return blockEntities; }
        bool HasSpawn() const { return gotSpawn; }
        int ChunksReceived() const { return chunksReceived; }
        void PumpRequests(int budget);
        int PendingSubChunkRequests() const;

        static const char* PacketName(uint32_t id);

    private:
        bool PredictPlaceState(int x, int y, int z, int32_t runtimeId, bool resolved);
        void HandleBatch(const std::vector<uint8_t>& msg);
        void OnGamePacket(uint32_t id, const std::vector<uint8_t>& body);
        void RegisterPacketHandlers();
        void ParseBlockEntities(const uint8_t* data, size_t size, bool scan);
        BlockEntityStore blockEntities;
        void RegisterServerUiHandlers();
        void RegisterWorldExtraHandlers();
        void RegisterProtocolExtras();
        void RegisterIgnoredHandlers();
        struct PropertyDef {
            std::string name;
            int type = 0;
            std::vector<std::string> values;
        };
        void ParsePropertySchema(const std::vector<uint8_t>& nbt);
        void ApplyEntityProperties(uint64_t rid, const std::string& type,
                                   const proto::EntityProperties& props);
        mutable std::mutex propertyMutex;
        std::unordered_map<std::string, std::vector<PropertyDef>> propertySchemas;
        CameraEffects cameraEffects;
        struct CameraPreset {
            std::string name, parent;
            std::optional<glm::vec3> position;
            std::optional<glm::vec2> rotation;
            std::optional<glm::vec2> offset;
            std::optional<glm::vec3> entityOffset;
        };
        std::vector<CameraPreset> cameraPresets;
        mutable std::mutex worldExtraMutex;
        std::deque<SignEditRequest> signRequests;
        std::map<uint32_t, SignEditRequest> signForms;
        uint32_t signFormSerial = 0;
        std::map<int, std::map<int, int>> windowData;
        std::map<int64_t, MapView> maps;
        std::deque<NpcDialogueView> npcDialogues;
        std::optional<TradeView> pendingTrade;
        std::map<int64_t, glm::vec3> trackedLocations;
        std::atomic<uint64_t> syntheticEntityId{0x7000000000ull};
        std::atomic<int> gameCreditsStatus{0};
        std::atomic<int> controlsScheme{0};
        mutable std::mutex serverUiMutex;
        std::deque<ServerForm> pendingForms;
        std::atomic<bool> formClosed{false};
        std::vector<BossBar> bossBars;
        struct Objective {
            std::string name, display;
            int sortOrder = 0;
        };
        std::map<std::string, Objective> objectives;
        std::string sidebarObjective;
        struct ScoreKey {
            std::string objective;
            int64_t id = 0;
            bool operator<(const ScoreKey& o) const {
                return objective != o.objective ? objective < o.objective : id < o.id;
            }
        };
        struct ScoreValue {
            std::string name;
            int32_t score = 0;
            int64_t entity = 0;
        };
        std::map<ScoreKey, ScoreValue> scoreValues;
        std::map<int64_t, int64_t> scoreIdentities;
        std::deque<ServerToast> toasts;
        std::atomic<uint32_t> inputLocks{0};
        std::atomic<bool> immobile{false};
        std::atomic<bool> legacyInventory{false};
        std::atomic<uint32_t> hiddenHud{0};
        net::PacketDispatcher dispatcher;
        bool SendRequestNetworkSettings();
        bool SendLogin();
        bool SendResourcePackResponse(proto::ResourcePackClientResponsePacketResponseStatus status);
        bool SendRequestChunkRadius();
        bool SendClientCacheStatus();
        bool SendSetLocalPlayerAsInitialized();
        void FinishLoadingIfReady();

        bool Transmit(uint32_t packetId, const std::vector<uint8_t>& body, bool reliable);
        void HandleLevelChunk(const proto::LevelChunkPacket& packet);
        void HandleSubChunk(const proto::SubchunkPacket& packet);
        void QueueSubChunkRequests(int dim, int cx, int cz, int firstSubY, int count, bool far = false);
        void UpdateColumnLod();
        void FillUnstreamedColumns();
        bool clientGen = false;
        bool flatWorld = false;
        int maxViewDistance = 32;
        std::atomic<int> grantedRadius{0};
        std::atomic<int64_t> lastColumnMs{0};
        std::chrono::steady_clock::time_point lastFill;
        uint64_t statFilled = 0;
        bool ApplySubChunk(int cx, int cz, int absSubY, const SubChunkData& sc);
        void ApplyBiomeColumn(int cx, int cz, const std::vector<uint8_t>& payload);

        void ChunkWorkerMain();
        void StartChunkWorker();
        void StopChunkWorker();
        std::thread chunkThread;
        std::unique_ptr<ThreadPool> decodePool;
        std::mutex chunkMutex;
        std::condition_variable chunkCv;
        std::deque<std::pair<uint32_t, std::vector<uint8_t>>> chunkIn;
        bool chunkStop = false;
        bool chunkBusy = false;

        bool ConnectImpl(const std::string& host, uint16_t port, bool transfer);
        void BeginTransfer();
        void ResetForTransfer();
        PackDownloader packs;
        std::future<std::pair<bool, std::string>> authTask;
        size_t packsExpected = 0;
        std::atomic<bool> transferring{false};
        std::atomic<bool> transferRequested{false};
        std::thread transferThread;
        std::thread::id transferThreadId;
        std::mutex updateMutex;
        std::string transferHost;
        uint16_t transferPort = 0;
        std::string lastHost;
        uint16_t lastPort = 0;
        mutable std::mutex disconnectMutex;
        DisconnectInfo disconnectInfo;
        void SetDisconnectInfo(int reason, const std::string& message, bool lost);
        void SetOutdated(int who);
        bool usedRak = false;
        std::unique_ptr<Transport> injectedTransport;

        std::unique_ptr<Transport> net;
        std::unique_ptr<PacketCipher> cipher;
        std::mutex txMutex;
        std::string transportMode = "auto";
        std::unique_ptr<ClientIdentity> identity;
        std::atomic<State> state{State::Disconnected};
        std::string lastError;
        PacketHandler onPacket;
        RecordTap recordTap;
        std::mutex tapMutex;
        std::atomic<bool> tapActive{false};
        bool replayMode = false;
        net::PacketDispatcher replayClientDispatcher;
        bool replayClientReady = false;
        std::vector<ReplayClientEvent> replayClientEvents;
        std::mutex dropMutex;
        std::vector<uint32_t> dropped;
        std::atomic<bool> dropActive{false};
        std::vector<uint32_t> droppedOut;
        std::atomic<bool> dropOutActive{false};

        Level netLevel;
        std::atomic<bool> blockNamesLearned{false};
        std::atomic<bool> itemUseOnActive{false};
        int itemUseDest[3] = {0, 0, 0};
        std::atomic<bool> haveLoggedInteraction{false};
        mutable std::mutex placeMutex;
        PendingPlace pendingPlace;
        void ConfirmPlace(int x, int y, int z, int32_t runtimeId);
        struct PredictedPlace {
            int x = 0, y = 0, z = 0;
            uint16_t id = 0, previous = 0;
            std::chrono::steady_clock::time_point at;
        };
        std::mutex predictMutex;
        std::vector<PredictedPlace> predictedPlaces;
        void ExpirePredictions();
        void NoteUnknownBlock(int32_t networkId);
        std::atomic<bool> gotSpawn{false};
        std::atomic<int> chunksReceived{0};
        std::mutex reqMutex;
        mutable std::mutex tabMutex;
        mutable std::mutex deathMutex;
        std::string deathCause;
        bool deathPending = false;
        std::vector<TabPlayer> tabPlayers;
        std::map<int64_t, int> permByUid;
        mutable std::mutex worldMutex;
        WorldSettings world;
        AdventureSettings adventure;
        ScreenAnimation screenAnimation;
        std::atomic<bool> creditsRequested{false};
        mutable std::mutex commandMutex;
        std::vector<ui::CommandInfo> commands;
        uint32_t commandVersion = 0;
        struct CrackState {
            int x = 0, y = 0, z = 0;
            double progress = 0.0;
            double perSecond = 0.0;
            std::chrono::steady_clock::time_point since;
        };
        std::mutex crackMutex;
        std::vector<CrackState> crackStates;
        int lastClearedCrack[3] = {0, 0, 0};
        std::chrono::steady_clock::time_point lastClearedAt;
        std::set<std::tuple<int, int, int, int>> requestedSlots;
        class NearestQueue {
        public:
            using Key = std::tuple<int, int, int, int>;
            NearestQueue() : set(Less{this}) {}
            NearestQueue(const NearestQueue&) = delete;
            NearestQueue& operator=(const NearestQueue&) = delete;
            NearestQueue& operator=(NearestQueue&&) {
                set.clear();
                return *this;
            }
            void SetCenter(int x, int z) {
                if (x == cx && z == cz)
                    return;
                std::vector<Key> all(set.begin(), set.end());
                set.clear();
                cx = x;
                cz = z;
                set.insert(all.begin(), all.end());
            }
            void push(const Key& k) { set.insert(k); }
            bool empty() const { return set.empty(); }
            size_t size() const { return set.size(); }
            Key front() const { return *set.begin(); }
            void pop() { set.erase(set.begin()); }

        private:
            struct Less {
                const NearestQueue* q;
                bool operator()(const Key& a, const Key& b) const {
                    const auto d = [this](const Key& k) {
                        const long long dx = std::get<1>(k) - q->cx, dz = std::get<3>(k) - q->cz;
                        return dx * dx + dz * dz;
                    };
                    const long long da = d(a), db = d(b);
                    if (da != db)
                        return da < db;
                    return std::tie(std::get<0>(a), std::get<1>(a), std::get<3>(a), std::get<2>(a)) <
                           std::tie(std::get<0>(b), std::get<1>(b), std::get<3>(b), std::get<2>(b));
                }
            };
            int cx = 0, cz = 0;
            std::set<Key, Less> set;
        };
        NearestQueue reqQueue;
        NearestQueue reqQueueFar;
        struct ColumnInfo {
            int dim = 0, firstSubY = 0, slots = 0;
            std::vector<uint8_t> biomes;
        };
        std::unordered_map<ChunkPos, ColumnInfo, ChunkPosHash> columns;
        int nearRadius = 32;
        int lodScanCentre[2] = {INT32_MIN, INT32_MIN};
        int lodScanRadius = -1;
        std::map<std::tuple<int, int, int, int>, std::chrono::steady_clock::time_point> inflight;
        std::map<std::tuple<int, int, int, int>, int> retries;
        static constexpr int kMaxSubChunkRetries = 3;
        void ForgetColumnRequests(int cx, int cz);
        void CompleteRequest(const std::tuple<int, int, int, int>& key, bool retry);
        void DropSlot(const std::tuple<int, int, int, int>& key, std::vector<std::pair<int, int>>& completed);
        void FinishColumns(const std::vector<std::pair<int, int>>& columns);
        std::map<std::pair<int, int>, int> colPending;

        std::string playerName = "Perlin";
        LoadedSkin skin;
        entity::EntityTracker entities;
        std::shared_ptr<xbox::Account> account;
        std::shared_ptr<friendworld::Lease> friendLease;
        xbox::Credentials onlineCreds;
        std::string accountKey;
        std::shared_ptr<EC_KEY> swapKey; // key pair of a background sign-in that finished during the connect
        std::string playerUuid;
        std::string serverAddress;
        std::string gameVersion;
        int protocolOverride = 0;
        std::string versionOverride;
        PlayerPose pose;
        uint64_t runtimeId = 0;
        int64_t uniqueId = 0;
        uint64_t authTick = 0;
        uint64_t serverTick = 0;
        float lastAuthPos[3] = {0, 0, 0};
        bool haveLastAuthPos = false;
        bool pendingTeleportAck = false;
        std::vector<uint32_t> pendingInputFlags;
        std::mutex pendingInputMutex;
        struct BrokenSupport {
            int x, y, z;
            std::vector<std::pair<int, uint16_t>> above;
            std::chrono::steady_clock::time_point at;
        };
        std::vector<BrokenSupport> brokenSupports;
        std::mutex supportMutex;
        std::chrono::steady_clock::time_point lastSwingAt;
        std::deque<BlockActionData> pendingBlockActions;
        std::deque<ServerMotion> serverMotions;
        void QueueServerMotion(ServerMotion motion);
        mutable std::mutex attrMutex;
        std::map<std::string, float> ownAttributes;
        std::array<uint64_t, 2> ownFlagWords{};
        std::array<bool, 2> ownFlagWordsKnown{};
        std::optional<proto::PlayerAttributesEntry> movementAttribute;
        // own copy of the movement AttributeInstance: modifiers, base (min, max, value), limits, value
        std::vector<proto::PlayerAttributesEntryModifiersEntry> movementModifiers;
        std::array<float, 3> movementBase{};
        std::array<float, 2> movementLimits{};
        float movementValue = 0.1f;
        std::vector<FreezeUpdate> freezeUpdates;
        void ApplyMovementAttribute(const proto::PlayerAttributesEntry& entry);
        int64_t ownPlayerFlags = 0;
        float ownScale = 1.0f, ownWidth = 0.6f, ownHeight = 1.8f;
        int predictionSyncCountdown = -1;
        bool flying = false;
        bool initializedSent = false;
        bool startGameSeen = false;
        bool spawnSeen = false;
        std::chrono::steady_clock::time_point spawnAt;
        std::chrono::steady_clock::time_point startGameAt;
        std::atomic<int> viewCenterChunk[2] = {0, 0};
        int viewRadiusChunks = 0;
        uint64_t movePacketsSent = 0;
        std::atomic<uint64_t> correctionsReceived{0};
        mutable std::mutex commandResultMutex;
        CommandResult commandResult;
        uint64_t statLevelChunks = 0, statRequestsSent = 0, statSubChunkReplies = 0, statSubChunksFailed = 0;
        std::atomic<uint64_t> statSubChunksApplied{0};
        std::chrono::steady_clock::time_point lastStatLog;
        float startPos[3] = {0, 0, 0};
        bool haveStartPos = false;
        std::set<std::pair<int, int>> seenColumns;
        uint64_t statResult[8] = {};
        void LogChunkStats();
        int lastBlockPos[3] = {0, 0, 0};
        bool haveBlockPos = false;
        int chunkRadius = 8;
        int serverProtocol = 0;

        bool mayFly = false;
        bool noClip = false;
        // ability 11, MovementAbilitiesComponent bit 2: changes jump while gliding
        bool instabuild = false;
        int gameType = 0;
        bool isFlying = false;
        uint32_t abilitiesSeq = 0;
        float flySpeed = 0.05f;
        float vFlySpeed = 1.0f;
        float walkSpeed = 0.1f;

        int worldSpawn[3] = {0, 0, 0};
        bool haveWorldSpawn = false;

        mutable std::mutex timeMutex;
        int64_t timeTotal = -1;
        bool clockPaused = false;
        uint64_t overworldClockId = 0x63d7ede73c38f916ULL;
        std::atomic<int> dimensionId{0};
        std::atomic<uint32_t> hurtCounter{0};
        float lastHealth = -1.0f;
        std::chrono::steady_clock::time_point lastHurtAt;
        void NoteHurt();
        void NoteKnockback(float vx, float vz);
        mutable std::mutex hurtMutex;
        HurtSource hurtSource;
        float knockX = 0, knockZ = 0;
        std::chrono::steady_clock::time_point knockAt;
        std::atomic<bool> dimAckPending{false};
        std::chrono::steady_clock::time_point dimAckSince;
        std::optional<uint32_t> dimLoadingId;
        void ResetWorldForDimension();
        void FinishDimensionChange();
        std::atomic<float> rainTarget{0.0f};
        std::atomic<float> thunderTarget{0.0f};
        std::atomic<bool> dayCycleRunning{true};
        std::atomic<uint64_t> diagPackets{0};
        std::atomic<int64_t> lastPacketMs{0};
        std::string lastViolation;
        std::atomic<uint64_t> txPackets{0};
        std::mutex idCountMutex;
        std::map<uint32_t, uint64_t> idCounts;
        std::atomic<uint32_t> diagLastId{0};
        std::chrono::steady_clock::time_point diagAt;
        uint64_t diagPrevPackets = 0;
        bool diagLostLogged = false;
        std::chrono::steady_clock::time_point timeAt;

        void RegisterPlayerStateHandlers();
        void ResetPlayerState();
        mutable std::mutex statusMutex;
        mutable std::mutex locatorMutex;
        std::vector<LocatorWaypoint> locatorWaypoints;
        Vitals vitals;
        InventoryView inventory;
        proto::ItemV4 inventoryRaw[36];
        std::atomic<int64_t> ridingUid{0};
        std::atomic<bool> ridingDriver{false};
        std::atomic<uint64_t> predictedVehicleRuntime{0};
        std::optional<std::array<float, 3>> ownSeatOffset;
        std::mutex vehicleCorrectionMutex;
        std::deque<VehicleCorrection> vehicleCorrections;
        proto::ItemV4 extraRaw[20];
        proto::ItemV4 horseRaw[17];
        int horseWindow = -1;
        proto::ItemV4 chestRaw[54];
        int chestWindow = -1;
        bool chestJustOpened = false;
        proto::ContainerSlotType chestSlotType = proto::ContainerSlotType::Container;
        std::chrono::steady_clock::time_point invServerAt[36]{};
        struct RecipeIngredientInfo {
            bool empty = true;
            std::string name, tag;
            int meta = 0;
        };
        struct RecipeInfo {
            bool shaped = false;
            int w = 0, h = 0;
            std::vector<RecipeIngredientInfo> in;
            proto::ItemV4 out;
            ItemStackInfo outInfo;
            uint32_t netId = 0;
        };
        std::vector<RecipeInfo> recipes;
        std::map<int32_t, std::string> invSummaries;
        struct InvSnapshot {
            InventoryView view;
            proto::ItemV4 raw[36];
            proto::ItemV4 extra[20];
            proto::ItemV4 chest[54];
            proto::ItemV4 horse[17];
        };
        std::map<int32_t, InvSnapshot> invSnapshots;
        bool SendLegacyInventoryLocked(const InvSnapshot& before);
        int craftedRecipe = -1;
        std::deque<std::function<bool(proto::ItemStackRequest&)>> invQueue;
        bool invCloseWanted = false;
        int32_t invPendingRequest = 0;
        std::map<int32_t, std::chrono::steady_clock::time_point> invInFlight;
        std::chrono::steady_clock::time_point invPendingAt;
        void InvRecomputeOutputLocked();
        ItemStackInfo& InvInfoLocked(int addr);
        proto::ItemV4& InvRawLocked(int addr);
        void InvEnqueue(std::function<bool(proto::ItemStackRequest&)> op);
        bool SendCloseNow();
        struct ActiveEffect {
            int id = 0;
            int amplifier = 0;
            int32_t duration = 0;
            uint64_t appliedTick = 0;
            bool ambient = false, particles = true;
        };
        std::vector<ActiveEffect> activeEffects;
        std::vector<EffectUpdate> pendingEffectUpdates;
        std::vector<ServerMovementEffect> pendingMovementEffects;
        bool inventoryOpen = false, serverClosedInventory = false, workbenchOpen = false,
             workbenchJustOpened = false;
        bool horseJustOpened = false;
        int inventoryWindow = 0;
        int32_t nextStackRequestId = -3;
        struct PendingDrop {
            int slot;
            int count;
            int attempt;
        };
        std::map<int32_t, PendingDrop> pendingDrops;
        int dropForm = 0;
        bool SendDropRequest(int slot, int count, int attempt);
        std::map<int, std::string> itemNames;
        CreativeCatalog creative;
        std::map<uint32_t, proto::ItemV4> creativeRaw;
        std::map<uint64_t, proto::ItemV4> droppedRaw;
        TitleText titleText;
        void PushChat(const std::string& line);
        std::mutex chatMutex;
        std::vector<std::string> chatInbox;
        void PushSound(SoundRequest request);
        std::mutex soundMutex;
        std::vector<SoundRequest> soundInbox;
        std::mutex particleMutex;
        std::vector<ParticleEvent> particleInbox;
        void PushParticle(ParticleEvent event);

        bool compressEnabled = false;
        uint16_t compressThreshold = 0;
        uint16_t compressAlgorithm = 0;
        bool cacheStatusSent = false;
        bool packsCompletedSent = false;
        bool radiusSent = false;
    };

}
