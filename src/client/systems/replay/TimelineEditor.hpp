#pragma once
#include "client/systems/replay/Timeline.hpp"

namespace perlin::replay {
    inline constexpr int kTrackCount = 3;
    enum Track : int { kCameraTrack = 0, kSpeedTrack = 1, kTimeOfDayTrack = 2 };

    struct KeySelection {
        int track = -1;
        int index = -1;
    };

    class TimelineEditor {
    public:
        TimelineEditor(Timeline& timeline, KeySelection& selection) : tl(timeline), sel(selection) {}

        int Count(int track) const;
        double KeyTime(int track, int index) const;
        Interp InterpOf(int track, int index) const;

        const KeySelection& Selection() const { return sel; }
        bool Valid() const {
            return sel.track >= 0 && sel.track < kTrackCount && sel.index >= 0 &&
                   sel.index < Count(sel.track);
        }
        void DropInvalidSelection();
        void Select(int track, int index) {
            sel.track = track;
            sel.index = index;
        }
        void Deselect() { Select(-1, -1); }
        bool OnTrack(int track) const { return Valid() && sel.track == track; }

        CamKey* SelectedCamera() {
            return OnTrack(kCameraTrack) ? &tl.camera[static_cast<size_t>(sel.index)] : nullptr;
        }
        ValueKey* SelectedValue();

        void SelectAdded(int track, double tMs);

        bool MoveSelectedTo(double tMs);
        bool NudgeTime(int direction, bool fine, double durationMs);
        bool CycleInterp(int direction);
        bool NudgeFov(int direction, bool fine);
        bool NudgeRoll(int direction, bool fine);
        bool StepSpeed(int direction);
        bool NudgeTimeOfDay(int direction, bool fine);
        bool SetPose(double x, double y, double z, float yaw, float pitch);
        bool DeleteSelected();

        void AddCamera(const CamKey& key);
        void AddSpeed(double tMs, float value);
        void AddTimeOfDay(double tMs, float ticks);

    private:
        void SetKeyTime(int track, int index, double tMs);
        void SetInterp(int track, int index, Interp interp);
        void Resort(double selectedTMs);

        Timeline& tl;
        KeySelection& sel;
    };

    struct TimelineViewport {
        double start = 0;
        double span = 0;

        static constexpr double kMinSpanMs = 200.0;

        void Normalize(double durationMs);
        void Fit() { span = 0; }
        void Follow(double tMs, double durationMs);
        void Pan(double wheel, double durationMs);
        void ZoomAt(double anchorMs, double fraction, double wheel, double durationMs);
        void ZoomAroundCentre(double factor, double durationMs);
        void CentreOn(double tMs, double durationMs);
        void DragThumb(double targetMs, double trackWidth, double durationMs);
    };
}
