#include "client/systems/replay/TimelineEditor.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace perlin::replay {
    namespace {
        constexpr std::array<float, 14> kSpeedLadder = {0.05f, 0.1f, 0.25f, 0.5f,  0.75f, 1.0f,  1.5f,
                                                        2.0f,  4.0f, 8.0f,  16.0f, 32.0f, 64.0f, 100.0f};
        constexpr double kSelectTolerance = 51.0;
        constexpr double kResortTolerance = 0.5;

        template <class F> decltype(auto) WithTrack(Timeline& tl, int track, F&& f) {
            return track == kCameraTrack  ? f(tl.camera)
                   : track == kSpeedTrack ? f(tl.speed)
                                          : f(tl.timeOfDay);
        }
        template <class F> decltype(auto) WithTrack(const Timeline& tl, int track, F&& f) {
            return track == kCameraTrack  ? f(tl.camera)
                   : track == kSpeedTrack ? f(tl.speed)
                                          : f(tl.timeOfDay);
        }
    }

    int TimelineEditor::Count(int track) const {
        return WithTrack(tl, track, [](const auto& keys) { return static_cast<int>(keys.size()); });
    }

    double TimelineEditor::KeyTime(int track, int index) const {
        return WithTrack(tl, track, [&](const auto& keys) { return keys[static_cast<size_t>(index)].tMs; });
    }

    Interp TimelineEditor::InterpOf(int track, int index) const {
        return WithTrack(tl, track,
                         [&](const auto& keys) { return keys[static_cast<size_t>(index)].interp; });
    }

    void TimelineEditor::SetKeyTime(int track, int index, double tMs) {
        WithTrack(tl, track, [&](auto& keys) { keys[static_cast<size_t>(index)].tMs = tMs; });
    }

    void TimelineEditor::SetInterp(int track, int index, Interp interp) {
        WithTrack(tl, track, [&](auto& keys) { keys[static_cast<size_t>(index)].interp = interp; });
    }

    void TimelineEditor::DropInvalidSelection() {
        if (!Valid())
            Deselect();
    }

    ValueKey* TimelineEditor::SelectedValue() {
        if (!Valid())
            return nullptr;
        if (sel.track == kSpeedTrack)
            return &tl.speed[static_cast<size_t>(sel.index)];
        if (sel.track == kTimeOfDayTrack)
            return &tl.timeOfDay[static_cast<size_t>(sel.index)];
        return nullptr;
    }

    void TimelineEditor::SelectAdded(int track, double tMs) {
        sel.track = track;
        for (int i = 0; i < Count(track); i++)
            if (std::fabs(KeyTime(track, i) - tMs) < kSelectTolerance)
                sel.index = i;
    }

    void TimelineEditor::Resort(double selectedTMs) {
        const auto byTime = [](const auto& a, const auto& b) { return a.tMs < b.tMs; };
        std::ranges::stable_sort(tl.camera, byTime);
        std::ranges::stable_sort(tl.speed, byTime);
        std::ranges::stable_sort(tl.timeOfDay, byTime);
        if (sel.track < 0 || sel.track >= kTrackCount)
            return;
        for (int i = 0; i < Count(sel.track); i++)
            if (std::fabs(KeyTime(sel.track, i) - selectedTMs) < kResortTolerance) {
                sel.index = i;
                break;
            }
    }

    bool TimelineEditor::MoveSelectedTo(double tMs) {
        if (!Valid())
            return false;
        SetKeyTime(sel.track, sel.index, tMs);
        Resort(tMs);
        return true;
    }

    bool TimelineEditor::NudgeTime(int direction, bool fine, double durationMs) {
        if (!Valid())
            return false;
        return MoveSelectedTo(
            std::clamp(KeyTime(sel.track, sel.index) + direction * (fine ? 10.0 : 100.0), 0.0, durationMs));
    }

    bool TimelineEditor::CycleInterp(int direction) {
        if (!Valid())
            return false;
        constexpr int kInterpCount = 6;
        SetInterp(sel.track, sel.index,
                  static_cast<Interp>(
                      (static_cast<int>(InterpOf(sel.track, sel.index)) + direction + kInterpCount) %
                      kInterpCount));
        return true;
    }

    bool TimelineEditor::NudgeFov(int direction, bool fine) {
        CamKey* key = SelectedCamera();
        if (!key)
            return false;
        key->fov = std::clamp(key->fov + static_cast<float>(direction) * (fine ? 1.0f : 5.0f), 5.0f, 170.0f);
        return true;
    }

    bool TimelineEditor::NudgeRoll(int direction, bool fine) {
        CamKey* key = SelectedCamera();
        if (!key)
            return false;
        key->roll =
            std::clamp(key->roll + static_cast<float>(direction) * (fine ? 1.0f : 5.0f), -180.0f, 180.0f);
        return true;
    }

    bool TimelineEditor::StepSpeed(int direction) {
        if (!OnTrack(kSpeedTrack))
            return false;
        float& v = tl.speed[static_cast<size_t>(sel.index)].value;
        size_t best = 0;
        for (size_t i = 1; i < kSpeedLadder.size(); i++)
            if (std::fabs(kSpeedLadder[i] - v) < std::fabs(kSpeedLadder[best] - v))
                best = i;
        const int target =
            std::clamp(static_cast<int>(best) + direction, 0, static_cast<int>(kSpeedLadder.size()) - 1);
        v = kSpeedLadder[static_cast<size_t>(target)];
        return true;
    }

    bool TimelineEditor::NudgeTimeOfDay(int direction, bool fine) {
        if (!OnTrack(kTimeOfDayTrack))
            return false;
        float& v = tl.timeOfDay[static_cast<size_t>(sel.index)].value;
        v = std::fmod(v + static_cast<float>(direction) * (fine ? 100.0f : 1000.0f) + 24000.0f, 24000.0f);
        return true;
    }

    bool TimelineEditor::SetPose(double x, double y, double z, float yaw, float pitch) {
        CamKey* key = SelectedCamera();
        if (!key)
            return false;
        key->x = x;
        key->y = y;
        key->z = z;
        key->yaw = yaw;
        key->pitch = pitch;
        return true;
    }

    bool TimelineEditor::DeleteSelected() {
        if (!Valid())
            return false;
        WithTrack(tl, sel.track, [&](auto& keys) { keys.erase(keys.begin() + sel.index); });
        sel.index = -1;
        return true;
    }

    void TimelineEditor::AddCamera(const CamKey& key) {
        tl.SetCamera(key);
        SelectAdded(kCameraTrack, key.tMs);
    }

    void TimelineEditor::AddSpeed(double tMs, float value) {
        tl.SetSpeed({tMs, value, Interp::Linear});
        SelectAdded(kSpeedTrack, tMs);
    }

    void TimelineEditor::AddTimeOfDay(double tMs, float ticks) {
        tl.SetTimeOfDay({tMs, ticks, Interp::Linear});
        SelectAdded(kTimeOfDayTrack, tMs);
    }

    void TimelineViewport::Normalize(double durationMs) {
        if (span <= 0)
            span = durationMs;
        span = std::clamp(span, std::min(kMinSpanMs, durationMs), durationMs);
        start = std::clamp(start, 0.0, std::max(0.0, durationMs - span));
    }

    void TimelineViewport::Follow(double tMs, double durationMs) {
        if (tMs > start + span || tMs < start)
            start = std::clamp(tMs - span * 0.1, 0.0, std::max(0.0, durationMs - span));
    }

    void TimelineViewport::Pan(double wheel, double durationMs) {
        start = std::clamp(start - wheel * span * 0.1, 0.0, std::max(0.0, durationMs - span));
    }

    void TimelineViewport::ZoomAt(double anchorMs, double fraction, double wheel, double durationMs) {
        const double next =
            std::clamp(span * std::pow(0.8, wheel), std::min(kMinSpanMs, durationMs), durationMs);
        span = next;
        start = std::clamp(anchorMs - fraction * next, 0.0, std::max(0.0, durationMs - next));
    }

    void TimelineViewport::ZoomAroundCentre(double factor, double durationMs) {
        const double next = std::clamp(span * factor, std::min(kMinSpanMs, durationMs), durationMs);
        const double centre = start + span * 0.5;
        span = next;
        start = std::clamp(centre - next * 0.5, 0.0, std::max(0.0, durationMs - next));
    }

    void TimelineViewport::CentreOn(double tMs, double durationMs) {
        start = std::clamp(tMs - span * 0.5, 0.0, std::max(0.0, durationMs - span));
    }

    void TimelineViewport::DragThumb(double targetMs, double trackWidth, double durationMs) {
        const double px = (targetMs - start) / std::max(1.0, span) * trackWidth;
        start = std::clamp(start + px / trackWidth * durationMs, 0.0, std::max(0.0, durationMs - span));
    }
}
