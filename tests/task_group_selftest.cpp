#include "util/TaskGroup.hpp"

#include <chrono>
#include <cstdio>

int main() {
    using namespace std::chrono_literals;
    int failures = 0;
    const auto check = [&](bool ok, const char* what) {
        if (!ok) {
            std::fprintf(stderr, "FAIL: %s\n", what);
            failures++;
        }
    };

    std::atomic<int> finished{0};
    std::atomic<int> stopped{0};
    {
        perlin::TaskGroup group;
        for (int i = 0; i < 64; i++)
            check(group.Run([&] { finished++; }), "tasks start while the group is open");
        check(group.Run([&](std::stop_token stop) {
            while (!stop.stop_requested())
                std::this_thread::sleep_for(1ms);
            stopped++;
        }), "a stop-aware task starts");
        std::this_thread::sleep_for(50ms);
        check(group.Active() == 1, "finished tasks are reaped, the waiting one stays");
        group.Shutdown();
        check(stopped.load() == 1, "shutdown stops and joins a waiting task");
        check(!group.Run([&] { finished++; }), "a closed group refuses new work");
    }
    check(finished.load() == 64, "every accepted task ran exactly once");

    {
        perlin::TaskGroup group;
        group.Run([&](std::stop_token stop) {
            while (!stop.stop_requested())
                std::this_thread::sleep_for(1ms);
            stopped++;
        });
    }
    check(stopped.load() == 2, "the destructor stops and joins");

    if (failures)
        return 1;
    std::puts("task_group_selftest ok");
    return 0;
}
