#include "session/Session.hpp"

#include "core/Auth.hpp"
#include "core/Commands.hpp"
#include "core/Json.hpp"
#include "core/Log.hpp"
#include "core/Server.hpp"
#include "inventory/Recipes.hpp"
#include "world/BlockCatalog.hpp"
#include "world/Blocks.hpp"

#include "protocol/Packets.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <unordered_map>

namespace perlin::server {
    namespace proto = proto2225;

    namespace {
        constexpr int32_t kProtocol = 2225;
        constexpr const char* kGameVersion = "1.26.60";
        constexpr double kEyeHeight = 1.62;
        constexpr int kChunksPerTick = 6;
        constexpr auto kIdleTimeout = std::chrono::seconds(60);
        constexpr auto kHeartbeat = std::chrono::seconds(5);
        constexpr auto kLoginTimeout = std::chrono::seconds(60);

        template <class P> bool Decode(std::span<const uint8_t> body, P& packet) {
            net::ReadStream stream(body.data(), body.size());
            return Read(stream, packet) && stream.ok;
        }

        std::array<uint8_t, 16> ParseUuid(const std::string& text, uint64_t fallbackSeed) {
            std::array<uint8_t, 16> text16{};
            int n = 0;
            for (size_t i = 0; i < text.size() && n < 32; i++) {
                const char c = text[i];
                int v;
                if (c >= '0' && c <= '9')
                    v = c - '0';
                else if (c >= 'a' && c <= 'f')
                    v = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F')
                    v = c - 'A' + 10;
                else
                    continue;
                text16[static_cast<size_t>(n / 2)] =
                    static_cast<uint8_t>((text16[static_cast<size_t>(n / 2)] << 4) | v);
                n++;
            }
            if (n != 32) {
                for (size_t i = 0; i < 16; i++)
                    text16[i] = static_cast<uint8_t>((fallbackSeed * 0x9E3779B97F4A7C15ull) >> (i * 4));
            }
            std::array<uint8_t, 16> wire{};
            for (size_t i = 0; i < 8; i++) {
                wire[i] = text16[7 - i];
                wire[8 + i] = text16[15 - i];
            }
            return wire;
        }

        int ImageSide(const json::Value& v, const char* key, int fallback) {
            const int64_t n = v[key].Int(fallback);
            return n > 0 && n <= 1024 ? static_cast<int>(n) : fallback;
        }

        proto::Skin SkinFromClientData(const json::Value& c, const std::string& playerName) {
            proto::Skin s;
            s.skinId = c["SkinId"].String("Standard_Custom");
            s.skinResourcePack = json::Base64UrlDecode(c["SkinResourcePatch"].String());
            s.skinData.width = ImageSide(c, "SkinImageWidth", 64);
            s.skinData.height = ImageSide(c, "SkinImageHeight", 64);
            const std::string rgba = json::Base64UrlDecode(c["SkinData"].String());
            const size_t want =
                static_cast<size_t>(s.skinData.width) * static_cast<size_t>(s.skinData.height) * 4;
            s.skinData.data.assign(rgba.begin(), rgba.end());
            if (s.skinData.data.size() != want) {
                s.skinData.width = 64;
                s.skinData.height = 64;
                s.skinData.data.assign(64 * 64 * 4, 0x80);
            }
            s.capeData.width = ImageSide(c, "CapeImageWidth", 0);
            s.capeData.height = ImageSide(c, "CapeImageHeight", 0);
            const std::string cape = json::Base64UrlDecode(c["CapeData"].String());
            if (cape.size() ==
                static_cast<size_t>(s.capeData.width) * static_cast<size_t>(s.capeData.height) * 4)
                s.capeData.data.assign(cape.begin(), cape.end());
            else
                s.capeData = {};
            s.geometryData = json::Base64UrlDecode(c["SkinGeometryData"].String());
            s.geometryDataVersion = c["SkinGeometryDataEngineVersion"].String();
            s.animationData = json::Base64UrlDecode(c["SkinAnimationData"].String());
            s.capeId = c["CapeId"].String();
            s.fullSkinId = s.skinId + s.capeId;
            s.armSize = c["ArmSize"].String() == "slim" ? proto::SkinArmSize::Slim : proto::SkinArmSize::Wide;
            s.premium = c["PremiumSkin"].Bool();
            s.persona = c["PersonaSkin"].Bool();
            s.capeOnClassic = c["CapeOnClassicSkin"].Bool();
            if (const std::string color = c["SkinColor"].String(); color.size() > 1 && color[0] == '#') {
                try {
                    const uint32_t v = static_cast<uint32_t>(std::stoul(color.substr(1), nullptr, 16));
                    s.skinColor = static_cast<int32_t>(color.size() <= 7 ? (0xFF000000u | v) : v);
                } catch (...) {}
            }
            for (const json::Value& a : c["AnimatedImageData"].items) {
                proto::SkinAnimationsEntry anim;
                anim.skinImage.width = ImageSide(a, "ImageWidth", 0);
                anim.skinImage.height = ImageSide(a, "ImageHeight", 0);
                const std::string frames = json::Base64UrlDecode(a["Image"].String());
                if (frames.size() != static_cast<size_t>(anim.skinImage.width) *
                                         static_cast<size_t>(anim.skinImage.height) * 4)
                    continue;
                anim.skinImage.data.assign(frames.begin(), frames.end());
                anim.animationType = static_cast<uint32_t>(a["Type"].Int());
                anim.animationFrames = static_cast<float>(a["Frames"].Number());
                anim.expressionType = static_cast<uint32_t>(a["AnimationExpression"].Int());
                s.animations.push_back(std::move(anim));
            }
            static const char* const kPieceNames[] = {"",
                                                      "persona_skeleton",
                                                      "persona_body",
                                                      "persona_skin",
                                                      "persona_bottom",
                                                      "persona_feet",
                                                      "persona_dress",
                                                      "persona_top",
                                                      "persona_high_pants",
                                                      "persona_hands",
                                                      "persona_outerwear",
                                                      "persona_facial_hair",
                                                      "persona_mouth",
                                                      "persona_eyes",
                                                      "persona_hair",
                                                      "persona_hood"};
            for (const json::Value& piece : c["PersonaPieces"].items) {
                proto::SkinPersonalPiecesEntry e;
                e.pieceId = piece["PieceId"].String();
                const std::string type = piece["PieceType"].String();
                for (size_t i = 1; i < std::size(kPieceNames); i++)
                    if (type == kPieceNames[i])
                        e.pieceType = static_cast<proto::PersonaPieceType>(i);
                e.packId.bytes = ParseUuid(piece["PackId"].String(), 0);
                e.isDefaultPiece = piece["IsDefault"].Bool();
                e.productId = piece["ProductId"].String();
                s.personalPieces.push_back(std::move(e));
            }
            for (const json::Value& tint : c["PieceTintColors"].items) {
                proto::SkinPieceTintColorsEntry e;
                e.pieceType = tint["PieceType"].String();
                for (size_t i = 0; i < 4 && i < tint["Colors"].items.size(); i++) {
                    const std::string col = tint["Colors"].items[i].String();
                    if (col.size() > 1 && col[0] == '#')
                        try {
                            e.colors[i] = static_cast<int32_t>(std::stoul(col.substr(1), nullptr, 16));
                        } catch (...) {}
                }
                s.pieceTintColors.push_back(std::move(e));
            }
            s.trusted = "true";
            (void)playerName;
            return s;
        }
    }

    namespace {
        struct FoodInfo {
            int nutrition;
            float saturation;
            bool always;
            int ticks;
            const char* leftover;
        };

        const FoodInfo* FoodOf(const std::string& fullName) {
            static const std::unordered_map<std::string, FoodInfo> foods = {
                {"apple", {4, 2.4f, false, 32, nullptr}},
                {"baked_potato", {5, 6.0f, false, 32, nullptr}},
                {"beef", {3, 1.8f, false, 32, nullptr}},
                {"beetroot", {1, 1.2f, false, 32, nullptr}},
                {"beetroot_soup", {6, 7.2f, false, 32, "minecraft:bowl"}},
                {"bread", {5, 6.0f, false, 32, nullptr}},
                {"carrot", {3, 3.6f, false, 32, nullptr}},
                {"chicken", {2, 1.2f, false, 32, nullptr}},
                {"chorus_fruit", {4, 2.4f, true, 32, nullptr}},
                {"cod", {2, 0.4f, false, 32, nullptr}},
                {"cooked_beef", {8, 12.8f, false, 32, nullptr}},
                {"cooked_chicken", {6, 7.2f, false, 32, nullptr}},
                {"cooked_cod", {5, 6.0f, false, 32, nullptr}},
                {"cooked_mutton", {6, 9.6f, false, 32, nullptr}},
                {"cooked_porkchop", {8, 12.8f, false, 32, nullptr}},
                {"cooked_rabbit", {5, 6.0f, false, 32, nullptr}},
                {"cooked_salmon", {6, 9.6f, false, 32, nullptr}},
                {"cookie", {2, 0.4f, false, 32, nullptr}},
                {"dried_kelp", {1, 0.6f, false, 16, nullptr}},
                {"enchanted_golden_apple", {4, 9.6f, true, 32, nullptr}},
                {"golden_apple", {4, 9.6f, true, 32, nullptr}},
                {"glow_berries", {2, 0.4f, false, 32, nullptr}},
                {"golden_carrot", {6, 14.4f, false, 32, nullptr}},
                {"honey_bottle", {6, 1.2f, false, 40, "minecraft:glass_bottle"}},
                {"melon_slice", {2, 1.2f, false, 32, nullptr}},
                {"mushroom_stew", {6, 7.2f, false, 32, "minecraft:bowl"}},
                {"mutton", {2, 1.2f, false, 32, nullptr}},
                {"poisonous_potato", {2, 1.2f, false, 32, nullptr}},
                {"porkchop", {3, 1.8f, false, 32, nullptr}},
                {"potato", {1, 0.6f, false, 32, nullptr}},
                {"pufferfish", {1, 0.2f, false, 32, nullptr}},
                {"pumpkin_pie", {8, 4.8f, false, 32, nullptr}},
                {"rabbit", {3, 1.8f, false, 32, nullptr}},
                {"rabbit_stew", {10, 12.0f, false, 32, "minecraft:bowl"}},
                {"rotten_flesh", {4, 0.8f, false, 32, nullptr}},
                {"salmon", {2, 0.4f, false, 32, nullptr}},
                {"spider_eye", {2, 3.2f, false, 32, nullptr}},
                {"suspicious_stew", {6, 7.2f, true, 32, "minecraft:bowl"}},
                {"sweet_berries", {2, 0.4f, false, 32, nullptr}},
                {"tropical_fish", {1, 0.2f, false, 32, nullptr}},
                {"milk_bucket", {0, 0.0f, true, 32, "minecraft:bucket"}},
                {"potion", {0, 0.0f, true, 32, "minecraft:glass_bottle"}},
            };
            std::string n = fullName.starts_with("minecraft:") ? fullName.substr(10) : fullName;
            if (n == "clownfish")
                n = "tropical_fish";
            if (n == "appleEnchanted")
                n = "enchanted_golden_apple";
            const auto it = foods.find(n);
            return it == foods.end() ? nullptr : &it->second;
        }

        std::string ItemNameOf(int16_t id) {
            const CatalogItem* item = BlockCatalog::Instance().ItemById(id);
            return item ? item->name : std::string();
        }

        std::string Pretty(const std::string& id) {
            std::string n = id.starts_with("minecraft:") ? id.substr(10) : id;
            if (n == "zombie_pigman")
                n = "zombified_piglin";
            if (n == "evocation_illager")
                n = "evoker";
            if (n.ends_with("_v2"))
                n.resize(n.size() - 3);
            bool up = true;
            for (char& c : n) {
                if (c == '_') {
                    c = ' ';
                    up = true;
                } else if (up) {
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                    up = false;
                }
            }
            return n;
        }

        int XpForLevel(int level) {
            if (level >= 30)
                return 112 + (level - 30) * 9;
            if (level >= 15)
                return 37 + (level - 15) * 5;
            return 7 + level * 2;
        }

        bool BypassesArmor(Session::DamageCause c) {
            using C = Session::DamageCause;
            return c == C::Fall || c == C::Void || c == C::Drowning || c == C::Starving || c == C::Suffocation ||
                   c == C::Poison || c == C::Wither || c == C::Magic || c == C::Kill || c == C::Burning;
        }

        bool FireCause(Session::DamageCause c) {
            using C = Session::DamageCause;
            return c == C::Lava || c == C::Fire || c == C::Burning || c == C::Magma;
        }
    }

    Session::Session(Server& server, std::shared_ptr<Connection> connection, uint64_t runtimeId)
        : server(server), connection(std::move(connection)), runtimeId(runtimeId),
          skin(std::make_unique<proto::Skin>()) {
        address = this->connection->Describe();
        posX = server.SpawnX() + 0.5;
        posZ = server.SpawnZ() + 0.5;
        posY = server.SpawnY() + kEyeHeight;
    }

    Session::~Session() = default;

    const proto::Skin& Session::GetSkin() const {
        return *skin;
    }

    template <class P> void Session::Send(const P& packet) {
        std::vector<uint8_t> body;
        if (!net::EncodeChecked(packet, body)) {
            LogWarn(std::string("refusing to send malformed ") + P::kName);
            return;
        }
        Queue(P::kId, body);
    }

    void Session::Queue(uint32_t id, const std::vector<uint8_t>& body) {
        if (state == State::Closed)
            return;
        BatchCodec::AppendPacket(outgoing, id, body);
    }

    void Session::Flush() {
        if (outgoing.empty() || state == State::Closed)
            return;
        const std::vector<uint8_t> wire = codec.Seal(outgoing);
        outgoing.clear();
        if (!connection->Send(wire.data(), wire.size(), true))
            Close();
    }

    void Session::SendImmediate(uint32_t id, const std::vector<uint8_t>& body) {
        std::vector<uint8_t> content;
        BatchCodec::AppendPacket(content, id, body);
        const std::vector<uint8_t> wire = codec.Seal(content);
        connection->Send(wire.data(), wire.size(), true);
    }

    void Session::Close() {
        if (state == State::Closed)
            return;
        const bool wasInGame = state == State::InGame;
        state = State::Closed;
        if (wasInGame)
            server.OnPlayerLeft(*this);
        connection->Close();
    }

    void Session::Kick(const std::string& reason) {
        if (state == State::Closed)
            return;
        proto::DisconnectPacket p;
        p.reason = proto::DisconnectFailReason::Unknown;
        p.message = reason;
        p.filteredMessage = reason;
        Send(p);
        Flush();
        Close();
    }

    void Session::OnMessage(std::span<const uint8_t> wire) {
        if (state == State::Closed)
            return;
        lastPacket = std::chrono::steady_clock::now();
        std::vector<uint8_t> content;
        if (!codec.Open(wire, content)) {
            LogWarn(connection->Describe() + ": corrupt batch, closing");
            Close();
            return;
        }
        const bool ok = BatchCodec::Split(content, [this](uint32_t id, std::span<const uint8_t> body) {
            if (state != State::Closed)
                Dispatch(id, body);
        });
        if (!ok) {
            LogWarn(connection->Describe() + ": truncated batch, closing");
            Close();
        }
    }

    void Session::Dispatch(uint32_t id, std::span<const uint8_t> body) {
        switch (id) {
        case proto::RequestNetworkSettingsPacket::kId:
            OnRequestSettings(body);
            break;
        case proto::LoginPacket::kId:
            OnLogin(body);
            break;
        case proto::ResourcePackClientResponsePacket::kId:
            OnPackResponse(body);
            break;
        case proto::RequestChunkRadiusPacket::kId:
            OnChunkRadius(body);
            break;
        case proto::SetLocalPlayerAsInitializedPacket::kId:
            OnInitialized();
            break;
        case proto::TextPacket::kId:
            OnText(body);
            break;
        case proto::CommandRequestPacket::kId:
            OnCommand(body);
            break;
        case proto::PlayerAuthInputPacket::kId:
            OnAuthInput(body);
            break;
        case proto::MovePlayerPacket::kId:
            OnMovePlayer(body);
            break;
        case proto::ItemStackRequestPacket::kId:
            OnStackRequest(body);
            break;
        case proto::MobEquipmentPacket::kId:
            OnMobEquipment(body);
            break;
        case proto::InventoryTransactionPacket::kId:
            OnTransaction(body);
            break;
        case proto::PlayerActionPacket::kId:
            OnPlayerAction(body);
            break;
        case proto::BlockPickRequestPacket::kId:
            OnPickBlock(body);
            break;
        case proto::InteractPacket::kId:
            OnInteract(body);
            break;
        case proto::ContainerClosePacket::kId:
            OnContainerClose(body);
            break;
        case proto::AnimatePacket::kId:
            OnAnimate(body);
            break;
        case proto::PlayerSkinPacket::kId:
            OnPlayerSkin(body);
            break;
        case proto::TickSyncPacket::kId:
            OnTickSync(body);
            break;
        case proto::NetworkStackLatencyPacket::kId:
            OnLatency(body);
            break;
        case proto::EmotePacket::kId:
            OnEmote(body);
            break;
        case proto::LevelSoundEventPacket::kId:
            OnSound(body);
            break;
        case proto::RespawnPacket::kId:
            OnRespawn(body);
            break;
        case proto::ModalFormResponsePacket::kId:
            OnFormResponse(body);
            break;
        case proto::ClientToServerHandshakePacket::kId:
        case proto::ClientCacheStatusPacket::kId:
        default:
            break;
        }
    }

    void Session::OnRequestSettings(std::span<const uint8_t> body) {
        proto::RequestNetworkSettingsPacket req;
        if (state != State::AwaitSettings || !Decode(body, req)) {
            Close();
            return;
        }
        if (req.clientProtocol != kProtocol) {
            proto::PlayStatusPacket p;
            p.status = req.clientProtocol < kProtocol ? proto::PlayStatusPacketStatus::FailedClient
                                                      : proto::PlayStatusPacketStatus::FailedSpawn;
            std::vector<uint8_t> out;
            net::EncodeChecked(p, out);
            SendImmediate(proto::PlayStatusPacket::kId, out);
            LogInfo(connection->Describe() + ": protocol " + std::to_string(req.clientProtocol) +
                    " rejected (server speaks " + std::to_string(kProtocol) + ")");
            Close();
            return;
        }
        proto::NetworkSettingsPacket settings;
        settings.compressionThreshold = server.Config().compressionThreshold;
        settings.compressionAlgorithm = proto::NetworkSettingsPacketCompressionAlgorithm::Deflate;
        std::vector<uint8_t> out;
        net::EncodeChecked(settings, out);
        SendImmediate(proto::NetworkSettingsPacket::kId, out);
        codec.EnableCompression(server.Config().compressionThreshold);
        state = State::AwaitLogin;
        stateSince = std::chrono::steady_clock::now();
    }

    void Session::OnLogin(std::span<const uint8_t> body) {
        proto::LoginPacket login;
        if (state != State::AwaitLogin || !Decode(body, login)) {
            Close();
            return;
        }
        const auth::Login login2 = auth::VerifyLogin(
            login.tokens.identity, login.tokens.client,
            server.Config().authRootKeys.empty() ? auth::DefaultRootKeys() : server.Config().authRootKeys);
        if (!login2.chainValid) {
            LogWarn(connection->Describe() + ": login rejected: " + login2.error);
            Kick("Invalid login");
            return;
        }
        if (server.Config().xboxAuth) {
            std::string why;
            if (!login2.xboxVerified) {
                why = "not signed in to Xbox Live";
            } else if (!connection->offerSdp.empty() &&
                       !auth::VerifyOfferBinding(connection->offerSdp, login2.clientKey, why)) {
            } else {
                why.clear();
            }
            if (!why.empty()) {
                LogInfo(connection->Describe() + ": refused: " + why);
                proto::DisconnectPacket d;
                d.reason = proto::DisconnectFailReason::NotAuthenticated;
                d.message = "This server requires you to be signed in to Xbox Live";
                d.filteredMessage = d.message;
                Send(d);
                Flush();
                Close();
                return;
            }
        }
        name = login2.name;
        xuid = login2.xuid;
        std::string uuidText = login2.uuid;
        const json::Value clientData = login2.clientData;
        const bool haveClient = login2.clientDataValid;
        if (name.empty() && haveClient)
            name = clientData["ThirdPartyName"].String();
        if (name.empty()) {
            Kick("Invalid login: no player name");
            return;
        }
        for (char& c : name)
            if (static_cast<unsigned char>(c) < 0x20)
                c = ' ';
        if (name.size() > 32)
            name.resize(32);
        uuid = ParseUuid(uuidText, runtimeId);
        *skin = SkinFromClientData(haveClient ? clientData : json::Value{}, name);
        isOperator = server.IsOperator(name, xuid);
        isVisitor =
            !server.Config().loopbackOnly && !isOperator && server.PermissionOf(name, xuid) == "visitor";
        gameMode = server.Config().gameMode;
        creative = gameMode == 1;

        if (server.Config().whitelist && !server.Config().loopbackOnly && !server.IsWhitelisted(name, xuid)) {
            LogInfo(name + " refused: not on the allowlist");
            Kick("disconnectionScreen.notAllowed");
            return;
        }
        if (server.IsBanned(name, xuid) && !server.Config().loopbackOnly) {
            LogInfo(name + " refused: banned");
            Kick("You are banned from this server.");
            return;
        }
        if (std::string reason; server.HookPreLogin(*this, reason)) {
            LogInfo(name + " refused by a plugin");
            Kick(reason.empty() ? "disconnectionScreen.noReason" : reason);
            return;
        }
        if (server.Full() && !server.IgnoresPlayerLimit(name, xuid)) {
            proto::PlayStatusPacket p;
            p.status = proto::PlayStatusPacketStatus::FailedServerFull;
            Send(p);
            Flush();
            Close();
            return;
        }
        if (Session* other = server.FindByName(name); other && other != this && other->InGame()) {
            Kick("A player with this name is already connected");
            return;
        }

        proto::PlayStatusPacket ok;
        ok.status = proto::PlayStatusPacketStatus::LoginSuccess;
        Send(ok);

        proto::ResourcePacksInfoPacket info;
        info.mustAccept = false;
        Send(info);
        state = State::ResourcePacks;
        stateSince = std::chrono::steady_clock::now();
        LogInfo(name + " logging in from " + connection->Describe());
    }

    void Session::OnPackResponse(std::span<const uint8_t> body) {
        proto::ResourcePackClientResponsePacket r;
        if (!Decode(body, r)) {
            Close();
            return;
        }
        using Status = proto::ResourcePackClientResponsePacketResponseStatus;
        switch (r.responseStatus) {
        case Status::Refused:
            Kick("Resource packs refused");
            break;
        case Status::SendPacks:
            Kick("Requested a resource pack this server does not have");
            break;
        case Status::HaveAllPacks: {
            proto::ResourcePackStackPacket stack;
            stack.mustAccept = false;
            stack.gameVersion = "*";
            Send(stack);
            state = State::ResourceStack;
            break;
        }
        case Status::Completed:
            if (state == State::ResourceStack || state == State::ResourcePacks) {
                SendStartGame();
                SendWorldSetup();
                state = State::Loading;
                stateSince = std::chrono::steady_clock::now();
            }
            break;
        }
    }

    void Session::SendStartGame() {
        const ServerConfig& cfg = server.Config();
        proto::StartGamePacket g;
        g.entityId = static_cast<int64_t>(runtimeId);
        g.runtimeEntityId = runtimeId;
        g.playerGamemode = static_cast<proto::GameMode>(gameMode);
        g.playerPosition = {static_cast<float>(posX), static_cast<float>(posY), static_cast<float>(posZ)};
        g.rotation = {0.0f, 0.0f};
        g.seed = cfg.seed;
        g.biomeType = 0;
        g.biomeName = "plains";
        g.dimension = proto::StartGamePacketDimension::Overworld;
        g.generator = cfg.generator == "vanilla" ? 1 : 2;
        g.worldGamemode = static_cast<proto::GameMode>(cfg.gameMode);
        g.difficulty = cfg.difficulty;
        g.spawnPosition = {static_cast<int32_t>(std::floor(server.SpawnX())),
                           static_cast<int32_t>(std::floor(server.SpawnY())),
                           static_cast<int32_t>(std::floor(server.SpawnZ()))};
        g.achievementsDisabled = true;
        g.rainLevel = server.Raining() ? 1.0f : 0.0f;
        g.lightningLevel = server.Thundering() ? 1.0f : 0.0f;
        g.isMultiplayer = true;
        g.broadcastToLan = false;
        g.enableCommands = true;
        g.permissionLevel = isOperator  ? proto::PermissionLevel::Operator
                            : isVisitor ? proto::PermissionLevel::Visitor
                                        : proto::PermissionLevel::Member;
        g.serverChunkTickRange = static_cast<int32_t>(cfg.tickDistance);
        g.chatRestrictionLevel =
            cfg.chatRestriction == "Disabled"  ? proto::StartGamePacketChatRestrictionLevel::Disabled
            : cfg.chatRestriction == "Dropped" ? proto::StartGamePacketChatRestrictionLevel::Dropped
                                               : proto::StartGamePacketChatRestrictionLevel::None;
        g.gameVersion = "*";
        g.levelId = cfg.levelName;
        g.worldName = cfg.name;
        g.currentTick = static_cast<int64_t>(server.TickCount());
        g.multiplayerCorrelationId = "perlin-" + std::to_string(runtimeId);
        g.serverAuthoritativeInventory = true;
        g.engine = "Perlin Server";
        g.blockNetworkIdsAreHashes = true;
        g.serverIdentifier = cfg.name;
        Send(g);
    }

    void Session::SendWorldSetup() {
        Queue(proto::ItemRegistryPacket::kId, server.ItemRegistryBody());
        Queue(proto::CreativeContentPacket::kId, server.CreativeContentBody());
        if (!Recipes::Instance().CraftingDataBody().empty())
            Queue(proto::CraftingDataPacket::kId, Recipes::Instance().CraftingDataBody());
        Send(proto::BiomeDefinitionListPacket{});
        Send(proto::AvailableEntityIdentifiersPacket{});

        proto::SetTimePacket t;
        t.time = static_cast<int32_t>(server.DayTime() % 2147483647);
        Send(t);

        {
            proto::PlayerListPacket self;
            proto::PlayerRecord rec;
            rec.type = proto::PlayerRecordType::Add;
            rec.uuid.bytes = uuid;
            rec.entityUniqueId = static_cast<int64_t>(runtimeId);
            rec.username = name;
            rec.xboxUserId = xuid;
            rec.skinData = *skin;
            rec.playerColor = -1;
            rec.isHost = false;
            self.records.push_back(std::move(rec));
            Send(self);
        }
        SendCommands();
        SendAbilities();
        SendAttributes();
        SendInventory();
    }

    void Session::SendCommands() {
        proto::AvailableCommandsPacket commands;
        FillAvailableCommands(commands, server.Commands(), isOperator);
        Send(commands);
    }

    void Session::SendAttributes() {
        proto::UpdateAttributesPacket p;
        p.runtimeEntityId = runtimeId;
        p.tick = server.TickCount();
        const auto add = [&](const char* attribute, float min, float max, float current, float fallback) {
            proto::PlayerAttributesEntry a;
            a.name = attribute;
            a.min = a.defaultMin = min;
            a.max = a.defaultMax = max;
            a.current = current;
            a.default_ = fallback;
            p.attributes.push_back(std::move(a));
        };
        const float maxHealth = MaxHealth();
        add("minecraft:health", 0.0f, maxHealth, std::clamp(std::ceil(health), 0.0f, maxHealth), 20.0f);
        add("minecraft:player.hunger", 0.0f, 20.0f, static_cast<float>(food), 20.0f);
        add("minecraft:player.saturation", 0.0f, 20.0f, saturation, 5.0f);
        add("minecraft:player.exhaustion", 0.0f, 5.0f, std::min(exhaustion, 5.0f), 0.0f);
        add("minecraft:player.level", 0.0f, 24791.0f, static_cast<float>(xpLevel), 0.0f);
        add("minecraft:player.experience", 0.0f, 1.0f, std::clamp(xpProgress, 0.0f, 1.0f), 0.0f);
        add("minecraft:absorption", 0.0f, 16.0f, std::min(absorption, 16.0f), 0.0f);
        float speed = 0.1f;
        if (const int level = EffectLevel(1); level > 0)
            speed *= 1.0f + 0.2f * static_cast<float>(level);
        if (const int level = EffectLevel(2); level > 0)
            speed *= std::max(0.0f, 1.0f - 0.15f * static_cast<float>(level));
        add("minecraft:movement", 0.0f, 3.4028235e38f, speed, 0.1f);
        Send(p);
    }

    void Session::SendAbilities() {
        namespace F = proto::FlagsAbilitySet;
        uint32_t enabled = 0;
        if (!isVisitor && gameMode != 6) {
            if (gameMode != 2)
                enabled |= F::Build | F::Mine;
            enabled |= F::DoorsAndSwitches | F::OpenContainers | F::AttackPlayers | F::AttackMobs;
        }
        if (gameMode == 1)
            enabled |= F::InstantBuild | F::MayFly | F::Invulnerable;
        if (gameMode == 6)
            enabled |= F::MayFly | F::Flying | F::Invulnerable | F::NoClip;
        if (isOperator)
            enabled |= F::OperatorCommands;
        proto::UpdateAbilitiesPacket p;
        p.entityUniqueId = static_cast<int64_t>(runtimeId);
        p.permissionLevel = isOperator  ? proto::PermissionLevel::Operator
                            : isVisitor ? proto::PermissionLevel::Visitor
                                        : proto::PermissionLevel::Member;
        p.commandPermission =
            isOperator ? proto::CommandPermissionLevel::Operator : proto::CommandPermissionLevel::Normal;
        proto::AbilityLayers layer;
        layer.type = proto::AbilityLayersType::Base;
        layer.allowed = F::Count - 1;
        layer.enabled = enabled;
        const float speed = gameMode == 6 ? static_cast<float>(flySpeedLevel) : 1.0f;
        layer.flySpeed = 0.05f * speed;
        layer.verticalFlySpeed = 1.0f * speed;
        layer.walkSpeed = 0.1f;
        p.abilities.push_back(layer);
        Send(p);
    }

    bool Session::SetFlySpeedLevel(int level) {
        if (gameMode != 6 || (level != 1 && level != 2 && level != 5 && level != 10 && level != 20))
            return false;
        flySpeedLevel = level;
        if (state == State::InGame)
            SendAbilities();
        return true;
    }

    void Session::SendTime(int32_t ticks) {
        proto::SetTimePacket t;
        t.time = ticks;
        Send(t);
    }

    void Session::SendWeather(bool rain, bool thunder) {
        const auto event = [&](proto::LevelEventPacketEvent e, int data) {
            proto::LevelEventPacket p;
            p.event = e;
            p.position = {0, 0, 0};
            p.data = data;
            Send(p);
        };
        event(rain ? proto::LevelEventPacketEvent::StartRain : proto::LevelEventPacketEvent::StopRain,
              rain ? 65535 : 0);
        event(thunder ? proto::LevelEventPacketEvent::StartThunder
                      : proto::LevelEventPacketEvent::StopThunder,
              thunder ? 65535 : 0);
    }

    void Session::SendGameRule(const std::string& lowerName, bool boolean, int value) {
        proto::GameRulesChangedPacket p;
        proto::GameRuleI32 r;
        r.name = lowerName;
        r.editable = true;
        if (boolean) {
            r.type = proto::GameRuleI32Type::Bool;
            r.value = value != 0;
        } else {
            r.type = proto::GameRuleI32Type::Int;
            r.value = static_cast<int32_t>(value);
        }
        p.rules.push_back(std::move(r));
        Send(p);
    }

    void Session::SendSpawnPosition(int x, int y, int z, bool world) {
        proto::SetSpawnPositionPacket p;
        p.spawnType = world ? proto::SetSpawnPositionPacketSpawnType::World
                            : proto::SetSpawnPositionPacketSpawnType::Player;
        p.playerPosition = {x, y, z};
        p.dimension = 0;
        p.worldPosition = {x, y, z};
        Send(p);
    }

    void Session::SendDifficulty(int difficulty) {
        proto::SetDifficultyPacket p;
        p.difficulty = static_cast<uint32_t>(difficulty);
        Send(p);
    }

    void Session::SendTitle(int type, const std::string& text, int fadeIn, int stay, int fadeOut) {
        proto::SetTitlePacket p;
        static const proto::SetTitlePacketType kinds[] = {proto::SetTitlePacketType::Clear,
                                                          proto::SetTitlePacketType::Reset,
                                                          proto::SetTitlePacketType::SetTitle,
                                                          proto::SetTitlePacketType::SetSubtitle,
                                                          proto::SetTitlePacketType::ActionBarMessage,
                                                          proto::SetTitlePacketType::SetDurations};
        p.type = kinds[std::clamp(type, 0, 5)];
        p.text = text;
        p.fadeInTime = fadeIn;
        p.stayTime = stay;
        p.fadeOutTime = fadeOut;
        p.xuid = xuid;
        Send(p);
    }

    void Session::SendActionBar(const std::string& text) {
        SendText(static_cast<uint8_t>(proto::TextPacketType::JukeboxPopup), {}, text);
    }

    void Session::SetGameMode(int mode) {
        if (mode != 0 && mode != 1 && mode != 2 && mode != 6)
            return;
        if (mode == 6 && gameMode != 6)
            flySpeedLevel = 1;
        const int old = gameMode;
        gameMode = mode;
        creative = mode == 1;
        if (state != State::InGame)
            return;
        if (old != mode)
            server.HookGameModeChange(*this, old, mode);
        proto::SetPlayerGameTypePacket p;
        p.gamemode = static_cast<proto::GameMode>(mode);
        Send(p);
        SendAbilities();
    }

    void Session::SetOperator(bool op) {
        if (isOperator == op)
            return;
        isOperator = op;
        if (op)
            isVisitor = false;
        if (state != State::InGame)
            return;
        SendAbilities();
        SendCommands();
    }

    void Session::SendRaw(const std::string& message) {
        size_t from = 0;
        while (from <= message.size()) {
            const size_t nl = message.find('\n', from);
            const std::string line =
                message.substr(from, nl == std::string::npos ? std::string::npos : nl - from);
            SendText(0, {}, line);
            if (nl == std::string::npos)
                break;
            from = nl + 1;
        }
    }

    void Session::SendInventory() {
        const auto content = [&](proto::WindowIDVarint window, proto::ContainerSlotType type,
                                 const ItemStack* stacks, size_t n) {
            proto::InventoryContentPacket p;
            p.windowId = window;
            p.container.containerId = type;
            for (size_t i = 0; i < n; i++)
                p.input.push_back(ToWire(stacks[i]));
            Send(p);
        };
        content(proto::WindowIDVarint::Inventory, proto::ContainerSlotType::HotbarAndInventory,
                inventory.main.data(), inventory.main.size());
        content(proto::WindowIDVarint::Armor, proto::ContainerSlotType::Armor, inventory.armor.data(),
                inventory.armor.size());
        content(proto::WindowIDVarint::Offhand, proto::ContainerSlotType::Offhand, &inventory.offhand, 1);
        SendHeldSelection();
    }

    void Session::SendSlot(int slot) {
        if (slot < 0 || slot >= PlayerInventory::kMainSlots)
            return;
        proto::InventorySlotPacket p;
        p.windowId = proto::WindowIDVarint::Inventory;
        p.slot = static_cast<uint32_t>(slot);
        proto::FullContainerName c;
        c.containerId = proto::ContainerSlotType::HotbarAndInventory;
        p.container = c;
        p.item = ToWire(inventory.main[static_cast<size_t>(slot)]);
        Send(p);
    }

    void Session::SendHeldSelection() {
        proto::MobEquipmentPacket m;
        m.runtimeEntityId = runtimeId;
        m.item = ToWire(inventory.Held());
        m.slot = static_cast<uint8_t>(inventory.selected);
        m.selectedSlot = static_cast<uint8_t>(inventory.selected);
        m.windowId = proto::WindowID::Inventory;
        Send(m);
    }

    void Session::CorrectBlock(int x, int y, int z) {
        server.SendBlockTo(*this, x, y, z);
    }

    bool Session::InReach(int x, int y, int z) const {
        const double dx = x + 0.5 - posX, dy = y + 0.5 - posY, dz = z + 0.5 - posZ;
        return dx * dx + dy * dy + dz * dz <= 12.0 * 12.0;
    }

    void Session::OnStackRequest(std::span<const uint8_t> body) {
        proto::ItemStackRequestPacket p;
        if (!Decode(body, p) || state != State::InGame)
            return;
        proto::ItemStackResponsePacket reply;
        bool armorChanged = false;
        bool containerChanged = false;
        for (const proto::ItemStackRequest& r : p.requests) {
            const bool furnace = inventory.containerKind >= 2 && inventory.container.size() == 3;
            const ItemStack smelted = furnace ? inventory.container[2] : ItemStack{};
            const RequestResult result = ApplyStackRequest(inventory, r, creative);
            if (result.ok && furnace && !smelted.Empty() && inventory.container.size() == 3) {
                const ItemStack& now = inventory.container[2];
                const int taken = now.Empty() || now.networkId != smelted.networkId ? smelted.count : smelted.count - now.count;
                if (taken > 0) {
                    const std::string out = ItemNameOf(smelted.networkId);
                    float each = 0.1f;
                    if (out.ends_with("_ingot") || out == "minecraft:netherite_scrap")
                        each = out.contains("gold") ? 1.0f : out.contains("netherite") ? 2.0f : 0.7f;
                    else if (out == "minecraft:diamond" || out == "minecraft:emerald")
                        each = 1.0f;
                    else if (out == "minecraft:redstone" || out == "minecraft:lapis_lazuli" || out == "minecraft:quartz")
                        each = out == "minecraft:redstone" ? 0.3f : 0.2f;
                    else if (out.starts_with("minecraft:cooked_") || out == "minecraft:baked_potato" ||
                             out == "minecraft:dried_kelp" || out.ends_with("terracotta"))
                        each = 0.35f;
                    else if (out == "minecraft:charcoal" || out == "minecraft:sponge")
                        each = 0.15f;
                    else if (out == "minecraft:green_dye" || out == "minecraft:lime_dye")
                        each = 1.0f;
                    static std::mt19937 rng{std::random_device{}()};
                    const float total = each * static_cast<float>(taken);
                    int whole = static_cast<int>(std::floor(total));
                    if (std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < total - static_cast<float>(whole))
                        whole++;
                    AddExperience(whole);
                }
            }
            if (result.ok)
                for (const auto& [container, slot] : result.touched)
                    if (container != proto::ContainerSlotType::HotbarAndInventory && container != proto::ContainerSlotType::Hotbar &&
                        container != proto::ContainerSlotType::Inventory && container != proto::ContainerSlotType::Armor &&
                        container != proto::ContainerSlotType::Offhand && container != proto::ContainerSlotType::Cursor &&
                        container != proto::ContainerSlotType::CraftingInput)
                        containerChanged = true;
            reply.responses.push_back(MakeResponse(inventory, r.requestId, result));
            if (result.ok)
                for (const auto& [container, slot] : result.touched)
                    if (container == proto::ContainerSlotType::Armor) {
                        armorChanged = true;
                        break;
                    }
            if (result.ok)
                for (const ItemStack& thrown : result.dropped)
                    server.DropFromPlayer(*this, thrown);
        }
        if (armorChanged)
            server.BroadcastArmor(*this);
        if (containerChanged)
            server.SyncContainer(*this);
        if (!reply.responses.empty())
            Send(reply);
    }

    void Session::OnMobEquipment(std::span<const uint8_t> body) {
        proto::MobEquipmentPacket m;
        if (!Decode(body, m) || state != State::InGame)
            return;
        if (m.selectedSlot < PlayerInventory::kHotbar) {
            if (inventory.selected != m.selectedSlot)
                StopUsing();
            inventory.selected = m.selectedSlot;
        }
    }

    void Session::OnInteract(std::span<const uint8_t> body) {
        proto::InteractPacket i;
        if (!Decode(body, i) || state != State::InGame)
            return;
        if (i.actionId == proto::InteractPacketActionId::OpenInventory) {
            proto::ContainerOpenPacket open;
            open.windowId = proto::WindowID::Inventory;
            open.windowType = proto::WindowType::Inventory;
            open.coordinates = {static_cast<int32_t>(std::floor(posX)),
                                static_cast<int32_t>(std::floor(posY)),
                                static_cast<int32_t>(std::floor(posZ))};
            open.runtimeEntityId = -1;
            Send(open);
        }
    }

    void Session::OnContainerClose(std::span<const uint8_t> body) {
        proto::ContainerClosePacket c;
        if (!Decode(body, c) || state != State::InGame)
            return;
        server.CloseContainer(*this);
        proto::ContainerClosePacket ack;
        ack.windowId = c.windowId;
        ack.windowType = openType;
        ack.server = false;
        Send(ack);
        openType = proto::WindowType::Inventory;
    }

    void Session::OnPickBlock(std::span<const uint8_t> body) {
        proto::BlockPickRequestPacket p;
        if (!Decode(body, p) || state != State::InGame || !InReach(p.x, p.y, p.z))
            return;
        std::string blockName;
        if (server.GetBlock(p.x, p.y, p.z, blockName) != perlin::abi::Status::Ok)
            return;
        const CatalogItem* item = BlockCatalog::Instance().ItemByName(blockName);
        if (!item)
            return;
        int slot = -1;
        for (int i = 0; i < PlayerInventory::kHotbar; i++)
            if (!inventory.main[static_cast<size_t>(i)].Empty() &&
                inventory.main[static_cast<size_t>(i)].networkId == item->networkId)
                slot = i;
        if (slot < 0 && creative) {
            slot = inventory.selected;
            for (int i = 0;
                 i < PlayerInventory::kHotbar && !inventory.main[static_cast<size_t>(slot)].Empty(); i++)
                if (inventory.main[static_cast<size_t>(i)].Empty())
                    slot = i;
            inventory.main[static_cast<size_t>(slot)] =
                inventory.MakeStack(item->networkId, MaxStack(item->networkId));
            SendSlot(slot);
        }
        if (slot < 0)
            return;
        inventory.selected = slot;
        SendHeldSelection();
    }

    void Session::OnPlayerAction(std::span<const uint8_t> body) {
        proto::PlayerActionPacket a;
        if (!Decode(body, a) || state != State::InGame)
            return;
        const auto& pos = a.position;
        if (a.action == proto::Action::DimensionChangeAck) {
            awaitingDimensionAck = false;
            return;
        }
        switch (a.action) {
        case proto::Action::StartSprint:
        case proto::Action::StopSprint:
            sprinting = a.action == proto::Action::StartSprint && (food > 6 || gameMode == 1);
            dataDirty = true;
            break;
        case proto::Action::Swimming:
        case proto::Action::StopSwimming:
            swimming = a.action == proto::Action::Swimming;
            dataDirty = true;
            break;
        case proto::Action::StartGlide:
        case proto::Action::StopGlide:
            gliding = a.action == proto::Action::StartGlide;
            dataDirty = true;
            break;
        case proto::Action::Jump:
            Exhaust(sprinting ? 0.2f : 0.05f);
            break;
        case proto::Action::StartBreak:
            if (isVisitor || gameMode == 6)
                break;
            if (creative)
                server.PlayerBreak(*this, pos.x, pos.y, pos.z);
            break;
        case proto::Action::StopBreak:
        case proto::Action::CreativePlayerDestroyBlock:
            if (isVisitor || gameMode == 6)
                break;
            lastActive = std::chrono::steady_clock::now();
            server.PlayerBreak(*this, pos.x, pos.y, pos.z);
            break;
        default:
            break;
        }
    }

    void Session::OnTransaction(std::span<const uint8_t> body) {
        proto::InventoryTransactionPacket t;
        if (!Decode(body, t) || state != State::InGame)
            return;
        if (t.transaction.transactionType == proto::TransactionTransactionType::ItemUseOnEntity) {
            const auto* on = std::get_if<proto::TransactionTransactionDataCase1>(&t.transaction.transactionData);
            if (!on || isVisitor || gameMode == 6 || dead)
                return;
            lastActive = std::chrono::steady_clock::now();
            if (on->actionType == proto::TransactionTransactionDataCase1ActionType::Attack) {
                if (on->hotbarSlot >= 0 && on->hotbarSlot < PlayerInventory::kHotbar)
                    inventory.selected = on->hotbarSlot;
                const ItemStack held = inventory.Held();
                float damage = AttackDamage(held);
                if (server.HookPlayerAttack(*this, on->entityRuntimeId, damage))
                    return;
                if (Session* victim = server.FindSession(on->entityRuntimeId)) {
                    AttackPlayer(*victim, held);
                    return;
                }
                const bool crit = !onGround && fallDistance > 0.0f && !swimming && !gliding && !flying;
                if (server.Mobs().Attack(*this, on->entityRuntimeId, damage, sprinting ? 0.9f : 0.4f)) {
                    Exhaust(0.1f);
                    if (crit)
                        for (Session* s : server.PlayersIn(dimension))
                            s->MobEvent(on->entityRuntimeId, 17, 0);
                    const std::string n = held.Empty() ? std::string() : ItemNameOf(held.networkId);
                    if (n.ends_with("_sword") || n == "minecraft:trident" || n == "minecraft:mace")
                        DamageHeld(1);
                    else if (n.ends_with("_axe") || n.ends_with("_pickaxe") || n.ends_with("_shovel") || n.ends_with("_hoe"))
                        DamageHeld(2);
                    if (sprinting) {
                        sprinting = false;
                        dataDirty = true;
                    }
                }
            } else {
                server.Mobs().Interact(*this, on->entityRuntimeId);
            }
            return;
        }
        if (t.transaction.transactionType == proto::TransactionTransactionType::ItemRelease) {
            const auto* release = std::get_if<proto::TransactionTransactionDataCase2>(&t.transaction.transactionData);
            if (!release || dead)
                return;
            if (useTicks >= 0 && ItemNameOf(useItem) == "minecraft:bow") {
                ReleaseBow();
            } else if (release->actionType == proto::TransactionTransactionDataCase2ActionType::Consume && useTicks >= 0) {
                const FoodInfo* f = FoodOf(ItemNameOf(useItem));
                if (f && useTicks >= f->ticks - 6)
                    FinishUsing();
                else
                    StopUsing();
            } else {
                StopUsing();
            }
            return;
        }
        if (t.transaction.transactionType != proto::TransactionTransactionType::ItemUse)
            return;
        const auto* use = std::get_if<proto::TransactionUseItem>(&t.transaction.transactionData);
        if (!use)
            return;
        if (use->hotbarSlot >= 0 && use->hotbarSlot < PlayerInventory::kHotbar)
            inventory.selected = use->hotbarSlot;
        const auto& at = use->blockPosition;
        if (isVisitor || gameMode == 6)
            return;
        lastActive = std::chrono::steady_clock::now();
        switch (use->actionType) {
        case proto::TransactionUseItemActionType::BreakBlock:
            server.PlayerBreak(*this, at.x, at.y, at.z);
            break;
        case proto::TransactionUseItemActionType::ClickBlock:
            if (server.HookPlayerUse(*this, at.x, at.y, at.z, use->face)) {
                CorrectBlock(at.x, at.y, at.z);
                SendInventory();
                break;
            }
            if ((sneaking && !inventory.Held().Empty()) || !server.UseBlock(*this, at.x, at.y, at.z)) {
                if (!EquipHeldArmor()) {
                    const ItemStack& held = inventory.Held();
                    const std::string n = held.Empty() ? std::string() : ItemNameOf(held.networkId);
                    if (!held.Empty() && (FoodOf(n) || n == "minecraft:bow")) {
                        if (useTicks < 0)
                            StartUsing();
                    } else {
                        server.PlayerPlace(*this, at.x, at.y, at.z, use->face);
                    }
                }
            }
            break;
        case proto::TransactionUseItemActionType::ClickAir:
            if (!EquipHeldArmor() && useTicks < 0)
                StartUsing();
            break;
        default:
            break;
        }
    }

    void Session::OnChunkRadius(std::span<const uint8_t> body) {
        proto::RequestChunkRadiusPacket req;
        if (!Decode(body, req) || state == State::AwaitSettings || state == State::AwaitLogin)
            return;
        radius = std::clamp<int32_t>(req.chunkRadius, 2, static_cast<int32_t>(server.Config().viewDistance));
        proto::ChunkRadiusUpdatePacket reply;
        reply.chunkRadius = radius;
        Send(reply);
        PublishCenter(true);
    }

    void Session::SwitchDimension(int to, double x, double y, double z) {
        proto::ChangeDimensionPacket change;
        change.dimension = to;
        change.position = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
        change.respawn = false;
        Send(change);
        dimension = to;
        awaitingDimensionAck = true;
        dimensionChangedAt = std::chrono::steady_clock::now();
        sent.clear();
        posX = x;
        posY = y;
        posZ = z;
        moved = true;
        PublishCenter(true);
    }

    void Session::PublishCenter(bool force) {
        const ChunkPos now{static_cast<int32_t>(std::floor(posX)) >> 4,
                           static_cast<int32_t>(std::floor(posZ)) >> 4};
        if (!force && centerSent && now == center)
            return;
        center = now;
        centerSent = true;
        if (radius == 0)
            return;
        proto::NetworkChunkPublisherUpdatePacket u;
        u.coordinates = {static_cast<int32_t>(std::floor(posX)), static_cast<int32_t>(std::floor(posY)),
                         static_cast<int32_t>(std::floor(posZ))};
        u.radius = static_cast<uint32_t>(radius * 16);
        Send(u);
        for (auto it = sent.begin(); it != sent.end();) {
            if (std::abs(it->x - center.x) > radius + 1 || std::abs(it->z - center.z) > radius + 1)
                it = sent.erase(it);
            else
                ++it;
        }
    }

    bool Session::WantsChunk(ChunkPos pos) const {
        if (radius == 0 || sent.contains(pos))
            return false;
        const int dx = pos.x - center.x, dz = pos.z - center.z;
        return dx * dx + dz * dz <= radius * radius;
    }

    void Session::StreamChunks() {
        if (radius == 0 || !centerSent)
            return;
        std::vector<ChunkPos> want;
        for (int dx = -radius; dx <= radius; dx++)
            for (int dz = -radius; dz <= radius; dz++) {
                const ChunkPos p{center.x + dx, center.z + dz};
                if (WantsChunk(p))
                    want.push_back(p);
            }
        std::sort(want.begin(), want.end(), [this](const ChunkPos& a, const ChunkPos& b) {
            const int da = (a.x - center.x) * (a.x - center.x) + (a.z - center.z) * (a.z - center.z);
            const int db = (b.x - center.x) * (b.x - center.x) + (b.z - center.z) * (b.z - center.z);
            return da < db;
        });
        int budget = connection->Local() ? kChunksPerTick * 8
                     : state == State::Loading ? kChunksPerTick * 3
                                               : kChunksPerTick * 2;
        int asked = 0;
        for (const ChunkPos& pos : want) {
            auto entry = server.World(dimension).Get(pos);
            if (!entry) {
                if (++asked >= 128)
                    break;
                continue;
            }
            if (budget <= 0)
                break;
            const std::vector<uint8_t>& payload = server.World(dimension).Payload(*entry);
            proto::LevelChunkPacket lc;
            lc.x = pos.x;
            lc.z = pos.z;
            lc.dimension = dimension;
            lc.subChunkCount = static_cast<uint32_t>(RangeOf(dimension).SectionCount());
            lc.cacheEnabled = false;
            lc.payload = payload;
            Send(lc);
            sent.insert(pos);
            budget--;
        }
        if (!spawnSent && state == State::Loading) {
            size_t near = 0;
            for (const ChunkPos& p : sent)
                if (std::abs(p.x - center.x) <= 1 && std::abs(p.z - center.z) <= 1)
                    near++;
            if (near >= 9 || (radius < 2 && !sent.empty())) {
                proto::PlayStatusPacket spawn;
                spawn.status = proto::PlayStatusPacketStatus::PlayerSpawn;
                Send(spawn);
                spawnSent = true;
            }
        }
    }

    void Session::OnInitialized() {
        if (state != State::Loading)
            return;
        state = State::InGame;
        server.OnPlayerJoined(*this);
    }

    void Session::OnText(std::span<const uint8_t> body) {
        proto::TextPacket t;
        if (!Decode(body, t) || state != State::InGame)
            return;
        if (t.type != proto::TextPacketType::Chat || t.message.empty())
            return;
        std::string message = t.message;
        if (message.size() > 512)
            message.resize(512);
        lastActive = std::chrono::steady_clock::now();
        server.HandleChat(*this, message);
    }

    void Session::OnAnimate(std::span<const uint8_t> body) {
        proto::AnimatePacket p;
        if (!Decode(body, p) || state != State::InGame)
            return;
        p.runtimeEntityId = runtimeId;
        for (Session* other : server.InGamePlayers())
            if (other != this)
                other->Send(p);
    }

    void Session::OnPlayerSkin(std::span<const uint8_t> body) {
        proto::PlayerSkinPacket p;
        if (!Decode(body, p) || state != State::InGame)
            return;
        *skin = p.skin;
        p.uuid.bytes = uuid;
        for (Session* other : server.InGamePlayers())
            if (other != this)
                other->Send(p);
    }

    void Session::OnTickSync(std::span<const uint8_t> body) {
        proto::TickSyncPacket p;
        if (!Decode(body, p))
            return;
        p.responseTime = static_cast<int64_t>(server.TickCount());
        Send(p);
    }

    void Session::OnLatency(std::span<const uint8_t> body) {
        proto::NetworkStackLatencyPacket p;
        if (!Decode(body, p) || !p.needsResponse)
            return;
        p.needsResponse = 0;
        Send(p);
    }

    void Session::OnEmote(std::span<const uint8_t> body) {
        proto::EmotePacket p;
        if (!Decode(body, p) || state != State::InGame)
            return;
        p.entityId = runtimeId;
        p.xuid = xuid;
        for (Session* other : server.InGamePlayers())
            if (other != this)
                other->Send(p);
    }

    void Session::OnSound(std::span<const uint8_t> body) {
        proto::LevelSoundEventPacket p;
        if (!Decode(body, p) || state != State::InGame)
            return;
        for (Session* other : server.InGamePlayers())
            if (other != this)
                other->Send(p);
    }

    void Session::OnRespawn(std::span<const uint8_t> body) {
        proto::RespawnPacket p;
        if (!Decode(body, p) || state != State::InGame)
            return;
        const bool wasDead = dead;
        if (dead) {
            dead = false;
            health = MaxHealth();
            food = 20;
            saturation = 5.0f;
            exhaustion = 0;
            foodTimer = 0;
            absorption = 0;
            fireTicks = 0;
            air = 300;
            fallDistance = 0;
            invulnerable = 60;
            dataDirty = true;
            posX = (hasSpawnPoint ? spawnPointX : server.SpawnX()) + 0.5;
            posY = (hasSpawnPoint ? spawnPointY : server.SpawnY()) + 1.62;
            posZ = (hasSpawnPoint ? spawnPointZ : server.SpawnZ()) + 0.5;
            SendAttributes();
        }
        proto::RespawnPacket reply;
        reply.position = {static_cast<float>(posX), static_cast<float>(posY), static_cast<float>(posZ)};
        reply.state = 1;
        reply.runtimeEntityId = runtimeId;
        Send(reply);
        if (wasDead && dimension != kOverworld)
            server.ChangeDimension(*this, kOverworld, posX, posY, posZ);
        else if (wasDead)
            Teleport(posX, posY, posZ);
        if (wasDead)
            server.HookPlayerRespawn(*this);
    }

    void Session::OnCommand(std::span<const uint8_t> body) {
        proto::CommandRequestPacket c;
        if (!Decode(body, c) || state != State::InGame)
            return;
        lastActive = std::chrono::steady_clock::now();
        server.RunPlayerCommand(*this, c.command);
    }

    void Session::SetPosition(double x, double y, double z, float p, float yawValue) {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            return;
        if (std::abs(x) > 3.0e7 || std::abs(z) > 3.0e7 || std::abs(y) > 1.0e5)
            return;
        if (awaitingDimensionAck) {
            if (std::chrono::steady_clock::now() - dimensionChangedAt < std::chrono::seconds(15))
                return;
            awaitingDimensionAck = false;
        }
        const bool changed = x != posX || y != posY || z != posZ || p != pitch || yawValue != yaw;
        if (changed)
            lastActive = std::chrono::steady_clock::now();
        moved = moved || changed;
        posX = x;
        posY = y;
        posZ = z;
        pitch = p;
        yaw = yawValue;
        if (state == State::InGame || state == State::Loading)
            PublishCenter(false);
    }

    void Session::OnAuthInput(std::span<const uint8_t> body) {
        proto::PlayerAuthInputPacket in;
        if (!Decode(body, in) || (state != State::InGame && state != State::Loading))
            return;
        const double previousY = posY;
        SetPosition(in.position.x, in.position.y, in.position.z, in.pitch, in.yaw);
        const bool wasSneaking = sneaking;
        sneaking = std::find(in.inputData.begin(), in.inputData.end(), proto::InputData::Sneaking) != in.inputData.end();
        if (wasSneaking != sneaking)
            dataDirty = true;
        if (state != State::InGame)
            return;
        if (std::abs(posX - lastHookX) + std::abs(posY - lastHookY) + std::abs(posZ - lastHookZ) > 0.1) {
            lastHookX = posX;
            lastHookY = posY;
            lastHookZ = posZ;
            server.HookPlayerMove(*this);
        }
        if (!awaitingDimensionAck)
            MovementTick(in, previousY);
        if (in.blockAction && !isVisitor && gameMode != 6 && !dead)
            for (const auto& action : *in.blockAction) {
                const auto& at = action.position;
                if (action.action == proto::Action::PredictBreak ||
                    (action.action == proto::Action::StartBreak && creative) ||
                    action.action == proto::Action::CreativePlayerDestroyBlock) {
                    lastActive = std::chrono::steady_clock::now();
                    server.PlayerBreak(*this, at.x, at.y, at.z);
                }
            }
    }

    void Session::OnMovePlayer(std::span<const uint8_t> body) {
        proto::MovePlayerPacket m;
        if (!Decode(body, m) || (state != State::InGame && state != State::Loading))
            return;
        SetPosition(m.position.x, m.position.y, m.position.z, m.pitch, m.yaw);
    }

    void Session::Teleport(double x, double y, double z, float yawDegrees, float pitchDegrees) {
        yaw = yawDegrees;
        pitch = pitchDegrees;
        Teleport(x, y, z);
    }

    void Session::Teleport(double x, double y, double z) {
        posX = x;
        posY = y;
        posZ = z;
        proto::MovePlayerPacket m;
        m.runtimeId = runtimeId;
        m.position = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
        m.pitch = pitch;
        m.yaw = yaw;
        m.headYaw = yaw;
        m.mode = proto::MovePlayerPacketMode::Teleport;
        m.teleport = proto::MovePlayerPacketTeleport{};
        Send(m);
        PublishCenter(false);
    }

    void Session::SendText(uint8_t type, const std::string& source, const std::string& message) {
        proto::TextPacket t;
        t.needsTranslation = false;
        t.category = type == static_cast<uint8_t>(proto::TextPacketType::Chat)
                         ? proto::TextPacketCategory::Authored
                         : proto::TextPacketCategory::MessageOnly;
        t.type = static_cast<proto::TextPacketType>(type);
        t.sourceName = source;
        t.message = message;
        Send(t);
    }

    void Session::ShowArmor(const Session& wearer) {
        proto::MobArmorEquipmentPacket p;
        p.runtimeEntityId = wearer.runtimeId;
        p.helmet = ToWire(wearer.inventory.armor[0]);
        p.chestplate = ToWire(wearer.inventory.armor[1]);
        p.leggings = ToWire(wearer.inventory.armor[2]);
        p.boots = ToWire(wearer.inventory.armor[3]);
        Send(p);
    }

    bool Session::EquipHeldArmor() {
        ItemStack& held = inventory.main[static_cast<size_t>(inventory.selected)];
        if (held.Empty() || held.count != 1)
            return false;
        const int slot = ArmorSlotFor(held.networkId);
        if (slot < 0)
            return false;
        std::swap(held, inventory.armor[static_cast<size_t>(slot)]);
        if (!held.Empty())
            held.stackId = inventory.NewStackId();
        inventory.armor[static_cast<size_t>(slot)].stackId = inventory.NewStackId();
        SendSlot(inventory.selected);
        proto::InventorySlotPacket p;
        p.windowId = proto::WindowIDVarint::Armor;
        p.slot = static_cast<uint32_t>(slot);
        proto::FullContainerName c;
        c.containerId = proto::ContainerSlotType::Armor;
        p.container = c;
        p.item = ToWire(inventory.armor[static_cast<size_t>(slot)]);
        Send(p);
        SendHeldSelection();
        server.BroadcastArmor(*this);
        return true;
    }

    void Session::ShowPlayer(const Session& other) {
        proto::PlayerListPacket list;
        proto::PlayerRecord rec;
        rec.type = proto::PlayerRecordType::Add;
        rec.uuid.bytes = other.uuid;
        rec.entityUniqueId = static_cast<int64_t>(other.runtimeId);
        rec.username = other.name;
        rec.xboxUserId = other.xuid;
        rec.skinData = other.GetSkin();
        rec.playerColor = -1;
        list.records.push_back(std::move(rec));
        Send(list);

        proto::AddPlayerPacket add;
        add.uuid.bytes = other.uuid;
        add.username = other.name;
        add.runtimeId = other.runtimeId;
        add.position = {static_cast<float>(other.posX), static_cast<float>(other.posY),
                        static_cast<float>(other.posZ)};
        add.pitch = other.pitch;
        add.yaw = other.yaw;
        add.headYaw = other.yaw;
        add.gamemode = static_cast<proto::GameMode>(other.gameMode);
        add.uniqueId = static_cast<int64_t>(other.runtimeId);
        add.permissionLevel = other.isOperator  ? proto::PermissionLevel::Operator
                              : other.isVisitor ? proto::PermissionLevel::Visitor
                                                : proto::PermissionLevel::Member;
        add.commandPermission = other.isOperator ? proto::CommandPermissionLevel::Operator
                                                 : proto::CommandPermissionLevel::Normal;
        Send(add);
    }

    void Session::HidePlayer(const Session& other) {
        proto::RemoveEntityPacket r;
        r.entityIdSelf = static_cast<int64_t>(other.runtimeId);
        Send(r);
        proto::PlayerListPacket list;
        proto::PlayerRecord rec;
        rec.type = proto::PlayerRecordType::Remove;
        rec.uuid.bytes = other.uuid;
        list.records.push_back(std::move(rec));
        Send(list);
    }

    void Session::SendMove(const Session& other, uint8_t mode) {
        proto::MovePlayerPacket m;
        m.runtimeId = other.runtimeId;
        m.position = {static_cast<float>(other.posX), static_cast<float>(other.posY),
                      static_cast<float>(other.posZ)};
        m.pitch = other.pitch;
        m.yaw = other.yaw;
        m.headYaw = other.yaw;
        m.mode = static_cast<proto::MovePlayerPacketMode>(mode);
        Send(m);
    }

    void Session::ShowItem(const ItemEntity& item) {
        proto::AddItemEntityPacket p;
        p.entityIdSelf = static_cast<int64_t>(item.id);
        p.runtimeEntityId = item.id;
        p.item = ToWire(item.stack);
        p.position = {static_cast<float>(item.x), static_cast<float>(item.y), static_cast<float>(item.z)};
        p.velocity = {static_cast<float>(item.vx), static_cast<float>(item.vy), static_cast<float>(item.vz)};
        Send(p);
    }

    void Session::HideItem(uint64_t id) {
        proto::RemoveEntityPacket p;
        p.entityIdSelf = static_cast<int64_t>(id);
        Send(p);
    }

    void Session::MoveItem(const ItemEntity& item) {
        proto::MoveEntityPacket p;
        p.runtimeEntityId = item.id;
        p.flags = item.onGround ? 1 : 0;
        p.position = {static_cast<float>(item.x), static_cast<float>(item.y), static_cast<float>(item.z)};
        Send(p);
    }

    void Session::TakeItem(uint64_t itemId, uint64_t takerRuntimeId) {
        proto::TakeItemEntityPacket p;
        p.runtimeEntityId = takerRuntimeId;
        p.target = itemId;
        Send(p);
    }

    namespace {
        uint8_t MobAngle(float degrees) {
            return static_cast<uint8_t>(static_cast<int>(std::floor(degrees / 360.0f * 256.0f)) & 255);
        }

        proto::MetadataDictionaryEntry FloatEntry(proto::MetadataDictionaryEntryKey key, float value) {
            proto::MetadataDictionaryEntry e;
            e.key = key;
            e.type = proto::MetadataDictionaryEntryType::Float;
            auto& inner = e.value.template emplace<4>();
            inner.template emplace<4>(value);
            return e;
        }

        proto::MetadataDictionaryEntry IntEntry(proto::MetadataDictionaryEntryKey key, int32_t value) {
            proto::MetadataDictionaryEntry e;
            e.key = key;
            e.type = proto::MetadataDictionaryEntryType::Int;
            auto& inner = e.value.template emplace<4>();
            inner.template emplace<3>(value);
            return e;
        }

        std::vector<proto::MetadataDictionaryEntry> MobMetadata(const Mob& mob) {
            std::vector<proto::MetadataDictionaryEntry> out;
            if (mob.kind == 1)
                out.push_back(IntEntry(proto::MetadataDictionaryEntryKey::Variant, BlockRegistry::Instance().Hash(mob.block)));
            if (mob.kind == 2)
                out.push_back(IntEntry(proto::MetadataDictionaryEntryKey::FuseLength, std::max(0, mob.fuse)));
            if (mob.kind == 3)
                out.push_back(IntEntry(proto::MetadataDictionaryEntryKey::ExperienceValue, mob.block));
            proto::MetadataDictionaryEntry flags;
            flags.key = proto::MetadataDictionaryEntryKey::Flags;
            flags.type = proto::MetadataDictionaryEntryType::Long;
            flags.value.template emplace<1>(static_cast<int64_t>(MobWorld::Flags(mob)));
            out.push_back(flags);
            const float scale = mob.baby ? 0.5f : 1.0f;
            out.push_back(FloatEntry(proto::MetadataDictionaryEntryKey::Scale, scale));
            out.push_back(FloatEntry(proto::MetadataDictionaryEntryKey::BoundingboxWidth, mob.def->width * scale));
            out.push_back(FloatEntry(proto::MetadataDictionaryEntryKey::BoundingboxHeight, mob.def->height * scale));
            return out;
        }
    }

    void Session::ShowMob(const Mob& mob) {
        proto::AddEntityPacket p;
        p.uniqueId = static_cast<int64_t>(mob.id);
        p.runtimeId = mob.id;
        p.entityType = mob.def->id;
        p.position = {static_cast<float>(mob.x), static_cast<float>(mob.y), static_cast<float>(mob.z)};
        p.velocity = {static_cast<float>(mob.vx), static_cast<float>(mob.vy), static_cast<float>(mob.vz)};
        p.pitch = mob.pitch;
        p.yaw = mob.yaw;
        p.headYaw = mob.headYaw;
        p.bodyYaw = mob.yaw;
        proto::EntityAttributesEntry hp;
        hp.name = "minecraft:health";
        hp.min = 0;
        hp.value = mob.health;
        hp.max = mob.def->health;
        p.attributes.push_back(hp);
        proto::EntityAttributesEntry move;
        move.name = "minecraft:movement";
        move.min = 0;
        move.value = mob.def->speed;
        move.max = 3.4028235e38f;
        p.attributes.push_back(move);
        p.metadata = MobMetadata(mob);
        Send(p);
    }

    void Session::PlaySound(const std::string& sound, double x, double y, double z, int extra) {
        proto::LevelSoundEventPacket p;
        p.soundId = sound;
        p.position = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
        p.extraData = extra;
        p.entityType = ":";
        p.entityUniqueId = -1;
        Send(p);
    }

    void Session::SendLevelEvent(int event, double x, double y, double z, int data) {
        proto::LevelEventPacket p;
        p.event = static_cast<proto::LevelEventPacketEvent>(event);
        p.position = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
        p.data = data;
        Send(p);
    }

    void Session::MoveMob(const Mob& mob) {
        proto::MoveEntityPacket p;
        p.runtimeEntityId = mob.id;
        p.flags = mob.onGround ? 1 : 0;
        p.position = {static_cast<float>(mob.x), static_cast<float>(mob.y), static_cast<float>(mob.z)};
        p.rotation.pitch = MobAngle(mob.pitch);
        p.rotation.yaw = MobAngle(mob.yaw);
        p.rotation.headYaw = MobAngle(mob.headYaw);
        Send(p);
    }

    void Session::MobData(const Mob& mob) {
        proto::SetEntityDataPacket p;
        p.runtimeEntityId = mob.id;
        p.metadata = MobMetadata(mob);
        p.tick = server.TickCount();
        Send(p);
    }

    void Session::MobMotion(const Mob& mob) {
        proto::SetEntityMotionPacket p;
        p.runtimeEntityId = mob.id;
        p.velocity = {static_cast<float>(mob.vx), static_cast<float>(mob.vy), static_cast<float>(mob.vz)};
        p.tick = server.TickCount();
        Send(p);
    }

    void Session::MobEvent(uint64_t id, int event, int data) {
        proto::EntityEventPacket p;
        p.runtimeEntityId = id;
        p.eventId = static_cast<proto::EntityEventPacketEventId>(event);
        p.data = data;
        Send(p);
    }

    void Session::OpenWindow(int window, proto::WindowType type, int x, int y, int z) {
        openWindow = window;
        openType = type;
        proto::ContainerOpenPacket open;
        open.windowId = static_cast<proto::WindowID>(window);
        open.windowType = type;
        open.coordinates = {x, y, z};
        open.runtimeEntityId = -1;
        Send(open);
    }

    void Session::SendWindowContents(proto::ContainerSlotType type, const std::vector<ItemStack>& slots) {
        if (openWindow < 0)
            return;
        proto::InventoryContentPacket p;
        p.windowId = static_cast<proto::WindowIDVarint>(openWindow);
        p.container.containerId = type;
        for (const ItemStack& s : slots)
            p.input.push_back(ToWire(s));
        Send(p);
    }

    void Session::SendWindowData(int property, int value) {
        if (openWindow < 0)
            return;
        proto::ContainerSetDataPacket p;
        p.windowId = static_cast<proto::WindowID>(openWindow);
        p.property = property;
        p.value = value;
        Send(p);
    }

    void Session::CloseWindowFromServer() {
        if (openWindow < 0)
            return;
        proto::ContainerClosePacket p;
        p.windowId = static_cast<proto::WindowID>(openWindow);
        p.windowType = openType;
        p.server = true;
        Send(p);
        openWindow = -1;
    }

    float Session::MaxHealth() const {
        return 20.0f + 4.0f * static_cast<float>(EffectLevel(21));
    }

    int Session::EffectLevel(int id) const {
        const auto it = effects.find(id);
        return it == effects.end() ? 0 : it->second.amplifier + 1;
    }

    int Session::ArmorValue() const {
        int points = 0;
        for (const ItemStack& a : inventory.armor)
            if (!a.Empty())
                points += ArmorPoints(a.networkId);
        return points;
    }

    void Session::SendEffect(int id, int event) {
        if (state != State::InGame)
            return;
        proto::MobEffectPacket p;
        p.runtimeEntityId = runtimeId;
        p.eventId = static_cast<proto::MobEffectPacketEventId>(event);
        p.effectId = id;
        if (const auto it = effects.find(id); it != effects.end()) {
            p.amplifier = it->second.amplifier;
            p.particles = it->second.particles;
            p.duration = it->second.ticks;
            p.ambient = it->second.ambient;
        }
        p.tick = server.TickCount();
        Send(p);
    }

    void Session::AddEffect(int id, int amplifier, int ticks, bool particles, bool ambient) {
        if (id <= 0 || ticks <= 0)
            return;
        if (id == 6) {
            Heal(static_cast<float>(4 << std::clamp(amplifier, 0, 10)));
            return;
        }
        if (id == 7) {
            Damage(static_cast<float>(6 << std::clamp(amplifier, 0, 10)), DamageCause::Magic, posX, posZ);
            return;
        }
        if (id == 23) {
            SetFood(food + amplifier + 1, saturation + static_cast<float>(amplifier + 1) * 2.0f);
            return;
        }
        auto it = effects.find(id);
        const bool fresh = it == effects.end();
        if (!fresh && it->second.amplifier > amplifier)
            return;
        if (!fresh && it->second.amplifier == amplifier && it->second.ticks > ticks)
            return;
        ActiveEffect& e = effects[id];
        e.amplifier = amplifier;
        e.ticks = ticks;
        e.particles = particles;
        e.ambient = ambient;
        if (id == 22)
            absorption = std::max(absorption, 4.0f * static_cast<float>(amplifier + 1));
        SendEffect(id, fresh ? 1 : 2);
        attributesDirty = true;
    }

    bool Session::RemoveEffect(int id) {
        const auto it = effects.find(id);
        if (it == effects.end())
            return false;
        SendEffect(id, 3);
        effects.erase(it);
        if (id == 22)
            absorption = 0;
        if (id == 21)
            health = std::min(health, MaxHealth());
        attributesDirty = true;
        return true;
    }

    void Session::ClearEffects() {
        std::vector<int> ids;
        for (const auto& [id, e] : effects)
            ids.push_back(id);
        for (int id : ids)
            RemoveEffect(id);
    }

    void Session::SetFood(int value, float sat) {
        food = std::clamp(value, 0, 20);
        saturation = std::clamp(sat, 0.0f, static_cast<float>(food));
        attributesDirty = true;
    }

    void Session::Exhaust(float amount) {
        if (gameMode == 1 || gameMode == 6 || dead)
            return;
        exhaustion = std::min(40.0f, exhaustion + amount);
    }

    void Session::OnBlockMined(float hardness) {
        Exhaust(0.005f);
        if (hardness <= 0.0f || gameMode == 1)
            return;
        const ItemStack& held = inventory.Held();
        if (held.Empty())
            return;
        const std::string n = ItemNameOf(held.networkId);
        if (n.ends_with("_sword") || n.ends_with("trident") || n.ends_with("mace"))
            DamageHeld(2);
        else if (n.ends_with("_pickaxe") || n.ends_with("_axe") || n.ends_with("_shovel") || n.ends_with("_hoe") ||
                 n.ends_with("shears"))
            DamageHeld(1);
    }

    void Session::DamageHeld(int amount) {
        if (gameMode == 1 || gameMode == 6)
            return;
        ItemStack& held = inventory.main[static_cast<size_t>(inventory.selected)];
        if (held.Empty())
            return;
        const int max = MaxDurability(held.networkId);
        if (max <= 0)
            return;
        held.damage = static_cast<uint16_t>(std::min(max + 1, held.damage + amount));
        if (held.damage >= max) {
            held = ItemStack{};
            for (Session* s : server.PlayersIn(dimension))
                s->PlaySound("Break", posX, posY, posZ);
        } else {
            held.stackId = inventory.NewStackId();
        }
        SendSlot(inventory.selected);
        SendHeldSelection();
    }

    void Session::SendArmorSlot(int slot) {
        proto::InventorySlotPacket p;
        p.windowId = proto::WindowIDVarint::Armor;
        p.slot = static_cast<uint32_t>(slot);
        proto::FullContainerName c;
        c.containerId = proto::ContainerSlotType::Armor;
        p.container = c;
        p.item = ToWire(inventory.armor[static_cast<size_t>(slot)]);
        Send(p);
    }

    void Session::DamageArmor(float amount) {
        if (gameMode == 1 || gameMode == 6)
            return;
        const int loss = std::max(1, static_cast<int>(amount / 4.0f));
        bool changed = false;
        for (int slot = 0; slot < 4; slot++) {
            ItemStack& a = inventory.armor[static_cast<size_t>(slot)];
            if (a.Empty())
                continue;
            const int max = MaxDurability(a.networkId);
            if (max <= 0)
                continue;
            a.damage = static_cast<uint16_t>(std::min(max + 1, a.damage + loss));
            if (a.damage >= max) {
                a = ItemStack{};
                for (Session* s : server.PlayersIn(dimension))
                    s->PlaySound("Break", posX, posY, posZ);
            } else {
                a.stackId = inventory.NewStackId();
            }
            SendArmorSlot(slot);
            changed = true;
        }
        if (changed)
            server.BroadcastArmor(*this);
    }

    bool Session::Shielding(double fromX, double fromZ) const {
        if (!sneaking)
            return false;
        const auto isShield = [](const ItemStack& s) {
            return !s.Empty() && ItemNameOf(s.networkId) == "minecraft:shield";
        };
        if (!isShield(inventory.offhand) && !isShield(inventory.Held()))
            return false;
        const double yawRad = static_cast<double>(yaw) * 3.14159265358979323846 / 180.0;
        const double lookX = -std::sin(yawRad), lookZ = std::cos(yawRad);
        const double dx = fromX - posX, dz = fromZ - posZ;
        const double len = std::sqrt(dx * dx + dz * dz);
        return len < 1e-6 || (dx * lookX + dz * lookZ) / len > 0.0;
    }

    void Session::Hurt(float amount, double fromX, double fromZ) {
        Damage(amount, DamageCause::Mob, fromX, fromZ);
    }

    void Session::Kill() {
        Damage(std::numeric_limits<float>::max() / 4.0f, DamageCause::Kill, posX, posZ);
    }

    bool Session::Damage(float amount, DamageCause cause, double fromX, double fromZ, const std::string& source,
                         Session* attacker, float knockback) {
        if (dead || amount <= 0 || state != State::InGame)
            return false;
        const bool bypassInvulnerable = cause == DamageCause::Void || cause == DamageCause::Kill;
        if ((gameMode == 1 || gameMode == 6) && !bypassInvulnerable)
            return false;
        if (FireCause(cause) && (EffectLevel(12) > 0 || !server.RuleValue("firedamage")))
            return false;
        if (cause == DamageCause::Fall && !server.RuleValue("falldamage"))
            return false;
        if (cause == DamageCause::Drowning && !server.RuleValue("drowningdamage"))
            return false;
        if (cause == DamageCause::Starving && server.Config().difficulty == 0)
            return false;
        if (server.HookPlayerDamage(*this, amount, cause, attacker, source) || amount <= 0)
            return false;
        const bool blockable = cause == DamageCause::Mob || cause == DamageCause::Player ||
                               cause == DamageCause::Projectile || cause == DamageCause::Explosion;
        if (blockable && Shielding(fromX, fromZ)) {
            for (Session* s : server.PlayersIn(dimension))
                s->PlaySound("ShieldBlock", posX, posY, posZ);
            ItemStack& shield = ItemNameOf(inventory.offhand.networkId) == "minecraft:shield"
                                    ? inventory.offhand
                                    : inventory.main[static_cast<size_t>(inventory.selected)];
            if (amount >= 3.0f && gameMode != 1) {
                const int max = MaxDurability(shield.networkId);
                shield.damage = static_cast<uint16_t>(shield.damage + 1 + static_cast<int>(amount));
                if (max > 0 && shield.damage >= max)
                    shield = ItemStack{};
                else
                    shield.stackId = inventory.NewStackId();
                SendInventory();
            }
            return false;
        }
        float dealt = amount;
        if (invulnerable > 10 && !bypassInvulnerable) {
            if (amount <= lastHurt)
                return false;
            dealt = amount - lastHurt;
            lastHurt = amount;
        } else {
            lastHurt = amount;
            invulnerable = 20;
        }
        if (!BypassesArmor(cause)) {
            float armor = 0, toughness = 0;
            for (const ItemStack& a : inventory.armor)
                if (!a.Empty()) {
                    armor += static_cast<float>(ArmorPoints(a.networkId));
                    toughness += ArmorToughness(a.networkId);
                }
            if (armor > 0) {
                DamageArmor(dealt);
                const float effective =
                    std::clamp(armor - 4.0f * dealt / (toughness + 8.0f), armor / 5.0f, 20.0f);
                dealt *= 1.0f - effective / 25.0f;
            }
        }
        if (const int resistance = EffectLevel(11); resistance > 0 && cause != DamageCause::Void && cause != DamageCause::Kill)
            dealt *= std::max(0.0f, 1.0f - 0.2f * static_cast<float>(resistance));
        if (dealt <= 0)
            return false;
        if (absorption > 0) {
            const float used = std::min(absorption, dealt);
            absorption -= used;
            dealt -= used;
        }
        health -= dealt;
        Exhaust(0.1f);
        attributesDirty = true;
        for (Session* s : server.PlayersIn(dimension))
            s->MobEvent(runtimeId, 2, 0);
        const bool pushes = cause == DamageCause::Mob || cause == DamageCause::Player ||
                            cause == DamageCause::Projectile || cause == DamageCause::Explosion;
        if (pushes && knockback > 0) {
            float resist = 0;
            for (const ItemStack& a : inventory.armor)
                if (!a.Empty())
                    resist += KnockbackResistance(a.networkId);
            const float strength = knockback * std::max(0.0f, 1.0f - resist);
            const double dx = posX - fromX, dz = posZ - fromZ;
            const double len = std::max(0.01, std::sqrt(dx * dx + dz * dz));
            proto::SetEntityMotionPacket kb;
            kb.runtimeEntityId = runtimeId;
            kb.velocity = {static_cast<float>(dx / len * strength), std::min(0.4f, 0.36f * strength / 0.4f + 0.04f),
                           static_cast<float>(dz / len * strength)};
            kb.tick = server.TickCount();
            Send(kb);
        }
        if (health <= 0)
            Die(cause, attacker ? attacker->Name() : source);
        else
            SendAttributes();
        return true;
    }

    void Session::Die(DamageCause cause, const std::string& source) {
        server.HookPlayerDeath(*this, cause, source);
        dead = true;
        health = 0;
        StopUsing();
        SendAttributes();
        for (Session* s : server.InGamePlayers())
            if (s != this)
                s->MobEvent(runtimeId, 3, 0);
        if (!server.RuleValue("keepinventory")) {
            const auto drop = [&](ItemStack& slot) {
                if (slot.Empty())
                    return;
                std::uniform_real_distribution<double> unit(-0.2, 0.2);
                static std::mt19937 rng{std::random_device{}()};
                server.SpawnItem(slot, posX, posY - 1.0, posZ, unit(rng), 0.2, unit(rng), 40, dimension);
                slot = ItemStack{};
            };
            for (ItemStack& slot : inventory.main)
                drop(slot);
            for (ItemStack& slot : inventory.armor)
                drop(slot);
            drop(inventory.offhand);
            drop(inventory.cursor);
            for (ItemStack& slot : inventory.craft)
                drop(slot);
            const int xp = std::min(xpLevel * 7, 100);
            server.SpawnXp(dimension, posX, posY - 1.0, posZ, xp);
            xpLevel = 0;
            xpProgress = 0;
            xpTotal = 0;
            SendInventory();
            server.BroadcastArmor(*this);
        }
        ClearEffects();
        fireTicks = 0;
        air = 300;
        fallDistance = 0;
        std::string message;
        const std::string from = source.empty() ? std::string() : Pretty(source);
        switch (cause) {
        case DamageCause::Mob:
            message = name + " was slain by " + (from.empty() ? std::string("a mob") : from);
            break;
        case DamageCause::Player:
            message = name + " was slain by " + from;
            break;
        case DamageCause::Projectile:
            message = name + " was shot by " + (from.empty() ? std::string("an arrow") : from);
            break;
        case DamageCause::Explosion:
            message = from.empty() ? name + " blew up" : name + " was blown up by " + from;
            break;
        case DamageCause::Fall:
            message = name + " fell from a high place";
            break;
        case DamageCause::Void:
            message = name + " fell out of the world";
            break;
        case DamageCause::Lava:
            message = name + " tried to swim in lava";
            break;
        case DamageCause::Fire:
            message = name + " went up in flames";
            break;
        case DamageCause::Burning:
            message = name + " burned to death";
            break;
        case DamageCause::Drowning:
            message = name + " drowned";
            break;
        case DamageCause::Starving:
            message = name + " starved to death";
            break;
        case DamageCause::Suffocation:
            message = name + " suffocated in a wall";
            break;
        case DamageCause::Cactus:
            message = name + " was pricked to death";
            break;
        case DamageCause::Magma:
            message = name + " discovered the floor was lava";
            break;
        case DamageCause::BerryBush:
            message = name + " was poked to death by a sweet berry bush";
            break;
        case DamageCause::Wither:
            message = name + " withered away";
            break;
        case DamageCause::Magic:
        case DamageCause::Poison:
            message = name + " was killed by magic";
            break;
        case DamageCause::FallingBlock:
            message = name + " was squashed by a falling block";
            break;
        default:
            message = name + " died";
            break;
        }
        if (server.RuleValue("showdeathmessages"))
            server.Broadcast(message);
    }

    void Session::Heal(float amount) {
        if (dead || amount <= 0 || health >= MaxHealth())
            return;
        health = std::min(MaxHealth(), health + amount);
        attributesDirty = true;
    }

    void Session::AddExperience(int points) {
        if (points <= 0 || dead)
            return;
        xpTotal += points;
        float progress = xpProgress + static_cast<float>(points) / static_cast<float>(XpForLevel(xpLevel));
        const int before = xpLevel;
        while (progress >= 1.0f) {
            progress = (progress - 1.0f) * static_cast<float>(XpForLevel(xpLevel));
            xpLevel++;
            progress /= static_cast<float>(XpForLevel(xpLevel));
        }
        xpProgress = progress;
        if (xpLevel > before && xpLevel % 5 == 0)
            PlaySound("LevelUp", posX, posY, posZ);
        attributesDirty = true;
    }

    void Session::AddLevels(int levels) {
        xpLevel = std::clamp(xpLevel + levels, 0, 24791);
        if (xpLevel == 0 && levels < 0)
            xpProgress = 0;
        if (levels > 0 && xpLevel % 5 == 0)
            PlaySound("LevelUp", posX, posY, posZ);
        attributesDirty = true;
    }

    std::string Session::BlockNameAt(double x, double y, double z) const {
        ChunkManager& world = server.World(dimension);
        const int bx = static_cast<int>(std::floor(x)), by = static_cast<int>(std::floor(y)),
                  bz = static_cast<int>(std::floor(z));
        const DimensionRange range = RangeOf(dimension);
        if (by < range.minY || by > range.MaxY() || !world.Peek({bx >> 4, bz >> 4}))
            return {};
        return BlockRegistry::Instance().Name(world.GetBlock(bx, by, bz));
    }

    float Session::AttackDamage(const ItemStack& held) const {
        float damage = 1.0f;
        if (!held.Empty()) {
            const std::string n = ItemNameOf(held.networkId);
            const auto tier = [&](float wood, float stone, float iron, float gold, float diamond, float netherite,
                                  float copper) {
                if (n.contains("wooden_"))
                    return wood;
                if (n.contains("stone_"))
                    return stone;
                if (n.contains("iron_"))
                    return iron;
                if (n.contains("golden_"))
                    return gold;
                if (n.contains("diamond_"))
                    return diamond;
                if (n.contains("netherite_"))
                    return netherite;
                if (n.contains("copper_"))
                    return copper;
                return 1.0f;
            };
            if (n.ends_with("_sword"))
                damage = tier(5, 6, 7, 5, 8, 9, 6);
            else if (n.ends_with("_axe"))
                damage = tier(4, 5, 6, 4, 7, 8, 5);
            else if (n.ends_with("_pickaxe"))
                damage = tier(3, 4, 5, 3, 6, 7, 4);
            else if (n.ends_with("_shovel"))
                damage = tier(2, 3, 4, 2, 5, 6, 3);
            else if (n.ends_with("_hoe"))
                damage = tier(2, 3, 4, 2, 5, 6, 3);
            else if (n.ends_with("_spear"))
                damage = tier(2, 3, 4, 2, 5, 6, 3);
            else if (n == "minecraft:trident")
                damage = 9.0f;
            else if (n == "minecraft:mace") {
                damage = 6.0f;
                if (!onGround && fallDistance > 1.5f) {
                    const float f = fallDistance;
                    damage += 4.0f * std::min(f, 3.0f) + 2.0f * std::clamp(f - 3.0f, 0.0f, 5.0f) +
                              std::max(0.0f, f - 8.0f);
                }
            }
        }
        damage += 3.0f * static_cast<float>(EffectLevel(5));
        damage -= 4.0f * static_cast<float>(EffectLevel(18));
        const bool falling = !onGround && fallDistance > 0.0f && !swimming && !gliding && !flying;
        if (falling && EffectLevel(15) == 0)
            damage *= 1.5f;
        return std::max(0.0f, damage);
    }

    void Session::AttackPlayer(Session& victim, const ItemStack& held) {
        if (&victim == this || !victim.InGame() || victim.Dead() || victim.dimension != dimension ||
            !server.RuleValue("pvp"))
            return;
        if (victim.gameMode == 1 || victim.gameMode == 6)
            return;
        const double dx = victim.posX - posX, dy = victim.posY - posY, dz = victim.posZ - posZ;
        if (dx * dx + dy * dy + dz * dz > 7.0 * 7.0)
            return;
        const bool crit = !onGround && fallDistance > 0.0f && !swimming && !gliding && !flying;
        const float damage = AttackDamage(held);
        const float knock = sprinting ? 0.9f : 0.4f;
        if (victim.Damage(damage, DamageCause::Player, posX, posZ, name, this, knock)) {
            if (crit)
                for (Session* s : server.PlayersIn(dimension))
                    s->MobEvent(victim.runtimeId, 17, 0);
            const std::string n = held.Empty() ? std::string() : ItemNameOf(held.networkId);
            if (n.ends_with("_sword") || n == "minecraft:trident" || n == "minecraft:mace")
                DamageHeld(1);
            else if (n.ends_with("_axe") || n.ends_with("_pickaxe") || n.ends_with("_shovel") || n.ends_with("_hoe"))
                DamageHeld(2);
            if (sprinting)
                sprinting = false;
        }
        Exhaust(0.1f);
    }

    void Session::StartUsing() {
        const ItemStack& held = inventory.Held();
        if (held.Empty()) {
            StopUsing();
            return;
        }
        const std::string n = ItemNameOf(held.networkId);
        if (n == "minecraft:bow") {
            bool ammo = gameMode == 1;
            for (const ItemStack& s : inventory.main)
                if (!s.Empty() && ItemNameOf(s.networkId) == "minecraft:arrow")
                    ammo = true;
            if (!ItemNameOf(inventory.offhand.networkId).empty() &&
                ItemNameOf(inventory.offhand.networkId) == "minecraft:arrow")
                ammo = true;
            if (!ammo)
                return;
            useTicks = 0;
            useSlot = inventory.selected;
            useItem = held.networkId;
            return;
        }
        const FoodInfo* f = FoodOf(n);
        if (!f)
            return;
        if (!f->always && food >= 20 && gameMode != 1)
            return;
        useTicks = 0;
        useSlot = inventory.selected;
        useItem = held.networkId;
    }

    void Session::StopUsing() {
        useTicks = -1;
        useSlot = -1;
        useItem = 0;
    }

    void Session::ReleaseBow() {
        const int ticks = useTicks;
        StopUsing();
        if (ticks < 0)
            return;
        float charge = static_cast<float>(ticks) / 20.0f;
        charge = (charge * charge + charge * 2.0f) / 3.0f;
        if (charge < 0.1f)
            return;
        charge = std::min(charge, 1.0f);
        if (gameMode != 1) {
            bool taken = false;
            for (int i = 0; i < PlayerInventory::kMainSlots && !taken; i++) {
                ItemStack& s = inventory.main[static_cast<size_t>(i)];
                if (!s.Empty() && ItemNameOf(s.networkId) == "minecraft:arrow") {
                    s.count = static_cast<uint16_t>(s.count - 1);
                    if (s.count == 0)
                        s = ItemStack{};
                    else
                        s.stackId = inventory.NewStackId();
                    SendSlot(i);
                    taken = true;
                }
            }
            if (!taken && ItemNameOf(inventory.offhand.networkId) == "minecraft:arrow") {
                inventory.offhand.count = static_cast<uint16_t>(inventory.offhand.count - 1);
                if (inventory.offhand.count == 0)
                    inventory.offhand = ItemStack{};
                taken = true;
                SendInventory();
            }
            if (!taken)
                return;
            DamageHeld(1);
        }
        if (dimension != kOverworld)
            return;
        const double yawRad = static_cast<double>(yaw) * 3.14159265358979323846 / 180.0;
        const double pitchRad = static_cast<double>(pitch) * 3.14159265358979323846 / 180.0;
        const double dirX = -std::sin(yawRad) * std::cos(pitchRad), dirY = -std::sin(pitchRad),
                     dirZ = std::cos(yawRad) * std::cos(pitchRad);
        Mob* arrow = server.Mobs().Spawn("minecraft:arrow", posX + dirX * 0.5, posY - 0.1 + dirY * 0.5, posZ + dirZ * 0.5, false);
        if (!arrow)
            return;
        const double speed = 3.0 * charge;
        arrow->vx = dirX * speed;
        arrow->vy = dirY * speed;
        arrow->vz = dirZ * speed;
        arrow->owner = runtimeId;
        arrow->baby = charge >= 1.0f;
        arrow->yaw = yaw;
        arrow->pitch = pitch;
        for (Session* s : server.PlayersIn(dimension)) {
            s->MobMotion(*arrow);
            s->PlaySound("Bow", posX, posY, posZ);
        }
        Exhaust(0.0f);
    }

    void Session::FinishUsing() {
        ItemStack& held = inventory.main[static_cast<size_t>(inventory.selected)];
        if (held.Empty() || held.networkId != useItem) {
            StopUsing();
            return;
        }
        const std::string n = ItemNameOf(held.networkId);
        const FoodInfo* f = FoodOf(n);
        StopUsing();
        if (!f)
            return;
        if (server.HookItemConsume(*this, n)) {
            SendInventory();
            SendAttributes();
            return;
        }
        static std::mt19937 rng{std::random_device{}()};
        const auto chance = [&](float p) { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < p; };
        if (gameMode != 1) {
            food = std::min(20, food + f->nutrition);
            saturation = std::min(static_cast<float>(food), saturation + f->saturation);
        }
        const std::string bare = n.substr(n.find(':') + 1);
        if (bare == "golden_apple") {
            AddEffect(10, 1, 100);
            AddEffect(22, 0, 2400);
        } else if (bare == "enchanted_golden_apple" || bare == "appleEnchanted") {
            AddEffect(10, 1, 600);
            AddEffect(22, 3, 2400);
            AddEffect(11, 0, 6000);
            AddEffect(12, 0, 6000);
        } else if (bare == "chicken" && chance(0.3f)) {
            AddEffect(17, 0, 600);
        } else if (bare == "rotten_flesh" && chance(0.8f)) {
            AddEffect(17, 0, 600);
        } else if (bare == "poisonous_potato" && chance(0.6f)) {
            AddEffect(19, 0, 100);
        } else if (bare == "spider_eye") {
            AddEffect(19, 0, 100);
        } else if (bare == "pufferfish") {
            AddEffect(17, 2, 300);
            AddEffect(9, 0, 300);
            AddEffect(19, 3, 1200);
        } else if (bare == "honey_bottle") {
            RemoveEffect(19);
        } else if (bare == "milk_bucket") {
            ClearEffects();
        } else if (bare == "chorus_fruit") {
            for (int attempt = 0; attempt < 16; attempt++) {
                const double tx = posX + std::uniform_real_distribution<double>(-8, 8)(rng);
                const double tz = posZ + std::uniform_real_distribution<double>(-8, 8)(rng);
                const double feet = posY - 1.62;
                for (int dy = 8; dy >= -8; dy--) {
                    const double ty = std::floor(feet) + dy;
                    const std::string below = BlockNameAt(tx, ty - 1, tz);
                    if (below.empty() || !BlockSolid(BlockRegistry::Instance().Find(below)))
                        continue;
                    if (BlockSolid(server.World(dimension).GetBlock(static_cast<int>(std::floor(tx)), static_cast<int>(ty),
                                                                    static_cast<int>(std::floor(tz)))) ||
                        BlockSolid(server.World(dimension).GetBlock(static_cast<int>(std::floor(tx)), static_cast<int>(ty) + 1,
                                                                    static_cast<int>(std::floor(tz)))))
                        continue;
                    Teleport(std::floor(tx) + 0.5, ty + 1.62, std::floor(tz) + 0.5);
                    fallDistance = 0;
                    attempt = 16;
                    break;
                }
            }
        }
        if (gameMode != 1) {
            held.count = static_cast<uint16_t>(held.count - 1);
            if (held.count == 0)
                held = ItemStack{};
            else
                held.stackId = inventory.NewStackId();
            if (f->leftover) {
                const CatalogItem* rest = BlockCatalog::Instance().ItemByName(f->leftover);
                if (rest) {
                    if (held.Empty())
                        held = inventory.MakeStack(rest->networkId, 1);
                    else if (inventory.Give(rest->networkId, 1) > 0) {
                        ItemStack spill;
                        spill.networkId = rest->networkId;
                        spill.count = 1;
                        server.DropFromPlayer(*this, spill);
                    }
                }
            }
            SendInventory();
        }
        for (Session* s : server.PlayersIn(dimension)) {
            s->MobEvent(runtimeId, 57, static_cast<int>(held.networkId) << 16);
            s->PlaySound("Burp", posX, posY, posZ);
        }
        attributesDirty = true;
    }

    void Session::SendSelfData() {
        namespace F = proto::FlagsMetadataFlags1;
        uint64_t flags = F::HasCollision | F::AffectedByGravity | F::CanClimb | F::Walker;
        if (air >= 300 || EffectLevel(13) > 0)
            flags |= F::Breathing;
        if (fireTicks > 0 && EffectLevel(12) == 0)
            flags |= F::Onfire;
        if (sneaking)
            flags |= F::Sneaking;
        if (sprinting)
            flags |= F::Sprinting;
        if (swimming)
            flags |= F::Swimming;
        if (gliding)
            flags |= F::Gliding;
        std::vector<proto::MetadataDictionaryEntry> meta;
        proto::MetadataDictionaryEntry f;
        f.key = proto::MetadataDictionaryEntryKey::Flags;
        f.type = proto::MetadataDictionaryEntryType::Long;
        f.value.template emplace<1>(static_cast<int64_t>(flags));
        meta.push_back(f);
        proto::MetadataDictionaryEntry a;
        a.key = proto::MetadataDictionaryEntryKey::Air;
        a.type = proto::MetadataDictionaryEntryType::Short;
        a.value.template emplace<4>().template emplace<2>(static_cast<int16_t>(std::clamp(air, -20, 300)));
        meta.push_back(a);
        proto::MetadataDictionaryEntry m;
        m.key = proto::MetadataDictionaryEntryKey::MaxAirdataMaxAir;
        m.type = proto::MetadataDictionaryEntryType::Short;
        m.value.template emplace<4>().template emplace<2>(static_cast<int16_t>(300));
        meta.push_back(m);
        proto::SetEntityDataPacket p;
        p.runtimeEntityId = runtimeId;
        p.metadata = meta;
        p.tick = server.TickCount();
        for (Session* s : server.PlayersIn(dimension))
            s->Send(p);
    }

    void Session::MovementTick(const proto::PlayerAuthInputPacket& in, double previousY) {
        const auto has = [&](proto::InputData d) {
            return std::find(in.inputData.begin(), in.inputData.end(), d) != in.inputData.end();
        };
        const bool wasSprinting = sprinting, wasSwimming = swimming, wasGliding = gliding;
        if (has(proto::InputData::StartSprinting))
            sprinting = true;
        if (has(proto::InputData::StopSprinting))
            sprinting = false;
        if (has(proto::InputData::Sprinting))
            sprinting = true;
        else if (!has(proto::InputData::StartSprinting) && sprinting && in.moveVector.z <= 0.0f)
            sprinting = false;
        if (has(proto::InputData::StartSwimming))
            swimming = true;
        if (has(proto::InputData::StopSwimming))
            swimming = false;
        if (has(proto::InputData::StartGliding))
            gliding = true;
        if (has(proto::InputData::StopGliding))
            gliding = false;
        if (has(proto::InputData::StartFlying))
            flying = true;
        if (has(proto::InputData::StopFlying))
            flying = false;
        if (gameMode != 1 && gameMode != 6 && flying && !(gameMode == 1))
            flying = false;
        if (sprinting && food <= 6 && gameMode != 1)
            sprinting = false;
        if (wasSprinting != sprinting || wasSwimming != swimming || wasGliding != gliding)
            dataDirty = true;
        if (gameMode == 1 || gameMode == 6 || dead) {
            fallDistance = 0;
            onGround = true;
            return;
        }
        const double dy = posY - previousY;
        const bool collided = has(proto::InputData::VerticalCollision);
        const std::string feet = BlockNameAt(posX, posY - 1.62 + 0.1, posZ);
        const std::string under = BlockNameAt(posX, posY - 1.62 - 0.2, posZ);
        const bool inWater = feet.contains("water") || feet.contains("bubble_column");
        const bool climbing = feet.contains("ladder") || feet.contains("vine") || feet.contains("scaffolding") ||
                              feet.contains("web") || feet.contains("powder_snow") || feet.contains("sweet_berry");
        const bool grounded = collided && dy <= 0.0001;
        if (has(proto::InputData::StartJumping)) {
            Exhaust(sprinting ? 0.2f : 0.05f);
            jumping = true;
        }
        const double mx = posX - lastMoveX, mz = posZ - lastMoveZ;
        lastMoveX = posX;
        lastMoveZ = posZ;
        const double horizontal = std::sqrt(mx * mx + mz * mz);
        if (horizontal < 2.0) {
            if (swimming || inWater)
                Exhaust(static_cast<float>(0.01 * horizontal));
            else if (sprinting && onGround)
                Exhaust(static_cast<float>(0.1 * horizontal));
        }
        if (inWater || climbing || flying || EffectLevel(27) > 0 || EffectLevel(24) > 0 || feet.contains("lava")) {
            fallDistance = 0;
        } else if (gliding) {
            if (dy > -0.5)
                fallDistance = 0;
        } else if (dy < 0 && !grounded) {
            fallDistance -= static_cast<float>(dy);
        }
        if (grounded && !onGround) {
            float damage = std::ceil(fallDistance - 3.0f - static_cast<float>(EffectLevel(8)) - 0.001f);
            const std::string land = under.empty() ? feet : under;
            if (land.contains("hay_block") || land.contains("honey_block"))
                damage = std::ceil(damage * 0.2f);
            if (land.contains("slime") || land.contains("_bed") || land == "minecraft:bed")
                damage = land.contains("slime") && !sneaking ? 0.0f : std::ceil(damage * 0.5f);
            if (land.contains("powder_snow") || land.contains("water") || land.contains("cobweb") || land.contains(":web"))
                damage = 0;
            if (land.contains("pointed_dripstone") && fallDistance > 2.0f)
                damage = std::ceil((fallDistance - 2.0f) * 2.0f);
            if (damage > 0)
                Damage(damage, DamageCause::Fall, posX, posZ);
            fallDistance = 0;
            jumping = false;
        } else if (grounded) {
            fallDistance = 0;
        }
        onGround = grounded;
    }

    void Session::FoodTick() {
        if (gameMode == 1 || gameMode == 6 || dead)
            return;
        const int difficulty = server.Config().difficulty;
        if (const int hunger = EffectLevel(17); hunger > 0)
            Exhaust(0.005f * static_cast<float>(hunger));
        if (exhaustion > 4.0f) {
            exhaustion -= 4.0f;
            if (saturation > 0)
                saturation = std::max(0.0f, saturation - 1.0f);
            else if (difficulty != 0)
                food = std::max(0, food - 1);
            attributesDirty = true;
        }
        const bool regen = server.RuleValue("naturalregeneration") != 0;
        if (difficulty == 0) {
            if (regen && health < MaxHealth() && server.TickCount() % 20 == 0)
                Heal(1.0f);
            if (food < 20 && server.TickCount() % 10 == 0) {
                food++;
                attributesDirty = true;
            }
        }
        if (regen && saturation > 0 && health < MaxHealth() && food >= 20) {
            if (++foodTimer >= 10) {
                const float amount = std::min(saturation, 6.0f);
                Heal(amount / 6.0f);
                Exhaust(amount);
                foodTimer = 0;
            }
        } else if (regen && food >= 18 && health < MaxHealth()) {
            if (++foodTimer >= 80) {
                Heal(1.0f);
                Exhaust(6.0f);
                foodTimer = 0;
            }
        } else if (food <= 0) {
            if (++foodTimer >= 80) {
                if (health > 10.0f || difficulty == 3 || (health > 1.0f && difficulty == 2))
                    Damage(1.0f, DamageCause::Starving, posX, posZ);
                foodTimer = 0;
            }
        } else {
            foodTimer = 0;
        }
    }

    void Session::EffectTick() {
        std::vector<int> expired;
        for (auto& [id, e] : effects) {
            const int amp = e.amplifier;
            if (id == 10) {
                const int interval = std::max(1, 50 >> amp);
                if (e.ticks % interval == 0)
                    Heal(1.0f);
            } else if (id == 19 || id == 25) {
                const int interval = std::max(1, 25 >> amp);
                if (e.ticks % interval == 0 && (health > 1.0f || id == 25))
                    Damage(1.0f, DamageCause::Poison, posX, posZ);
            } else if (id == 20) {
                const int interval = std::max(1, 40 >> amp);
                if (e.ticks % interval == 0)
                    Damage(1.0f, DamageCause::Wither, posX, posZ);
            }
            if (--e.ticks <= 0)
                expired.push_back(id);
        }
        for (int id : expired)
            RemoveEffect(id);
    }

    void Session::EnvironmentTick() {
        if (gameMode == 1 || gameMode == 6 || dead) {
            if (fireTicks > 0 || air < 300) {
                fireTicks = 0;
                air = 300;
                dataDirty = true;
            }
            return;
        }
        envTicks++;
        const double feetY = posY - 1.62;
        const std::string feet = BlockNameAt(posX, feetY + 0.1, posZ);
        const std::string body = BlockNameAt(posX, feetY + 1.0, posZ);
        const std::string eye = BlockNameAt(posX, posY, posZ);
        const std::string below = BlockNameAt(posX, feetY - 0.2, posZ);
        const auto any = [&](const char* what) {
            return feet.contains(what) || body.contains(what);
        };
        const bool inWater = any("water") || any("bubble_column");
        const bool inLava = any("lava");
        if (eye.contains("water") && EffectLevel(13) == 0) {
            air--;
            if (air <= -20) {
                air = 0;
                Damage(2.0f, DamageCause::Drowning, posX, posZ);
            }
            dataDirty = true;
        } else if (air < 300) {
            air = std::min(300, air + 4);
            dataDirty = true;
        }
        if (inLava) {
            if (EffectLevel(12) == 0)
                fireTicks = std::max(fireTicks, 300);
            Damage(4.0f, DamageCause::Lava, posX, posZ);
            fallDistance = 0;
        } else if (any("fire") && !any("campfire")) {
            fireTicks = std::max(fireTicks, 160);
            Damage(1.0f, DamageCause::Fire, posX, posZ);
        } else if (any("campfire") && !any("soul_campfire")) {
            Damage(1.0f, DamageCause::Fire, posX, posZ);
        } else if (any("soul_campfire")) {
            Damage(2.0f, DamageCause::Fire, posX, posZ);
        }
        if (inWater || (server.Raining() && dimension == kOverworld && fireTicks > 0 && envTicks % 20 == 0 &&
                        BlockNameAt(posX, posY + 1.0, posZ).ends_with(":air"))) {
            if (fireTicks > 0)
                dataDirty = true;
            fireTicks = 0;
        }
        if (fireTicks > 0) {
            if (EffectLevel(12) > 0) {
                fireTicks = 0;
                dataDirty = true;
            } else {
                if (fireTicks % 20 == 0 && !inLava)
                    Damage(1.0f, DamageCause::Burning, posX, posZ);
                if (--fireTicks == 0)
                    dataDirty = true;
            }
        }
        if (any("cactus"))
            Damage(1.0f, DamageCause::Cactus, posX, posZ);
        if (any("sweet_berry_bush") && (std::abs(posX - lastMoveX) > 0.003 || std::abs(posZ - lastMoveZ) > 0.003))
            Damage(1.0f, DamageCause::BerryBush, posX, posZ);
        if (below.contains("magma") && !sneaking && EffectLevel(12) == 0 && onGround)
            Damage(1.0f, DamageCause::Magma, posX, posZ);
        if (!eye.empty() && envTicks % 10 == 0) {
            const uint16_t id = BlockRegistry::Instance().Find(eye);
            if (id != 0 && BlockSolid(id) && !BlockLiquid(id) && !eye.contains("glass") && !eye.contains("leaves") &&
                !eye.contains("slab") && !eye.contains("stairs") && !eye.contains("fence") && !eye.contains("door") &&
                !eye.contains("wall") && !eye.contains("pane") && !eye.contains("chest") && !eye.contains("trapdoor"))
                Damage(1.0f, DamageCause::Suffocation, posX, posZ);
        }
        if (fireTicks > 0 != lastFireSent)
            dataDirty = true;
    }

    void Session::SurvivalTick() {
        if (invulnerable > 0)
            invulnerable--;
        if (!dead && !awaitingDimensionAck && posY - 1.62 < RangeOf(dimension).minY - 64)
            Damage(4.0f, DamageCause::Void, posX, posZ);
        if (!dead) {
            EffectTick();
            FoodTick();
            EnvironmentTick();
            if (useTicks >= 0) {
                if (inventory.selected != useSlot || inventory.Held().networkId != useItem) {
                    StopUsing();
                } else {
                    useTicks++;
                    const std::string n = ItemNameOf(useItem);
                    if (n != "minecraft:bow") {
                        const FoodInfo* f = FoodOf(n);
                        if (useTicks % 4 == 0 && f && f->nutrition > 0 && useTicks < f->ticks)
                            for (Session* s : server.PlayersIn(dimension))
                                if (s != this)
                                    s->MobEvent(runtimeId, 57, static_cast<int>(useItem) << 16);
                        if (f && useTicks >= f->ticks)
                            FinishUsing();
                    }
                }
            }
        }
        if (dataDirty || air != lastAirSent || (fireTicks > 0) != lastFireSent) {
            dataDirty = false;
            lastAirSent = air;
            lastFireSent = fireTicks > 0;
            SendSelfData();
        }
        if (attributesDirty) {
            attributesDirty = false;
            SendAttributes();
        }
    }

    void Session::SendBlockUpdate(int x, int y, int z, int32_t networkId) {
        proto::UpdateBlockPacket u;
        u.position = {x, y, z};
        u.blockRuntimeId = static_cast<uint32_t>(networkId);
        u.flags = 3;
        u.layer = 0;
        Send(u);
    }

    void Session::Tick(uint64_t) {
        if (state == State::Closed)
            return;
        if (state == State::InGame)
            SurvivalTick();
        const auto now = std::chrono::steady_clock::now();
        if (!connection->Alive()) {
            Close();
            return;
        }
        if ((state == State::InGame && now - lastPacket > kIdleTimeout) ||
            (state != State::InGame && now - stateSince > kLoginTimeout)) {
            LogInfo((name.empty() ? connection->Describe() : name) + " timed out");
            Close();
            return;
        }
        const uint32_t idleMinutes = server.Config().idleTimeoutMinutes;
        if (idleMinutes > 0 && state == State::InGame && !server.Config().loopbackOnly &&
            now - lastActive > std::chrono::minutes(idleMinutes)) {
            LogInfo(name + " kicked: idle for " + std::to_string(idleMinutes) + " minutes");
            Kick("disconnectionScreen.idleTimeout");
            return;
        }
        if (state == State::Loading || state == State::InGame)
            StreamChunks();
        if (state == State::InGame && now - lastHeartbeat > kHeartbeat) {
            lastHeartbeat = now;
            proto::SetTimePacket t;
            t.time = static_cast<int32_t>(server.DayTime() % 2147483647);
            Send(t);
        }
        Flush();
    }

    std::vector<Session::EffectState> Session::Effects() const {
        std::vector<EffectState> out;
        for (const auto& [id, e] : effects)
            out.push_back({id, e.amplifier, e.ticks, e.particles, e.ambient});
        return out;
    }

    void Session::SetHealth(float value) {
        if (dead || state != State::InGame)
            return;
        health = std::min(value, MaxHealth());
        if (health <= 0) {
            Die(DamageCause::Kill, "plugin");
            return;
        }
        SendAttributes();
    }

    void Session::SetVelocity(double x, double y, double z) {
        proto::SetEntityMotionPacket p;
        p.runtimeEntityId = runtimeId;
        p.velocity = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
        p.tick = server.TickCount();
        Send(p);
    }

    void Session::SetSpawnPoint(int x, int y, int z) {
        hasSpawnPoint = true;
        spawnPointX = x;
        spawnPointY = y;
        spawnPointZ = z;
        if (state == State::InGame)
            SendSpawnPosition(x, y, z, false);
    }

    void Session::Transfer(const std::string& host, uint16_t port) {
        proto::TransferPacket p;
        p.serverAddress = host;
        p.port = port;
        p.reloadWorld = true;
        Send(p);
        Flush();
    }

    uint32_t Session::ShowForm(const std::string& json) {
        const uint32_t id = nextFormId++;
        if (openForms.size() > 64)
            openForms.clear();
        openForms.insert(id);
        proto::ModalFormRequestPacket p;
        p.formId = id;
        p.data = json;
        Send(p);
        return id;
    }

    void Session::OnFormResponse(std::span<const uint8_t> body) {
        proto::ModalFormResponsePacket p;
        if (!Decode(body, p) || state != State::InGame || !openForms.erase(p.formId))
            return;
        server.HookFormResponse(*this, p.formId, p.data ? *p.data : std::string());
    }

    void Session::PlaySoundTo(const std::string& sound, double x, double y, double z, float volume, float pitchValue) {
        proto::PlaySoundPacket p;
        p.name = sound;
        p.coordinates = {static_cast<int32_t>(x * 8), static_cast<int32_t>(y * 8), static_cast<int32_t>(z * 8)};
        p.volume = volume;
        p.pitch = pitchValue;
        Send(p);
    }

    void Session::SpawnParticle(const std::string& particle, double x, double y, double z) {
        proto::SpawnParticleEffectPacket p;
        p.dimension = static_cast<uint8_t>(dimension);
        p.entityId = -1;
        p.position = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
        p.particleName = particle;
        Send(p);
    }
}
