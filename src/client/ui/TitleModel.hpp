#pragma once
#include <chrono>
#include <string>
#include <vector>
#include <cstdint>
#include <string>
#include <vector>

namespace perlin::ui {
    struct ServerEntry {
        std::string name;
        std::string address;
        enum class Status : uint8_t { Unknown, Pinging, Online, Offline } status = Status::Unknown;
        std::string motd{};
        std::string version{};
        int players = 0, maxPlayers = 0;
        int pingMs = 0;
        enum class Kind : uint8_t { Server, Lan, Realm, Friend, Header } kind = Kind::Server;
        std::string right{};
        std::string note{};
        bool hosting = false;
        std::string icon{};
        bool offline = false;
        long long realmId = 0;
        std::string xuid{};
        bool pingable = false;
    };

    enum class ServerTab : uint8_t { Saved, Lan, Realms, Friends };

    struct AccountRow {
        std::string id;
        std::string name;
        bool microsoft = false;
        bool active = false;
        std::string headKey;
    };

    inline bool Joinable(const ServerEntry& e) {
        return e.kind == ServerEntry::Kind::Header ? false
                                                   : (e.kind == ServerEntry::Kind::Friend ? e.hosting : true);
    }

    struct ReplayEntry {
        std::string name, file, line1, line2;
        bool broken = false;
        long long created = 0;
        long long sizeBytes = 0;
        unsigned durationMs = 0;
    };

    // A singleplayer world, as the world list shows it.
    struct WorldEntry {
        std::string folder;      // its folder under the worlds directory
        std::string name;        // LevelName
        std::string line2, line3; // "folder (date)" and "Survival Mode, Cheats, Version: ..."
        std::string icon;        // image source for the preview, empty when there is none
        long long lastPlayed = 0;
        long long seed = 0;
        int gameType = 0, difficulty = 2, generator = 1;
        bool cheats = false, structures = true, hardcore = false;
    };

    enum class TitlePage {
        Main,
        Servers,
        Edit,
        Direct,
        AddRealm,
        Accounts,
        AccountName,
        AccountSignIn,
        Replays,
        ReplayRename,
        ReplayDelete,
        Worlds,
        CreateWorld,
        EditWorld,
        DeleteWorld,
        SuperflatWorld
    };

    struct TitleModel {
        TitlePage page = TitlePage::Main;
        std::vector<ServerEntry> servers;
        ServerTab tab = ServerTab::Lan;
        std::vector<ServerEntry> lan, realms, friends;
        bool friendsOpen[4] = {true, true, false, false};
        std::vector<ServerEntry> featured, savedView;
        std::string tabMessage;
        std::string joinMessage;
        std::vector<ServerEntry>& Shown() {
            switch (tab) {
            case ServerTab::Lan:
                return lan;
            case ServerTab::Realms:
                return realms;
            case ServerTab::Friends:
                return friends;
            default:
                return savedView.empty() ? servers : savedView;
            }
        }
        const std::vector<ServerEntry>& Shown() const { return const_cast<TitleModel*>(this)->Shown(); }
        int selected = -1;
        int savedOffset = 0;
        int SavedIndex() const {
            const int i = selected - savedOffset;
            return tab == ServerTab::Saved && selected >= 0 && i >= 0 && i < static_cast<int>(servers.size())
                       ? i
                       : -1;
        }
        int scroll = 0;
        bool adding = false;
        std::string name;
        std::string address;
        int focus = 0;
        bool caret = false;
        int spinner = 0;
        std::string version;
        std::string copyright;

        std::string updateText;
        bool updateAction = false;
        bool nativePanorama = false;
        std::string logoSrc;
        int logoW = 256, logoH = 64;

        std::vector<ReplayEntry> replays;
        int replaySelected = -1;
        int replayScroll = 0;
        int replaySort = 0;
        bool replaySortDesc = true;
        std::string replayMessage;
        std::string replayName;

        // Select World: the worlds matching the search, newest played first
        std::vector<WorldEntry> worlds;
        int worldSelected = -1;
        int worldScroll = 0;
        std::string worldSearch;
        bool worldSearchFocus = false;
        std::string worldMessage;
        int worldTotal = 0;
        // Create New World (also Re-Create)
        int createTab = 0; // Game, World, More
        int createFocus = 0; // 0 world name, 1 seed
        std::string createName = "New World", createSeed, createFolder;
        int createMode = 0; // 0 Survival, 1 Creative, 2 Adventure, 3 Hardcore
        int createDifficulty = 2; // Peaceful, Easy, Normal, Hard
        bool createCheats = false;
        int createType = 0; // 0 Default, 1 Superflat
        bool createStructures = true, createBonus = false;
        // Superflat layers in the game's preset text form, top layer last: "minecraft:bedrock,2*minecraft:dirt,..."
        std::string createFlatLayers = "minecraft:bedrock,2*minecraft:dirt,minecraft:grass_block";
        std::string flatEdit; // the text being edited on the Customize page
        int flatPreset = 0;
        // Edit World
        std::string editWorldName, editWorldMessage;

        std::string accountLabel;
        std::string accountUser;
        std::string accountHead;

        std::string dollSrc;
        std::string playerName;
        std::vector<AccountRow> accounts;
        int accountScroll = 0;
        std::string accountName;
        std::string signInUrl, signInCode, signInStatus;
    };

    inline constexpr const char* kTitleMulti = "title_multi";
    inline constexpr const char* kTitleUpdate = "title_update";
    inline constexpr const char* kTitleSingle = "title_single";

    inline constexpr const char* kSingleplayerTip =
        "Singleplayer is in very early development and is not playable yet.";
    inline constexpr const char* kTitleQuit = "title_quit";
    inline constexpr const char* kTitleOptions = "title_options";
    inline constexpr const char* kTitleMods = "title_mods";
    inline constexpr const char* kTitleWardrobe = "title_wardrobe";
    inline constexpr const char* kTitleReplays = "title_replays";
    inline constexpr const char* kReplayPlay = "rp_play";
    inline constexpr const char* kReplayRename = "rp_rename";
    inline constexpr const char* kReplayDelete = "rp_delete";
    inline constexpr const char* kReplayFolder = "rp_folder";
    inline constexpr const char* kReplaySort = "rp_sort";
    inline constexpr const char* kReplayDirection = "rp_dir";
    inline constexpr const char* kReplayDone = "rp_done";
    inline constexpr const char* kReplayRowPrefix = "rprow_";
    inline constexpr const char* kReplayRenameOk = "rp_rename_ok";
    inline constexpr const char* kReplayNameField = "rp_name";
    inline constexpr const char* kReplayDeleteOk = "rp_delete_ok";
    inline constexpr const char* kReplayDialogCancel = "rp_dialog_cancel";
    inline constexpr const char* kWorldSearch = "w_search";
    inline constexpr const char* kWorldRowPrefix = "wrow_";
    inline constexpr const char* kWorldPlayPrefix = "wgo_";
    inline constexpr const char* kWorldPlay = "w_play";
    inline constexpr const char* kWorldCreate = "w_create";
    inline constexpr const char* kWorldEdit = "w_edit";
    inline constexpr const char* kWorldDelete = "w_delete";
    inline constexpr const char* kWorldRecreate = "w_recreate";
    inline constexpr const char* kWorldBack = "w_back";
    inline constexpr const char* kCreateTabPrefix = "ctab_";
    inline constexpr const char* kCreateName = "c_name";
    inline constexpr const char* kCreateSeed = "c_seed";
    inline constexpr const char* kCreateMode = "c_mode";
    inline constexpr const char* kCreateDifficulty = "c_diff";
    inline constexpr const char* kCreateCheats = "c_cheats";
    inline constexpr const char* kCreateType = "c_type";
    inline constexpr const char* kCreateStructures = "c_struct";
    inline constexpr const char* kCreateBonus = "c_bonus";
    inline constexpr const char* kCreateDone = "c_done";
    inline constexpr const char* kCreateCancel = "c_cancel";
    inline constexpr const char* kEditWorldName = "ew_name";
    inline constexpr const char* kEditWorldResetIcon = "ew_icon";
    inline constexpr const char* kEditWorldFolder = "ew_folder";
    inline constexpr const char* kEditWorldBackup = "ew_backup";
    inline constexpr const char* kEditWorldBackupFolder = "ew_backups";
    inline constexpr const char* kEditWorldSave = "ew_save";
    inline constexpr const char* kEditWorldCancel = "ew_cancel";
    inline constexpr const char* kDeleteWorldOk = "dw_ok";
    inline constexpr const char* kDeleteWorldCancel = "dw_cancel";
    inline constexpr const char* kCreateCustomize = "c_customize";
    inline constexpr const char* kFlatField = "sf_text";
    inline constexpr const char* kFlatPreset = "sf_preset";
    inline constexpr const char* kFlatDone = "sf_done";
    inline constexpr const char* kFlatCancel = "sf_cancel";
    inline constexpr const char* kServerJoin = "servers_join";
    inline constexpr const char* kServerDirect = "servers_direct";
    inline constexpr const char* kServerAdd = "servers_add";
    inline constexpr const char* kServerAddRealm = "servers_addrealm";
    inline constexpr const char* kServerEdit = "servers_edit";
    inline constexpr const char* kServerDelete = "servers_delete";
    inline constexpr const char* kServerRefresh = "servers_refresh";
    inline constexpr const char* kServerCancel = "servers_cancel";
    inline constexpr const char* kEditName = "edit_name";
    inline constexpr const char* kEditAddress = "edit_address";
    inline constexpr const char* kEditDone = "edit_done";
    inline constexpr const char* kEditCancel = "edit_cancel";
    inline constexpr const char* kTitleAccounts = "title_accounts";
    inline constexpr const char* kAccountRowPrefix = "accrow_";
    inline constexpr const char* kAccountRemovePrefix = "accrm_";
    inline constexpr const char* kAccountAddMicrosoft = "acc_add_ms";
    inline constexpr const char* kAccountAddOffline = "acc_add_off";
    inline constexpr const char* kAccountDone = "acc_done";
    inline constexpr const char* kAccountNameField = "acc_name";
    inline constexpr const char* kAccountNameDone = "acc_name_ok";
    inline constexpr const char* kAccountCancel = "acc_cancel";
    inline constexpr const char* kServerTabPrefix = "stab_";
    inline constexpr const char* kServerRowPrefix = "row_";
    inline constexpr const char* kServerJoinPrefix = "go_";
}
