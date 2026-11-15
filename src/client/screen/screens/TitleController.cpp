#include "client/screen/screens/TitleController.hpp"

#include "client/screen/screens/ScreenSupport.hpp"
#include "client/screen/screens/TitleSupport.hpp"
#include "client/systems/bindings/Keys.hpp"
#include "client/ui/GuiViewport.hpp"
#include "client/ui/TextEdit.hpp"
#include "util/Env.hpp"
#include "util/Log.hpp"
#include "util/Version.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace perlin::screen {
    TitleController::TitleController(Deps d)
        : titleScreen(d.titleScreen), title(d.title), options(d.options), input(d.input),
          inputManager(d.inputManager), panorama(d.panorama), screens(d.screens), chat(d.chat),
          replayLibrary(d.replayLibrary), uiAssets(d.uiAssets), jsonScreens(d.jsonScreens), ports(d.ports) {}

    bool TitleController::Update(ui::Hud& hud, int fbW, int fbH) {
        const auto now = Clock::now();
        if (!titleScreen.active || titleScreen.options) {
            HideOverlay(hud);
            if (titleScreen.active)
                panorama.Draw(fbW, fbH, true);
            else
                titleScreen.leftDown = false;
            return false;
        }
        if (ports.UpdateAssetScreen(hud, fbW, fbH))
            return true;
        if (!titleScreen.IsOpen())
            Show(now);

        const auto [scale, guiW, guiH] = ui::OverlayViewport(hud, fbW, fbH);
        HandleTitleTyping();
        if (!titleScreen.active)
            return false;
        ScrollTitleLists(guiH);

        ports.ReleaseIdleServerPacks();
        panorama.Draw(fbW, fbH,
                          !(title.page == ui::TitlePage::Main || title.page == ui::TitlePage::Accounts ||
                            title.page == ui::TitlePage::AccountName ||
                            title.page == ui::TitlePage::AccountSignIn));

        SyncTitleAccounts();
        SyncTitleServers();
        LoadTitleLogo(now);
        RefreshTitleOverlay(hud, guiW, guiH);

        const glm::vec2 pointer = screen::PointerOrPreview(input);
        hud.Overlay().MouseMove(pointer.x / static_cast<float>(scale), pointer.y / static_cast<float>(scale));
        hud.UpdateOverlay(fbW, fbH, options.guiScale);

        const bool down = input.IsMouseDown(0);
        if (!down && titleScreen.leftDown && now - titleScreen.openedAt > std::chrono::milliseconds(150))
            HandleTitleClick(hud.Overlay().OverlayHover(), now);
        titleScreen.leftDown = down;
        return titleScreen.active || titleScreen.IsOpen();
    }

    void TitleController::HideOverlay(ui::Hud& hud) {
        if (!titleScreen.IsOpen())
            return;
        hud.Overlay().ClearOverlay();
        screens.Close(titleScreen);
        titleScreen.signature.clear();
        if (!titleScreen.active && !chat.IsOpen())
            input.SetTextMode(false);
    }

    void TitleController::Show(TimePoint now) {
        screens.Open(titleScreen);
        titleScreen.openedAt = now;
        titleScreen.leftDown = true;
        titleScreen.signature.clear();
        title.page = ui::TitlePage::Main;
        title.selected = -1;
        title.scroll = 0;
        title::LoadServers(title.servers, options.server);
        title.version = std::string("Perlin ") + kVersion + " (" + ports.BuildLabel() + ")";
        title.copyright = "Not affiliated with Mojang AB.";
        if (titleScreen.returnToServers) {
            titleScreen.returnToServers = false;
            title.page = ui::TitlePage::Servers;
            title.tab = ui::ServerTab::Saved;
            for (size_t i = 0; i < title.servers.size(); i++)
                if (title.servers[i].address == options.server)
                    title.selected = static_cast<int>(i) + title.savedOffset;
        }
        if (titleScreen.returnToReplays) {
            titleScreen.returnToReplays = false;
            title.page = ui::TitlePage::Replays;
            title.replaySelected = -1;
            title.replayScroll = 0;
            replayLibrary.Refresh();
        }
        if (const char* preview = PERLIN_ENV("PERLIN_PREVIEW_TITLE"))
            ApplyTitlePreview(preview);
        input.SetTextMode(true);
    }

    void TitleController::ApplyTitlePreview(const std::string& page) {
        if (page == "servers" || page == "edit" || page == "direct") {
            if (title.servers.empty())
                title.servers = {{.name = "Fozzy Minecraft Server", .address = "23.109.60.29:25565"},
                                 {.name = "Minecraft Server", .address = "play.example.net:19132"}};
            title.page = page == "servers" ? ui::TitlePage::Servers
                         : page == "edit"  ? ui::TitlePage::Edit
                                           : ui::TitlePage::Direct;
            title.selected = 0;
            title.name = title.servers[0].name;
            title.address = title.servers[0].address;
            title.focus = 1;
        } else if (page == "replays") {
            title.page = ui::TitlePage::Replays;
            title.replaySelected = -1;
            title.replayScroll = 0;
            replayLibrary.Refresh();
        } else if (page == "accounts") {
            if (ports.AccountsEmpty())
                ports.AddPreviewAccounts();
            title.page = ui::TitlePage::Accounts;
        } else if (page == "mods" || page == "wardrobe") {
            titleScreen.openView = page == "mods" ? 4 : 5;
            titleScreen.options = true;
        }
    }

    void TitleController::TitleCommit() {
        if (title.address.empty())
            return;
        if (title.page == ui::TitlePage::Direct) {
            ports.JoinServer(title.address);
            return;
        }
        if (title.page == ui::TitlePage::AddRealm) {
            ports.AddRealm(title.address);
            return;
        }
        const ui::ServerEntry entry{.name = title.name.empty() ? "Minecraft Server" : title.name, .address = title.address};
        if (title.adding || title.SavedIndex() < 0) {
            title.servers.push_back(entry);
            title.selected = static_cast<int>(title.servers.size()) - 1 + title.savedOffset;
        } else {
            title.servers[static_cast<size_t>(title.SavedIndex())] = entry;
        }
        title::SaveServers(title.servers);
        title.page = ui::TitlePage::Servers;
        title.tab = ui::ServerTab::Saved;
        ports.ForgetPing(entry.address);
        ports.Ping({entry.address});
    }

    void TitleController::TitleJoinRow(int row) {
        const std::vector<ui::ServerEntry>& list = title.Shown();
        if (row < 0 || row >= static_cast<int>(list.size()))
            return;
        const ui::ServerEntry& e = list[static_cast<size_t>(row)];
        switch (e.kind) {
        case ui::ServerEntry::Kind::Header:
            break;
        case ui::ServerEntry::Kind::Realm:
            ports.JoinRealm(e.realmId);
            break;
        case ui::ServerEntry::Kind::Friend:
            ports.JoinFriend(e);
            break;
        case ui::ServerEntry::Kind::Lan:
            if (e.xuid.starts_with("lan:"))
                ports.JoinLanGame(e);
            else
                ports.JoinServer(e.address);
            break;
        default:
            ports.JoinServer(e.address);
            break;
        }
    }

    void TitleController::TitleBack() {
        if (title.page == ui::TitlePage::AccountSignIn)
            ports.CancelSignIn();
        title.page = title::ParentPage(title.page);
    }

    void TitleController::TitleAddOffline() {
        std::string name = title.accountName;
        while (!name.empty() && name.back() == ' ')
            name.pop_back();
        if (name.empty())
            return;
        ports.AddOfflineAccount(name);
        ports.ApplyActiveAccount();
        title.page = ui::TitlePage::Accounts;
    }

    void TitleController::TitleEditServer(bool adding) {
        title.adding = adding;
        title.page = ui::TitlePage::Edit;
        title.focus = 0;
        if (adding) {
            title.name = "Minecraft Server";
            title.address.clear();
            return;
        }
        const ui::ServerEntry& s = title.servers[static_cast<size_t>(std::max(0, title.SavedIndex()))];
        title.name = s.name;
        title.address = s.address;
    }

    void TitleController::HandleTitleTyping() {
        for (const auto& t : inputManager.TakeTyped()) {
            if (!titleScreen.active)
                break;
            const bool special = !t.character;
            if (special && t.key == Key::Escape) {
                TitleBack();
            } else if (title::IsReplayPage(title.page)) {
                replayLibrary.Typed(t);
            } else if (title.page == ui::TitlePage::AccountName) {
                ui::TextLimits limits{16};
                limits.accept = [](uint32_t cp) { return cp != ' '; };
                if (special && t.key == Key::Enter)
                    TitleAddOffline();
                else
                    ui::TextEdit::Focused().Apply(t, title.accountName, limits);
            } else if (title::IsServerFieldPage(title.page)) {
                const bool editingAddress = title.page != ui::TitlePage::Edit || title.focus == 1;
                std::string& field = editingAddress ? title.address : title.name;
                if (special && t.key == Key::Tab && title.page == ui::TitlePage::Edit)
                    title.focus ^= 1;
                else if (special && t.key == Key::Enter)
                    TitleCommit();
                else
                    ui::TextEdit::Focused().Apply(t, field, ui::TextLimits{.maxBytes = 120});
            } else if (title.page == ui::TitlePage::Servers) {
                const int count = static_cast<int>(title.Shown().size());
                if (special && t.key == Key::Enter)
                    TitleJoinRow(title.selected);
                else if (special && t.key == Key::Down && count)
                    title.selected = std::min(count - 1, title.selected + 1);
                else if (special && t.key == Key::Up && count)
                    title.selected = std::max(0, title.selected - 1);
            }
        }
    }

    void TitleController::ScrollTitleLists(int guiH) {
        switch (title.page) {
        case ui::TitlePage::Servers:
            title::ScrollList(title.scroll, title.selected, static_cast<int>(title.Shown().size()),
                       ui::TitleVisibleRows(guiH), inputManager.Scroll());
            break;
        case ui::TitlePage::Replays:
            title::ScrollList(title.replayScroll, title.replaySelected, static_cast<int>(title.replays.size()),
                       ui::TitleVisibleRows(guiH), inputManager.Scroll());
            break;
        case ui::TitlePage::Accounts:
            title::ScrollList(title.accountScroll, -1, static_cast<int>(title.accounts.size()),
                       ui::TitleAccountRows(guiH), inputManager.Scroll());
            break;
        default:
            break;
        }
    }

    void TitleController::SyncTitleAccounts() {
        if (ports.PollAccounts()) {
            ports.ApplyActiveAccount();
            if (title.page == ui::TitlePage::AccountSignIn)
                title.page = ui::TitlePage::Accounts;
        }
        const TitlePorts::SignInInfo progress = ports.SignInProgress();
        title.signInUrl = progress.url;
        title.signInCode = progress.code;
        title.signInStatus = progress.status;
        title.accounts.clear();
        const TitlePorts::AccountSnapshot snapshot = ports.Accounts();
        for (const TitlePorts::AccountInfo& e : snapshot.entries) {
            const std::string head =
                title::SkinHeadKey(uiAssets, e.skinKnown ? e.skin : (e.active ? options.skin : std::string()));
            title.accounts.push_back({e.id, e.name, e.microsoft, e.active, head});
        }
        title.accountHead = title::SkinHeadKey(uiAssets, options.skin);
        if (snapshot.hasActive)
            title.accountUser = snapshot.activeName.empty() ? std::string("Microsoft account") : snapshot.activeName;
        else
            title.accountUser = options.playerName;
        title.accountLabel = "Logged in as " + title.accountUser;
        title.playerName = title.accountUser;
        if (title.page == ui::TitlePage::Accounts && titleScreen.lastPage != ui::TitlePage::Accounts &&
            !PERLIN_ENV("PERLIN_PREVIEW"))
            ports.BackfillGamertags(Clock::now());
    }

    void TitleController::SyncTitleServers() {
        const bool enteredServers =
            title.page == ui::TitlePage::Servers && titleScreen.lastPage != ui::TitlePage::Servers &&
            titleScreen.lastPage != ui::TitlePage::Edit && titleScreen.lastPage != ui::TitlePage::Direct &&
            titleScreen.lastPage != ui::TitlePage::AddRealm;
        if (enteredServers) {
            std::vector<std::string> addresses;
            addresses.reserve(title.servers.size());
            for (const ui::ServerEntry& e : title.servers)
                addresses.push_back(e.address);
            ports.Ping(addresses);
            if (!PERLIN_ENV("PERLIN_PREVIEW"))
                ports.RefreshFeatured(false);
        }
        titleScreen.lastPage = title.page;
        ports.PollServers(title);
        if (ports.TakeFeaturedPing()) {
            std::vector<std::string> addresses;
            for (const ui::ServerEntry& e : title.featured) {
                ports.ForgetPing(e.address);
                addresses.push_back(e.address);
            }
            if (!addresses.empty())
                ports.Ping(addresses);
        }
        for (ui::ServerEntry& e : title.servers)
            title::ApplyPingResult(e, ports.PingResult(e.address), true);
        for (ui::ServerEntry& e : title.featured)
            title::ApplyPingResult(e, ports.PingResult(e.address), false);
        RebuildSavedView();
    }

    void TitleController::RebuildSavedView() {
        title.savedView.clear();
        if (!title.featured.empty()) {
            title.savedView.push_back(title::SectionHeader("Featured Servers", title.featured.size(), 3));
            if (title.friendsOpen[3])
                title.savedView.insert(title.savedView.end(), title.featured.begin(), title.featured.end());
        }
        const int offset = static_cast<int>(title.savedView.size());
        if (offset != title.savedOffset) {
            if (title.tab == ui::ServerTab::Saved && title.selected >= title.savedOffset)
                title.selected += offset - title.savedOffset;
            title.savedOffset = offset;
        }
        title.savedView.insert(title.savedView.end(), title.servers.begin(), title.servers.end());
    }

    void TitleController::LoadTitleLogo(TimePoint now) {
        title.nativePanorama = panorama.Ready();
        if (logoBedrock || now < logoRetryAt)
            return;
        logoRetryAt = now + std::chrono::seconds(2);
        constexpr const char* kLogoPath = "textures/ui/title";
        Texture t;
        if (!uiAssets.Image(kLogoPath, t) || t.width <= 0 || t.height <= 0)
            return;
        logoBedrock = true;
        title.logoSrc = kLogoPath;
        title.logoW = 256;
        title.logoH = std::max(1, static_cast<int>(256.0 * t.height / t.width + 0.5));
        if (title.logoH > 64) {
            title.logoW = std::max(1, static_cast<int>(64.0 * t.width / t.height + 0.5));
            title.logoH = 64;
        }
    }

    std::string TitleController::TitleSignature(int guiW, int guiH) {
        const int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
        title.caret = (ms / 500) % 2 == 0;
        title.spinner = static_cast<int>(ms / 250);
        const char* caret = title.caret ? "c" : "n";

        const TitlePorts::UpdateInfo u = ports.UpdateStatus();
        title.updateAction = u.action;
        title.updateText = u.text;

        std::string signature = std::to_string(guiW) + "x" + std::to_string(guiH) + "|" +
                                std::to_string(static_cast<int>(title.page)) + "|" +
                                std::to_string(title.selected) + "|" + std::to_string(title.scroll) + "|" +
                                std::to_string(title.focus) + (title.nativePanorama ? "|np" : "|sp") + "|" +
                                title.logoSrc;
        signature += "|" + title.updateText + (title.updateAction ? "!" : "");
        signature += "|" + title.accountLabel + "|" + title.accountUser + "|" + title.accountHead;
        signature += "|" + std::to_string(title.accountScroll);
        if (title.page == ui::TitlePage::Accounts)
            for (const ui::AccountRow& a : title.accounts)
                signature += "|" + a.id + a.name + (a.active ? "*" : "") + a.headKey;
        else if (title.page == ui::TitlePage::AccountName)
            signature += "|" + title.accountName + caret;
        else if (title.page == ui::TitlePage::AccountSignIn)
            signature += "|" + title.signInUrl + "|" + title.signInCode + "|" + title.signInStatus;
        if (title::IsReplayPage(title.page)) {
            signature += "|r" + std::to_string(title.replaySelected) + "|" + std::to_string(title.replayScroll) +
                         "|" + std::to_string(title.replaySort) + (title.replaySortDesc ? "d" : "a") + "|" +
                         title.replayMessage + "|" + title.replayName + caret + "|" +
                         std::to_string(title.replays.size());
            for (const ui::ReplayEntry& r : title.replays)
                signature += "|" + r.file + r.name + r.line1;
        }
        if (title::IsServerFieldPage(title.page)) {
            signature += "|" + title.joinMessage + "|" + title.name + "|" + title.address + "|" + caret +
                         (title.adding ? "a" : "e");
        } else if (title.page == ui::TitlePage::Servers) {
            signature += "|t" + std::to_string(static_cast<int>(title.tab)) + "|" + title.tabMessage + "|" +
                         title.joinMessage;
            signature += "|" + std::to_string(title.Shown().size()) + (title.friendsOpen[0] ? "H" : "h") +
                         (title.friendsOpen[1] ? "O" : "o") + (title.friendsOpen[2] ? "F" : "f") +
                         (title.friendsOpen[3] ? "S" : "s");
            for (const ui::ServerEntry& s : title.Shown())
                signature += "|" + s.name + "|" + s.address + "|" + std::to_string(static_cast<int>(s.status)) +
                             s.motd + "|" + std::to_string(s.players) + "/" + std::to_string(s.maxPlayers) + "|" +
                             std::to_string(s.pingMs) + s.right + s.note + (s.offline ? "o" : "") +
                             (s.status == ui::ServerEntry::Status::Pinging ? std::to_string(title.spinner & 3)
                                                                           : std::string());
            if (title.Shown().empty())
                signature += "|" + std::to_string(title.spinner & 3);
        }
        return signature;
    }

    void TitleController::RefreshTitleOverlay(ui::Hud& hud, int guiW, int guiH) {
        std::string signature = TitleSignature(guiW, guiH);
        const char* forcedScreen = PERLIN_ENV("PERLIN_SHOW_SCREEN");
        ui::ScreenData forcedData;
        if (forcedScreen) {
            
            jsonScreens.Warm(ports.ServerPackDirectories());
            jsonScreens.WaitReady(5000);
            forcedData = title::ForcedScreenData(forcedScreen, options.playerName);
            float testYaw = 0.0f, testPitch = 0.0f;
            if (const char* head = PERLIN_ENV("PERLIN_DOLL_HEAD"))
                std::sscanf(head, "%f,%f", &testYaw, &testPitch);
            jsonScreens.SetPaperDoll(title::SkinDollKey(uiAssets, options.skin, options.cape, options.skinParts.cape, testYaw, testPitch, dollKeyLast));
            signature = "forced:" + std::string(forcedScreen) + std::to_string(jsonScreens.Generation()) +
                        forcedData.Signature();
        }
        if (signature == titleScreen.signature && hud.Overlay().HasOverlay())
            return;
        titleScreen.signature = signature;
        std::string error;
        ui::SceneDoc forced;
        if (forcedScreen) {
            if (!jsonScreens.Build(forcedScreen, forcedData, guiW, guiH, 0.0f, forced) ||
                !hud.Overlay().SetOverlay(forced, error))
                Log("screens", "forced {} failed: {}", forcedScreen, error);
        } else if (!hud.Overlay().SetOverlay(ui::BuildTitleScene(title, guiW, guiH), error)) {
            Log("ui", "title screen: {}", error);
        }
    }

    void TitleController::OpenTitleOptions(int view) {
        titleScreen.openView = view;
        titleScreen.options = true;
    }

    void TitleController::OpenServerTab(ui::ServerTab tab) {
        title.tab = tab;
        title.selected = -1;
        title.scroll = 0;
        ports.SetJoinMessage({});
        ports.RefreshTab(tab);
    }

    void TitleController::RefreshSavedServers() {
        ports.RefreshFeatured(true);
        title::LoadServers(title.servers, options.server);
        title.selected = -1;
        std::vector<std::string> addresses;
        for (const ui::ServerEntry& e : title.servers) {
            ports.ForgetPing(e.address);
            addresses.push_back(e.address);
        }
        ports.Ping(addresses);
    }

    void TitleController::ClickServerRow(int row, TimePoint now) {
        const std::vector<ui::ServerEntry>& shown = title.Shown();
        if (row >= static_cast<int>(shown.size()))
            return;
        const ui::ServerEntry& entry = shown[static_cast<size_t>(row)];
        if (entry.kind == ui::ServerEntry::Kind::Header) {
            title.friendsOpen[std::clamp<long long>(entry.realmId, 0, 3)] ^= true;
            title.selected = -1;
            return;
        }
        if (row == titleScreen.clickRow && now - titleScreen.clickAt < std::chrono::milliseconds(400))
            TitleJoinRow(row);
        title.selected = row;
        titleScreen.clickRow = row;
        titleScreen.clickAt = now;
    }

    void TitleController::Activate(const std::string& hover, TimePoint now) {
        HandleTitleClick(hover, now);
    }

    void TitleController::HandleTitleClick(const std::string& hover, TimePoint now) {
        const auto rowOf = [&](const char* prefix) {
            const std::string p = prefix;
            return hover.starts_with(p) ? std::atoi(hover.c_str() + p.size()) : -1;
        };
        const auto showPage = [&](ui::TitlePage page) { title.page = page; };
        if (replayLibrary.Click(hover)) {
        } else if (hover == ui::kTitleAccounts) {
            showPage(ui::TitlePage::Accounts);
        } else if (hover == ui::kAccountDone) {
            showPage(ui::TitlePage::Main);
        } else if (hover == ui::kAccountAddOffline) {
            title.accountName.clear();
            showPage(ui::TitlePage::AccountName);
        } else if (hover == ui::kAccountAddMicrosoft) {
            ports.BeginSignIn();
            showPage(ui::TitlePage::AccountSignIn);
        } else if (hover == ui::kAccountNameDone) {
            TitleAddOffline();
        } else if (hover == ui::kAccountCancel || hover == ui::kServerCancel || hover == ui::kEditCancel) {
            TitleBack();
        } else if (const int rm = rowOf(ui::kAccountRemovePrefix); rm >= 0) {
            if (rm < static_cast<int>(title.accounts.size())) {
                ports.RemoveAccount(title.accounts[static_cast<size_t>(rm)].id);
                ports.ApplyActiveAccount();
            }
        } else if (const int acc = rowOf(ui::kAccountRowPrefix); acc >= 0) {
            if (acc < static_cast<int>(title.accounts.size()) &&
                ports.SetActiveAccount(title.accounts[static_cast<size_t>(acc)].id))
                ports.ApplyActiveAccount();
        } else if (hover == ui::kTitleSingle) {
            ports.JoinSingleplayer();
        } else if (hover == ui::kTitleUpdate) {
            if (ports.ActOnUpdate())
                ports.Quit();
        } else if (hover == ui::kTitleMulti) {
            showPage(ui::TitlePage::Servers);
            OpenServerTab(ui::ServerTab::Lan);
        } else if (hover == ui::kTitleOptions) {
            OpenTitleOptions(1);
        } else if (hover == ui::kTitleMods) {
            OpenTitleOptions(4);
        } else if (hover == ui::kTitleWardrobe) {
            OpenTitleOptions(5);
        } else if (hover == ui::kTitleQuit) {
            ports.Quit();
        } else if (hover == ui::kServerJoin) {
            TitleJoinRow(title.selected);
        } else if (hover == ui::kServerDirect) {
            showPage(ui::TitlePage::Direct);
            title.address = options.server;
        } else if (hover == ui::kServerAddRealm) {
            showPage(ui::TitlePage::AddRealm);
            title.address.clear();
            ports.SetJoinMessage({});
        } else if (hover == ui::kServerAdd) {
            TitleEditServer(true);
        } else if (hover == ui::kServerEdit) {
            TitleEditServer(false);
        } else if (hover == ui::kServerDelete) {
            if (title.SavedIndex() >= 0) {
                title.servers.erase(title.servers.begin() + title.SavedIndex());
                title.selected = -1;
                title::SaveServers(title.servers);
            }
        } else if (const int tab = rowOf(ui::kServerTabPrefix); tab >= 0 && tab < 4) {
            OpenServerTab(static_cast<ui::ServerTab>(tab));
        } else if (hover == ui::kServerRefresh) {
            if (title.tab == ui::ServerTab::Saved) {
                RefreshSavedServers();
            } else {
                ports.SetJoinMessage({});
                ports.RefreshTab(title.tab);
            }
        } else if (hover == ui::kEditName) {
            title.focus = 0;
        } else if (hover == ui::kEditAddress) {
            title.focus = 1;
        } else if (hover == ui::kEditDone) {
            TitleCommit();
        } else if (const int go = rowOf(ui::kServerJoinPrefix); go >= 0) {
            TitleJoinRow(go);
        } else if (const int row = rowOf(ui::kServerRowPrefix); row >= 0) {
            ClickServerRow(row, now);
        }
    }
}
