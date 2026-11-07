#pragma once
#include <array>
#include <charconv>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <tuple>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <utility>
#include <vector>

#include "perlin/abi.hpp"
#include "perlin/act.hpp"
#include "perlin/game.hpp"
#include "perlin/gfx.hpp"
#include "perlin/input.hpp"
#include "perlin/io.hpp"
#include "perlin/rpc.hpp"
#include "perlin/settings.hpp"
#include "perlin/types.hpp"
#include "perlin/ui.hpp"

namespace perlin {
    enum class Flow { pass, cancel };

    namespace log {
        inline void write(int level, std::string_view text) {
            abi::Writer w;
            w.put<int32_t>(level).str(text);
            (void)detail::call_void(Op::Log, w);
        }
        template <class... Args> void debug(std::format_string<Args...> fmt, Args&&... args) {
            write(0, std::format(fmt, std::forward<Args>(args)...));
        }
        template <class... Args> void info(std::format_string<Args...> fmt, Args&&... args) {
            write(1, std::format(fmt, std::forward<Args>(args)...));
        }
        template <class... Args> void warn(std::format_string<Args...> fmt, Args&&... args) {
            write(2, std::format(fmt, std::forward<Args>(args)...));
        }
        template <class... Args> void error(std::format_string<Args...> fmt, Args&&... args) {
            write(3, std::format(fmt, std::forward<Args>(args)...));
        }
    }

    template <abi::Event E> struct EventTag {
        using Payload = abi::payload_t<E>;
        static constexpr abi::Event id = E;
    };

}

#include "perlin/gen/event_tags.hpp"

namespace perlin {

    template <class T>
    concept EventTagLike = requires {
        typename T::Payload;
        { T::id } -> std::convertible_to<abi::Event>;
    };

    enum class Key : int {
        None = 0,
        A = 'A',
        B,
        C,
        D,
        E,
        F,
        G,
        H,
        I,
        J,
        K,
        L,
        M,
        N,
        O,
        P,
        Q,
        R,
        S,
        T,
        U,
        V,
        W,
        X,
        Y,
        Z,
        Num0 = '0',
        Num1,
        Num2,
        Num3,
        Num4,
        Num5,
        Num6,
        Num7,
        Num8,
        Num9,
        F1 = 0x70,
        F2,
        F3,
        F4,
        F5,
        F6,
        F7,
        F8,
        F9,
        F10,
        F11,
        F12,
        Space = 0x20,
        Tab = 0x09,
        Enter = 0x0D,
        Escape = 0x1B,
        Backspace = 0x08,
        Shift = 0x10,
        Control = 0x11,
        Alt = 0x12,
        Left = 0x25,
        Up,
        Right,
        Down,
        Home = 0x24,
        End = 0x23,
        PageUp = 0x21,
        PageDown = 0x22,
        Delete = 0x2E,
        MouseMiddle = 1000,
        MouseLeft,
        MouseRight,
    };

    inline std::string key_id(Key key) {
        const int k = static_cast<int>(key);
        if (k >= 'A' && k <= 'Z')
            return std::string("key.keyboard.") + static_cast<char>(k - 'A' + 'a');
        if (k >= '0' && k <= '9')
            return std::string("key.keyboard.") + static_cast<char>(k);
        if (k >= 0x70 && k <= 0x7B)
            return "key.keyboard.f" + std::to_string(k - 0x70 + 1);
        switch (key) {
        case Key::Space:
            return "key.keyboard.space";
        case Key::Tab:
            return "key.keyboard.tab";
        case Key::Enter:
            return "key.keyboard.enter";
        case Key::Shift:
            return "key.keyboard.shift";
        case Key::Control:
            return "key.keyboard.control";
        case Key::Alt:
            return "key.keyboard.alt";
        case Key::Left:
            return "key.keyboard.left";
        case Key::Up:
            return "key.keyboard.up";
        case Key::Right:
            return "key.keyboard.right";
        case Key::Down:
            return "key.keyboard.down";
        case Key::Home:
            return "key.keyboard.home";
        case Key::End:
            return "key.keyboard.end";
        case Key::PageUp:
            return "key.keyboard.page.up";
        case Key::PageDown:
            return "key.keyboard.page.down";
        case Key::Delete:
            return "key.keyboard.delete";
        case Key::MouseMiddle:
            return "key.mouse.middle";
        case Key::MouseLeft:
            return "key.mouse.left";
        case Key::MouseRight:
            return "key.mouse.right";
        default:
            return {};
        }
    }

    inline Result<void> set_mod_icon(std::string_view relative_png) {
        abi::Writer w;
        w.write(relative_png);
        return detail::call_void(Op::IconSet, w);
    }

    struct CommandCall {
        std::string name;
        std::string raw_args;
        std::vector<std::string> args;

        size_t size() const { return args.size(); }
        bool empty() const { return args.empty(); }
        const std::string& operator[](size_t i) const { return args[i]; }
        std::string_view arg_or(size_t i, std::string_view fallback) const {
            return i < args.size() ? std::string_view(args[i]) : fallback;
        }

        template <class... A> void reply(std::format_string<A...> fmt, A&&... a) const {
            (void)act::message(fmt, std::forward<A>(a)...);
        }
    };

    namespace detail_cmd {
        template <class T> struct is_optional : std::false_type {};
        template <class T> struct is_optional<std::optional<T>> : std::true_type {};

        template <class F> struct signature : signature<decltype(&F::operator())> {};
        template <class C, class R, class... A> struct signature<R (C::*)(A...) const> {
            using args = std::tuple<std::remove_cvref_t<A>...>;
        };
        template <class C, class R, class... A> struct signature<R (C::*)(A...)> {
            using args = std::tuple<std::remove_cvref_t<A>...>;
        };
        template <class R, class... A> struct signature<R (*)(A...)> {
            using args = std::tuple<std::remove_cvref_t<A>...>;
        };

        template <class T> bool parse_value(std::string_view token, T& out) {
            if constexpr (std::is_same_v<T, bool>) {
                if (token == "true" || token == "on" || token == "1" || token == "yes")
                    out = true;
                else if (token == "false" || token == "off" || token == "0" || token == "no")
                    out = false;
                else
                    return false;
                return true;
            } else if constexpr (std::is_same_v<T, std::string>) {
                out = std::string(token);
                return true;
            } else if constexpr (std::is_enum_v<T>) {
                const auto value = meta::enum_cast_nocase<T>(token);
                if (!value)
                    return false;
                out = *value;
                return true;
            } else if constexpr (std::is_floating_point_v<T>) {
                return perlin::detail::parse_floating(token, out);
            } else {
                static_assert(std::is_integral_v<T>, "unsupported command argument type");
                const auto r = std::from_chars(token.data(), token.data() + token.size(), out);
                return r.ec == std::errc{} && r.ptr == token.data() + token.size();
            }
        }

        template <class T> bool parse_arg(const CommandCall& call, size_t index, T& out) {
            if constexpr (is_optional<T>::value) {
                if (index >= call.args.size()) {
                    out = std::nullopt;
                    return true;
                }
                typename T::value_type inner{};
                if (!parse_value<typename T::value_type>(call.args[index], inner))
                    return false;
                out = std::move(inner);
                return true;
            } else {
                return index < call.args.size() && parse_value<T>(call.args[index], out);
            }
        }

        template <class T> std::string describe() {
            if constexpr (is_optional<T>::value) {
                return "[" + describe<typename T::value_type>().substr(1);
            } else if constexpr (std::is_same_v<T, bool>) {
                return "<on|off>";
            } else if constexpr (std::is_same_v<T, std::string>) {
                return "<text>";
            } else if constexpr (std::is_enum_v<T>) {
                std::string out = "<";
                for (size_t i = 0; i < meta::enum_count<T>; i++) {
                    if (i)
                        out += '|';
                    out += meta::enum_names<T>[i];
                }
                return out + ">";
            } else if constexpr (std::is_floating_point_v<T>) {
                return "<number>";
            } else {
                return "<integer>";
            }
        }

        template <class T> std::string describe_closed() {
            std::string text = describe<T>();
            if (is_optional<T>::value && !text.empty())
                text.back() = ']';
            return text;
        }

        template <class Tuple, size_t... I> std::string usage_of(std::index_sequence<I...>) {
            std::string out;
            ((out += " " + describe_closed<std::tuple_element_t<I, Tuple>>()), ...);
            return out;
        }

        template <class F> constexpr bool takes_call() {
            using Args = typename signature<std::remove_cvref_t<F>>::args;
            if constexpr (std::tuple_size_v<Args> == 1)
                return std::is_same_v<std::tuple_element_t<0, Args>, CommandCall>;
            else
                return false;
        }
    }

    enum class TimerId : uint32_t {};

    class Context {
    public:
        using Duration = std::chrono::duration<double>;

        template <EventTagLike Tag, class Fn> void on(Fn&& fn) {
            add_handler(Tag::id, wrap<typename Tag::Payload>(std::forward<Fn>(fn)));
        }
        template <abi::Event E, class Fn> void on(Fn&& fn) { on<EventTag<E>>(std::forward<Fn>(fn)); }

        template <class Fn> void on_chat(Fn&& fn) { on<event::ChatReceived>(std::forward<Fn>(fn)); }
        template <class Fn> void on_tick(Fn&& fn) { on<event::Tick>(std::forward<Fn>(fn)); }
        template <class Fn> void on_frame(Fn&& fn) { on<event::Frame>(std::forward<Fn>(fn)); }
        template <class Fn> void on_unload(Fn&& fn) { unload_handlers.push_back(std::forward<Fn>(fn)); }

        template <class Rep, class Period, class Fn>
        TimerId after(std::chrono::duration<Rep, Period> delay, Fn&& fn) {
            return add_timer(std::chrono::duration_cast<Duration>(delay), Duration::zero(),
                             std::forward<Fn>(fn));
        }
        template <class Rep, class Period, class Fn>
        TimerId every(std::chrono::duration<Rep, Period> period, Fn&& fn) {
            const auto p = std::chrono::duration_cast<Duration>(period);
            return add_timer(p, p, std::forward<Fn>(fn));
        }
        void cancel(TimerId timer) {
            for (auto& t : timers)
                if (t.id == timer)
                    t.alive = false;
        }

        template <meta::fixed_string Name, class Fn> void command(std::string_view description, Fn&& fn) {
            using Fun = std::remove_cvref_t<Fn>;
            const std::string name(Name.view());
            if constexpr (detail_cmd::takes_call<Fun>()) {
                add_command(name, "", std::string(description),
                            [f = std::forward<Fn>(fn)](const CommandCall& c) mutable { f(c); });
            } else {
                using Args = typename detail_cmd::signature<Fun>::args;
                const std::string usage =
                    detail_cmd::usage_of<Args>(std::make_index_sequence<std::tuple_size_v<Args>>{});
                add_command(name, usage, std::string(description),
                            [f = std::forward<Fn>(fn), usage, name](const CommandCall& c) mutable {
                                Args values{};
                                const bool ok = [&]<size_t... I>(std::index_sequence<I...>) {
                                    return (detail_cmd::parse_arg<std::tuple_element_t<I, Args>>(
                                                c, I, std::get<I>(values)) &&
                                            ...);
                                }(std::make_index_sequence<std::tuple_size_v<Args>>{});
                                if (!ok) {
                                    c.reply("Usage: /{}{}", name, usage);
                                    return;
                                }
                                std::apply(f, std::move(values));
                            });
            }
        }

        template <class Fn>
        void command(std::string name, std::string usage, std::string description, Fn&& fn) {
            add_command(name, usage, description, std::forward<Fn>(fn));
        }

        template <meta::fixed_string Id, class Fn>
        void bind(std::string_view label, Key default_key, Fn&& fn) {
            bind(std::string(Id.view()), std::string(label), default_key, std::forward<Fn>(fn));
        }
        template <class Fn> void bind(std::string id, std::string label, Key default_key, Fn&& fn) {
            abi::Writer w;
            w.write(id, label, key_id(default_key));
            (void)detail::call_void(Op::KeyRegister, w);
            keys[id] = wrap_key(std::forward<Fn>(fn));
        }

        template <class C, class R, class... A> static auto member(C* self, R (C::*fn)(A...)) {
            return [self, fn](A... a) -> R { return (self->*fn)(std::forward<A>(a)...); };
        }
        template <class C, class R, class... A> static auto member(const C* self, R (C::*fn)(A...) const) {
            return [self, fn](A... a) -> R { return (self->*fn)(std::forward<A>(a)...); };
        }
        template <meta::fixed_string Name, class C, class F>
            requires std::is_member_function_pointer_v<F>
        void command(std::string_view description, C* self, F fn) {
            command<Name>(description, member(self, fn));
        }
        template <meta::fixed_string Id, class C, class F>
            requires std::is_member_function_pointer_v<F>
        void bind(std::string_view label, Key default_key, C* self, F fn) {
            bind<Id>(label, default_key, member(self, fn));
        }
        template <meta::fixed_string Layer, class C, class F>
            requires std::is_member_function_pointer_v<F>
        void hud(C* self, F fn, int z = 0) {
            hud<Layer>(member(self, fn), z);
        }
        Context& raw() { return *this; }

        template <meta::fixed_string Layer, class Fn> void hud(Fn&& fn, int z = 0) {
            hud(std::string(Layer.view()), std::forward<Fn>(fn), z);
        }
        template <class Fn> void hud(std::string layer, Fn&& fn, int z = 0) {
            layers.push_back({std::move(layer), z, std::forward<Fn>(fn), {}});
        }

        template <class... Args> void print(std::format_string<Args...> fmt, Args&&... args) {
            (void)act::message(fmt, std::forward<Args>(args)...);
        }

        template <class T, class Fn> void on_change(const Setting<T>& setting, Fn&& fn) {
            change_handlers[std::string(setting.id())].push_back(
                [f = std::forward<Fn>(fn)](const std::string& value) mutable {
                    if constexpr (std::is_invocable_v<Fn&, T>)
                        f(detail_settings::parse<T>(value));
                    else
                        f();
                });
        }
        template <class Fn> void on_press(const Button& button, Fn&& fn) {
            press_handlers[std::string(button.id())] = std::forward<Fn>(fn);
        }
        template <class Fn> void on_setting_changed(Fn&& fn) {
            setting_observers.push_back(std::forward<Fn>(fn));
        }

        Config& config() {
            if (!cfg)
                cfg = std::make_unique<Config>();
            return *cfg;
        }

        uint64_t tick_count() const { return ticks; }
        double uptime() const { return elapsed; }

        void dispatch(abi::Event id, abi::Reader& r, uint32_t& flags);
        void run_unload();

        using Subscriptions = std::array<uint32_t, abi::kSubscriptionWords>;
        Subscriptions subscriptions() const {
            Subscriptions mask{};
            const auto set = [&mask](uint32_t id) {
                if (id / 32 < mask.size())
                    mask[id / 32] |= 1u << (id % 32);
            };
            set(static_cast<uint32_t>(abi::Event::HttpResponse));
            set(static_cast<uint32_t>(abi::Event::SettingChanged));
            for (const auto& entry : handlers)
                if (!entry.second.empty())
                    set(static_cast<uint32_t>(entry.first));
            if (!timers.empty())
                set(static_cast<uint32_t>(abi::Event::Tick));
            if (!layers.empty())
                set(static_cast<uint32_t>(abi::Event::Frame));
            if (!commands.empty())
                set(static_cast<uint32_t>(abi::Event::Command));
            if (!keys.empty())
                set(static_cast<uint32_t>(abi::Event::Key));
            return mask;
        }

    private:
        struct Timer {
            TimerId id;
            double due, period;
            std::function<void()> fn;
            bool alive = true;
        };
        struct Layer {
            std::string name;
            int z;
            std::function<void(ui::Canvas&)> draw;
            std::vector<uint8_t> last;
        };
        using Handler = std::function<Flow(abi::Reader&)>;

        template <class P, class Fn> Handler wrap(Fn&& fn) {
            return [f = std::forward<Fn>(fn)](abi::Reader& r) mutable -> Flow {
                if constexpr (std::is_same_v<P, abi::EventNothing>) {
                    if constexpr (std::is_void_v<std::invoke_result_t<Fn&>>) {
                        f();
                        return Flow::pass;
                    } else {
                        return f();
                    }
                } else {
                    P payload = r.read<P>();
                    if constexpr (std::is_invocable_v<Fn&>) {
                        if constexpr (std::is_void_v<std::invoke_result_t<Fn&>>) {
                            f();
                            return Flow::pass;
                        } else {
                            return f();
                        }
                    } else if constexpr (std::is_void_v<std::invoke_result_t<Fn&, const P&>>) {
                        f(payload);
                        return Flow::pass;
                    } else {
                        return f(payload);
                    }
                }
            };
        }

        template <class Fn> std::function<void(bool)> wrap_key(Fn&& fn) {
            if constexpr (std::is_invocable_v<Fn&, bool>)
                return std::forward<Fn>(fn);
            else
                return [f = std::forward<Fn>(fn)](bool down) mutable {
                    if (down)
                        f();
                };
        }

        void add_handler(abi::Event id, Handler h) {
            handlers[static_cast<uint32_t>(id)].push_back(std::move(h));
        }

        void add_command(const std::string& name, const std::string& usage, const std::string& description,
                         std::function<void(const CommandCall&)> fn) {
            abi::Writer w;
            w.write(name, usage.empty() ? std::string("/") + name : "/" + name + usage, description);
            (void)detail::call_void(Op::CommandRegister, w);
            commands[name] = std::move(fn);
        }

    public:
        void remove_command(const std::string& name) {
            abi::Writer w;
            w.write(name);
            (void)detail::call_void(Op::CommandUnregister, w);
            commands.erase(name);
        }
        template <meta::fixed_string Name> void remove_command() { remove_command(std::string(Name.view())); }

    private:
        template <class Fn> TimerId add_timer(Duration delay, Duration period, Fn&& fn) {
            const TimerId id = static_cast<TimerId>(next_timer++);
            timers.push_back({id, elapsed + delay.count(), period.count(), std::forward<Fn>(fn), true});
            return id;
        }

        void run_timers();
        void draw_layers(const abi::EventFrame& frame);
        static std::vector<std::string> split(const std::string& text);

        std::unordered_map<uint32_t, std::vector<Handler>> handlers;
        std::vector<std::function<void()>> unload_handlers;
        std::vector<Timer> timers;
        std::vector<Layer> layers;
        std::unordered_map<std::string, std::function<void(const CommandCall&)>> commands;
        std::unordered_map<std::string, std::function<void(bool)>> keys;
        std::unordered_map<std::string, std::vector<std::function<void(const std::string&)>>> change_handlers;
        std::unordered_map<std::string, std::function<void()>> press_handlers;
        std::vector<std::function<void(const abi::EventSettingChanged&)>> setting_observers;
        std::unique_ptr<Config> cfg;
        uint32_t next_timer = 1;
        uint64_t ticks = 0;
        double elapsed = 0;
    };

    inline std::vector<std::string> Context::split(const std::string& text) {
        std::vector<std::string> out;
        size_t at = 0;
        while (at < text.size()) {
            while (at < text.size() && text[at] == ' ')
                at++;
            if (at >= text.size())
                break;
            std::string token;
            if (text[at] == '"') {
                at++;
                while (at < text.size() && text[at] != '"')
                    token += text[at++];
                at++;
            } else {
                while (at < text.size() && text[at] != ' ')
                    token += text[at++];
            }
            out.push_back(std::move(token));
        }
        return out;
    }

    inline void Context::run_timers() {
        for (size_t i = 0; i < timers.size(); i++) {
            Timer& t = timers[i];
            if (!t.alive || t.due > elapsed)
                continue;
            auto fn = t.fn;
            if (t.period > 0)
                t.due += t.period;
            else
                t.alive = false;
            fn();
        }
        std::erase_if(timers, [](const Timer& t) { return !t.alive; });
    }

    inline void Context::draw_layers(const abi::EventFrame& frame) {
        for (Layer& layer : layers) {
            ui::Canvas canvas(frame.guiWidth, frame.guiHeight);
            layer.draw(canvas);
            const auto& data = canvas.commands().data;
            if (data == layer.last)
                continue;
            layer.last = data;
            if (data.empty())
                (void)ui::clear(layer.name);
            else
                (void)ui::submit(layer.name, layer.z, canvas);
        }
    }

    inline void Context::dispatch(abi::Event id, abi::Reader& r, uint32_t& flags) {
        switch (id) {
        case abi::Event::Tick: {
            abi::Reader copy = r;
            const auto t = copy.read<abi::payload_t<abi::Event::Tick>>();
            ticks = t.tick;
            elapsed += t.dt;
            run_timers();
            break;
        }
        case abi::Event::Frame: {
            abi::Reader copy = r;
            draw_layers(copy.read<abi::payload_t<abi::Event::Frame>>());
            break;
        }
        case abi::Event::Command: {
            abi::Reader copy = r;
            const auto c = copy.read<abi::payload_t<abi::Event::Command>>();
            if (const auto it = commands.find(c.name); it != commands.end()) {
                CommandCall call{c.name, c.args, split(c.args)};
                it->second(call);
                flags |= abi::kEventCancel;
            }
            break;
        }
        case abi::Event::Key: {
            abi::Reader copy = r;
            const auto k = copy.read<abi::payload_t<abi::Event::Key>>();
            if (const auto it = keys.find(k.id); it != keys.end())
                it->second(k.down);
            break;
        }
        case abi::Event::SettingChanged: {
            abi::Reader copy = r;
            const auto e = copy.read<abi::payload_t<abi::Event::SettingChanged>>();
            if (e.kind == abi::SettingKind::Button) {
                if (const auto it = press_handlers.find(e.id); it != press_handlers.end())
                    it->second();
            } else {
                detail_settings::store().put(e.id, e.value);
                if (const auto it = change_handlers.find(e.id); it != change_handlers.end())
                    for (auto& fn : it->second)
                        fn(e.value);
            }
            for (auto& observer : setting_observers)
                observer(e);
            break;
        }
        case abi::Event::HttpResponse: {
            abi::Reader copy = r;
            const auto e = copy.read<abi::payload_t<abi::Event::HttpResponse>>();
            auto& pending = net::detail_net::pending();
            if (const auto it = pending.find(e.id); it != pending.end()) {
                net::Response response;
                response.status = e.status;
                response.error = e.error;
                for (const auto& h : e.headers)
                    response.headers.push_back({h.first, h.second});
                response.body = e.body;
                auto cb = std::move(it->second);
                pending.erase(it);
                if (cb)
                    cb(response);
            }
            break;
        }
        default:
            break;
        }
        const auto it = handlers.find(static_cast<uint32_t>(id));
        if (it == handlers.end())
            return;
        for (Handler& h : it->second) {
            abi::Reader copy = r;
            if (h(copy) == Flow::cancel)
                flags |= abi::kEventCancel;
        }
    }

    inline void Context::run_unload() {
        for (auto& fn : unload_handlers)
            fn();
        if (cfg && cfg->changed())
            (void)cfg->save();
    }

    namespace detail {
        inline Context& context() {
            static Context ctx;
            return ctx;
        }
    }
}

void perlin_plugin_main(perlin::Context& ctx);

extern "C" {
__attribute__((export_name("perlin_alloc"))) inline void* perlin_alloc(uint32_t size) {
    return ::operator new(size ? size : 1);
}
__attribute__((export_name("perlin_free"))) inline void perlin_free(void* p) {
    ::operator delete(p);
}
__attribute__((export_name("perlin_subscriptions"))) inline uint32_t perlin_subscriptions() {
    return perlin::detail::context().subscriptions()[0];
}
__attribute__((export_name("perlin_subscriptions_hi"))) inline uint32_t perlin_subscriptions_hi() {
    return perlin::abi::kSubscriptionWords > 1 ? perlin::detail::context().subscriptions()[1] : 0u;
}
__attribute__((export_name("perlin_subscriptions_word"))) inline uint32_t perlin_subscriptions_word(uint32_t word) {
    const auto mask = perlin::detail::context().subscriptions();
    return word < mask.size() ? mask[word] : 0u;
}
__attribute__((export_name("perlin_event"))) inline uint32_t perlin_event(uint32_t id, const uint8_t* data,
                                                                          uint32_t len) {
    perlin::Context& ctx = perlin::detail::context();
    perlin::abi::Reader r(data, len);
    uint32_t flags = 0;
    const auto event = static_cast<perlin::abi::Event>(id);
    if (event == perlin::abi::Event::Load) {
        perlin_plugin_main(ctx);
        return 0;
    }
    if (event == perlin::abi::Event::Unload) {
        ctx.dispatch(event, r, flags);
        ctx.run_unload();
        return 0;
    }
    ctx.dispatch(event, r, flags);
    return flags;
}
}
