#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdint>

namespace perlin {
    inline float JavaSin(float x) {
        static const struct Table {
            float v[65536];
            Table() {
                for (int i = 0; i < 65536; i++)
                    v[i] = static_cast<float>(std::sin(i * 3.141592653589793 * 2.0 / 65536.0));
            }
        } table;
        return table.v[static_cast<int>(x * 10430.378f) & 65535];
    }

    class DamageEffect {
    public:
        static constexpr int kDuration = 10;
        static constexpr float kMaxRoll = 14.0f;
        static constexpr float kOverlayAlpha = 1.0f - 178.0f / 255.0f;

        void Hurt(float hurtDirDegrees) {
            hurtTime = kDuration;
            dir = hurtDirDegrees;
            age = 0;
            directed = true;
        }

        void Redirect(float hurtDirDegrees) {
            if (hurtTime > 0 && age <= 2)
                dir = hurtDirDegrees;
        }

        void Tick() {
            if (hurtTime > 0) {
                hurtTime--;
                age++;
            }
        }

        bool Active() const { return hurtTime > 0; }

        float HurtFraction(float partialTick) const {
            const float f = (static_cast<float>(hurtTime) - partialTick) / static_cast<float>(kDuration);
            return hurtTime > 0 ? (f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f)) : 0.0f;
        }

        glm::mat4 ViewMatrix(float partialTick, int strength) const {
            float f = static_cast<float>(hurtTime) - partialTick;
            if (f < 0.0f || strength <= 0)
                return glm::mat4(1.0f);
            f /= static_cast<float>(kDuration);
            f = JavaSin(f * f * f * f * 3.14159265f);
            const float tilt = -f * kMaxRoll * (static_cast<float>(strength > 100 ? 100 : strength) / 100.0f);
            const glm::vec3 up(0, 1, 0), forward(0, 0, 1);
            glm::mat4 m(1.0f);
            m = glm::rotate(m, glm::radians(-dir), up);
            m = glm::rotate(m, glm::radians(tilt), forward);
            m = glm::rotate(m, glm::radians(dir), up);
            return m;
        }

        float TintAlpha() const { return hurtTime > 0 ? kOverlayAlpha : 0.0f; }

    private:
        int hurtTime = 0;
        int age = 0;
        float dir = 0.0f;
        bool directed = false;
    };
}
