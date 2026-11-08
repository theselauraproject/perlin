#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace perlin::server::vanilla {

    class DataPack {
    public:
        ~DataPack();

        static std::unique_ptr<DataPack> Embedded();

        std::string Read(const std::string& relative) const;
        bool Has(const std::string& relative) const;
        std::vector<std::string> List(const std::string& relativePrefix) const;

    private:
        DataPack() = default;
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
