#include "client/ui/RmlScene.hpp"

#include "util/Log.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/DataTypeRegister.h>

namespace perlin::ui {
    struct SceneModels::Entry {
        Scene scene;
        Rml::DataModelHandle handle;
        std::string rml;
    };

    namespace {
        template <class V>
        bool SameKeys(const std::map<std::string, V>& a, const std::map<std::string, V>& b) {
            if (a.size() != b.size())
                return false;
            auto i = a.begin();
            auto j = b.begin();
            for (; i != a.end(); ++i, ++j)
                if (i->first != j->first)
                    return false;
            return true;
        }
    }

    namespace {
        bool SameShape(const std::vector<SceneNode>& a, const std::vector<SceneNode>& b) {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); i++)
                if (a[i].kind != b[i].kind || a[i].runs.size() != b[i].runs.size() ||
                    !SameShape(a[i].children, b[i].children))
                    return false;
            return true;
        }

        bool SameRowShape(const std::vector<SceneRow>& a, const std::vector<SceneRow>& b) {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); i++) {
                if (!SameKeys(a[i].v, b[i].v) || !SameKeys(a[i].runs, b[i].runs) || !SameKeys(a[i].rows, b[i].rows))
                    return false;
                for (const auto& [name, runs] : a[i].runs)
                    if (runs.size() != b[i].runs.at(name).size())
                        return false;
                for (const auto& [name, rows] : a[i].rows)
                    if (!SameRowShape(rows, b[i].rows.at(name)))
                        return false;
            }
            return true;
        }

        class RowDefinition final : public Rml::VariableDefinition {
        public:
            RowDefinition(Rml::VariableDefinition* text, Rml::VariableDefinition* runs)
                : Rml::VariableDefinition(Rml::DataVariableType::Struct), text(text), runs(runs) {}

            void SetRows(Rml::VariableDefinition* definition) { rows = definition; }

            Rml::DataVariable Child(void* ptr, const Rml::DataAddressEntry& address) override {
                SceneRow& row = *static_cast<SceneRow*>(ptr);
                if (address.index >= 0)
                    return {};
                if (const auto it = row.v.find(address.name); it != row.v.end())
                    return Rml::DataVariable(text, &it->second);
                if (const auto it = row.runs.find(address.name); it != row.runs.end())
                    return Rml::DataVariable(runs, &it->second);
                if (const auto it = row.rows.find(address.name); it != row.rows.end())
                    return Rml::DataVariable(rows, &it->second);
                return {};
            }

        private:
            Rml::VariableDefinition* text;
            Rml::VariableDefinition* runs;
            Rml::VariableDefinition* rows = nullptr;
        };

        void RegisterTypes(Rml::DataModelConstructor& c) {
            auto run = c.RegisterStruct<SceneRun>();
            run.RegisterMember("text", &SceneRun::text);
            run.RegisterMember("colored", &SceneRun::colored);
            run.RegisterMember("color", &SceneRun::color);
            run.RegisterMember("weight", &SceneRun::weight);
            run.RegisterMember("style", &SceneRun::style);
            run.RegisterMember("background", &SceneRun::background);
            run.RegisterMember("display", &SceneRun::display);
            run.RegisterMember("width", &SceneRun::width);
            run.RegisterMember("overflow", &SceneRun::overflow);
            c.RegisterArray<std::vector<SceneRun>>();

            auto node = c.RegisterStruct<SceneNode>();
            node.RegisterMember("kind", &SceneNode::kind);
            node.RegisterMember("id", &SceneNode::id);
            node.RegisterMember("cls", &SceneNode::cls);
            node.RegisterMember("snd", &SceneNode::snd);
            node.RegisterMember("vol", &SceneNode::vol);
            node.RegisterMember("pit", &SceneNode::pit);
            node.RegisterMember("x", &SceneNode::x);
            node.RegisterMember("y", &SceneNode::y);
            node.RegisterMember("w", &SceneNode::w);
            node.RegisterMember("h", &SceneNode::h);
            node.RegisterMember("knob", &SceneNode::knob);
            node.RegisterMember("z", &SceneNode::z);
            node.RegisterMember("src", &SceneNode::src);
            node.RegisterMember("uv", &SceneNode::uv);
            node.RegisterMember("nine", &SceneNode::nine);
            node.RegisterMember("tiled", &SceneNode::tiled);
            node.RegisterMember("tileScale", &SceneNode::tileScale);
            node.RegisterMember("clipDirection", &SceneNode::clipDirection);
            node.RegisterMember("tint", &SceneNode::tint);
            node.RegisterMember("alpha", &SceneNode::alpha);
            node.RegisterMember("keep", &SceneNode::keep);
            node.RegisterMember("glint", &SceneNode::glint);
            node.RegisterMember("key", &SceneNode::key);
            node.RegisterMember("color", &SceneNode::color);
            node.RegisterMember("decorator", &SceneNode::decorator);
            node.RegisterMember("family", &SceneNode::family);
            node.RegisterMember("align", &SceneNode::align);
            node.RegisterMember("size", &SceneNode::size);
            node.RegisterMember("lineHeight", &SceneNode::lineHeight);
            node.RegisterMember("runs", &SceneNode::runs);
            c.RegisterArray<std::vector<SceneNode>>();
            node.RegisterMember("children", &SceneNode::children);

            Rml::DataTypeRegister* types = c.GetDataTypeRegister();
            auto row = std::make_unique<RowDefinition>(types->GetDefinition<std::string>(),
                                                       types->GetDefinition<std::vector<SceneRun>>());
            RowDefinition* rowRaw = row.get();
            c.RegisterCustomDataVariableDefinition<SceneRow>(std::move(row));
            auto rowArray = std::make_unique<Rml::ArrayDefinition<std::vector<SceneRow>>>(rowRaw);
            Rml::VariableDefinition* rowArrayRaw = rowArray.get();
            c.RegisterCustomDataVariableDefinition<std::vector<SceneRow>>(std::move(rowArray));
            rowRaw->SetRows(rowArrayRaw);
        }
    }

    SceneModels::SceneModels() = default;
    SceneModels::~SceneModels() = default;

    bool SceneModels::Attach(Rml::Context& context, const std::string& slot, const Scene& scene,
                             const std::string& rml) {
        Detach(context, slot);
        const bool firstUse = !types;
        if (firstUse)
            types = std::make_unique<Rml::DataTypeRegister>();
        Rml::DataModelConstructor constructor = context.CreateDataModel(slot, types.get());
        if (!constructor) {
            if (firstUse)
                types.reset();
            return false;
        }
        if (firstUse)
            RegisterTypes(constructor);
        auto entry = std::make_unique<Entry>();
        entry->scene = scene;
        entry->rml = rml;
        Scene& data = entry->scene;
        constructor.Bind("nodes", &data.nodes);
        for (auto& [name, value] : data.vars)
            constructor.Bind(name, &value);
        for (auto& [name, value] : data.flags)
            constructor.Bind(name, &value);
        for (auto& [name, value] : data.texts)
            constructor.Bind(name, &value);
        for (auto& [name, value] : data.rows)
            constructor.Bind(name, &value);
        for (auto& [name, value] : data.lists)
            constructor.Bind(name, &value);
        entry->handle = constructor.GetModelHandle();
        entries[slot] = std::move(entry);
        return true;
    }

    bool SceneModels::Matches(const std::string& slot, const std::string& rml) const {
        const auto it = entries.find(slot);
        return it != entries.end() && it->second->rml == rml;
    }

    bool SceneModels::Refresh(const std::string& slot, const Scene& scene, const std::string& rml) {
        const auto it = entries.find(slot);
        if (it == entries.end())
            return false;
        Entry& e = *it->second;
        Scene& data = e.scene;
        if (e.rml != rml || !e.handle || !SameKeys(data.vars, scene.vars) || !SameKeys(data.flags, scene.flags) ||
            !SameKeys(data.texts, scene.texts) || !SameKeys(data.rows, scene.rows) ||
            !SameKeys(data.lists, scene.lists))
            return false;
        if (!SameShape(data.nodes, scene.nodes))
            return false;
        for (const auto& [name, list] : data.lists)
            if (!SameShape(list, scene.lists.at(name)))
                return false;
        for (const auto& [name, runs] : data.texts)
            if (runs.size() != scene.texts.at(name).size())
                return false;
        for (const auto& [name, rows] : data.rows)
            if (!SameRowShape(rows, scene.rows.at(name)))
                return false;
        if (data.nodes != scene.nodes) {
            data.nodes = scene.nodes;
            e.handle.DirtyVariable("nodes");
        }
        for (auto& [name, value] : data.vars)
            if (value != scene.vars.at(name)) {
                value = scene.vars.at(name);
                e.handle.DirtyVariable(name);
            }
        for (auto& [name, value] : data.flags)
            if (value != scene.flags.at(name)) {
                value = scene.flags.at(name);
                e.handle.DirtyVariable(name);
            }
        for (auto& [name, value] : data.texts) {
            value = scene.texts.at(name);
            e.handle.DirtyVariable(name);
        }
        for (auto& [name, value] : data.rows) {
            value = scene.rows.at(name);
            e.handle.DirtyVariable(name);
        }
        for (auto& [name, value] : data.lists) {
            value = scene.lists.at(name);
            e.handle.DirtyVariable(name);
        }
        return true;
    }

    void SceneModels::Detach(Rml::Context& context, const std::string& slot) {
        const auto it = entries.find(slot);
        if (it == entries.end())
            return;
        context.RemoveDataModel(slot);
        entries.erase(it);
    }

    void SceneModels::Forget() {
        entries.clear();
        types.reset();
    }
}
