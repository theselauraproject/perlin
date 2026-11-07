#include "world/Blocks.hpp"

#include <algorithm>

namespace perlin::server {
    namespace {
        void PutName(std::string& o, const std::string& s) {
            o.push_back(static_cast<char>(s.size() & 0xFF));
            o.push_back(static_cast<char>((s.size() >> 8) & 0xFF));
            o += s;
        }

        std::string Key(const std::string& name, const StateList& states) {
            std::string k = name;
            for (const auto& [key, v] : states) {
                k += '|' + key + '=';
                if (const bool* b = std::get_if<bool>(&v))
                    k += *b ? "t" : "f";
                else if (const int32_t* i = std::get_if<int32_t>(&v))
                    k += std::to_string(*i);
                else
                    k += std::get<std::string>(v);
            }
            return k;
        }
    }

    int32_t BlockNetworkHash(const std::string& name, const StateList& states) {
        std::string nbt;
        nbt.push_back(10);
        PutName(nbt, "");
        nbt.push_back(8);
        PutName(nbt, "name");
        PutName(nbt, name);
        nbt.push_back(10);
        PutName(nbt, "states");
        for (const auto& [key, v] : states) {
            if (const bool* b = std::get_if<bool>(&v)) {
                nbt.push_back(1);
                PutName(nbt, key);
                nbt.push_back(*b ? 1 : 0);
            } else if (const int32_t* i = std::get_if<int32_t>(&v)) {
                nbt.push_back(3);
                PutName(nbt, key);
                for (int s = 0; s < 4; s++)
                    nbt.push_back(static_cast<char>((static_cast<uint32_t>(*i) >> (8 * s)) & 0xFF));
            } else {
                nbt.push_back(8);
                PutName(nbt, key);
                PutName(nbt, std::get<std::string>(v));
            }
        }
        nbt.push_back(0);
        nbt.push_back(0);
        uint32_t h = 0x811c9dc5u;
        for (const unsigned char c : nbt) {
            h ^= c;
            h *= 0x01000193u;
        }
        return static_cast<int32_t>(h);
    }

    BlockRegistry::BlockRegistry() {
        entries.push_back({"minecraft:air", {}, BlockNetworkHash("minecraft:air", {})});
        byKey["minecraft:air"] = 0;
    }

    BlockRegistry& BlockRegistry::Instance() {
        static BlockRegistry registry;
        return registry;
    }

    uint16_t BlockRegistry::Intern(const std::string& name, StateList states) {
        std::sort(states.begin(), states.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        const std::string key = Key(name, states);
        std::lock_guard lock(mutex);
        if (const auto it = byKey.find(key); it != byKey.end())
            return it->second;
        if (entries.size() >= 0xFFFF)
            return kAir;
        const uint16_t id = static_cast<uint16_t>(entries.size());
        entries.push_back({name, states, BlockNetworkHash(name, states)});
        byKey.emplace(key, id);
        return id;
    }

    uint16_t BlockRegistry::Find(const std::string& name) const {
        std::lock_guard lock(mutex);
        const auto it = byKey.find(name);
        return it == byKey.end() ? kAir : it->second;
    }

    int32_t BlockRegistry::Hash(uint16_t id) const {
        std::lock_guard lock(mutex);
        return id < entries.size() ? entries[id].hash : entries[0].hash;
    }

    std::string BlockRegistry::Name(uint16_t id) const {
        std::lock_guard lock(mutex);
        return id < entries.size() ? entries[id].name : entries[0].name;
    }

    StateList BlockRegistry::States(uint16_t id) const {
        std::lock_guard lock(mutex);
        return id < entries.size() ? entries[id].states : StateList{};
    }

    size_t BlockRegistry::Size() const {
        std::lock_guard lock(mutex);
        return entries.size();
    }
}
