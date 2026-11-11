
#include "client/player/PlayerPhysics.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace perlin::physics;

namespace {

    struct Facts {
        double friction = 0.6, hs = 1, vs = 1, fluidH = 0;
        int flags = 0, surface = 0;
    };
    struct Region {
        int mn[3], mx[3];
        Facts facts;
    };

    struct TraceWorld : World {
        Facts base;
        std::vector<Box> boxes;
        std::vector<Region> regions;

        BlockShape Query(int x, int y, int z) const override {
            Facts f = base;
            for (const Region& r : regions)
                if (x >= r.mn[0] && x < r.mx[0] && y >= r.mn[1] && y < r.mx[1] && z >= r.mn[2] && z < r.mx[2])
                    f = r.facts;
            BlockShape s;
            s.friction = static_cast<float>(f.friction);
            s.surface = static_cast<uint8_t>(f.surface);
            if (f.flags & 2) {
                s.fluid = 1;
                s.fluidLevel = static_cast<float>(f.fluidH);
            } else if (f.flags & 4) {
                s.fluid = 2;
                s.fluidLevel = static_cast<float>(f.fluidH);
            }
            s.climbable = (f.flags & 1) != 0;
            s.web = (f.flags & 8) != 0;
            s.speedFactor = static_cast<float>(f.hs);
            const Box cell{double(x), double(y), double(z), x + 1.0, y + 1.0, z + 1.0};
            for (const Box& b : boxes)
                if (b.Intersects(cell))
                    s.boxes.push_back(b.Offset(-x, -y, -z));
            return s;
        }
    };

    Facts ReadFacts(std::istringstream& in) {
        Facts f;
        in >> f.friction >> f.hs >> f.vs >> f.fluidH >> f.flags >> f.surface;
        return f;
    }

    struct Expect {
        double x, y, z, vx, vy, vz;
        int og, ch, cv;
        bool hasBox = false;
        Box box;
    };

    struct FieldStat {
        double maxErr = 0;
        int maxTick = -1;
        int fails = 0;
    };

    constexpr const char* kFields[] = {"x", "y", "z", "vx", "vy", "vz", "onGround", "collideH", "collideV"};
    constexpr int kNumFields = 9;

    struct ScenarioReport {
        std::string name;
        int ticks = 0, failedTicks = 0;
        int firstFailTick = -1;
        std::string firstFail;
        FieldStat field[kNumFields];
    };

    struct Options {
        std::string path;
        bool resync = false, sprintFromActor = true, sneakFromActor = true, exact = false, useSpeed = false,
             verbose = false;
        double eps = 1e-5;
        std::string only, csv;
    };

    int Usage() {
        std::fprintf(stderr, "usage: trace_diff <trace.txt> [--resync] [--sprint-from actor|input] "
                             "[--sneak-from actor|input]\n"
                             "                  [--use-speed] [--exact]\n"
                             "                  [--eps E] [--only SUBSTR] [--csv FILE] [--verbose]\n");
        return 2;
    }

}

int main(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        if (a == "--resync")
            opt.resync = true;
        else if (a == "--exact")
            opt.exact = true;
        else if (a == "--use-speed")
            opt.useSpeed = true;
        else if (a == "--verbose")
            opt.verbose = true;
        else if (a == "--sprint-from") {
            const char* v = next();
            if (!v)
                return Usage();
            opt.sprintFromActor = std::strcmp(v, "input") != 0;
        } else if (a == "--sneak-from") {
            const char* v = next();
            if (!v)
                return Usage();
            opt.sneakFromActor = std::strcmp(v, "input") != 0;
        } else if (a == "--eps") {
            const char* v = next();
            if (!v)
                return Usage();
            opt.eps = std::atof(v);
        } else if (a == "--only") {
            const char* v = next();
            if (!v)
                return Usage();
            opt.only = v;
        } else if (a == "--csv") {
            const char* v = next();
            if (!v)
                return Usage();
            opt.csv = v;
        } else if (!a.empty() && a[0] == '-')
            return Usage();
        else
            opt.path = a;
    }
    if (opt.path.empty())
        return Usage();

    std::ifstream file(opt.path);
    if (!file) {
        std::fprintf(stderr, "cannot open %s\n", opt.path.c_str());
        return 2;
    }
    std::FILE* csv = nullptr;
    if (!opt.csv.empty()) {
        csv = std::fopen(opt.csv.c_str(), "w");
        if (!csv) {
            std::fprintf(stderr, "cannot write %s\n", opt.csv.c_str());
            return 2;
        }
        std::fprintf(csv, "scenario,tick,fwd,strafe,yaw,jump,sprint,sneak,"
                          "v_x,v_y,v_z,v_vx,v_vy,v_vz,v_og,"
                          "p_x,p_y,p_z,p_vx,p_vy,p_vz,p_og,"
                          "d_x,d_y,d_z,d_vx,d_vy,d_vz\n");
    }

    std::vector<ScenarioReport> reports;
    ScenarioReport* rep = nullptr;
    bool skipping = false;
    State state;
    TraceWorld world;
    Attributes attr;
    attr.currentMovementMath = false;
    Input input;
    Expect prev{};
    bool havePrev = false;
    std::string lastInput, line;
    int lineNo = 0;

    while (std::getline(file, line)) {
        ++lineNo;
        std::istringstream in(line);
        std::string tag;
        if (!(in >> tag) || tag[0] == '#')
            continue;

        if (tag == "S") {
            std::string name;
            int og = 0, steps = 0, cx = 0, cy = 0, cz = 0;
            state = State{};
            in >> name >> state.x >> state.y >> state.z >> state.vx >> state.vy >> state.vz >> og >>
                state.jumpDelay >> steps >> cx >> cy >> cz;
            float yaw = 0, pitch = 0;
            Box bb;
            if (in >> yaw) {
                state.yaw = yaw;
                if (in >> pitch >> bb.x0 >> bb.y0 >> bb.z0 >> bb.x1 >> bb.y1 >> bb.z1) {
                    state.box = bb;
                    state.hasBox = true;
                }
            }
            state.onGround = og != 0;
            state.horizontalCollision = cx || cz;
            state.verticalCollision = cy != 0;
            skipping = !opt.only.empty() && !name.contains(opt.only);
            rep = nullptr;
            havePrev = false;
            if (!skipping) {
                reports.push_back({});
                rep = &reports.back();
                rep->name = name;
            }
        } else if (tag == "W") {
            world = TraceWorld{};
            world.base = ReadFacts(in);
            int nb = 0, nr = 0, unloaded = 0;
            in >> nb >> nr >> unloaded;
            if (unloaded && rep)
                std::fprintf(stderr, "warning: %s has unloaded chunks in its world window\n",
                             rep->name.c_str());
        } else if (tag == "B") {
            Box b;
            in >> b.x0 >> b.y0 >> b.z0 >> b.x1 >> b.y1 >> b.z1;
            world.boxes.push_back(b);
        } else if (tag == "R") {
            Region r;
            in >> r.mn[0] >> r.mn[1] >> r.mn[2] >> r.mx[0] >> r.mx[1] >> r.mx[2];
            r.facts = ReadFacts(in);
            world.regions.push_back(r);
        } else if (tag == "A") {
            int jb = 0, lv = 0, sf = 0;
            float speed = 0.1f, jumpStrength = 0.42f;
            in >> jb >> lv >> sf;
            if (in >> speed >> jumpStrength) {
                if (opt.useSpeed)
                    attr.speed = speed;
            }
            attr.jumpBoost = jb;
            attr.levitation = lv;
            attr.slowFalling = sf != 0;
        } else if (tag == "K") {
        } else if (tag == "I") {
            if (skipping)
                continue;
            double fwd = 0, strafe = 0, yaw = 0, pitch = 0;
            int jump = 0, jumpPressed = 0, sprint = 0, sneak = 0, actorSprint = -1, actorSneak = -1,
                forced = 0;
            int flying = 0, gliding = 0, swimming = 0, headInWater = 0, creative = 0;
            in >> fwd >> strafe >> yaw >> jump >> jumpPressed >> sprint >> sneak;
            if (in >> pitch >> actorSprint >> actorSneak)
                if (in >> forced)
                    in >> flying >> gliding >> swimming >> headInWater >> creative;
            lastInput = line;
            input = Input{};
            input.jump = jump != 0;
            input.sprint = (opt.sprintFromActor && actorSprint >= 0) ? actorSprint != 0 : sprint != 0;
            input.sneak = (opt.sneakFromActor && actorSneak >= 0) ? actorSneak != 0 : sneak != 0;
            if (actorSprint >= 0) {
                input.forward = static_cast<float>(fwd);
                input.strafe = static_cast<float>(-strafe);
            } else {
                const MoveVector mv = ProcessMoveInput(static_cast<float>(fwd), static_cast<float>(-strafe),
                                                       sneak != 0, state.inWater);
                input.forward = mv.forward;
                input.strafe = mv.strafe;
            }
            input.flying = flying != 0;
            input.creativeFlight = creative != 0;
            input.flyUp = input.flying && input.jump;
            input.flyDown = input.flying && sneak != 0;
            input.gliding = gliding != 0;
            input.swimming = swimming != 0 && !input.gliding;
            input.headInWater = headInWater != 0;
            input.pitch = static_cast<float>(pitch);
            input.lookY = std::sin(static_cast<float>(-pitch) * 3.14159265f / 180.0f);
            if (input.flying)
                input.sneak = false;
            state.yaw = static_cast<float>(yaw);
            if (opt.resync && havePrev) {
                state.x = prev.x;
                state.y = prev.y;
                state.z = prev.z;
                state.vx = prev.vx;
                state.vy = prev.vy;
                state.vz = prev.vz;
                state.onGround = prev.og != 0;
                state.horizontalCollision = prev.ch != 0;
                state.verticalCollision = prev.cv != 0;
                state.hasBox = prev.hasBox;
                if (prev.hasBox)
                    state.box = prev.box;
            }
            Attributes resolvedAttributes = attr;
            if (!opt.useSpeed)
                resolvedAttributes.speed *= input.sprint ? 1.3f : 1.0f;
            Step(state, input, resolvedAttributes, world);
        } else if (tag == "E") {
            if (skipping || !rep)
                continue;
            Expect e{};
            int cz = 0;
            in >> e.x >> e.y >> e.z >> e.vx >> e.vy >> e.vz >> e.og >> e.ch >> e.cv >> cz;
            e.ch = e.ch || cz;
            int inWater = 0;
            if (in >> inWater >> e.box.x0 >> e.box.y0 >> e.box.z0 >> e.box.x1 >> e.box.y1 >> e.box.z1)
                e.hasBox = true;
            const double got[kNumFields] = {state.x,
                                            state.y,
                                            state.z,
                                            state.vx,
                                            state.vy,
                                            state.vz,
                                            double(state.onGround),
                                            double(state.horizontalCollision),
                                            double(state.verticalCollision)};
            const double want[kNumFields] = {e.x,  e.y,          e.z,          e.vx,        e.vy,
                                             e.vz, double(e.og), double(e.ch), double(e.cv)};
            const int tick = rep->ticks++;
            bool tickBad = false;
            for (int f = 0; f < kNumFields; f++) {
                const double err = std::fabs(got[f] - want[f]);
                double tol = f < 6 ? opt.eps : 0.5;
                bool oneUlp = false;
                if (f < 3 && !opt.exact) {
                    const float g = static_cast<float>(got[f]), w = static_cast<float>(want[f]);
                    oneUlp = g == w || std::nextafter(w, g) == g;
                }
                FieldStat& st = rep->field[f];
                if (err > st.maxErr) {
                    st.maxErr = err;
                    st.maxTick = tick;
                }
                if (err > tol && !oneUlp) {
                    st.fails++;
                    if (!tickBad && rep->firstFailTick < 0) {
                        char buf[512];
                        std::snprintf(buf, sizeof(buf), "%s perlin %.9f vanilla %.9f (d %+.3e) | input: %s",
                                      kFields[f], got[f], want[f], got[f] - want[f], lastInput.c_str());
                        rep->firstFailTick = tick;
                        rep->firstFail = buf;
                    }
                    if (opt.verbose)
                        std::printf("  %s t%-4d %-8s perlin %.9f vanilla %.9f (d %+.3e)\n", rep->name.c_str(),
                                    tick, kFields[f], got[f], want[f], got[f] - want[f]);
                    tickBad = true;
                }
            }
            if (tickBad)
                rep->failedTicks++;
            if (csv) {
                std::fprintf(csv, "%s,%d,%g,%g,%g,%d,%d,%d,", rep->name.c_str(), tick, input.forward,
                             -input.strafe, state.yaw, input.jump, input.sprint, input.sneak);
                std::fprintf(csv, "%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%d,", e.x, e.y, e.z, e.vx, e.vy, e.vz, e.og);
                std::fprintf(csv, "%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%d,", state.x, state.y, state.z, state.vx,
                             state.vy, state.vz, int(state.onGround));
                std::fprintf(csv, "%.3e,%.3e,%.3e,%.3e,%.3e,%.3e\n", state.x - e.x, state.y - e.y,
                             state.z - e.z, state.vx - e.vx, state.vy - e.vy, state.vz - e.vz);
            }
            prev = e;
            havePrev = true;
        } else {
            std::fprintf(stderr, "line %d: unknown tag '%s' (ignored)\n", lineNo, tag.c_str());
        }
    }
    if (csv)
        std::fclose(csv);

    int totalTicks = 0, totalFailed = 0, badScenarios = 0;
    std::map<std::string, int> firstFieldCount;
    for (const auto& r : reports) {
        totalTicks += r.ticks;
        totalFailed += r.failedTicks;
        if (r.failedTicks == 0)
            continue;
        badScenarios++;
        firstFieldCount[r.firstFail.substr(0, r.firstFail.find(' '))]++;
        std::printf("FAIL %-28s %4d/%-4d ticks bad, first at t%d: %s\n", r.name.c_str(), r.failedTicks,
                    r.ticks, r.firstFailTick, r.firstFail.c_str());
        std::printf("     max err:");
        for (int f = 0; f < kNumFields; f++)
            if (r.field[f].fails)
                std::printf(" %s=%.2e@t%d", kFields[f], r.field[f].maxErr, r.field[f].maxTick);
        std::printf("\n");
    }
    std::printf("\n%zu scenarios, %d ticks checked%s: %d ticks diverged in %d scenarios\n", reports.size(),
                totalTicks, opt.resync ? " (resync)" : "", totalFailed, badScenarios);
    if (!firstFieldCount.empty()) {
        std::printf("first diverging field:");
        for (const auto& [f, n] : firstFieldCount)
            std::printf(" %s x%d", f.c_str(), n);
        std::printf("\n");
    }
    if (totalTicks == 0) {
        std::fprintf(stderr, "no ticks checked (wrong file or --only filter?)\n");
        return 2;
    }
    return totalFailed == 0 ? 0 : 1;
}
