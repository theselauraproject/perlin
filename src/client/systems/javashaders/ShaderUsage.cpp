#include "client/systems/javashaders/ShaderUsage.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <string>

namespace perlin::jshader {
    namespace {
        constexpr size_t kMinIdentifier = 5, kMaxIdentifier = 12;

        bool IdentifierStart(char c) {
            return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
        }
        bool IdentifierPart(char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        }

        std::string_view NextIdentifier(std::string_view text, size_t& at) {
            while (at < text.size() && !IdentifierStart(text[at]))
                at++;
            const size_t begin = at;
            while (at < text.size() && IdentifierPart(text[at]))
                at++;
            return text.substr(begin, at - begin);
        }

        bool AllDigits(std::string_view s) {
            return !s.empty() && std::all_of(s.begin(), s.end(), [](char d) { return d >= '0' && d <= '9'; });
        }

        int Number(std::string_view digits) {
            int value = 0;
            std::from_chars(digits.data(), digits.data() + digits.size(), value);
            return value;
        }

        bool ShadowSampler(std::string_view word) {
            if (word == "shadow" || word == "watershadow" || word == "shadowtex")
                return true;
            if (word.starts_with("shadowtex") && (word.size() == 10 || word.size() == 12))
                return word[9] == '0' || word[9] == '1';
            return false;
        }

        void Classify(std::string_view word, ShaderUsage& usage) {
            if (word.size() < kMinIdentifier || word.size() > kMaxIdentifier)
                return;
            if (word.starts_with("colortex") && AllDigits(word.substr(8))) {
                const int index = Number(word.substr(8));
                if (index < kMaxColorTex)
                    usage.colorTex[static_cast<size_t>(index)] = true;
            } else if (word.size() == 5 && word.starts_with("gaux") && word[4] >= '1' && word[4] <= '4') {
                usage.colorTex[static_cast<size_t>(3 + (word[4] - '0'))] = true;
            } else if (word.starts_with("shadowcolor") && AllDigits(word.substr(11))) {
                const int index = Number(word.substr(11));
                if (index < 2) {
                    usage.shadowColor[static_cast<size_t>(index)] = true;
                    usage.shadow = true;
                }
            } else if (word == "gcolor") {
                usage.colorTex[0] = true;
            } else if (word == "gdepth") {
                usage.colorTex[1] = true;
            } else if (word == "shadowcolor") {
                usage.shadowColor[0] = true;
                usage.shadow = true;
            } else if (ShadowSampler(word)) {
                usage.shadow = true;
            }
        }

        void ScanColorImages(std::string_view text, ShaderUsage& usage) {
            constexpr std::string_view kPrefix = "colorimg";
            for (size_t at = text.find(kPrefix); at != std::string_view::npos;
                 at = text.find(kPrefix, at + kPrefix.size())) {
                if (at > 0 && IdentifierPart(text[at - 1]))
                    continue;
                size_t end = at + kPrefix.size();
                while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end])))
                    end++;
                if (end == at + kPrefix.size() || (end < text.size() && IdentifierPart(text[end])))
                    continue;
                const int index = Number(text.substr(at + kPrefix.size(), end - at - kPrefix.size()));
                if (index >= 0 && index < kMaxColorTex)
                    usage.colorImage[static_cast<size_t>(index)] = true;
            }
        }
    }

    void ShaderUsage::Merge(const ShaderUsage& other) {
        for (size_t i = 0; i < colorTex.size(); i++) {
            colorTex[i] = colorTex[i] || other.colorTex[i];
            colorImage[i] = colorImage[i] || other.colorImage[i];
        }
        for (size_t i = 0; i < shadowColor.size(); i++)
            shadowColor[i] = shadowColor[i] || other.shadowColor[i];
        shadow = shadow || other.shadow;
        depthTex1 = depthTex1 || other.depthTex1;
        depthTex2 = depthTex2 || other.depthTex2;
    }

    ShaderUsage ScanShaderText(std::string_view text) {
        ShaderUsage usage;
        size_t at = 0;
        for (std::string_view word = NextIdentifier(text, at); !word.empty(); word = NextIdentifier(text, at))
            Classify(word, usage);
        usage.depthTex1 = text.contains("depthtex1");
        usage.depthTex2 = text.contains("depthtex2");
        ScanColorImages(text, usage);
        return usage;
    }

}
