#include "client/systems/renderer/gpu/PackSkyMesh.hpp"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace perlin::gpu {

    void PackMesh::Vertex(float x, float y, float z, const uint8_t rgba[4], float u, float v) {
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

    void PackMesh::Quad(const float centre[3], const float right[3], const float up[3], const uint8_t rgba[4],
                        float u0, float v0, float u1, float v1) {
        const glm::vec3 c(centre[0], centre[1], centre[2]), r(right[0], right[1], right[2]),
            u(up[0], up[1], up[2]);
        const glm::vec3 p0 = c - r - u, p1 = c + r - u, p2 = c + r + u, p3 = c - r + u;
        Vertex(p0.x, p0.y, p0.z, rgba, u0, v1);
        Vertex(p1.x, p1.y, p1.z, rgba, u1, v1);
        Vertex(p2.x, p2.y, p2.z, rgba, u1, v0);
        Vertex(p0.x, p0.y, p0.z, rgba, u0, v1);
        Vertex(p2.x, p2.y, p2.z, rgba, u1, v0);
        Vertex(p3.x, p3.y, p3.z, rgba, u0, v0);
    }

}

namespace perlin::gpu::sky {
    namespace {
        constexpr float kTwoPi = 6.2831853f;
        constexpr int kFanSegments = 16;

        uint8_t ToByte(float v) {
            return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f);
        }

        void Quad(PackMesh& mesh, const glm::vec3& c, const glm::vec3& r, const glm::vec3& u,
                  const uint8_t rgba[4], float u0, float v0, float u1, float v1) {
            mesh.Quad(&c.x, &r.x, &u.x, rgba, u0, v0, u1, v1);
        }

        class Lcg {
        public:
            float Next() {
                seed = seed * 1664525u + 1013904223u;
                return static_cast<float>((seed >> 8) & 0xFFFFFF) / 16777216.0f;
            }

        private:
            uint32_t seed = 10842u;
        };
    }

    PackMesh EndBox() {
        PackMesh mesh;
        const uint8_t grey[4] = {40, 40, 40, 255};
        const glm::mat4 faces[6] = {
            glm::mat4(1.0f),
            glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0)),
            glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1, 0, 0)),
            glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1, 0, 0)),
            glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 0, 1)),
            glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0, 0, 1)),
        };
        const float uv[4][2] = {{0, 0}, {0, 16}, {16, 16}, {16, 0}};
        for (const glm::mat4& f : faces) {
            const glm::vec3 p[4] = {
                glm::vec3(f * glm::vec4(-100, -100, -100, 1)), glm::vec3(f * glm::vec4(-100, -100, 100, 1)),
                glm::vec3(f * glm::vec4(100, -100, 100, 1)), glm::vec3(f * glm::vec4(100, -100, -100, 1))};
            for (int k : {0, 1, 2, 0, 2, 3})
                mesh.Vertex(p[k].x, p[k].y, p[k].z, grey, uv[k][0], uv[k][1]);
        }
        return mesh;
    }

    StarField Stars() {
        StarField field;
        const uint8_t white[4] = {255, 255, 255, 255};
        Lcg rnd;
        for (int i = 0; i < 1500; i++) {
            const float z = rnd.Next() * 2.0f - 1.0f, t = rnd.Next() * kTwoPi,
                        r = std::sqrt(std::max(0.0f, 1.0f - z * z));
            const glm::vec3 dir(r * std::cos(t), z, r * std::sin(t));
            const glm::vec3 right = glm::normalize(
                glm::cross(dir, std::fabs(dir.y) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0)));
            const glm::vec3 up = glm::cross(dir, right);
            const float size = 0.15f + rnd.Next() * 0.1f;
            Quad(field.mesh, dir * 100.0f, right * size, up * size, white, 0, 0, 1, 1);
        }
        field.starVertices = field.mesh.count;
        Quad(field.mesh, glm::vec3(0, 100, 0), glm::vec3(0, 0, 30), glm::vec3(30, 0, 0), white, 0, 0, 1, 1);
        return field;
    }

    PackMesh Dome(const float skyColor[3]) {
        PackMesh mesh;
        const uint8_t colour[4] = {ToByte(skyColor[0]), ToByte(skyColor[1]), ToByte(skyColor[2]), 255};
        constexpr int kLat = 12, kLon = 24;
        const auto point = [](int la, int lo) {
            const float phi = (static_cast<float>(la) / kLat - 0.5f) * 3.14159265f,
                        th = static_cast<float>(lo) / kLon * kTwoPi;
            return glm::vec3(std::cos(phi) * std::cos(th), std::sin(phi), std::cos(phi) * std::sin(th)) *
                   100.0f;
        };
        for (int la = 0; la < kLat; la++)
            for (int lo = 0; lo < kLon; lo++) {
                const glm::vec3 p00 = point(la, lo), p10 = point(la + 1, lo), p11 = point(la + 1, lo + 1),
                                p01 = point(la, lo + 1);
                for (const glm::vec3* p : {&p00, &p10, &p11, &p00, &p11, &p01})
                    mesh.Vertex(p->x, p->y, p->z, colour, 0, 0);
            }
        return mesh;
    }

    PackMesh SunriseFan(const float sunrise[4], float sunAngle) {
        PackMesh mesh;
        const uint8_t centre[4] = {ToByte(sunrise[0]), ToByte(sunrise[1]), ToByte(sunrise[2]),
                                   ToByte(sunrise[3])};
        const uint8_t rim[4] = {centre[0], centre[1], centre[2], 0};
        const float sinSun = std::sin(sunAngle);
        const glm::mat4 fan =
            glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0)) *
            glm::rotate(glm::mat4(1.0f), glm::radians(sinSun < 0.0f ? 180.0f : 0.0f), glm::vec3(0, 0, 1)) *
            glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 0, 1));
        const glm::vec3 mid = glm::vec3(fan * glm::vec4(0, 100, 0, 1));
        glm::vec3 prev(0.0f);
        for (int i = 0; i <= kFanSegments; i++) {
            const float t = static_cast<float>(i) * kTwoPi / static_cast<float>(kFanSegments);
            const float sn = std::sin(t), cs = std::cos(t);
            const glm::vec3 p =
                glm::vec3(fan * glm::vec4(sn * 120.0f, cs * 120.0f, -cs * 40.0f * sunrise[3], 1));
            if (i > 0) {
                mesh.Vertex(mid.x, mid.y, mid.z, centre, 0, 0);
                mesh.Vertex(prev.x, prev.y, prev.z, rim, 0, 0);
                mesh.Vertex(p.x, p.y, p.z, rim, 0, 0);
            }
            prev = p;
        }
        return mesh;
    }

    PackMesh MoonQuad(float moonPhase) {
        PackMesh mesh;
        const uint8_t white[4] = {255, 255, 255, 255};
        const int phase = static_cast<int>(std::floor(moonPhase)) & 7;
        const float u0 = static_cast<float>(phase % 4) / 4.0f, v0 = static_cast<float>(phase / 4) / 2.0f;
        Quad(mesh, glm::vec3(0, -100, 0), glm::vec3(0, 0, 20), glm::vec3(20, 0, 0), white, u0, v0, u0 + 0.25f,
             v0 + 0.5f);
        return mesh;
    }

    PackMesh DarkDisc() {
        PackMesh mesh;
        const uint8_t black[4] = {0, 0, 0, 255};
        glm::vec3 prev(0.0f);
        for (int i = 0; i <= kFanSegments; i++) {
            const float t = static_cast<float>(i) * kTwoPi / static_cast<float>(kFanSegments);
            const glm::vec3 p(std::sin(t) * 512.0f, -16.0f, std::cos(t) * 512.0f);
            if (i > 0) {
                mesh.Vertex(0.0f, -16.0f, 0.0f, black, 0, 0);
                mesh.Vertex(p.x, p.y, p.z, black, 0, 0);
                mesh.Vertex(prev.x, prev.y, prev.z, black, 0, 0);
            }
            prev = p;
        }
        return mesh;
    }

}
