#include <cassert>
#include <cmath>
#include <cstdio>
#include "client/replay/Timeline.hpp"
using namespace perlin::replay;
int main() {
    Timeline t;
    CamKey a;
    a.tMs = 0;
    a.x = 0;
    a.yaw = 350;
    a.fov = 70;
    a.interp = Interp::Linear;
    CamKey b;
    b.tMs = 1000;
    b.x = 10;
    b.yaw = 10;
    b.fov = 90;
    t.SetCamera(b);
    t.SetCamera(a);
    assert(t.camera.size() == 2 && t.camera[0].tMs == 0);
    CamPose p;
    assert(t.EvaluateCamera(500, p));
    assert(std::fabs(p.x - 5) < 1e-6);
    assert(std::fabs(p.yaw - 360) < 1e-3 || std::fabs(p.yaw) < 1e-3);
    assert(std::fabs(p.fov - 80) < 1e-3);
    assert(t.EvaluateCamera(-5, p) && p.x == 0 && t.EvaluateCamera(5000, p) && p.x == 10);
    t.SetSpeed({0, 1, Interp::Linear});
    t.SetSpeed({1000, 3, Interp::Linear});
    assert(std::fabs(t.SpeedAt(500) - 2) < 1e-4);
    t.SetTimeOfDay({0, 23000, Interp::Linear});
    t.SetTimeOfDay({1000, 1000, Interp::Linear});
    float tod;
    assert(t.TimeOfDayAt(500, tod) && std::fabs(tod - 0) < 1.0);
    Timeline u;
    assert(Timeline::FromJson(t.ToJson(), u));
    assert(u.camera.size() == 2 && u.speed.size() == 2 && u.timeOfDay.size() == 2);
    assert(CountExportFrames(Timeline{}, 0, 2000, 30) == 60);
    assert(t.RemoveNear(0, 20) && t.camera.size() == 1);
    std::puts("timeline_selftest ok");
}
