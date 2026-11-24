#include "client/screen/screens/TitleController.hpp"
#include "client/screen/screens/TitleSupport.hpp"
#include "client/selftest/Check.hpp"
#include "client/selftest/SelfTestGlobals.hpp"
#include "client/selftest/SelfTests.hpp"
#include "client/selftest/TestEnvironment.hpp"
#include "client/systems/resources/ServerPacks.hpp"
#include "client/systems/resources/TexturePack.hpp"
#include "util/Paths.hpp"

#include <algorithm>

namespace {
    using namespace perlin;
    using screen::TitlePorts;

    class FakePorts final : public TitlePorts {
    public:
        std::vector<std::string> calls;
        TitlePorts::AccountSnapshot snapshot;
        bool updateAvailable = false;
        bool pollAccounts = false;
        bool setActiveResult = true;
        std::vector<std::string> pinged;
        std::string joinedAddress;
        long long joinedRealm = -1;
        std::string lastMessage = "unset";

        void Note(const std::string& s) { calls.push_back(s); }
        int Count(const std::string& s) const { return static_cast<int>(std::count(calls.begin(), calls.end(), s)); }

        bool PollAccounts() override { return pollAccounts; }
        SignInInfo SignInProgress() override { return {"https://sign.in", "CODE", "waiting"}; }
        AccountSnapshot Accounts() override { return snapshot; }
        bool AccountsEmpty() override { return snapshot.entries.empty(); }
        void AddPreviewAccounts() override { Note("AddPreviewAccounts"); }
        void BackfillGamertags(std::chrono::steady_clock::time_point) override { Note("BackfillGamertags"); }
        void BeginSignIn() override { Note("BeginSignIn"); }
        void CancelSignIn() override { Note("CancelSignIn"); }
        void RemoveAccount(const std::string& id) override { Note("RemoveAccount:" + id); }
        bool SetActiveAccount(const std::string& id) override {
            Note("SetActiveAccount:" + id);
            return setActiveResult;
        }
        void AddOfflineAccount(const std::string& name) override { Note("AddOfflineAccount:" + name); }
        void ApplyActiveAccount() override { Note("ApplyActiveAccount"); }
        void JoinServer(const std::string& address) override {
            joinedAddress = address;
            Note("JoinServer:" + address);
        }
        void AddRealm(const std::string& code) override { Note("AddRealm:" + code); }
        void JoinRealm(long long id) override {
            joinedRealm = id;
            Note("JoinRealm");
        }
        void JoinFriend(const ui::ServerEntry& e) override { Note("JoinFriend:" + e.name); }
        void JoinLanGame(const ui::ServerEntry& e) override { Note("JoinLanGame:" + e.name); }
        void ForgetPing(const std::string& address) override { Note("ForgetPing:" + address); }
        void Ping(const std::vector<std::string>& addresses) override {
            pinged = addresses;
            Note("Ping");
        }
        void RefreshFeatured(bool force) override { Note(force ? "RefreshFeatured:force" : "RefreshFeatured"); }
        void RefreshTab(ui::ServerTab tab) override { Note("RefreshTab:" + std::to_string(static_cast<int>(tab))); }
        void PollServers(ui::TitleModel&) override { Note("PollServers"); }
        bool TakeFeaturedPing() override { return false; }
        ui::ServerEntry PingResult(const std::string&) override { return {}; }
        void SetJoinMessage(const std::string& m) override {
            lastMessage = m;
            Note("SetJoinMessage");
        }
        void JoinSingleplayer() override { Note("JoinSingleplayer"); }
        void ReleaseIdleServerPacks() override { Note("ReleaseIdleServerPacks"); }
        bool UpdateAssetScreen(ui::Hud&, int, int) override { return false; }
        std::vector<std::string> ServerPackDirectories() override { return {}; }
        std::string BuildLabel() override { return "test"; }
        UpdateInfo UpdateStatus() override { return {updateAvailable, "update"}; }
        bool ActOnUpdate() override {
            Note("ActOnUpdate");
            return updateAvailable;
        }
        void Quit() override { Note("Quit"); }
    };

    struct Rig {
        perlin::Options options;
        perlin::Input input;
        perlin::InputManager inputManager{input, options};
        std::unique_ptr<RenderContext> noRenderer;
        ui::PanoramaBackdrop panorama{options, noRenderer, selftest::G().uiAssets, selftest::G().texturePack,
                                      selftest::G().serverPacks};
        screen::Manager screens;
        ui::ChatLog chat;
        ui::TitleModel title;
        screen::TitleScreen& titleScreen = screens.Emplace<screen::TitleScreen>(screen::Id::Title, screen::Traits{true});
        screen::ReplayLibrary replayLibrary{screen::ReplayLibrary::Deps{
            title, [] {}, [](const std::string&) {}, [](const std::string&) {}, [](const std::string&) {}}};
        std::vector<screen::WorldLaunch> launched;
        screen::WorldLibrary worldLibrary{screen::WorldLibrary::Deps{
            title, [] {}, [](const std::string&) {},
            [this](const screen::WorldLaunch& w) { launched.push_back(w); },
            [] { return paths::DataDir() + "/singleplayer/worlds"; }, [] { return paths::DataDir() + "/backups"; }}};
        FakePorts ports;
        screen::TitleController controller{screen::TitleController::Deps{
            titleScreen, title, options, input, inputManager, panorama, screens, chat, replayLibrary, worldLibrary,
            selftest::G().uiAssets, selftest::G().jsonScreens, ports}};
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

        void Click(const std::string& element) { controller.Activate(element, now); }
    };
}

namespace perlin::selftest {
    int TitleController() {
        Checker t("title_controller_test");
        TempDataDir data;

        {
            Rig r;
            ui::Hud hud;
            r.titleScreen.active = false;
            t.Check(!r.controller.Update(hud, 800, 600), "inactive title does not take the frame");
            t.Check(r.ports.calls.empty(), "inactive title makes no port calls");
        }
        {
            Rig r;
            r.Click(ui::kTitleSingle);
            t.Check(r.title.page == ui::TitlePage::CreateWorld, "singleplayer with no worlds opens Create New World");
            r.Click(ui::kCreateBonus);
            r.Click(ui::kCreateDone);
            t.Check(r.launched.size() == 1 && r.launched[0].fresh && r.launched[0].folder == "New World" &&
                        r.launched[0].bonusChest,
                    "Create New World starts a new world with the chosen settings");
            r.title.page = ui::TitlePage::Main;
            r.Click(ui::kTitleQuit);
            t.Equal(r.ports.Count("Quit"), 1, "quit button quits");
            r.Click(ui::kTitleUpdate);
            t.Equal(r.ports.Count("Quit"), 1, "update without pending action does not quit");
            r.ports.updateAvailable = true;
            r.Click(ui::kTitleUpdate);
            t.Equal(r.ports.Count("Quit"), 2, "update that needs restart quits");
        }
        {
            Rig r;
            r.Click(ui::kTitleMulti);
            t.Check(r.title.page == ui::TitlePage::Servers, "multiplayer opens the server page");
            t.Check(r.title.tab == ui::ServerTab::Lan, "multiplayer starts on the LAN tab");
            t.Equal(r.ports.Count("RefreshTab:1"), 1, "opening the tab refreshes it");
            t.Check(r.ports.lastMessage.empty(), "opening a tab clears the join message");
            r.Click(std::string(ui::kServerTabPrefix) + "2");
            t.Check(r.title.tab == ui::ServerTab::Realms, "tab prefix selects realms");
            t.Equal(r.ports.Count("RefreshTab:2"), 1, "realms tab refresh requested");
            r.Click(std::string(ui::kServerTabPrefix) + "9");
            t.Check(r.title.tab == ui::ServerTab::Realms, "out-of-range tab ignored");
            r.Click(ui::kServerRefresh);
            t.Equal(r.ports.Count("RefreshTab:2"), 2, "refresh on a non-saved tab refreshes that tab");
        }
        {
            Rig r;
            r.options.server = "play.example.net:19132";
            r.Click(ui::kTitleMulti);
            r.Click(std::string(ui::kServerTabPrefix) + "0");
            t.Check(r.title.tab == ui::ServerTab::Saved, "saved tab selected");
            r.title.servers = {{.name = "A", .address = "a.example:1"}, {.name = "B", .address = "b.example:2"}};
            r.Click(ui::kServerRefresh);
            t.Equal(r.ports.Count("RefreshFeatured:force"), 1, "saved refresh forces featured refresh");
            t.Equal(r.title.servers.size(), size_t{1}, "saved refresh reloads the persisted list");
            t.Check(r.title.selected == -1, "saved refresh clears selection");
            t.Check(r.ports.pinged.size() == r.title.servers.size(), "every reloaded server is pinged");
        }
        {
            Rig r;
            r.title.page = ui::TitlePage::Servers;
            r.title.tab = ui::ServerTab::Saved;
            r.title.servers = {{.name = "A", .address = "a.example:1"}};
            r.title.selected = 0;
            r.Click(ui::kServerEdit);
            t.Check(r.title.page == ui::TitlePage::Edit && !r.title.adding, "edit opens the edit page");
            t.Check(r.title.name == "A" && r.title.address == "a.example:1", "edit page preloads the selection");
            r.title.name = "Renamed";
            r.title.address = "c.example:3";
            r.Click(ui::kEditDone);
            t.Check(r.title.page == ui::TitlePage::Servers && r.title.tab == ui::ServerTab::Saved,
                    "commit returns to saved servers");
            t.Check(r.title.servers.size() == 1 && r.title.servers[0].name == "Renamed" &&
                        r.title.servers[0].address == "c.example:3",
                    "commit replaces the edited entry");
            t.Equal(r.ports.Count("ForgetPing:c.example:3"), 1, "commit forgets stale ping");
            t.Equal(r.ports.Count("Ping"), 1, "commit pings the new address");

            r.Click(ui::kServerAdd);
            t.Check(r.title.adding && r.title.address.empty() && r.title.name == "Minecraft Server",
                    "add starts from defaults");
            r.title.address = "d.example:4";
            r.Click(ui::kEditDone);
            t.Equal(r.title.servers.size(), size_t{2}, "add appends an entry");
            t.Check(r.title.selected == 1, "added entry is selected");

            r.Click(ui::kServerDelete);
            t.Equal(r.title.servers.size(), size_t{1}, "delete removes the selected entry");
            t.Check(r.title.selected == -1, "delete clears selection");

            ui::TitleModel reread;
            std::vector<ui::ServerEntry> persisted;
            screen::title::LoadServers(persisted, "");
            t.Check(persisted.size() == 1 && persisted[0].address == "c.example:3", "changes persisted to disk");
        }
        {
            Rig r;
            r.title.page = ui::TitlePage::Edit;
            r.title.address.clear();
            r.Click(ui::kEditDone);
            t.Check(r.title.page == ui::TitlePage::Edit, "commit with empty address stays on the edit page");
            t.Check(r.title.servers.empty(), "empty address creates no entry");
        }
        {
            Rig r;
            r.options.server = "keep.example:19132";
            r.Click(ui::kServerDirect);
            t.Check(r.title.page == ui::TitlePage::Direct, "direct connect page opened");
            t.Check(r.title.address == "keep.example:19132", "direct connect preloads the last server");
            r.title.address = "direct.example:19132";
            r.Click(ui::kEditDone);
            t.Equal(r.ports.joinedAddress, std::string("direct.example:19132"), "direct commit joins the address");
        }
        {
            Rig r;
            r.Click(ui::kServerAddRealm);
            t.Check(r.title.page == ui::TitlePage::AddRealm && r.title.address.empty(), "add realm page opened");
            r.title.address = "REALMCODE";
            r.Click(ui::kEditDone);
            t.Equal(r.ports.Count("AddRealm:REALMCODE"), 1, "realm commit submits the invite code");
        }
        {
            Rig r;
            r.title.page = ui::TitlePage::Servers;
            r.title.tab = ui::ServerTab::Lan;
            ui::ServerEntry lan{.name = "Lan", .address = "1.2.3.4:19132", .kind = ui::ServerEntry::Kind::Lan};
            lan.xuid = "lan:abc";
            ui::ServerEntry plain{.name = "Plain", .address = "5.6.7.8:19132", .kind = ui::ServerEntry::Kind::Lan};
            ui::ServerEntry realm{.name = "Realm", .address = "", .kind = ui::ServerEntry::Kind::Realm, .realmId = 42};
            ui::ServerEntry header{.name = "Header", .kind = ui::ServerEntry::Kind::Header};
            header.realmId = 2;
            r.title.lan = {lan, plain, realm, header};
            r.Click(std::string(ui::kServerJoinPrefix) + "0");
            t.Equal(r.ports.Count("JoinLanGame:Lan"), 1, "lan entry with lan xuid joins via lan path");
            r.Click(std::string(ui::kServerJoinPrefix) + "1");
            t.Equal(r.ports.joinedAddress, std::string("5.6.7.8:19132"), "plain lan entry joins by address");
            r.Click(std::string(ui::kServerJoinPrefix) + "2");
            t.Check(r.ports.joinedRealm == 42, "realm entry joins by realm id");
            const int before = static_cast<int>(r.ports.calls.size());
            r.Click(std::string(ui::kServerJoinPrefix) + "3");
            t.Check(static_cast<int>(r.ports.calls.size()) == before, "header rows are not joinable");
            r.Click(std::string(ui::kServerJoinPrefix) + "99");
            t.Check(static_cast<int>(r.ports.calls.size()) == before, "out-of-range row ignored");

            const bool openBefore = r.title.friendsOpen[2];
            r.Click(std::string(ui::kServerRowPrefix) + "3");
            t.Check(r.title.friendsOpen[2] != openBefore, "clicking a header toggles its section");

            r.Click(std::string(ui::kServerRowPrefix) + "1");
            t.Check(r.title.selected == 1, "row click selects");
            t.Equal(r.ports.joinedAddress, std::string("5.6.7.8:19132"), "single click does not join again");
            r.ports.joinedAddress.clear();
            r.Click(std::string(ui::kServerRowPrefix) + "1");
            t.Equal(r.ports.joinedAddress, std::string("5.6.7.8:19132"), "double click joins the row");
        }
        {
            Rig r;
            r.Click(ui::kTitleAccounts);
            t.Check(r.title.page == ui::TitlePage::Accounts, "accounts page opened");
            r.Click(ui::kAccountAddMicrosoft);
            t.Equal(r.ports.Count("BeginSignIn"), 1, "microsoft sign-in begins");
            t.Check(r.title.page == ui::TitlePage::AccountSignIn, "sign-in page shown");
            r.Click(ui::kAccountCancel);
            t.Equal(r.ports.Count("CancelSignIn"), 1, "cancel from sign-in cancels the flow");
            t.Check(r.title.page == screen::title::ParentPage(ui::TitlePage::AccountSignIn),
                    "cancel returns to the parent page");

            r.Click(ui::kAccountAddOffline);
            t.Check(r.title.page == ui::TitlePage::AccountName && r.title.accountName.empty(),
                    "offline account name page cleared");
            r.Click(ui::kAccountNameDone);
            t.Equal(r.ports.Count("AddOfflineAccount:"), 0, "empty name rejected");
            r.title.accountName = "Steve   ";
            r.Click(ui::kAccountNameDone);
            t.Equal(r.ports.Count("AddOfflineAccount:Steve"), 1, "name trimmed before adding");
            t.Check(r.title.page == ui::TitlePage::Accounts, "adding returns to accounts");
            t.Check(r.ports.Count("ApplyActiveAccount") >= 1, "new account applied");

            r.title.accounts = {{"id0", "Alex", false, false, ""}, {"id1", "Sam", true, true, ""}};
            r.Click(std::string(ui::kAccountRowPrefix) + "1");
            t.Equal(r.ports.Count("SetActiveAccount:id1"), 1, "row click activates the account");
            const int applied = r.ports.Count("ApplyActiveAccount");
            r.ports.setActiveResult = false;
            r.Click(std::string(ui::kAccountRowPrefix) + "0");
            t.Equal(r.ports.Count("ApplyActiveAccount"), applied, "failed activation does not apply the account");
            r.Click(std::string(ui::kAccountRemovePrefix) + "0");
            t.Equal(r.ports.Count("RemoveAccount:id0"), 1, "remove button removes the account");
            r.Click(std::string(ui::kAccountRemovePrefix) + "7");
            t.Equal(r.ports.Count("RemoveAccount:id0"), 1, "out-of-range remove ignored");
            r.Click(ui::kAccountDone);
            t.Check(r.title.page == ui::TitlePage::Main, "done returns to main menu");
        }
        {
            Rig r;
            r.Click(ui::kTitleOptions);
            t.Check(r.titleScreen.options && r.titleScreen.openView == 1, "options button opens options view 1");
            r.Click(ui::kTitleMods);
            t.Check(r.titleScreen.openView == 4, "mods button opens view 4");
            r.Click(ui::kTitleWardrobe);
            t.Check(r.titleScreen.openView == 5, "wardrobe button opens view 5");
        }
        {
            Rig r;
            r.title.page = ui::TitlePage::Edit;
            r.Click(ui::kEditName);
            t.Equal(r.title.focus, 0, "name field focus");
            r.Click(ui::kEditAddress);
            t.Equal(r.title.focus, 1, "address field focus");
            r.Click("not_an_element");
            t.Equal(r.title.focus, 1, "unknown element ignored");
        }
        return t.Finish();
    }
}
