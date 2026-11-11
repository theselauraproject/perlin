#include "client/ui/SkinPreview.hpp"

#include <algorithm>
#include <cmath>
#include <atomic>
#include <future>
#include <thread>

#include "client/multiplayer/SkinLoader.hpp"

namespace perlin::ui {
    namespace {
        struct Box {
            float mn[3], mx[3];
            int u, v;
            int w, h, d;
            bool overlay;
            bool cape;
            bool head = false;
        };

        struct Hit {
            float t;
            unsigned char c[4];
        };

        struct Sampler {
            const std::vector<unsigned char>* px;
            int w, h;
            float sx, sy;
            bool Get(float u, float v, unsigned char* out) const {
                const int x = std::clamp(static_cast<int>(u * sx), 0, w - 1);
                const int y = std::clamp(static_cast<int>(v * sy), 0, h - 1);
                const unsigned char* p =
                    &(*px)[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4];
                out[0] = p[0];
                out[1] = p[1];
                out[2] = p[2];
                out[3] = p[3];
                return true;
            }
        };

        bool Intersect(const Box& b, const float* o, const float* dir, float& tNear, int& axis, int& sign) {
            float t0 = -1e9f, t1 = 1e9f;
            int a = -1, s = 0;
            for (int i = 0; i < 3; i++) {
                if (std::fabs(dir[i]) < 1e-8f) {
                    if (o[i] < b.mn[i] || o[i] > b.mx[i])
                        return false;
                    continue;
                }
                float ta = (b.mn[i] - o[i]) / dir[i], tb = (b.mx[i] - o[i]) / dir[i];
                int sg = dir[i] > 0 ? -1 : 1;
                if (ta > tb)
                    std::swap(ta, tb);
                if (ta > t0) {
                    t0 = ta;
                    a = i;
                    s = sg;
                }
                t1 = std::min(t1, tb);
                if (t0 > t1)
                    return false;
            }
            if (a < 0 || t1 < 0.0f)
                return false;
            tNear = t0;
            axis = a;
            sign = s;
            return true;
        }

        void FaceUv(const Box& b, int axis, int sign, const float* p, float& u, float& v) {
            const float sx = b.mx[0] - b.mn[0], sy = b.mx[1] - b.mn[1], sz = b.mx[2] - b.mn[2];
            const float fx = (p[0] - b.mn[0]) / sx, fy = (b.mx[1] - p[1]) / sy, fz = (p[2] - b.mn[2]) / sz;
            const float w = static_cast<float>(b.w), h = static_cast<float>(b.h), d = static_cast<float>(b.d);
            const float u0 = static_cast<float>(b.u), v0 = static_cast<float>(b.v);
            if (axis == 2) {
                const bool front = b.cape ? sign < 0 : sign > 0;
                if (sign > 0) {
                    u = (front ? u0 + d : u0 + 2 * d + w) + (front ? fx : 1.0f - fx) * w;
                } else {
                    u = (front ? u0 + d : u0 + 2 * d + w) + (front ? 1.0f - fx : 1.0f - fx) * w;
                    if (front)
                        u = u0 + d + (1.0f - fx) * w;
                }
                v = v0 + d + fy * h;
            } else if (axis == 0) {
                if (sign < 0) {
                    u = u0 + fz * d;
                } else {
                    u = u0 + d + w + (1.0f - fz) * d;
                }
                v = v0 + d + fy * h;
            } else {
                if (sign > 0) {
                    u = u0 + d + fx * w;
                    v = v0 + fz * d;
                } else {
                    u = u0 + d + w + fx * w;
                    v = v0 + (1.0f - fz) * d;
                }
            }
        }
    }

    bool GuessSlimSkin(const std::vector<unsigned char>& skin, int skinW, int skinH) {
        return DetectSlimSkin(skin, skinW, skinH);
    }

    Texture RenderSkin3D(const SkinView& view, int outW, int outH) {
        Texture out;
        out.width = outW;
        out.height = outH;
        out.pixels.assign(static_cast<size_t>(outW) * static_cast<size_t>(outH) * 4, 0);
        const bool haveSkin =
            view.skin && view.skinW >= 64 && view.skinH >= 64 &&
            view.skin->size() >= static_cast<size_t>(view.skinW) * static_cast<size_t>(view.skinH) * 4;
        if (!haveSkin || outW <= 0 || outH <= 0)
            return out;
        const Sampler skin{view.skin, view.skinW, view.skinH, static_cast<float>(view.skinW) / 64.0f,
                           static_cast<float>(view.skinH) / 64.0f};
        const bool haveCape =
            view.cape && view.capeW >= 22 && view.capeH >= 17 &&
            view.cape->size() >= static_cast<size_t>(view.capeW) * static_cast<size_t>(view.capeH) * 4;
        const Sampler cape{view.cape, view.capeW, view.capeH, static_cast<float>(view.capeW) / 64.0f,
                           static_cast<float>(view.capeH) / 32.0f};
        const float aw = view.slim ? 3.0f : 4.0f;
        std::vector<Box> boxes;
        const auto add = [&](float x0, float y0, float z0, float x1, float y1, float z1, int u, int v, int w,
                             int h, int d, float grow, bool overlay) {
            Box b{{x0 - grow, y0 - grow, z0 - grow},
                  {x1 + grow, y1 + grow, z1 + grow},
                  u,
                  v,
                  w,
                  h,
                  d,
                  overlay,
                  false};
            b.head = y0 >= 24.0f;
            boxes.push_back(b);
        };
        const int iaw = view.slim ? 3 : 4;
        for (int layer = 0; layer < 2; layer++) {
            const float g = layer == 0 ? 0.0f : 0.5f;
            const bool ov = layer == 1;
            if (!ov || view.hat)
                add(-4, 24, -4, 4, 32, 4, ov ? 32 : 0, 0, 8, 8, 8, g, ov);
            const float gb = layer == 0 ? 0.0f : 0.25f;
            if (!ov || view.jacket)
                add(-4, 12, -2, 4, 24, 2, 16, ov ? 32 : 16, 8, 12, 4, gb, ov);
            if (!ov || view.rightSleeve)
                add(-4 - aw, 12, -2, -4, 24, 2, 40, ov ? 32 : 16, iaw, 12, 4, gb, ov);
            if (!ov || view.leftSleeve)
                add(4, 12, -2, 4 + aw, 24, 2, ov ? 48 : 32, 48, iaw, 12, 4, gb, ov);
            if (!ov || view.rightPants)
                add(-4, 0, -2, 0, 12, 2, 0, ov ? 32 : 16, 4, 12, 4, gb, ov);
            if (!ov || view.leftPants)
                add(0, 0, -2, 4, 12, 2, 0 + (ov ? 0 : 16), 48, 4, 12, 4, gb, ov);
        }
        if (haveCape) {
            Box c{{-5, 8, -3.2f}, {5, 24, -2.1f}, 0, 0, 10, 16, 1, false, true, false};
            boxes.push_back(c);
        }
        const float cy = std::cos(view.yaw * 0.017453292f), sy = std::sin(view.yaw * 0.017453292f);
        const float cp = std::cos(view.pitch * 0.017453292f), sp = std::sin(view.pitch * 0.017453292f);
        const float scale = static_cast<float>(outH) / 36.0f;
        const auto toModel = [&](const float* a, float* r, bool direction) {
            const float x1 = a[0];
            const float y1 = cp * a[1] + sp * a[2];
            const float z1 = -sp * a[1] + cp * a[2];
            r[0] = cy * x1 - sy * z1;
            r[1] = y1;
            r[2] = sy * x1 + cy * z1;
            if (!direction)
                r[1] += 16.0f;
        };
        const float dirView[3] = {0.0f, 0.0f, -1.0f};
        float md[3];
        toModel(dirView, md, true);
        float ex[3], ey[3], e0[3];
        {
            const float ax[3] = {1.0f, 0.0f, 0.0f}, ay[3] = {0.0f, 1.0f, 0.0f}, az[3] = {0.0f, 0.0f, 60.0f};
            toModel(ax, ex, true);
            toModel(ay, ey, true);
            toModel(az, e0, false);
        }

        // head turns on its own about the neck (0, 24, 0): rays for head boxes moved into head space
        const bool headTurned = view.headYaw != 0.0f || view.headPitch != 0.0f;
        const float hcy = std::cos(-view.headYaw * 0.017453292f),
                    hsy = std::sin(-view.headYaw * 0.017453292f);
        const float hcp = std::cos(-view.headPitch * 0.017453292f),
                    hsp = std::sin(-view.headPitch * 0.017453292f);
        const auto toHead = [&](const float* a, float* r, bool point) {
            const float x = a[0], y = point ? a[1] - 24.0f : a[1], z = a[2];
            const float x1 = hcy * x + hsy * z, z1 = -hsy * x + hcy * z;
            r[0] = x1;
            r[1] = hcp * y - hsp * z1;
            r[2] = hsp * y + hcp * z1;
            if (point)
                r[1] += 24.0f;
        };
        float hd[3];
        toHead(md, hd, false);

        struct Screen {
            int i0, i1, j0, j1;
        };
        std::vector<Screen> bounds(boxes.size());
        for (size_t k = 0; k < boxes.size(); k++) {
            const Box& b = boxes[k];
            float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
            for (int c = 0; c < 8; c++) {
                const float px = (c & 1) ? b.mx[0] : b.mn[0];
                const float py = (c & 2) ? b.mx[1] : b.mn[1];
                const float pz = (c & 4) ? b.mx[2] : b.mn[2];
                const float qy = py - 16.0f;
                const float vx = cy * px + sy * pz;
                const float z1 = -sy * px + cy * pz;
                const float vy = cp * qy - sp * z1;
                minX = std::min(minX, vx);
                maxX = std::max(maxX, vx);
                minY = std::min(minY, vy);
                maxY = std::max(maxY, vy);
            }
            if (b.head && headTurned) {
                // turned head can reach past its resting box; widen its screen bounds
                minX -= 3.0f;
                maxX += 3.0f;
                minY -= 3.0f;
                maxY += 3.0f;
            }
            Screen s;
            s.i0 =
                std::max(0, static_cast<int>(std::floor(minX * scale + static_cast<float>(outW) * 0.5f)) - 1);
            s.i1 = std::min(outW - 1,
                            static_cast<int>(std::ceil(maxX * scale + static_cast<float>(outW) * 0.5f)) + 1);
            s.j0 =
                std::max(0, static_cast<int>(std::floor(static_cast<float>(outH) * 0.5f - maxY * scale)) - 1);
            s.j1 = std::min(outH - 1,
                            static_cast<int>(std::ceil(static_cast<float>(outH) * 0.5f - minY * scale)) + 1);
            bounds[k] = s;
        }

        const auto renderRows = [&](int rowFrom, int rowTo) {
            std::vector<Hit> hits;
            hits.reserve(24);
            for (int j = rowFrom; j < rowTo; j++) {
                const float vy = (static_cast<float>(outH) * 0.5f - static_cast<float>(j) - 0.5f) / scale;
                for (int i = 0; i < outW; i++) {
                    const float vx = (static_cast<float>(i) + 0.5f - static_cast<float>(outW) * 0.5f) / scale;
                    float mo[3] = {e0[0] + ex[0] * vx + ey[0] * vy, e0[1] + ex[1] * vx + ey[1] * vy,
                                   e0[2] + ex[2] * vx + ey[2] * vy};
                    hits.clear();
                    for (size_t k = 0; k < boxes.size(); k++) {
                        const Screen& sb = bounds[k];
                        if (i < sb.i0 || i > sb.i1 || j < sb.j0 || j > sb.j1)
                            continue;
                        const Box& b = boxes[k];
                        const float* ro = mo;
                        const float* rd = md;
                        float ho[3];
                        if (b.head && headTurned) {
                            toHead(mo, ho, true);
                            ro = ho;
                            rd = hd;
                        }
                        float t;
                        int axis, sign;
                        if (!Intersect(b, ro, rd, t, axis, sign))
                            continue;
                        float p[3] = {ro[0] + rd[0] * t, ro[1] + rd[1] * t, ro[2] + rd[2] * t};
                        float u, v;
                        FaceUv(b, axis, sign, p, u, v);
                        Hit h;
                        h.t = t;
                        if (b.cape)
                            cape.Get(u, v, h.c);
                        else
                            skin.Get(u, v, h.c);
                        if (!b.overlay && !b.cape)
                            h.c[3] = 255;
                        else if (h.c[3] < 20)
                            continue;
                        hits.push_back(h);
                    }
                    if (hits.empty())
                        continue;
                    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.t < b.t; });
                    float r = 0, g = 0, bl = 0, a = 0;
                    for (const Hit& h : hits) {
                        const float ha = static_cast<float>(h.c[3]) / 255.0f;
                        const float w = (1.0f - a) * ha;
                        r += w * static_cast<float>(h.c[0]);
                        g += w * static_cast<float>(h.c[1]);
                        bl += w * static_cast<float>(h.c[2]);
                        a += w;
                        if (a > 0.99f)
                            break;
                    }
                    if (a <= 0.001f)
                        continue;
                    unsigned char* px = &out.pixels[(static_cast<size_t>(j) * static_cast<size_t>(outW) +
                                                     static_cast<size_t>(i)) *
                                                    4];
                    px[0] = static_cast<unsigned char>(std::clamp(r / a, 0.0f, 255.0f));
                    px[1] = static_cast<unsigned char>(std::clamp(g / a, 0.0f, 255.0f));
                    px[2] = static_cast<unsigned char>(std::clamp(bl / a, 0.0f, 255.0f));
                    px[3] = static_cast<unsigned char>(std::clamp(a * 255.0f, 0.0f, 255.0f));
                }
            }
        };

        int top = outH, bottom = -1;
        for (const Screen& s : bounds) {
            top = std::min(top, s.j0);
            bottom = std::max(bottom, s.j1);
        }
        if (bottom < top)
            return out;
        const int rows = bottom - top + 1;
        const int workers = std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, 8);
        if (workers <= 1 || rows < 64) {
            renderRows(top, bottom + 1);
            return out;
        }
        std::vector<std::future<void>> jobs;
        const int chunk = (rows + workers * 4 - 1) / (workers * 4);
        std::atomic<int> next{top};
        jobs.reserve(workers);
        for (int w = 0; w < workers; w++)
            jobs.push_back(std::async(std::launch::async, [&] {
                for (;;) {
                    const int from = next.fetch_add(chunk);
                    if (from > bottom)
                        break;
                    renderRows(from, std::min(from + chunk, bottom + 1));
                }
            }));
        for (auto& j : jobs)
            j.get();
        return out;
    }
}
