#pragma once
#include "client/javashaders/Include.hpp"
#include "client/javashaders/PackSource.hpp"
#include "client/javashaders/Properties.hpp"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace perlin::jshader {
    enum class Stage { Vertex, Geometry, Fragment, Compute };

    struct Directives {
        std::map<std::string, std::string> constants;
        double Number(const std::string& name, double fallback) const;
        bool Bool(const std::string& name, bool fallback) const;
        std::string Text(const std::string& name, const std::string& fallback = {}) const;
    };

    struct ProgramSource {
        std::string name;
        std::string sourceName;
        ExpandedSource vertex, geometry, fragment;
        std::vector<ExpandedSource> compute;
        std::vector<int> drawBuffers;
        bool hasDrawBuffersTag = false;
        bool found = false;
    };

    struct ProgramInfo {
        const char* name;
        const char* fallback;
    };
    const std::vector<ProgramInfo>& StandardPrograms();
    std::string FallbackFor(const std::string& name);

    class ShaderPackInstance {
    public:
        ShaderPackInstance(std::shared_ptr<PackSource> pack, OptionValues overrides, MacroMap engineMacros);

        const PackSource& Source() const { return *pack; }
        const Properties& Props() const { return props; }
        // shaders.properties without preprocessing: iris reads screens, sliders, profiles and feature flags
        // from it so #if branches do not change the option layout
        const Properties& RawProps() const { return rawProps; }
        // iris feature flags the pack asked for that the renderer has (IRIS_FEATURE_* in the shaders)
        const std::set<std::string>& ActiveFeatures() const { return activeFeatures; }
        // required feature flags the renderer lacks: the pack cannot run
        const std::vector<std::string>& MissingFeatures() const { return missingFeatures; }
        const MacroMap& Macros() const { return macros; }
        const MacroMap& EngineMacros() const { return engineMacros; }
        const std::vector<ShaderOption>& Options() const { return options; }
        const OptionValues& Overrides() const { return overrides; }
        const std::string& Name() const { return packName; }

        void SetDimension(const std::string& folder);
        const std::string& Dimension() const { return dimension; }

        std::shared_ptr<ProgramSource> Program(const std::string& name);
        std::vector<std::string> PassChain(const std::string& base) const;
        bool ProgramExists(const std::string& name) const;
        bool ProgramEnabled(const std::string& name) const;

        const Directives& GetDirectives();
        const std::map<int, std::vector<std::string>>& BlockIds() const { return blockIds; }
        const std::map<int, std::vector<std::string>>& EntityIds() const { return entityIds; }
        const std::map<int, std::vector<std::string>>& ItemIds() const { return itemIds; }
        std::string Lang(const std::string& key, const std::string& locale = "en_US") const;
        std::string FirstError() const { return firstError; }

        void Reload(OptionValues overrides);

    private:
        std::string FindFile(const std::string& base, const char* ext) const;
        std::shared_ptr<ProgramSource> Load(const std::string& name);
        void LoadProperties();
        void ScanOptions();
        void LoadIdTables();
        void LoadLang();

        std::shared_ptr<PackSource> pack;
        OptionValues overrides;
        MacroMap engineAll;
        MacroMap engineMacros;
        MacroMap macros;
        Properties props, rawProps;
        std::set<std::string> activeFeatures;
        std::vector<std::string> missingFeatures;
        std::vector<ShaderOption> options;
        std::string dimension;
        std::string packName;
        std::map<std::string, std::shared_ptr<ProgramSource>> cache;
        Directives directives;
        bool directivesLoaded = false;
        std::map<int, std::vector<std::string>> blockIds, entityIds, itemIds;
        std::map<std::string, std::map<std::string, std::string>> lang;
        std::string firstError;
    };
}
