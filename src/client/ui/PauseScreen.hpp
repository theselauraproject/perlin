#pragma once
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
              resetW = 50, doneX = 0, doneY = 0, doneW = 200;
        float contentH = 0, maxScroll = 0;
        float RowY(int i, float scroll) const {
            return listTop + 6.0f + static_cast<float>(i) * pitch - scroll;
        }
    };
    KeybindLayout ComputeKeybindLayout(int rows, int guiW, int guiH);
    std::string BuildKeybindsScreenRml(const std::vector<KeybindRow>& rows, const std::string& title,
                                       const std::string& done, const std::string& reset, float scroll,
                                       int guiW, int guiH);
    inline constexpr const char* kKeybindDoneId = "kbdone";

    struct OptionsRow {
        std::string label;
        std::string value;
        float fraction = -1.0f;
        bool header = false;
        bool locked = false;
        bool wide = false;
        int tab = 0;
        std::string tip;
        int impact = 0;
        std::string icon;
        // The label and value carry Minecraft colour codes (shader pack settings do).
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
    std::string BuildOptionsScreenRml(const std::vector<OptionsRow>& rows, const std::string& title,
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
    std::string BuildListScreenRml(const std::vector<ListRow>& rows, const std::string& title,
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
    std::string BuildWorldScreenRml(const std::vector<WorldRow>& rows, const std::string& title,
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
    std::string BuildPackScreenRml(const std::vector<PackEntry>& available,
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
    std::string BuildShaderScreenRml(const std::vector<std::string>& rows, int selectedRow, int toggleRow,
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
    std::string BuildModScreenRml(const std::vector<ModEntry>& rows, int selected, const ModDetail& detail,
                                  const std::string& title, const std::string& search, bool caret,
                                  const std::string& showing, const std::string& hint,
                                  const std::string& open, const std::string& done, float scroll,
                                  float detailScroll, int guiW, int guiH);

    std::string DefaultPauseScreenTemplate();
    std::string BuildPauseScreenRml(const std::string& templateText, const PauseScreenText& text, int guiW,
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
    std::string BuildWardrobeRml(const std::vector<WardrobeCard>& skins,
                                 const std::vector<WardrobeCard>& capes, bool skinOpen, bool capeOpen,
                                 int firstLine, const std::string& previewImg, const std::string& previewName,
                                 bool slim, bool canTrashSkin, bool canTrashCape, const WardrobeText& t,
                                 int guiW, int guiH);

    std::string BuildLanguageScreenRml(const std::vector<std::string>& rows, int selectedRow,
                                       const std::string& title, const std::string& subtitle,
                                       const std::string& done, float scroll, int guiW, int guiH);
}
