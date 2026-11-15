#pragma once
#include "client/ui/Scene.hpp"
#include "client/ui/draw/Canvas.hpp"
#include "client/ui/draw/Style.hpp"

#include <vector>

namespace perlin::ui::draw {
    struct Hit {
        const SceneNode* node = nullptr;
        int parent = -1;
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        float cx0 = -1e9f, cy0 = -1e9f, cx1 = 1e9f, cy1 = 1e9f;
        bool pointer = true;
    };

    struct Shift {
        std::string id;
        float x = 0, y = 0;
    };

    struct Rendered {
        std::vector<Batch> batches;
        std::vector<Hit> hits;
        float panoSpan = 0;
        std::vector<Shift> shifts;
        bool usesHover = false;
        bool starved = false;
        uint32_t fontStamp = 0;
    };

    void RenderScene(const Scene& scene, const HoverSet& hover, TextureSource& textures, int guiW, int guiH,
                     Rendered& out);
    int HitTest(const Rendered& r, float x, float y);
    void HoverChain(const Rendered& r, int index, HoverSet& out);
    float PanoOffset(float span, double seconds);
}
