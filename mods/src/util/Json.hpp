#pragma once
#include <glaze/glaze.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace perlin::json {

    class ArrayRange;
    class ObjectRange;

    class View {
    public:
        View() = default;
        explicit View(const glz::generic* node) : node(node) {}

        bool valid() const { return node != nullptr && !node->holds<std::nullptr_t>(); }
        explicit operator bool() const { return valid(); }

        bool isObject() const { return node && node->is_object(); }
        bool isArray() const { return node && node->is_array(); }
        bool isString() const { return node && node->is_string(); }
        bool isNumber() const { return node && node->is_number(); }
        bool isBool() const { return node && node->is_boolean(); }

        View operator[](std::string_view key) const {
            if (!isObject())
                return {};
            const auto& object = node->get<glz::generic::object_t>();
            const auto it = object.find(key);
            return it == object.end() ? View() : View(&it->second);
        }

        View operator[](size_t index) const {
            if (!isArray())
                return {};
            const auto& array = node->get<glz::generic::array_t>();
            return index < array.size() ? View(&array[index]) : View();
        }

        bool contains(std::string_view key) const { return (*this)[key].node != nullptr; }

        size_t size() const {
            if (isArray())
                return node->get<glz::generic::array_t>().size();
            if (isObject())
                return node->get<glz::generic::object_t>().size();
            return 0;
        }

        std::string str(std::string fallback = {}) const {
            return isString() ? node->get<std::string>() : fallback;
        }
        std::string text() const {
            if (isString())
                return node->get<std::string>();
            if (isBool())
                return node->get<bool>() ? "true" : "false";
            if (isNumber()) {
                const double value = node->get<double>();
                if (value == static_cast<double>(static_cast<long long>(value)))
                    return std::to_string(static_cast<long long>(value));
                return std::to_string(value);
            }
            return {};
        }
        double number(double fallback = 0) const { return isNumber() ? node->get<double>() : fallback; }
        float real(float fallback = 0) const { return static_cast<float>(number(fallback)); }
        int integer(int fallback = 0) const {
            return isNumber() ? static_cast<int>(node->get<double>()) : fallback;
        }
        bool boolean(bool fallback = false) const { return isBool() ? node->get<bool>() : fallback; }

        inline ArrayRange items() const;
        inline ObjectRange members() const;

    private:
        const glz::generic* node = nullptr;
        friend class ArrayRange;
        friend class ObjectRange;
    };

    class ArrayRange {
    public:
        explicit ArrayRange(const glz::generic* node) : node(node) {}
        struct Iterator {
            const glz::generic* it;
            View operator*() const { return View(it); }
            Iterator& operator++() {
                ++it;
                return *this;
            }
            bool operator!=(const Iterator& other) const { return it != other.it; }
        };
        Iterator begin() const { return {node ? node->get<glz::generic::array_t>().data() : nullptr}; }
        Iterator end() const {
            if (!node)
                return {nullptr};
            const auto& array = node->get<glz::generic::array_t>();
            return {array.data() + array.size()};
        }

    private:
        const glz::generic* node;
    };

    class ObjectRange {
    public:
        explicit ObjectRange(const glz::generic* node) : node(node) {}
        struct Member {
            const std::string& key;
            View value;
        };
        struct Iterator {
            const std::pair<std::string, glz::generic>* it;
            Member operator*() const { return Member{it->first, View(&it->second)}; }
            Iterator& operator++() {
                ++it;
                return *this;
            }
            bool operator!=(const Iterator& other) const { return it != other.it; }
        };
        Iterator begin() const { return {node ? node->get<glz::generic::object_t>().begin() : nullptr}; }
        Iterator end() const { return {node ? node->get<glz::generic::object_t>().end() : nullptr}; }

    private:
        const glz::generic* node;
    };

    inline ArrayRange View::items() const {
        return ArrayRange(isArray() ? node : nullptr);
    }
    inline ObjectRange View::members() const {
        return ObjectRange(isObject() ? node : nullptr);
    }

    class Document {
    public:
        static std::optional<Document> Parse(std::string_view text, bool comments = false);

        View root() const { return View(&value); }

    private:
        glz::generic value;
    };

    template <class T> std::string Write(const T& value) {
        std::string out;
        if (glz::write_json(value, out))
            out.clear();
        return out;
    }

    template <class T> std::optional<T> Read(std::string_view text, bool comments = false) {
        T out{};
        const auto error =
            comments ? glz::read<glz::opts{.comments = true, .error_on_unknown_keys = false}>(out, text)
                     : glz::read<glz::opts{.error_on_unknown_keys = false}>(out, text);
        if (error)
            return std::nullopt;
        return out;
    }

}
