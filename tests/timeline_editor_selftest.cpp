#include "client/systems/replay/TimelineEditor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

using namespace perlin::replay;

namespace {
    int failures = 0;
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            std::printf("FAIL line %d: %s\n", __LINE__, #c);                                                 \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

    const float kValueLadder[] = {0.05f, 0.1f, 0.25f, 0.5f,  0.75f, 1.0f,  1.5f,
                                  2.0f,  4.0f, 8.0f,  16.0f, 32.0f, 64.0f, 100.0f};

    struct Reference {
        Timeline timeline;
        int selTrack = -1, selIndex = -1;
        bool dirty = false;

        int count(int tr) const {
            return tr == 0   ? static_cast<int>(timeline.camera.size())
                   : tr == 1 ? static_cast<int>(timeline.speed.size())
                             : static_cast<int>(timeline.timeOfDay.size());
        }
        double keyT(int tr, int i) const {
            return tr == 0   ? timeline.camera[static_cast<size_t>(i)].tMs
                   : tr == 1 ? timeline.speed[static_cast<size_t>(i)].tMs
                             : timeline.timeOfDay[static_cast<size_t>(i)].tMs;
        }
        void setKeyT(int tr, int i, double v) {
            (tr == 0   ? timeline.camera[static_cast<size_t>(i)].tMs
             : tr == 1 ? timeline.speed[static_cast<size_t>(i)].tMs
                       : timeline.timeOfDay[static_cast<size_t>(i)].tMs) = v;
        }
        Interp interpOf(int tr, int i) const {
            return tr == 0   ? timeline.camera[static_cast<size_t>(i)].interp
                   : tr == 1 ? timeline.speed[static_cast<size_t>(i)].interp
                             : timeline.timeOfDay[static_cast<size_t>(i)].interp;
        }
        void setInterp(int tr, int i, Interp v) {
            (tr == 0   ? timeline.camera[static_cast<size_t>(i)].interp
             : tr == 1 ? timeline.speed[static_cast<size_t>(i)].interp
                       : timeline.timeOfDay[static_cast<size_t>(i)].interp) = v;
        }
        bool selValid() const {
            return selTrack >= 0 && selTrack < 3 && selIndex >= 0 && selIndex < count(selTrack);
        }
        void resort(double selectedT) {
            const auto byT = [](const auto& a, const auto& b) { return a.tMs < b.tMs; };
            std::ranges::stable_sort(timeline.camera, byT);
            std::ranges::stable_sort(timeline.speed, byT);
            std::ranges::stable_sort(timeline.timeOfDay, byT);
            if (selTrack >= 0 && selTrack < 3)
                for (int i = 0; i < count(selTrack); i++)
                    if (std::fabs(keyT(selTrack, i) - selectedT) < 0.5) {
                        selIndex = i;
                        break;
                    }
            dirty = true;
        }
        void addCamera(const CamKey& ck) {
            timeline.SetCamera(ck);
            dirty = true;
            selTrack = 0;
            for (int i = 0; i < count(0); i++)
                if (std::fabs(keyT(0, i) - ck.tMs) < 51.0)
                    selIndex = i;
        }
        void deleteSelected() {
            if (!selValid())
                return;
            if (selTrack == 0)
                timeline.camera.erase(timeline.camera.begin() + selIndex);
            else if (selTrack == 1)
                timeline.speed.erase(timeline.speed.begin() + selIndex);
            else
                timeline.timeOfDay.erase(timeline.timeOfDay.begin() + selIndex);
            selIndex = -1;
            dirty = true;
        }
        void sideAction(const std::string& base, int dir, bool fine, double dur, double t, double px,
                        double py, double pz, float yaw, float pitch, float ticks) {
            if (base == "s_time" && selValid()) {
                const double nt =
                    std::clamp(keyT(selTrack, selIndex) + dir * (fine ? 10.0 : 100.0), 0.0, dur);
                setKeyT(selTrack, selIndex, nt);
                resort(nt);
            } else if (base == "s_interp" && selValid()) {
                setInterp(
                    selTrack, selIndex,
                    static_cast<Interp>((static_cast<int>(interpOf(selTrack, selIndex)) + dir + 6) % 6));
                dirty = true;
            } else if (base == "s_fov" && selValid() && selTrack == 0) {
                CamKey& ck = timeline.camera[static_cast<size_t>(selIndex)];
                ck.fov = std::clamp(ck.fov + static_cast<float>(dir) * (fine ? 1.0f : 5.0f), 5.0f, 170.0f);
                dirty = true;
            } else if (base == "s_roll" && selValid() && selTrack == 0) {
                CamKey& ck = timeline.camera[static_cast<size_t>(selIndex)];
                ck.roll =
                    std::clamp(ck.roll + static_cast<float>(dir) * (fine ? 1.0f : 5.0f), -180.0f, 180.0f);
                dirty = true;
            } else if (base == "s_value" && selValid() && selTrack == 1) {
                float& v = timeline.speed[static_cast<size_t>(selIndex)].value;
                int best = 0;
                for (int i = 1; i < 14; i++)
                    if (std::fabs(kValueLadder[i] - v) < std::fabs(kValueLadder[best] - v))
                        best = i;
                v = kValueLadder[std::clamp(best + dir, 0, 13)];
                dirty = true;
            } else if (base == "s_value" && selValid() && selTrack == 2) {
                float& v = timeline.timeOfDay[static_cast<size_t>(selIndex)].value;
                v = std::fmod(v + static_cast<float>(dir) * (fine ? 100.0f : 1000.0f) + 24000.0f, 24000.0f);
                dirty = true;
            } else if (base == "s_setview" && selValid() && selTrack == 0) {
                CamKey& ck = timeline.camera[static_cast<size_t>(selIndex)];
                ck.x = px;
                ck.y = py;
                ck.z = pz;
                ck.yaw = yaw;
                ck.pitch = pitch;
                dirty = true;
            } else if (base == "s_delete") {
                deleteSelected();
            } else if (base == "s_addspeed") {
                timeline.SetSpeed({t, 1.0f, Interp::Linear});
                selTrack = 1;
                for (int i = 0; i < count(1); i++)
                    if (std::fabs(keyT(1, i) - t) < 51.0)
                        selIndex = i;
                dirty = true;
            } else if (base == "s_addtime") {
                timeline.SetTimeOfDay({t, ticks, Interp::Linear});
                selTrack = 2;
                for (int i = 0; i < count(2); i++)
                    if (std::fabs(keyT(2, i) - t) < 51.0)
                        selIndex = i;
                dirty = true;
            }
        }
    };

    bool Apply(TimelineEditor& ed, const std::string& base, int dir, bool fine, double dur, double t,
               double px, double py, double pz, float yaw, float pitch, float ticks) {
        if (base == "s_time")
            return ed.NudgeTime(dir, fine, dur);
        if (base == "s_interp")
            return ed.CycleInterp(dir);
        if (base == "s_fov")
            return ed.NudgeFov(dir, fine);
        if (base == "s_roll")
            return ed.NudgeRoll(dir, fine);
        if (base == "s_value")
            return ed.StepSpeed(dir) || ed.NudgeTimeOfDay(dir, fine);
        if (base == "s_setview")
            return ed.SetPose(px, py, pz, yaw, pitch);
        if (base == "s_delete")
            return ed.DeleteSelected();
        if (base == "s_addspeed") {
            ed.AddSpeed(t, 1.0f);
            return true;
        }
        if (base == "s_addtime") {
            ed.AddTimeOfDay(t, ticks);
            return true;
        }
        return false;
    }

    void ExpectSame(const Reference& ref, const Timeline& tl, const KeySelection& sel, bool dirty, int step) {
        const bool same =
            ref.timeline.ToJson() == tl.ToJson() && ref.selTrack == sel.track && ref.selIndex == sel.index;
        if (!same || ref.dirty != dirty) {
            std::printf("FAIL differential at step %d (same=%d dirty ref=%d got=%d)\n", step, same, ref.dirty,
                        dirty);
            failures++;
            std::exit(1);
        }
    }

    void Differential(unsigned seed) {
        std::mt19937 rng(seed);
        const auto pick = [&](int n) { return static_cast<int>(rng() % static_cast<unsigned>(n)); };
        const char* const actions[] = {"s_time",    "s_interp", "s_fov",      "s_roll",    "s_value",
                                       "s_setview", "s_delete", "s_addspeed", "s_addtime", "s_none"};
        Reference ref;
        Timeline tl;
        KeySelection sel;
        const double dur = 60000.0;
        for (int step = 0; step < 1500; step++) {
            const bool fine = pick(2) == 0;
            const int dir = pick(2) ? 1 : -1;
            const double t = pick(600) * 100.0 + pick(3) * 10.0;
            const double px = pick(100), py = pick(100), pz = pick(100);
            const float yaw = static_cast<float>(pick(360)), pitch = static_cast<float>(pick(180) - 90),
                        ticks = static_cast<float>(pick(24000));
            TimelineEditor ed(tl, sel);
            bool dirty = false;
            ref.dirty = false;
            if (ref.timeline.camera.size() + ref.timeline.speed.size() + ref.timeline.timeOfDay.size() > 40) {
                ref.timeline.Clear();
                tl.Clear();
                ref.selTrack = ref.selIndex = -1;
                ed.Deselect();
            }
            switch (pick(5)) {
            case 0: {
                CamKey ck;
                ck.tMs = t;
                ck.fov = static_cast<float>(30 + pick(100));
                ck.roll = static_cast<float>(pick(40) - 20);
                ck.interp = static_cast<Interp>(pick(6));
                ref.addCamera(ck);
                ed.AddCamera(ck);
                dirty = true;
                break;
            }
            case 1: {
                const int track = pick(3);
                const int n = ref.count(track);
                if (n > 0) {
                    ref.selTrack = track;
                    ref.selIndex = pick(n);
                    ed.Select(track, ref.selIndex);
                }
                break;
            }
            case 2: {
                const double nt = std::clamp(t, 0.0, dur);
                if (ref.selValid()) {
                    ref.setKeyT(ref.selTrack, ref.selIndex, nt);
                    ref.resort(nt);
                }
                dirty = ed.MoveSelectedTo(nt);
                break;
            }
            default: {
                const std::string base = actions[pick(10)];
                const int repeats = pick(6) == 0 ? 60 : 1;
                for (int r = 0; r < repeats; r++) {
                    ref.sideAction(base, dir, fine, dur, t, px, py, pz, yaw, pitch, ticks);
                    dirty = Apply(ed, base, dir, fine, dur, t, px, py, pz, yaw, pitch, ticks) || dirty;
                    ExpectSame(ref, tl, sel, dirty, step);
                }
                break;
            }
            }
            ExpectSame(ref, tl, sel, dirty, step);
            if (pick(50) == 0) {
                sel.index = pick(6) - 2;
                ref.selIndex = sel.index;
                ed.DropInvalidSelection();
                if (!ref.selValid()) {
                    ref.selTrack = ref.selIndex = -1;
                }
                ExpectSame(ref, tl, sel, ref.dirty, step);
            }
            ref.dirty = dirty = false;
        }
    }

    struct RefView {
        double viewStart = 0, viewSpan = 0;
    };

    void Viewport(unsigned seed) {
        std::mt19937 rng(seed);
        const auto real = [&](double lo, double hi) {
            return std::uniform_real_distribution<double>(lo, hi)(rng);
        };
        for (int i = 0; i < 20000; i++) {
            const double dur = std::max(1.0, real(1, 600000));
            RefView r{real(-1000, dur), real(-10, dur * 1.5)};
            TimelineViewport v{r.viewStart, r.viewSpan};
            if (r.viewSpan <= 0)
                r.viewSpan = dur;
            r.viewSpan = std::clamp(r.viewSpan, std::min(200.0, dur), dur);
            r.viewStart = std::clamp(r.viewStart, 0.0, std::max(0.0, dur - r.viewSpan));
            v.Normalize(dur);
            CHECK(v.start == r.viewStart && v.span == r.viewSpan);
            const double t = real(-100, dur + 100), wheel = real(-5, 5);
            switch (i % 5) {
            case 0:
                if (t > r.viewStart + r.viewSpan || t < r.viewStart)
                    r.viewStart = std::clamp(t - r.viewSpan * 0.1, 0.0, std::max(0.0, dur - r.viewSpan));
                v.Follow(t, dur);
                break;
            case 1:
                r.viewStart =
                    std::clamp(r.viewStart - wheel * r.viewSpan * 0.1, 0.0, std::max(0.0, dur - r.viewSpan));
                v.Pan(wheel, dur);
                break;
            case 2: {
                const double frac = real(0, 1);
                const double ns = std::clamp(r.viewSpan * std::pow(0.8, wheel), std::min(200.0, dur), dur);
                r.viewSpan = ns;
                r.viewStart = std::clamp(t - frac * ns, 0.0, std::max(0.0, dur - ns));
                v.ZoomAt(t, frac, wheel, dur);
                break;
            }
            case 3: {
                const double factor = i % 2 ? 0.6 : 1.6;
                const double ns = std::clamp(r.viewSpan * factor, std::min(200.0, dur), dur);
                const double center = r.viewStart + r.viewSpan * 0.5;
                r.viewSpan = ns;
                r.viewStart = std::clamp(center - ns * 0.5, 0.0, std::max(0.0, dur - ns));
                v.ZoomAroundCentre(factor, dur);
                break;
            }
            default: {
                const float w = static_cast<float>(real(100, 1500));
                const double px = (t - r.viewStart) / std::max(1.0, r.viewSpan) * w;
                r.viewStart = std::clamp(r.viewStart + px / w * dur, 0.0, std::max(0.0, dur - r.viewSpan));
                v.DragThumb(t, w, dur);
                break;
            }
            }
            CHECK(v.start == r.viewStart && v.span == r.viewSpan);
            CHECK(v.span >= std::min(200.0, dur) - 1e-9 && v.span <= dur + 1e-9);
            CHECK(v.start >= 0 && v.start <= std::max(0.0, dur - v.span) + 1e-9);
        }
    }
}

int main() {
    for (unsigned seed = 1; seed <= 8; seed++)
        Differential(seed);
    Viewport(7);
    std::printf(failures ? "timeline_editor_selftest FAILED\n" : "timeline_editor_selftest passed\n");
    return failures ? 1 : 0;
}
