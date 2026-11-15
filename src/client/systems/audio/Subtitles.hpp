#pragma once
#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace perlin::audio {
    std::string SubtitleText(const std::string& soundDefinition);

    class Subtitles {
    public:
        struct Line {
            std::string text;
            int direction = 0;
            float brightness = 1.0f;
            float age = 0;
        };

        void Add(const std::string& soundDefinition, const glm::dvec3& position, float volume, bool global,
                 double now);
        std::vector<Line> Visible(const glm::dvec3& listener, float listenerYawDegrees, double now);
        void Clear() { entries.clear(); }
        size_t Count() const { return entries.size(); }
        static constexpr double kLifetime = 3.0;

    private:
        struct Entry {
            std::string text;
            glm::dvec3 position{0.0};
            double time = 0;
            float volume = 1;
            bool global = false;
        };
        std::vector<Entry> entries;
    };
}
