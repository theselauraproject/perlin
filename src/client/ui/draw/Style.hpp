#pragma once
#include "client/ui/Scene.hpp"
#include "client/ui/draw/Color.hpp"

#include <string>
#include <unordered_set>
#include <vector>

namespace perlin::ui::draw {
    using HoverSet = std::unordered_set<const SceneNode*>;

    struct Style {
        bool hasBg = false;
        Color bg;
        Color color = Rgb(0xffffff);
        std::string family = "minecraft-shadow";
        float size = 8;
        float line = 9;
        int align = 0;
        bool nowrap = true;
        bool pre = false;
        bool clip = false;
        bool hidden = false;
        bool pointer = true;
        float border = 0;
        Color borderColor;
        bool hoverDependent = false;
    };

    bool HasClass(const SceneNode& n, const char* cls);
    Style ResolveStyle(Theme theme, const SceneNode& n, const Style& parent,
                       const std::vector<const SceneNode*>& ancestors, const HoverSet& hover);
}
