#pragma once
#include "client/input/InputManager.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace perlin::ui {
    struct TextLimits {
        size_t maxBytes = 4096;
        size_t maxChars = static_cast<size_t>(-1);
        std::function<bool(uint32_t)> accept;
        bool multiline = false;
    };

    class TextEdit {
    public:
        static TextEdit& Focused();
        static void SetClipboard(std::function<std::string()> get,
                                 std::function<void(const std::string&)> set);

        bool Apply(const InputManager::Typed& t, std::string& text, const TextLimits& limits = {});
        bool Apply(const InputManager::Typed& t, std::string& text, size_t& caret,
                   const TextLimits& limits = {});

        void State(const std::string& text, size_t& caret, size_t& selStart, size_t& selEnd) const;
        void Reset() { target = nullptr; }

    private:
        struct Snapshot {
            std::string text;
            size_t caret = 0, anchor = 0;
        };
        void Attach(std::string& text);
        void Push(const std::string& text, bool coalesce);
        bool Erase(std::string& text);
        void Insert(std::string& text, const std::string& add, const TextLimits& limits);

        const std::string* target = nullptr;
        std::string seen;
        size_t caret = 0, anchor = 0;
        std::vector<Snapshot> undo, redo;
        bool typing = false;
    };

    std::string TextFieldRml(const std::string& value, bool caretOn);
    std::string TextFieldPlain(const std::string& value, bool caretOn);
}
