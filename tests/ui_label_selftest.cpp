#include "client/ui/JsonUi.hpp"
#include "client/ui/UiLayout.hpp"

#include <cstdio>
#include <functional>
#include <string>

using namespace perlin::ui;
namespace json = perlin::json;

static int fails = 0;
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                               \
            fails++;                                                                                         \
        }                                                                                                    \
    } while (0)

namespace {
    struct NoData : UiData {
        bool Global(const std::string&, json::Value&) const override { return false; }
    };

    json::Value Parse(const char* text) {
        json::Value v;
        std::string error;
        json::Value::Parse(text, v, error);
        return v;
    }

    const LNode* FindLabel(const LNode& n) {
        if (n.type == "label")
            return &n;
        for (const auto& kid : n.kids)
            if (const LNode* found = FindLabel(*kid))
                return found;
        return nullptr;
    }

    // a 6-unit monospace font keeps the arithmetic obvious
    const LNode* Layout(const std::string& text, std::unique_ptr<LNode>& keep, Node& root) {
        root = Node{};
        root.name = "root";
        root.type = "panel";
        root.props = Parse(R"({"type":"panel","size":["100%","100%"]})");
        auto label = std::make_unique<Node>();
        label->name = "label";
        label->type = "label";
        label->props = Parse(R"({"type":"label","size":["default",10],"anchor_from":"top_left","anchor_to":"top_left"})");
        label->props.Set("text", json::Value(text));
        root.children.push_back(std::move(label));
        static NoData data;
        UiServices services;
        services.textWidth = [](const std::string& s, float scale, bool) {
            return 6.0f * static_cast<float>(s.size()) * scale;
        };
        services.lineHeight = [](float scale, bool) { return 9.0f * scale; };
        services.localize = [](const std::string& k) { return k; };
        keep = EvaluateTree(root, data, services, 200.0f, 100.0f);
        return keep ? FindLabel(*keep) : nullptr;
    }
}

int main() {
    {
        // a leading space is drawn, not just counted in the width (scoreboard rows often start with one)
        std::unique_ptr<LNode> keep;
        Node root;
        const LNode* l = Layout(" Points: 0", keep, root);
        CHECK(l != nullptr);
        if (l) {
            CHECK(l->lines.size() == 1);
            CHECK(!l->lines.empty() && l->lines[0] == " Points: 0");
            CHECK(l->w == 6.0f * 10.0f);
        }
    }
    {
        std::unique_ptr<LNode> keep;
        Node root;
        const LNode* l = Layout("a  b", keep, root);
        CHECK(l && l->lines.size() == 1 && l->lines[0] == "a  b");
    }
    if (fails)
        printf("%d failures\n", fails);
    else
        printf("ui_label_selftest ok\n");
    return fails ? 1 : 0;
}
