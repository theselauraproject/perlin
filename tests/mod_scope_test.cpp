#include "client/systems/mods/ModManager.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

using namespace perlin::mods;
namespace abi = perlin::abi;

static int failures = 0;
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                          \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

static abi::Status Invoke(ModManager& m, abi::Op op) {
    CallContext ctx;
    ctx.permissions = ~0u;
    abi::Reader reader(nullptr, 0);
    abi::Writer writer;
    return m.InvokeOp(op, ctx, reader, writer);
}

namespace {
    struct Owner {
        std::atomic<int> magic{1};
        std::atomic<int> calls{0};
        std::atomic<int> violations{0};
    };
}

static void RegistrationLifecycle() {
    ModManager m;
    CHECK(!m.HasOp(abi::Op::Clock));
    Owner owner;
    {
        OpScope scope(m);
        scope.Register(abi::Op::Clock, [&owner](CallContext&, abi::Reader&, abi::Writer&) {
            owner.calls++;
            return abi::Status::Ok;
        });
        CHECK(m.HasOp(abi::Op::Clock));
        CHECK(Invoke(m, abi::Op::Clock) == abi::Status::Ok);
        CHECK(owner.calls == 1);
    }
    CHECK(!m.HasOp(abi::Op::Clock));
    CHECK(Invoke(m, abi::Op::Clock) == abi::Status::UnknownOp);
    CHECK(owner.calls == 1);
}

static void ScopesAreIndependent() {
    ModManager m;
    int unscoped = 0;
    m.Register(abi::Op::Log, [&unscoped](CallContext&, abi::Reader&, abi::Writer&) {
        unscoped++;
        return abi::Status::Ok;
    });
    auto a = std::make_unique<OpScope>(m);
    OpScope b(m);
    a->Register(abi::Op::Clock, [](CallContext&, abi::Reader&, abi::Writer&) { return abi::Status::Ok; });
    b.Register(abi::Op::ApiInfo, [](CallContext&, abi::Reader&, abi::Writer&) { return abi::Status::Ok; });
    a.reset();
    CHECK(!m.HasOp(abi::Op::Clock));
    CHECK(m.HasOp(abi::Op::ApiInfo));
    CHECK(m.HasOp(abi::Op::Log));
    CHECK(Invoke(m, abi::Op::Log) == abi::Status::Ok);
    CHECK(unscoped == 1);
}

static void ReplacementSurvivesOldScopeClose() {
    ModManager m;
    int which = 0;
    auto first = std::make_unique<OpScope>(m);
    first->Register(abi::Op::Clock, [&which](CallContext&, abi::Reader&, abi::Writer&) {
        which = 1;
        return abi::Status::Ok;
    });
    OpScope second(m);
    second.Register(abi::Op::Clock, [&which](CallContext&, abi::Reader&, abi::Writer&) {
        which = 2;
        return abi::Status::Ok;
    });
    first.reset();
    CHECK(m.HasOp(abi::Op::Clock));
    CHECK(Invoke(m, abi::Op::Clock) == abi::Status::Ok);
    CHECK(which == 2);
}

static void QueuedTasksDropWithScope() {
    ModManager m;
    int ran = 0, kept = 0;
    {
        OpScope scope(m);
        scope.PostMain([&ran] { ran++; });
        m.PostMain([&kept] { kept++; });
    }
    m.Pump();
    CHECK(ran == 0);
    CHECK(kept == 1);

    OpScope live(m);
    live.PostMain([&ran] { ran++; });
    m.Pump();
    CHECK(ran == 1);
}

static void PostAfterCloseIsRejected() {
    ModManager m;
    int ran = 0;
    ScopeId id;
    {
        OpScope scope(m);
        id = scope.Id();
    }
    m.PostMain([&ran] { ran++; }, id);
    m.Pump();
    CHECK(ran == 0);
}

static void ClosedBetweenDrainAndRun() {
    ModManager m;
    int ran = 0;
    auto scope = std::make_unique<OpScope>(m);
    OpScope* raw = scope.get();
    m.PostMain([&scope] { scope.reset(); });
    raw->PostMain([&ran] { ran++; });
    m.PostMain([&ran] { ran += 10; });
    m.Pump();
    CHECK(ran == 10);
}

static void ImageLoaderReleasedWithScope() {
    ModManager m;
    bool called = false;
    {
        OpScope scope(m);
        scope.SetImageLoader([&called](const std::string&, int&, int&, std::vector<uint8_t>&) {
            called = true;
            return false;
        });
    }
    CHECK(!called);
}

static void CloseWaitsForInFlightHandlers() {
    ModManager m;
    for (int round = 0; round < 40; round++) {
        Owner owner;
        auto scope = std::make_unique<OpScope>(m);
        std::atomic<bool> entered{false};
        scope->Register(abi::Op::Clock, [&owner, &entered](CallContext&, abi::Reader&, abi::Writer&) {
            entered = true;
            if (owner.magic.load() != 1)
                owner.violations++;
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
            if (owner.magic.load() != 1)
                owner.violations++;
            owner.calls++;
            return abi::Status::Ok;
        });
        std::atomic<bool> stop{false};
        std::thread caller([&] {
            while (!stop.load())
                Invoke(m, abi::Op::Clock);
        });
        while (!entered.load())
            std::this_thread::yield();
        scope.reset();
        owner.magic = 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        stop = true;
        caller.join();
        CHECK(owner.violations == 0);
        CHECK(owner.calls >= 1);
    }
}

static void ConcurrentChurn() {
    ModManager m;
    std::atomic<bool> stop{false};
    std::atomic<int> violations{0};
    std::vector<std::thread> callers;
    for (int t = 0; t < 4; t++)
        callers.emplace_back([&] {
            while (!stop.load())
                Invoke(m, abi::Op::Clock);
        });
    for (int i = 0; i < 400; i++) {
        Owner owner;
        {
            OpScope scope(m);
            scope.Register(abi::Op::Clock, [&owner, &violations](CallContext&, abi::Reader&, abi::Writer&) {
                if (owner.magic.load() != 1)
                    violations++;
                return abi::Status::Ok;
            });
            scope.PostMain([&owner, &violations] {
                if (owner.magic.load() != 1)
                    violations++;
            });
            std::this_thread::yield();
        }
        owner.magic = 0;
        m.Pump();
    }
    stop = true;
    for (auto& t : callers)
        t.join();
    CHECK(violations == 0);
}

int main() {
    RegistrationLifecycle();
    ScopesAreIndependent();
    ReplacementSurvivesOldScopeClose();
    QueuedTasksDropWithScope();
    PostAfterCloseIsRejected();
    ClosedBetweenDrainAndRun();
    ImageLoaderReleasedWithScope();
    CloseWaitsForInFlightHandlers();
    ConcurrentChurn();
    std::printf(failures ? "mod_scope_test FAILED\n" : "mod_scope_test ok\n");
    return failures ? 1 : 0;
}
