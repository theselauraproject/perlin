#pragma once
#include <vector>
#include "client/resources/TextureLoader.hpp"

namespace perlin::ui {
    struct SkinView {
        const std::vector<unsigned char>* skin = nullptr;
        int skinW = 0, skinH = 0;
        const std::vector<unsigned char>* cape = nullptr;
        int capeW = 0, capeH = 0;
        bool slim = false;
        float yaw = 0.0f, pitch = 0.0f;
        // head rotation vs body, degrees (positive yaw same way as `yaw`)
        float headYaw = 0.0f, headPitch = 0.0f;
        bool showBody = true;
        bool jacket = true, leftSleeve = true, rightSleeve = true, leftPants = true, rightPants = true,
             hat = true;
    };
    Texture RenderSkin3D(const SkinView& view, int outW, int outH);
    bool GuessSlimSkin(const std::vector<unsigned char>& skin, int skinW, int skinH);
}
