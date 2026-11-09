#include "client/ui/SettingInfo.hpp"
#include <algorithm>
#include <cctype>
#include <iterator>
#include "client/i18n/Lang.hpp"
#include "util/Strings.hpp"

namespace perlin::ui {
    namespace {
        struct Entry {
            const char* english;
            const char* labelKey;
            const char* descKey;
            const char* fallback;
            int impact;
        };

        const Entry kEntries[] = {
            {"FOV", "options.fov", "options.fov.description", "Viewing angle of the in-game camera.", 1},
            {"Render Distance", "options.renderDistance", "options.renderDistance.description",
             "How far away chunks are drawn. High values can hurt performance.", 3},
            {"Brightness", "options.gamma", "options.gamma.description", "Overall screen brightness.", 0},
            {"Max Framerate", "options.framerateLimit", "",
             "Caps how many frames per second the game renders. Lower caps save power and heat.", 1},
            {"VSync", "options.vsync", "",
             "Synchronizes frames with your monitor's refresh rate to prevent screen tearing.", 1},
            {"Fullscreen", "options.fullscreen", "",
             "Fills the whole screen without window borders. F11 toggles it.", 1},
            {"Center Cursor", "perlin.options.centerCursor", "",
             "Moves the pointer to the middle of the window when the inventory, a menu or a form opens.", 0},
            {"GUI Scale", "options.guiScale.optionName", "options.guiScale.optionName.description",
             "Size of the HUD and menus.", 1},
            {"Fancy Graphics", "options.graphics.fancy", "",
             "Enables higher quality leaves, water and other visual details.", 2},
            {"Motion Blur", "options.motionBlur", "", "Blurs the screen while you turn the camera quickly.",
             2},
            {"Menu Blur", "options.accessibility.menu_background_blurriness", "",
             "How strongly the world, or the panorama on loading screens and menus, is blurred behind them.",
             2},
            {"Entity Shadows", "options.entityShadows", "", "Draws simple shadows beneath mobs and players.",
             1},
            {"FOV Effects", "options.fovEffectScale", "options.fov.toggle.description",
             "Allow sprinting or potions to temporarily change the field of view.", 0},
            {"Damage Tilt", "options.damageTilt", "", "How much the camera shakes when you take damage.", 0},
            {"Damage Tint", "perlin.options.damageTint", "", "Tints the screen red when you take damage.", 0},
            {"Supersampling", "options.rgss", "",
             "Renders the world at a higher resolution and scales it down for smoother edges. Very demanding "
             "on the GPU.",
             3},
            {"Upscaler", "", "",
             "Draws the world at a lower resolution and rebuilds it at window size for more FPS. The HUD and "
             "menus stay sharp. FSR 1.0 keeps edges cleanest, Bicubic is cheaper, Bilinear is cheapest.",
             3},
            {"Upscale Quality", "", "",
             "How much of the window size the world is drawn at: 100% is native, lower is faster and softer.",
             3},
            {"Upscale Sharpness", "", "",
             "Contrast-adaptive sharpening after the upscale. Brings back some of the detail the lower "
             "resolution loses.",
             3},
            {"Enable LODs", "", "",
             "Distant terrain as simplified tiles, so the render distance can go far past the loaded chunks.",
             3},
            {"Clouds", "options.renderClouds", "", "Shows or hides the clouds in the sky.", 2},
            {"Cloud Range", "", "", "How far away clouds are drawn. Larger ranges draw more clouds.", 2},
            {"Better Grass", "", "",
             "Grass block sides blend with the grass next to them so there are no visible edges between "
             "blocks.",
             1},
            {"Glass", "", "",
             "Choose how glass panes and blocks join: Normal keeps borders, Connected merges neighbouring "
             "glass, Clear removes the frame.",
             1},
            {"Shadow Distance", "", "",
             "How far from you shadows are rendered. Auto picks a distance from your render distance.", 3},
            {"Held Item Light", "", "", "Brightness of the light cast by items you hold, such as torches.",
             1},
            {"Dynamic Lights", "", "",
             "Held and dropped light sources illuminate the world around them. Fancy updates more often.", 2},
            {"Third Person Crosshair", "perlin.options.thirdCrosshair", "",
             "Show the crosshair in third-person views.", 0},
            {"Perlin User Badges", "perlin.options.perlinBadges", "",
             "Shows the Perlin logo next to the names of players who also use Perlin, yourself included, in nametags and the tab list.", 0},
            {"Third Person Nametag", "perlin.options.thirdNametag", "",
             "Show your own nametag in third-person views.", 0},
            {"Zoom Level", "", "", "How strongly the zoom key magnifies the view.", 0},
            {"Zoom In Time", "", "", "How long the zoom takes to reach full magnification.", 0},
            {"Zoom Out Time", "", "", "How long it takes to return to normal after releasing zoom.", 0},
            {"Zoom Easing", "", "", "The curve used when zooming in and out.", 0},
            {"Zoom Scroll Step", "", "", "How much each mouse wheel notch changes the zoom while zoomed.", 0},
            {"Zoom Smoothing", "", "", "Smooths zoom changes. Higher is slower and softer.", 0},
            {"Zoom Reset On Press", "", "",
             "Return to the default zoom level every time you press the zoom key.", 0},
            {"Cinematic Camera", "", "", "Smooths mouse movement while zoomed for a cinematic feel.", 0},
            {"Scale Sensitivity", "", "", "Lowers mouse sensitivity while zoomed so aiming stays steady.", 0},
            {"Hot Reload On Save", "", "",
             "Automatically reloads texture packs, shader packs and mods when their files change on disk.",
             1},
            {"Renderer", "", "",
             "The graphics API Perlin draws with. Auto picks the best one for this machine. Takes effect "
             "after a "
             "restart.",
             2},
            {"Auto Update", "perlin.options.autoUpdate", "",
             "Download new Perlin builds in the background and install them on the next launch.", 0},
            {"Discord Status", "perlin.options.discordPresence", "",
             "Show what you are playing on your Discord profile.", 0},
            {"Show Server in Discord", "perlin.options.discordShowServer", "",
             "Include the server address in your Discord status.", 0},
            {"Discord Friends Can Join", "perlin.options.discordJoin", "",
             "Let Discord friends join the server you are on from your profile.", 0},
            {"Connection", "perlin.options.transport", "",
             "Network protocol for servers. Auto checks for NetherNet and falls back to RakNet. Applies on "
             "the "
             "next join.",
             0},
            {"Ask On Version Mismatch", "perlin.options.versionMismatchAsk", "",
             "Ask before joining an older server as its version. Off joins as the server's version without "
             "asking.",
             0},
            {"Sensitivity", "options.sensitivity", "options.sensitivity.description",
             "Adjust the speed of the camera when looking around.", 0},
            {"Invert Mouse", "options.invertMouse", "options.invertYAxis.Mouse.description",
             "Reverses up and down when looking around.", 0},
            {"Sprint", "key.sprint", "",
             "Hold: sprint while the key is held. Toggle: press once to keep sprinting.", 0},
            {"Sneak", "key.sneak", "options.sneakOption.description",
             "Choose your preferred control method for crouching.", 0},
            {"Show Subtitles", "options.showSubtitles", "options.enableGameplaySubtitles.description",
             "Add subtitles for all sounds.", 0},
            {"Show Hand", "options.showHand", "options.hidehand.description",
             "Show your hand and held item in first-person view.", 0},
            {"View Bobbing", "options.viewBobbing", "", "The view and hand sway slightly while walking.", 0},
            {"Camera Bobbing", "perlin.options.bobCamera", "", "The camera sways slightly while walking.", 0},
            {"Hand Bobbing", "perlin.options.bobHand", "", "The held item sways while walking.", 0},
            {"Swing Animation", "perlin.options.swing", "",
             "Play the hand swing when attacking or breaking blocks.", 0},
            {"Equip Animation", "perlin.options.equip", "", "Play the raise animation when switching items.",
             0},
            {"Hand Sway", "perlin.options.sway", "", "The held item lags slightly behind when you turn.", 0},
            {"Left Handed", "options.mainHand.left", "",
             "Show your main hand on the left side of the screen.", 0},
            {"Show Offhand", "perlin.options.offhand", "", "Show the item in your offhand.", 0},
            {"Hand Size", "perlin.options.handScale", "", "Scales the first-person hand and held item.", 0},
            {"Friendly Fire", "", "createWorldScreen.pvp.description", "Players can hurt each other.", 0},
            {"Show Coordinates", "", "createWorldScreen.showCoordinates.description",
             "Display your current position at all times.", 0},
            {"Player Waypoints", "", "createWorldScreen.playerWaypointsEveryone.description",
             "Adds other players as waypoints to the locator bar.", 0},
            {"Show Days Played", "", "createWorldScreen.showDaysPlayed.description",
             "Number of in-game days that have passed.", 0},
            {"Fire Spreads", "", "createWorldScreen.fireSpreads.description",
             "Might want to build with stone.", 0},
            {"Recipes Unlocking", "", "createWorldScreen.recipesUnlock.description",
             "Collect materials to automatically unlock recipes.", 0},
            {"TNT Explodes", "", "createWorldScreen.tntexplodes.description", "Red box goes boom.", 0},
            {"Respawn Blocks Explode", "", "createWorldScreen.respawnblocksexplode.description",
             "Respawn anchors and beds can explode if poorly placed.", 0},
            {"Mob Loot", "", "createWorldScreen.mobLoot.description", "Mobs drop items when killed.", 0},
            {"Natural Regeneration", "", "createWorldScreen.naturalregeneration.description",
             "Gain or lose health based on your hunger.", 0},
            {"Tile Drops", "", "createWorldScreen.tileDrops.description",
             "Blocks drop items when they are broken.", 0},
            {"Immediate Respawn", "", "createWorldScreen.immediaterespawn.description",
             "Skip the death screen and respawn immediately.", 0},
            {"Skip Night By Sleeping", "", "createWorldScreen.playerSleepPercentage.description",
             "Percentage of players that must be in bed to skip to morning.", 0},
            {"Respawn Radius", "", "createWorldScreen.respawnRadius.description",
             "Randomly respawn within this many blocks of where you died.", 0},
            {"Activate Cheats", "", "",
             "Allow commands and cheats in this world. Only operators can change this.", 0},
            {"Normal Day/Night Cycle", "", "createWorldScreen.daylightcycle.description",
             "Turn off to freeze the day/night cycle.", 0},
            {"Always Day", "", "createWorldScreen.alwaysDay.description",
             "Night never comes, it is always noon.", 0},
            {"Keep Inventory", "", "createWorldScreen.keepInventory.description",
             "Keep all items in the inventory when you die.", 0},
            {"Mob Spawn Normally", "", "createWorldScreen.mobSpawn.description",
             "Turn off to prevent mobs from spawning.", 0},
            {"Mob Griefing", "", "createWorldScreen.mobgriefing.description",
             "Mobs can break blocks and interfere with the environment.", 0},
            {"Entities Drop Loot", "", "createWorldScreen.entitiesdroploot.description",
             "Non-mob entities, like paintings, drop items when destroyed.", 0},
            {"Normal Weather Cycle", "", "createWorldScreen.weathercycle.description",
             "Like rain, snow, and thunderstorms.", 0},
            {"Command Blocks Enabled", "", "createWorldScreen.commandblocksenabled.description",
             "These are special programmable blocks.", 0},
            {"Difficulty", "options.difficulty", "", "How hard the world is. Peaceful removes hostile mobs.",
             0},
            {"Default Mode", "", "createWorldScreen.gameMode.default.description",
             "Select a game mode for players entering your world.", 0},
            {"Your Game Mode", "", "createWorldScreen.gameMode.personal.description",
             "Select your preferred game mode.", 0},
        };

    }

    bool FindSettingInfo(const std::string& label, SettingInfo& out) {
        const std::string l = str::Lower(label);
        for (const Entry& e : kEntries) {
            bool match = l == str::Lower(e.english);
            if (!match && *e.labelKey) {
                const std::string t = i18n::Tr(e.labelKey);
                match = t != e.labelKey && str::Lower(t) == l;
            }
            if (!match)
                continue;
            std::string d;
            if (*e.descKey) {
                d = i18n::Tr(e.descKey);
                if (d == e.descKey)
                    d.clear();
            }
            out.description = d.empty() ? e.fallback : d;
            out.impact = e.impact;
            return true;
        }
        return false;
    }
}
