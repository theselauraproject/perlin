#pragma once
#include <charconv>
#include <format>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "perlin/rpc.hpp"

namespace perlin {
    namespace detail {
        template <std::floating_point T> bool parse_floating(std::string_view text, T& out) {
            if (text.empty() || text.size() > 64)
                return false;
            const std::string copy(text);
            char* end = nullptr;
            const double value = std::strtod(copy.c_str(), &end);
            if (end != copy.c_str() + copy.size())
                return false;
            out = static_cast<T>(value);
            return true;
        }
    }

    namespace detail_settings {
        class Store {
        public:
            const std::string* find(std::string_view id) {
                load();
                const auto it = values.find(id);
                return it == values.end() ? nullptr : &it->second;
            }
            void put(std::string_view id, std::string value) {
                load();
                values[std::string(id)] = std::move(value);
            }

        private:
            void load() {
                if (loaded)
                    return;
                loaded = true;
                if (auto list = detail::call_list<abi::SettingEntry>(Op::SettingList))
                    for (auto& entry : *list)
                        values[std::move(entry.id)] = std::move(entry.value);
            }
            std::map<std::string, std::string, std::less<>> values;
            bool loaded = false;
        };

        inline Store& store() {
            static Store instance;
            return instance;
        }

        template <class T> T parse(std::string_view text) {
            if constexpr (std::is_same_v<T, bool>) {
                return text == "true" || text == "1";
            } else if constexpr (std::is_same_v<T, std::string>) {
                return std::string(text);
            } else if constexpr (std::is_enum_v<T>) {
                return meta::enum_cast_nocase<T>(text).value_or(T{});
            } else if constexpr (std::is_floating_point_v<T>) {
                T value{};
                (void)detail::parse_floating(text, value);
                return value;
            } else {
                static_assert(std::is_integral_v<T>,
                              "a setting is a bool, a number, a string or an enumeration");
                T value{};
                (void)std::from_chars(text.data(), text.data() + text.size(), value);
                return value;
            }
        }

        template <class T> std::string format(const T& value) {
            if constexpr (std::is_same_v<T, bool>) {
                return value ? "true" : "false";
            } else if constexpr (std::is_same_v<T, std::string>) {
                return value;
            } else if constexpr (std::is_enum_v<T>) {
                return std::string(meta::enum_name(value));
            } else {
                return std::format("{}", value);
            }
        }
    }

    inline Result<std::string> setting_text(std::string_view id) {
        abi::Writer w;
        w.write(id);
        auto bytes = detail::call(Op::SettingGet, w);
        if (!bytes)
            return unexpected(bytes.error());
        return std::string(bytes->begin(), bytes->end());
    }

    template <class T> T setting(std::string_view id) {
        const std::string* text = detail_settings::store().find(id);
        return text ? detail_settings::parse<T>(*text) : T{};
    }

    template <class T> class Setting {
    public:
        using Value = T;
        constexpr explicit Setting(std::string_view setting_id) : name(setting_id) {}

        T get() const { return setting<T>(name); }
        operator T() const { return get(); }
        T operator*() const { return get(); }
        constexpr std::string_view id() const { return name; }

        Result<void> set(const T& value) const {
            abi::Writer w;
            w.write(name, detail_settings::format(value));
            auto r = detail::call_void(Op::SettingSet, w);
            if (r)
                detail_settings::store().put(name, detail_settings::format(value));
            return r;
        }

        friend bool operator==(const Setting& s, const T& other)
            requires std::equality_comparable<T>
        {
            return s.get() == other;
        }

    private:
        std::string_view name;
    };

    using Toggle = Setting<bool>;
    using Integer = Setting<int>;
    using Number = Setting<double>;
    using Text = Setting<std::string>;
    template <meta::Enumeration E> using Choice = Setting<E>;

    class Button {
    public:
        constexpr explicit Button(std::string_view button_id) : name(button_id) {}
        constexpr std::string_view id() const { return name; }

    private:
        std::string_view name;
    };
}
