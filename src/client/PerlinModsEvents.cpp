#include "client/PerlinInternal.hpp"
#include <filesystem>
#include <fstream>
#include "client/Perlin.hpp"
#include <filesystem>
#include <fstream>
#include "network/Http.hpp"

#include "client/input/KeyMapping.hpp"
#include "client/mods/ModManager.hpp"
#include "client/platform/AppPlatform.hpp"
#include "client/ui/PluginLayer.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include "util/Log.hpp"

#ifndef __EMSCRIPTEN__
#include <httplib.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#endif
#endif

namespace perlin {
    namespace {
        using Clock = std::chrono::steady_clock;

        abi::Item ToItem(const ui::HudItem& s) {
            abi::Item i;
            if (s.Empty())
                return i;
            i.name = s.name;
            i.count = s.count;
            i.aux = s.aux;
            i.damage = s.maxDamage > 0 ? s.aux : 0;
            i.enchanted = s.enchanted;
            i.customName = s.customName;
            for (const auto& e : s.enchants)
                i.enchants.emplace_back(e.first, e.second);
            i.lore = s.lore;
            return i;
        }

        abi::Entity ToEntity(const entity::Entity& e) {
            abi::Entity out;
            out.runtimeId = e.runtimeId;
            out.uniqueId = e.uniqueId;
            out.type = e.type;
            out.name = e.name;
            out.nameTag = e.nameTag;
            out.uuid = e.uuid;
            out.x = e.pos.x;
            out.y = e.pos.y;
            out.z = e.pos.z;
            out.yaw = e.yaw;
            out.pitch = e.pitch;
            out.headYaw = e.headYaw;
            out.health = e.health;
            out.scale = e.scale;
            out.onGround = e.onGround;
            out.isPlayer = e.isPlayer;
            out.isItem = e.isItem;
            out.isSelf = e.isSelf;
            out.flags = abi::EntityFlags::from_bits(e.flags);
            out.itemName = e.itemName;
            out.itemCount = e.itemCount;
            out.mainHand = e.mainHand;
            out.offHand = e.offHand;
            for (int i = 0; i < 4; i++)
                out.armor[static_cast<size_t>(i)] = e.armor[i];
            out.vehicleUid = e.vehicleUid;
            out.ownerUid = e.ownerUid;
            out.targetUid = e.targetUid;
            return out;
        }

#ifndef __EMSCRIPTEN__
        bool PrivateAddress(const sockaddr* sa) {
            if (sa->sa_family == AF_INET) {
                const auto* v4 = reinterpret_cast<const sockaddr_in*>(sa);
                const uint32_t a = ntohl(v4->sin_addr.s_addr);
                return (a >> 24) == 127 || (a >> 24) == 10 || (a >> 24) == 0 || (a >> 16) == 0xC0A8 ||
                       (a >> 20) == 0xAC1 || (a >> 16) == 0xA9FE || (a >> 22) == 0x191;
            }
            if (sa->sa_family == AF_INET6) {
                const auto* v6 = reinterpret_cast<const sockaddr_in6*>(sa);
                const uint8_t* b = reinterpret_cast<const uint8_t*>(&v6->sin6_addr);
                bool loopback = true;
                for (int i = 0; i < 15; i++)
                    loopback = loopback && b[i] == 0;
                if (loopback && (b[15] == 1 || b[15] == 0))
                    return true;
                if ((b[0] & 0xFE) == 0xFC || (b[0] == 0xFE && (b[1] & 0xC0) == 0x80))
                    return true;
                static const uint8_t mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
                if (std::memcmp(b, mapped, 12) == 0) {
                    const uint32_t a = (static_cast<uint32_t>(b[12]) << 24) |
                                       (static_cast<uint32_t>(b[13]) << 16) |
                                       (static_cast<uint32_t>(b[14]) << 8) | b[15];
                    return (a >> 24) == 127 || (a >> 24) == 10 || (a >> 16) == 0xC0A8 || (a >> 20) == 0xAC1 ||
                           (a >> 16) == 0xA9FE;
                }
            }
            return false;
        }

        bool ResolveSafe(const std::string& host, std::string& address, std::string& error) {
#ifdef _WIN32
            static const bool started = [] {
                WSADATA data;
                return WSAStartup(MAKEWORD(2, 2), &data) == 0;
            }();
            (void)started;
#endif
            addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            addrinfo* list = nullptr;
            if (getaddrinfo(host.c_str(), nullptr, &hints, &list) != 0 || !list) {
                error = "could not resolve host";
                return false;
            }
            bool ok = true;
            for (addrinfo* p = list; p; p = p->ai_next)
                if (PrivateAddress(p->ai_addr))
                    ok = false;
            if (ok) {
                char buf[64] = {};
                const addrinfo* p = list;
                if (p->ai_family == AF_INET)
                    inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in*>(p->ai_addr)->sin_addr, buf,
                              sizeof(buf));
                else
                    inet_ntop(AF_INET6, &reinterpret_cast<const sockaddr_in6*>(p->ai_addr)->sin6_addr, buf,
                              sizeof(buf));
                address = buf;
            } else {
                error = "requests to local or private network addresses are blocked";
            }
            freeaddrinfo(list);
            return ok;
        }
#endif
    }

    void Perlin::StartModHttp(const std::string& plugin, uint32_t id, std::string method, std::string url,
                              int timeoutMs, std::vector<std::pair<std::string, std::string>> headers,
                              std::vector<uint8_t> body, std::string savePath) {
        modHttpActive++;
        std::thread([this, plugin, id, method = std::move(method), url = std::move(url), timeoutMs,
                     headers = std::move(headers), body = std::move(body), savePath = std::move(savePath)]() {
            abi::EventHttp ev;
            ev.id = id;
#ifdef __EMSCRIPTEN__
            // the browser's fetch: same https-only rule and size limits; the browser applies its own CORS and
            // private-network checks in place of the address filter below
            do {
                if (!url.starts_with("https://")) {
                    ev.error = "only https:// URLs are allowed";
                    break;
                }
                std::map<std::string, std::string> extra;
                std::string contentType;
                for (const auto& [k, v] : headers) {
                    std::string lower = k;
                    std::transform(lower.begin(), lower.end(), lower.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (lower == "content-type")
                        contentType = v;
                    else if (lower != "host" && lower != "content-length" && lower != "transfer-encoding" &&
                             lower != "connection")
                        extra[k] = v;
                }
                const perlin::http::Response res =
                    perlin::http::Request({}, 0, true, method, url, std::string(body.begin(), body.end()),
                                          contentType, extra, std::max(1, timeoutMs / 1000));
                if (res.status == 0) {
                    ev.error = "the request failed (offline, or the site does not allow browser requests)";
                    break;
                }
                if (res.body.size() > (savePath.empty() ? (4u << 20) : (64u << 20))) {
                    ev.error =
                        savePath.empty() ? "response larger than 4 MiB" : "download larger than 64 MiB";
                    break;
                }
                ev.status = res.status;
                for (const auto& [k, v] : res.headers)
                    ev.headers.emplace_back(k, v);
                ev.body.assign(res.body.begin(), res.body.end());
                if (!savePath.empty() && ev.status >= 200 && ev.status < 300) {
                    std::error_code ec;
                    std::filesystem::create_directories(std::filesystem::path(savePath).parent_path(), ec);
                    std::ofstream out(savePath, std::ios::binary | std::ios::trunc);
                    out.write(reinterpret_cast<const char*>(ev.body.data()),
                              static_cast<std::streamsize>(ev.body.size()));
                    if (!out)
                        ev.error = "could not write the file";
                    ev.body.clear();
                }
            } while (false);
#else
            do {
                static const std::string scheme = "https://";
                if (url.compare(0, scheme.size(), scheme) != 0) {
                    ev.error = "only https:// URLs are allowed";
                    break;
                }
                const size_t hostStart = scheme.size();
                size_t pathStart = url.find_first_of("/?#", hostStart);
                std::string authority = url.substr(
                    hostStart, pathStart == std::string::npos ? std::string::npos : pathStart - hostStart);
                std::string path = pathStart == std::string::npos ? "/" : url.substr(pathStart);
                if (path[0] != '/')
                    path = "/" + path;
                if (authority.contains('@') || authority.empty()) {
                    ev.error = "invalid URL";
                    break;
                }
                int port = 443;
                std::string host = authority;
                if (const size_t colon = authority.rfind(':');
                    colon != std::string::npos && !authority.contains(']')) {
                    host = authority.substr(0, colon);
                    port = std::atoi(authority.c_str() + colon + 1);
                    if (port <= 0 || port > 65535) {
                        ev.error = "invalid port";
                        break;
                    }
                }
                std::string address, error;
                if (!ResolveSafe(host, address, error)) {
                    ev.error = error;
                    break;
                }
                httplib::SSLClient cli(host, port);
                perlin::http::UseSystemCa(cli);
                cli.set_hostname_addr_map({{host, address}});
                cli.set_connection_timeout(0, timeoutMs * 1000);
                cli.set_read_timeout(timeoutMs / 1000, (timeoutMs % 1000) * 1000);
                cli.set_write_timeout(timeoutMs / 1000, (timeoutMs % 1000) * 1000);
                cli.set_follow_location(false);
                cli.enable_server_certificate_verification(true);
                httplib::Request req;
                req.method = method;
                req.path = path;
                for (const auto& h : headers) {
                    std::string lower = h.first;
                    std::transform(lower.begin(), lower.end(), lower.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (lower == "host" || lower == "content-length" || lower == "transfer-encoding" ||
                        lower == "connection")
                        continue;
                    req.headers.emplace(h.first, h.second);
                }
                if (!body.empty())
                    req.body.assign(body.begin(), body.end());
                bool tooLarge = false;
                req.content_receiver = [&](const char* data, size_t len, uint64_t, uint64_t) {
                    if (ev.body.size() + len > (savePath.empty() ? (4u << 20) : (64u << 20))) {
                        tooLarge = true;
                        return false;
                    }
                    ev.body.insert(ev.body.end(), data, data + len);
                    return true;
                };
                httplib::Response res;
                httplib::Error err = httplib::Error::Success;
                const bool ok = cli.send(req, res, err);
                if (tooLarge) {
                    ev.error =
                        savePath.empty() ? "response larger than 4 MiB" : "download larger than 64 MiB";
                    ev.body.clear();
                    break;
                }
                if (!ok) {
                    ev.error = httplib::to_string(err);
                    break;
                }
                ev.status = res.status;
                for (const auto& h : res.headers) {
                    std::string key = h.first;
                    std::transform(key.begin(), key.end(), key.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    ev.headers.emplace_back(std::move(key), h.second);
                }
                if (!savePath.empty() && ev.status >= 200 && ev.status < 300) {
                    std::error_code ec;
                    std::filesystem::create_directories(std::filesystem::path(savePath).parent_path(), ec);
                    std::ofstream out(savePath, std::ios::binary | std::ios::trunc);
                    out.write(reinterpret_cast<const char*>(ev.body.data()),
                              static_cast<std::streamsize>(ev.body.size()));
                    if (!out)
                        ev.error = "could not write the file";
                    ev.body.clear();
                }
            } while (false);
#endif
            {
                std::lock_guard lock(modMutex);
                modHttp.emplace_back(plugin, std::move(ev));
            }
            modHttpActive--;
        }).detach();
    }

    void Perlin::NoteModSound(const std::string& name, double x, double y, double z, float volume,
                              float pitch) {
        if (!mods::ModManager::Instance().WantsEvent(abi::Event::Sound))
            return;
        abi::EventSound e;
        e.name = name;
        e.x = x;
        e.y = y;
        e.z = z;
        e.volume = volume;
        e.pitch = pitch;
        if (modSounds.size() < 256)
            modSounds.push_back(std::move(e));
    }

    bool Perlin::FilterIncomingChat(const std::string& line) {
        auto& mm = mods::ModManager::Instance();
        if (!mm.WantsEvent(abi::Event::ChatReceived))
            return false;
        abi::EventChat e;
        e.type = abi::ChatType::Raw;
        e.message = line;
        e.raw = line;
        abi::Writer w;
        w.write(e);
        return mm.Dispatch(abi::Event::ChatReceived, w);
    }

    bool Perlin::InterceptOutgoing(BedrockSession*, const std::string& text) {
        auto& mm = mods::ModManager::Instance();
        if (text.empty() || !mm.HasPlugins())
            return false;
        if (text[0] == '/') {
            const size_t space = text.find(' ');
            const std::string name =
                text.substr(1, space == std::string::npos ? std::string::npos : space - 1);
            const std::string args = space == std::string::npos ? std::string() : text.substr(space + 1);
            for (const mods::PluginCommand& cmd : mm.Commands())
                if (cmd.name == name) {
                    abi::EventCommand e;
                    e.name = name;
                    e.args = args;
                    abi::Writer w;
                    w.write(e);
                    mm.DispatchTo(cmd.plugin, abi::Event::Command, w);
                    return true;
                }
        }
        if (!mm.WantsEvent(abi::Event::ChatSending))
            return false;
        abi::EventText e;
        e.text = text;
        abi::Writer w;
        w.write(e);
        return mm.Dispatch(abi::Event::ChatSending, w);
    }

    void Perlin::MergePluginCommands(BedrockSession* live) {
        auto& mm = mods::ModManager::Instance();
        if (!live && modCommandsVersion == 0 && mm.CommandVersion() == 0)
            return;
        const uint32_t serverVersion = live ? live->CommandsVersion() : 0;
        const uint64_t pluginVersion = mm.CommandVersion();
        const std::string signature =
            std::to_string(serverVersion) + ":" + std::to_string(pluginVersion) + ":" + (live ? "1" : "0");
        if (signature == modCommandSig)
            return;
        modCommandSig = signature;
        modCommandsVersion = serverVersion;
        std::vector<ui::CommandInfo> list;
        if (live)
            list = live->Commands();
        for (const mods::PluginCommand& cmd : mm.Commands()) {
            ui::CommandInfo info;
            info.name = cmd.name;
            info.description = cmd.description.empty() ? cmd.usage : cmd.description;
            info.overloads.emplace_back();
            list.push_back(std::move(info));
        }
        chat.SetCommands(std::move(list));
    }

    void Perlin::ApplyModHud(ui::HudState& s) {
        const auto now = Clock::now();
        if (s.actionBarAlpha <= 0.0f && now < modToastUntil) {
            const float remaining = std::chrono::duration<float>(modToastUntil - now).count();
            s.actionBar = modToastText;
            s.actionBarAlpha = std::clamp(remaining * 2.0f, 0.0f, 1.0f);
        }
        if (s.titleAlpha <= 0.0f && !modTitleText.empty()) {
            const int elapsed = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(now - modTitleStart).count());
            const int total = modTitleFadeIn + modTitleStay + modTitleFadeOut;
            if (elapsed >= total) {
                modTitleText.clear();
                modSubtitleText.clear();
            } else {
                float alpha = 1.0f;
                if (elapsed < modTitleFadeIn)
                    alpha = static_cast<float>(elapsed) / static_cast<float>(std::max(1, modTitleFadeIn));
                else if (elapsed > modTitleFadeIn + modTitleStay)
                    alpha = static_cast<float>(total - elapsed) /
                            static_cast<float>(std::max(1, modTitleFadeOut));
                s.title = modTitleText;
                s.subtitle = modSubtitleText;
                s.titleAlpha = std::clamp(alpha, 0.0f, 1.0f);
            }
        }
    }

    void Perlin::UpdateModLayer(ui::Hud& hud) {
        auto& mm = mods::ModManager::Instance();
        // hud layers show in game; "screen:..." layers and ScreenButtonAdd buttons show over open screens
        const bool onScreen = CurrentScreen() != abi::Screen::None;
        const bool want = options.showHud && mm.HasPlugins() && !(replayCtl.Playing() && replayUi.hideHud);
        if (!want) {
            if (modLayerShown) {
                hud.Overlay().ClearPluginLayer();
                modLayerShown = false;
                modLayerVersion = 0;
            }
            return;
        }
        std::string buttonKey;
        for (const ScreenButtonView& b : modScreenButtons)
            buttonKey += b.def.id + (b.hover ? "+" : "-") + b.def.label + "|";
        const int w = static_cast<int>(hud.GuiWidth()), h = static_cast<int>(hud.GuiHeight());
        const uint64_t version = mm.LayerVersion();
        if (modLayerShown && version == modLayerVersion && w == modLayerW && h == modLayerH &&
            onScreen == modLayerOnScreen && buttonKey == modLayerButtons)
            return;
        modLayerVersion = version;
        modLayerW = w;
        modLayerH = h;
        modLayerOnScreen = onScreen;
        modLayerButtons = buttonKey;
        std::vector<mods::UiLayer> layers;
        for (mods::UiLayer& l : mm.Layers())
            if (l.name.starts_with("screen:") ? onScreen : !onScreen)
                layers.push_back(std::move(l));
        if (onScreen && !modScreenButtons.empty()) {
            mods::UiLayer buttons;
            buttons.plugin = "perlin";
            buttons.name = "screen:buttons";
            buttons.z = 1000;
            abi::Writer c;
            for (const ScreenButtonView& b : modScreenButtons) {
                const float x = static_cast<float>(b.def.x), y = static_cast<float>(b.def.y);
                const float bw = static_cast<float>(b.def.width), bh = static_cast<float>(b.def.height);
                c.put<uint8_t>(static_cast<uint8_t>(abi::Draw::Rect))
                    .put(x)
                    .put(y)
                    .put(bw)
                    .put(bh)
                    .put<uint32_t>(b.hover ? 0x505050E0u : 0x303030D0u);
                c.put<uint8_t>(static_cast<uint8_t>(abi::Draw::Border))
                    .put(x)
                    .put(y)
                    .put(bw)
                    .put(bh)
                    .put<uint32_t>(b.hover ? 0xFFFFFFFFu : 0x000000FFu)
                    .put(1.0f);
                c.put<uint8_t>(static_cast<uint8_t>(abi::Draw::Text))
                    .put(x + bw * 0.5f)
                    .put(y + (bh - 8.0f) * 0.5f)
                    .put<uint32_t>(b.hover ? 0xFFFFA0FFu : 0xE0E0E0FFu)
                    .put(1.0f)
                    .put<uint8_t>(1)
                    .put<uint8_t>(1)
                    .str(b.def.label);
            }
            buttons.commands.assign(c.Data(), c.Data() + c.Size());
            layers.push_back(std::move(buttons));
        }
        if (layers.empty()) {
            if (modLayerShown)
                hud.Overlay().ClearPluginLayer();
            modLayerShown = false;
            return;
        }
        std::string error;
        modLayerShown = hud.Overlay().SetPluginLayer(ui::BuildPluginLayerRml(layers, w, h), error);
        if (!modLayerShown)
            Log("mods", "{}", error);
    }

    void Perlin::TickMods(BedrockSession*) {
        auto& mm = mods::ModManager::Instance();
        if (!mm.WantsEvent(abi::Event::Tick))
            return;
        abi::EventTick t;
        t.dt = 0.05f;
        t.tick = hudAnim.ticks;
        abi::Writer w;
        w.write(t);
        mm.Dispatch(abi::Event::Tick, w);
    }

    void Perlin::PumpMods(BedrockSession* live, Level* level, ui::Hud* hudPtr, float dt) {
        auto& mm = mods::ModManager::Instance();
        mm.SetAutoReload(options.hotReload);
        mm.SetTick(hudAnim.ticks);
        mm.Update();
        mm.Pump();
        modLive = live;
        modLevel = level;
        modHud = hudPtr;
        modDt = dt;
        inputManager.SetPluginGrab(mm.GrabsMouse(), mm.GrabsKeyboard());
        PumpModInput(mm);
        if (hudPtr) {
            modGuiW = static_cast<int>(hudPtr->GuiWidth());
            modGuiH = static_cast<int>(hudPtr->GuiHeight());
        }
        if (!mm.HasPlugins())
            return;

        const bool connected = live && live->GetState() == BedrockSession::State::InGame;
        if (connected != modWasConnected) {
            modWasConnected = connected;
            abi::EventConnection e;
            e.server = options.server;
            if (!connected && session) {
                const auto info = session->GetDisconnectInfo();
                e.message = info.message;
                e.reason = static_cast<abi::DisconnectReason>(info.reason);
            }
            abi::Writer w;
            w.write(e);
            mm.Dispatch(connected ? abi::Event::Connected : abi::Event::Disconnected, w);
            modEntitySeeded = false;
        }

        std::vector<std::pair<std::string, abi::EventHttp>> responses;
        {
            std::lock_guard lock(modMutex);
            responses.swap(modHttp);
        }
        for (auto& r : responses) {
            abi::Writer w;
            w.write(r.second);
            mm.DispatchTo(r.first, abi::Event::HttpResponse, w);
        }

        const std::vector<mods::PluginKey> keys = mm.Keys();
        if (!keys.empty()) {
            const bool free = input.Captured() && !input.TextMode() && !pause.shown && !chat.IsOpen() &&
                              !inventory.shown && !death.shown;
            const bool inScreen = !input.TextMode() && CurrentScreen() != abi::Screen::None;
            for (const mods::PluginKey& key : keys) {
                const Binding b = Binding::Parse(key.binding);
                const bool active = key.context == abi::KeyContext::Any        ? (free || inScreen)
                                    : key.context == abi::KeyContext::InScreen ? inScreen
                                                                               : free;
                const bool mods = (!key.ctrl || input.IsKeyDown(Key::Control)) &&
                                  (!key.shift || input.IsKeyDown(Key::Shift)) &&
                                  (!key.alt || input.IsKeyDown(Key::Alt));
                bool down = false;
                if (active && mods && b.device == Binding::Device::Keyboard)
                    down = input.IsKeyDown(b.code);
                else if (active && mods && b.device == Binding::Device::Mouse)
                    down = input.IsMouseDown(b.code);
                bool& was = modKeyDown[key.plugin + "/" + key.id];
                if (down != was) {
                    was = down;
                    abi::EventKey e;
                    e.id = key.id;
                    e.down = down;
                    abi::Writer w;
                    w.write(e);
                    mm.DispatchTo(key.plugin, abi::Event::Key, w);
                }
            }
        }

        const abi::Screen screen = CurrentScreen();
        if (screen != modLastScreen) {
            abi::EventScreen e;
            e.from = static_cast<abi::Screen>(modLastScreen);
            e.to = static_cast<abi::Screen>(screen);
            modLastScreen = screen;
            abi::Writer w;
            w.write(e);
            mm.Dispatch(abi::Event::ScreenChanged, w);
        }

        if (hudState.selectedSlot != modLastSlot) {
            if (modLastSlot >= 0 && mm.WantsEvent(abi::Event::SlotChanged)) {
                abi::EventSlot e;
                e.slot = hudState.selectedSlot;
                e.item = ToItem(hudState.hotbar[std::clamp(hudState.selectedSlot, 0, 8)]);
                abi::Writer w;
                w.write(e);
                mm.Dispatch(abi::Event::SlotChanged, w);
            }
            modLastSlot = hudState.selectedSlot;
        }

        if (live && connected) {
            const int dim = live->DimensionId();
            if (dim != modLastDim) {
                abi::EventDimension e;
                e.from = static_cast<abi::Dimension>(modLastDim);
                e.to = static_cast<abi::Dimension>(dim);
                modLastDim = dim;
                abi::Writer w;
                w.write(e);
                mm.Dispatch(abi::Event::DimensionChanged, w);
            }
            const uint32_t hurts = live->HurtCount();
            if (hurts != modLastHurt) {
                modLastHurt = hurts;
                const auto source = live->LatestHurtSource();
                abi::EventDamage e;
                e.known = source.known;
                e.dx = source.dx;
                e.dz = source.dz;
                e.health = hudState.health;
                abi::Writer w;
                w.write(e);
                mm.Dispatch(abi::Event::Damage, w);
            }
            const bool dead = live->DeathPending();
            if (dead != modWasDead) {
                modWasDead = dead;
                abi::Writer w;
                if (dead) {
                    abi::EventText e;
                    e.text = live->DeathCause();
                    w.write(e);
                    mm.Dispatch(abi::Event::Death, w);
                } else {
                    mm.Dispatch(abi::Event::Respawned, w);
                }
            }
            const auto title = live->GetTitleText();
            if (title.serial != modTitleSerial) {
                modTitleSerial = title.serial;
                if (mm.WantsEvent(abi::Event::Title)) {
                    abi::EventTitle e;
                    e.title = title.title;
                    e.subtitle = title.subtitle;
                    e.actionBar = title.actionBar;
                    abi::Writer w;
                    w.write(e);
                    mm.Dispatch(abi::Event::Title, w);
                }
            }
        }

        if (mm.WantsEvent(abi::Event::EntityAdded) || mm.WantsEvent(abi::Event::EntityRemoved)) {
            const std::vector<entity::Entity> list =
                live ? live->Entities().Snapshot() : previewEntities.Snapshot();
            std::unordered_set<uint64_t> current;
            current.reserve(list.size());
            for (const entity::Entity& e : list)
                current.insert(e.runtimeId);
            if (modEntitySeeded) {
                for (const entity::Entity& e : list)
                    if (!modEntityIds.contains(e.runtimeId)) {
                        abi::EventEntity ev;
                        ev.entity = ToEntity(e);
                        abi::Writer w;
                        w.write(ev);
                        mm.Dispatch(abi::Event::EntityAdded, w);
                    }
                for (const uint64_t id : modEntityIds)
                    if (!current.contains(id)) {
                        abi::EventEntity ev;
                        ev.entity.runtimeId = id;
                        abi::Writer w;
                        w.write(ev);
                        mm.Dispatch(abi::Event::EntityRemoved, w);
                    }
            }
            modEntityIds = std::move(current);
            modEntitySeeded = true;
        } else {
            modEntitySeeded = false;
        }

        if (!modSounds.empty()) {
            std::vector<abi::EventSound> list;
            list.swap(modSounds);
            for (const abi::EventSound& s : list) {
                abi::Writer w;
                w.write(s);
                mm.Dispatch(abi::Event::Sound, w);
            }
        }

        modWantPackets = mm.WantsEvent(abi::Event::Packet);
        {
            std::vector<std::pair<uint32_t, std::vector<uint8_t>>> list;
            {
                std::lock_guard lock(modMutex);
                list.swap(modPackets);
            }
            for (auto& p : list) {
                abi::EventPacket e;
                e.id = p.first;
                e.outgoing = false;
                e.body = std::move(p.second);
                abi::Writer w;
                w.write(e);
                mm.Dispatch(abi::Event::Packet, w);
            }
        }

        PumpModHooks(live);

        if (mm.WantsEvent(abi::Event::Frame)) {
            abi::EventFrame f;
            f.dt = dt;
            f.fps = static_cast<float>(debug.fps);
            f.guiWidth = modGuiW;
            f.guiHeight = modGuiH;
            abi::Writer w;
            w.write(f);
            mm.Dispatch(abi::Event::Frame, w);
        }
    }
}

namespace perlin {
    void Perlin::PumpModInput(mods::ModManager& mm) {
        if (inputManager.TakeGrabFailsafe() && (mm.GrabsMouse() || mm.GrabsKeyboard())) {
            mm.ReleaseGrabs();
            inputManager.SetPluginGrab(false, false);
            chat.AddMessage("\xC2\xA7"
                            "eA plugin was holding the mouse and keyboard; Escape x3 gave them back.",
                            hudAnim.ticks);
        }
        const bool gm = mm.GrabsMouse(), gk = mm.GrabsKeyboard();
        if (renderer) {
            int w = 0, h = 0;
            renderer->FramebufferSize(w, h);
            modFbW = w;
            modFbH = h;
        }
        if (!modNativeReady && renderer) {
            NativeHandleInfo n;
            if (renderer->QueryNativeHandles(n)) {
                std::lock_guard lock(modNativeMutex);
                modNative = {n.api, n.adapter, n.window, n.display, n.device};
                modNativeReady = true;
            }
        }
        if (gm != modGrabMouseWas) {
            modGrabMouseWas = gm;
            if (gm) {
                input.SetFreeCursor(true);
                if (input.Captured())
                    platform->SetCursorCaptured(false);
            } else if (!replayCtl.Playing()) {
                input.SetFreeCursor(false);
            }
        }
        if (gk != modGrabKeyboardWas) {
            modGrabKeyboardWas = gk;
            if (gk)
                input.SetTextMode(true);
            else if (!pause.shown && !chat.IsOpen() && !exportUi.open && !kfUi.open)
                input.SetTextMode(false);
        }
        std::vector<InputManager::PluginInput> events = inputManager.TakePluginInput();
        if (events.empty() || (!gm && !gk))
            return;
        const std::vector<std::string> targets = mm.GrabbingPlugins();
        if (targets.empty())
            return;
        for (const InputManager::PluginInput& e : events) {
            abi::Writer w;
            abi::Event type = abi::Event::MouseMove;
            switch (e.type) {
            case InputManager::PluginInput::Type::Move: {
                abi::EventMouseMove m;
                m.x = e.x;
                m.y = e.y;
                w.write(m);
                type = abi::Event::MouseMove;
                break;
            }
            case InputManager::PluginInput::Type::Button: {
                abi::EventMouseButton m;
                m.button = e.code;
                m.down = e.down;
                m.x = e.x;
                m.y = e.y;
                w.write(m);
                type = abi::Event::MouseButton;
                break;
            }
            case InputManager::PluginInput::Type::Wheel: {
                abi::EventMouseWheel m;
                m.dx = e.x;
                m.dy = e.y;
                w.write(m);
                type = abi::Event::MouseWheel;
                break;
            }
            case InputManager::PluginInput::Type::Key: {
                abi::EventKeyRaw m;
                m.key = e.code;
                m.down = e.down;
                m.repeat = e.repeat;
                m.ctrl = e.ctrl;
                m.shift = e.shift;
                m.alt = e.alt;
                w.write(m);
                type = abi::Event::KeyRaw;
                break;
            }
            case InputManager::PluginInput::Type::Text: {
                abi::EventText m;
                m.text = e.text;
                w.write(m);
                type = abi::Event::TextInput;
                break;
            }
            }
            for (const std::string& plugin : targets)
                mm.DispatchTo(plugin, type, w);
        }
    }
}
