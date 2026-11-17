#pragma once
#include <cstdio>
#include <string>

namespace perlin::selftest {
    class Checker {
    public:
        explicit Checker(const char* suite) : suite(suite) {}

        void Check(bool ok, const char* what) {
            total++;
            if (!ok) {
                std::fprintf(stderr, "FAIL [%s]: %s\n", suite, what);
                failures++;
            }
        }

        template <class A, class B> void Equal(const A& actual, const B& expected, const char* what) {
            Check(actual == expected, what);
        }

        int Finish() const {
            if (failures == 0)
                std::printf("%s: ok (%d checks)\n", suite, total);
            else
                std::printf("%s: %d of %d checks failed\n", suite, failures, total);
            return failures == 0 ? 0 : 1;
        }

    private:
        const char* suite;
        int total = 0;
        int failures = 0;
    };
}
