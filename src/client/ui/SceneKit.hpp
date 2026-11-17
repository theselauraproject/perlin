#pragma once
#include "client/ui/Scene.hpp"

#include <string>
#include <vector>

namespace perlin::ui::kit {
    std::vector<SceneRun> Plain(const std::string& text, const std::string& color = {});
    std::vector<SceneRun> Formatted(const std::string& text, const std::string& color = {});
    std::vector<SceneRun> Auto(const std::string& text, bool formatted, const std::string& color = {});

    SceneNode& Box(SceneBuilder& b, float x, float y, float w, float h, const std::string& cls = {},
                   const std::string& id = {}, const std::string& z = "auto");
    SceneNode& Begin(SceneBuilder& b, float x, float y, float w, float h, const std::string& cls = {},
                     const std::string& id = {}, const std::string& z = "auto");
    SceneNode& Rect(SceneBuilder& b, float x, float y, float w, float h, const std::string& color,
                    const std::string& z = "auto", const std::string& cls = {}, const std::string& id = {});
    SceneNode& Gradient(SceneBuilder& b, float x, float y, float w, float h, const std::string& decorator,
                        const std::string& z = "auto");
    SceneNode& Label(SceneBuilder& b, float x, float y, float w, const std::string& cls,
                     std::vector<SceneRun> runs, const std::string& z = "auto", const std::string& id = {});
    SceneNode& Image(SceneBuilder& b, float x, float y, float w, float h, const std::string& src,
                     const std::string& z = "auto", const std::string& id = {});
    SceneNode& Item(SceneBuilder& b, float x, float y, float w, float h, const std::string& key,
                    const std::string& z = "auto", const std::string& id = {});
    SceneNode& Button(SceneBuilder& b, const std::string& id, float x, float y, float w,
                      std::vector<SceneRun> label, bool off = false);
    SceneNode& Slider(SceneBuilder& b, const std::string& id, float x, float y, float w,
                      std::vector<SceneRun> label, float fraction);
    std::string Px(float v);
    void Backdrop(SceneBuilder& b, float w, float h, bool panorama);
    SceneNode& Dim(SceneBuilder& b, float w, float h, int alpha);
    void Append(std::vector<SceneNode>& dst, std::vector<SceneNode>& src);
    SceneNode& Container(std::vector<SceneNode>& dst, const std::string& id, float x, float y, float w, float h,
                         const std::string& z, std::vector<SceneNode> children = {});
    void TopLayer(std::vector<SceneNode>& dst, float w, float h, const std::string& z, std::vector<SceneNode>& cursor,
                  std::vector<SceneNode>& tooltip);
    SceneNode& Heading(SceneBuilder& b, float guiW, float y, const std::string& title);
}
