#pragma once
#include <string>

namespace perlin::ui::ore {
    enum class Kind { Primary, Neutral, Secondary, Destructive };

    const std::string& Styles();

    std::string Button(const std::string& id, int x, int y, int w, int h, const std::string& label, Kind kind,
                       bool big = false, bool disabled = false);

    std::string IconButton(const std::string& id, int x, int y, int size, const std::string& iconSrc,
                           Kind kind);

    std::string Card(int x, int y, int w, int h, int z = 2);

    std::string Label(int x, int y, int w, const std::string& text, const char* align = "left",
                      bool muted = false, bool big = false);
}
