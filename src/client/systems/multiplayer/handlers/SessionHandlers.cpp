#include "client/systems/multiplayer/handlers/HandlerSupport.hpp"

namespace perlin {
    using namespace proto;
    using namespace handlers;

    std::string BedrockSession::BrandFrom(const std::string& engine, const std::string& gameVersion) {
        const auto trim = [](const std::string& v) {
            const size_t a = v.find_first_not_of(" \t\r\n");
            if (a == std::string::npos)
                return std::string();
            return v.substr(a, v.find_last_not_of(" \t\r\n") - a + 1);
        };
        const auto numeric = [](const std::string& v) {
            if (v.empty())
                return false;
            for (char c : v)
                if (!((c >= '0' && c <= '9') || c == '.'))
                    return false;
            return true;
        };
        const std::string e = trim(engine);
        bool letters = false;
        for (char c : e)
            letters = letters || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (letters) {
            std::string lower;
            for (char c : e)
                lower += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            if (lower == "vanilla" || lower == "bedrock" || lower == "bds" || lower == "minecraft" ||
                lower == "bedrock dedicated server")
                return "vanilla";
            return e;
        }
        if (numeric(trim(gameVersion)) && (e.empty() || numeric(e)))
            return "vanilla";
        return "custom";
    }

    void BedrockSession::RegisterSessionHandlers() {
        On<NetworkSettingsPacket>([this](const NetworkSettingsPacket& p) {
            compressThreshold = p.compressionThreshold;
            compressAlgorithm = static_cast<uint16_t>(p.compressionAlgorithm);
            compressEnabled = true;
            Log("bedrock", "NetworkSettings threshold={} algorithm={}", p.compressionThreshold,
                compressAlgorithm);
            if (!SendLogin()) {
                lastError = "failed to send Login";
                state = State::Failed;
            } else {
                state = State::WaitingPlayStatus;
            }
        });

        On<PlayStatusPacket>([this](const PlayStatusPacket& p) {
            Log("bedrock", "PlayStatus {}", static_cast<int>(p.status));
            using Kind = session::PlayStatusKind;
            const Kind kind = p.status == PlayStatusPacketStatus::LoginSuccess   ? Kind::LoginSuccess
                              : p.status == PlayStatusPacketStatus::FailedClient ? Kind::FailedClient
                              : p.status == PlayStatusPacketStatus::FailedSpawn  ? Kind::FailedSpawn
                              : p.status == PlayStatusPacketStatus::PlayerSpawn  ? Kind::PlayerSpawn
                                                                                 : Kind::Other;
            const session::Phase phase = state == State::WaitingPlayStatus ? session::Phase::WaitingPlayStatus
                                         : state == State::InGame          ? session::Phase::InGame
                                                                           : session::Phase::Other;
            const auto decision =
                session::DecidePlayStatus(kind, static_cast<int>(p.status), phase, cacheStatusSent);
            using Action = session::PlayStatusDecision::Action;
            switch (decision.action) {
            case Action::Fail:
                lastError = decision.error;
                if (decision.outdated != session::OutdatedSide::None)
                    SetOutdated(decision.outdated == session::OutdatedSide::Client ? 1 : 2);
                state = State::Failed;
                break;
            case Action::SendCacheStatus:
                SendClientCacheStatus();
                cacheStatusSent = true;
                break;
            case Action::EnterGame:
                if (!spawnSeen) {
                    spawnSeen = true;
                    spawnAt = std::chrono::steady_clock::now();
                }
                state = State::InGame;
                Log("bedrock", "PlayerSpawn -> waiting for local terrain before initialising");
                break;
            case Action::Ignore:
                break;
            }
        });

        On<DisconnectPacket>([this](const DisconnectPacket& p) {
            lastError = session::DescribeDisconnect(static_cast<int>(p.reason), p.message);
            Log("bedrock", "{}", lastError);
            SetDisconnectInfo(static_cast<int>(p.reason), p.message, false);
            if (p.reason == DisconnectFailReason::OutdatedClient ||
                p.reason == DisconnectFailReason::OutdatedServer ||
                p.reason == DisconnectFailReason::VersionMismatch)
                SetOutdated(p.reason == DisconnectFailReason::OutdatedClient   ? 1
                            : p.reason == DisconnectFailReason::OutdatedServer ? 2
                                                                               : 0);
            state = State::Failed;
        });

        On<ServerToClientHandshakePacket>([this](const ServerToClientHandshakePacket& p) {
            if (!usedRak) {
                Log("bedrock", "ServerToClientHandshake over NetherNet -> replying without encryption");
                Send(ClientToServerHandshakePacket{});
                return;
            }
            auto fresh = std::make_unique<PacketCipher>();
            std::string error;
            if (!fresh->InitFromHandshake(*identity, p.token, error)) {
                lastError = "encryption handshake failed: " + error;
                state = State::Failed;
                return;
            }
            Log("bedrock", "ServerToClientHandshake -> encryption on");
            {
                std::lock_guard<std::mutex> lock(txMutex);
                cipher = std::move(fresh);
            }
            Send(ClientToServerHandshakePacket{});
        });

        On<ResourcePacksInfoPacket>([this](const ResourcePacksInfoPacket& info) {
            Log("bedrock", "ResourcePacksInfo ({} packs)", info.texturePacks.size());
            state = State::ResourcePacks;
            std::vector<std::string> wanted = packs.Plan(info.texturePacks);
            if (wanted.empty()) {
                packs.MountAll();
                i18n::Lang::Get().Invalidate();
                SendResourcePackResponse(PackStatus::HaveAllPacks);
            } else {
                packsExpected = wanted.size();
                proto::ResourcePackClientResponsePacket ask;
                ask.responseStatus = PackStatus::SendPacks;
                ask.responseStatusName = "downloading";
                ask.resourcepackids = wanted;
                Log("bedrock", "asking the server for {} resource pack(s)", wanted.size());
                Send(ask);
            }
        });

        OnPartial<TextPacket>([this](const TextPacket& t) {
            const int type = static_cast<int>(t.type);
            if (type == 3 || type == 4 || type == 5)
                return;
            const std::string line =
                ui::FormatTextPacket(type, t.needsTranslation, t.sourceName, t.message, t.parameters);
            if (line.empty())
                return;
            Log("chat", "{}", line);
            PushChat(line);
        });
        OnPartial<CommandOutputPacket>([this](const CommandOutputPacket& c) {
            for (const auto& e : c.output) {
                const std::string line = ui::FormatCommandOutput(e.messageId, e.success, e.parameters);
                if (!line.empty())
                    PushChat(line);
            }
        });

        On<ResourcePackDataInfoPacket>([this](const ResourcePackDataInfoPacket& info) {
            const auto step = packs.OnDataInfo(info);
            if (step.step == PackDownloader::Step::RequestChunk) {
                for (const uint32_t index : step.chunks)
                    Send(proto::ResourcePackChunkRequestPacket{.packId = step.packId, .chunkIndex = index});
            } else if (step.step == PackDownloader::Step::Failed) {
                lastError = "resource pack download failed: " + step.message;
                state = State::Failed;
            } else if (step.step == PackDownloader::Step::PackDone) {
                for (const uint32_t index : step.chunks)
                    Send(proto::ResourcePackChunkRequestPacket{.packId = step.nextPackId,
                                                               .chunkIndex = index});
                if (packs.AllReceived()) {
                    packs.MountAll();
                    i18n::Lang::Get().Invalidate();
                    SendResourcePackResponse(PackStatus::HaveAllPacks);
                }
            } else if (step.step == PackDownloader::Step::Ignore && !step.message.empty()) {
                Log("packs", "{}", step.message);
            }
        });

        On<ResourcePackChunkDataPacket>([this](const ResourcePackChunkDataPacket& chunk) {
            const auto step = packs.OnChunk(chunk);
            if (step.step == PackDownloader::Step::RequestChunk) {
                for (const uint32_t index : step.chunks)
                    Send(proto::ResourcePackChunkRequestPacket{.packId = step.packId, .chunkIndex = index});
            } else if (step.step == PackDownloader::Step::Failed) {
                lastError = "resource pack download failed: " + step.message;
                state = State::Failed;
            } else if (step.step == PackDownloader::Step::PackDone) {
                for (const uint32_t index : step.chunks)
                    Send(proto::ResourcePackChunkRequestPacket{.packId = step.nextPackId,
                                                               .chunkIndex = index});
                if (packs.AllReceived()) {
                    packs.MountAll();
                    i18n::Lang::Get().Invalidate();
                    SendResourcePackResponse(PackStatus::HaveAllPacks);
                }
            }
        });

        On<TransferPacket>([this](const TransferPacket& t) {
            Log("bedrock", "Transfer -> {}:{}", t.serverAddress, t.port);
            transferHost = t.serverAddress;
            transferPort = t.port;
            transferRequested = true;
        });

        On<ResourcePackStackPacket>([this](const ResourcePackStackPacket&) {
            Log("bedrock", "ResourcePackStack");
            if (!packsCompletedSent) {
                SendResourcePackResponse(PackStatus::Completed);
                packsCompletedSent = true;
            }
            if (!cacheStatusSent) {
                SendClientCacheStatus();
                cacheStatusSent = true;
            }
        });

        On<ResourcePacksReadyForValidationPacket>([this](const ResourcePacksReadyForValidationPacket&) {
            Log("bedrock", "ResourcePacksReadyForValidation");
            if (!packsCompletedSent) {
                SendResourcePackResponse(PackStatus::Completed);
                packsCompletedSent = true;
            }
            if (!cacheStatusSent) {
                SendClientCacheStatus();
                cacheStatusSent = true;
            }
        });

        On<PacketViolationWarningPacket>([this](const PacketViolationWarningPacket& p) {
            {
                std::lock_guard<std::mutex> lk(disconnectMutex);
                lastViolation = std::string("it could not read our ") +
                                PacketName(static_cast<uint32_t>(p.packetId)) + " packet (" +
                                p.reason.substr(0, 120) + ")";
            }
            if (static_cast<int>(p.severity) >= 2)
                SetDisconnectInfo(-1,
                                  std::string("The server disconnected you: it rejected a packet from Perlin "
                                              "as invalid.\nPacket: ") +
                                      PacketName(static_cast<uint32_t>(p.packetId)) + "\n" +
                                      p.reason.substr(0, 200),
                                  true);
            Log("bedrock", "VIOLATION type={} severity={} packet={} ({}) reason='{}'",
                static_cast<int>(p.violationType), static_cast<int>(p.severity), p.packetId,
                PacketName(static_cast<uint32_t>(p.packetId)), p.reason.substr(0, 200));
        });

        OnPartial<StartGamePacket>([this](const StartGamePacket& p) {
            DropFormsForWorldChange();
            BlockPalette::Instance().UseProtocol(serverProtocol);
            {
                std::lock_guard lock(worldNameMutex);
                worldName = p.worldName;
            }
            ParsePropertySchema(p.propertyData.bytes);
            if (!p.blockProperties.empty()) {
                const size_t n = RegisterCustomBlocks(p.blockProperties);
                Log("blocks", "server defines {} custom blocks ({} states)", p.blockProperties.size(), n);
            } else {
                BlockPalette::Instance().TruncateSequence();
            }
            const float x = p.playerPosition.x, y = p.playerPosition.y, z = p.playerPosition.z;
            runtimeId = p.runtimeEntityId;
            uniqueId = p.entityId;
            gameType = static_cast<int>(p.playerGamemode);
            {
                std::lock_guard<std::mutex> lk(worldMutex);
                world.difficulty = p.difficulty;
                world.worldGameMode = static_cast<int>(p.worldGamemode);
                world.commandsEnabled = p.enableCommands;
                world.known = true;
                for (const GameRuleI32& rule : p.gamerules) {
                    std::string name = rule.name;
                    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                    if (const bool* b = std::get_if<bool>(&rule.value))
                        world.rules[name] = *b ? 1 : 0;
                    else if (const int32_t* i = std::get_if<int32_t>(&rule.value))
                        world.rules[name] = *i;
                }
            }
            if (gameType == 1)
                mayFly = true;
            if (!pose.known && SaneWorldPos(x, y, z))
                pose = {x, y, z, p.rotation.z, p.rotation.x, p.rotation.z, false, true};
            if (std::isfinite(x) && std::isfinite(z) && std::fabs(x) <= 100000.0f &&
                std::fabs(z) <= 100000.0f) {
                startPos[0] = x;
                startPos[1] = y;
                startPos[2] = z;
                haveStartPos = true;
            }
            clientGen = p.clientSideGeneration;
            flatWorld = p.generator == 2;
            serverAuthBreaking = p.serverAuthoritativeBlockBreaking;
            Log("bedrock", "serverAuthoritativeBlockBreaking={}",
                (p.serverAuthoritativeBlockBreaking ? "true" : "false"));
            Log("bedrock", "serverAuthoritativeInventory={}",
                (p.serverAuthoritativeInventory ? "true" : "false"));
            legacyInventory = !p.serverAuthoritativeInventory;
            BlockPalette::Instance().SetSequential(!p.blockNetworkIdsAreHashes);
            Log("bedrock", "blockNetworkIdsAreHashes={}", (p.blockNetworkIdsAreHashes ? "true" : "false"));
            Log("bedrock", "StartGame gameVersion={} engine={} paletteChecksum={} blockProperties={}",
                p.gameVersion, p.engine, p.blockPalletteChecksum, p.blockProperties.size());
            {
                std::lock_guard lock(brandMutex);
                serverBrand = BrandFrom(p.engine, p.gameVersion);
            }
            previewOffhand = p.engine.find("Perlin Server") != std::string::npos;
            Log("bedrock", "client_side_generation={} (requested chunk radius {}, max view distance {})",
                (clientGen ? "true" : "false"), chunkRadius, maxViewDistance);
            gotSpawn = true;
            serverTick = static_cast<uint64_t>(std::max<int64_t>(p.currentTick, 0));
            authTick = serverTick;
            dimensionId = static_cast<int>(p.dimension);
            rainTarget = p.rainLevel > 0.0f ? 1.0f : 0.0f;
            thunderTarget = p.lightningLevel > 0.0f ? 1.0f : 0.0f;
            Log("bedrock", "StartGame spawn=({},{},{}) runtime={} gamerules={}", x, y, z, runtimeId,
                p.gamerules.size());
            for (const GameRuleI32& rule : p.gamerules) {
                if (!EqualsIgnoreCase(rule.name, "dodaylightcycle"))
                    continue;
                if (const bool* enabled = std::get_if<bool>(&rule.value)) {
                    SetDayCycle(*enabled);
                    Log("bedrock", "doDaylightCycle={}", *enabled);
                }
            }
            Log("bedrock", "time: dayCycleStopTime={} currentTick={}", p.dayCycleStopTime, p.currentTick);
            double known = 0.0;
            if (!GetWorldTicks(known) && p.currentTick > 0)
                NoteTime(p.currentTick);
            SendRequestChunkRadius();
            SendLoadingScreen(true);
            startGameSeen = true;
            startGameAt = std::chrono::steady_clock::now();
            state = State::InGame;
            Log("bedrock", "StartGame handled -> waiting for PlayerSpawn");
        });
    }
}
