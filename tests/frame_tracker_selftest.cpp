#include "client/systems/javashaders/FrameTracker.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace perlin;
using namespace perlin::jshader;

namespace {
    int failures = 0;

    void Expect(bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL %s\n", what);
            failures++;
        }
    }

    bool Near(float a, float b, float eps = 1e-4f) {
        return std::fabs(a - b) <= eps;
    }

    FrameParams Params(float t, double x = 0, double y = 64, double z = 0) {
        FrameParams p{};
        for (int i = 0; i < 4; i++) {
            p.view[i * 5] = 1.0f;
            p.projection[i * 5] = 1.0f;
        }
        p.projection[10] = -1.002f;
        p.projection[11] = -1.0f;
        p.projection[14] = -0.2f;
        p.projection[15] = 0.0f;
        p.cameraPosition[0] = x;
        p.cameraPosition[1] = y;
        p.cameraPosition[2] = z;
        p.timeSeconds = t;
        p.renderDistance = 8;
        p.worldTick = -1;
        p.worldDay = -1;
        p.sky.rainBrightness = 1.0f;
        std::snprintf(p.biome, sizeof(p.biome), "minecraft:plains");
        return p;
    }

    void TestHistory() {
        FrameTracker tracker;
        PipelineConfig cfg;
        FrameTracker::Scene scene;
        FrameState a, b;
        tracker.Update(Params(1.0f, 10, 64, 5), cfg, scene, a);
        Expect(a.previousCameraPosition[0] == 10 && a.previousCameraPosition[2] == 5,
               "first frame prev camera is current");
        Expect(std::memcmp(a.prevModelView, a.modelView, sizeof(a.modelView)) == 0,
               "first frame prev matrix is current");
        tracker.Update(Params(1.05f, 12, 64, 5), cfg, scene, b);
        Expect(b.previousCameraPosition[0] == 10, "second frame keeps previous camera");
        Expect(std::memcmp(b.prevModelView, a.modelView, sizeof(a.modelView)) == 0,
               "second frame prev matrix is last frame");
        Expect(Near(b.frameTime, 0.05f), "frame time is delta");
        Expect(b.cameraPosition[0] == 12, "camera position copied");
    }

    void TestSmoothing() {
        FrameTracker tracker;
        PipelineConfig cfg;
        cfg.wetnessHalflife = 10.0f;
        cfg.drynessHalflife = 10.0f;
        FrameTracker::Scene scene;
        FrameState s;
        FrameParams p = Params(0.0f);
        p.sky.rainBrightness = 1.0f;
        tracker.Update(p, cfg, scene, s);
        Expect(s.wetness == 0.0f, "starts dry");
        p = Params(1.0f);
        p.sky.rainBrightness = 0.0f;
        tracker.Update(p, cfg, scene, s);
        Expect(s.rainStrength == 1.0f, "rain strength follows brightness");
        Expect(s.wetness > 0.0f && s.wetness < 1.0f, "wetness approaches rain gradually");
        float last = s.wetness;
        for (int i = 2; i < 40; i++) {
            p = Params(static_cast<float>(i));
            p.sky.rainBrightness = 0.0f;
            tracker.Update(p, cfg, scene, s);
            Expect(s.wetness >= last, "wetness monotonic while raining");
            last = s.wetness;
        }
        Expect(last > 0.9f, "wetness converges");
    }

    void TestEyeBrightness() {
        FrameTracker tracker;
        PipelineConfig cfg;
        FrameTracker::Scene scene;
        scene.noSkyLight = true;
        FrameState s;
        FrameParams p = Params(0.0f);
        p.eyeBlockLight = 20;
        p.eyeSkyLight = 15;
        tracker.Update(p, cfg, scene, s);
        Expect(s.eyeBrightness[0] == 240, "block light clamped to 15");
        Expect(s.eyeBrightness[1] == 0, "no sky light dimension zeroes sky");
        Expect(s.eyeBrightnessSmooth[0] == 240, "first frame is not smoothed");
    }

    void TestFog() {
        FrameTracker tracker;
        PipelineConfig cfg;
        FrameTracker::Scene scene;
        FrameState s;
        FrameParams p = Params(0.0f);
        tracker.Update(p, cfg, scene, s);
        Expect(Near(s.fogEnd, 1024.0f) && s.fogStart == 0.0f, "clear overworld fog");
        p = Params(0.1f);
        p.sky.rainBrightness = 0.0f;
        tracker.Update(p, cfg, scene, s);
        Expect(Near(s.fogEnd, 768.0f) && Near(s.fogStart, -160.0f), "rain pulls fog in");
        p = Params(0.2f);
        p.cameraMedium = 1;
        tracker.Update(p, cfg, scene, s);
        Expect(s.fogMode == 0x0801 && s.isEyeInWater == 1, "underwater uses exp2 fog");
        float early = s.fogEnd;
        for (int i = 0; i < 400; i++) {
            p = Params(0.2f + 0.2f * static_cast<float>(i + 1));
            p.cameraMedium = 1;
            tracker.Update(p, cfg, scene, s);
        }
        Expect(s.fogEnd > early, "water vision improves over time");
        p = Params(1000.0f);
        tracker.Update(p, cfg, scene, s);
        p = Params(1000.1f);
        p.cameraMedium = 1;
        tracker.Update(p, cfg, scene, s);
        Expect(s.fogEnd < early + 1.0f, "water vision resets after leaving water");
        scene.nether = true;
        p = Params(1001.0f);
        tracker.Update(p, cfg, scene, s);
        Expect(s.fogMode == 0x2601 && s.dimensionNether && s.dimensionId == -1 && s.heavyFog == 1.0f,
               "nether fog");
        scene.nether = false;
        scene.end = true;
        tracker.Update(p, cfg, scene, s);
        Expect(s.dimensionEnd && s.dimensionId == 1, "end dimension flags");
    }

    void TestSun() {
        FrameTracker tracker;
        PipelineConfig cfg;
        FrameTracker::Scene scene;
        FrameState s;
        FrameParams p = Params(0.0f);
        p.sky.sunAngle = 0.0f;
        tracker.Update(p, cfg, scene, s);
        Expect(Near(s.sunAngle, 0.25f) && s.worldTime == 6000, "sun angle zero maps to noon");
        Expect(tracker.ShadowLightWorld().y > 0.99f, "daytime shadow light points up");
        p.worldTick = 24000 * 3 + 1234;
        p.worldDay = 3;
        tracker.Update(p, cfg, scene, s);
        Expect(s.worldTime == 1234 && s.worldDay == 3, "explicit world time wins");
        scene.nether = true;
        tracker.Update(Params(0.0f), cfg, scene, s);
        Expect(Near(s.sunAngle, 0.75f), "nether has fixed sun");
    }

    void TestShadowReuse() {
        FrameTracker tracker;
        PipelineConfig cfg;
        cfg.shadowEnabled = true;
        cfg.shadowIntervalSize = 2.0f;
        cfg.shadowDistance = 100.0f;
        cfg.shadowNearPlane = -100.0f;
        cfg.shadowFarPlane = 100.0f;
        FrameTracker::Scene scene;
        FrameState s;
        FrameTracker::ShadowInputs in;
        in.interval = 2;
        in.depthReady = true;
        in.frameStamp = 1;
        FrameParams p = Params(0.0f, 0.3, 64, 0.3);
        tracker.Update(p, cfg, scene, s);
        auto first = tracker.UpdateShadows(p, cfg, s, in);
        Expect(!first.reuse && !first.partial, "first shadow frame renders");
        p = Params(0.05f, 0.5, 64, 0.3);
        tracker.Update(p, cfg, scene, s);
        auto second = tracker.UpdateShadows(p, cfg, s, in);
        Expect(second.reuse, "odd frame reuses kept shadow");
        Expect(std::memcmp(second.drawView, first.drawView, sizeof(first.drawView)) == 0,
               "reuse keeps draw view");
        in.frameStamp = 2;
        auto third = tracker.UpdateShadows(p, cfg, s, in);
        Expect(!third.reuse, "interval boundary renders");
        in.frameStamp = 1;
        p = Params(0.1f, 40, 64, 0.3);
        tracker.Update(p, cfg, scene, s);
        auto far = tracker.UpdateShadows(p, cfg, s, in);
        Expect(!far.reuse, "large movement forces render");
        tracker.ForgetShadow();
        p = Params(0.15f, 40, 64, 0.3);
        tracker.Update(p, cfg, scene, s);
        auto forgotten = tracker.UpdateShadows(p, cfg, s, in);
        Expect(!forgotten.reuse && !forgotten.partial, "forgotten shadow renders");
        FrameTracker::ShadowInputs partial = in;
        partial.interval = 1;
        partial.cacheUsable = true;
        partial.cacheMatchesMeshes = true;
        auto kept = tracker.UpdateShadows(p, cfg, s, partial);
        Expect(kept.partial && !kept.reuse, "stable camera uses partial cache");
        partial.cacheMatchesMeshes = false;
        auto stale = tracker.UpdateShadows(p, cfg, s, partial);
        Expect(!stale.partial, "changed meshes invalidate partial reuse");
        partial.cacheMatchesMeshes = true;
        partial.depthReady = false;
        auto nodepth = tracker.UpdateShadows(p, cfg, s, partial);
        Expect(!nodepth.partial && !nodepth.reuse, "missing depth disables reuse");
    }
}

int main() {
    TestHistory();
    TestSmoothing();
    TestEyeBrightness();
    TestFog();
    TestSun();
    TestShadowReuse();
    if (failures == 0)
        std::printf("frame_tracker_selftest OK\n");
    return failures == 0 ? 0 : 1;
}
