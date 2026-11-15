#pragma once
#include <string>
#include <vector>

namespace perlin::replay {
    enum class Interp { Smooth = 0, Linear = 1, Hold = 2, EaseIn = 3, EaseOut = 4, EaseInOut = 5 };
    const char* InterpName(Interp i);

    struct CamKey {
        double tMs = 0;
        double x = 0, y = 0, z = 0;
        float yaw = 0, pitch = 0, roll = 0, fov = 70;
        Interp interp = Interp::Smooth;
    };
    struct ValueKey {
        double tMs = 0;
        float value = 1;
        Interp interp = Interp::Linear;
    };
    struct CamPose {
        double x = 0, y = 0, z = 0;
        float yaw = 0, pitch = 0, roll = 0, fov = 70;
    };

    struct Timeline {
        std::vector<CamKey> camera;
        std::vector<ValueKey> speed;
        std::vector<ValueKey> timeOfDay;
        bool freezeTimeOfDay = false;

        bool Empty() const { return camera.empty() && speed.empty() && timeOfDay.empty(); }
        bool EvaluateCamera(double tMs, CamPose& out) const;
        float SpeedAt(double tMs, float fallback = 1.0f) const;
        bool TimeOfDayAt(double tMs, float& ticks) const;

        void SetCamera(const CamKey& k);
        void SetSpeed(const ValueKey& k);
        void SetTimeOfDay(const ValueKey& k);
        bool RemoveNear(int track, double tMs, double toleranceMs = 150);
        void Clear();

        std::string ToJson() const;
        static bool FromJson(const std::string& text, Timeline& out);
    };

    long long CountExportFrames(const Timeline& tl, double startMs, double endMs, double fps);
}
