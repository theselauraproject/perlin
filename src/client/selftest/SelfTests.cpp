#include "client/selftest/SelfTests.hpp"

#include <cstdio>

namespace perlin::selftest {
    int Run(std::string_view name) {
        if (name == "handler_dispatch")
            return HandlerDispatch();
        if (name == "armor_packets")
            return ArmorPackets();
        if (name == "server_ui_packets")
            return ServerUiPackets();
        if (name == "audio_packets")
            return AudioPackets();
        if (name == "input_reporting")
            return InputReporting();
        if (name == "movement_protocol")
            return MovementProtocol();
        if (name == "movement_effects")
            return MovementEffects();
        if (name == "attribute_replay")
            return AttributeReplay();
        if (name == "status_effect_replay")
            return StatusEffectReplay();
        if (name == "ranged_item")
            return RangedItem();
        if (name == "item_use_packets")
            return ItemUsePackets();
        if (name == "creative_inventory")
            return CreativeInventory();
        if (name == "camera_effects_packets")
            return CameraEffectsPackets();
        if (name == "session_codec_context")
            return SessionCodecContext();
        if (name == "chunk_pipeline")
            return ChunkPipeline();
        if (name == "recipe_packets")
            return RecipePackets();
        if (name == "title_controller")
            return TitleController();
        if (name == "pack_manager")
            return PackManagerTest();
        if (name == "server_browser")
            return ServerBrowser();
        if (name == "audio_director")
            return AudioDirectorTest();
        if (name == "light_band_cache")
            return LightBandCache();
        if (name == "debug_overlay")
            return DebugOverlay();
        if (name == "player_interaction")
            return PlayerInteraction();
        if (name == "hud_screen" || name == "hud_screen_preview")
            return HudScreen();
        if (name == "studio")
            return Studio();
        if (name == "worldview" || name == "worldview_preview")
            return Worldview();
        if (name == "title_ports")
            return TitlePorts();
        if (name == "connection_controllers" || name == "connection_controllers_preview")
            return ConnectionControllers(name == "connection_controllers_preview");
        std::fprintf(stderr, "unknown selftest: %.*s\n", static_cast<int>(name.size()), name.data());
        return 2;
    }
}
