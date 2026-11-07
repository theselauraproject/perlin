#include "client/replay/Timeline.hpp"

#include <algorithm>
#include <cmath>

#include "client/ui/Json.hpp"

namespace perlin::replay {
    const char* InterpName(Interp i) {
        switch (i) {
        case Interp::Smooth:
            return "Smooth";
        case Interp::Linear:
            return "Linear";
        case Interp::Hold:
            return "Hold";
        case Interp::EaseIn:
            return "Ease In";
        case Interp::EaseOut:
            return "Ease Out";
        case Interp::EaseInOut:
            return "Ease In/Out";
        }
        return "?";
    }

    namespace {
        double Shape(Interp i, double u) {
            u = std::clamp(u, 0.0, 1.0);
            switch (i) {
            case Interp::Hold:
                return 0.0;
            case Interp::EaseIn:
                return u * u;
            case Interp::EaseOut:
                return 1.0 - (1.0 - u) * (1.0 - u);
            case Interp::EaseInOut:
                return u * u * (3.0 - 2.0 * u);
            default:
                return u;
            }
        }

        double CatmullRom(double p0, double p1, double p2, double p3, double u) {
            const double u2 = u * u, u3 = u2 * u;
            return 0.5 * ((2 * p1) + (-p0 + p2) * u + (2 * p0 - 5 * p1 + 4 * p2 - p3) * u2 +
                          (-p0 + 3 * p1 - 3 * p2 + p3) * u3);
        }

        double Near(double a, double b) {
            while (b - a > 180.0)
                b -= 360.0;
            while (b - a < -180.0)
                b += 360.0;
            return b;
        }

        template <class K> size_t Upper(const std::vector<K>& v, double t) {
            size_t i = 0;
            while (i < v.size() && v[i].tMs <= t)
                i++;
            return i;
        }
    }

    bool Timeline::EvaluateCamera(double t, CamPose& out) const {
        if (camera.empty())
            return false;
        const auto pose = [](const CamKey& k) {
            CamPose p;
            p.x = k.x;
            p.y = k.y;
            p.z = k.z;
            p.yaw = k.yaw;
            p.pitch = k.pitch;
            p.roll = k.roll;
            p.fov = k.fov;
            return p;
        };
        const size_t hi = Upper(camera, t);
        if (hi == 0) {
            out = pose(camera.front());
            return true;
        }
        if (hi >= camera.size()) {
            out = pose(camera.back());
            return true;
        }
        const size_t i1 = hi - 1, i2 = hi;
        const CamKey& a = camera[i1];
        const CamKey& b = camera[i2];
        const double span = std::max(1.0, b.tMs - a.tMs);
        const double raw = (t - a.tMs) / span;
        if (a.interp == Interp::Hold) {
            out = pose(a);
            return true;
        }
        const double u = a.interp == Interp::Smooth ? raw : Shape(a.interp, raw);
        const CamKey& c0 = camera[i1 == 0 ? 0 : i1 - 1];
        const CamKey& c3 = camera[i2 + 1 < camera.size() ? i2 + 1 : i2];
        const auto lerp = [&](double p0, double p1, double p2, double p3) {
            return a.interp == Interp::Smooth ? CatmullRom(p0, p1, p2, p3, u) : p1 + (p2 - p1) * u;
        };
        out.x = lerp(c0.x, a.x, b.x, c3.x);
        out.y = lerp(c0.y, a.y, b.y, c3.y);
        out.z = lerp(c0.z, a.z, b.z, c3.z);
        const double ya = a.yaw, yb = Near(ya, b.yaw), y0 = Near(ya, c0.yaw), y3 = Near(yb, c3.yaw);
        out.yaw = static_cast<float>(lerp(y0, ya, yb, y3));
        out.pitch = static_cast<float>(lerp(c0.pitch, a.pitch, b.pitch, c3.pitch));
        const double ra = a.roll, rb = Near(ra, b.roll), r0 = Near(ra, c0.roll), r3 = Near(rb, c3.roll);
        out.roll = static_cast<float>(lerp(r0, ra, rb, r3));
        out.fov = static_cast<float>(a.fov + (b.fov - a.fov) *
                                                 (a.interp == Interp::Smooth ? u * u * (3 - 2 * u) : u));
        out.fov = std::clamp(out.fov, 1.0f, 170.0f);
        return true;
    }

    namespace {
        float EvalValue(const std::vector<ValueKey>& v, double t, float fallback, bool wrapDay) {
            if (v.empty())
                return fallback;
            const size_t hi = Upper(v, t);
            if (hi == 0)
                return v.front().value;
            if (hi >= v.size())
                return v.back().value;
            const ValueKey& a = v[hi - 1];
            const ValueKey& b = v[hi];
            if (a.interp == Interp::Hold)
                return a.value;
            const double u = Shape(a.interp == Interp::Smooth ? Interp::EaseInOut : a.interp,
                                   (t - a.tMs) / std::max(1.0, b.tMs - a.tMs));
            double bv = b.value;
            if (wrapDay) {
                while (bv < a.value)
                    bv += 24000.0;
            }
            return static_cast<float>(a.value + (bv - a.value) * u);
        }
    }

    float Timeline::SpeedAt(double t, float fallback) const {
        return std::clamp(EvalValue(speed, t, fallback, false), 0.001f, 1000.0f);
    }

    bool Timeline::TimeOfDayAt(double t, float& ticks) const {
        if (timeOfDay.empty())
            return false;
        ticks = freezeTimeOfDay ? timeOfDay.front().value : EvalValue(timeOfDay, t, 0.0f, true);
        ticks = std::fmod(std::fmod(ticks, 24000.0f) + 24000.0f, 24000.0f);
        return true;
    }

    namespace {
        template <class K> void Put(std::vector<K>& v, const K& k) {
            for (K& e : v) {
                if (std::fabs(e.tMs - k.tMs) < 50.0) {
                    e = k;
                    return;
                }
            }
            v.push_back(k);
            std::sort(v.begin(), v.end(), [](const K& a, const K& b) { return a.tMs < b.tMs; });
        }
        template <class K> bool Drop(std::vector<K>& v, double t, double tol) {
            size_t best = v.size();
            double bd = tol;
            for (size_t i = 0; i < v.size(); i++) {
                const double d = std::fabs(v[i].tMs - t);
                if (d <= bd) {
                    bd = d;
                    best = i;
                }
            }
            if (best == v.size())
                return false;
            v.erase(v.begin() + static_cast<long>(best));
            return true;
        }
    }

    void Timeline::SetCamera(const CamKey& k) {
        Put(camera, k);
    }
    void Timeline::SetSpeed(const ValueKey& k) {
        Put(speed, k);
    }
    void Timeline::SetTimeOfDay(const ValueKey& k) {
        Put(timeOfDay, k);
    }
    bool Timeline::RemoveNear(int track, double t, double tol) {
        return track == 0 ? Drop(camera, t, tol) : track == 1 ? Drop(speed, t, tol) : Drop(timeOfDay, t, tol);
    }
    void Timeline::Clear() {
        camera.clear();
        speed.clear();
        timeOfDay.clear();
        freezeTimeOfDay = false;
    }

    std::string Timeline::ToJson() const {
        ui::Json root = ui::Json::MakeObject();
        ui::Json cams = ui::Json::MakeArray();
        for (const CamKey& k : camera) {
            ui::Json o = ui::Json::MakeObject();
            o.Set("t", ui::Json(static_cast<double>(k.tMs)));
            o.Set("x", ui::Json(static_cast<double>(k.x)));
            o.Set("y", ui::Json(static_cast<double>(k.y)));
            o.Set("z", ui::Json(static_cast<double>(k.z)));
            o.Set("yaw", ui::Json(static_cast<double>(k.yaw)));
            o.Set("pitch", ui::Json(static_cast<double>(k.pitch)));
            o.Set("roll", ui::Json(static_cast<double>(k.roll)));
            o.Set("fov", ui::Json(static_cast<double>(k.fov)));
            o.Set("i", ui::Json(static_cast<double>(static_cast<int>(k.interp))));
            cams.Push(o);
        }
        root.Set("camera", cams);
        const auto vals = [](const std::vector<ValueKey>& v) {
            ui::Json a = ui::Json::MakeArray();
            for (const ValueKey& k : v) {
                ui::Json o = ui::Json::MakeObject();
                o.Set("t", ui::Json(static_cast<double>(k.tMs)));
                o.Set("v", ui::Json(static_cast<double>(k.value)));
                o.Set("i", ui::Json(static_cast<double>(static_cast<int>(k.interp))));
                a.Push(o);
            }
            return a;
        };
        root.Set("speed", vals(speed));
        root.Set("timeOfDay", vals(timeOfDay));
        root.Set("freezeTime", ui::Json(freezeTimeOfDay));
        return root.Dump();
    }

    bool Timeline::FromJson(const std::string& text, Timeline& out) {
        ui::Json root;
        std::string err;
        if (!ui::Json::Parse(text, root, err))
            return false;
        Timeline tl;
        const auto num = [](const ui::Json& o, const char* k, double d) {
            const ui::Json* f = o.Find(k);
            return f ? f->NumberOr(d) : d;
        };
        if (const ui::Json* a = root.Find("camera"); a && a->IsArray())
            for (const ui::Json& o : a->array) {
                CamKey k;
                k.tMs = num(o, "t", 0);
                k.x = num(o, "x", 0);
                k.y = num(o, "y", 0);
                k.z = num(o, "z", 0);
                k.yaw = static_cast<float>(num(o, "yaw", 0));
                k.pitch = static_cast<float>(num(o, "pitch", 0));
                k.roll = static_cast<float>(num(o, "roll", 0));
                k.fov = static_cast<float>(num(o, "fov", 70));
                k.interp = static_cast<Interp>(std::clamp(static_cast<int>(num(o, "i", 0)), 0, 5));
                tl.camera.push_back(k);
            }
        const auto readVals = [&](const char* name, std::vector<ValueKey>& v) {
            if (const ui::Json* a = root.Find(name); a && a->IsArray())
                for (const ui::Json& o : a->array) {
                    ValueKey k;
                    k.tMs = num(o, "t", 0);
                    k.value = static_cast<float>(num(o, "v", 1));
                    k.interp = static_cast<Interp>(std::clamp(static_cast<int>(num(o, "i", 1)), 0, 5));
                    v.push_back(k);
                }
        };
        readVals("speed", tl.speed);
        readVals("timeOfDay", tl.timeOfDay);
        if (const ui::Json* f = root.Find("freezeTime"))
            tl.freezeTimeOfDay = f->BoolOr(false);
        const auto byTime = [](const auto& a, const auto& b) { return a.tMs < b.tMs; };
        std::sort(tl.camera.begin(), tl.camera.end(), byTime);
        std::sort(tl.speed.begin(), tl.speed.end(), byTime);
        std::sort(tl.timeOfDay.begin(), tl.timeOfDay.end(), byTime);
        out = std::move(tl);
        return true;
    }

    long long CountExportFrames(const Timeline& tl, double startMs, double endMs, double fps) {
        if (!(fps > 0) || endMs <= startMs)
            return 0;
        const double stepBase = 1000.0 / fps;
        double t = startMs;
        long long n = 0;
        while (t < endMs - 0.01 && n < 100000000LL) {
            t += stepBase * tl.SpeedAt(t);
            n++;
        }
        return n;
    }
}
