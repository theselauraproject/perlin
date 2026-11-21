#pragma once
#include "client/screen/OverlayScreen.hpp"
#include "client/ui/Scene.hpp"
#include "client/screen/ScreenHost.hpp"
#include "client/screen/screens/TitleScreen.hpp"
#include "client/systems/config/Options.hpp"
#include "client/systems/diagnostics/BenchmarkDriver.hpp"
#include "client/systems/gfx/RenderContext.hpp"
#include "client/systems/mods/ModManager.hpp"
#include "client/systems/mods/ModUiRequest.hpp"
#include "client/systems/replay/ReplayController.hpp"
#include "client/systems/resources/ShaderPack.hpp"
#include <functional>
#include <memory>
#include "client/screen/Frame.hpp"
#include "client/systems/resources/PackInfo.hpp"
#include "client/systems/resources/ShaderPackCache.hpp"
#include "client/screen/screens/WardrobeState.hpp"
#include <map>
#include <chrono>
#include <string>
#include <vector>
#include <string>
#include <vector>

namespace perlin::ui {
    struct PauseScreenText {
        std::string title, back, options, quit, advancements, stats, feedback, bugs, lan, players, world,
            mods;
        bool showReplay = false;
        bool replayRecording = false;
        std::string replayStart, replayStop;
        bool optionsEnabled = false;
        bool showPanel = false;
        std::string panelTitle, roleOperator, roleMember, roleVisitor;
        struct PanelRow {
            std::string name;
            int perm = -1;
        };
        std::vector<PanelRow> panel;
    };
    inline constexpr const char* kPauseResumeId = "resume";
    inline constexpr const char* kPauseOptionsId = "options";
    inline constexpr const char* kPauseQuitId = "quit";
    inline constexpr const char* kPauseFeedbackId = "feedback";
    inline constexpr const char* kPausePlayersId = "players";
    inline constexpr const char* kPauseWorldId = "world";
    inline constexpr const char* kPauseWardrobeId = "wardrobe";
    inline constexpr const char* kPauseModsId = "mods";
    inline constexpr const char* kPauseBugsId = "bugs";
    inline constexpr const char* kPauseReplayStartId = "replaystart";
    inline constexpr const char* kPauseReplayStopId = "replaystop";
    inline constexpr const char* kOptionsDoneId = "done";

    struct KeybindRow {
        bool header = false;
        std::string label, value;
        int color = 0;
        bool canReset = false;
    };
    struct KeybindLayout {
        float listTop = 32, listBottom = 0, pitch = 24, labelX = 0, keyX = 0, keyW = 75, resetX = 0,
              resetW = 50, doneX = 0, doneY = 0, doneW = 200, undoX = 0, undoW = 0;
        float contentH = 0, maxScroll = 0;
        float RowY(int i, float scroll) const {
            return listTop + 6.0f + static_cast<float>(i) * pitch - scroll;
        }
    };
    KeybindLayout ComputeKeybindLayout(int rows, int guiW, int guiH);
    ui::SceneDoc BuildKeybindsScreen(const std::vector<KeybindRow>& rows, const std::string& title,
                                       const std::string& done, const std::string& reset, float scroll,
                                       int guiW, int guiH, const std::string& undo = {}, bool canUndo = false);
    inline constexpr const char* kKeybindDoneId = "kbdone";
    inline constexpr const char* kKeybindUndoId = "kbundo";

    struct OptionsRow {
        std::string label;
        std::string value;
        float fraction = -1.0f;
        bool header = false;
        bool locked = false;
        bool wide = false;
        int tab = 0;
        std::string tip{};
        int impact = 0;
        std::string icon{};
        bool formatted = false;
    };

    struct OptionsLayout {
        int count = 0;
        std::vector<float> x, y, rowW;
        std::vector<char> visible;
        float w = 150.0f;
        float h = 20.0f;
        float doneX = 0, doneY = 0, doneW = 200.0f;
        float titleY = 15.0f;
        float listTop = 32.0f, listBottom = 0.0f, contentH = 0.0f, maxScroll = 0.0f;
        bool scrolling = false;
    };

    OptionsLayout ComputeOptionsLayout(const std::vector<OptionsRow>& rows, int guiW, int guiH, float scroll,
                                       int columns = 2);
    ui::SceneDoc BuildOptionsScreen(const std::vector<OptionsRow>& rows, const std::string& title,
                                      const std::string& done, float scroll, int guiW, int guiH,
                                      int hover = -1, int columns = 2);

    struct ListButton {
        std::string text;
        float w = 60.0f;
        bool enabled = true;
        int color = 0;
    };
    struct ListRow {
        bool header = false;
        std::string label, sub;
        int labelColor = 0;
        std::vector<ListButton> buttons;
    };
    struct ListLayout {
        float listTop = 32, listBottom = 0, pitch = 24, labelX = 0, rightX = 0, doneX = 0, doneY = 0,
              doneW = 200, contentH = 0, maxScroll = 0;
        float RowY(int i, float scroll) const {
            return listTop + 6.0f + static_cast<float>(i) * pitch - scroll;
        }
        float ButtonX(const ListRow& row, int index) const;
    };
    ListLayout ComputeListLayout(int rows, int guiW, int guiH);
    ui::SceneDoc BuildListScreen(const std::vector<ListRow>& rows, const std::string& title,
                                   const std::string& done, float scroll, int guiW, int guiH);

    struct WorldRow {
        enum class Kind { Section, Toggle, Segmented, Slider, Number, Button } kind = Kind::Section;
        std::string label, desc;
        bool on = false;
        std::vector<std::string> options;
        int selected = 0;
        float fraction = 0.0f;
        std::string value;
        bool editing = false;
        bool enabled = true;
    };
    struct WorldLayout {
        float panelX = 0, panelW = 0, listTop = 0, listBottom = 0, contentH = 0, maxScroll = 0, doneX = 0,
              doneY = 0, doneW = 200;
        std::vector<float> y, h;
    };
    WorldLayout ComputeWorldLayout(const std::vector<WorldRow>& rows, int guiW, int guiH, float scroll);
    ui::SceneDoc BuildWorldScreen(const std::vector<WorldRow>& rows, const std::string& title,
                                    const std::string& done, float scroll, int guiW, int guiH);

    struct PackEntry {
        std::string name, desc, icon;
        bool movable = false;
        bool canSelect = false, canUnselect = false, canUp = false, canDown = false;
    };
    struct PackScreenLayout {
        float panelW = 200, leftX = 0, rightX = 0, top = 36, bottom = 0, rowH = 36, btnY = 0, btnW = 200,
              openX = 0, doneX = 0;
        float maxScrollA = 0, maxScrollB = 0;
        float RowY(int i, float scroll) const { return top + 2.0f + static_cast<float>(i) * rowH - scroll; }
    };
    PackScreenLayout ComputePackScreenLayout(int available, int selected, int guiW, int guiH);
    ui::SceneDoc BuildPackScreen(const std::vector<PackEntry>& available,
                                   const std::vector<PackEntry>& selected, const std::string& title,
                                   const std::string& subtitle, const std::string& availableLabel,
                                   const std::string& selectedLabel, const std::string& open,
                                   const std::string& done, float scrollA, float scrollB, int guiW, int guiH);

    struct ShaderScreenLayout {
        float colX = 0, colW = 300, listTop = 32, listBottom = 0, rowH = 22, contentH = 0, maxScroll = 0;
        float btnW1 = 0, row1Y = 0, row2Y = 0, openX = 0, settingsX = 0, thirdW = 0, cancelX = 0, applyX = 0,
              doneX = 0;
        float RowY(int i, float scroll) const {
            return listTop + 6.0f + static_cast<float>(i) * rowH - scroll;
        }
    };
    ShaderScreenLayout ComputeShaderScreenLayout(int rows, int guiW, int guiH);
    ui::SceneDoc BuildShaderScreen(const std::vector<std::string>& rows, int selectedRow, int toggleRow,
                                     const std::string& title, const std::string& subtitle,
                                     const std::string& dragText, const std::string& open,
                                     const std::string& settings, const std::string& cancel,
                                     const std::string& apply, const std::string& done, float scroll,
                                     int guiW, int guiH, const std::string& progressText = {},
                                     float progress = -1.0f);

    struct ModEntry {
        std::string name, desc, badge, icon;
        int badgeColor = 0;
    };
    struct ModDetail {
        bool valid = false;
        std::string name, badge, version, author, icon, body, toggleLabel, reloadLabel, settingsLabel;
        int badgeColor = 0;
        bool toggleEnabled = true;
        bool hasSettings = false;
    };
    struct ModScreenLayout {
        float leftX = 8, leftW = 0, rightX = 0, rightW = 0, searchY = 20, searchH = 18, listTop = 56,
              listBottom = 0, rowH = 36;
        float detailBtnY = 0, detailTextTop = 0, detailTextBottom = 0, toggleX = 0, toggleW = 0, reloadX = 0,
              reloadW = 0, settingsX = 0, settingsW = 0;
        float btnY = 0, btnW = 200, openX = 0, doneX = 0;
        float maxScroll = 0, maxDetailScroll = 0;
        float RowY(int i, float scroll) const {
            return listTop + 2.0f + static_cast<float>(i) * rowH - scroll;
        }
    };
    ModScreenLayout ComputeModScreenLayout(int rows, const ModDetail& detail, int guiW, int guiH);
    ui::SceneDoc BuildModScreen(const std::vector<ModEntry>& rows, int selected, const ModDetail& detail,
                                  const std::string& title, const std::string& search, bool caret,
                                  const std::string& showing, const std::string& hint,
                                  const std::string& open, const std::string& done, float scroll,
                                  float detailScroll, int guiW, int guiH);

    ui::SceneDoc BuildPauseScreen(const PauseScreenText& text, int guiW,
                                    int guiH);

    struct WardrobeCard {
        std::string name;
        std::string img;
        bool selected = false;
    };
    struct WardrobeText {
        std::string title, skins, capes, addSkin, addCape, slim, slimOn, slimOff, done, dropHint, dragHint;
    };
    struct WardrobeLine {
        int kind = 0;
        int row = 0;
        float y = 0;
    };
    struct WardrobeLayout {
        float listX = 0, listW = 0, listTop = 0, listBottom = 0;
        float rightX = 0, rightW = 0;
        float prevX = 0, prevY = 0, prevW = 0, prevH = 0, nameY = 0, slimY = 0, slimX = 0, slimW = 0;
        float cardW = 44, cardH = 66, gap = 3, headH = 20, addW = 86, trashW = 20;
        int cols = 1;
        float doneX = 0, doneY = 0, doneW = 200, hintY = 0;
        int totalLines = 0, firstLine = 0, maxFirst = 0;
        std::vector<WardrobeLine> lines;
        float AddX() const { return listX + listW - addW; }
        float TrashX() const { return AddX() - 3.0f - trashW; }
        float ToggleW() const { return TrashX() - 4.0f - listX; }
        float CardX(int i) const { return listX + static_cast<float>(i % cols) * (cardW + gap); }
    };
    WardrobeLayout ComputeWardrobeLayout(int skinCards, int capeCards, bool skinOpen, bool capeOpen,
                                         int firstLine, int guiW, int guiH);
    ui::SceneDoc BuildWardrobeScene(const std::vector<WardrobeCard>& skins,
                                 const std::vector<WardrobeCard>& capes, bool skinOpen, bool capeOpen,
                                 int firstLine, const std::string& previewImg, const std::string& previewName,
                                 bool slim, bool canTrashSkin, bool canTrashCape, const WardrobeText& t,
                                 int guiW, int guiH);

    ui::SceneDoc BuildLanguageScreen(const std::vector<std::string>& rows, int selectedRow,
                                       const std::string& title, const std::string& subtitle,
                                       const std::string& done, float scroll, int guiW, int guiH);
}

namespace perlin::screen {
    struct OptionsModel;
    struct PauseFrame;

    struct PauseHost {
        Options& options;
        std::unique_ptr<RenderContext>& renderer;
        const std::string& runningRenderer;
        std::string& javaPackPending;
        replay::ReplayController& replay;
        TitleScreen& title;
        mods::ModManager& manager;
        mods::UiRequest& modRequest;
        std::function<void(const std::string&)> toggleMod;
        std::function<bool()> benchmarkRunning;
        std::function<void(const bench::Settings&)> startBenchmark;
        std::function<void(bool)> setVersionMismatchNoAsk;
        std::function<void(bool)> toggleReplayRecording;
        std::function<void(ui::Hud&)> leaveToServerList;
        std::function<void()> commitTexturePacks;
        std::function<void()> applyTexturePacks;
        std::function<void()> applyShaderPack;
        std::function<bool()> discordSupported;
        std::function<bool()> updateSupported;
        std::function<void(const std::string&)> openUrl;
        std::function<bool(const char*)> pickFile;
        ShaderPackCache& packCache;
        std::function<std::string()> activeAccountId;
        std::function<void(const std::string&, const std::string&, const std::string&)> setAccountSkin;
    };

    struct PackMenuInput {
        float gx = 0.0f, gy = 0.0f;
        bool click = false;
        float wheel = 0.0f;
    };

    struct PauseScreen : OverlayScreen {
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        PauseScreen(Id id, Traits traits, const Host& host, const PauseHost& services)
            : OverlayScreen(id, traits), host(host), services(services) {}
        Claim Update(Frame& frame) override;
        void UpdatePauseScreen(ui::Hud& hud, BedrockSession* live, int fbW, int fbH);
        bool OnChatSettings() const { return view == 1 && optionsUi.page == 12; }

        bool previewOpened = false, players = false;
        int view = 0;
        WardrobeState wardrobe;

    struct OptionsMenu {
        int drag = -1, page = 0, sub = 0;
        int bindCapture = -1, controlsTab = 0, bindDevice = 0;
        bool bindWasCapturing = false;
        bool bindLeftArmed = false;
        struct BindUndo {
            int target = -1;
            int device = 0;
            std::string plugin, id;
            Binding previous;
        };
        std::vector<BindUndo> bindUndo;
        TimePoint bindStartedAt;
        bool touchEditing = false, touchEditHidden = false, touchEditSeen = false;
        float kbScroll = 0.0f, scroll = 0.0f;
        int scrollKey = -1;
        int hoverRow = -1;
        int typingRow = -1;
        int typingKey = -1;
        std::string typingText;
        bool typingCommit = false;
        bool rightDown = false;
        TimePoint hoverSince;
        std::map<std::string, std::pair<int, double>> worldOverride;
        std::string signature;
    } optionsUi;


    struct PackMetaCacheEntry {
        std::string stamp;
        PackMeta meta;
    };
    struct PackMenu {
        float scrollActive = 0.0f, scrollAvailable = 0.0f, shaderScroll = 0.0f;
        float modScroll = 0.0f, modDetailScroll = 0.0f;
        std::string modSelected, modSearch;
        std::string shaderPending, metaStampDir;
        bool shaderPendingEnabled = false, shaderPendingInit = false;
        bool closeWhenLoaded = false, shaderLoadWatch = false;
        std::map<std::string, PackMetaCacheEntry> metaCache;
        std::map<std::string, std::string> modIconStamp;
        std::vector<std::string> foundCache;
        TimePoint foundAt;
        std::vector<std::string> pending;
        bool pendingInit = false;
        std::vector<std::string> modRowTips;
    } packUi;

    private:
        void BuildChatOptions(OptionsModel& m, PauseFrame& f);
        void BuildControlsOptions(OptionsModel& m, PauseFrame& f);
        void BuildJavaShaderSettingsOptions(OptionsModel& m, PauseFrame& f);
        void BuildKeyBindsOptions(OptionsModel& m, PauseFrame& f);
        void BuildMainOptions(OptionsModel& m, PauseFrame& f);
        void BuildModSettingsOptions(OptionsModel& m, PauseFrame& f);
        void BuildModsOptions(OptionsModel& m, PauseFrame& f);
        void BuildPerlinOptions(OptionsModel& m, PauseFrame& f);
        void BuildResourcePacksOptions(OptionsModel& m, PauseFrame& f);
        void BuildShaderPacksOptions(OptionsModel& m, PauseFrame& f);
        void BuildShaderSettingsOptions(OptionsModel& m, PauseFrame& f);
        void BuildSkinOptions(OptionsModel& m, PauseFrame& f);
        void BuildSoundOptions(OptionsModel& m, PauseFrame& f);
        void BuildVideoOptions(OptionsModel& m, PauseFrame& f);
        void BuildWorldOptions(OptionsModel& m, PauseFrame& f);
        void LeavePackMenu(PauseFrame& f);
        void ListPacks(TimePoint now, const std::string& dir, bool allowMcpack);
        void OpenPackFolder(const std::string& dir);
        const PackMeta& PackMetaFor(const std::string& dir, const std::string& name);
        void UpdateKeybindPage(PauseFrame& f);
        void UpdateLanguagePage(PauseFrame& f, const PackMenuInput& in);
        void UpdateModsPage(PauseFrame& f, const PackMenuInput& in);
        void UpdateOptionsMenu(PauseFrame& f);
        void UpdatePackMenu(PauseFrame& f);
        void UpdatePauseMenu(PauseFrame& f);
        void UpdateResourcePackPage(PauseFrame& f, const PackMenuInput& in);
        void UpdateShaderPackPage(PauseFrame& f, const PackMenuInput& in);
        const Binding& BindingOf(Action a) const {
            return optionsUi.bindDevice == 1 ? services.options.GetPadKey(a) : services.options.GetKey(a);
        }
        void SetBindingOf(Action a, const Binding& b) {
            if (!(BindingOf(a) == b)) {
                optionsUi.bindUndo.push_back({static_cast<int>(a), optionsUi.bindDevice, {}, {}, BindingOf(a)});
                if (optionsUi.bindUndo.size() > 64)
                    optionsUi.bindUndo.erase(optionsUi.bindUndo.begin());
            }
            if (optionsUi.bindDevice == 1)
                services.options.SetPadKey(a, b);
            else
                services.options.SetKey(a, b);
        }
        void UpdateWardrobe(PauseFrame& f);

        const Host& host;
        const PauseHost& services;
    };
}
