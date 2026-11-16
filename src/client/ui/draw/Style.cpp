#include "client/ui/draw/Style.hpp"

#include <cstring>
#include <string_view>

namespace perlin::ui::draw {
    bool HasClass(const SceneNode& n, const char* cls) {
        const std::string_view all = n.cls;
        const std::string_view want = cls;
        size_t at = 0;
        while (at < all.size()) {
            while (at < all.size() && all[at] == ' ')
                at++;
            size_t end = all.find(' ', at);
            if (end == std::string_view::npos)
                end = all.size();
            if (all.substr(at, end - at) == want)
                return true;
            at = end;
        }
        return false;
    }

    namespace {
        struct Ctx {
            const SceneNode& n;
            const std::vector<const SceneNode*>& anc;
            const HoverSet& hover;
            Style& s;

            bool Has(const char* c) const { return HasClass(n, c); }
            const SceneNode* Ancestor(const char* c) const {
                for (auto it = anc.rbegin(); it != anc.rend(); ++it)
                    if (HasClass(**it, c))
                        return *it;
                return nullptr;
            }
            bool Hot(const SceneNode* node) const { return node && hover.contains(node); }
            bool SelfHot() const { return hover.contains(&n); }
            void Bg(uint32_t rgb, uint8_t a = 255) {
                s.hasBg = true;
                s.bg = Rgb(rgb, a);
            }
            void Text(uint32_t rgb) { s.color = Rgb(rgb); }
            void OnlyOnHover(const char* self, const char* host) {
                if (!Has(self))
                    return;
                const SceneNode* h = Ancestor(host);
                s.hoverDependent = true;
                if (!Hot(h))
                    s.hidden = true;
            }
        };

        void Colors(Ctx& c, uint32_t gray, uint32_t dim) {
            if (c.Has("gray"))
                c.Text(gray);
            if (c.Has("dim"))
                c.Text(dim);
            if (c.Has("green"))
                c.Text(0x55ff55);
            if (c.Has("yellow"))
                c.Text(0xffff55);
            if (c.Has("red"))
                c.Text(0xff5555);
            if (c.Has("dark"))
                c.Text(0x606060);
        }

        void Aligns(Ctx& c) {
            if (c.Has("right") || c.Has("r"))
                c.s.align = 2;
            if (c.Has("ctr") || c.Has("c") || c.Has("center"))
                c.s.align = 1;
        }

        void Title(Ctx& c) {
            if (c.Has("h")) {
                c.s.hoverDependent = true;
                if (!c.Hot(c.Ancestor("btn")) && !c.Hot(c.Ancestor("sbtn")))
                    c.s.hidden = true;
            }
            c.OnlyOnHover("tip", "btn");
            c.OnlyOnHover("pt", "pg");
            c.OnlyOnHover("go", "row");
            c.OnlyOnHover("hl", "acc");
            if (c.Has("panoclip"))
                c.Bg(0x000000);
            if (c.Has("blab")) {
                c.Text(0xffffff);
                if (c.Ancestor("btn") && HasClass(*c.Ancestor("btn"), "off"))
                    c.Text(0xa0a0a0);
                if (c.Ancestor("sbtn"))
                    c.Text(0xffffff);
            }
            Colors(c, 0x808080, 0xa0a0a0);
            Aligns(c);
        }

        void Menu(Ctx& c) {
            if (c.Has("g"))
                c.Text(0xa0a0a0);
            Aligns(c);
            if (c.Has("mini")) {
                c.Bg(c.SelfHot() && !c.Has("off") ? 0xffffff : 0x000000);
                c.s.hoverDependent = true;
            }
            if (c.Has("panel")) {
                c.s.clip = true;
                c.Bg(0x000000, 170);
            }
            if (c.Has("prow")) {
                c.s.hoverDependent = true;
                if (c.SelfHot())
                    c.Bg(0xffffff, 40);
            }
            if (c.Has("sel"))
                c.Bg(0x000000);
            if (c.Has("sep"))
                c.Bg(0xffffff, 60);
            if (c.Has("sepb"))
                c.Bg(0x000000, 191);
            if (c.Has("track"))
                c.Bg(0x000000);
            if (c.Has("thumb"))
                c.Bg(0x808080);
            if (c.Has("wrap")) {
                c.Text(c.Has("open") ? 0xe0e0e0 : 0xa0a0a0);
                c.s.nowrap = false;
                c.s.clip = !c.Has("open");
            }
            if (c.Has("srow")) {
                c.s.hoverDependent = true;
                if (c.SelfHot())
                    c.Bg(0xffffff, 30);
            }
            if (c.Has("noicon"))
                c.Bg(0x555555);
            if (c.Has("badge"))
                c.Bg(0x000000, 150);
            if (c.Has("modframe"))
                c.Bg(0xffffff);
            else if (c.Has("frame"))
                c.Bg(0xd0d0d0);
            if (c.Has("listline"))
                c.Bg(0xffffff, 200);
            if (c.Has("listback"))
                c.Bg(0x000000, 60);
            if (c.Has("listclip"))
                c.s.clip = true;
            if (c.Has("rowselect"))
                c.Bg(0x000000, 110);
            if (c.Has("rowtoggle"))
                c.Bg(0x000000, 150);
            if (c.Has("searchedge"))
                c.Bg(0xa0a0a0);
            if (c.Has("searchfill"))
                c.Bg(0x000000);
            if (c.Has("progresspanel")) {
                c.Bg(0x000000, 215);
                c.s.border = 1;
                c.s.borderColor = Rgb(0xa0a0a0);
            }
            if (c.Has("progresstrack"))
                c.Bg(0x404040);
            if (c.Has("progressfill"))
                c.Bg(0x80ff20);
            if (c.Has("cardsel"))
                c.Bg(0xffffff, 34);
            if (c.Has("cardline"))
                c.Bg(0xf0f0f0);
            if (c.Has("hintback"))
                c.Bg(0x000000, 130);
            const SceneNode* ico = c.Ancestor("ico");
            if (c.Has("ov")) {
                c.s.hoverDependent = true;
                if (c.Hot(ico))
                    c.Bg(0x909090, 160);
                else
                    c.s.hidden = true;
            }
            if (c.Has("ch")) {
                c.s.hoverDependent = true;
                if (!c.Hot(ico))
                    c.s.hidden = true;
                else
                    c.Bg(c.Hot(c.Ancestor("zone")) ? 0xffffff : 0xa0a0a0);
            }
            if (c.Has("zone")) {
                c.s.hoverDependent = true;
                if (!c.Hot(ico))
                    c.s.hidden = true;
            }
        }

        void Pause(Ctx& c) {
            if (c.Has("ppa"))
                c.Bg(0xc6c6c6);
            if (c.Has("ppb"))
                c.Bg(0xffffff);
            if (c.Has("ppc"))
                c.Bg(0x373737);
            if (c.Has("pptitle"))
                c.s.align = 1;
            if (c.Has("ppname"))
                c.s.clip = true;
            if (c.Has("pprole"))
                c.s.align = 2;
        }

        void List(Ctx& c) {
            if (c.Has("sub"))
                c.Text(0xa0a0a0);
            if (c.Has("head")) {
                c.s.align = 1;
                c.Text(0xffff55);
            }
            if (c.Has("sep"))
                c.Bg(0xffffff, 51);
            if (c.Has("sepb"))
                c.Bg(0x000000, 191);
            if (c.Has("sbtrack"))
                c.Bg(0x000000);
            if (c.Has("sbthumb") || c.Has("wthumb"))
                c.Bg(0x808080);
            if (c.Has("lpanel"))
                c.Bg(0x000000, 112);
            if (c.Has("wpanel"))
                c.Bg(0x484848);
            if (c.Has("wline"))
                c.Bg(0x2e2e2e);
            if (c.Has("wedge"))
                c.Bg(c.Has("editing") ? 0xffffff : 0x1e1e1e);
            if (c.Has("wtrack"))
                c.Bg(c.Has("on") ? 0x3b8526 : 0x555555);
            if (c.Has("wseg"))
                c.Bg(c.Has("on") ? 0x3b8526 : 0xd0d0d0);
            if (c.Has("wfill"))
                c.Bg(0x3b8526);
            if (c.Has("wknob0"))
                c.Bg(0x8a8a8a);
            if (c.Has("wknob1"))
                c.Bg(0xd0d0d0);
            if (c.Has("wfield"))
                c.Bg(0x313233);
            if (c.Has("wd"))
                c.Text(0xa0a0a0);
            if (c.Has("seg"))
                c.s.align = 1;
        }

        void Keybinds(Ctx& c) {
            if (c.Has("head"))
                c.s.align = 1;
            if (c.Has("sep"))
                c.Bg(0xffffff, 51);
            if (c.Has("sepb"))
                c.Bg(0x000000, 191);
            if (c.Has("track"))
                c.Bg(0x000000);
            if (c.Has("thumb"))
                c.Bg(0x808080);
        }

        void Options(Ctx& c) {
            if (c.Has("hdr")) {
                c.s.align = 1;
                c.Text(0xffff55);
            }
            if (c.Has("tip0"))
                c.Bg(0x100010);
            if (c.Has("tip1"))
                c.Bg(0x2d0a63);
            if (c.Has("tiptx") || c.Has("tipimp"))
                c.s.line = 10;
            if (c.Has("tipimp"))
                c.Text(0xaaaaaa);
            if (c.Has("sbtrack"))
                c.Bg(0x000000);
            if (c.Has("sbthumb"))
                c.Bg(0x808080);
        }

        void Slots(Ctx& c, bool inventory) {
            if (c.Has("label")) {
                c.s.family = "minecraft";
                c.Text(0x404040);
            }
            if (c.Has("search"))
                c.Text(0xffffff);
            if (c.Has("slot") || c.Has("hl")) {
                const SceneNode* slot = c.Ancestor("slot");
                if (c.Has("hl")) {
                    c.s.hoverDependent = true;
                    c.s.pointer = false;
                    if (!c.Hot(slot))
                        c.s.hidden = true;
                    else
                        c.Bg(0xffffff, 128);
                }
            }
            if (c.Has("cnt"))
                c.s.align = 2;
            if (c.Has("fxd"))
                c.Text(0x7f7f7f);
            if (c.Has("ticon"))
                c.s.pointer = false;
            if (inventory) {
                c.OnlyOnHover("rh", "rbtn");
                c.OnlyOnHover("ph", "pg");
            }
            if (c.Has("txt"))
                c.s.line = 10;
            if (c.n.id == "tip" || c.n.id == "cursor" || c.n.id == "top")
                c.s.pointer = false;
        }

        void Engine(Ctx& c) {
            if (c.Has("t")) {
                c.s.family = "minecraft";
                c.s.nowrap = true;
                c.s.pre = true;
                if (c.Has("s"))
                    c.s.family = c.Has("ten") ? "minecraft-ten-shadow" : "minecraft-shadow";
                else if (c.Has("o"))
                    c.s.family = c.Has("ten") ? "minecraft-ten-outline" : "minecraft-outline";
                else if (c.Has("ten"))
                    c.s.family = "minecraft-ten";
            }
            if (c.Has("bt"))
                c.s.pointer = true;
            c.OnlyOnHover("ul", "link");
            c.OnlyOnHover("hv", "bt");
            if (c.Has("df")) {
                c.s.hoverDependent = true;
                if (c.Hot(c.Ancestor("bt")))
                    c.s.hidden = true;
            }
        }

        void Misc(Ctx& c, Theme theme) {
            switch (theme) {
            case Theme::Replay:
                if (c.Has("gray"))
                    c.Text(0xaaaaaa);
                if (c.Has("yellow"))
                    c.Text(0xffff55);
                if (c.Has("green"))
                    c.Text(0x55ff55);
                Aligns(c);
                break;
            case Theme::Server:
                Aligns(c);
                break;
            case Theme::Form: {
                Aligns(c);
                const SceneNode* btn = c.Ancestor("btn");
                const bool hot = c.Has("btn") ? c.SelfHot() : c.Hot(btn);
                if (c.Has("bg"))
                    c.Bg(0x000000, 150);
                if (c.Has("panel"))
                    c.Bg(0x101010, 235);
                if (c.Has("t"))
                    c.s.pre = true;
                if (c.Has("btn")) {
                    c.s.hoverDependent = true;
                    c.Bg(hot ? 0xffffff : 0x000000);
                }
                if (btn && !c.Has("btn"))
                    c.s.hoverDependent = true;
                if (c.Has("rim"))
                    c.Bg(hot ? 0xafafaf : 0xaaaaaa);
                if (c.Has("face"))
                    c.Bg(hot ? 0x757575 : 0x6f6f6f);
                if (c.Has("shade") || c.Has("edge"))
                    c.Bg(0x565656);
                if (c.Has("lbl")) {
                    c.s.align = 1;
                    c.s.pre = true;
                    c.s.clip = true;
                }
                if (c.Has("field"))
                    c.Bg(0xffffff);
                if (c.Has("in"))
                    c.Bg(c.Ancestor("focus") ? 0x101030 : 0x000000);
                if (c.Has("txt")) {
                    c.Text(0xe0e0e0);
                    c.s.nowrap = true;
                    c.s.clip = true;
                }
                break;
            }
            case Theme::Download:
                if (c.Has("dim"))
                    c.Text(0x9a9ca6);
                if (c.Has("bad"))
                    c.Text(0xff6b6b);
                if (c.Has("track"))
                    c.Bg(0x05060a);
                if (c.Has("fill"))
                    c.Bg(c.Has("bad") ? 0xc84a4a : 0x62d26a);
                Aligns(c);
                break;
            default:
                break;
            }
        }
    }

    Style ResolveStyle(Theme theme, const SceneNode& n, const Style& parent,
                       const std::vector<const SceneNode*>& anc, const HoverSet& hover) {
        Style s;
        s.family = parent.family;
        s.size = parent.size;
        s.line = parent.line;
        s.color = parent.color;
        s.align = parent.align;
        s.nowrap = parent.nowrap;
        s.pre = parent.pre;
        s.pointer = parent.pointer;
        Ctx c{n, anc, hover, s};
        if (c.Has("clip"))
            s.clip = true;
        if (c.Has("ctr") || c.Has("center"))
            s.align = 1;
        if (c.Has("right"))
            s.align = 2;
        if (c.Has("big")) {
            s.size = 16;
            s.line = 17;
        }
        if (c.Has("nowrap"))
            s.nowrap = true;
        if (n.id == "tip" || n.id == "cursor" || n.id == "top")
            s.pointer = false;
        if (!n.cls.empty() || theme == Theme::Hud) {
            switch (theme) {
            case Theme::Title:
                Title(c);
                break;
            case Theme::Menu:
                Menu(c);
                break;
            case Theme::Pause:
                Pause(c);
                break;
            case Theme::List:
                List(c);
                break;
            case Theme::Keybinds:
                Keybinds(c);
                break;
            case Theme::Options:
                Options(c);
                break;
            case Theme::Inventory:
                Slots(c, true);
                break;
            case Theme::Creative:
            case Theme::Chest:
                Slots(c, false);
                break;
            case Theme::Hud:
                Engine(c);
                break;
            default:
                Misc(c, theme);
                break;
            }
            if (theme != Theme::Hud && theme != Theme::Title && (c.Has("link") || c.Has("ul")))
                c.OnlyOnHover("ul", "link");
        }
        return s;
    }
}
