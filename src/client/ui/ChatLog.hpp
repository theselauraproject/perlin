#pragma once
#include "client/input/InputManager.hpp"
#include "client/ui/CommandInfo.hpp"
#include "client/ui/HudState.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace perlin::ui {
    class ChatLog {
    public:
        static constexpr int kWidth = 320;
        static constexpr int kMaxLines = 100;
        static constexpr int kMaxHistory = 100;
        static constexpr int kMaxInput = 256;

        // java chat options, already resolved to gui pixels
        struct Layout {
            int width = 320;
            float scale = 1.0f;
            int lineHeight = 9;
            int focusedLines = 20;
            int unfocusedLines = 10;
            float textAlpha = 1.0f;
            float backgroundAlpha = 0.5f;
        };
        void SetLayout(const Layout& next);

        void AddMessage(const std::string& text, uint64_t tick);
        void Clear() {
            lines.clear();
            messages.clear();
            scroll = 0;
        }
        std::vector<std::string> RecentLines(size_t count) const {
            std::vector<std::string> out;
            const size_t from = lines.size() > count ? lines.size() - count : 0;
            for (size_t i = from; i < lines.size(); i++)
                out.push_back(lines[i].text);
            return out;
        }
        void SetCommands(std::vector<CommandInfo> list) {
            commands = std::move(list);
            Recompute();
        }
        void SetPlayers(std::vector<std::string> names) { players = std::move(names); }
        struct Coordinate {
            size_t begin = 0, end = 0;
            std::string command, x, y, z;
        };
        static std::vector<Coordinate> FindCoordinates(const std::string& line);
        static std::vector<std::string> FindUrls(const std::string& text);

        void Open(const std::string& prefill, uint64_t nowMs);
        void Close();
        bool IsOpen() const { return open; }

        struct Result {
            bool close = false;
            bool send = false;
            std::string text;
        };
        Result Type(const std::vector<InputManager::Typed>& typed, float wheel);

        void Fill(HudState& s, uint64_t tick, uint64_t nowMs) const;

        static std::vector<std::string> Wrap(const std::string& text, int width);

    private:
        struct UrlSpan {
            size_t begin = 0, end = 0;
            std::string url;
        };
        struct Line {
            std::string text;
            uint64_t added;
            std::vector<UrlSpan> urls;
        };
        int WrapWidth() const { return std::max(1, static_cast<int>(layout.width / layout.scale)); }
        int PageLines() const { return std::max(1, layout.focusedLines); }
        void AppendWrapped(const std::string& text, uint64_t tick);
        void Insert(uint32_t codepoint);
        void Erase(bool forward, bool word);
        void MoveCaret(int delta, bool word);
        void Recall(int direction);
        void Recompute();
        void ApplySuggestion(size_t index);
        std::vector<CommandInfo> commands;
        std::vector<std::string> players;
        std::vector<std::string> suggestions;
        size_t suggestIndex = 0, suggestWordStart = 0;
        mutable size_t suggestOffset = 0;
        std::string hint;
        bool cycling = false;
        std::string cycleBase;

        Layout layout;
        struct Message {
            std::string text;
            uint64_t added;
        };
        // kept unwrapped: a width or scale change wraps them again
        std::deque<Message> messages;
        std::deque<Line> lines;
        std::vector<std::string> history;
        int historyIndex = -1;
        std::string draft;
        std::string input;
        size_t caret = 0;
        int scroll = 0;
        bool open = false;
        uint64_t openedAt = 0;
    };
}
