#include <perlin/plugin.hpp>

#include <mod_settings.hpp>

#include <vector>

using namespace perlin;

struct Sketchpad : Plugin {
    void onEnable(PluginCtx& c) override {
        c.bind<"toggle">("Toggle sketchpad", Key::F8, this, &Sketchpad::toggle);
        c.on_press(settings::clear, [this] {
            dots.clear();
            dirty = true;
        });
    }
    void onDisable(PluginCtx&) override { (void)gfx::clear("sketch"); }

    void toggle() {
        open = !open;
        (void)(open ? input::grab(true, true) : input::release());
        dirty = true;
    }

    void onMouseMove(const abi::EventMouseMove& e) {
        mx = e.x, my = e.y;
        if (drawing && inside())
            dots.push_back({mx, my});
        dirty = true;
    }
    void onMouseButton(const abi::EventMouseButton& e) {
        if (e.button == 0)
            drawing = e.down && inside();
        if (drawing)
            dots.push_back({e.x, e.y});
        dirty = true;
    }
    void onKeyRaw(const abi::EventKeyRaw& e) {
        if (e.down && e.key == static_cast<int32_t>(input::Code::Escape) && open)
            toggle();
    }
    void onFrame(const abi::EventFrame&) {
        if (!dirty)
            return;
        dirty = false;
        if (!open) {
            (void)gfx::clear("sketch");
            return;
        }
        gfx::DrawList list;
        list.batch(0, px, py, px + pw, py + ph);
        list.rect(px, py, px + pw, py + ph, gfx::rgba8(20, 20, 28, 220));
        const float s = static_cast<float>(settings::brush.get());
        for (auto [x, y] : dots)
            list.rect(x - s / 2, y - s / 2, x + s / 2, y + s / 2, gfx::rgba8(255, 200, 60));
        list.rect(mx - 2, my - 2, mx + 2, my + 2, gfx::rgba8(255, 255, 255));
        (void)gfx::submit("sketch", 0, list);
    }

private:
    bool inside() const { return mx >= px && mx < px + pw && my >= py && my < py + ph; }
    bool open = false, drawing = false, dirty = false;
    float mx = 0, my = 0, px = 80, py = 80, pw = 480, ph = 320;
    std::vector<std::pair<float, float>> dots;
};
PERLIN_REGISTER(Sketchpad)
