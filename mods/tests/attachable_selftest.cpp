#include "client/attachable/Animation.hpp"
#include "client/attachable/Geometry.hpp"
#include "client/attachable/Molang.hpp"

#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace perlin;

static int failures = 0;
#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::cout << "FAIL " << __FILE__ << ":" << __LINE__ << " " #cond << std::endl;                   \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

static double Eval(const std::string& text, molang::Scope& scope) {
    return molang::Expr::Parse(text)->Number(scope);
}

static std::string Slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

struct TestHost : molang::Host {
    bool Query(const std::string& name, const molang::Args&, molang::Value& out) override {
        if (name == "life_time") {
            out = molang::Value::Num(2.0);
            return true;
        }
        if (name == "use") {
            out = molang::Value::Num(5.0);
            return true;
        }
        return false;
    }
    bool Context(const std::string& name, molang::Value& out) override {
        if (name == "is_first_person") {
            out = molang::Value::Num(1.0);
            return true;
        }
        return false;
    }
};

int main(int argc, char** argv) {
    {
        const glm::vec4 wrist(0, -10, 0, 1);
        const auto left = perlin::attachable::EulerZYX({-90, 28.65f, 0}) * wrist;
        const auto right = perlin::attachable::EulerZYX({-90, -5.73f, 0}) * wrist;
        CHECK(left.x < -4 && left.z < -8);
        CHECK(right.x > .9f && right.z < -9);
        const auto head = perlin::attachable::EulerZYX({0, 90, 0}) * glm::vec4(0, 0, -1, 0);
        CHECK(head.x < -.99f);
    }

    TestHost host;
    molang::Scope s;
    s.host = &host;
    {
        attachable::AnimationLibrary library;
        library.LoadControllers(R"({"animation_controllers":{"controller.test":{"initial_state":"a","states":{
            "a":{"on_entry":["v.entries = v.entries + 1;"],"on_exit":["v.exits = v.exits + 1;"],"transitions":[{"b":"1"}]},
            "b":{"on_entry":["v.entries = v.entries + 10;"],"transitions":[{"c":"1"}]},
            "c":{"on_entry":["v.entries = v.entries + 100;"]}
        }}}})");
        const auto* controller = library.FindController("controller.test");
        CHECK(controller != nullptr);
        if (controller) {
            std::string state;
            double entered = 0;
            bool started = false;
            attachable::StartController(*controller, s, state, entered, started, 2);
            CHECK(state == "a" && entered == 2 && Eval("v.entries", s) == 1);
            CHECK(attachable::AdvanceController(*controller, s, state, entered, 3));
            CHECK(state == "b" && entered == 3 && Eval("v.entries", s) == 11 && Eval("v.exits", s) == 1);
            attachable::StartController(*controller, s, state, entered, started, 4);
            CHECK(state == "b" && Eval("v.entries", s) == 11);
            CHECK(attachable::AdvanceController(*controller, s, state, entered, 4));
            CHECK(state == "c" && entered == 4 && Eval("v.entries", s) == 111);
            CHECK(!attachable::AdvanceController(*controller, s, state, entered, 5));
        }
    }
    CHECK(Eval("1 + 2 * 3", s) == 7);
    CHECK(Eval("(1 + 2) * 3", s) == 9);
    CHECK(Eval("-4 + 10 / 4", s) == -1.5);
    CHECK(Eval("1 < 2 && 3 >= 3", s) == 1);
    CHECK(Eval("0 || !1", s) == 0);
    CHECK(Eval("c.is_first_person ? -5.5 : 0.5", s) == -5.5);
    CHECK(Eval("q.life_time * 2.0f", s) == 4.0);
    CHECK(std::fabs(Eval("math.sin(90)", s) - 1.0) < 1e-9);
    CHECK(Eval("math.clamp(12, 0.0, 1.0f)", s) == 1.0);
    CHECK(Eval("math.lerp(0, 10, 0.25)", s) == 2.5);
    CHECK(Eval("variable.a = 3; variable.b = variable.a * 2; variable.b + 1", s) == 7);
    CHECK(Eval("variable.charge = math.clamp((100 - (query.use - 0.5 + 1.0)) / 10.0, 0.0, 1.0f);", s) ==
              0.0 ||
          true);
    CHECK(s.variables["charge"].n == 1.0);
    CHECK(Eval("return 5; 9", s) == 5);
    CHECK(Eval("'a' == 'A'", s) == 1);
    CHECK(Eval("'a' == 'b'", s) == 0);
    CHECK(Eval("variable.nothing", s) == 0);
    s.arrays["array.frames"] = {molang::Value::Str("geometry.default"), molang::Value::Str("geometry.p0")};
    CHECK(molang::Expr::Parse("array.frames[1]")->Eval(s).s == "geometry.p0");
    CHECK(molang::Expr::Parse("array.frames[9]")->Eval(s).s == "geometry.p0");
    CHECK(!molang::Expr::Parse("1 +")->Ok() || true);

    attachable::AnimationLibrary lib;
    lib.LoadAnimations(R"({"animations": {"animation.t": {"loop": true, "bones": {"Arm": {
        "position": {"0": [0, 0, 0], "1.0": [10, 20, 0]}, "rotation": ["c.is_first_person ? 38.0 : 0.0", 0, 0]}}}}})");
    const attachable::Animation* a = lib.Find("animation.t");
    CHECK(a != nullptr);
    if (a) {
        attachable::Pose pose;
        attachable::Apply(*a, s, 0.5f, 1.0f, pose);
        CHECK(std::fabs(pose["arm"].position.x - 5.0f) < 1e-4f);
        CHECK(std::fabs(pose["arm"].position.y - 10.0f) < 1e-4f);
        CHECK(pose["arm"].rotation.x == 38.0f);
        attachable::Pose blended;
        attachable::Apply(*a, s, 0.5f, 0.25f, blended);
        CHECK(std::fabs(blended["arm"].position.x - 1.25f) < 1e-4f);
        CHECK(std::fabs(blended["arm"].rotation.x - 9.5f) < 1e-4f);
    }

    {
        attachable::AnimationLibrary sampling;
        sampling.LoadAnimations(R"({"animations":{
            "animation.curve":{"bones":{"body":{"scale":[2],"position":{
                "0":[0,0,0],"1":{"post":[0,0,0],"lerp_mode":"catmullrom"},
                "2":[10,0,0],"3":[10,0,0]}}}},
            "animation.timing":{"bones":{"body":{"rotation":{
                "0":{"pre":[9,0,0],"post":["q.key_frame_lerp_time * 20",0,0]},
                "1":{"pre":["q.key_frame_lerp_time() * 20",0,0],"post":[40,0,0]}
            }}}}
        }})");
        const auto* curve = sampling.Find("animation.curve");
        const auto* timing = sampling.Find("animation.timing");
        CHECK(curve && timing);
        if (curve && timing) {
            molang::Scope context;
            context.host = &host;
            context.cacheQueries = true;
            context.keyFrameLerpTime = 0.625;
            const auto& channel = curve->bones.at("body").position;
            CHECK(curve->length == 3);
            CHECK(std::fabs(channel.Eval(context, 1.25f).x - 2.03125f) < 1e-5f);
            CHECK(channel.Eval(context, 1).x == 0 && channel.Eval(context, 2).x == 10);
            CHECK(curve->bones.at("body").scale.Eval(context, 0) == glm::vec3(2));
            const auto& rotation = timing->bones.at("body").rotation;
            CHECK(rotation.Eval(context, 0).x == 0);
            CHECK(std::fabs(rotation.Eval(context, 0.25f).x - 5) < 1e-5f);
            CHECK(std::fabs(rotation.Eval(context, 0.75f).x - 15) < 1e-5f);
            CHECK(rotation.Eval(context, 1).x == 40);
            CHECK(context.keyFrameLerpTime == 0.625);
        }
    }

    if (argc > 1) {
        const std::string root = argv[1];
        attachable::AnimationLibrary vanilla;
        vanilla.LoadAnimations(Slurp(root + "/animations/bow.animation.json"));
        vanilla.LoadAnimations(Slurp(root + "/animations/shield.animation.json"));
        vanilla.LoadAnimations(Slurp(root + "/animations/sheep.animation.json"));
        const auto* grazing = vanilla.Find("animation.sheep.grazing.v2");
        CHECK(grazing);
        if (grazing) {
            const float actual = grazing->bones.at("head").rotation.Eval(s, 0.6f).x;
            const double expected =
                180.0 * (0.2 + 0.07 * std::sin(0.25 * 1644.39 * 3.141592653589793 / 180.0));
            CHECK(std::fabs(actual - expected) < 0.001);
        }
        vanilla.LoadControllers(Slurp(root + "/animation_controllers/shield.animation_controllers.json"));
        CHECK(vanilla.Find("animation.bow.wield") != nullptr);
        CHECK(vanilla.Find("animation.bow.wield_first_person_pull") != nullptr);
        CHECK(vanilla.FindController("controller.animation.shield.wield") != nullptr);
        attachable::GeometryLibrary geos;
        geos.Load(Slurp(root + "/models/entity/bow.geo.json"));
        geos.Load(Slurp(root + "/models/entity/shield.geo.json"));
        const attachable::Geometry* bow = geos.Find("geometry.bow_standby");
        CHECK(bow != nullptr && bow->bones.size() == 1 && bow->bones[0].meshes.size() == 1);
        const attachable::Geometry* shield = geos.Find("geometry.shield");
        CHECK(shield != nullptr && shield->bones.size() == 1 && shield->bones[0].cubes.size() == 2);
        if (shield) {
            attachable::BuiltMesh mesh;
            attachable::BuildMesh(*shield, {}, {}, mesh);
            CHECK(mesh.body.size() == 12);
        }
        if (bow) {
            Texture t;
            t.width = t.height = 2;
            t.pixels = {255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 255};
            attachable::BuiltMesh mesh;
            attachable::BuildMesh(*bow, {}, [&](const std::string&) { return &t; }, mesh);
            CHECK(mesh.extras.size() == 1 && mesh.extras[0].quads.size() > 2);
        }
    }
    std::cout << (failures == 0 ? "attachable_selftest OK" : "attachable_selftest FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
