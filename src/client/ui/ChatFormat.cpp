#include "client/ui/ChatFormat.hpp"
#include "client/i18n/Lang.hpp"
#include "client/ui/Json.hpp"

#include <cctype>
#include <cstring>
#include <unordered_map>

namespace perlin::ui {
    namespace {
        std::string Pretty(const std::string& key) {
            std::string s = key;
            const size_t a = s.find('.');
            if (a != std::string::npos) {
                s = s.substr(a + 1);
                if (s.size() > 5 && s.compare(s.size() - 5, 5, ".name") == 0)
                    s.resize(s.size() - 5);
            }
            for (char& c : s)
                if (c == '_')
                    c = ' ';
            bool up = true;
            for (char& c : s) {
                if (up && std::isalpha(static_cast<unsigned char>(c)))
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                up = c == ' ';
            }
            return s;
        }

        bool Looks(const std::string& key) {
            static const char* kPrefix[] = {"entity.",   "item.",        "tile.",   "block.",
                                            "death.",    "commands.",    "chat.",   "multiplayer.",
                                            "gameMode.", "enchantment.", "effect.", "potion."};
            for (const char* p : kPrefix)
                if (key.starts_with(p))
                    return true;
            return false;
        }

        std::string Substitute(const std::string& format, const std::vector<std::string>& args) {
            std::string out;
            size_t next = 0;
            for (size_t i = 0; i < format.size(); i++) {
                if (format[i] != '%') {
                    out += format[i];
                    continue;
                }
                size_t j = i + 1;
                if (j < format.size() && format[j] == '%') {
                    out += '%';
                    i = j;
                    continue;
                }
                size_t k = j;
                while (k < format.size() && std::isdigit(static_cast<unsigned char>(format[k])))
                    k++;
                int index = -1;
                if (k > j && k < format.size() && format[k] == '$') {
                    index = std::atoi(format.substr(j, k - j).c_str()) - 1;
                    k++;
                } else {
                    k = j;
                }
                if (k < format.size() && format[k] == 's') {
                    const size_t use = index >= 0 ? static_cast<size_t>(index) : next++;
                    if (use < args.size())
                        out += args[use];
                    i = k;
                } else {
                    out += '%';
                }
            }
            return out;
        }

        std::string Param(const std::string& p) {
            return !p.empty() && p[0] == '%' ? Translate(p, {}) : p;
        }

        std::string RawNode(const Json& v);

        std::string RawArray(const Json& v) {
            std::string out;
            for (const Json& e : v.array)
                out += RawNode(e);
            return out;
        }

        std::string RawNode(const Json& v) {
            if (v.IsString())
                return v.string;
            if (v.IsArray())
                return RawArray(v);
            if (!v.IsObject())
                return {};
            std::string out;
            if (const Json* r = v.Find("rawtext"))
                out += RawNode(*r);
            if (const Json* t = v.Find("text"); t && t->IsString())
                out += t->string;
            if (const Json* t = v.Find("translate"); t && t->IsString()) {
                std::vector<std::string> with;
                if (const Json* w = v.Find("with")) {
                    if (w->IsArray()) {
                        for (const Json& e : w->array)
                            with.push_back(RawNode(e));
                    } else if (const Json* rt = w->Find("rawtext"); rt && rt->IsArray()) {
                        for (const Json& e : rt->array)
                            with.push_back(RawNode(e));
                    }
                }
                out += Translate(t->string, with);
            }
            if (const Json* s = v.Find("score"); s && s->IsObject())
                if (const Json* n = s->Find("name"); n && n->IsString())
                    out += n->string;
            if (const Json* s = v.Find("selector"); s && s->IsString())
                out += s->string;
            return out;
        }
    }

    std::string Translate(const std::string& rawKey, const std::vector<std::string>& parameters) {
        std::string key = rawKey;
        std::string prefix;
        size_t p = 0;
        while (p + 1 < key.size() && key.compare(p, 2, "\xC2\xA7") == 0 && p + 3 <= key.size())
            p += 3;
        prefix = key.substr(0, p);
        key = key.substr(p);
        if (!key.empty() && key[0] == '%')
            key.erase(0, 1);
        std::vector<std::string> args;
        args.reserve(parameters.size());
        for (const std::string& a : parameters)
            args.push_back(Param(a));
        std::string pattern;
        if (i18n::Lang::Get().Find(key, pattern))
            return prefix + Substitute(pattern, args);
        if (Looks(key) &&
            (key.starts_with("entity.") || key.starts_with("item.") || key.starts_with("tile.") ||
             key.starts_with("block.") || key.starts_with("effect.") || key.starts_with("enchantment.")))
            return prefix + Pretty(key);
        if (!parameters.empty() && !key.contains(' ') && Looks(key)) {
            std::string out = key;
            for (const std::string& a : args)
                out += " " + a;
            return prefix + out;
        }
        return prefix + (args.empty() ? key : Substitute(key, args));
    }

    std::string FormatRawText(const std::string& text) {
        const size_t first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || (text[first] != '{' && text[first] != '['))
            return text;
        Json v;
        std::string error;
        if (!Json::Parse(text, v, error))
            return text;
        return RawNode(v);
    }

    std::string FormatTextPacket(int type, bool needsTranslation, const std::string& source,
                                 const std::string& message, const std::vector<std::string>& parameters) {
        switch (type) {
        case 1:
            // no author: servers that format the whole line themselves; the game shows it bare, no "<> "
            if (source.empty())
                return message;
            return Translate("chat.type.text", {source, message});
        case 7:
            return Translate("commands.message.display.incoming", {source, message});
        case 8:
            return Translate("chat.type.announcement",
                             {source.empty() ? i18n::Tr("perlin.chat.server") : source, message});
        case 9:
        case 10:
        case 11:
            return FormatRawText(message);
        case 2:
            return Translate(message, parameters);
        default:
            return needsTranslation ? Translate(message, parameters) : message;
        }
    }

    std::string FormatCommandOutput(const std::string& messageId, bool success,
                                    const std::vector<std::string>& parameters) {
        std::string out = Translate(messageId, parameters);
        if (!success && !out.starts_with("\xC2\xA7"))
            out = "\xC2\xA7"
                  "c" +
                  out;
        return out;
    }
}
