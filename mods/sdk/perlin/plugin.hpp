#pragma once
#include <type_traits>

#include "perlin/runtime.hpp"

namespace perlin {
    class Plugin {
    public:
        virtual ~Plugin() = default;
        virtual void registerCommands(Context&) {}
        virtual void onEnable(Context&) {}
        virtual void onDisable(Context&) {}

        Context& ctx() const { return *ctx_; }

    private:
        template <class T> friend void detail_boot(Context&);
        Context* ctx_ = nullptr;
    };

    using PluginCtx = Context;

    namespace detail_plugin {
        template <class T> T& instance() {
            static T value;
            return value;
        }

#include "perlin/gen/plugin_wire.hpp"
    }

    template <class T> void detail_boot(Context& ctx) {
        static_assert(std::is_base_of_v<Plugin, T>, "a plugin derives from perlin::Plugin");
        T& self = detail_plugin::instance<T>();
        static_cast<Plugin&>(self).ctx_ = &ctx;
        self.registerCommands(ctx);
        detail_plugin::wire_events(self, ctx);
        self.onEnable(ctx);
        ctx.on_unload([&self, &ctx] { self.onDisable(ctx); });
    }
}

#define PERLIN_REGISTER(T)                                                                                   \
    void perlin_plugin_main(::perlin::Context& c) {                                                          \
        ::perlin::detail_boot<T>(c);                                                                         \
    }
