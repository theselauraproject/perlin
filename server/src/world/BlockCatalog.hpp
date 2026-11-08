#pragma once
#include "world/Blocks.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace perlin::server {

    struct CatalogItem {
        std::string name;
        int16_t networkId = 0;
        int32_t blockHash = 0;

        bool block = true;
    };

    class BlockCatalog {
    public:
        static BlockCatalog& Instance();

        bool Load(const std::string& blockStatesPath, const std::string& itemNamesPath, std::string& error,
                  const std::string& extraItemsPath = {});

        void LoadBuiltin();
        bool Loaded() const { return !items.empty(); }
        bool FromFiles() const { return fromFiles; }

        const std::vector<CatalogItem>& Items() const { return items; }
        const CatalogItem* ItemById(int16_t id) const;
        const CatalogItem* ItemByBlockHash(int32_t hash) const;
        const CatalogItem* ItemByName(const std::string& name) const;

        uint16_t RegistryId(int32_t hash) const;

        uint16_t DefaultBlock(const std::string& name) const;
        uint16_t Resolve(std::string_view state) const;
        std::string StateText(uint16_t id) const;
        bool KnownHash(int32_t hash) const { return states.contains(hash); }
        bool Knows(const std::string& fullName) const { return defaultByName.contains(fullName); }
        size_t StateCount() const { return states.size(); }

        static std::string FindFile(const std::string& dir, const std::string& name);

    private:
        struct State {
            std::string name;
            StateList list;
        };
        void BuildItems(const std::vector<std::string>& names,
                        const std::unordered_map<std::string, int32_t>& defaultHash);
        std::unordered_map<int32_t, State> states;
        std::vector<CatalogItem> items;
        std::unordered_map<std::string, int32_t> defaultByName;
        std::unordered_map<int16_t, size_t> byId;
        std::unordered_map<int32_t, size_t> byHash;
        std::unordered_map<std::string, size_t> byName;
        bool fromFiles = false;
        mutable std::mutex resolveMutex;
        mutable std::unordered_map<std::string, uint16_t> resolved;
        mutable std::unordered_map<std::string, std::vector<int32_t>> hashesByName;
    };
}
