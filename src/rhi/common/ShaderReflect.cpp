#include "rhi/common/ShaderReflect.hpp"

#include <charconv>
#include <cstdio>
#include <cstdlib>

namespace rhi {
    namespace {
        bool IsIdent(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        }
        bool IsSpace(char c) {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        }

        struct Cursor {
            std::string_view text;
            size_t at = 0;

            bool Done() const { return at >= text.size(); }
            void Skip() {
                while (at < text.size() && IsSpace(text[at]))
                    at++;
            }
            bool Eat(std::string_view token) {
                Skip();
                if (text.substr(at, token.size()) != token)
                    return false;
                at += token.size();
                return true;
            }
            bool Number(uint32_t& out) {
                Skip();
                const char* begin = text.data() + at;
                const char* end = text.data() + text.size();
                const auto [p, ec] = std::from_chars(begin, end, out);
                if (ec != std::errc())
                    return false;
                at += static_cast<size_t>(p - begin);
                return true;
            }
            std::string_view Ident() {
                Skip();
                const size_t start = at;
                while (at < text.size() && IsIdent(text[at]))
                    at++;
                return text.substr(start, at - start);
            }
        };

        std::string_view LineAt(std::string_view text, size_t pos, size_t& lineBegin) {
            // an empty line starts and ends on the same newline: search strictly before pos
            size_t b = pos ? text.rfind('\n', pos - 1) : std::string_view::npos;
            b = b == std::string_view::npos ? 0 : b + 1;
            size_t e = text.find('\n', pos);
            if (e == std::string_view::npos)
                e = text.size();
            lineBegin = b;
            return text.substr(b, e - b);
        }

        void ReflectHlsl(std::string_view src, ShaderInterface& out) {
            size_t pos = 0;
            while ((pos = src.find("register(", pos)) != std::string_view::npos) {
                const size_t clauseBegin = pos;
                size_t lineBegin;
                const std::string_view line = LineAt(src, pos, lineBegin);
                Cursor c{src, pos + 9};
                c.Skip();
                if (c.at >= src.size()) {
                    pos += 9;
                    continue;
                }
                ShaderResource r;
                r.registerClass = src[c.at++];
                uint32_t index = 0;
                if (!c.Number(index)) {
                    pos += 9;
                    continue;
                }
                r.binding = index;
                if (c.Eat(",")) {
                    c.Skip();
                    if (c.Eat("space")) {
                        uint32_t space = 0;
                        c.Number(space);
                        r.set = space;
                    }
                }
                if (!c.Eat(")")) {
                    pos += 9;
                    continue;
                }
                r.clauseBegin = clauseBegin;
                r.clauseEnd = c.at;
                pos = c.at;

                Cursor decl{line, 0};
                std::string_view type = decl.Ident();
                while (type == "static" || type == "globallycoherent" || type == "extern")
                    type = decl.Ident();
                if (type == "cbuffer" || type == "ConstantBuffer") {
                    r.kind = ResourceKind::UniformBuffer;
                } else if (type == "SamplerState") {
                    r.kind = ResourceKind::Sampler;
                } else if (type == "SamplerComparisonState") {
                    r.kind = ResourceKind::Sampler;
                    r.comparison = true;
                } else if (type == "Texture2D") {
                    r.kind = ResourceKind::SampledTexture;
                } else if (type == "Texture2DArray") {
                    r.kind = ResourceKind::SampledTexture;
                    r.dim = TextureDim::Tex2DArray;
                } else if (type == "Texture3D") {
                    r.kind = ResourceKind::SampledTexture;
                    r.dim = TextureDim::Tex3D;
                } else if (type == "TextureCube") {
                    r.kind = ResourceKind::SampledTexture;
                    r.dim = TextureDim::Cube;
                } else if (type == "RWTexture2D" || type == "RWTexture2DArray" || type == "RWTexture3D") {
                    r.kind = ResourceKind::StorageTexture;
                    r.dim = type == "RWTexture2DArray" ? TextureDim::Tex2DArray
                            : type == "RWTexture3D"    ? TextureDim::Tex3D
                                                       : TextureDim::Tex2D;
                } else if (type == "StructuredBuffer" || type == "ByteAddressBuffer") {
                    r.kind = ResourceKind::StorageBuffer;
                } else if (type == "RWStructuredBuffer" || type == "RWByteAddressBuffer") {
                    r.kind = ResourceKind::StorageBufferRW;
                } else {
                    continue;
                }
                // `Type<...> name :` or `cbuffer name :`
                std::string_view rest = line.substr(decl.at);
                if (const size_t lt = rest.find('<'); lt != std::string_view::npos && lt < 3) {
                    int depth = 0;
                    size_t i = lt;
                    for (; i < rest.size(); i++) {
                        if (rest[i] == '<')
                            depth++;
                        else if (rest[i] == '>' && --depth == 0)
                            break;
                    }
                    rest = rest.substr(std::min(i + 1, rest.size()));
                }
                Cursor n{rest, 0};
                r.name = std::string(n.Ident());
                out.resources.push_back(std::move(r));
            }
        }

        void ReflectWgsl(std::string_view src, ShaderInterface& out) {
            size_t pos = 0;
            while (pos < src.size()) {
                size_t lineBegin;
                const std::string_view line = LineAt(src, pos, lineBegin);
                pos = lineBegin + line.size() + 1;
                if (line.contains("var<push_constant>"))
                    out.usesPushConstants = true;
                if (line.empty() || line[0] != '@')
                    continue;
                if (line.find("@binding(") == std::string_view::npos ||
                    line.find("@group(") == std::string_view::npos)
                    continue;
                ShaderResource r;
                uint32_t binding = 0, group = 0;
                size_t first = std::string_view::npos, last = 0;
                {
                    const size_t b = line.find("@binding(");
                    Cursor c{line, b + 9};
                    if (!c.Number(binding) || !c.Eat(")"))
                        continue;
                    first = (std::min)(first, b);
                    last = (std::max)(last, c.at);
                }
                {
                    const size_t g = line.find("@group(");
                    Cursor c{line, g + 7};
                    if (!c.Number(group) || !c.Eat(")"))
                        continue;
                    first = (std::min)(first, g);
                    last = (std::max)(last, c.at);
                }
                r.binding = binding;
                r.set = group;
                r.clauseBegin = lineBegin + first;
                r.clauseEnd = lineBegin + last;
                const size_t varAt = line.find("var", last);
                if (varAt == std::string_view::npos)
                    continue;
                Cursor c{line, varAt + 3};
                std::string_view space;
                if (line[c.at] == '<') {
                    const size_t close = line.find('>', c.at);
                    if (close == std::string_view::npos)
                        continue;
                    space = line.substr(c.at + 1, close - c.at - 1);
                    c.at = close + 1;
                }
                r.name = std::string(c.Ident());
                c.Skip();
                if (c.at < line.size() && line[c.at] == ':')
                    c.at++;
                c.Skip();
                const std::string_view type = line.substr(c.at);
                const auto starts = [&](std::string_view p) { return type.substr(0, p.size()) == p; };
                if (space.substr(0, 7) == "uniform") {
                    r.kind = ResourceKind::UniformBuffer;
                } else if (space.substr(0, 7) == "storage") {
                    r.kind = space.contains("read_write") ? ResourceKind::StorageBufferRW
                                                          : ResourceKind::StorageBuffer;
                } else if (starts("sampler_comparison")) {
                    r.kind = ResourceKind::Sampler;
                    r.comparison = true;
                } else if (starts("sampler")) {
                    r.kind = ResourceKind::Sampler;
                } else if (starts("texture_storage_")) {
                    r.kind = ResourceKind::StorageTexture;
                    r.dim = starts("texture_storage_2d_array") ? TextureDim::Tex2DArray
                            : starts("texture_storage_3d")     ? TextureDim::Tex3D
                                                               : TextureDim::Tex2D;
                } else if (starts("texture_depth_")) {
                    r.kind = ResourceKind::SampledTexture;
                    r.depth = true;
                    r.dim = starts("texture_depth_2d_array") ? TextureDim::Tex2DArray
                            : starts("texture_depth_cube")   ? TextureDim::Cube
                                                             : TextureDim::Tex2D;
                } else if (starts("texture_")) {
                    r.kind = ResourceKind::SampledTexture;
                    r.dim = starts("texture_2d_array") ? TextureDim::Tex2DArray
                            : starts("texture_3d")     ? TextureDim::Tex3D
                            : starts("texture_cube")   ? TextureDim::Cube
                                                       : TextureDim::Tex2D;
                } else {
                    continue;
                }
                out.resources.push_back(std::move(r));
            }
        }

        void ReflectMsl(std::string_view src, ShaderInterface& out) {
            // resources are entry point parameters: `texture2d<float, access::sample> name [[texture(28)]]`.
            // textures and samplers are set 1, buffers set 0, except where a
            // `// perlin:msl-sets texture <base> <set> buffer <base> <set>` line moves the indices from a base on
            // to another set (java packs: storage images in set 2, storage buffers in set 3)
            uint32_t textureBase = ~0u, textureSet = 1, bufferBase = ~0u, bufferSet = 0;
            if (const size_t m = src.find("// perlin:msl-sets "); m != std::string_view::npos) {
                const std::string line(src.substr(m, src.find('\n', m) - m));
                unsigned tb = 0, ts = 0, bb = 0, bs = 0;
                if (std::sscanf(line.c_str(), "// perlin:msl-sets texture %u %u buffer %u %u", &tb, &ts, &bb,
                                &bs) == 4) {
                    textureBase = tb;
                    textureSet = ts;
                    bufferBase = bb;
                    bufferSet = bs;
                }
            }
            size_t pos = 0;
            while ((pos = src.find("[[", pos)) != std::string_view::npos) {
                const size_t clauseBegin = pos;
                Cursor c{src, pos + 2};
                const std::string_view kind = c.Ident();
                pos += 2;
                if (kind != "buffer" && kind != "texture" && kind != "sampler")
                    continue;
                if (!c.Eat("("))
                    continue;
                uint32_t index = 0;
                if (!c.Number(index) || !c.Eat(")") || !c.Eat("]]"))
                    continue;
                ShaderResource r;
                r.binding = index;
                r.nativeIndex = index;
                r.clauseBegin = clauseBegin;
                r.clauseEnd = c.at;
                // the parameter declaration runs back to the previous `,` or `(` outside any <...>
                size_t b = clauseBegin;
                int angle = 0;
                while (b > 0) {
                    const char ch = src[b - 1];
                    if (ch == '>')
                        angle++;
                    else if (ch == '<')
                        angle--;
                    else if ((ch == ',' || ch == '(') && angle <= 0)
                        break;
                    b--;
                }
                std::string_view decl = src.substr(b, clauseBegin - b);
                while (!decl.empty() && IsSpace(decl.back()))
                    decl.remove_suffix(1);
                size_t nameBegin = decl.size();
                while (nameBegin > 0 && IsIdent(decl[nameBegin - 1]))
                    nameBegin--;
                r.name = std::string(decl.substr(nameBegin));
                while (!decl.empty() && IsSpace(decl.front()))
                    decl.remove_prefix(1);
                const auto starts = [&](std::string_view p) { return decl.substr(0, p.size()) == p; };
                if (kind == "sampler") {
                    r.kind = ResourceKind::Sampler;
                    r.set = 1;
                } else if (kind == "texture") {
                    r.set = 1;
                    if (starts("depth")) {
                        r.depth = true;
                        r.kind = ResourceKind::SampledTexture;
                        r.dim = starts("depth2d_array") ? TextureDim::Tex2DArray
                                : starts("depthcube")   ? TextureDim::Cube
                                                        : TextureDim::Tex2D;
                    } else {
                        const bool write =
                            decl.contains("access::write") || decl.contains("access::read_write");
                        r.kind = write ? ResourceKind::StorageTexture : ResourceKind::SampledTexture;
                        r.dim = starts("texture2d_array") ? TextureDim::Tex2DArray
                                : starts("texture3d")     ? TextureDim::Tex3D
                                : starts("texturecube")   ? TextureDim::Cube
                                                          : TextureDim::Tex2D;
                    }
                    if (index >= textureBase) {
                        r.set = textureSet;
                        r.binding = index - textureBase;
                    }
                } else {
                    r.set = 0;
                    if (index >= bufferBase) {
                        r.set = bufferSet;
                        r.binding = index - bufferBase;
                    }
                    if (starts("constant"))
                        r.kind = ResourceKind::UniformBuffer;
                    else if (starts("const device"))
                        r.kind = ResourceKind::StorageBuffer;
                    else
                        r.kind = ResourceKind::StorageBufferRW;
                }
                out.resources.push_back(std::move(r));
                pos = c.at;
            }
        }
    }

    const ShaderResource* ShaderInterface::Find(uint32_t set, uint32_t binding, ResourceKind kind) const {
        for (const ShaderResource& r : resources)
            if (r.set == set && r.binding == binding && r.kind == kind)
                return &r;
        return nullptr;
    }

    ShaderInterface ReflectShader(ShaderFormat format, std::string_view source) {
        ShaderInterface out;
        switch (format) {
        case ShaderFormat::Wgsl:
            ReflectWgsl(source, out);
            break;
        case ShaderFormat::Hlsl:
            ReflectHlsl(source, out);
            break;
        case ShaderFormat::Msl:
            ReflectMsl(source, out);
            break;
        default:
            break;
        }
        return out;
    }
}
