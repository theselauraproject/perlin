#include <span>
#include "client/ui/ChatFormat.hpp"
#include "util/Env.hpp"
#include "client/multiplayer/BedrockSession.hpp"
#include "world/level/chunk/ChunkCodec.hpp"
#include "client/multiplayer/CustomBlocks.hpp"
#include "client/i18n/Lang.hpp"
#include "client/player/VoxelShapes.hpp"
#include "client/mods/ModManager.hpp"
#include "network/ClientIdentity.hpp"
#include "network/PacketDump.hpp"
#include "world/level/block/BlockPalette.hpp"

#include <atomic>
#include <cctype>
#include <chrono>
#include <variant>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include "util/Log.hpp"

namespace perlin {
    using namespace proto;

    namespace {
        using PackStatus = ResourcePackClientResponsePacketResponseStatus;

        bool SaneWorldPos(float x, float y, float z) {
            return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::fabs(x) < 100000.0f &&
                   std::fabs(y) < 10000.0f && std::fabs(z) < 100000.0f;
        }

        std::string UuidKey(const Uuid& u) {
            static const char* hex = "0123456789abcdef";
            std::string s;
            for (uint8_t b : u.bytes) {
                s += hex[b >> 4];
                s += hex[b & 15];
            }
            return s;
        }

        bool MetaNumber(const MetadataDictionaryEntry& m, double& out) {
            if (const auto* v = std::get_if<1>(&m.value)) { out = static_cast<double>(*v); return true; }
            if (const auto* v = std::get_if<2>(&m.value)) { out = static_cast<double>(*v); return true; }
            if (const auto* v = std::get_if<3>(&m.value)) { out = static_cast<double>(*v); return true; }
            if (const auto* inner = std::get_if<4>(&m.value)) {
                if (const auto* v = std::get_if<1>(inner)) { out = *v; return true; }
                if (const auto* v = std::get_if<2>(inner)) { out = *v; return true; }
                if (const auto* v = std::get_if<3>(inner)) { out = *v; return true; }
                if (const auto* v = std::get_if<4>(inner)) { out = *v; return true; }
                if (const auto* v = std::get_if<8>(inner)) { out = static_cast<double>(*v); return true; }
            }
            return false;
        }

        bool MetaInt(const MetadataDictionaryEntry& m, int64_t& out) {
            const auto take = [&](const auto& v) { out = static_cast<int64_t>(v); return true; };
            if (const auto* v = std::get_if<1>(&m.value)) return take(*v);
            if (const auto* v = std::get_if<2>(&m.value)) return take(*v);
            if (const auto* v = std::get_if<3>(&m.value)) return take(*v);
            if (const auto* inner = std::get_if<4>(&m.value)) {
                if (const auto* v = std::get_if<1>(inner)) return take(*v);
                if (const auto* v = std::get_if<2>(inner)) return take(*v);
                if (const auto* v = std::get_if<3>(inner)) return take(*v);
                if (const auto* v = std::get_if<4>(inner)) return take(*v);
                if (const auto* v = std::get_if<8>(inner)) return take(*v);
            }
            return false;
        }

        void ApplyMetadata(entity::EntityTracker& tracker, uint64_t rid, const std::vector<MetadataDictionaryEntry>& metadata) {
            int variant = -1, mark = -1, color = -1;
            float boxWidth = 0.0f, boxHeight = 0.0f;
            static const bool debugTags = std::getenv("PERLIN_DEBUG_NAMETAGS") != nullptr;
            for (const MetadataDictionaryEntry& m : metadata) {
                if (m.key == MetadataDictionaryEntryKey::Nametag) {
                    if (const auto* inner = std::get_if<4>(&m.value))
                        if (const auto* str = std::get_if<5>(inner)) {
                            tracker.SetNameTag(rid, *str);
                            if (debugTags) {
                                std::string flat = *str;
                                for (char& c : flat)
                                    if (c == '\n')
                                        c = '|';
                                Log("nametag", "set rid={} text={}", rid, flat);
                            }
                        }
                    continue;
                }
                double n = 0;
                if (!MetaNumber(m, n))
                    continue;
                switch (m.key) {
                case MetadataDictionaryEntryKey::Flags: {
                    int64_t bits = 0;
                    tracker.SetFlags(rid, MetaInt(m, bits) ? static_cast<uint64_t>(bits) : static_cast<uint64_t>(static_cast<int64_t>(n)));
                    break;
                }
                case MetadataDictionaryEntryKey::FlagsExtended: {
                    int64_t bits = 0;
                    const uint64_t ext = MetaInt(m, bits) ? static_cast<uint64_t>(bits) : static_cast<uint64_t>(static_cast<int64_t>(n));
                    tracker.SetFlagsExtended(rid, ext);
                    tracker.SetCrawling(rid, ((ext >> 50) & 1ull) != 0);
                    tracker.SetSleeping(rid, ((ext >> 12) & 1ull) != 0);
                    break;
                }
                case MetadataDictionaryEntryKey::PlayerFlags: tracker.SetSleeping(rid, (static_cast<int64_t>(n) & 2) != 0, true); break;
                case MetadataDictionaryEntryKey::Scale: tracker.SetScale(rid, static_cast<float>(n)); break;
                case MetadataDictionaryEntryKey::BoundingboxWidth: boxWidth = static_cast<float>(n); break;
                case MetadataDictionaryEntryKey::BoundingboxHeight: boxHeight = static_cast<float>(n); break;
                case MetadataDictionaryEntryKey::Variant: variant = static_cast<int>(n); break;
                case MetadataDictionaryEntryKey::ExperienceValue: {
                    const int xp = static_cast<int>(n);
                    static constexpr int kSteps[] = {2477, 1237, 617, 307, 149, 73, 37, 17, 7, 3};
                    int icon = 0;
                    for (size_t i = 0; i < std::size(kSteps); i++)
                        if (xp >= kSteps[i]) {
                            icon = 10 - static_cast<int>(i);
                            break;
                        }
                    variant = icon;
                    break;
                }
                case MetadataDictionaryEntryKey::MarkVariant: mark = static_cast<int>(n); break;
                case MetadataDictionaryEntryKey::Color: color = static_cast<int>(n); break;
                case MetadataDictionaryEntryKey::TargetEid: {
                    int64_t target = 0;
                    if (MetaInt(m, target)) tracker.SetTarget(rid, target);
                    break;
                }
                case MetadataDictionaryEntryKey::OwnerEid: tracker.SetOwner(rid, static_cast<int64_t>(n)); break;
                default: break;
                }
            }
            if (variant >= 0 || mark >= 0 || color >= 0)
                tracker.SetVariant(rid, std::max(variant, 0), std::max(mark, 0), std::max(color, 0));
            if (boxWidth > 0.0f && boxHeight > 0.0f)
                tracker.SetBoundingBox(rid, boxWidth, boxHeight);
        }

        uint64_t MetaFlags(const std::vector<MetadataDictionaryEntry>& metadata) {
            for (const MetadataDictionaryEntry& m : metadata) {
                int64_t bits = 0;
                if (m.key == MetadataDictionaryEntryKey::Flags && MetaInt(m, bits))
                    return static_cast<uint64_t>(bits);
            }
            return 0;
        }

        std::shared_ptr<const entity::SkinData> ConvertSkin(const Skin& s) {
            static std::atomic<uint64_t> counter{1};
            auto out = std::make_shared<entity::SkinData>();
            out->serial = counter++;
            out->skinId = s.skinId;
            out->skinWidth = s.skinData.width;
            out->skinHeight = s.skinData.height;
            out->skinRgba = s.skinData.data;
            if (static_cast<size_t>(out->skinWidth) * static_cast<size_t>(out->skinHeight) * 4 != out->skinRgba.size()) {
                out->skinWidth = out->skinHeight = 0;
                out->skinRgba.clear();
            }
            out->capeWidth = s.capeData.width;
            out->capeHeight = s.capeData.height;
            out->capeRgba = s.capeData.data;
            if (static_cast<size_t>(out->capeWidth) * static_cast<size_t>(out->capeHeight) * 4 != out->capeRgba.size()) {
                out->capeWidth = out->capeHeight = 0;
                out->capeRgba.clear();
            }
            out->slim = s.armSize == SkinArmSize::Slim;
            if (!s.geometryData.empty() && s.geometryData.contains("geometry")) {
                out->geometryJson = s.geometryData;
                const std::string& patch = s.skinResourcePack;
                const size_t at = patch.find("\"default\"");
                if (at != std::string::npos) {
                    const size_t q1 = patch.find('"', patch.find(':', at) + 1);
                    const size_t q2 = q1 == std::string::npos ? q1 : patch.find('"', q1 + 1);
                    if (q2 != std::string::npos)
                        out->geometryName = patch.substr(q1 + 1, q2 - q1 - 1);
                }
                if (out->geometryName.empty())
                    out->geometryName = "geometry.humanoid.custom";
            }
            return out;
        }

        template <class P> void Trace(BedrockSession& session) {
            session.OnPartial<P>([](const P& packet) {
                Log("trace", "{} {}", P::kName, net::Dump(packet));
            });
        }

        bool EqualsIgnoreCase(const std::string& a, std::string_view b) {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); i++)
                if (std::tolower(static_cast<unsigned char>(a[i])) !=
                    std::tolower(static_cast<unsigned char>(b[i])))
                    return false;
            return true;
        }
    }

    void BedrockSession::RegisterPacketHandlers() {
        InstallPersistentChunkResolver();
        RegisterServerUiHandlers();
        RegisterWorldExtraHandlers();
        RegisterProtocolExtras();
        RegisterIgnoredHandlers();
        dispatcher.OnFailure([](uint32_t id, const char* name, size_t size) {
            Log("net", "failed to decode {} (id {}, {} bytes)", name, id, size);
        });

        On<NetworkSettingsPacket>([this](const NetworkSettingsPacket& p) {
            compressThreshold = p.compressionThreshold;
            compressAlgorithm = static_cast<uint16_t>(p.compressionAlgorithm);
            compressEnabled = true;
            Log("bedrock", "NetworkSettings threshold={} algorithm={}", p.compressionThreshold, compressAlgorithm);
            if (!SendLogin()) {
                lastError = "failed to send Login";
                state = State::Failed;
            } else {
                state = State::WaitingPlayStatus;
            }
        });

        On<PlayStatusPacket>([this](const PlayStatusPacket& p) {
            Log("bedrock", "PlayStatus {}", static_cast<int>(p.status));
            // status 1: our client outdated; 2 (FailedSpawn in protocol data, LoginFailedServer in bedrock): server outdated;
            // either can arrive before network settings, not only after Login
            if ((p.status == PlayStatusPacketStatus::FailedClient || p.status == PlayStatusPacketStatus::FailedSpawn) && state != State::InGame) {
                const bool clientOld = p.status == PlayStatusPacketStatus::FailedClient;
                lastError = clientOld ? "Perlin is outdated for this server" : "the server is outdated for Perlin";
                SetOutdated(clientOld ? 1 : 2);
                state = State::Failed;
                return;
            }
            if (state == State::WaitingPlayStatus) {
                if (p.status != PlayStatusPacketStatus::LoginSuccess) {
                    const int code = static_cast<int>(p.status);
                    lastError = "login rejected, PlayStatus=" + std::to_string(code);
                    state = State::Failed;
                } else if (!cacheStatusSent) {
                    SendClientCacheStatus();
                    cacheStatusSent = true;
                }
            } else if (p.status == PlayStatusPacketStatus::LoginSuccess && !cacheStatusSent) {
                SendClientCacheStatus();
                cacheStatusSent = true;
            } else if (p.status == PlayStatusPacketStatus::PlayerSpawn) {
                if (!spawnSeen) {
                    spawnSeen = true;
                    spawnAt = std::chrono::steady_clock::now();
                }
                state = State::InGame;
                Log("bedrock", "PlayerSpawn -> waiting for local terrain before initialising");
            }
        });

        On<DisconnectPacket>([this](const DisconnectPacket& p) {
            lastError = "server disconnect, reason=" + std::to_string(static_cast<int>(p.reason)) +
                        (p.message.empty() ? "" : " message='" + p.message + "'");
            switch (static_cast<int>(p.reason)) {
                case 44:
                    lastError += " (ServerIdConflict: the server still has a session with this name/id from an earlier"
                                 " run. Wait ~30 s, restart the server, or join with a different --name)";
                    break;
                case 35:
                case 8:
                    lastError += " (version mismatch with Perlin's 1.26.60)";
                    break;
                case 46:
                    lastError += " (the server wants an Xbox login: use --login)";
                    break;
                default: break;
            }
            Log("bedrock", "{}", lastError);
            SetDisconnectInfo(static_cast<int>(p.reason), p.message, false);
            if (p.reason == DisconnectFailReason::OutdatedClient || p.reason == DisconnectFailReason::OutdatedServer ||
                p.reason == DisconnectFailReason::VersionMismatch)
                SetOutdated(p.reason == DisconnectFailReason::OutdatedClient ? 1 : p.reason == DisconnectFailReason::OutdatedServer ? 2 : 0);
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
            const std::string line = ui::FormatTextPacket(type, t.needsTranslation, t.sourceName, t.message, t.parameters);
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
                    Send(proto::ResourcePackChunkRequestPacket{.packId = step.nextPackId, .chunkIndex = index});
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
                    Send(proto::ResourcePackChunkRequestPacket{.packId = step.nextPackId, .chunkIndex = index});
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
                lastViolation = std::string("it could not read our ") + PacketName(static_cast<uint32_t>(p.packetId)) + " packet (" + p.reason.substr(0, 120) + ")";
            }
            if (static_cast<int>(p.severity) >= 2)
                SetDisconnectInfo(-1, std::string("The server disconnected you: it rejected a packet from Perlin as invalid.\nPacket: ") +
                                          PacketName(static_cast<uint32_t>(p.packetId)) + "\n" + p.reason.substr(0, 200), true);
            Log("bedrock", "VIOLATION type={} severity={} packet={} ({}) reason='{}'", static_cast<int>(p.violationType), static_cast<int>(p.severity), p.packetId, PacketName(static_cast<uint32_t>(p.packetId)), p.reason.substr(0, 200));
        });

        OnPartial<StartGamePacket>([this](const StartGamePacket& p) {
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
            if (gameType == 1) mayFly = true;
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
            // generator 2 is the flat type
            flatWorld = p.generator == 2;
            serverAuthBreaking = p.serverAuthoritativeBlockBreaking;
            Log("bedrock", "serverAuthoritativeBlockBreaking={}", (p.serverAuthoritativeBlockBreaking ? "true" : "false"));
            Log("bedrock", "serverAuthoritativeInventory={}", (p.serverAuthoritativeInventory ? "true" : "false"));
            legacyInventory = !p.serverAuthoritativeInventory;
            BlockPalette::Instance().SetSequential(!p.blockNetworkIdsAreHashes);
            Log("bedrock", "blockNetworkIdsAreHashes={}", (p.blockNetworkIdsAreHashes ? "true" : "false"));
            Log("bedrock", "StartGame gameVersion={} engine={} paletteChecksum={} blockProperties={}", p.gameVersion, p.engine, p.blockPalletteChecksum, p.blockProperties.size());
            Log("bedrock", "client_side_generation={} (requested chunk radius {}, max view distance {})", (clientGen ? "true" : "false"), chunkRadius, maxViewDistance);
            gotSpawn = true;
            serverTick = static_cast<uint64_t>(std::max<int64_t>(p.currentTick, 0));
            authTick = serverTick;
            dimensionId = static_cast<int>(p.dimension);
            rainTarget = p.rainLevel > 0.0f ? 1.0f : 0.0f;
            thunderTarget = p.lightningLevel > 0.0f ? 1.0f : 0.0f;
            Log("bedrock", "StartGame spawn=({},{},{}) runtime={} gamerules={}", x, y, z, runtimeId, p.gamerules.size());
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

        On<NetworkChunkPublisherUpdatePacket>([this](const NetworkChunkPublisherUpdatePacket& p) {
            const int cx = static_cast<int>(std::floor(static_cast<double>(p.coordinates.x) / 16.0));
            const int cz = static_cast<int>(std::floor(static_cast<double>(p.coordinates.z) / 16.0));
            const int radius = static_cast<int>((p.radius + 15) / 16);
            viewCenterChunk[0] = cx;
            viewCenterChunk[1] = cz;
            viewRadiusChunks = radius;
            static int logged = 0;
            if (logged++ < 6)
                Log("chunk", "publisher centre ({},{},{}) radius {} blocks, {} server-built chunks listed", p.coordinates.x, p.coordinates.y, p.coordinates.z, p.radius, p.savedChunks.size());
            if (radius > 0)
                PruneChunks(cx, cz, radius + 2);
        });

        On<DimensionDataPacket>([](const DimensionDataPacket& p) {
            for (const DimensionDataPacketDefinitionsEntry& d : p.definitions) {
                const int id = d.id.contains("nether") ? 1 : d.id.contains("end") ? 2 : 0;
                Log("chunk", "dimension {} minY={} height={}", d.id, d.minY, d.heightRange);
                ChunkCodec::SetDimension(id, d.minY, d.heightRange);
            }
        });

        On<ChunkRadiusUpdatePacket>([this](const ChunkRadiusUpdatePacket& p) {
            grantedRadius = p.chunkRadius;
            Log("chunk", "server granted chunk radius {} (asked {})", p.chunkRadius, chunkRadius);
            if (p.chunkRadius < chunkRadius)
                Log("chunk", "the server limits the view distance to {} chunks; LODs can only show terrain the server sends", p.chunkRadius);
        });

        On<ItemRegistryPacket>([this](const ItemRegistryPacket& p) {
            // Some proxies follow the real registry with an empty one; it carries nothing, so it must not wipe the names.
            if (p.itemstates.empty()) {
                std::lock_guard lock(statusMutex);
                if (!itemNames.empty()) {
                    Log("items", "ignoring an empty item registry");
                    return;
                }
            }
            Log("items", "server defines {} custom items", RegisterCustomItems(p.itemstates));
            {
                std::lock_guard lock(statusMutex);
                itemNames.clear();
                for (const ItemstatesEntry& item : p.itemstates)
                    itemNames[item.runtimeId] = item.name;
                const auto rename = [this](ItemStackInfo& info) {
                    if (info.name != "minecraft:unknown")
                        return;
                    if (const auto it = itemNames.find(info.networkId); it != itemNames.end())
                        info.name = it->second;
                };
                InventoryView& v = inventory;
                for (std::span<ItemStackInfo> group : {std::span<ItemStackInfo>(v.hotbar), std::span<ItemStackInfo>(v.main), std::span<ItemStackInfo>(v.armor),
                                    std::span<ItemStackInfo>(v.craft), std::span<ItemStackInfo>(v.work), std::span<ItemStackInfo>(v.horse),
                                    std::span<ItemStackInfo>(v.chest)})
                    for (ItemStackInfo& info : group)
                        rename(info);
                rename(v.offhand);
                rename(v.output);
                rename(v.cursor);
            }
            for (const ItemstatesEntry& item : p.itemstates)
                if (item.name == "minecraft:shield") {
                    net::context::SetShieldItemID(item.runtimeId);
                    return;
                }
        });

        On<SetTimePacket>([this](const SetTimePacket& p) {
            Log("bedrock", "SetTime {}", p.time);
            NoteTime(p.time);
        });

        On<SyncWorldClocksPacket>([this](const SyncWorldClocksPacket& p) {
            constexpr uint64_t kOverworldClockId = 0x63d7ede73c38f916ULL;
            if (p.payloadType == SyncWorldClocksPacketPayloadType::InitializeRegistry) {
                for (const WorldClockData& clock : p.clocks)
                    if (clock.id == kOverworldClockId || clock.name == "minecraft:overworld") {
                        overworldClockId = clock.id;
                        Log("bedrock", "clock init {} time={} paused={}", clock.name, clock.time, clock.paused);
                        NoteClock(clock.time, clock.paused);
                    }
            } else if (p.payloadType == SyncWorldClocksPacketPayloadType::SyncState) {
                for (const SyncWorldClockStateData& state : p.syncStates)
                    if (state.clockId == overworldClockId || state.clockId == kOverworldClockId) {
                        Log("bedrock", "clock sync time={} paused={}", state.time, state.paused);
                        NoteClock(state.time, state.paused);
                    }
            }
        });

        On<ChangeDimensionPacket>([this](const ChangeDimensionPacket& p) {
            entities.Clear();
            Log("bedrock", "ChangeDimension {} -> {} pos=({},{},{})", dimensionId.load(), p.dimension, p.position.x, p.position.y, p.position.z);
            dimensionId = p.dimension;
            ResetWorldForDimension();
            if (SaneWorldPos(p.position.x, p.position.y, p.position.z)) {
                pose = {p.position.x, p.position.y, p.position.z, pose.yaw, pose.pitch,
                        pose.headYaw, false, true};
                haveLastAuthPos = false;
                ServerMotion motion;
                motion.poseChanged = true;
                motion.teleport = true;
                motion.hasVelocity = true;
                QueueServerMotion(motion);
            }
            dimLoadingId = p.loadingScreenId;
            dimAckSince = std::chrono::steady_clock::now();
            dimAckPending = true;
            SendLoadingScreen(true, p.loadingScreenId);
        });

        On<PlaySoundPacket>([this](const PlaySoundPacket& p) {
            SoundRequest r;
            r.kind = SoundRequest::Kind::Named;
            r.name = p.name;
            r.x = static_cast<float>(p.coordinates.x) / 8.0f;
            r.y = static_cast<float>(p.coordinates.y) / 8.0f;
            r.z = static_cast<float>(p.coordinates.z) / 8.0f;
            r.volume = p.volume;
            r.pitch = p.pitch;
            r.global = p.bypassListenerRangeCheck;
            PushSound(std::move(r));
        });

        On<StopSoundPacket>([this](const StopSoundPacket& p) {
            SoundRequest r;
            r.kind = SoundRequest::Kind::Stop;
            r.name = p.name;
            r.stopAll = p.stopAll;
            PushSound(std::move(r));
        });

        On<LevelSoundEventPacket>([this](const LevelSoundEventPacket& p) {
            SoundRequest r;
            r.kind = SoundRequest::Kind::Level;
            r.name = p.soundId;
            r.x = p.position.x;
            r.y = p.position.y;
            r.z = p.position.z;
            r.extra = p.extraData;
            r.entityType = p.entityType;
            r.baby = p.isBabyMob;
            r.global = p.isGlobal;
            PushSound(std::move(r));
        });

        On<AvailableCommandsPacket>([this](const AvailableCommandsPacket& p) {
            using VT = AvailableCommandsPacketCommandDataEntryOverloadsEntryParametersEntryValueType;
            std::vector<ui::CommandInfo> list;
            list.reserve(p.commandData.size());
            for (const auto& c : p.commandData) {
                ui::CommandInfo info;
                info.name = c.name;
                info.description = c.description;
                for (const auto& o : c.overloads) {
                    std::vector<ui::CommandParam> params;
                    for (const auto& a : o.parameters) {
                        ui::CommandParam cp;
                        cp.name = a.parameterName;
                        cp.optional = a.optional;
                        const uint16_t flags = static_cast<uint16_t>(a.enumType);
                        const size_t index = static_cast<uint16_t>(a.valueType);
                        if (flags == static_cast<uint16_t>(AvailableCommandsPacketCommandDataEntryOverloadsEntryParametersEntryEnumType::SoftEnum)) {
                            if (index < p.dynamicEnums.size()) {
                                cp.values = p.dynamicEnums[index].values;
                                cp.softEnum = p.dynamicEnums[index].name;
                            }
                        } else if (flags & 0x20) {
                            if (index < p.enums.size())
                                for (const uint32_t v : p.enums[index].values)
                                    if (v < p.enumValues.size())
                                        cp.values.push_back(p.enumValues[v]);
                        } else if (a.valueType == VT::Target || a.valueType == VT::WildcardTarget) {
                            cp.target = true;
                        }
                        params.push_back(std::move(cp));
                    }
                    info.overloads.push_back(std::move(params));
                }
                list.push_back(std::move(info));
            }
            std::lock_guard lock(commandMutex);
            commands = std::move(list);
            commandVersion++;
        });

        On<LevelEventPacket>([this](const LevelEventPacket& p) {
            if (p.event == LevelEventPacketEvent::BlockStartBreak || p.event == LevelEventPacketEvent::BlockStopBreak ||
                p.event == LevelEventPacketEvent::BlockBreakSpeed) {
                const int bx = static_cast<int>(std::floor(p.position.x)), by = static_cast<int>(std::floor(p.position.y)),
                          bz = static_cast<int>(std::floor(p.position.z));
                std::lock_guard lock(crackMutex);
                const auto now = std::chrono::steady_clock::now();
                const auto it = std::find_if(crackStates.begin(), crackStates.end(),
                                             [&](const CrackState& c) { return c.x == bx && c.y == by && c.z == bz; });
                if (p.event == LevelEventPacketEvent::BlockStopBreak) {
                    if (it != crackStates.end())
                        crackStates.erase(it);
                } else {
                    if (it == crackStates.end()) {
                        if (bx == lastClearedCrack[0] && by == lastClearedCrack[1] && bz == lastClearedCrack[2] &&
                            now - lastClearedAt < std::chrono::milliseconds(750))
                            return;
                        bool present = false;
                        const uint16_t cur = netLevel.GetBlock(bx, by, bz, present);
                        if (present && cur == 0)
                            return;
                    }
                    CrackState* c = it != crackStates.end() ? &*it : &crackStates.emplace_back();
                    if (it == crackStates.end() || c->since == std::chrono::steady_clock::time_point{}) {
                        c->x = bx; c->y = by; c->z = bz; c->progress = 0.0;
                    } else {
                        c->progress = std::min(1.0, c->progress + c->perSecond * std::chrono::duration<double>(now - c->since).count());
                    }
                    c->since = now;
                    c->perSecond = static_cast<double>(p.data) / 65535.0 * 20.0;
                }
                return;
            }
            {
                const int ev = static_cast<int>(p.event);
                if ((ev >= 2000 && ev <= 2035) || ev >= 16384) {
                    ParticleEvent pe;
                    pe.event = ev;
                    pe.x = p.position.x;
                    pe.y = p.position.y;
                    pe.z = p.position.z;
                    pe.data = static_cast<int32_t>(p.data);
                    PushParticle(std::move(pe));
                }
            }
            const auto named = [&](const char* sound, float volume = 1.0f, float pitch = 1.0f) {
                SoundRequest r;
                r.name = sound;
                r.x = p.position.x;
                r.y = p.position.y;
                r.z = p.position.z;
                r.volume = volume;
                r.pitch = pitch;
                PushSound(std::move(r));
            };
            switch (p.event) {
            case LevelEventPacketEvent::SoundClick: named("random.click", 1.0f, 1.0f); break;
            case LevelEventPacketEvent::SoundClickFail: named("random.click", 1.0f, 0.8f); break;
            case LevelEventPacketEvent::SoundShoot: named("random.bow"); break;
            case LevelEventPacketEvent::SoundDoor: named("random.door_open"); break;
            case LevelEventPacketEvent::SoundFizz: named("random.fizz", 0.5f); break;
            case LevelEventPacketEvent::SoundIgnite: named("fire.ignite"); break;
            case LevelEventPacketEvent::SoundGhast: named("mob.ghast.moan"); break;
            case LevelEventPacketEvent::SoundGhastShoot: named("mob.ghast.fireball"); break;
            case LevelEventPacketEvent::SoundBlazeShoot: named("mob.blaze.shoot"); break;
            case LevelEventPacketEvent::SoundEndermanTeleport: named("mob.endermen.portal"); break;
            case LevelEventPacketEvent::SoundAnvilBreak: named("random.anvil_break"); break;
            case LevelEventPacketEvent::SoundAnvilUse: named("random.anvil_use"); break;
            case LevelEventPacketEvent::SoundAnvilFall: named("random.anvil_land"); break;
            case LevelEventPacketEvent::SoundPop: named("random.pop"); break;
            case LevelEventPacketEvent::SoundPortal: named("portal.portal"); break;
            case LevelEventPacketEvent::SoundOrb: {
                // suppress the server echo of a pickup already predicted locally
                const auto nowMs = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
                const float dx = p.position.x - lastOrbX, dy = p.position.y - lastOrbY, dz = p.position.z - lastOrbZ;
                if (nowMs - lastLocalOrbMs < 200u && dx * dx + dy * dy + dz * dz < 9.0f)
                    break;
                named("random.orb");
                break;
            }
            case LevelEventPacketEvent::SoundTotem: named("random.totem"); break;
            case LevelEventPacketEvent::SoundCamera: named("camera.take_picture"); break;
            case LevelEventPacketEvent::ParticleDestroy: {
                SoundRequest r;
                r.kind = SoundRequest::Kind::Level;
                r.name = "break";
                r.x = p.position.x;
                r.y = p.position.y;
                r.z = p.position.z;
                r.extra = p.data;
                PushSound(std::move(r));
                break;
            }
            default:
                break;
            }
            switch (p.event) {
            case LevelEventPacketEvent::StartRain:
                rainTarget = 1.0f;
                break;
            case LevelEventPacketEvent::StopRain:
                rainTarget = 0.0f;
                break;
            case LevelEventPacketEvent::StartThunder:
                thunderTarget = 1.0f;
                break;
            case LevelEventPacketEvent::StopThunder:
                thunderTarget = 0.0f;
                break;
            default:
                break;
            }
        });

        On<SetCommandsEnabledPacket>([this](const SetCommandsEnabledPacket& p) {
            std::lock_guard<std::mutex> lk(worldMutex);
            world.commandsEnabled = p.enabled;
        });
        On<SetDifficultyPacket>([this](const SetDifficultyPacket& p) {
            std::lock_guard<std::mutex> lk(worldMutex);
            world.difficulty = static_cast<int>(p.difficulty);
        });
        On<GameRulesChangedPacket>([this](const GameRulesChangedPacket& p) {
            {
                std::lock_guard<std::mutex> lk(worldMutex);
                for (const GameRuleI32& rule : p.rules) {
                    std::string name = rule.name;
                    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                    if (const bool* b = std::get_if<bool>(&rule.value))
                        world.rules[name] = *b ? 1 : 0;
                    else if (const int32_t* i = std::get_if<int32_t>(&rule.value))
                        world.rules[name] = *i;
                }
            }
            for (const GameRuleI32& rule : p.rules)
                if (EqualsIgnoreCase(rule.name, "dodaylightcycle"))
                    if (const bool* enabled = std::get_if<bool>(&rule.value))
                        SetDayCycle(*enabled);
        });

        On<SetSpawnPositionPacket>([this](const SetSpawnPositionPacket& p) {
            const BlockCoordinates& world = p.worldPosition;
            const int32_t worldY = static_cast<int32_t>(world.y);
            Log("bedrock", "SetSpawnPosition type={} pos=({},{},{}) dim={} spawn=({},{},{})", static_cast<int>(p.spawnType), p.playerPosition.x, static_cast<int32_t>(p.playerPosition.y), p.playerPosition.z, p.dimension, world.x, worldY, world.z);
            if (worldY < 1000) {
                worldSpawn[0] = world.x;
                worldSpawn[1] = worldY;
                worldSpawn[2] = world.z;
                haveWorldSpawn = true;
            }
        });

        On<UpdateBlockPacket>([this](const UpdateBlockPacket& p) {
            lastBlockPos[0] = p.position.x;
            lastBlockPos[1] = static_cast<int32_t>(p.position.y);
            lastBlockPos[2] = p.position.z;
            haveBlockPos = true;
            static int blockLogs = 0;
            if (blockLogs < 400 || PERLIN_ENV("PERLIN_BLOCK_DEBUG")) {
                if (blockLogs < 400)
                    ++blockLogs;
                Log("block", "update_block ({},{},{}) layer {} id {} {} afterAuthTick={}", p.position.x, p.position.y, p.position.z, p.layer, static_cast<int32_t>(p.blockRuntimeId), BlockPalette::Instance().Name(BlockPalette::Instance().InternRuntime(static_cast<int32_t>(p.blockRuntimeId))), LastAuthTick());
            }
            NoteUnknownBlock(static_cast<int32_t>(p.blockRuntimeId));
            if (p.layer == 0)
                ConfirmPlace(p.position.x, static_cast<int32_t>(p.position.y), p.position.z, static_cast<int32_t>(p.blockRuntimeId));
            if (p.layer == 0) {
                const uint16_t id = BlockPalette::Instance().InternRuntime(static_cast<int32_t>(p.blockRuntimeId));
                auto& mm = mods::ModManager::Instance();
                if (mm.Wants(abi::Event::BlockChanged)) {
                    const uint16_t was = netLevel.GetBlock(lastBlockPos[0], lastBlockPos[1], lastBlockPos[2]);
                    if (was != id) {
                        abi::Writer w;
                        w.write(abi::EventBlockChanged{lastBlockPos[0], lastBlockPos[1], lastBlockPos[2], BlockPalette::Instance().Name(was),
                                                       BlockPalette::Instance().Name(id)});
                        mm.Queue(abi::Event::BlockChanged, std::move(w));
                    }
                }
                netLevel.SetBlock(lastBlockPos[0], lastBlockPos[1], lastBlockPos[2], id);
                std::lock_guard lock(supportMutex);
                const auto now = std::chrono::steady_clock::now();
                std::erase_if(brokenSupports, [&](const BrokenSupport& b) {
                    if (now - b.at > std::chrono::seconds(3))
                        return true;
                    if (b.x != lastBlockPos[0] || b.y != lastBlockPos[1] || b.z != lastBlockPos[2])
                        return false;
                    if (id != 0)
                        for (const auto& [y, old] : b.above) {
                            bool loaded = false;
                            if (netLevel.GetBlock(b.x, y, b.z, loaded) == 0 && loaded)
                                netLevel.SetBlock(b.x, y, b.z, old);
                        }
                    return true;
                });
            }
            else if (p.layer == 1)
                netLevel.SetLiquid(lastBlockPos[0], lastBlockPos[1], lastBlockPos[2],
                                   BlockPalette::Instance().InternRuntime(static_cast<int32_t>(p.blockRuntimeId)));
        });

        On<UpdateSubchunkBlocksPacket>([this](const UpdateSubchunkBlocksPacket& p) {
            const auto sane = [&](const BlockUpdate& b) {
                const int32_t y = static_cast<int32_t>(b.position.y);
                if (y < -64 || y >= 320 || std::abs(b.position.x) > 30000000 || std::abs(b.position.z) > 30000000)
                    return false;
                return netLevel.HasChunk(b.position.x >> 4, b.position.z >> 4);
            };
            static int logged = 0;
            if (logged < 100 || PERLIN_ENV("PERLIN_BLOCK_DEBUG")) {
                if (logged < 100)
                    ++logged;
                int ok = 0;
                for (const BlockUpdate& b : p.blocks) ok += sane(b) ? 1 : 0;
                std::cout << "[block] update_subchunk_blocks at (" << p.x << "," << p.y << "," << p.z << ") " << p.blocks.size()
                          << " blocks (" << ok << " applied), " << p.extra.size() << " extra";
                if (!p.blocks.empty())
                    std::cout << "; first (" << p.blocks[0].position.x << "," << static_cast<int32_t>(p.blocks[0].position.y) << ","
                              << p.blocks[0].position.z << ") rid " << p.blocks[0].runtimeId;
                std::cout << " afterAuthTick=" << LastAuthTick() << std::endl;
            }
            for (const BlockUpdate& b : p.extra)
                if (sane(b))
                    netLevel.SetLiquid(b.position.x, static_cast<int32_t>(b.position.y), b.position.z,
                                       BlockPalette::Instance().InternRuntime(static_cast<int32_t>(b.runtimeId)));
            for (const BlockUpdate& b : p.blocks) {
                if (!sane(b))
                    continue;
                NoteUnknownBlock(static_cast<int32_t>(b.runtimeId));
                ConfirmPlace(b.position.x, static_cast<int32_t>(b.position.y), b.position.z, static_cast<int32_t>(b.runtimeId));
                netLevel.SetBlock(b.position.x, static_cast<int32_t>(b.position.y), b.position.z,
                                  BlockPalette::Instance().InternRuntime(static_cast<int32_t>(b.runtimeId)));
            }
        });

        On<UpdateBlockSyncedPacket>([this](const UpdateBlockSyncedPacket& p) {
            const int x = p.position.x, y = static_cast<int32_t>(p.position.y), z = p.position.z;
            const uint16_t id = BlockPalette::Instance().InternRuntime(static_cast<int32_t>(p.blockRuntimeId));
            if (p.layer == 0)
                ConfirmPlace(x, y, z, static_cast<int32_t>(p.blockRuntimeId));
            if (p.layer == 0)
                netLevel.SetBlock(x, y, z, id);
            else if (p.layer == 1)
                netLevel.SetLiquid(x, y, z, id);
        });

        On<MovePlayerPacket>([this](const MovePlayerPacket& p) {
            if (runtimeId != 0 && p.runtimeId != runtimeId)
                return;
            runtimeId = p.runtimeId;
            const Vec3F& eye = p.position;
            if (!SaneWorldPos(eye.x, eye.y, eye.z))
                return;
            pose = {eye.x, eye.y - kPlayerEyeHeight, eye.z, p.yaw, p.pitch, p.headYaw, p.onGround, true};
            haveLastAuthPos = false;
            gotSpawn = true;
            const bool forced = p.mode == MovePlayerPacketMode::Teleport || p.mode == MovePlayerPacketMode::Reset;
            if (forced) {
                pendingTeleportAck = true;
                ServerMotion motion;
                motion.poseChanged = true;
                motion.teleport = true;
                motion.hasRotation = true;
                motion.filterTravel = p.mode == MovePlayerPacketMode::Teleport;
                motion.tick = p.tick;
                motion.onGround = p.onGround;
                motion.yaw = p.yaw;
                motion.pitch = p.pitch;
                motion.hasVelocity = true;
                QueueServerMotion(motion);
            }
        });

        On<DeathInfoPacket>([this](const DeathInfoPacket& p) {
            const std::string text = ui::FormatTextPacket(2, true, std::string(), p.cause, p.messages);
            Log("bedrock", "death_info {} -> {}", p.cause, text);
            std::lock_guard<std::mutex> lk(deathMutex);
            deathCause = text;
            deathPending = true;
        });

        On<RespawnPacket>([this](const RespawnPacket& p) {
            Log("bedrock", "Respawn pos=({},{},{}) state={}", p.position.x, p.position.y, p.position.z, static_cast<int>(p.state));
            if (p.state != 1)
                return;
            {
                std::lock_guard<std::mutex> lk(deathMutex);
                deathCause.clear();
                deathPending = false;
            }
            if (SaneWorldPos(p.position.x, p.position.y, p.position.z)) {
                pose = {p.position.x, p.position.y - kPlayerEyeHeight,
                        p.position.z, pose.yaw,
                        pose.pitch,   pose.headYaw,
                        false,        true};
                gotSpawn = true;
                haveLastAuthPos = false;
                ServerMotion motion;
                motion.poseChanged = true;
                motion.teleport = true;
                motion.hasVelocity = true;
                QueueServerMotion(motion);
            }
            proto::PlayerActionPacket done;
            done.runtimeEntityId = runtimeId;
            done.action = static_cast<proto::Action>(7);
            done.position = {0, 0, 0};
            done.resultPosition = {0, 0, 0};
            done.face = -1;
            Log("bedrock", "respawn ready -> PlayerAction Respawn");
            Send(done);
        });

        On<CorrectPlayerMovePredictionPacket>([this](const CorrectPlayerMovePredictionPacket& p) {
            if (p.predictionType == CorrectPlayerMovePredictionPacketPredictionType::Vehicle) {
                if (!SaneWorldPos(p.position.x, p.position.y, p.position.z))
                    return;
                VehicleCorrection c;
                c.tick = p.tick;
                c.pos[0] = p.position.x;
                c.pos[1] = p.position.y;
                c.pos[2] = p.position.z;
                c.delta[0] = p.delta.x;
                c.delta[1] = p.delta.y;
                c.delta[2] = p.delta.z;
                c.rotation[0] = p.rotation.x;
                c.rotation[1] = p.rotation.z;
                c.angularVelocity = p.angularVelocity;
                c.onGround = p.onGround;
                std::lock_guard lk(vehicleCorrectionMutex);
                vehicleCorrections.push_back(c);
                while (vehicleCorrections.size() > 64)
                    vehicleCorrections.pop_front();
                return;
            }
            if (p.predictionType != CorrectPlayerMovePredictionPacketPredictionType::Player)
                return;
            if (!SaneWorldPos(p.position.x, p.position.y, p.position.z))
                return;
            pose.x = p.position.x;
            pose.y = p.position.y - kPlayerEyeHeight;
            pose.z = p.position.z;
            pose.onGround = p.onGround;
            haveLastAuthPos = false;
            ArmPredictionSync();
            if (correctionsReceived++ < 5 || correctionsReceived % 100 == 0)
                Log("move", "server correction #{} tick={} pos=({},{},{}) delta=({},{},{})", correctionsReceived.load(), p.tick, p.position.x, p.position.y, p.position.z, p.delta.x, p.delta.y, p.delta.z);
            if (PERLIN_ENV("PERLIN_BLOCK_DEBUG"))
                Log("move", "correction_received #{} tick={} afterAuthTick={}", correctionsReceived.load(), p.tick, LastAuthTick());
            ServerMotion motion;
            motion.poseChanged = true;
            motion.correction = true;
            motion.tick = p.tick;
            motion.onGround = p.onGround;
            motion.hasVelocity = true;
            motion.velocity[0] = p.delta.x;
            motion.velocity[1] = p.delta.y;
            motion.velocity[2] = p.delta.z;
            QueueServerMotion(motion);
        });

        On<SetEntityMotionPacket>([this](const SetEntityMotionPacket& p) {
            if (runtimeId == 0 || p.runtimeEntityId != runtimeId)
                return;
            ServerMotion motion;
            motion.hasVelocity = true;
            motion.stampedVelocity = p.tick != 0;
            motion.tick = p.tick;
            motion.velocity[0] = p.velocity.x;
            motion.velocity[1] = p.velocity.y;
            motion.velocity[2] = p.velocity.z;
            QueueServerMotion(motion);
            if (PERLIN_ENV("PERLIN_BLOCK_DEBUG"))
                Log("move", "motion_received tick={} afterAuthTick={} velocity=({},{},{})", p.tick, LastAuthTick(), p.velocity.x, p.velocity.y, p.velocity.z);
            NoteKnockback(p.velocity.x, p.velocity.z);
        });

        On<AddPlayerPacket>([this](const AddPlayerPacket& p) {
            if (runtimeId != 0 && p.runtimeId == runtimeId)
                return;
            {
                std::lock_guard<std::mutex> lk(tabMutex);
                permByUid[p.uniqueId] = static_cast<int>(p.permissionLevel);
                for (TabPlayer& t : tabPlayers)
                    if (t.uid == p.uniqueId)
                        t.perm = static_cast<int>(p.permissionLevel);
            }
            std::string held;
            if (const auto it = itemNames.find(p.heldItem.networkId); p.heldItem.networkId != 0 && it != itemNames.end())
                held = it->second;
            // AddPlayer carries the feet position; move packets carry the eye
            entities.AddPlayer(p.runtimeId, p.uniqueId, UuidKey(p.uuid), p.username,
                               {p.position.x, p.position.y, p.position.z}, p.pitch, p.yaw, p.headYaw, MetaFlags(p.metadata), held);
            if (std::getenv("PERLIN_DEBUG_NAMETAGS") != nullptr) {
                std::string flat = p.username;
                for (char& c : flat)
                    if (c == '\n')
                        c = '|';
                Log("npcname", "addplayer rid={} user={}", p.runtimeId, flat);
            }
            ApplyMetadata(entities, p.runtimeId, p.metadata);
            ApplyEntityProperties(p.runtimeId, "minecraft:player", p.properties);
        });

        On<AddPaintingPacket>([this](const AddPaintingPacket& p) {
            entities.AddEntity(p.runtimeEntityId, p.entityIdSelf, "minecraft:painting", {p.coordinates.x, p.coordinates.y, p.coordinates.z}, 0.0f,
                               0.0f, 0.0f, 0, 1.0f, 0, 0, 0);
            entities.SetPropertyText(p.runtimeEntityId, "painting_motive", p.title);
            entities.SetProperty(p.runtimeEntityId, "painting_direction", static_cast<float>(p.direction));
        });

        On<AddEntityPacket>([this](const AddEntityPacket& p) {
            if (runtimeId != 0 && p.runtimeId == runtimeId)
                return;
            entities.AddEntity(p.runtimeId, p.uniqueId, p.entityType, {p.position.x, p.position.y, p.position.z}, p.pitch, p.yaw,
                               p.headYaw, MetaFlags(p.metadata), 1.0f, 0, 0, 0);
            ApplyMetadata(entities, p.runtimeId, p.metadata);
            ApplyEntityProperties(p.runtimeId, p.entityType, p.properties);
            for (const EntityAttributesEntry& a : p.attributes) {
                entities.SetAttribute(p.runtimeId, a.name, a.min, a.value, a.max);
                if (a.name == "minecraft:health")
                    entities.SetHealth(p.runtimeId, a.value);
            }
        });

        On<VoxelShapesPacket>([](const VoxelShapesPacket& p) {
            VoxelShapeRegistry::Instance().Set(p);
            Log("bedrock", "VoxelShapes: {} shapes, {} named", p.shapes.size(), p.nameMap.size());
        });

        On<AddItemEntityPacket>([this](const AddItemEntityPacket& p) {
            const auto it = itemNames.find(p.item.networkId);
            if (p.item.networkId == 0 || it == itemNames.end() || p.item.count == 0)
                return;
            if (droppedRaw.size() > 1024)
                droppedRaw.clear();
            droppedRaw[p.runtimeEntityId] = p.item;
            entities.AddItem(p.runtimeEntityId, p.entityIdSelf, {p.position.x, p.position.y, p.position.z}, it->second, p.item.count,
                             static_cast<int>(p.item.metadata), p.item.blockRuntimeId, false);
            entities.SetMotion(p.runtimeEntityId, {p.velocity.x, p.velocity.y, p.velocity.z});
        });
        On<SetEntityMotionPacket>([this](const SetEntityMotionPacket& p) {
            entities.SetMotion(p.runtimeEntityId, {p.velocity.x, p.velocity.y, p.velocity.z});
        });

        On<TakeItemEntityPacket>([this](const TakeItemEntityPacket& p) {
            droppedRaw.erase(p.runtimeEntityId);
            entity::Entity taken;
            if (entities.Find(p.runtimeEntityId, taken) && runtimeId != 0 && p.target == runtimeId) {
                const auto nowMs = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
                if (taken.isItem) {
                    SoundRequest r;
                    r.kind = SoundRequest::Kind::Named;
                    r.name = "random.pop";
                    r.x = static_cast<float>(taken.pos.x);
                    r.y = static_cast<float>(taken.pos.y);
                    r.z = static_cast<float>(taken.pos.z);
                    r.volume = 0.2f;
                    r.pitch = 1.4f + (static_cast<float>(taken.runtimeId % 7) * 0.03f);
                    PushSound(std::move(r));
                } else if (taken.type == "minecraft:xp_orb") {
                    // vanilla predicts the orb pickup sound locally; server echo (SoundOrb) suppressed below for the same pickup
                    SoundRequest r;
                    r.kind = SoundRequest::Kind::Named;
                    r.name = "random.orb";
                    r.x = static_cast<float>(taken.pos.x);
                    r.y = static_cast<float>(taken.pos.y);
                    r.z = static_cast<float>(taken.pos.z);
                    r.volume = 0.35f;
                    r.pitch = 1.0f + (static_cast<float>(taken.runtimeId % 5) * 0.03f);
                    PushSound(std::move(r));
                    lastLocalOrbMs = nowMs;
                    lastOrbX = r.x;
                    lastOrbY = r.y;
                    lastOrbZ = r.z;
                }
            }
            entities.Pickup(p.runtimeEntityId, p.target);
        });

        On<RemoveEntityPacket>([this](const RemoveEntityPacket& p) { entities.Remove(p.entityIdSelf); });

        On<MoveEntityPacket>([this](const MoveEntityPacket& p) {
            if (runtimeId != 0 && p.runtimeEntityId == runtimeId)
                return;
            // the client-predicted vehicle is moved by our own simulation
            if (p.runtimeEntityId == predictedVehicleRuntime.load())
                return;
            constexpr float kByte = 360.0f / 256.0f;
            entities.Move(p.runtimeEntityId, {p.position.x, p.position.y, p.position.z}, p.rotation.pitch * kByte,
                          p.rotation.yaw * kByte, p.rotation.headYaw * kByte, (p.flags & 1) != 0);
        });

        On<MoveEntityDeltaPacket>([this](const MoveEntityDeltaPacket& p) {
            if (runtimeId != 0 && p.runtimeEntityId == runtimeId)
                return;
            if (p.runtimeEntityId == predictedVehicleRuntime.load())
                return;
            constexpr float kByte = 360.0f / 256.0f;
            double x = 0, y = 0, z = 0;
            float pitch = 0, yaw = 0, head = 0;
            if (p.x) x = *p.x;
            if (p.y) y = *p.y;
            if (p.z) z = *p.z;
            if (p.rotX) pitch = *p.rotX * kByte;
            if (p.rotY) yaw = *p.rotY * kByte;
            if (p.rotZ) head = *p.rotZ * kByte;
            entities.MoveDelta(p.runtimeEntityId, p.x ? &x : nullptr, p.y ? &y : nullptr, p.z ? &z : nullptr,
                               p.rotX ? &pitch : nullptr, p.rotY ? &yaw : nullptr, p.rotZ ? &head : nullptr, p.onGround);
        });

        On<MovePlayerPacket>([this](const MovePlayerPacket& p) {
            if (runtimeId != 0 && p.runtimeId == runtimeId)
                return;
            entities.Move(p.runtimeId, {p.position.x, p.position.y, p.position.z}, p.pitch, p.yaw, p.headYaw, p.onGround);
        });

        On<SetEntityDataPacket>([this](const SetEntityDataPacket& p) {
            if (runtimeId != 0 && p.runtimeEntityId == runtimeId) {
                std::lock_guard lk(attrMutex);
                for (const MetadataDictionaryEntry& m : p.metadata) {
                    int64_t integer = 0;
                    if (m.key == MetadataDictionaryEntryKey::Flags || m.key == MetadataDictionaryEntryKey::FlagsExtended) {
                        if (MetaInt(m, integer)) {
                            const size_t word = m.key == MetadataDictionaryEntryKey::Flags ? 0 : 1;
                            ownFlagWords[word] = static_cast<uint64_t>(integer);
                            ownFlagWordsKnown[word] = true;
                        }
                        continue;
                    }
                    if (m.key == MetadataDictionaryEntryKey::RiderSeatPosition) {
                        if (const auto* inner = std::get_if<4>(&m.value))
                            if (const auto* seat = std::get_if<9>(inner))
                                ownSeatOffset = std::array<float, 3>{seat->x, seat->y, seat->z};
                        continue;
                    }
                    double n = 0;
                    if (!MetaNumber(m, n))
                        continue;
                    if (m.key == MetadataDictionaryEntryKey::PlayerFlags)
                        ownPlayerFlags = static_cast<int64_t>(n);
                    else if (m.key == MetadataDictionaryEntryKey::FreezingEffectStrength)
                        freezeUpdates.push_back({p.tick, static_cast<float>(n)});
                    else if (m.key == MetadataDictionaryEntryKey::Scale)
                        ownScale = static_cast<float>(n);
                    else if (m.key == MetadataDictionaryEntryKey::BoundingboxWidth)
                        ownWidth = static_cast<float>(n);
                    else if (m.key == MetadataDictionaryEntryKey::BoundingboxHeight)
                        ownHeight = static_cast<float>(n);
                }
                return;
            }
            ApplyMetadata(entities, p.runtimeEntityId, p.metadata);
            entity::Entity target;
            if (entities.Find(p.runtimeEntityId, target))
                ApplyEntityProperties(p.runtimeEntityId, target.type, p.properties);
        });

        On<PlayerListPacket>([this](const PlayerListPacket& p) {
            for (const PlayerRecord& r : p.records) {
                const std::string key = UuidKey(r.uuid);
                if (r.type == PlayerRecordType::Add) {
                    auto skin = ConvertSkin(r.skinData);
                    entities.SetSkin(key, skin);
                    std::lock_guard<std::mutex> lk(tabMutex);
                    bool found = false;
                    for (TabPlayer& t : tabPlayers)
                        if (t.uuid == key) {
                            t.name = r.username;
                            t.skin = skin;
                            found = true;
                        }
                    if (!found) {
                        TabPlayer tp{key, r.username, skin};
                        tp.uid = r.entityUniqueId;
                        tabPlayers.push_back(std::move(tp));
                    }
                    for (TabPlayer& t : tabPlayers)
                        if (t.uuid == key) {
                            t.uid = r.entityUniqueId;
                            if (const auto pm = permByUid.find(t.uid); pm != permByUid.end())
                                t.perm = pm->second;
                        }
                } else {
                    entities.RemoveSkin(key);
                    std::lock_guard<std::mutex> lk(tabMutex);
                    tabPlayers.erase(std::remove_if(tabPlayers.begin(), tabPlayers.end(), [&](const TabPlayer& t) { return t.uuid == key; }),
                                     tabPlayers.end());
                }
            }
        });

        On<PlayerSkinPacket>([this](const PlayerSkinPacket& p) {
            const std::string key = UuidKey(p.uuid);
            auto skin = ConvertSkin(p.skin);
            entities.SetSkin(key, skin, false);
            std::lock_guard<std::mutex> lk(tabMutex);
            for (TabPlayer& t : tabPlayers)
                if (t.uuid == key)
                    t.skin = skin;
        });

        On<MobEquipmentPacket>([this](const MobEquipmentPacket& p) {
            if (runtimeId != 0 && p.runtimeEntityId == runtimeId)
                return;
            std::string name;
            if (const auto it = itemNames.find(p.item.networkId); p.item.networkId != 0 && it != itemNames.end())
                name = it->second;
            if (static_cast<int>(p.windowId) == 119)
                entities.SetOffHand(p.runtimeEntityId, name);
            else
                entities.SetHeld(p.runtimeEntityId, name);
        });

        On<MobArmorEquipmentPacket>([this](const MobArmorEquipmentPacket& p) {
            if (runtimeId != 0 && p.runtimeEntityId == runtimeId)
                return;
            const auto nameOf = [&](const auto& item) -> std::string {
                if (const auto it = itemNames.find(item.networkId); item.networkId != 0 && it != itemNames.end())
                    return it->second;
                return {};
            };
            entities.SetArmor(p.runtimeEntityId, 0, nameOf(p.helmet));
            entities.SetArmor(p.runtimeEntityId, 1, nameOf(p.chestplate));
            entities.SetArmor(p.runtimeEntityId, 2, nameOf(p.leggings));
            entities.SetArmor(p.runtimeEntityId, 3, nameOf(p.boots));
        });

        On<AnimatePacket>([this](const AnimatePacket& p) {
            if (p.actionId == AnimatePacketActionId::SwingArm && !(runtimeId != 0 && p.runtimeEntityId == runtimeId))
                entities.Swing(p.runtimeEntityId);
        });

        On<EntityEventPacket>([this](const EntityEventPacket& p) {
            if (p.eventId == EntityEventPacketEventId::HurtAnimation && !(runtimeId != 0 && p.runtimeEntityId == runtimeId))
                entities.Hurt(p.runtimeEntityId);
            if (p.eventId == EntityEventPacketEventId::DeathAnimation && !(runtimeId != 0 && p.runtimeEntityId == runtimeId))
                entities.Die(p.runtimeEntityId);
        });

        On<UpdateAttributesPacket>([this](const UpdateAttributesPacket& p) {
            if (runtimeId != 0 && p.runtimeEntityId != runtimeId) {
                for (const PlayerAttributesEntry& a : p.attributes) {
                    entities.SetAttribute(p.runtimeEntityId, a.name, a.min, a.current, a.max);
                    if (a.name == "minecraft:health")
                        entities.SetHealth(p.runtimeEntityId, a.current);
                }
                return;
            }
            {
                std::lock_guard lk(attrMutex);
                for (const PlayerAttributesEntry& a : p.attributes) {
                    ownAttributes[a.name] = a.current;
                    if (a.name == "minecraft:movement") {
                        movementAttribute = a;
                        ApplyMovementAttribute(a);
                    }
                }
            }
            for (const PlayerAttributesEntry& a : p.attributes) {
                if (a.name == "minecraft:health") {
                    const bool recent = std::chrono::steady_clock::now() - lastHurtAt < std::chrono::milliseconds(250);
                    if (lastHealth >= 0.0f && a.current < lastHealth - 0.01f && a.current > 0.0f && !recent)
                        NoteHurt();
                    lastHealth = a.current;
                    std::lock_guard<std::mutex> lk(deathMutex);
                    if (a.current > 0.0f && deathPending) {
                        deathPending = false;
                        deathCause.clear();
                    } else if (a.current <= 0.0f) {
                        deathPending = true;
                    }
                }
            }
        });

        On<EntityEventPacket>([this](const EntityEventPacket& p) {
            if (runtimeId != 0 && p.runtimeEntityId == runtimeId && p.eventId == EntityEventPacketEventId::HurtAnimation)
                NoteHurt();
            if (runtimeId != 0 && p.runtimeEntityId == runtimeId && p.eventId == EntityEventPacketEventId::DeathAnimation) {
                Log("bedrock", "own death animation");
                std::lock_guard<std::mutex> lk(deathMutex);
                if (lastHealth <= 0.01f)
                    deathPending = true;
            }
        });

        On<EntityEventPacket>([this](const EntityEventPacket& p) {
            const char* event = nullptr;
            const char* direct = nullptr;
            switch (p.eventId) {
            case EntityEventPacketEventId::HurtAnimation: event = "hurt"; break;
            case EntityEventPacketEventId::DeathAnimation: event = "death"; break;
            case EntityEventPacketEventId::ElderGuardianCurse: event = "elderguardian.curse"; break;
            case EntityEventPacketEventId::EatingItem: event = "eat"; break;
            case EntityEventPacketEventId::WitchDrinkPotion: event = "drink"; break;
            case EntityEventPacketEventId::PlayerAddXpLevels: direct = "random.levelup"; break;
            case EntityEventPacketEventId::CreeperPrimeFuse: direct = "random.fuse"; break;
            case EntityEventPacketEventId::ConsumeTotem: direct = "random.totem"; break;
            default: break;
            }
            if (!event && !direct)
                return;
            SoundRequest r;
            const bool self = runtimeId != 0 && p.runtimeEntityId == runtimeId;
            if (self && (p.eventId == EntityEventPacketEventId::EatingItem))
                return;
            if (self) {
                r.entityType = "minecraft:player";
                r.x = pose.x;
                r.y = pose.y;
                r.z = pose.z;
            } else {
                entity::Entity e;
                if (!entities.Find(p.runtimeEntityId, e))
                    return;
                r.entityType = e.type;
                r.x = static_cast<float>(e.pos.x);
                r.y = static_cast<float>(e.pos.y);
                r.z = static_cast<float>(e.pos.z);
                r.baby = e.Has(entity::flag::Baby);
            }
            if (direct) {
                r.kind = SoundRequest::Kind::Named;
                r.name = direct;
                r.entityType.clear();
            } else {
                r.kind = SoundRequest::Kind::Level;
                r.name = event;
            }
            PushSound(std::move(r));
        });

        On<SpawnParticleEffectPacket>([this](const SpawnParticleEffectPacket& p) {
            ParticleEvent pe;
            pe.name = p.particleName;
            if (p.molangVariables)
                pe.molang = *p.molangVariables;
            pe.x = p.position.x;
            pe.y = p.position.y;
            pe.z = p.position.z;
            PushParticle(std::move(pe));
        });

        On<EntityEventPacket>([this](const EntityEventPacket& p) {
            if (p.eventId != EntityEventPacketEventId::DeathAnimation || (runtimeId != 0 && p.runtimeEntityId == runtimeId))
                return;
            entity::Entity e;
            if (!entities.Find(p.runtimeEntityId, e) || e.isPlayer)
                return;
            ParticleEvent pe;
            pe.event = 2007;
            pe.x = e.pos.x;
            pe.y = e.pos.y + 0.5;
            pe.z = e.pos.z;
            PushParticle(std::move(pe));
        });

        On<EntityEventPacket>([this](const EntityEventPacket& p) {
            if (p.eventId != EntityEventPacketEventId::ConsumeTotem)
                return;
            glm::dvec3 at{pose.x, pose.y + 0.9, pose.z};
            if (runtimeId != 0 && p.runtimeEntityId == runtimeId) {
                std::lock_guard<std::mutex> lk(worldMutex);
                screenAnimation = {kTotemScreenAnimation, std::chrono::steady_clock::now()};
            } else {
                entity::Entity e;
                if (!entities.Find(p.runtimeEntityId, e))
                    return;
                at = {e.pos.x, e.pos.y + 0.9, e.pos.z};
            }
            ParticleEvent pe;
            pe.event = 20065;
            pe.x = at.x;
            pe.y = at.y;
            pe.z = at.z;
            PushParticle(std::move(pe));
        });

        On<SetEntityLinkPacket>([this](const SetEntityLinkPacket& p) {
            const int64_t rider = p.link.riderEntityId == 0 ? static_cast<int64_t>(uniqueId) : p.link.riderEntityId;
            entities.SetVehicle(rider, p.link.type == 0 ? 0 : p.link.riddenEntityId);
            if (rider == static_cast<int64_t>(uniqueId)) {
                ridingUid = p.link.type == 0 ? 0 : p.link.riddenEntityId;
                ridingDriver = p.link.type == 1;
            }
            Log("bedrock", "link rider={} vehicle={} type={}", rider, p.link.riddenEntityId, int(p.link.type));
        });

        On<UpdateAbilitiesPacket>([this](const UpdateAbilitiesPacket& p) {
            {
                std::lock_guard<std::mutex> lk(tabMutex);
                permByUid[p.entityUniqueId] = static_cast<int>(p.permissionLevel);
                for (TabPlayer& t : tabPlayers)
                    if (t.uid == p.entityUniqueId)
                        t.perm = static_cast<int>(p.permissionLevel);
            }
            if (runtimeId != 0 && p.entityUniqueId != 0 && uniqueId != 0 && p.entityUniqueId != uniqueId)
                return;
            bool fly = false, flying = false, clip = false, build = false;
            for (const AbilityLayers& layer : p.abilities) {
                if (layer.allowed & (1u << 10)) fly = (layer.enabled & (1u << 10)) != 0;
                if (layer.allowed & (1u << 11)) build = (layer.enabled & (1u << 11)) != 0;
                if (layer.allowed & (1u << 9)) flying = (layer.enabled & (1u << 9)) != 0;
                if (layer.allowed & (1u << 17)) clip = (layer.enabled & (1u << 17)) != 0;
                if (layer.allowed & (1u << 13)) flySpeed = layer.flySpeed;
                if (layer.allowed & (1u << 19)) vFlySpeed = layer.verticalFlySpeed;
                if (layer.allowed & (1u << 14)) walkSpeed = layer.walkSpeed;
            }
            mayFly = fly;
            instabuild = build;
            isFlying = flying;
            ++abilitiesSeq;
            noClip = clip;
            Log("bedrock", "abilities mayFly={} flying={} noclip={} fly={}", mayFly, isFlying, noClip, flySpeed);
        });

        On<UpdatePlayerGameTypePacket>([this](const UpdatePlayerGameTypePacket& p) {
            if (uniqueId != 0 && p.playerUniqueId != uniqueId)
                return;
            gameType = static_cast<int>(p.gamemode);
            int worldGameType;
            {
                std::lock_guard<std::mutex> lk(worldMutex);
                worldGameType = world.worldGameMode;
            }
            ApplyGameTypeAbilities(worldGameType);
        });
        On<SetDefaultGameTypePacket>([this](const SetDefaultGameTypePacket& p) {
            std::lock_guard<std::mutex> lk(worldMutex);
            world.worldGameMode = static_cast<int>(p.gamemode);
        });

        On<UpdateAdventureSettingsPacket>([this](const UpdateAdventureSettingsPacket& p) {
            std::lock_guard<std::mutex> lk(worldMutex);
            adventure = {p.noPvm, p.noMvp, p.immutableWorld, p.showNameTags, p.autoJump};
        });

        On<OnScreenTextureAnimationPacket>([this](const OnScreenTextureAnimationPacket& p) {
            std::lock_guard<std::mutex> lk(worldMutex);
            screenAnimation = {static_cast<int>(p.animationType), std::chrono::steady_clock::now()};
        });

        On<ShowCreditsPacket>([this](const ShowCreditsPacket& p) {
            if (p.status == 0 && (runtimeId == 0 || p.runtimeEntityId == runtimeId))
                creditsRequested = true;
        });

        On<SetPlayerGameTypePacket>([this](const SetPlayerGameTypePacket& p) {
            gameType = static_cast<int>(p.gamemode);
            int worldGameType;
            {
                std::lock_guard<std::mutex> lk(worldMutex);
                worldGameType = world.worldGameMode;
            }
            ApplyGameTypeAbilities(worldGameType);
        });

        On<NetworkStackLatencyPacket>([this](const NetworkStackLatencyPacket& p) {
            if (p.needsResponse)
                SendTickSync(p.timestamp);
        });

        On<CommandOutputPacket>([this](const CommandOutputPacket& p) {
            {
                std::lock_guard lock(commandResultMutex);
                ++commandResult.received;
                commandResult.success = p.successCount;
                commandResult.output.clear();
                for (const auto& message : p.output) {
                    commandResult.output += message.messageId;
                    for (const auto& parameter : message.parameters) commandResult.output += " " + parameter;
                    commandResult.output += '\n';
                }
            }
            std::cout << "[bedrock] CommandOutput success=" << p.successCount << " msgs=" << p.output.size();
            for (size_t i = 0; i < p.output.size() && i < 4; i++) {
                std::cout << " [" << p.output[i].messageId;
                for (size_t j = 0; j < p.output[i].parameters.size() && j < 4; j++)
                    std::cout << " '" << p.output[i].parameters[j].substr(0, 40) << "'";
                std::cout << "]";
            }
            std::cout << std::endl;
        });

        if (PERLIN_ENV("PERLIN_TRACE")) {
            Trace<StartGamePacket>(*this);
            Trace<UpdateAbilitiesPacket>(*this);
            Trace<TextPacket>(*this);
            Trace<CommandOutputPacket>(*this);
            Trace<MovePlayerPacket>(*this);
        }

        On<LevelChunkPacket>([this](const LevelChunkPacket& p) { HandleLevelChunk(p); });
        On<SubchunkPacket>([this](const SubchunkPacket& p) { HandleSubChunk(p); });
        RegisterPlayerStateHandlers();
    }

}
