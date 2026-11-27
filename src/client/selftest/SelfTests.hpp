#pragma once
#include <string_view>

namespace perlin::selftest {
    int HandlerDispatch();
    int ArmorPackets();
    int ServerUiPackets();
    int AudioPackets();
    int InputReporting();
    int MovementProtocol();
    int MovementEffects();
    int AttributeReplay();
    int StatusEffectReplay();
    int RangedItem();
    int ItemUsePackets();
    int CreativeInventory();
    int CameraEffectsPackets();
    int SessionCodecContext();
    int ChunkPipeline();
    int RecipePackets();
    int TitleController();
    int PackManagerTest();
    int ServerBrowser();
    int AudioDirectorTest();
    int LightBandCache();
    int DebugOverlay();
    int PlayerInteraction();
    int HudScreen();
    int Studio();
    int Worldview();
    int TitlePorts();
    int ConnectionControllers(bool preview);
    int Run(std::string_view name);
}
