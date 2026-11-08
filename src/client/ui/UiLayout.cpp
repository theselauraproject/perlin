#include "client/ui/UiLayout.hpp"
#include "client/ui/TenFont.hpp"
#include "client/ui/UiExpr.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <unordered_map>

namespace perlin::ui {
    namespace {
        constexpr float kUnknown = std::numeric_limits<float>::quiet_NaN();
        bool Known(float v) {
            return !std::isnan(v);
        }

        struct Term {
            enum Kind : uint8_t {
                Px,
                Pct,
                PctChildren,
                PctChildrenMax,
                PctSibling,
                PctX,
                PctY,
                Fill,
                Default
            };
            Kind kind = Px;
            double value = 0;
            int sign = 1;
        };
        using Dim = std::vector<Term>;

        Dim ParseDim(const Json& v) {
            Dim dim;
            if (v.IsNumber()) {
                dim.push_back({Term::Px, v.number, 1});
                return dim;
            }
            if (!v.IsString())
                return dim;
            const std::string& s = v.string;
            size_t i = 0;
            int sign = 1;
            const auto skip = [&] {
                while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
                    i++;
            };
            while (true) {
                skip();
                if (i >= s.size())
                    break;
                if (s[i] == '-' && (dim.empty() || sign != 1 || true)) {
                    if (dim.empty()) {
                        sign = -1;
                        i++;
                        skip();
                    }
                } else if (s[i] == '+' && dim.empty()) {
                    i++;
                    skip();
                }
                Term t;
                t.sign = sign;
                if (s.compare(i, 7, "default") == 0) {
                    t.kind = Term::Default;
                    i += 7;
                } else if (s.compare(i, 4, "fill") == 0) {
                    t.kind = Term::Fill;
                    i += 4;
                } else {
                    size_t j = i;
                    while (j < s.size() && (std::isdigit(static_cast<unsigned char>(s[j])) || s[j] == '.'))
                        j++;
                    if (j == i)
                        break;
                    t.value = std::atof(s.substr(i, j - i).c_str());
                    i = j;
                    if (i < s.size() && s[i] == '%') {
                        i++;
                        t.kind = Term::Pct;
                        if (s.compare(i, 2, "cm") == 0) {
                            t.kind = Term::PctChildrenMax;
                            i += 2;
                        } else if (s.compare(i, 2, "sm") == 0) {
                            t.kind = Term::PctSibling;
                            i += 2;
                        } else if (i < s.size() && s[i] == 'c') {
                            t.kind = Term::PctChildren;
                            i++;
                        } else if (i < s.size() && s[i] == 'x') {
                            t.kind = Term::PctX;
                            i++;
                        } else if (i < s.size() && s[i] == 'y') {
                            t.kind = Term::PctY;
                            i++;
                        }
                    } else if (s.compare(i, 2, "px") == 0) {
                        i += 2;
                    }
                }
                dim.push_back(t);
                skip();
                if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
                    sign = s[i] == '-' ? -1 : 1;
                    i++;
                } else {
                    sign = 1;
                }
            }
            return dim;
        }

        struct DimContext {
            float parent = kUnknown;
            float parentX = kUnknown;
            float parentY = kUnknown;
            float children = kUnknown;
            float childrenMax = kUnknown;
            float sibling = kUnknown;
            float fill = kUnknown;
            float intrinsic = kUnknown;
            float defaultValue = kUnknown;
        };

        bool EvalDim(const Dim& dim, const DimContext& c, float& out) {
            double total = 0;
            for (const Term& t : dim) {
                double v = 0;
                switch (t.kind) {
                case Term::Px:
                    v = t.value;
                    break;
                case Term::Pct:
                    if (!Known(c.parent))
                        return false;
                    v = t.value / 100.0 * c.parent;
                    break;
                case Term::PctChildren:
                    if (!Known(c.children))
                        return false;
                    v = t.value / 100.0 * c.children;
                    break;
                case Term::PctChildrenMax:
                    if (!Known(c.childrenMax))
                        return false;
                    v = t.value / 100.0 * c.childrenMax;
                    break;
                case Term::PctSibling:
                    if (!Known(c.sibling))
                        return false;
                    v = t.value / 100.0 * c.sibling;
                    break;
                case Term::PctX:
                    if (!Known(c.parentX))
                        return false;
                    v = t.value / 100.0 * c.parentX;
                    break;
                case Term::PctY:
                    if (!Known(c.parentY))
                        return false;
                    v = t.value / 100.0 * c.parentY;
                    break;
                case Term::Fill:
                    if (Known(c.fill))
                        v = c.fill;
                    else if (Known(c.parent))
                        v = c.parent;
                    else
                        return false;
                    break;
                case Term::Default:
                    if (Known(c.intrinsic))
                        v = c.intrinsic;
                    else if (Known(c.defaultValue))
                        v = c.defaultValue;
                    else
                        return false;
                    break;
                }
                total += t.sign * v;
            }
            out = static_cast<float>(total);
            return true;
        }

        bool UsesKind(const Dim& dim, Term::Kind kind) {
            return std::any_of(dim.begin(), dim.end(), [&](const Term& t) { return t.kind == kind; });
        }
        bool UsesParent(const Dim& dim) {
            return std::any_of(dim.begin(), dim.end(),
                               [](const Term& t) { return t.kind == Term::Pct || t.kind == Term::Fill; });
        }

        double Ease(const std::string& name, double t) {
            t = std::clamp(t, 0.0, 1.0);
            if (name.empty() || name == "linear")
                return t;
            const bool in = !name.starts_with("in_out_") && name.starts_with("in_");
            const bool out = name.starts_with("out_");
            const bool inOut = name.starts_with("in_out_");
            std::string kind = name;
            if (inOut)
                kind = name.substr(7);
            else if (in)
                kind = name.substr(3);
            else if (out)
                kind = name.substr(4);
            constexpr double kPi = 3.14159265358979323846;
            auto easeIn = [&](double x) -> double {
                if (kind == "sine")
                    return 1 - std::cos(x * kPi / 2);
                if (kind == "quad")
                    return x * x;
                if (kind == "cubic")
                    return x * x * x;
                if (kind == "quart")
                    return x * x * x * x;
                if (kind == "quint")
                    return x * x * x * x * x;
                if (kind == "expo")
                    return x == 0 ? 0 : std::pow(2.0, 10 * x - 10);
                if (kind == "circ")
                    return 1 - std::sqrt(1 - x * x);
                if (kind == "back") {
                    const double c1 = 1.70158;
                    return (c1 + 1) * x * x * x - c1 * x * x;
                }
                if (kind == "elastic") {
                    if (x == 0 || x == 1)
                        return x;
                    return -std::pow(2.0, 10 * x - 10) * std::sin((x * 10 - 10.75) * (2 * kPi / 3));
                }
                if (kind == "bounce") {
                    const double y = 1 - x;
                    const double n1 = 7.5625, d1 = 2.75;
                    double b;
                    if (y < 1 / d1)
                        b = n1 * y * y;
                    else if (y < 2 / d1) {
                        const double z = y - 1.5 / d1;
                        b = n1 * z * z + 0.75;
                    } else if (y < 2.5 / d1) {
                        const double z = y - 2.25 / d1;
                        b = n1 * z * z + 0.9375;
                    } else {
                        const double z = y - 2.625 / d1;
                        b = n1 * z * z + 0.984375;
                    }
                    return 1 - b;
                }
                if (kind == "spring") {
                    return 1 - std::exp(-6 * x) * std::cos(x * 2 * kPi * 2);
                }
                return x;
            };
            if (kind == "spring")
                return std::clamp(1 - std::exp(-6 * t) * std::cos(t * 4 * kPi), -0.5, 1.5);
            if (in)
                return easeIn(t);
            if (out)
                return 1 - easeIn(1 - t);
            if (inOut)
                return t < 0.5 ? easeIn(t * 2) / 2 : 1 - easeIn((1 - t) * 2) / 2;
            return t;
        }

        bool AnimNumber(const Json& j, double& v, bool& pct) {
            pct = false;
            if (j.IsNumber()) {
                v = j.number;
                return true;
            }
            if (j.IsString() && !j.string.empty()) {
                char* end = nullptr;
                v = std::strtod(j.string.c_str(), &end);
                if (end == j.string.c_str())
                    return false;
                pct = j.string.contains('%');
                return true;
            }
            return false;
        }

        Json LerpJson(const Json& a, const Json& b, double t) {
            if (a.IsArray() && b.IsArray()) {
                Json out = Json::MakeArray();
                for (size_t i = 0; i < std::min(a.array.size(), b.array.size()); i++)
                    out.Push(LerpJson(a.array[i], b.array[i], t));
                return out;
            }
            double av, bv;
            bool ap, bp;
            if (AnimNumber(a, av, ap) && AnimNumber(b, bv, bp)) {
                const double v = av + (bv - av) * t;
                if (ap || bp) {
                    char buf[48];
                    std::snprintf(buf, sizeof(buf), "%g%%", v);
                    return Json(std::string(buf));
                }
                return Json(v);
            }
            return t < 1.0 ? a : b;
        }

        struct Collection {
            std::string name;
            int index;
        };

        class Evaluator {
        public:
            Evaluator(const UiData& data, const UiServices& services) : data(data), services(services) {}

            std::unique_ptr<LNode> Instantiate(const Node& node,
                                               const std::vector<Collection>& outerCollections,
                                               const Vars* factoryVars, int parentLayer, float parentAlpha,
                                               bool propagateAlpha) {
                auto n = std::make_unique<LNode>();
                n->src = &node;
                n->type = node.type;
                n->id = node.name;

                std::vector<Collection> scoped;
                if (const Json* ci = node.props.Find("collection_index"); ci && ci->IsNumber()) {
                    scoped = outerCollections;
                    for (size_t i = scoped.size(); i-- > 0;)
                        if (scoped[i].index < 0) {
                            scoped[i].index = static_cast<int>(ci->number);
                            break;
                        }
                } else if (const Json* cn = node.props.Find("collection_name");
                           cn && cn->IsString() && !cn->string.empty() && node.type != "grid") {
                    scoped = outerCollections;
                    scoped.push_back({cn->string, -1});
                }
                const std::vector<Collection>& collections = scoped.empty() ? outerCollections : scoped;
                if (!collections.empty()) {
                    n->collection = collections.back().name;
                    n->index = collections.back().index;
                }
                const Vars* vars = factoryVars;
                bool deferredView = false;
                for (const Json& b : node.bindings) {
                    if (!b.IsObject() || !b.Find("binding_type") ||
                        b.Find("binding_type")->StringOr("") != "view")
                        continue;
                    const Json* sc = b.Find("source_control_name");
                    if (sc && sc->IsString() && !sc->string.empty() && sc->string != node.name)
                        deferredView = true;
                }
                ApplyBindings(*n, collections);
                ResolveProps(*n, vars);
                if (const Json* ignored = n->p.Find("ignored"); ignored && Truthy(*ignored))
                    return nullptr;

                bool visible = true;
                if (const Json* v = n->p.Find("visible"))
                    visible = Truthy(*v) || (v->IsString() && v->string.empty());

                if (const Json* v = n->bag.Find("#visible"))
                    visible = Truthy(*v);
                n->visible = visible;
                if (services.anim && services.anim->destroyed.contains(n->id))
                    return nullptr;
                if (visible)
                    ApplyAnimations(*n, AnimKey(*n));

                n->layer = parentLayer + static_cast<int>(n->Num("layer", 0));
                float alpha = 1.0f;
                if (const Json* a = n->p.Find("alpha")) {
                    if (a->IsNumber())
                        alpha = static_cast<float>(a->number);
                    else if (a->IsString() && !a->string.empty() && a->string[0] == '@') {
                        const bool known = a->string.contains("background") ||
                                           a->string.contains("item_name") ||
                                           a->string.contains("actionbar") || a->string.contains("title") ||
                                           a->string.contains("chat");
                        if (!known && services.anim && services.animDef) {
                            std::unordered_map<std::string, Json> out;
                            RunAnimation(a->string, n->src->ns, AnimKey(*n) + "|alpha" + a->string, n.get(),
                                         out);
                            const auto it = out.find("alpha");
                            alpha = it != out.end() && it->second.IsNumber()
                                        ? static_cast<float>(it->second.number)
                                        : 1.0f;
                        } else {
                            alpha = AnimatedAlpha(a->string);
                        }
                    }
                }
                if (const Json* a = n->bag.Find("#alpha"); a && a->IsNumber())
                    alpha = static_cast<float>(a->number);
                n->alpha = propagateAlpha ? alpha * parentAlpha : alpha;
                const bool propagate =
                    n->Bool("propagate_alpha", false) ||
                    (n->bag.Find("#propagateAlpha") && Truthy(*n->bag.Find("#propagateAlpha")));

                if (n->type == "label") {
                    n->text = ResolveText(*n, vars);
                }
                if (!n->visible)
                    return n;

                auto addKid = [&](std::unique_ptr<LNode> kid) {
                    if (kid)
                        n->kids.push_back(std::move(kid));
                };
                for (const auto& child : node.children)
                    addKid(Instantiate(*child, collections, factoryVars, n->layer, n->alpha, propagate));
                if (deferredView) {
                    ApplyBindings(*n, collections);

                    if (n->type == "label")
                        n->text = ResolveText(*n, vars);
                    bool nowVisible = true;
                    if (const Json* v = n->p.Find("visible"))
                        nowVisible = Truthy(*v) || (v->IsString() && v->string.empty());
                    if (const Json* v = n->bag.Find("#visible"))
                        nowVisible = Truthy(*v);
                    if (!nowVisible) {
                        n->visible = false;
                        n->kids.clear();
                        return n;
                    }
                }

                if (node.gridTemplate) {
                    int cols = 1, rows = 1;
                    GridDimensions(*n, cols, rows);
                    const std::string collection = n->Str("collection_name");
                    int count = std::max(0, cols * rows);
                    if (!collection.empty())
                        count = std::min(count, data.CollectionSize(collection));

                    if (const std::string rescale = n->Str("grid_rescaling_type");
                        rescale == "horizontal" || rescale == "vertical") {
                        int maxItems = -1;
                        if (const Json* m = n->bag.Find("#maximum_grid_items"); m && m->IsNumber())
                            maxItems = static_cast<int>(m->number);
                        else if (const Json* m = n->p.Find("maximum_grid_items"); m && m->IsNumber())
                            maxItems = static_cast<int>(m->number);
                        if (collection.empty())
                            count = std::max(0, maxItems);
                        else
                            count = maxItems >= 0 ? std::min(maxItems, data.CollectionSize(collection))
                                                  : data.CollectionSize(collection);
                    }
                    for (int i = 0; i < count; i++) {
                        std::vector<Collection> inner = collections;
                        if (!collection.empty())
                            inner.push_back({collection, i});
                        addKid(Instantiate(*node.gridTemplate, inner, factoryVars, n->layer, n->alpha,
                                           propagate));
                    }
                }
                if (!node.factoryControls.empty()) {
                    std::string name = node.name;
                    if (const Json* f = n->p.Find("factory"); f && f->IsObject())
                        if (const Json* fn = f->Find("name"); fn && fn->IsString())
                            name = fn->string;

                    if (const Json* count = n->bag.Find("#collection_length");
                        count && count->IsNumber() && node.factoryControls.size() == 1) {
                        const std::string c = n->Str("collection_name");
                        const int total = std::clamp(static_cast<int>(count->number), 0, 1000);
                        for (int i = 0; i < total; i++) {
                            std::vector<Collection> inner = collections;
                            if (!inner.empty() && inner.back().name == c && inner.back().index < 0)
                                inner.back().index = i;
                            else if (!c.empty())
                                inner.push_back({c, i});
                            auto kid = Instantiate(*node.factoryControls.front().second, inner, factoryVars,
                                                   n->layer, n->alpha, propagate);
                            if (kid)
                                kid->fromFactory = true;
                            addKid(std::move(kid));
                        }
                    }
                    if (const Json* ids = n->bag.Find("#collection_length"); ids && ids->IsArray()) {
                        int index = 0;
                        for (const Json& id : ids->array) {
                            if (id.IsString())
                                for (const auto& entry : node.factoryControls)
                                    if (entry.first == id.string) {
                                        std::vector<Collection> inner = collections;
                                        if (const std::string c = n->Str("collection_name"); !c.empty())
                                            inner.push_back({c, index});
                                        auto kid = Instantiate(*entry.second, inner, factoryVars, n->layer,
                                                               n->alpha, propagate);
                                        if (kid)
                                            kid->fromFactory = true;
                                        addKid(std::move(kid));
                                    }
                            index++;
                        }
                    }
                    for (const FactoryItem& item : data.Factory(name))
                        for (const auto& entry : node.factoryControls)
                            if (entry.first == item.id) {
                                std::vector<Collection> inner = collections;
                                if (!item.collection.empty())
                                    inner.push_back({item.collection, item.index});
                                auto kid = Instantiate(*entry.second, inner, &item.vars, n->layer, n->alpha,
                                                       propagate);
                                if (kid)
                                    kid->fromFactory = true;
                                addKid(std::move(kid));
                            }
                }
                return n;
            }

            void Measure(LNode& n, float pw, float ph, float fillW, float fillH, float sibW, float sibH) {
                n.dependsW = n.dependsH = false;
                const bool isStack = n.type == "stack_panel";
                const bool isGrid = n.type == "grid";

                Dim wd, hd;
                SizeSpecs(n, wd, hd);
                n.dependsW = UsesParent(wd);
                n.dependsH = UsesParent(hd);

                float intrinsicW = kUnknown, intrinsicH = kUnknown;
                Intrinsic(n, pw, wd, intrinsicW, intrinsicH);

                float w = kUnknown, h = kUnknown;
                auto resolve = [&]() {
                    DimContext cw, ch;
                    cw.parent = pw;
                    ch.parent = ph;

                    cw.parentX = ch.parentX = pw;
                    cw.parentY = ch.parentY = ph;
                    cw.fill = fillW;
                    ch.fill = fillH;
                    cw.sibling = sibW;
                    ch.sibling = sibH;
                    cw.intrinsic = intrinsicW;
                    ch.intrinsic = intrinsicH;
                    if (!Known(w))
                        EvalDim(wd, cw, w);
                    if (!Known(h))
                        EvalDim(hd, ch, h);
                    if (!Known(w))
                        EvalDim(wd, cw, w);
                    if (!Known(h))
                        EvalDim(hd, ch, h);
                };
                resolve();

                ClampSize(n, pw, ph, w, h);
                const bool wKnown0 = Known(w), hKnown0 = Known(h);
                const float w0 = w, h0 = h;

                const bool needsChildrenW =
                    UsesKind(wd, Term::PctChildren) || UsesKind(wd, Term::PctChildrenMax);
                const bool needsChildrenH =
                    UsesKind(hd, Term::PctChildren) || UsesKind(hd, Term::PctChildrenMax);
                const bool inheritW = n.Bool("inherit_max_sibling_width", false),
                           inheritH = n.Bool("inherit_max_sibling_height", false);
                const bool needsSiblingW = UsesKind(wd, Term::PctSibling) || inheritW;
                const bool needsSiblingH = UsesKind(hd, Term::PctSibling) || inheritH;
                n.needSibW = needsSiblingW;
                n.needSibH = needsSiblingH;

                std::vector<LNode*> live;
                for (auto& k : n.kids)
                    if (k->visible)
                        live.push_back(k.get());

                const bool vertical = isStack && StackVertical(n);
                MeasureChildren(n, live, w, h, isStack, vertical, isGrid);

                float sumW = 0, sumH = 0, maxW = 0, maxH = 0;
                if (isGrid) {
                    GridExtent(n, live, Known(w) ? w : n.w, Known(h) ? h : n.h, sumW, sumH);
                    maxW = sumW;
                    maxH = sumH;
                } else {

                    std::vector<std::string> resting;
                    if (n.type == "button" || n.type == "edit_box") {
                        for (const char* key : {"hover_control", "pressed_control", "locked_control"})
                            if (const std::string name = n.Str(key);
                                !name.empty() && name != n.Str("default_control"))
                                resting.push_back(name);
                    } else if (n.type == "toggle" || n.type == "dropdown") {
                        for (const char* key :
                             {"checked_control", "checked_hover_control", "unchecked_hover_control",
                              "checked_locked_control", "unchecked_locked_control",
                              "checked_locked_hover_control", "unchecked_locked_hover_control"})
                            if (const std::string name = n.Str(key);
                                !name.empty() && name != n.Str("unchecked_control"))
                                resting.push_back(name);
                    }
                    for (LNode* k : live) {
                        if (std::find(resting.begin(), resting.end(), k->id) != resting.end())
                            continue;
                        maxW = std::max(maxW, k->w);
                        maxH = std::max(maxH, k->h);
                        sumW += k->w;
                        sumH += k->h;
                    }
                }
                {
                    DimContext cw, ch;
                    cw.parent = pw;
                    ch.parent = ph;
                    cw.parentX = ch.parentX = pw;
                    cw.parentY = ch.parentY = ph;
                    cw.fill = fillW;
                    ch.fill = fillH;
                    cw.sibling = sibW;
                    ch.sibling = sibH;
                    cw.intrinsic = intrinsicW;
                    ch.intrinsic = intrinsicH;
                    cw.childrenMax = maxW;
                    ch.childrenMax = maxH;

                    cw.children = sumW;
                    ch.children = sumH;
                    if (!Known(w) || needsChildrenW) {
                        float t = kUnknown;
                        if (EvalDim(wd, cw, t))
                            w = t;
                    }
                    if (!Known(h) || needsChildrenH) {
                        float t = kUnknown;
                        if (EvalDim(hd, ch, t))
                            h = t;
                    }
                    if (!Known(w)) {
                        float t = kUnknown;
                        if (EvalDim(wd, cw, t))
                            w = t;
                    }
                    if (!Known(h)) {
                        float t = kUnknown;
                        if (EvalDim(hd, ch, t))
                            h = t;
                    }
                }
                if (!Known(w))
                    w = 0;
                if (!Known(h))
                    h = 0;
                if (inheritW && Known(sibW))
                    w = std::max(w, sibW);
                if (inheritH && Known(sibH))
                    h = std::max(h, sibH);
                ClampSize(n, pw, ph, w, h);
                n.w = w;
                n.h = h;

                {
                    bool redo = false;
                    for (LNode* k : live)
                        if ((k->dependsW && (!wKnown0 || w0 != n.w)) ||
                            (k->dependsH && (!hKnown0 || h0 != n.h)))
                            redo = true;
                    if (redo)
                        MeasureChildren(n, live, n.w, n.h, isStack, vertical, isGrid);
                }
            }

            void Place(LNode& n, float ax, float ay) {
                n.ax = ax;
                n.ay = ay;
                if (n.type == "label")
                    WrapFinal(n);
                std::vector<LNode*> live;
                for (auto& k : n.kids)
                    if (k->visible)
                        live.push_back(k.get());
                if (n.type == "stack_panel") {
                    const bool vertical = StackVertical(n);

                    const bool childAnchors = n.Bool("use_child_anchors", false);
                    float cursor = 0;
                    for (LNode* k : live) {
                        float ox = 0, oy = 0;
                        if (vertical) {
                            float cx = 0, cy = 0;
                            if (childAnchors)
                                AnchorPosition(*k, n.w, n.h, cx, cy);
                            k->x = cx + ox;
                            k->y = cursor + oy;
                            cursor += k->h;
                        } else {
                            float cx = 0, cy = 0;
                            if (childAnchors)
                                AnchorPosition(*k, n.w, n.h, cx, cy);
                            k->x = cursor + ox;
                            k->y = cy + oy;
                            cursor += k->w;
                        }
                        Place(*k, ax + k->x, ay + k->y);
                    }
                } else if (n.type == "grid") {
                    PlaceGrid(n, live, ax, ay);
                } else {
                    for (LNode* k : live) {
                        float ox = 0, oy = 0;
                        Offset(*k, n.w, n.h, ox, oy);
                        float cx = 0, cy = 0;
                        AnchorPosition(*k, n.w, n.h, cx, cy);
                        k->x = cx + ox;
                        k->y = cy + oy;
                        Place(*k, ax + k->x, ay + k->y);
                    }
                }
            }

        private:
            const UiData& data;
            const UiServices& services;
            std::unordered_map<std::string, LNode*> byName;

            void WrapFinal(LNode& n) {
                const float scale = FontScale(n);
                const bool ten = TenLabel(n);
                int lines = 1;
                float width = 0;
                n.lines.clear();
                if (n.text.empty())
                    return;

                const float wrap = std::max(n.w, n.textWidth) + 0.01f;
                WrapLines(n.text, scale, ten, wrap, n.lines);
                (void)lines;
                (void)width;
            }

            void WrapLines(const std::string& text, float scale, bool ten, float wrapWidth,
                           std::vector<std::string>& out) const {
                const auto measure = [&](const std::string& s) { return Measure(s, scale, ten); };
                size_t start = 0;
                while (start <= text.size()) {
                    const size_t nl = text.find('\n', start);
                    const std::string paragraph =
                        text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
                    std::string line;
                    size_t i = 0;
                    while (i <= paragraph.size()) {
                        const size_t sp = paragraph.find(' ', i);
                        const std::string word =
                            paragraph.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
                        const std::string candidate = line.empty() ? word : line + " " + word;
                        if (!line.empty() && measure(candidate) > wrapWidth) {
                            out.push_back(line);
                            line = word;
                        } else {
                            line = candidate;
                        }
                        if (sp == std::string::npos)
                            break;
                        i = sp + 1;
                    }
                    out.push_back(line);
                    if (nl == std::string::npos)
                        break;
                    start = nl + 1;
                }
            }

            bool Lookup(const LNode& n, const std::string& name, const Vars* vars, Json& out) const {
                if (name.empty())
                    return false;
                if (name[0] == '#') {
                    if (const Json* v = n.bag.Find(name)) {
                        out = *v;
                        return true;
                    }
                    return data.Global(name, out);
                }
                if (name[0] == '$' && vars) {
                    auto it = vars->find(name);
                    if (it != vars->end()) {
                        out = it->second;
                        return true;
                    }
                }
                return false;
            }

            Json Resolve(const LNode& n, const Json& v, const Vars* vars) const {
                if (!v.IsString())
                    return v;
                const std::string& s = v.string;
                if (s.empty())
                    return v;
                if (s[0] == '#' || (s[0] == '$' && s.find_first_of(" ()") == std::string::npos)) {
                    Json out;
                    if (Lookup(n, s, vars, out))
                        return out;
                    return s[0] == '#' ? Json() : v;
                }
                if (IsExpression(s)) {
                    ExprEnv env;
                    env.binding = [&](const std::string& name, Json& out) {
                        return Lookup(n, name, vars, out);
                    };
                    env.variable = [&](const std::string& name, Json& out) {
                        return Lookup(n, name, vars, out);
                    };
                    Json result;
                    if (EvalExpression(s, env, result))
                        return result;
                    return Json();
                }
                return v;
            }

            void ApplyBindings(LNode& n, const std::vector<Collection>& collections) {
                byName[n.id] = &n;

                if (const Json* bag = n.src->props.Find("property_bag"); bag && bag->IsObject())
                    for (const auto& member : bag->object)
                        if (!n.bag.Find(member.first))
                            n.bag.Set(member.first, member.second);
                for (const Json& b : n.src->bindings) {
                    if (!b.IsObject())
                        continue;
                    const std::string type =
                        b.Find("binding_type") ? b.Find("binding_type")->StringOr("global") : "global";

                    if (type == "collection_details" || type == "none")
                        continue;
                    if (type == "view") {
                        const Json* expr = b.Find("source_property_name");
                        const Json* target = b.Find("target_property_name");
                        if (!expr || !expr->IsString() || !target || !target->IsString())
                            continue;
                        const LNode* source = &n;
                        if (const Json* sc = b.Find("source_control_name");
                            sc && sc->IsString() && !sc->string.empty()) {

                            std::string control = sc->string;
                            while (!control.empty() && (control.front() == '(' || control.front() == '\'' ||
                                                        control.front() == ' '))
                                control.erase(0, 1);
                            while (!control.empty() &&
                                   (control.back() == ')' || control.back() == '\'' || control.back() == ' '))
                                control.pop_back();
                            auto it = byName.find(control);
                            if (it != byName.end())
                                source = it->second;
                        }
                        Json result;
                        std::string text = expr->string;
                        if (!IsExpression(text) && !text.empty() && text[0] == '#') {
                            Lookup(*source, text, nullptr, result);
                        } else {
                            ExprEnv env;
                            env.binding = [&](const std::string& name, Json& out) {
                                return Lookup(*source, name, nullptr, out);
                            };
                            EvalExpression(text, env, result);
                        }

                        if (result.IsString() && result.string.size() > 1 && result.string[0] == '#' &&
                            result.string != target->string)
                            if (const Json* named = n.bag.Find(result.string))
                                result = *named;
                        n.bag.Set(target->string, result);
                        continue;
                    }
                    const Json* nameJson = b.Find("binding_name");
                    if (!nameJson || !nameJson->IsString())
                        continue;
                    const std::string name = nameJson->string;
                    std::string target = name;
                    if (const Json* o = b.Find("binding_name_override"); o && o->IsString())
                        target = o->string;
                    Json value;
                    bool have = false;

                    int row = -1;
                    std::string rowCollection;
                    if (type == "collection") {
                        const Json* c = b.Find("binding_collection_name");
                        rowCollection = c && c->IsString() ? c->string : std::string();
                        for (size_t i = collections.size(); i-- > 0;)
                            if (collections[i].name == rowCollection) {
                                row = collections[i].index;
                                break;
                            }
                    }
                    if (IsExpression(name)) {

                        ExprEnv env;
                        env.binding = [&](const std::string& nm, Json& out) {
                            if (row >= 0 && data.CollectionValue(rowCollection, row, nm, out))
                                return true;
                            return Lookup(n, nm, nullptr, out);
                        };
                        have = EvalExpression(name, env, value);

                        if (have && value.IsString() && value.string.size() > 1 && value.string[0] == '#') {
                            const std::string named = value.string;
                            have = row >= 0 ? data.CollectionValue(rowCollection, row, named, value)
                                            : data.Global(named, value);
                        }
                    } else if (type == "collection") {
                        const Json* c = b.Find("binding_collection_name");
                        const std::string collection = c && c->IsString() ? c->string : std::string();
                        for (size_t i = collections.size(); i-- > 0;)
                            if (collections[i].name == collection) {
                                have = data.CollectionValue(collection, collections[i].index, name, value);
                                break;
                            }
                    } else {
                        have = data.Global(name, value);
                    }
                    if (have)
                        n.bag.Set(target, std::move(value));
                    else if (target == "#visible")
                        n.bag.Set(target, Json(false));
                }
            }

            void ResolveProps(LNode& n, const Vars* vars) {
                for (const auto& member : n.src->props.object) {
                    const std::string& key = member.first;
                    if (key == "controls" || key == "bindings" || key == "anims")
                        continue;
                    const Json& v = member.second;
                    if (v.IsString() && !v.string.empty() &&
                        (v.string[0] == '#' || v.string[0] == '$' || IsExpression(v.string)))
                        n.p.Set(key, Resolve(n, v, vars));
                    else
                        n.p.Set(key, v);
                }
            }

            std::string ResolveText(LNode& n, const Vars* vars) {
                const Json* t = n.p.Find("text");
                std::string text;
                if (t && t->IsString())
                    text = t->string;
                else if (t && t->IsNumber())
                    text = t->Dump();
                if (t && t->IsNull()) {
                    Json b;

                    if (Lookup(n, "#text", vars, b))
                        text = b.IsString() ? b.string : (b.IsNull() ? std::string() : b.Dump());
                }
                if (n.Bool("localize", true) && services.localize && !text.empty() && !text.contains(' ') &&
                    text.contains('.'))
                    text = services.localize(text);
                return text;
            }

            void RunAnimation(const std::string& ref, const std::string& ns, const std::string& key, LNode* n,
                              std::unordered_map<std::string, Json>& out) {
                AnimHost* host = services.anim;
                if (!host || !services.animDef || ref.empty())
                    return;
                struct Step {
                    Json def;
                    double duration = 0;
                };
                std::vector<Step> chain;
                std::unordered_map<std::string, size_t> visited;
                size_t loopStart = SIZE_MAX;
                std::string cur = ref;
                while (!cur.empty() && chain.size() < 64) {
                    const auto hit = visited.find(cur);
                    if (hit != visited.end()) {
                        loopStart = hit->second;
                        break;
                    }
                    Json def = services.animDef(cur, ns);
                    if (!def.IsObject())
                        break;
                    visited[cur] = chain.size();
                    Step s;
                    s.duration =
                        std::max(0.0, def.Find("duration") ? def.Find("duration")->NumberOr(0) : 0.0);
                    const Json* next = def.Find("next");
                    s.def = std::move(def);
                    chain.push_back(std::move(s));
                    cur = next && next->IsString() ? next->string : std::string();
                }
                if (chain.empty())
                    return;

                auto& rec = host->recs[key];
                if (rec.seen == 0 && !rec.finished)
                    rec.start = host->now;
                rec.seen = host->epoch;

                double start = rec.start;
                const Json& first = chain.front().def;
                if (const Json* pe = first.Find("play_event"); pe && pe->IsString() && !pe->string.empty()) {
                    const auto ev = host->events.find(pe->string);
                    if (ev == host->events.end())
                        return;
                    start = std::max(start, ev->second);
                }
                if (const Json* rn = first.Find("animation_reset_name");
                    rn && rn->IsString() && !rn->string.empty()) {
                    const auto ev = host->events.find(rn->string);
                    if (ev != host->events.end() && ev->second > start)
                        start = ev->second;
                }
                double elapsed = std::max(0.0, host->now - start);
                if (loopStart != SIZE_MAX) {
                    double pre = 0, loop = 0;
                    for (size_t i = 0; i < chain.size(); i++)
                        (i < loopStart ? pre : loop) += chain[i].duration;
                    if (loop > 1e-4 && elapsed > pre)
                        elapsed = pre + std::fmod(elapsed - pre, loop);
                    else if (loop <= 1e-4)
                        elapsed = pre;
                }

                auto currentOf = [&](const std::string& type) -> Json {
                    if (n) {
                        if (type == "alpha")
                            return Json(1.0);
                        if (type == "offset" || type == "size" || type == "uv") {
                            if (const Json* v = n->p.Find(type); v && v->IsArray())
                                return *v;
                        }
                    }
                    Json z = Json::MakeArray();
                    z.Push(Json(0.0));
                    z.Push(Json(0.0));
                    return type == "alpha" || type == "clip" ? Json(0.0) : z;
                };
                bool active = false, finished = true;
                for (size_t i = 0; i < chain.size(); i++) {
                    const Json& def = chain[i].def;
                    const std::string type =
                        def.Find("anim_type") ? def.Find("anim_type")->StringOr("") : std::string();
                    const double d = chain[i].duration;
                    const bool current = elapsed < d || (d <= 0 && i + 1 == chain.size() && false);
                    double t = d > 0 ? std::min(1.0, elapsed / d) : 1.0;
                    if (type == "flip_book") {
                        const double fps = def.Find("fps") ? def.Find("fps")->NumberOr(1) : 1.0;
                        const int frames = std::max(
                            1, static_cast<int>(def.Find("frame_count") ? def.Find("frame_count")->NumberOr(1)
                                                                        : 1));
                        int frame =
                            static_cast<int>(std::floor(std::min(elapsed, d > 0 ? d : elapsed) * fps));
                        frame = std::clamp(frame, 0, frames - 1);
                        Json uv = Json::MakeArray();
                        double ix = 0, iy = 0, sx = 0, sy = 0;
                        if (const Json* iu = def.Find("initial_uv");
                            iu && iu->IsArray() && iu->array.size() >= 2) {
                            ix = iu->array[0].NumberOr(0);
                            iy = iu->array[1].NumberOr(0);
                        }
                        if (const Json* fs = def.Find("frame_step");
                            fs && fs->IsArray() && fs->array.size() >= 2) {
                            sx = fs->array[0].NumberOr(0);
                            sy = fs->array[1].NumberOr(0);
                        } else if (const Json* fs1 = def.Find("frame_step"); fs1 && fs1->IsNumber()) {
                            sx = fs1->number;
                        }
                        uv.Push(Json(ix + sx * frame));
                        uv.Push(Json(iy + sy * frame));
                        out["uv"] = uv;
                    } else if (type == "alpha" || type == "offset" || type == "size" || type == "uv" ||
                               type == "color" || type == "clip") {
                        const Json* from = def.Find("from");
                        const Json* to = def.Find("to");
                        const Json fromJson = from ? *from : currentOf(type);
                        const Json toJson = to ? *to : fromJson;
                        const std::string easing =
                            def.Find("easing") ? def.Find("easing")->StringOr("linear") : "linear";
                        out[type] = elapsed >= d ? toJson : LerpJson(fromJson, toJson, Ease(easing, t));
                    }
                    if (elapsed < d) {
                        active = true;
                        finished = false;
                        (void)current;
                        break;
                    }
                    elapsed -= d;
                }
                if (loopStart != SIZE_MAX) {
                    finished = false;
                    active = true;
                }
                if (active)
                    host->animating = true;
                if (finished && !rec.finished) {
                    rec.finished = true;
                    const Json& last = chain.back().def;
                    if (const Json* de = last.Find("destroy_at_end");
                        de && de->IsString() && !de->string.empty()) {
                        rec.destroyName = de->string;
                        host->destroyed.insert(de->string);
                    } else if (const Json* de2 = last.Find("destroy_at_end");
                               de2 && de2->IsBool() && de2->boolean && n) {
                        rec.destroyName = n->id;
                        host->destroyed.insert(n->id);
                    }
                    host->animating = true;
                }
                if (finished) {
                    const Json* roe = chain.back().def.Find("reset_on_end");
                    if (roe && Truthy(*roe))
                        out.clear();
                }
            }

            void ApplyAnimations(LNode& n, const std::string& instanceKey) {
                if (!services.anim || !services.animDef || !n.src)
                    return;
                std::vector<std::string> refs;
                if (const Json* list = n.src->props.Find("anims"); list && list->IsArray())
                    for (const Json& entry : list->array)
                        if (entry.IsString())
                            refs.push_back(entry.string);

                for (const auto& member : n.src->props.object) {
                    if (!member.second.IsString() || member.second.string.empty() ||
                        member.second.string[0] != '@')
                        continue;
                    if (services.animDef(member.second.string, n.src->ns).Find("anim_type"))
                        refs.push_back(member.second.string);
                }
                std::unordered_map<std::string, Json> out;
                for (const std::string& ref : refs)
                    RunAnimation(ref, n.src->ns, instanceKey + "|" + ref, &n, out);
                for (auto& [type, value] : out) {
                    if (type == "clip") {
                        if (value.IsNumber())
                            n.bag.Set("#clip_ratio", value);
                    } else {
                        n.p.Set(type, value);
                    }
                }
            }

            std::string AnimKey(const LNode& n) const {
                std::string key = n.src ? n.src->path : n.id;
                if (n.index >= 0)
                    key += "#" + std::to_string(n.index);
                return key;
            }

            float AnimatedAlpha(const std::string& reference) const {
                Json v;
                float backdrop = 1.0f;
                if (reference.contains("background") && data.Global("#hud_text_background_alpha", v) &&
                    v.IsNumber())
                    backdrop = static_cast<float>(v.number);
                if (reference.contains("item_name") && data.Global("#item_name_alpha", v) && v.IsNumber())
                    return static_cast<float>(v.number) * backdrop;
                if (reference.contains("actionbar") && data.Global("#actionbar_alpha", v) && v.IsNumber())
                    return static_cast<float>(v.number) * backdrop;
                if (reference.contains("title") && data.Global("#title_alpha", v) && v.IsNumber())
                    return static_cast<float>(v.number) * backdrop;
                if (reference.contains("chat") && data.Global("#chat_alpha", v) && v.IsNumber())
                    return static_cast<float>(v.number);
                return 1.0f;
            }

            void GridDimensions(const LNode& n, int& cols, int& rows) const {
                cols = rows = 1;
                if (const Json* d = n.Get("grid_dimensions"); d && d->IsArray() && d->array.size() >= 2) {
                    cols = static_cast<int>(d->array[0].NumberOr(1));
                    rows = static_cast<int>(d->array[1].NumberOr(1));
                }
                if (const Json* b = n.Get("grid_dimension_binding"); b) {
                    Json v;
                    if (b->IsArray())
                        v = *b;
                    else if (b->IsString())
                        data.Global(b->string, v);
                    if (v.IsArray() && v.array.size() >= 2) {
                        cols = static_cast<int>(v.array[0].NumberOr(1));
                        rows = static_cast<int>(v.array[1].NumberOr(1));
                    }
                }
                if (cols < 1)
                    cols = 1;
                if (rows < 1)
                    rows = 1;
            }

            static bool StackVertical(const LNode& n) {
                const Json* o = n.Get("orientation");
                return !(o && o->IsString() && o->string == "horizontal");
            }

            void SizeSpecs(LNode& n, Dim& w, Dim& h) const {
                const Json* size = n.Get("size");
                if (size && size->IsArray() && size->array.size() >= 2) {
                    w = ParseDim(size->array[0]);
                    h = ParseDim(size->array[1]);
                } else if (size && size->IsString()) {
                    w = h = ParseDim(*size);
                }
                const bool fill = n.Bool("fill", false);
                if (w.empty() || h.empty()) {
                    Term hundred{Term::Pct, 100, 1};
                    Term def{Term::Default, 0, 1};

                    const bool intrinsic = n.type == "label";
                    (void)fill;
                    if (n.type == "grid")
                        hundred.kind = Term::PctChildren;
                    if (w.empty())
                        w.push_back(intrinsic ? def : hundred);
                    if (h.empty())
                        h.push_back(intrinsic ? def : hundred);

                    if (n.type == "stack_panel" && !size) {
                        Dim& main = StackVertical(n) ? h : w;
                        main = {Term{Term::PctChildren, 100, 1}};
                    }
                }

            }

            void Intrinsic(LNode& n, float pw, const Dim& wd, float& iw, float& ih) {
                if (n.type == "label") {
                    float scale = FontScale(n);
                    const bool ten = TenLabel(n);

                    const std::string& plain = n.text;
                    float wrap = kUnknown;
                    DimContext c;
                    c.parent = pw;
                    float fixed = kUnknown;
                    if (!wd.empty() && !UsesKind(wd, Term::Default) && !UsesKind(wd, Term::PctChildren) &&
                        !UsesKind(wd, Term::PctChildrenMax) && !UsesKind(wd, Term::PctSibling)) {
                        float t = kUnknown;
                        if (EvalDim(wd, c, t))
                            fixed = t;
                    }
                    if (Known(fixed))
                        wrap = fixed;
                    float maxW = kUnknown;
                    if (const Json* m = n.Get("max_size"); m && m->IsArray() && m->array.size() >= 2) {
                        float t = kUnknown;
                        Dim md = ParseDim(m->array[0]);
                        DimContext mc;
                        mc.parent = pw;
                        if (!md.empty() && !UsesKind(md, Term::Default) && EvalDim(md, mc, t))
                            maxW = t;
                    }
                    if (!Known(wrap) && Known(maxW))
                        wrap = maxW;
                    const float line = LineHeight(scale, ten);
                    int lines = 1;
                    float width = 0;
                    LayoutText(plain, scale, ten, wrap, lines, width);
                    iw = std::ceil(width);
                    ih = static_cast<float>(lines) * line +
                         (lines - 1) * static_cast<float>(n.Num("line_padding", 0));
                    n.textLines = lines;
                    n.textWidth = width;
                    return;
                }
                if (n.type == "image") {
                    int tw = 0, th = 0;
                    const std::string tex = n.Str("texture");
                    if (!tex.empty() && services.textureSize && services.textureSize(tex, tw, th)) {
                        iw = static_cast<float>(tw);
                        ih = static_cast<float>(th);
                        if (const Json* uv = n.Get("uv_size"); uv && uv->IsArray() && uv->array.size() >= 2) {
                            iw = static_cast<float>(uv->array[0].NumberOr(iw));
                            ih = static_cast<float>(uv->array[1].NumberOr(ih));
                        }
                    }
                }
            }

            float FontScale(const LNode& n) const {
                float scale = static_cast<float>(n.Num("font_scale_factor", 1.0));
                const std::string size = n.Str("font_size", "normal");
                if (size == "small")
                    scale *= 0.5f;
                else if (size == "large")
                    scale *= 2.0f;
                else if (size == "extra_large")
                    scale *= 4.0f;
                return scale;
            }

            bool TenLabel(const LNode& n) const {
                if (!IsTenFontType(n.Str("font_type")))
                    return false;
                TenFont::Instance().EnsureLoaded();
                return TenFont::Instance().Available();
            }

            float Measure(const std::string& text, float scale, bool ten) const {
                if (services.textWidth)
                    return services.textWidth(text, scale, ten);
                return 6.0f * static_cast<float>(text.size()) * scale;
            }

            float LineHeight(float scale, bool ten) const {
                if (services.lineHeight)
                    return services.lineHeight(scale, ten);
                return 9.0f * scale;
            }

            void LayoutText(const std::string& text, float scale, bool ten, float wrapWidth, int& lines,
                            float& width) const {
                lines = 1;
                width = 0;
                if (text.empty())
                    return;
                const auto measure = [&](const std::string& s) { return Measure(s, scale, ten); };
                if (!Known(wrapWidth) || wrapWidth <= 0) {
                    size_t start = 0;
                    lines = 0;
                    while (start <= text.size()) {
                        const size_t nl = text.find('\n', start);
                        const std::string line =
                            text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
                        width = std::max(width, measure(line));
                        lines++;
                        if (nl == std::string::npos)
                            break;
                        start = nl + 1;
                    }
                    return;
                }
                lines = 0;
                size_t start = 0;
                while (start <= text.size()) {
                    const size_t nl = text.find('\n', start);
                    const std::string paragraph =
                        text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
                    std::string line;
                    size_t i = 0;
                    bool any = false;
                    while (i <= paragraph.size()) {
                        size_t sp = paragraph.find(' ', i);
                        std::string word =
                            paragraph.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
                        const std::string candidate = line.empty() ? word : line + " " + word;
                        if (!line.empty() && measure(candidate) > wrapWidth) {
                            width = std::max(width, measure(line));
                            lines++;
                            line = word;
                        } else {
                            line = candidate;
                        }
                        any = true;
                        if (sp == std::string::npos)
                            break;
                        i = sp + 1;
                    }
                    (void)any;
                    width = std::max(width, measure(line));
                    lines++;
                    if (nl == std::string::npos)
                        break;
                    start = nl + 1;
                }
                if (lines < 1)
                    lines = 1;
            }

            void ClampSize(const LNode& n, float pw, float ph, float& w, float& h) const {
                auto limit = [&](const char* key, bool isMax) {
                    const Json* v = n.Get(key);
                    if (!v || !v->IsArray() || v->array.size() < 2)
                        return;
                    for (int axis = 0; axis < 2; axis++) {
                        const Json& e = v->array[static_cast<size_t>(axis)];
                        if (e.IsString() && e.string == "default")
                            continue;
                        const Dim d = ParseDim(e);
                        if (d.empty())
                            continue;
                        DimContext c;
                        c.parent = axis == 0 ? pw : ph;
                        c.parentX = pw;
                        c.parentY = ph;
                        c.children = c.childrenMax = c.sibling = kUnknown;
                        float value = 0;
                        if (!EvalDim(d, c, value))
                            continue;

                        if (isMax && value <= 0.0f)
                            continue;
                        float& target = axis == 0 ? w : h;
                        target = isMax ? std::min(target, value) : std::max(target, value);
                    }
                };
                limit("max_size", true);
                limit("min_size", false);
            }

            void MeasureChildren(LNode& n, std::vector<LNode*>& live, float w, float h, bool isStack,
                                 bool vertical, bool isGrid) {
                float fillW = kUnknown, fillH = kUnknown;
                if (isStack) {
                    size_t fillCount = 0;
                    float used = 0;
                    std::vector<LNode*> fillKids;
                    for (LNode* k : live) {
                        Dim kw, kh;
                        SizeSpecs(*k, kw, kh);
                        const Dim& main = vertical ? kh : kw;
                        if (UsesKind(main, Term::Fill)) {
                            fillCount++;
                            fillKids.push_back(k);
                            continue;
                        }
                        Measure(*k, w, h, kUnknown, kUnknown, kUnknown, kUnknown);
                        used += vertical ? k->h : k->w;
                    }
                    if (fillCount > 0) {
                        const float total = vertical ? h : w;
                        const float each = Known(total)
                                               ? std::max(0.0f, total - used) / static_cast<float>(fillCount)
                                               : 0.0f;
                        for (LNode* k : fillKids)
                            Measure(*k, w, h, vertical ? kUnknown : each, vertical ? each : kUnknown,
                                    kUnknown, kUnknown);
                    }
                } else {
                    for (LNode* k : live)
                        Measure(*k, w, h, kUnknown, kUnknown, kUnknown, kUnknown);
                }
                (void)isGrid;
                bool anySibling = false;
                for (LNode* k : live)
                    if (k->needSibW || k->needSibH)
                        anySibling = true;
                if (anySibling) {
                    float maxW = 0, maxH = 0;
                    for (LNode* k : live) {
                        if (!k->needSibW)
                            maxW = std::max(maxW, k->w);
                        if (!k->needSibH)
                            maxH = std::max(maxH, k->h);
                    }
                    for (LNode* k : live)
                        if (k->needSibW || k->needSibH)
                            Measure(*k, w, h, kUnknown, kUnknown, k->needSibW ? maxW : kUnknown,
                                    k->needSibH ? maxH : kUnknown);
                }
            }

            bool KnownAtFirst(const Dim& d, float parent, float fill, float sib, float intrinsic) const {
                DimContext c;
                c.parent = parent;
                c.fill = fill;
                c.sibling = sib;
                c.intrinsic = intrinsic;
                float v = 0;
                return EvalDim(d, c, v);
            }

            void GridShape(const LNode& n, const std::vector<LNode*>& live, float width, float height,
                           int& cols, int& rows) const {
                GridDimensions(n, cols, rows);
                const std::string rescale = n.Str("grid_rescaling_type");
                if ((rescale != "horizontal" && rescale != "vertical") || live.empty())
                    return;
                float itemW = 0, itemH = 0;
                for (const LNode* k : live) {
                    itemW = std::max(itemW, k->w);
                    itemH = std::max(itemH, k->h);
                }
                const int items = static_cast<int>(live.size());
                if (rescale == "horizontal") {
                    if (itemW > 0 && Known(width) && width > 0)
                        cols = std::max(1, static_cast<int>(std::floor((width + 0.01f) / itemW)));
                    rows = (items + cols - 1) / cols;
                } else {
                    if (itemH > 0 && Known(height) && height > 0)
                        rows = std::max(1, static_cast<int>(std::floor((height + 0.01f) / itemH)));
                    cols = (items + rows - 1) / rows;
                }
                cols = std::max(cols, 1);
                rows = std::max(rows, 1);
            }

            void GridExtent(LNode& n, std::vector<LNode*>& live, float availW, float availH, float& w,
                            float& h) const {
                int cols = 1, rows = 1;
                GridShape(n, live, availW, availH, cols, rows);
                std::vector<float> colWidth(static_cast<size_t>(cols), 0.0f),
                    rowHeight(static_cast<size_t>(rows), 0.0f);
                for (size_t i = 0; i < live.size(); i++) {
                    const size_t c = i % static_cast<size_t>(cols), r = i / static_cast<size_t>(cols);
                    if (r >= rowHeight.size())
                        break;
                    colWidth[c] = std::max(colWidth[c], live[i]->w);
                    rowHeight[r] = std::max(rowHeight[r], live[i]->h);
                }
                w = 0;
                h = 0;
                for (float v : colWidth)
                    w += v;
                for (float v : rowHeight)
                    h += v;
            }

            void PlaceGrid(LNode& n, std::vector<LNode*>& live, float ax, float ay) {
                int cols = 1, rows = 1;
                GridShape(n, live, n.w, n.h, cols, rows);
                std::vector<float> colWidth(static_cast<size_t>(cols), 0.0f),
                    rowHeight(static_cast<size_t>(rows), 0.0f);
                for (size_t i = 0; i < live.size(); i++) {
                    const size_t c = i % static_cast<size_t>(cols), r = i / static_cast<size_t>(cols);
                    if (r >= rowHeight.size())
                        break;
                    colWidth[c] = std::max(colWidth[c], live[i]->w);
                    rowHeight[r] = std::max(rowHeight[r], live[i]->h);
                }
                for (size_t i = 0; i < live.size(); i++) {
                    const size_t c = i % static_cast<size_t>(cols), r = i / static_cast<size_t>(cols);
                    float x = 0, y = 0;
                    for (size_t k = 0; k < c; k++)
                        x += colWidth[k];
                    for (size_t k = 0; k < r && k < rowHeight.size(); k++)
                        y += rowHeight[k];
                    float ox = 0, oy = 0;
                    Offset(*live[i], n.w, n.h, ox, oy);
                    live[i]->x = x + ox;
                    live[i]->y = y + oy;
                    Place(*live[i], ax + live[i]->x, ay + live[i]->y);
                }
            }

            static void AnchorFraction(const std::string& name, float& fx, float& fy) {
                fx = fy = 0.5f;
                if (name.empty() || name == "center")
                    return;
                if (name == "top_left") {
                    fx = 0;
                    fy = 0;
                } else if (name == "top_middle") {
                    fx = 0.5f;
                    fy = 0;
                } else if (name == "top_right") {
                    fx = 1;
                    fy = 0;
                } else if (name == "left_middle") {
                    fx = 0;
                    fy = 0.5f;
                } else if (name == "right_middle") {
                    fx = 1;
                    fy = 0.5f;
                } else if (name == "bottom_left") {
                    fx = 0;
                    fy = 1;
                } else if (name == "bottom_middle") {
                    fx = 0.5f;
                    fy = 1;
                } else if (name == "bottom_right") {
                    fx = 1;
                    fy = 1;
                }
            }

            void AnchorPosition(const LNode& k, float pw, float ph, float& x, float& y) const {
                float ffx, ffy, tfx, tfy;
                AnchorFraction(k.Str("anchor_from"), ffx, ffy);
                AnchorFraction(k.Str("anchor_to"), tfx, tfy);
                x = pw * ffx - k.w * tfx;
                y = ph * ffy - k.h * tfy;
            }

            void Offset(const LNode& k, float pw, float ph, float& x, float& y) const {
                x = y = 0;
                const Json* o = k.Get("offset");
                if (o && o->IsArray() && o->array.size() >= 2) {
                    for (int axis = 0; axis < 2; axis++) {
                        const Dim d = ParseDim(o->array[static_cast<size_t>(axis)]);
                        DimContext c;
                        c.parent = axis == 0 ? pw : ph;
                        float v = 0;
                        if (EvalDim(d, c, v))
                            (axis == 0 ? x : y) = v;
                    }
                }
                if (k.Bool("use_anchored_offset", false)) {
                    if (const Json* v = k.bag.Find("#anchored_offset_value_x"); v && v->IsNumber())
                        x += static_cast<float>(v->number);
                    if (const Json* v = k.bag.Find("#anchored_offset_value_y"); v && v->IsNumber())
                        y += static_cast<float>(v->number);
                }
                if (const Json* v = k.bag.Find("#offset"); v && v->IsArray() && v->array.size() >= 2) {
                    x += static_cast<float>(v->array[0].NumberOr(0));
                    y += static_cast<float>(v->array[1].NumberOr(0));
                }
            }
        };
    }

    std::string StripFormatting(const std::string& text) {
        std::string out;
        out.reserve(text.size());
        for (size_t i = 0; i < text.size(); i++) {
            if (static_cast<unsigned char>(text[i]) == 0xC2 && i + 2 < text.size() + 0 &&
                static_cast<unsigned char>(text[i + 1]) == 0xA7) {
                i += 2;
                continue;
            }
            out += text[i];
        }
        return out;
    }

    std::unique_ptr<LNode> EvaluateTree(const Node& root, const UiData& data, const UiServices& services,
                                        float guiW, float guiH) {
        Evaluator evaluator(data, services);
        if (AnimHost* host = services.anim) {
            host->epoch++;
            host->animating = false;
            host->destroyed.clear();
            for (const auto& rec : host->recs)
                if (rec.second.finished && !rec.second.destroyName.empty())
                    host->destroyed.insert(rec.second.destroyName);
        }
        auto tree = evaluator.Instantiate(root, {}, nullptr, 0, 1.0f, false);
        if (AnimHost* host = services.anim)
            for (auto it = host->recs.begin(); it != host->recs.end();)
                it = (it->second.seen != host->epoch && it->second.destroyName.empty()) ? host->recs.erase(it)
                                                                                        : std::next(it);
        if (!tree)
            return nullptr;
        evaluator.Measure(*tree, guiW, guiH, kUnknown, kUnknown, kUnknown, kUnknown);
        tree->w = guiW;
        tree->h = guiH;
        evaluator.Place(*tree, 0, 0);
        return tree;
    }
}
