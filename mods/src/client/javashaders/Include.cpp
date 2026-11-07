#include "client/javashaders/Include.hpp"
#include "util/Strings.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace perlin::jshader {
    namespace {

        bool IsIdent(char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        }

        std::vector<std::string> ParseValueList(const std::string& comment) {
            std::vector<std::string> out;
            const size_t open = comment.find('[');
            const size_t close = comment.find(']', open == std::string::npos ? 0 : open);
            if (open == std::string::npos || close == std::string::npos)
                return out;
            std::istringstream ss(comment.substr(open + 1, close - open - 1));
            std::string word;
            while (ss >> word)
                out.push_back(word);
            return out;
        }

        struct DefineLine {
            bool commentedOut = false;
            std::string indent, name, value, comment;
        };

        bool ParseDefine(const std::string& line, DefineLine& d) {
            size_t i = 0;
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
                i++;
            d.indent = line.substr(0, i);
            if (line.compare(i, 2, "//") == 0) {
                d.commentedOut = true;
                i += 2;
                while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
                    i++;
            }
            if (i >= line.size() || line[i] != '#')
                return false;
            i++;
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
                i++;
            if (line.compare(i, 6, "define") != 0)
                return false;
            i += 6;
            if (i >= line.size() || (line[i] != ' ' && line[i] != '\t'))
                return false;
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
                i++;
            size_t n = i;
            while (n < line.size() && IsIdent(line[n]))
                n++;
            if (n == i)
                return false;
            d.name = line.substr(i, n - i);
            if (n < line.size() && line[n] == '(')
                return false;
            std::string rest = line.substr(n);
            const size_t cm = rest.find("//");
            if (cm != std::string::npos) {
                d.comment = rest.substr(cm + 2);
                rest = rest.substr(0, cm);
            }
            d.value = str::Trim(rest);
            return true;
        }

        struct ConstLine {
            std::string indent, type, name, value, comment, tail;
        };

        bool ParseConst(const std::string& line, ConstLine& c) {
            size_t i = 0;
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
                i++;
            c.indent = line.substr(0, i);
            if (line.compare(i, 5, "const") != 0 || i + 5 >= line.size() ||
                !std::isspace(static_cast<unsigned char>(line[i + 5])))
                return false;
            i += 5;
            while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])))
                i++;
            size_t t = i;
            while (t < line.size() && IsIdent(line[t]))
                t++;
            c.type = line.substr(i, t - i);
            if (c.type != "int" && c.type != "float" && c.type != "bool")
                return false;
            i = t;
            while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])))
                i++;
            size_t n = i;
            while (n < line.size() && IsIdent(line[n]))
                n++;
            if (n == i)
                return false;
            c.name = line.substr(i, n - i);
            i = n;
            while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])))
                i++;
            if (i >= line.size() || line[i] != '=')
                return false;
            i++;
            const size_t semi = line.find(';', i);
            if (semi == std::string::npos)
                return false;
            c.value = str::Trim(line.substr(i, semi - i));
            const std::string after = line.substr(semi + 1);
            const size_t cm = after.find("//");
            c.comment = cm == std::string::npos ? std::string() : after.substr(cm + 2);
            c.tail = after;
            return true;
        }

        void RecordOption(IncludeContext& ctx, ShaderOption opt) {
            if (!ctx.options)
                return;
            const std::string key = opt.name;
            if (!ctx.seenOptionKeys.insert(key).second)
                return;
            ctx.options->push_back(std::move(opt));
        }

        std::string ApplyOptions(IncludeContext& ctx, const std::string& file, const std::string& line) {
            if (!line.contains("define") && !line.contains("const"))
                return line;
            DefineLine d;
            if (ParseDefine(line, d)) {
                const bool hasList = d.comment.contains('[');
                if (d.value.empty() && (!d.commentedOut || true)) {
                    if (d.commentedOut && d.name.empty())
                        return line;
                    ShaderOption opt;
                    opt.name = d.name;
                    opt.kind = ShaderOption::Kind::Toggle;
                    opt.defaultOn = !d.commentedOut;
                    opt.comment = str::Trim(d.comment);
                    opt.file = file;
                    RecordOption(ctx, opt);
                    if (ctx.overrides) {
                        const auto it = ctx.overrides->find(d.name);
                        if (it != ctx.overrides->end()) {
                            const bool on = it->second == "true" || it->second == "1" || it->second == "on";
                            return d.indent + (on ? "" : "//") + "#define " + d.name +
                                   (d.comment.empty() ? "" : " //" + d.comment);
                        }
                    }
                    return line;
                }
                if (!d.commentedOut && hasList) {
                    ShaderOption opt;
                    opt.name = d.name;
                    opt.kind = ShaderOption::Kind::Value;
                    opt.defaultValue = d.value;
                    opt.values = ParseValueList(d.comment);
                    opt.comment = str::Trim(d.comment.substr(0, d.comment.find('[')));
                    opt.file = file;
                    RecordOption(ctx, opt);
                    if (ctx.overrides) {
                        const auto it = ctx.overrides->find(d.name);
                        if (it != ctx.overrides->end())
                            return d.indent + "#define " + d.name + " " + it->second + " //" + d.comment;
                    }
                }
                return line;
            }
            ConstLine c;
            if (ParseConst(line, c) && c.comment.contains('[')) {
                ShaderOption opt;
                opt.name = c.name;
                opt.kind = ShaderOption::Kind::Const;
                opt.type = c.type;
                opt.defaultValue = c.value;
                opt.values = ParseValueList(c.comment);
                opt.file = file;
                RecordOption(ctx, opt);
                if (ctx.overrides) {
                    const auto it = ctx.overrides->find(c.name);
                    if (it != ctx.overrides->end())
                        return c.indent + "const " + c.type + " " + c.name + " = " + it->second + ";" +
                               c.tail;
                }
            }
            return line;
        }

        void Expand(IncludeContext& ctx, const std::string& path, const std::string& text,
                    ExpandedSource& out, int depth) {
            out.files.insert(path);
            std::istringstream in(text);
            std::string line;
            int number = 0;
            while (std::getline(in, line)) {
                number++;
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                const std::string t = str::Trim(line);
                if (t.compare(0, 8, "#include") == 0 ||
                    (t.size() > 2 && t[0] == '#' && str::Trim(t.substr(1)).compare(0, 7, "include") == 0)) {
                    const size_t q1 = t.find_first_of("\"<");
                    const size_t q2 = q1 == std::string::npos ? q1 : t.find_first_of("\">", q1 + 1);
                    if (q1 != std::string::npos && q2 != std::string::npos) {
                        const std::string target = t.substr(q1 + 1, q2 - q1 - 1);
                        const std::string resolved = JoinPath(DirectoryOf(path), target);
                        if (depth > 32) {
                            out.ok = false;
                            out.error = "include depth exceeded at " + path + ":" + std::to_string(number);
                            return;
                        }
                        std::string content;
                        if (!ctx.pack->Read(resolved, content)) {
                            out.ok = false;
                            out.error = path + ":" + std::to_string(number) + ": cannot include " + resolved;
                            out.text += "\n";
                            out.origins.push_back({path, number});
                            continue;
                        }
                        Expand(ctx, resolved, content, out, depth + 1);
                        if (!out.ok)
                            return;
                        continue;
                    }
                }
                out.text += ApplyOptions(ctx, path, line);
                out.text += '\n';
                out.origins.push_back({path, number});
            }
        }
    }

    void ScanOptionsInText(IncludeContext& ctx, const std::string& path, const std::string& text) {
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            ApplyOptions(ctx, path, line);
        }
    }

    ExpandedSource ExpandIncludesText(IncludeContext& ctx, const std::string& path, const std::string& text) {
        ExpandedSource out;
        Expand(ctx, path, text, out, 0);
        return out;
    }

    ExpandedSource ExpandIncludes(IncludeContext& ctx, const std::string& path) {
        std::string text;
        if (!ctx.pack->Read(path, text)) {
            ExpandedSource out;
            out.ok = false;
            out.error = "cannot read " + path;
            return out;
        }
        return ExpandIncludesText(ctx, path, text);
    }
}
