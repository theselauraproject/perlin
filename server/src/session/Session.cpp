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
#include <random>

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

    Session::Session(Server& server, std::shared_ptr<Connection> connection, uint64_t runtimeId)
        : server(server), connection(std::move(connection)), runtimeId(runtimeId),
          skin(std::make_unique<proto::Skin>()) {
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
        add("minecraft:health", 0.0f, 20.0f, std::max(0.0f, std::ceil(health)), 20.0f);
        add("minecraft:player.hunger", 0.0f, 20.0f, 20.0f, 20.0f);
        add("minecraft:player.saturation", 0.0f, 20.0f, 5.0f, 5.0f);
        add("minecraft:player.exhaustion", 0.0f, 4.0f, 0.0f, 0.0f);
        add("minecraft:player.level", 0.0f, 24791.0f, 0.0f, 0.0f);
        add("minecraft:player.experience", 0.0f, 1.0f, 0.0f, 0.0f);
        add("minecraft:absorption", 0.0f, 16.0f, 0.0f, 0.0f);
        add("minecraft:movement", 0.0f, 3.4028235e38f, 0.1f, 0.1f);
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
        gameMode = mode;
        creative = mode == 1;
        if (state != State::InGame)
            return;
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
            const RequestResult result = ApplyStackRequest(inventory, r, creative);
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
        if (m.selectedSlot < PlayerInventory::kHotbar)
            inventory.selected = m.selectedSlot;
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
                float damage = 1.0f;
                const ItemStack& held = inventory.main[static_cast<size_t>(inventory.selected)];
                if (!held.Empty())
                    if (const CatalogItem* item = BlockCatalog::Instance().ItemById(held.networkId)) {
                        const std::string& n = item->name;
                        const float tier = n.contains("netherite") ? 4 : n.contains("diamond") ? 3 : n.contains("iron") ? 2 :
                                           n.contains("stone") || n.contains("golden") ? 1 : 0;
                        if (n.contains("_sword"))
                            damage = 4.0f + tier;
                        else if (n.contains("_axe"))
                            damage = 7.0f + std::min(tier, 2.0f);
                        else if (n.contains("_pickaxe"))
                            damage = 2.0f + tier;
                        else if (n.contains("_shovel"))
                            damage = 2.5f + tier;
                        else if (n.contains("trident"))
                            damage = 9.0f;
                        else if (n.contains("mace"))
                            damage = 6.0f;
                    }
                server.Mobs().Attack(*this, on->entityRuntimeId, damage);
            } else {
                server.Mobs().Interact(*this, on->entityRuntimeId);
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
            if ((sneaking && !inventory.Held().Empty()) || !server.UseBlock(*this, at.x, at.y, at.z))
                if (!EquipHeldArmor())
                    server.PlayerPlace(*this, at.x, at.y, at.z, use->face);
            break;
        case proto::TransactionUseItemActionType::ClickAir:
            EquipHeldArmor();
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
            health = 20.0f;
            posX = server.SpawnX() + 0.5;
            posY = server.SpawnY() + 1.62;
            posZ = server.SpawnZ() + 0.5;
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
        SetPosition(in.position.x, in.position.y, in.position.z, in.pitch, in.yaw);
        sneaking = std::find(in.inputData.begin(), in.inputData.end(), proto::InputData::Sneaking) != in.inputData.end();
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

        std::vector<proto::MetadataDictionaryEntry> MobMetadata(const Mob& mob) {
            std::vector<proto::MetadataDictionaryEntry> out;
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

    void Session::Heal(float amount) {
        if (dead || health >= 20.0f)
            return;
        health = std::min(20.0f, health + amount);
        SendAttributes();
    }

    void Session::Hurt(float amount, double fromX, double fromZ) {
        if (dead || gameMode == 1 || gameMode == 6 || hurtCooldown > 0 || amount <= 0)
            return;
        health -= amount;
        hurtCooldown = 10;
        regenTicks = 0;
        SendAttributes();
        for (Session* s : server.InGamePlayers())
            s->MobEvent(runtimeId, 2, 0);
        const double dx = posX - fromX, dz = posZ - fromZ;
        const double len = std::max(0.01, std::sqrt(dx * dx + dz * dz));
        proto::SetEntityMotionPacket kb;
        kb.runtimeEntityId = runtimeId;
        kb.velocity = {static_cast<float>(dx / len * 0.4), 0.36f, static_cast<float>(dz / len * 0.4)};
        kb.tick = server.TickCount();
        Send(kb);
        if (health <= 0) {
            dead = true;
            health = 0;
            for (Session* s : server.InGamePlayers())
                if (s != this)
                    s->MobEvent(runtimeId, 3, 0);
            for (ItemStack& slot : inventory.main)
                if (!slot.Empty() && !server.RuleValue("keepinventory")) {
                    server.SpawnItem(slot, posX, posY - 1.0, posZ, 0.0, 0.2, 0.0, 40);
                    slot = ItemStack{};
                }
            SendInventory();
            server.Broadcast(name + " died");
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
        if (state == State::InGame) {
            if (hurtCooldown > 0)
                hurtCooldown--;
            if (!dead && !awaitingDimensionAck && posY - 1.62 < RangeOf(dimension).minY - 64)
                Hurt(4.0f, posX, posZ);
            if (!dead && health < 20.0f && server.RuleValue("naturalregeneration") && ++regenTicks >= 80) {
                regenTicks = 0;
                Heal(1.0f);
            }
        }
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

}
