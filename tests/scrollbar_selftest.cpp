#include "client/ui/Scrollbar.hpp"

#include <cmath>
#include <cstdio>

using namespace perlin;

namespace {
    int failures = 0;

    void Check(bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL %s\n", what);
            failures++;
        }
    }

    bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }
}

int main() {
    const ui::Scrollbar bar = ui::MakeScrollbar(400, 40, 200, 800, 0, 600);
    Check(Near(bar.thumbH, 50.0f), "the thumb shows the visible share of the list");
    Check(Near(bar.thumbY, 40.0f), "an unscrolled list puts the thumb at the top");
    Check(bar.Contains(403, 100), "a press on the track is inside the bar");
    Check(!bar.Contains(399, 100) && !bar.Contains(406, 100), "presses beside the track miss the bar");
    Check(!bar.Contains(403, 39) && !bar.Contains(403, 240),
          "presses above and below the track miss the bar");

    const float grab = bar.Grab(50);
    Check(Near(grab, 10.0f), "pressing the thumb holds it where it was pressed");
    Check(Near(bar.ScrollFor(50, grab), 0.0f), "pressing the thumb does not move the list");
    Check(Near(bar.ScrollFor(125, grab), 300.0f), "dragging the thumb halfway scrolls halfway");
    Check(Near(bar.ScrollFor(500, grab), 600.0f), "dragging past the end stops at the bottom");
    Check(Near(bar.ScrollFor(-100, grab), 0.0f), "dragging past the start stops at the top");

    const float trackGrab = bar.Grab(140);
    Check(Near(trackGrab, 25.0f), "pressing the track grabs the middle of the thumb");
    Check(Near(bar.ScrollFor(140, trackGrab), 300.0f), "pressing the track moves the thumb to the pointer");

    const ui::Scrollbar scrolled = ui::MakeScrollbar(400, 40, 200, 800, 300, 600);
    Check(Near(scrolled.thumbY, 115.0f), "the thumb follows the scroll position");
    Check(Near(scrolled.ScrollFor(130, scrolled.Grab(130)), 300.0f),
          "grabbing a scrolled thumb keeps the list in place");

    const ui::Scrollbar idle = ui::MakeScrollbar(400, 40, 200, 150, 0, 0);
    Check(!idle.Contains(403, 100), "a list that fits has no bar to press");
    Check(Near(idle.ScrollFor(200, 0), 0.0f), "a list that fits never scrolls");

    const ui::Scrollbar tiny = ui::MakeScrollbar(400, 40, 200, 100000, 0, 99800);
    Check(Near(tiny.thumbH, 16.0f), "a very long list keeps a thumb large enough to grab");

    std::printf(failures ? "%d failure(s)\n" : "scrollbar_selftest ok\n", failures);
    return failures ? 1 : 0;
}
