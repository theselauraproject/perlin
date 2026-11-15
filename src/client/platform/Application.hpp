#pragma once
#include "client/platform/LaunchArgs.hpp"

#include <functional>
#include <memory>

class AppPlatform;

namespace perlin {
    class Input;

    class Application {
    public:
        virtual ~Application() = default;

        virtual bool Initialize(AppPlatform* platform) = 0;
        virtual void Shutdown() = 0;
        virtual Input& GetInput() = 0;
    };

    using ApplicationFactory = std::function<std::unique_ptr<Application>(const LaunchArgs&)>;
}
