#pragma once
#include <glm/glm.hpp>

#include <cmath>

namespace perlin {
    class FlightRoll {
    public:
        struct Result {
            float yaw = 0.0f;
            float pitch = 0.0f;
        };

        bool Active() const { return active; }
        float Roll() const { return rollDegrees; }
        bool Engaged() const { return active || std::abs(rollDegrees) > 0.01f; }

        void Begin(float yaw, float pitch);
        void End();
        Result Step(float dt, float mouseRight, float mouseUp, float yawKeys, float rollKeys,
                    float pitchKeys);
        void Settle(float dt, float yaw, float pitch);

    private:
        struct Smoother {
            double target = 0.0, remaining = 0.0, last = 0.0;
            double Next(double delta, double factor);
            void Reset() { target = remaining = last = 0.0; }
        };

        void Derive(Result& out);

        glm::dvec3 forward{0.0, 0.0, -1.0}, up{0.0, 1.0, 0.0}, right{1.0, 0.0, 0.0};
        Smoother pitchSmooth, yawSmooth, rollSmooth;
        bool active = false;
        float rollDegrees = 0.0f;
    };
}
