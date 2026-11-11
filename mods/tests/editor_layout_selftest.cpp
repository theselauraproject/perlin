#include <cassert>
#include <cstdio>
#include "client/ui/ReplayScreen.hpp"
using namespace perlin::ui;
int main() {
    for (int gw : {320, 427, 640, 960}) {
        const int gh = gw * 9 / 16;
        EditorModel m;
        m.durationMs = 80000;
        m.viewSpanMs = 80000;
        m.timeMs = 4810;
        m.cameraLabel = "Cam: Free";
        m.keyMs[0] = {1000, 5000, 20000};
        m.keyMs[1] = {3000};
        m.markerMs = {2000};
        m.selTrack = 0;
        m.selIndex = 1;
        for (int i = 0; i < 20; i++) {
            EditorRow r;
            r.kind = i % 3 == 0 ? EditorRow::Kind::Cycle : EditorRow::Kind::Toggle;
            r.id = "r" + std::to_string(i);
            r.label = "Row";
            r.value = "1";
            m.rows.push_back(r);
        }
        const EditorBuilt b = BuildEditorRml(m, gw, gh);
        const EditorLayout l = ComputeEditorLayout(gw, gh);
        assert(!b.rml.empty() && b.rml.contains("</rml>"));
        bool play = false, key = false, ruler = false;
        for (const EditorHit& h : b.hits) {
            play = play || h.id == "t_play";
            key = key || h.id == "key:0:1";
            ruler = ruler || h.id == "ruler";
            assert(h.w > 0 && h.h > 0);
            if (h.x + h.w > l.guiW + 1)
                std::fprintf(stderr, "overflow gw=%d id=%s x=%.0f w=%.0f\n", gw, h.id.c_str(), h.x, h.w);
            assert(h.x + h.w <= l.guiW + 1);
        }
        assert(play && key && ruler);
        assert(l.sideVisibleRows >= 1 && l.tlX1 > l.tlX0 + 50);
        const double t = EditorTimeAtX(l, m, EditorXAtTime(l, m, 12345));
        assert(t > 12344 && t < 12346);
    }
    std::puts("editor_layout_selftest ok");
}
