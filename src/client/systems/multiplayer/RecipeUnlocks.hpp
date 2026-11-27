#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace perlin::recipes {
    class Unlocks {
    public:
        void Clear() {
            present = false;
            ids.clear();
        }

        template<class Known> void Apply(uint32_t type, const std::vector<std::string>& names, Known known) {
            if (type > 4)
                return;
            present = true;
            switch (type) {
            case 1:
            case 2:
                for (const auto& name : names)
                    if (known(name))
                        ids.insert(name);
                break;
            case 3:
                for (const auto& name : names)
                    ids.erase(name);
                break;
            case 4:
                ids.clear();
                break;
            default:
                break;
            }
        }

        bool Allows(std::string_view id, bool alwaysUnlocked) const {
            return !present || alwaysUnlocked || ids.contains(id);
        }

    private:
        bool present = false;
        std::set<std::string, std::less<>> ids;
    };
}
