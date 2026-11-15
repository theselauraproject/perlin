#pragma once
#include "client/ui/Scene.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Rml {
    class Context;
    class DataTypeRegister;
}

namespace perlin::ui {
    class SceneModels {
    public:
        SceneModels();
        ~SceneModels();

        bool Attach(Rml::Context& context, const std::string& slot, const Scene& scene);
        void Detach(Rml::Context& context, const std::string& slot);
        void Forget();

    private:
        struct Entry;
        std::unique_ptr<Rml::DataTypeRegister> types;
        std::map<std::string, std::unique_ptr<Entry>> entries;
    };
}
