#include "client/SampleProfiler.hpp"

#if defined(__linux__) && !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <execinfo.h>
#include <link.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace perlin {
    namespace {
        constexpr int kDepth = 24;
        constexpr int kMax = 40000;
        void* gStacks[kMax][kDepth];
        int gDepths[kMax];
        volatile int gCount = 0;
        timer_t gTimer;
        bool gStarted = false;
        double gSince = 0;

        double Now() {
            timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
        }

        void OnSignal(int) {
            const int i = gCount;
            if (i >= kMax || Now() < gSince)
                return;
            gDepths[i] = backtrace(gStacks[i], kDepth);
            gCount = i + 1;
        }

        int Base(struct dl_phdr_info* info, size_t, void* data) {
            if (info->dlpi_name == nullptr || info->dlpi_name[0] == 0) {
                *static_cast<unsigned long*>(data) = info->dlpi_addr;
                return 1;
            }
            return 0;
        }
    }

    void StartSampleProfiler() {
        const char* path = std::getenv("PERLIN_SAMPLE");
        if (!path || gStarted)
            return;
        gStarted = true;
        gSince = Now() +
                 (std::getenv("PERLIN_SAMPLE_DELAY") ? std::atof(std::getenv("PERLIN_SAMPLE_DELAY")) : 0.0);
        void* warm[4];
        backtrace(warm, 4);
        std::signal(SIGUSR2, OnSignal);
        struct sigevent sev {};
        sev.sigev_notify = SIGEV_THREAD_ID;
        sev.sigev_signo = SIGUSR2;
        sev._sigev_un._tid = static_cast<int>(syscall(SYS_gettid));
        clockid_t clock;
        pthread_getcpuclockid(pthread_self(), &clock);
        // PERLIN_SAMPLE_WALL: sample by wall time, so time spent waiting shows up too
        if (std::getenv("PERLIN_SAMPLE_WALL"))
            clock = CLOCK_MONOTONIC;
        if (timer_create(clock, &sev, &gTimer) != 0)
            return;
        struct itimerspec spec {};
        spec.it_interval.tv_nsec = 1000000;
        spec.it_value.tv_nsec = 1000000;
        timer_settime(gTimer, 0, &spec, nullptr);
        // PERLIN_SAMPLE_SECONDS: write the samples that long after sampling starts, without waiting for the exit
        if (const char* secs = std::getenv("PERLIN_SAMPLE_SECONDS")) {
            static double stopAfter = 0;
            stopAfter = std::atof(secs) + (gSince - Now());
            pthread_t t;
            pthread_create(
                &t, nullptr,
                [](void*) -> void* {
                    timespec ts{static_cast<time_t>(stopAfter), 0};
                    nanosleep(&ts, nullptr);
                    StopSampleProfiler();
                    return nullptr;
                },
                nullptr);
            pthread_detach(t);
        }
    }

    void StopSampleProfiler() {
        const char* path = std::getenv("PERLIN_SAMPLE");
        if (!path || !gStarted)
            return;
        gStarted = false;
        struct itimerspec spec {};
        timer_settime(gTimer, 0, &spec, nullptr);
        unsigned long base = 0;
        dl_iterate_phdr(Base, &base);
        if (FILE* f = std::fopen(path, "w")) {
            for (int i = 0; i < gCount; i++) {
                for (int d = 0; d < gDepths[i]; d++)
                    std::fprintf(f, "%lx ", reinterpret_cast<unsigned long>(gStacks[i][d]) - base);
                std::fprintf(f, "\n");
            }
            std::fclose(f);
        }
    }
}
#else
namespace perlin {
    void StartSampleProfiler() {}
    void StopSampleProfiler() {}
}
#endif
