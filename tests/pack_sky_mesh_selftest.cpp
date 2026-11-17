#include "client/systems/renderer/gpu/PackSkyMesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace perlin::gpu;

namespace {
    int failures = 0;

    struct Reference {
        std::vector<uint8_t> bytes;
        uint32_t count = 0;
        void Vertex(float x, float y, float z, const uint8_t rgba[4], float u, float v) {
            uint8_t px[kPackVertexBytes] = {};
            const float pos[3] = {x, y, z};
            std::memcpy(px + kPos, pos, 12);
            std::memcpy(px + kColor, rgba, 4);
            const float uv[2] = {u, v}, light[2] = {240.0f, 240.0f};
            std::memcpy(px + kUv, uv, 8);
            std::memcpy(px + kLight, light, 8);
            const uint16_t n[4] = {0, 0x3C00, 0, 0};
            std::memcpy(px + kNormal, n, 8);
            bytes.insert(bytes.end(), px, px + kPackVertexBytes);
            count++;
        }
        void Quad(const glm::vec3& c, const glm::vec3& r, const glm::vec3& u, const uint8_t rgba[4], float u0,
                  float v0, float u1, float v1) {
            const glm::vec3 p0 = c - r - u, p1 = c + r - u, p2 = c + r + u, p3 = c - r + u;
            Vertex(p0.x, p0.y, p0.z, rgba, u0, v1);
            Vertex(p1.x, p1.y, p1.z, rgba, u1, v1);
            Vertex(p2.x, p2.y, p2.z, rgba, u1, v0);
            Vertex(p0.x, p0.y, p0.z, rgba, u0, v1);
            Vertex(p2.x, p2.y, p2.z, rgba, u1, v0);
            Vertex(p3.x, p3.y, p3.z, rgba, u0, v0);
        }
    };

    void Check(bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL %s\n", what);
            failures++;
        }
    }

    bool Same(const Reference& ref, const PackMesh& mesh) {
        return ref.count == mesh.count && ref.bytes == mesh.bytes;
    }

    Reference RefEnd() {
        Reference sb;
        const uint8_t grey[4] = {40, 40, 40, 255};
        const glm::mat4 faces[6] = {
            glm::mat4(1.0f),
            glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0)),
            glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1, 0, 0)),
            glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1, 0, 0)),
            glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 0, 1)),
            glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0, 0, 1)),
        };
        for (const glm::mat4& f : faces) {
            const glm::vec3 p[4] = {
                glm::vec3(f * glm::vec4(-100, -100, -100, 1)), glm::vec3(f * glm::vec4(-100, -100, 100, 1)),
                glm::vec3(f * glm::vec4(100, -100, 100, 1)), glm::vec3(f * glm::vec4(100, -100, -100, 1))};
            const float uv[4][2] = {{0, 0}, {0, 16}, {16, 16}, {16, 0}};
            for (int k : {0, 1, 2, 0, 2, 3})
                sb.Vertex(p[k].x, p[k].y, p[k].z, grey, uv[k][0], uv[k][1]);
        }
        return sb;
    }

    Reference RefStars(uint32_t& starCount) {
        Reference sb;
        const uint8_t white[4] = {255, 255, 255, 255};
        uint32_t seed = 10842u;
        const auto rnd = [&]() {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<float>((seed >> 8) & 0xFFFFFF) / 16777216.0f;
        };
        for (int i = 0; i < 1500; i++) {
            const float z = rnd() * 2.0f - 1.0f, t = rnd() * 6.2831853f,
                        r = std::sqrt(std::max(0.0f, 1.0f - z * z));
            const glm::vec3 dir(r * std::cos(t), z, r * std::sin(t));
            const glm::vec3 right = glm::normalize(
                glm::cross(dir, std::fabs(dir.y) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0)));
            const glm::vec3 up = glm::cross(dir, right);
            const float size = 0.15f + rnd() * 0.1f;
            sb.Quad(dir * 100.0f, right * size, up * size, white, 0, 0, 1, 1);
        }
        starCount = sb.count;
        sb.Quad(glm::vec3(0, 100, 0), glm::vec3(0, 0, 30), glm::vec3(30, 0, 0), white, 0, 0, 1, 1);
        return sb;
    }

    Reference RefDome(const float* skyColor) {
        Reference sb;
        const uint8_t sc[4] = {static_cast<uint8_t>(std::clamp(skyColor[0], 0.0f, 1.0f) * 255.0f),
                               static_cast<uint8_t>(std::clamp(skyColor[1], 0.0f, 1.0f) * 255.0f),
                               static_cast<uint8_t>(std::clamp(skyColor[2], 0.0f, 1.0f) * 255.0f), 255};
        constexpr int kLat = 12, kLon = 24;
        const auto point = [](int la, int lo) {
            const float phi = (static_cast<float>(la) / kLat - 0.5f) * 3.14159265f,
                        th = static_cast<float>(lo) / kLon * 6.2831853f;
            return glm::vec3(std::cos(phi) * std::cos(th), std::sin(phi), std::cos(phi) * std::sin(th)) *
                   100.0f;
        };
        for (int la = 0; la < kLat; la++)
            for (int lo = 0; lo < kLon; lo++) {
                const glm::vec3 p00 = point(la, lo), p10 = point(la + 1, lo), p11 = point(la + 1, lo + 1),
                                p01 = point(la, lo + 1);
                sb.Vertex(p00.x, p00.y, p00.z, sc, 0, 0);
                sb.Vertex(p10.x, p10.y, p10.z, sc, 0, 0);
                sb.Vertex(p11.x, p11.y, p11.z, sc, 0, 0);
                sb.Vertex(p00.x, p00.y, p00.z, sc, 0, 0);
                sb.Vertex(p11.x, p11.y, p11.z, sc, 0, 0);
                sb.Vertex(p01.x, p01.y, p01.z, sc, 0, 0);
            }
        return sb;
    }

    Reference RefSunrise(const float* sunrise, double a) {
        Reference sb;
        const auto c8 = [](float v) { return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
        const uint8_t centre[4] = {c8(sunrise[0]), c8(sunrise[1]), c8(sunrise[2]), c8(sunrise[3])};
        const uint8_t rim[4] = {centre[0], centre[1], centre[2], 0};
        const float sinSun = std::sin(static_cast<float>(a));
        const glm::mat4 fan =
            glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0)) *
            glm::rotate(glm::mat4(1.0f), glm::radians(sinSun < 0.0f ? 180.0f : 0.0f), glm::vec3(0, 0, 1)) *
            glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 0, 1));
        const glm::vec3 mid = glm::vec3(fan * glm::vec4(0, 100, 0, 1));
        glm::vec3 prev;
        for (int i = 0; i <= 16; i++) {
            const float t = static_cast<float>(i) * 6.2831853f / 16.0f;
            const float sn = std::sin(t), cs = std::cos(t);
            const glm::vec3 p =
                glm::vec3(fan * glm::vec4(sn * 120.0f, cs * 120.0f, -cs * 40.0f * sunrise[3], 1));
            if (i > 0) {
                sb.Vertex(mid.x, mid.y, mid.z, centre, 0, 0);
                sb.Vertex(prev.x, prev.y, prev.z, rim, 0, 0);
                sb.Vertex(p.x, p.y, p.z, rim, 0, 0);
            }
            prev = p;
        }
        return sb;
    }

    Reference RefMoon(float moonPhase) {
        Reference sb;
        const uint8_t white[4] = {255, 255, 255, 255};
        const int phase = static_cast<int>(std::floor(moonPhase)) & 7;
        const float u0 = static_cast<float>(phase % 4) / 4.0f, v0 = static_cast<float>(phase / 4) / 2.0f;
        sb.Quad(glm::vec3(0, -100, 0), glm::vec3(0, 0, 20), glm::vec3(20, 0, 0), white, u0, v0, u0 + 0.25f,
                v0 + 0.5f);
        return sb;
    }

    Reference RefDisc() {
        Reference sb;
        const uint8_t black[4] = {0, 0, 0, 255};
        glm::vec3 prev(0.0f);
        for (int i = 0; i <= 16; i++) {
            const float t = static_cast<float>(i) * 6.2831853f / 16.0f;
            const glm::vec3 p(std::sin(t) * 512.0f, -16.0f, std::cos(t) * 512.0f);
            if (i > 0) {
                sb.Vertex(0.0f, -16.0f, 0.0f, black, 0, 0);
                sb.Vertex(p.x, p.y, p.z, black, 0, 0);
                sb.Vertex(prev.x, prev.y, prev.z, black, 0, 0);
            }
            prev = p;
        }
        return sb;
    }
}

int main() {
    Check(Same(RefEnd(), sky::EndBox()), "end box matches original");
    uint32_t refStars = 0;
    const Reference stars = RefStars(refStars);
    const sky::StarField field = sky::Stars();
    Check(Same(stars, field.mesh) && field.starVertices == refStars, "stars match original");
    Check(field.starVertices == 1500u * 6u && field.mesh.count == field.starVertices + 6, "star counts");
    for (const auto& colour :
         {std::array<float, 3>{0.4f, 0.6f, 1.0f}, std::array<float, 3>{-1.0f, 2.0f, 0.5f}})
        Check(Same(RefDome(colour.data()), sky::Dome(colour.data())), "dome matches original");
    for (float angle : {0.0f, 1.0f, 3.5f, 5.9f}) {
        const float sunrise[4] = {1.0f, 0.5f, 0.2f, 0.7f};
        Check(Same(RefSunrise(sunrise, angle), sky::SunriseFan(sunrise, angle)), "sunrise matches original");
    }
    for (float phase : {0.0f, 1.0f, 3.0f, 4.0f, 7.0f, 9.0f, -1.0f})
        Check(Same(RefMoon(phase), sky::MoonQuad(phase)), "moon matches original");
    Check(Same(RefDisc(), sky::DarkDisc()), "dark disc matches original");
    Check(sky::Dome(std::array<float, 3>{0, 0, 0}.data()).count == 12u * 24u * 6u, "dome vertex count");
    Check(sky::SunriseFan(std::array<float, 4>{1, 1, 1, 1}.data(), 0.0f).count == 16u * 3u,
          "sunrise vertex count");
    if (failures == 0)
        std::printf("pack_sky_mesh_selftest OK\n");
    return failures == 0 ? 0 : 1;
}
