#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace perlin::i18n {
    struct LanguageInfo {
        std::string code;
        std::string name;
    };

    class Lang {
    public:
        static Lang& Get();

        void Configure(const std::string& resourceRoot, const std::string& dataDir,
                       const std::string& language);

        void Invalidate() {
            std::lock_guard lock(mutex);
            configured = false;
        }

        const std::string& Language() const { return code; }
        uint64_t Version() const { return version; }

        bool Has(const std::string& key) const;
        bool Find(const std::string& key, std::string& out) const;
        std::string Tr(const std::string& key) const;
        std::string Tr(const std::string& key, const std::vector<std::string>& args) const;
        static std::string Format(const std::string& pattern, const std::vector<std::string>& args);

        std::vector<LanguageInfo> Languages() const;

        static void Parse(const std::string& text, std::unordered_map<std::string, std::string>& out);
        void LoadFromText(const std::string& primary, const std::string& fallback = std::string());

    private:
        void Load();
        mutable std::mutex mutex;
        std::string root, data, code = "en_US", configuredCode;
        std::unordered_map<std::string, std::string> primary, fallback;
        std::vector<LanguageInfo> languages;
        uint64_t version = 1;
        bool configured = false;
    };

    inline std::string Tr(const std::string& key) {
        return Lang::Get().Tr(key);
    }
    inline std::string Tr(const std::string& key, const std::vector<std::string>& args) {
        return Lang::Get().Tr(key, args);
    }
}
