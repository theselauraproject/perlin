#include "client/ui/TextEdit.hpp"

#include "client/input/Keys.hpp"
#include "client/ui/RmlEmit.hpp"

#include <algorithm>

namespace perlin::ui {
    namespace {
        std::function<std::string()> gGetClipboard;
        std::function<void(const std::string&)> gSetClipboard;
        constexpr size_t kUndoDepth = 100;

        bool Continuation(char c) {
            return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
        }

        size_t Prev(const std::string& s, size_t i) {
            if (i == 0)
                return 0;
            i--;
            while (i > 0 && Continuation(s[i]))
                i--;
            return i;
        }

        size_t Next(const std::string& s, size_t i) {
            if (i >= s.size())
                return s.size();
            i++;
            while (i < s.size() && Continuation(s[i]))
                i++;
            return i;
        }

        bool Space(const std::string& s, size_t i) {
            return i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\t');
        }

        size_t PrevWord(const std::string& s, size_t i) {
            while (i > 0 && Space(s, Prev(s, i)))
                i = Prev(s, i);
            while (i > 0 && !Space(s, Prev(s, i)))
                i = Prev(s, i);
            return i;
        }

        size_t NextWord(const std::string& s, size_t i) {
            while (i < s.size() && Space(s, i))
                i = Next(s, i);
            while (i < s.size() && !Space(s, i))
                i = Next(s, i);
            return i;
        }

        void AppendUtf8(std::string& out, uint32_t cp) {
            if (cp < 0x80) {
                out += static_cast<char>(cp);
            } else if (cp < 0x800) {
                out += static_cast<char>(0xC0 | (cp >> 6));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                out += static_cast<char>(0xE0 | (cp >> 12));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
                out += static_cast<char>(0xF0 | (cp >> 18));
                out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
        }

        bool DecodeUtf8(const std::string& s, size_t& i, uint32_t& cp) {
            if (i >= s.size())
                return false;
            const unsigned char c = static_cast<unsigned char>(s[i]);
            const int extra = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
            if (extra < 0 || i + static_cast<size_t>(extra) >= s.size()) {
                i++;
                cp = 0xFFFD;
                return true;
            }
            cp = extra == 0 ? c : extra == 1 ? (c & 0x1F) : extra == 2 ? (c & 0x0F) : (c & 0x07);
            for (int k = 1; k <= extra; k++)
                cp = (cp << 6) | (static_cast<unsigned char>(s[i + static_cast<size_t>(k)]) & 0x3F);
            i += static_cast<size_t>(extra) + 1;
            return true;
        }
    }

    TextEdit& TextEdit::Focused() {
        static TextEdit edit;
        return edit;
    }

    void TextEdit::SetClipboard(std::function<std::string()> get,
                                std::function<void(const std::string&)> set) {
        gGetClipboard = std::move(get);
        gSetClipboard = std::move(set);
    }

    void TextEdit::Attach(std::string& text) {
        if (target == &text && seen == text)
            return;
        target = &text;
        seen = text;
        caret = anchor = text.size();
        undo.clear();
        redo.clear();
        typing = false;
    }

    void TextEdit::Push(const std::string& text, bool coalesce) {
        if (coalesce && typing)
            return;
        undo.push_back({text, caret, anchor});
        if (undo.size() > kUndoDepth)
            undo.erase(undo.begin());
        redo.clear();
        typing = coalesce;
    }

    bool TextEdit::Erase(std::string& text) {
        if (caret == anchor)
            return false;
        const size_t a = std::min(caret, anchor), b = std::max(caret, anchor);
        text.erase(a, b - a);
        caret = anchor = a;
        return true;
    }

    void TextEdit::Insert(std::string& text, const std::string& add, const TextLimits& limits) {
        std::string clean;
        size_t chars = 0;
        for (size_t k = 0; k < text.size(); k++)
            chars += Continuation(text[k]) ? 0 : 1;
        for (size_t i = 0; i < add.size();) {
            uint32_t cp = 0;
            if (!DecodeUtf8(add, i, cp))
                break;
            if (cp == '\r')
                continue;
            if (cp == '\n' && !limits.multiline)
                cp = ' ';
            if ((cp < 32 && cp != '\n') || cp == 127)
                continue;
            if (limits.accept && !limits.accept(cp))
                continue;
            std::string one;
            AppendUtf8(one, cp);
            if (text.size() + clean.size() + one.size() > limits.maxBytes || chars >= limits.maxChars)
                break;
            clean += one;
            chars++;
        }
        text.insert(caret, clean);
        caret += clean.size();
        anchor = caret;
    }

    bool TextEdit::Apply(const InputManager::Typed& t, std::string& text, size_t& externalCaret,
                         const TextLimits& limits) {
        Attach(text);
        if (const size_t at = std::min(externalCaret, text.size()); at != caret) {
            caret = anchor = at;
            typing = false;
        }
        const bool handled = Apply(t, text, limits);
        externalCaret = caret;
        return handled;
    }

    bool TextEdit::Apply(const InputManager::Typed& t, std::string& text, const TextLimits& limits) {
        Attach(text);
        caret = std::min(caret, text.size());
        anchor = std::min(anchor, text.size());
        bool handled = true;
        if (t.character) {
            if (t.ctrl)
                return true;
            const uint32_t cp = static_cast<uint32_t>(t.key);
            if (cp < 32 || cp == 127)
                return false;
            std::string add;
            AppendUtf8(add, cp);
            Push(text, true);
            Erase(text);
            Insert(text, add, limits);
        } else if (t.ctrl && t.key == 'A') {
            anchor = 0;
            caret = text.size();
        } else if (t.ctrl && (t.key == 'C' || t.key == 'X')) {
            if (caret != anchor) {
                const size_t a = std::min(caret, anchor), b = std::max(caret, anchor);
                if (gSetClipboard)
                    gSetClipboard(text.substr(a, b - a));
                if (t.key == 'X') {
                    Push(text, false);
                    Erase(text);
                }
            }
        } else if (t.ctrl && t.key == 'V') {
            const std::string clip = gGetClipboard ? gGetClipboard() : std::string();
            if (!clip.empty()) {
                Push(text, false);
                Erase(text);
                Insert(text, clip, limits);
            }
        } else if (t.ctrl && ((t.key == 'Z' && !t.shift) || t.key == 'Y' || (t.key == 'Z' && t.shift))) {
            const bool isUndo = t.key == 'Z' && !t.shift;
            auto& from = isUndo ? undo : redo;
            auto& to = isUndo ? redo : undo;
            if (!from.empty()) {
                to.push_back({text, caret, anchor});
                const Snapshot s = from.back();
                from.pop_back();
                text = s.text;
                caret = std::min(s.caret, text.size());
                anchor = std::min(s.anchor, text.size());
            }
            typing = false;
        } else if (t.key == Key::Left || t.key == Key::Right) {
            const bool left = t.key == Key::Left;
            if (caret != anchor && !t.shift) {
                caret = anchor = left ? std::min(caret, anchor) : std::max(caret, anchor);
            } else {
                caret = left ? (t.ctrl ? PrevWord(text, caret) : Prev(text, caret))
                             : (t.ctrl ? NextWord(text, caret) : Next(text, caret));
                if (!t.shift)
                    anchor = caret;
            }
            typing = false;
        } else if (t.key == Key::Home || t.key == Key::End) {
            caret = t.key == Key::Home ? 0 : text.size();
            if (!t.shift)
                anchor = caret;
            typing = false;
        } else if (t.key == Key::Backspace || t.key == Key::Delete) {
            if (caret != anchor) {
                Push(text, false);
                Erase(text);
            } else {
                const bool back = t.key == Key::Backspace;
                const size_t other = back ? (t.ctrl ? PrevWord(text, caret) : Prev(text, caret))
                                          : (t.ctrl ? NextWord(text, caret) : Next(text, caret));
                if (other != caret) {
                    Push(text, false);
                    const size_t a = std::min(other, caret), b = std::max(other, caret);
                    text.erase(a, b - a);
                    caret = anchor = a;
                }
            }
        } else {
            handled = false;
        }
        seen = text;
        return handled;
    }

    void TextEdit::State(const std::string& text, size_t& outCaret, size_t& selStart, size_t& selEnd) const {
        if (target && seen == text) {
            outCaret = std::min(caret, text.size());
            selStart = std::min(std::min(caret, anchor), text.size());
            selEnd = std::min(std::max(caret, anchor), text.size());
            return;
        }
        outCaret = selStart = selEnd = text.size();
    }

    std::string TextFieldPlain(const std::string& value, bool caretOn) {
        size_t caret = 0, a = 0, b = 0;
        TextEdit::Focused().State(value, caret, a, b);
        if (!caretOn)
            return value;
        return caret >= value.size() ? value + "_" : value.substr(0, caret) + "|" + value.substr(caret);
    }

    std::string TextFieldRml(const std::string& value, bool caretOn) {
        size_t caret = 0, a = 0, b = 0;
        TextEdit::Focused().State(value, caret, a, b);
        const auto bar = [&](size_t at) -> std::string {
            if (!caretOn || at != caret)
                return {};
            if (at >= value.size())
                return "_";
            return "<span style=\"display:inline-block;width:0px;overflow:visible;\">|</span>";
        };
        std::string out = EscapeRml(value.substr(0, a));
        out += bar(a);
        if (b > a) {
            out += "<span style=\"background-color:#3d6bffb4;color:#ffffff;\">" +
                   EscapeRml(value.substr(a, b - a)) + "</span>";
            out += bar(b);
        }
        out += EscapeRml(value.substr(b));
        return out;
    }
}
