#include "client/PerlinInternal.hpp"
#include "client/Perlin.hpp"

#include "client/mods/ModManager.hpp"
#include "client/platform/AppPlatform.hpp"
#include "client/ui/HudRenderers.hpp"
#include "client/ui/ItemData.hpp"
#include "client/ui/UiAssets.hpp"
#include "network/RakNet.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <thread>

namespace perlin {
    namespace {
        using Clock = std::chrono::steady_clock;
        using abi::Status;
        using mods::CallContext;

        Status ToStatus(util::SocketResult r) {
            switch (r) {
            case util::SocketResult::Ok:
                return Status::Ok;
            case util::SocketResult::BadArguments:
                return Status::BadArguments;
            case util::SocketResult::Unavailable:
                return Status::Unavailable;
            case util::SocketResult::NotFound:
                return Status::NotFound;
            }
            return Status::Failed;
        }

        bool Finite(std::initializer_list<float> values) {
            for (float v : values)
                if (!std::isfinite(v))
                    return false;
            return true;
        }

        std::string BareName(const std::string& id) {
            return id.starts_with("minecraft:") ? id.substr(10) : id;
        }

        int GuessMaxStack(const std::string& id, int durability) {
            if (durability > 0)
                return 1;
            const std::string n = BareName(id);
            static const std::set<std::string> sixteen = {"ender_pearl", "snowball", "egg", "bucket", "honey_bottle",
                                                          "armor_stand", "banner", "oak_sign"};
            static const std::set<std::string> single = {"potion", "splash_potion", "lingering_potion", "saddle", "cake",
                                                         "minecart", "chest_minecart", "hopper_minecart",
                                                         "tnt_minecart", "furnace_minecart", "totem_of_undying",
                                                         "writable_book", "bed", "water_bucket", "lava_bucket",
                                                         "milk_bucket", "powder_snow_bucket", "mushroom_stew",
                                                         "rabbit_stew", "beetroot_soup", "suspicious_stew"};
            if (sixteen.contains(n) || n.ends_with("_sign") || n.ends_with("_banner") || n.ends_with("_hanging_sign"))
                return 16;
            if (single.contains(n) || n.ends_with("_boat") || n.ends_with("_bed") || n.ends_with("_bucket") ||
                n.starts_with("music_disc"))
                return 1;
            return 64;
        }

        std::pair<std::string, int> SplitAddress(const std::string& address) {
            const size_t colon = address.rfind(':');
            if (colon == std::string::npos || address.find(':') != colon)
                return {address, 19132};
            return {address.substr(0, colon), std::clamp(std::atoi(address.c_str() + colon + 1), 1, 65535)};
        }
    }

    void Perlin::SetupModExtra() {
        using abi::Op;
        auto& mm = mods::ModManager::Instance();

        mm.Register(Op::ItemInfo, [this](CallContext&, abi::Reader& r, abi::Writer& w) {
            const std::string name = r.str();
            if (!r.Ok() || name.empty() || name.size() > 128)
                return Status::BadArguments;
            const std::string id = name.find(':') == std::string::npos ? "minecraft:" + name : name;
            abi::ItemInfoData info;
            info.found = true;
            info.name = id;
            info.displayName = ui::UiAssets::Instance().ItemName(id, 0);
            info.maxDurability = ui::MaxDurability(id);
            info.maxStack = GuessMaxStack(id, info.maxDurability);
            w.write(info);
            return Status::Ok;
        });

        mm.Register(Op::ItemNames, [this](CallContext&, abi::Reader&, abi::Writer& w) {
            if (!modLive)
                return Status::Unavailable;
            std::vector<std::string> names = modLive->ItemNameList();
            std::sort(names.begin(), names.end());
            w.write(names);
            return Status::Ok;
        });

        mm.Register(Op::SlotOverlay, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            abi::SlotOverlayData d = r.read<abi::SlotOverlayData>();
            if (!r.Ok() || d.id.empty() || d.id.size() > 64 || d.text.size() > 64 || d.slot < 0 || d.slot > 100 ||
                !std::isfinite(d.bar) || d.ticks < 0)
                return Status::BadArguments;
            const std::string prefix = ctx.plugin + "/";
            {
                std::lock_guard lock(modExtra.mutex);
                const std::string key = prefix + d.id;
                if (!modExtra.overlays.contains(key)) {
                    size_t mine = 0;
                    for (const auto& e : modExtra.overlays)
                        mine += e.first.starts_with(prefix) ? 1 : 0;
                    if (mine >= 64)
                        return Status::Unavailable;
                }
                ModExtra::Overlay o;
                o.forever = d.ticks == 0;
                o.until = Clock::now() + std::chrono::milliseconds(std::clamp(d.ticks, 0, 72000) * 50);
                o.data = std::move(d);
                modExtra.overlays[key] = std::move(o);
            }
            ui::BumpHudFilter();
            return Status::Ok;
        });

        mm.Register(Op::SlotOverlayClear, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const std::string id = r.str();
            if (!r.Ok() || id.empty())
                return Status::BadArguments;
            bool removed;
            {
                std::lock_guard lock(modExtra.mutex);
                removed = modExtra.overlays.erase(ctx.plugin + "/" + id) > 0;
            }
            if (removed)
                ui::BumpHudFilter();
            return Status::Ok;
        });

        mm.Register(Op::SocketOpen, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const abi::SocketOpenRequest q = r.read<abi::SocketOpenRequest>();
            if (!r.Ok())
                return Status::BadArguments;
            util::SocketOpenOptions o;
            o.id = q.id;
            o.webSocket = q.kind == abi::SocketKind::WebSocket;
            o.url = q.url;
            o.timeoutMs = q.timeoutMs;
            o.headers = q.headers;
            o.protocols = q.protocols;
            std::lock_guard lock(modExtra.mutex);
            auto& set = modExtra.sockets[ctx.plugin];
            if (!set)
                set = std::make_unique<util::SocketSet>();
            return ToStatus(set->Open(std::move(o)));
        });

        mm.Register(Op::SocketSend, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            abi::SocketSendRequest q = r.read<abi::SocketSendRequest>();
            if (!r.Ok())
                return Status::BadArguments;
            std::lock_guard lock(modExtra.mutex);
            const auto it = modExtra.sockets.find(ctx.plugin);
            if (it == modExtra.sockets.end())
                return Status::NotFound;
            return ToStatus(it->second->Send(q.id, q.binary, std::move(q.data)));
        });

        mm.Register(Op::SocketClose, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            abi::SocketCloseRequest q = r.read<abi::SocketCloseRequest>();
            if (!r.Ok())
                return Status::BadArguments;
            std::lock_guard lock(modExtra.mutex);
            const auto it = modExtra.sockets.find(ctx.plugin);
            if (it == modExtra.sockets.end())
                return Status::NotFound;
            return ToStatus(it->second->Close(q.id, q.code, std::move(q.reason)));
        });

        mm.Register(Op::ConnectionStats, [this](CallContext&, abi::Reader&, abi::Writer& w) {
            abi::ConnectionStatsData d;
            if (modLive) {
                d.connected = modLive->GetState() == BedrockSession::State::InGame;
                d.packetsIn = modLive->PacketsReceived();
                d.packetsOut = modLive->PacketsSent();
                d.bytesIn = modLive->BytesReceived();
                d.bytesOut = modLive->BytesSent();
            }
            d.address = d.connected ? options.server : std::string();
            d.pingMs = modExtra.pingMs.load();
            d.packetsInPerSecond = modExtra.perSecond[0];
            d.packetsOutPerSecond = modExtra.perSecond[1];
            d.bytesInPerSecond = modExtra.perSecond[2];
            d.bytesOutPerSecond = modExtra.perSecond[3];
            w.write(d);
            return Status::Ok;
        });

        mm.Register(Op::AudioStreamOpen, [this](CallContext& ctx, abi::Reader& r, abi::Writer& w) {
            const abi::AudioStreamRequest q = r.read<abi::AudioStreamRequest>();
            if (!r.Ok() || !Finite({q.volume, q.range}) || !std::isfinite(q.x) || !std::isfinite(q.y) ||
                !std::isfinite(q.z) || q.sampleRate < 8000 || q.sampleRate > 96000 || q.channels < 1 || q.channels > 2 ||
                static_cast<int>(q.category) < 0 || static_cast<int>(q.category) >= audio::kCategoryCount)
                return Status::BadArguments;
            std::lock_guard lock(modExtra.mutex);
            auto& mine = modExtra.streams[ctx.plugin];
            if (mine.size() >= abi::kAudioStreamMax)
                return Status::Unavailable;
            audio::StreamParams p;
            p.rate = static_cast<int>(q.sampleRate);
            p.channels = static_cast<int>(q.channels);
            p.category = static_cast<audio::Category>(q.category);
            p.volume = std::clamp(q.volume, 0.0f, 4.0f);
            p.positional = q.positional;
            p.position = glm::dvec3(q.x, q.y, q.z);
            p.range = std::clamp(q.range, 1.0f, 256.0f);
            const uint32_t id = audioEngine.StreamOpen(p);
            if (id == 0)
                return Status::Failed;
            mine.insert(id);
            w.put<uint32_t>(id);
            return Status::Ok;
        });

        mm.Register(Op::AudioStreamWrite, [this](CallContext& ctx, abi::Reader& r, abi::Writer& w) {
            const abi::AudioStreamWriteRequest q = r.read<abi::AudioStreamWriteRequest>();
            if (!r.Ok() || q.pcm.size() > (1u << 20))
                return Status::BadArguments;
            {
                std::lock_guard lock(modExtra.mutex);
                const auto it = modExtra.streams.find(ctx.plugin);
                if (it == modExtra.streams.end() || !it->second.contains(q.id))
                    return Status::NotFound;
            }
            std::vector<int16_t> samples(q.pcm.size() / 2);
            for (size_t i = 0; i < samples.size(); i++)
                samples[i] = static_cast<int16_t>(static_cast<uint16_t>(q.pcm[i * 2]) |
                                                  static_cast<uint16_t>(q.pcm[i * 2 + 1]) << 8);
            audio::StreamStatus status;
            if (!audioEngine.StreamWrite(q.id, samples.data(), samples.size(), status))
                return Status::NotFound;
            w.write(abi::AudioStreamStatus{status.bufferedMs, status.underruns});
            return Status::Ok;
        });

        mm.Register(Op::AudioStreamUpdate, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const abi::AudioStreamUpdateRequest q = r.read<abi::AudioStreamUpdateRequest>();
            if (!r.Ok() || !Finite({q.volume, q.range}) || !std::isfinite(q.x) || !std::isfinite(q.y) ||
                !std::isfinite(q.z))
                return Status::BadArguments;
            {
                std::lock_guard lock(modExtra.mutex);
                const auto it = modExtra.streams.find(ctx.plugin);
                if (it == modExtra.streams.end() || !it->second.contains(q.id))
                    return Status::NotFound;
            }
            return audioEngine.StreamUpdate(q.id, std::clamp(q.volume, 0.0f, 4.0f), q.positional,
                                            glm::dvec3(q.x, q.y, q.z), std::clamp(q.range, 1.0f, 256.0f))
                       ? Status::Ok
                       : Status::NotFound;
        });

        mm.Register(Op::AudioStreamClose, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const uint32_t id = r.get<uint32_t>();
            if (!r.Ok())
                return Status::BadArguments;
            {
                std::lock_guard lock(modExtra.mutex);
                const auto it = modExtra.streams.find(ctx.plugin);
                if (it == modExtra.streams.end() || it->second.erase(id) == 0)
                    return Status::NotFound;
            }
            audioEngine.StreamClose(id);
            return Status::Ok;
        });

        mm.Register(Op::SoundStop, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            const std::string name = r.str();
            if (!r.Ok() || name.empty() || name.size() > 128)
                return Status::BadArguments;
            sounds.Stop(name);
            return Status::Ok;
        });

        mm.Register(Op::MicList, [](CallContext&, abi::Reader&, abi::Writer& w) {
            w.write(audio::Capture::Devices());
            return Status::Ok;
        });

        mm.Register(Op::MicStart, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const abi::MicStartRequest q = r.read<abi::MicStartRequest>();
            if (!r.Ok() || q.device.size() > 256 || q.sampleRate < 8000 || q.sampleRate > 48000 || q.frameMs < 10 ||
                q.frameMs > 100)
                return Status::BadArguments;
            std::lock_guard lock(modExtra.mutex);
            if (modExtra.capture.Active() && modExtra.micOwner != ctx.plugin)
                return Status::Unavailable;
            if (!modExtra.capture.Start(q.device, static_cast<int>(q.sampleRate)))
                return Status::Failed;
            modExtra.micOwner = ctx.plugin;
            modExtra.micRate = q.sampleRate;
            modExtra.micFrame = q.sampleRate * q.frameMs / 1000;
            modExtra.micPending.clear();
            return Status::Ok;
        });

        mm.Register(Op::MicStop, [this](CallContext& ctx, abi::Reader&, abi::Writer&) {
            std::lock_guard lock(modExtra.mutex);
            if (modExtra.capture.Active() && modExtra.micOwner == ctx.plugin) {
                modExtra.capture.Stop();
                modExtra.micOwner.clear();
                modExtra.micPending.clear();
            }
            return Status::Ok;
        });

        mm.Register(Op::MicState, [this](CallContext& ctx, abi::Reader&, abi::Writer& w) {
            abi::MicStateData d;
            std::lock_guard lock(modExtra.mutex);
            if (modExtra.capture.Active() && modExtra.micOwner == ctx.plugin) {
                d.active = true;
                d.device = modExtra.capture.Device();
                d.sampleRate = modExtra.micRate;
            }
            w.write(d);
            return Status::Ok;
        });

        mm.Register(Op::PostEffect, [](CallContext&, abi::Reader& r, abi::Writer&) {
            const abi::PostEffectData d = r.read<abi::PostEffectData>();
            if (!r.Ok())
                return Status::BadArguments;
            return Status::Unavailable;
        });
        mm.MarkDeclaredOnly(Op::PostEffect);

        mm.Register(Op::ClipboardRead, [this](CallContext&, abi::Reader&, abi::Writer& w) {
            if (!platform)
                return Status::Unavailable;
            const std::string text = platform->GetClipboard();
            if (text.size() > (1u << 20))
                return Status::Failed;
            w.raw(text.data(), text.size());
            return Status::Ok;
        });

        mm.Register(Op::WindowControl, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            const abi::WindowControlData d = r.read<abi::WindowControlData>();
            if (!r.Ok() || d.title.size() > 256)
                return Status::BadArguments;
            if (!platform)
                return Status::Unavailable;
            if (d.fields.has(abi::WindowField::Fullscreen))
                options.fullscreen = d.fullscreen;
            if (d.fields.has(abi::WindowField::Title))
                platform->SetTitle(d.title);
            return Status::Ok;
        });

        mm.Register(Op::ServerConnect, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            const std::string address = r.str();
            if (!r.Ok() || address.empty() || address.size() > 260 ||
                std::any_of(address.begin(), address.end(), [](unsigned char c) { return c < 33 || c == 127; }))
                return Status::BadArguments;
            std::lock_guard lock(modExtra.mutex);
            modExtra.pendingJoin = address;
            return Status::Ok;
        });
    }

    void Perlin::ApplyModSlotOverlays(ui::HudState& state) {
        state.slotOverlays.clear();
        bool expired = false;
        {
            std::lock_guard lock(modExtra.mutex);
            const auto now = Clock::now();
            for (auto it = modExtra.overlays.begin(); it != modExtra.overlays.end();) {
                if (!it->second.forever && now >= it->second.until) {
                    it = modExtra.overlays.erase(it);
                    expired = true;
                    continue;
                }
                const abi::SlotOverlayData& d = it->second.data;
                ui::HudSlotOverlay o;
                o.slot = d.slot;
                o.text = d.text;
                o.textColor = d.textColor;
                o.corner = static_cast<int>(d.corner);
                o.background = d.background;
                o.bar = d.bar;
                o.barColor = d.barColor;
                state.slotOverlays.push_back(std::move(o));
                ++it;
            }
        }
        if (expired)
            ui::BumpHudFilter();
    }

    void Perlin::DropModExtra(const std::string& plugin) {
        std::unique_ptr<util::SocketSet> sockets;
        std::set<uint32_t> streams;
        bool overlays = false;
        {
            std::lock_guard lock(modExtra.mutex);
            if (const auto it = modExtra.sockets.find(plugin); it != modExtra.sockets.end()) {
                sockets = std::move(it->second);
                modExtra.sockets.erase(it);
            }
            if (const auto it = modExtra.streams.find(plugin); it != modExtra.streams.end()) {
                streams = std::move(it->second);
                modExtra.streams.erase(it);
            }
            overlays = std::erase_if(modExtra.overlays, [&](const auto& o) { return o.first.starts_with(plugin + "/"); }) > 0;
            if (modExtra.micOwner == plugin) {
                modExtra.capture.Stop();
                modExtra.micOwner.clear();
                modExtra.micPending.clear();
            }
        }
        sockets.reset();
        for (uint32_t id : streams)
            audioEngine.StreamClose(id);
        if (overlays)
            ui::BumpHudFilter();
    }

    void Perlin::PumpModExtra(BedrockSession* live) {
        auto& mm = mods::ModManager::Instance();
        const auto now = Clock::now();

        if (now - modExtra.sweepAt > std::chrono::seconds(1)) {
            modExtra.sweepAt = now;
            std::set<std::string> running, owners;
            for (const mods::ModInfo& info : mm.List())
                if (info.state == "running")
                    running.insert(info.name);
            {
                std::lock_guard lock(modExtra.mutex);
                for (const auto& s : modExtra.sockets)
                    owners.insert(s.first);
                for (const auto& s : modExtra.streams)
                    owners.insert(s.first);
                for (const auto& o : modExtra.overlays)
                    owners.insert(o.first.substr(0, o.first.find('/')));
                if (!modExtra.micOwner.empty())
                    owners.insert(modExtra.micOwner);
            }
            for (const std::string& owner : owners)
                if (!running.contains(owner))
                    DropModExtra(owner);
        }

        std::vector<std::pair<std::string, std::vector<util::SocketEvent>>> socketEvents;
        std::string join;
        std::vector<std::pair<std::string, abi::EventMicData>> mic;
        {
            std::lock_guard lock(modExtra.mutex);
            for (auto& [plugin, set] : modExtra.sockets) {
                std::vector<util::SocketEvent> batch;
                set->Poll(batch);
                if (!batch.empty())
                    socketEvents.emplace_back(plugin, std::move(batch));
            }
            join.swap(modExtra.pendingJoin);
            if (modExtra.capture.Active()) {
                modExtra.capture.Read(modExtra.micPending);
                const size_t frame = std::max<size_t>(modExtra.micFrame, 1);
                if (modExtra.micPending.size() > modExtra.micRate * 2u)
                    modExtra.micPending.erase(modExtra.micPending.begin(),
                                              modExtra.micPending.end() - static_cast<long>(modExtra.micRate));
                while (modExtra.micPending.size() >= frame) {
                    abi::EventMicData e;
                    e.sampleRate = modExtra.micRate;
                    double sum = 0;
                    int peak = 0;
                    e.pcm.resize(frame * 2);
                    for (size_t i = 0; i < frame; i++) {
                        const int16_t v = modExtra.micPending[i];
                        sum += static_cast<double>(v) * v;
                        peak = std::max(peak, std::abs(static_cast<int>(v)));
                        e.pcm[i * 2] = static_cast<uint8_t>(static_cast<uint16_t>(v) & 0xFF);
                        e.pcm[i * 2 + 1] = static_cast<uint8_t>(static_cast<uint16_t>(v) >> 8);
                    }
                    e.level = static_cast<float>(std::sqrt(sum / static_cast<double>(frame)) / 32768.0);
                    e.peak = static_cast<float>(peak) / 32768.0f;
                    mic.emplace_back(modExtra.micOwner, std::move(e));
                    modExtra.micPending.erase(modExtra.micPending.begin(),
                                              modExtra.micPending.begin() + static_cast<long>(frame));
                }
            }
        }

        for (auto& [plugin, batch] : socketEvents)
            for (util::SocketEvent& e : batch) {
                abi::Writer w;
                if (e.kind == util::SocketEvent::Kind::Open) {
                    w.write(abi::EventSocketOpen{e.id, e.text});
                    mm.DispatchTo(plugin, abi::Event::SocketOpen, w);
                } else if (e.kind == util::SocketEvent::Kind::Data) {
                    w.write(abi::EventSocketData{e.id, e.binary, std::move(e.data)});
                    mm.DispatchTo(plugin, abi::Event::SocketData, w);
                } else {
                    w.write(abi::EventSocketClose{e.id, e.code, e.text, e.error});
                    mm.DispatchTo(plugin, abi::Event::SocketClose, w);
                }
            }
        for (auto& [plugin, e] : mic) {
            abi::Writer w;
            w.write(e);
            mm.DispatchTo(plugin, abi::Event::MicData, w);
        }
        if (!join.empty())
            JoinServer(join);

        const bool connected = live && live->GetState() == BedrockSession::State::InGame;
        if (connected && now >= modExtra.nextPing && !modExtra.pinging.exchange(true)) {
            modExtra.nextPing = now + std::chrono::seconds(5);
            const auto [host, port] = SplitAddress(options.server);
            std::thread([this, host, port] {
                const auto start = Clock::now();
                std::string advert, error;
                if (RakNet::Ping(host, port, advert, error, 2000))
                    modExtra.pingMs.store(std::chrono::duration<float, std::milli>(Clock::now() - start).count());
                modExtra.pinging.store(false);
            }).detach();
        } else if (!connected) {
            modExtra.pingMs.store(-1.0f);
        }

        if (live && now - modExtra.rateAt >= std::chrono::seconds(1)) {
            const double seconds = std::chrono::duration<double>(now - modExtra.rateAt).count();
            const uint64_t in = live->PacketsReceived(), out = live->PacketsSent(), bin = live->BytesReceived(),
                           bout = live->BytesSent();
            if (modExtra.rateAt.time_since_epoch().count() != 0 && seconds < 5.0) {
                modExtra.perSecond[0] = static_cast<float>(static_cast<double>(in - modExtra.rateIn) / seconds);
                modExtra.perSecond[1] = static_cast<float>(static_cast<double>(out - modExtra.rateOut) / seconds);
                modExtra.perSecond[2] = static_cast<float>(static_cast<double>(bin - modExtra.rateBytesIn) / seconds);
                modExtra.perSecond[3] = static_cast<float>(static_cast<double>(bout - modExtra.rateBytesOut) / seconds);
            }
            modExtra.rateIn = in;
            modExtra.rateOut = out;
            modExtra.rateBytesIn = bin;
            modExtra.rateBytesOut = bout;
            modExtra.rateAt = now;
        }
    }
}
