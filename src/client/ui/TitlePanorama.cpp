#include "client/ui/TitlePanorama.hpp"

#include <algorithm>
#include <cmath>

namespace perlin::ui {
    namespace {
        struct V3 {
            float x, y, z;
        };

        V3 Mix(const V3& a, const V3& b, float t) {
            return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
        }
        float Dot(const V3& a, const V3& b) {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }

        constexpr V3 kFace[6][4] = {
            {{-1, 1, -1}, {1, 1, -1}, {1, -1, -1}, {-1, -1, -1}},
            {{1, 1, -1}, {1, 1, 1}, {1, -1, 1}, {1, -1, -1}},
            {{1, 1, 1}, {-1, 1, 1}, {-1, -1, 1}, {1, -1, 1}},
            {{-1, 1, 1}, {-1, 1, -1}, {-1, -1, -1}, {-1, -1, 1}},
            {{-1, 1, 1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, -1}},
            {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}},
        };
    }

    PanoramaView PanoramaViewAt(double seconds) {
        PanoramaView v;
        v.yawDegrees = static_cast<float>(std::fmod(seconds * (360.0 / 240.0), 360.0));
        v.pitchDegrees = static_cast<float>(8.0 + 3.0 * std::sin(seconds * 0.05));
        return v;
    }

    void BuildPanorama(const PanoramaView& view, int width, int height, PanoramaFace (&faces)[6], int grid) {
        constexpr float kDeg = 3.14159265358979f / 180.0f;
        const float yaw = view.yawDegrees * kDeg, pitch = view.pitchDegrees * kDeg;
        const V3 forward{std::cos(pitch) * std::sin(yaw), -std::sin(pitch), -std::cos(pitch) * std::cos(yaw)};
        const V3 right{std::cos(yaw), 0.0f, std::sin(yaw)};
        const V3 up{std::sin(pitch) * std::sin(yaw), std::cos(pitch), -std::sin(pitch) * std::cos(yaw)};
        const float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
        const float ty = std::tan(view.fovYDegrees * 0.5f * kDeg), tx = ty * aspect;
        constexpr float kNear = 0.02f;
        const int n = grid < 1 ? 1 : grid;

        for (int f = 0; f < 6; f++) {
            PanoramaFace& out = faces[f];
            out.vertices.clear();
            out.indices.clear();
            const V3* c = kFace[f];
            std::vector<PanoramaVertex> pts(static_cast<size_t>((n + 1) * (n + 1)));
            std::vector<char> ok(pts.size());
            for (int j = 0; j <= n; j++) {
                const float v = static_cast<float>(j) / static_cast<float>(n);
                const V3 left = Mix(c[0], c[3], v), rightEdge = Mix(c[1], c[2], v);
                for (int i = 0; i <= n; i++) {
                    const float u = static_cast<float>(i) / static_cast<float>(n);
                    const V3 p = Mix(left, rightEdge, u);
                    const float depth = Dot(p, forward);
                    const size_t k = static_cast<size_t>(j * (n + 1) + i);
                    ok[k] = depth > kNear;
                    if (!ok[k])
                        continue;
                    const float nx = Dot(p, right) / (depth * tx), ny = Dot(p, up) / (depth * ty);
                    pts[k] = {(nx + 1.0f) * 0.5f * static_cast<float>(width),
                              (1.0f - ny) * 0.5f * static_cast<float>(height), u, v};
                }
            }
            for (int j = 0; j < n; j++)
                for (int i = 0; i < n; i++) {
                    const size_t a = static_cast<size_t>(j * (n + 1) + i), b = a + 1,
                                 d = a + static_cast<size_t>(n + 1), e = d + 1;
                    if (!ok[a] || !ok[b] || !ok[d] || !ok[e])
                        continue;
                    const float minX =
                        std::fmin(std::fmin(pts[a].x, pts[b].x), std::fmin(pts[d].x, pts[e].x));
                    const float maxX =
                        std::fmax(std::fmax(pts[a].x, pts[b].x), std::fmax(pts[d].x, pts[e].x));
                    const float minY =
                        std::fmin(std::fmin(pts[a].y, pts[b].y), std::fmin(pts[d].y, pts[e].y));
                    const float maxY =
                        std::fmax(std::fmax(pts[a].y, pts[b].y), std::fmax(pts[d].y, pts[e].y));
                    if (maxX < 0 || maxY < 0 || minX > static_cast<float>(width) ||
                        minY > static_cast<float>(height))
                        continue;
                    const uint32_t base = static_cast<uint32_t>(out.vertices.size());
                    out.vertices.push_back(pts[a]);
                    out.vertices.push_back(pts[b]);
                    out.vertices.push_back(pts[e]);
                    out.vertices.push_back(pts[d]);
                    const uint32_t q[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
                    out.indices.insert(out.indices.end(), q, q + 6);
                }
        }
    }

    namespace {
        V3 Sub(const V3& a, const V3& b) {
            return {a.x - b.x, a.y - b.y, a.z - b.z};
        }

        struct FaceBasis {
            V3 origin, u, v, normal;
        };

        FaceBasis Basis(int f) {
            const V3* c = kFace[f];
            return {c[0],
                    Sub(c[1], c[0]),
                    Sub(c[3], c[0]),
                    {(c[0].x + c[2].x) * 0.5f, (c[0].y + c[2].y) * 0.5f, (c[0].z + c[2].z) * 0.5f}};
        }

        void SampleFace(const PanoramaImage& img, float u, float v, float out[4]) {
            const float x =
                std::clamp(u * static_cast<float>(img.width) - 0.5f, 0.0f, static_cast<float>(img.width - 1));
            const float y = std::clamp(v * static_cast<float>(img.height) - 0.5f, 0.0f,
                                       static_cast<float>(img.height - 1));
            const int x0 = static_cast<int>(x), y0 = static_cast<int>(y);
            const int x1 = std::min(x0 + 1, img.width - 1), y1 = std::min(y0 + 1, img.height - 1);
            const float fx = x - static_cast<float>(x0), fy = y - static_cast<float>(y0);
            const auto at = [&](int px, int py, int ch) {
                return static_cast<float>(img.rgba[(static_cast<size_t>(py) * img.width + px) * 4 + ch]);
            };
            for (int ch = 0; ch < 4; ch++) {
                const float top = at(x0, y0, ch) + (at(x1, y0, ch) - at(x0, y0, ch)) * fx;
                const float bottom = at(x0, y1, ch) + (at(x1, y1, ch) - at(x0, y1, ch)) * fx;
                out[ch] = top + (bottom - top) * fy;
            }
        }

        // the face a direction lands on, and where: same corners BuildPanorama draws with
        void SampleCube(const PanoramaImage (&in)[6], const FaceBasis (&basis)[6], const V3& dir,
                        float out[4]) {
            int best = 0;
            float bestDot = -1.0e30f;
            for (int g = 0; g < 6; g++) {
                const float d = Dot(dir, basis[g].normal);
                if (d > bestDot) {
                    bestDot = d;
                    best = g;
                }
            }
            const FaceBasis& b = basis[best];
            const float k = 1.0f / bestDot;
            const V3 rel = Sub({dir.x * k, dir.y * k, dir.z * k}, b.origin);
            SampleFace(in[best], Dot(rel, b.u) * 0.25f, Dot(rel, b.v) * 0.25f, out);
        }

        // sliding-window box blur, edges clamped; buffer is side x side x 4
        void BoxPass(std::vector<float>& buf, std::vector<float>& tmp, int side, int r, bool vertical) {
            const float inv = 1.0f / static_cast<float>(2 * r + 1);
            const size_t step = vertical ? static_cast<size_t>(side) * 4 : 4;
            for (int line = 0; line < side; line++) {
                const size_t base =
                    vertical ? static_cast<size_t>(line) * 4 : static_cast<size_t>(line) * side * 4;
                const auto at = [&](int i) {
                    return &buf[base + static_cast<size_t>(std::clamp(i, 0, side - 1)) * step];
                };
                double sum[4] = {0, 0, 0, 0};
                for (int i = -r; i <= r; i++)
                    for (int ch = 0; ch < 4; ch++)
                        sum[ch] += at(i)[ch];
                for (int i = 0; i < side; i++) {
                    float* o = &tmp[base + static_cast<size_t>(i) * step];
                    for (int ch = 0; ch < 4; ch++)
                        o[ch] = static_cast<float>(sum[ch]) * inv;
                    const float* add = at(i + r + 1);
                    const float* drop = at(i - r);
                    for (int ch = 0; ch < 4; ch++)
                        sum[ch] += static_cast<double>(add[ch]) - drop[ch];
                }
            }
            buf.swap(tmp);
        }
    }

    int PanoramaBlurRadius(int menuBlur, int faceSize, int fbHeight, float fovYDegrees) {
        if (menuBlur <= 0 || faceSize <= 0 || fbHeight <= 0)
            return 0;
        // the game: three box passes of radius menuBlur framebuffer pixels, variance r(r+1)
        const float sigmaPx = std::sqrt(static_cast<float>(menuBlur * (menuBlur + 1)));
        const float texelsPerPx = static_cast<float>(faceSize) *
                                  std::tan(fovYDegrees * 0.5f * 3.14159265358979f / 180.0f) /
                                  static_cast<float>(fbHeight);
        const float sigma = sigmaPx * texelsPerPx;
        const int r = static_cast<int>(std::lround((-1.0f + std::sqrt(1.0f + 4.0f * sigma * sigma)) * 0.5f));
        return std::max(1, r);
    }

    bool BlurPanorama(const PanoramaImage (&in)[6], int size, int radius, PanoramaImage (&out)[6]) {
        if (size <= 0)
            return false;
        for (const PanoramaImage& img : in)
            if (img.width <= 0 || img.height <= 0 ||
                img.rgba.size() < static_cast<size_t>(img.width) * img.height * 4)
                return false;
        radius = std::max(0, radius);
        FaceBasis basis[6];
        for (int g = 0; g < 6; g++)
            basis[g] = Basis(g);
        // three passes reach 3r past the edge
        const int pad = radius * 3 + 2;
        const int side = size + pad * 2;
        std::vector<float> buf(static_cast<size_t>(side) * side * 4), tmp(buf.size());
        for (int f = 0; f < 6; f++) {
            const FaceBasis& b = basis[f];
            for (int j = 0; j < side; j++) {
                const float v = (static_cast<float>(j - pad) + 0.5f) / static_cast<float>(size);
                for (int i = 0; i < side; i++) {
                    const float u = (static_cast<float>(i - pad) + 0.5f) / static_cast<float>(size);
                    float* px = &buf[(static_cast<size_t>(j) * side + i) * 4];
                    if (u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f) {
                        SampleFace(in[f], u, v, px);
                        continue;
                    }
                    const V3 dir{b.origin.x + b.u.x * u + b.v.x * v, b.origin.y + b.u.y * u + b.v.y * v,
                                 b.origin.z + b.u.z * u + b.v.z * v};
                    SampleCube(in, basis, dir, px);
                }
            }
            for (int pass = 0; pass < 3 && radius > 0; pass++) {
                BoxPass(buf, tmp, side, radius, false);
                BoxPass(buf, tmp, side, radius, true);
            }
            PanoramaImage& o = out[f];
            o.width = o.height = size;
            o.rgba.assign(static_cast<size_t>(size) * size * 4, 255);
            for (int j = 0; j < size; j++)
                for (int i = 0; i < size; i++)
                    for (int ch = 0; ch < 3; ch++)
                        o.rgba[(static_cast<size_t>(j) * size + i) * 4 + ch] =
                            static_cast<uint8_t>(std::clamp(
                                std::lround(buf[(static_cast<size_t>(j + pad) * side + i + pad) * 4 + ch]),
                                0L, 255L));
        }
        return true;
    }
}
