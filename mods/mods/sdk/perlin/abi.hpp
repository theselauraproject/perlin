#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "perlin/meta.hpp"
#include "perlin/gen/abi_types.hpp"
#include "perlin/gen/abi_spec.hpp"

namespace perlin::abi {

    namespace wire {
        template <class T>
        concept Raw = (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) || std::is_enum_v<T> ||
                      requires { T::wire_raw_tag; };

        template <class T> struct is_vector : std::false_type {};
        template <class T, class A> struct is_vector<std::vector<T, A>> : std::true_type {};

        template <class T> struct is_array : std::false_type {};
        template <class T, size_t N> struct is_array<std::array<T, N>> : std::true_type {};

        template <class T> struct is_pair : std::false_type {};
        template <class A, class B> struct is_pair<std::pair<A, B>> : std::true_type {};

        template <class T> struct is_optional : std::false_type {};
        template <class T> struct is_optional<std::optional<T>> : std::true_type {};

        template <class T> struct is_variant : std::false_type {};
        template <class... Ts> struct is_variant<std::variant<Ts...>> : std::true_type {};
    }

    class Writer {
    public:
        Writer() = default;
        explicit Writer(size_t reserve) { data.reserve(reserve); }

        template <class T> Writer& put(T value) {
            static_assert(std::is_trivially_copyable_v<T>);
            const size_t at = data.size();
            data.resize(at + sizeof(T));
            std::memcpy(data.data() + at, &value, sizeof(T));
            return *this;
        }
        Writer& str(std::string_view text) {
            put<uint32_t>(static_cast<uint32_t>(text.size()));
            data.insert(data.end(), text.begin(), text.end());
            return *this;
        }
        Writer& bytes(const void* p, size_t n) {
            put<uint32_t>(static_cast<uint32_t>(n));
            const auto* b = static_cast<const uint8_t*>(p);
            data.insert(data.end(), b, b + n);
            return *this;
        }
        Writer& raw(const void* p, size_t n) {
            const auto* b = static_cast<const uint8_t*>(p);
            data.insert(data.end(), b, b + n);
            return *this;
        }
        Writer& boolean(bool v) { return put<uint8_t>(v ? 1 : 0); }

        template <class... Ts> Writer& write(const Ts&... values) {
            (write_one(values), ...);
            return *this;
        }

        const uint8_t* Data() const { return data.data(); }
        size_t Size() const { return data.size(); }
        void Clear() { data.clear(); }
        std::vector<uint8_t> data;

    private:
        template <class T> void write_one(const T& v) {
            if constexpr (std::is_same_v<T, bool>) {
                boolean(v);
            } else if constexpr (wire::Raw<T>) {
                put(v);
            } else if constexpr (std::is_convertible_v<const T&, std::string_view>) {
                str(std::string_view(v));
            } else if constexpr (wire::is_array<T>::value) {
                using E = typename T::value_type;
                if constexpr (wire::Raw<E>)
                    raw(v.data(), v.size() * sizeof(E));
                else
                    for (const E& e : v)
                        write_one(e);
            } else if constexpr (wire::is_vector<T>::value) {
                using E = typename T::value_type;
                put<uint32_t>(static_cast<uint32_t>(v.size()));
                if constexpr (wire::Raw<E>)
                    raw(v.data(), v.size() * sizeof(E));
                else
                    for (const E& e : v)
                        write_one(e);
            } else if constexpr (wire::is_pair<T>::value) {
                write_one(v.first);
                write_one(v.second);
            } else if constexpr (wire::is_optional<T>::value) {
                boolean(v.has_value());
                if (v)
                    write_one(*v);
            } else if constexpr (wire::is_variant<T>::value) {
                put<uint8_t>(static_cast<uint8_t>(v.index()));
                std::visit([this](const auto& alternative) { write_one(alternative); }, v);
            } else if constexpr (std::is_same_v<T, std::monostate>) {
            } else if constexpr (requires { T::wire_rest_tag; }) {
                raw(v.value.data(), v.value.size());
            } else if constexpr (meta::Aggregate<T>) {
                meta::for_each_field(v, [&](const auto& member) { write_one(member); });
            } else {
                static_assert(sizeof(T) == 0, "type has no wire encoding");
            }
        }
    };

    class Reader {
    public:
        Reader() = default;
        Reader(const uint8_t* p, size_t n) : cur(p), end(p + n) {}

        template <class T> T get() {
            T value{};
            if (static_cast<size_t>(end - cur) < sizeof(T)) {
                failed = true;
                cur = end;
                return value;
            }
            std::memcpy(&value, cur, sizeof(T));
            cur += sizeof(T);
            return value;
        }
        std::string str() {
            const uint32_t n = get<uint32_t>();
            if (failed || static_cast<size_t>(end - cur) < n) {
                failed = true;
                cur = end;
                return {};
            }
            std::string out(reinterpret_cast<const char*>(cur), n);
            cur += n;
            return out;
        }
        std::vector<uint8_t> bytes() {
            const uint32_t n = get<uint32_t>();
            if (failed || static_cast<size_t>(end - cur) < n) {
                failed = true;
                cur = end;
                return {};
            }
            std::vector<uint8_t> out(cur, cur + n);
            cur += n;
            return out;
        }
        bool boolean() { return get<uint8_t>() != 0; }

        template <class T> T read() {
            T value{};
            read_into(value);
            return value;
        }
        template <class T> Reader& read_into(T& v) {
            if constexpr (std::is_same_v<T, bool>) {
                v = boolean();
            } else if constexpr (wire::Raw<T>) {
                v = get<T>();
            } else if constexpr (std::is_same_v<T, std::string>) {
                v = str();
            } else if constexpr (wire::is_array<T>::value) {
                using E = typename T::value_type;
                if constexpr (wire::Raw<E>) {
                    const size_t n = v.size() * sizeof(E);
                    if (Remaining() < n) {
                        fail();
                    } else {
                        std::memcpy(v.data(), cur, n);
                        cur += n;
                    }
                } else {
                    for (E& e : v)
                        read_into(e);
                }
            } else if constexpr (wire::is_vector<T>::value) {
                using E = typename T::value_type;
                const uint32_t n = get<uint32_t>();
                v.clear();
                if (failed)
                    return *this;
                if constexpr (wire::Raw<E>) {
                    if (static_cast<size_t>(n) * sizeof(E) > Remaining()) {
                        fail();
                        return *this;
                    }
                    v.resize(n);
                    if (n)
                        std::memcpy(v.data(), cur, static_cast<size_t>(n) * sizeof(E));
                    cur += static_cast<size_t>(n) * sizeof(E);
                } else {
                    if (n > Remaining()) {
                        fail();
                        return *this;
                    }
                    v.reserve(n);
                    for (uint32_t i = 0; i < n && !failed; i++) {
                        E e{};
                        read_into(e);
                        v.push_back(std::move(e));
                    }
                }
            } else if constexpr (wire::is_pair<T>::value) {
                read_into(v.first);
                read_into(v.second);
            } else if constexpr (wire::is_optional<T>::value) {
                if (boolean()) {
                    typename T::value_type e{};
                    read_into(e);
                    v = std::move(e);
                } else {
                    v.reset();
                }
            } else if constexpr (wire::is_variant<T>::value) {
                const uint8_t index = get<uint8_t>();
                if (failed || index >= std::variant_size_v<T>)
                    fail();
                else
                    read_alternative<0>(v, index);
            } else if constexpr (std::is_same_v<T, std::monostate>) {
            } else if constexpr (requires { T::wire_rest_tag; }) {
                using V = decltype(v.value);
                v.value = V(reinterpret_cast<const typename V::value_type*>(cur), reinterpret_cast<const typename V::value_type*>(end));
                cur = end;
            } else if constexpr (meta::Aggregate<T>) {
                meta::for_each_field(v, [&](auto& member) { read_into(member); });
            } else {
                static_assert(sizeof(T) == 0, "type has no wire encoding");
            }
            return *this;
        }

        size_t Remaining() const { return static_cast<size_t>(end - cur); }
        bool Ok() const { return !failed; }
        const uint8_t* Cursor() const { return cur; }
        void Skip(size_t n) {
            if (Remaining() < n)
                fail();
            else
                cur += n;
        }

    private:
        template <size_t I, class V> void read_alternative(V& v, size_t index) {
            if constexpr (I < std::variant_size_v<V>) {
                if (index == I) {
                    std::variant_alternative_t<I, V> alt{};
                    read_into(alt);
                    v = std::move(alt);
                } else {
                    read_alternative<I + 1>(v, index);
                }
            }
        }

        void fail() {
            failed = true;
            cur = end;
        }

        const uint8_t* cur = nullptr;
        const uint8_t* end = nullptr;
        bool failed = false;
    };

}
