#pragma once

namespace perlin::meta::detail {
    template <size_t N, class T> constexpr auto bind_fields(T&& t) {
        static_assert(
            N <= 64,
            "aggregate has too many members for perlin::meta (raise the limit in detail/field_binding.hpp)");
        if constexpr (N == 0) {
            return std::tuple<>{};
        } else if constexpr (N == 1) {
            auto& [m0] = t;
            return std::tie(m0);
        } else if constexpr (N == 2) {
            auto& [m0, m1] = t;
            return std::tie(m0, m1);
        } else if constexpr (N == 3) {
            auto& [m0, m1, m2] = t;
            return std::tie(m0, m1, m2);
        } else if constexpr (N == 4) {
            auto& [m0, m1, m2, m3] = t;
            return std::tie(m0, m1, m2, m3);
        } else if constexpr (N == 5) {
            auto& [m0, m1, m2, m3, m4] = t;
            return std::tie(m0, m1, m2, m3, m4);
        } else if constexpr (N == 6) {
            auto& [m0, m1, m2, m3, m4, m5] = t;
            return std::tie(m0, m1, m2, m3, m4, m5);
        } else if constexpr (N == 7) {
            auto& [m0, m1, m2, m3, m4, m5, m6] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6);
        } else if constexpr (N == 8) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7);
        } else if constexpr (N == 9) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8);
        } else if constexpr (N == 10) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9);
        } else if constexpr (N == 11) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10);
        } else if constexpr (N == 12) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11);
        } else if constexpr (N == 13) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12);
        } else if constexpr (N == 14) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13);
        } else if constexpr (N == 15) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14);
        } else if constexpr (N == 16) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15);
        } else if constexpr (N == 17) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16);
        } else if constexpr (N == 18) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17);
        } else if constexpr (N == 19) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18);
        } else if constexpr (N == 20) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19] =
                t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19);
        } else if constexpr (N == 21) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20);
        } else if constexpr (N == 22) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21);
        } else if constexpr (N == 23) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22);
        } else if constexpr (N == 24) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23);
        } else if constexpr (N == 25) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24);
        } else if constexpr (N == 26) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25);
        } else if constexpr (N == 27) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26);
        } else if constexpr (N == 28) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27);
        } else if constexpr (N == 29) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28);
        } else if constexpr (N == 30) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29);
        } else if constexpr (N == 31) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30);
        } else if constexpr (N == 32) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31);
        } else if constexpr (N == 33) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32);
        } else if constexpr (N == 34) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33);
        } else if constexpr (N == 35) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33, m34] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33,
                            m34);
        } else if constexpr (N == 36) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33, m34, m35] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33,
                            m34, m35);
        } else if constexpr (N == 37) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33, m34, m35, m36] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33,
                            m34, m35, m36);
        } else if constexpr (N == 38) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33, m34, m35, m36, m37] =
                t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33,
                            m34, m35, m36, m37);
        } else if constexpr (N == 39) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33, m34, m35, m36, m37,
                   m38] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33,
                            m34, m35, m36, m37, m38);
        } else if constexpr (N == 40) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33, m34, m35, m36, m37,
                   m38, m39] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33,
                            m34, m35, m36, m37, m38, m39);
        } else if constexpr (N == 41) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33, m34, m35, m36, m37,
                   m38, m39, m40] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33,
                            m34, m35, m36, m37, m38, m39, m40);
        } else if constexpr (N == 42) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33, m34, m35, m36, m37,
                   m38, m39, m40, m41] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33,
                            m34, m35, m36, m37, m38, m39, m40, m41);
        } else if constexpr (N == 43) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33, m34, m35, m36, m37,
                   m38, m39, m40, m41, m42] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33,
                            m34, m35, m36, m37, m38, m39, m40, m41, m42);
        } else if constexpr (N == 44) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33, m34, m35, m36, m37,
                   m38, m39, m40, m41, m42, m43] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33,
                            m34, m35, m36, m37, m38, m39, m40, m41, m42, m43);
        } else if constexpr (N == 45) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33, m34, m35, m36, m37,
                   m38, m39, m40, m41, m42, m43, m44] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33,
                            m34, m35, m36, m37, m38, m39, m40, m41, m42, m43, m44);
        } else if constexpr (N == 46) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33, m34, m35, m36, m37,
                   m38, m39, m40, m41, m42, m43, m44, m45] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33,
                            m34, m35, m36, m37, m38, m39, m40, m41, m42, m43, m44, m45);
        } else if constexpr (N == 47) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33, m34, m35, m36, m37,
                   m38, m39, m40, m41, m42, m43, m44, m45, m46] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33,
                            m34, m35, m36, m37, m38, m39, m40, m41, m42, m43, m44, m45, m46);
        } else if constexpr (N == 48) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33, m34, m35, m36, m37,
                   m38, m39, m40, m41, m42, m43, m44, m45, m46, m47] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33,
                            m34, m35, m36, m37, m38, m39, m40, m41, m42, m43, m44, m45, m46, m47);
        } else if constexpr (N == 49) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33, m34, m35, m36, m37,
                   m38, m39, m40, m41, m42, m43, m44, m45, m46, m47, m48] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33,
                            m34, m35, m36, m37, m38, m39, m40, m41, m42, m43, m44, m45, m46, m47, m48);
        } else if constexpr (N == 50) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33, m34, m35, m36, m37,
                   m38, m39, m40, m41, m42, m43, m44, m45, m46, m47, m48, m49] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33,
                            m34, m35, m36, m37, m38, m39, m40, m41, m42, m43, m44, m45, m46, m47, m48, m49);
        } else if constexpr (N == 51) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33, m34, m35, m36, m37,
                   m38, m39, m40, m41, m42, m43, m44, m45, m46, m47, m48, m49, m50] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33,
                            m34, m35, m36, m37, m38, m39, m40, m41, m42, m43, m44, m45, m46, m47, m48, m49,
                            m50);
        } else if constexpr (N == 52) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33, m34, m35, m36, m37,
                   m38, m39, m40, m41, m42, m43, m44, m45, m46, m47, m48, m49, m50, m51] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33,
                            m34, m35, m36, m37, m38, m39, m40, m41, m42, m43, m44, m45, m46, m47, m48, m49,
                            m50, m51);
        } else if constexpr (N == 53) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33, m34, m35, m36, m37,
                   m38, m39, m40, m41, m42, m43, m44, m45, m46, m47, m48, m49, m50, m51, m52] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33,
                            m34, m35, m36, m37, m38, m39, m40, m41, m42, m43, m44, m45, m46, m47, m48, m49,
                            m50, m51, m52);
        } else if constexpr (N == 54) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33, m34, m35, m36, m37,
                   m38, m39, m40, m41, m42, m43, m44, m45, m46, m47, m48, m49, m50, m51, m52, m53] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33,
                            m34, m35, m36, m37, m38, m39, m40, m41, m42, m43, m44, m45, m46, m47, m48, m49,
                            m50, m51, m52, m53);
        } else if constexpr (N == 55) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33, m34, m35, m36, m37,
                   m38, m39, m40, m41, m42, m43, m44, m45, m46, m47, m48, m49, m50, m51, m52, m53, m54] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33,
                            m34, m35, m36, m37, m38, m39, m40, m41, m42, m43, m44, m45, m46, m47, m48, m49,
                            m50, m51, m52, m53, m54);
        } else if constexpr (N == 56) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33, m34, m35, m36, m37,
                   m38, m39, m40, m41, m42, m43, m44, m45, m46, m47, m48, m49, m50, m51, m52, m53, m54, m55] =
                t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33,
                            m34, m35, m36, m37, m38, m39, m40, m41, m42, m43, m44, m45, m46, m47, m48, m49,
                            m50, m51, m52, m53, m54, m55);
        } else if constexpr (N == 57) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33, m34, m35, m36, m37,
                   m38, m39, m40, m41, m42, m43, m44, m45, m46, m47, m48, m49, m50, m51, m52, m53, m54, m55,
                   m56] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33,
                            m34, m35, m36, m37, m38, m39, m40, m41, m42, m43, m44, m45, m46, m47, m48, m49,
                            m50, m51, m52, m53, m54, m55, m56);
        } else if constexpr (N == 58) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33, m34, m35, m36, m37,
                   m38, m39, m40, m41, m42, m43, m44, m45, m46, m47, m48, m49, m50, m51, m52, m53, m54, m55,
                   m56, m57] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33,
                            m34, m35, m36, m37, m38, m39, m40, m41, m42, m43, m44, m45, m46, m47, m48, m49,
                            m50, m51, m52, m53, m54, m55, m56, m57);
        } else if constexpr (N == 59) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33, m34, m35, m36, m37,
                   m38, m39, m40, m41, m42, m43, m44, m45, m46, m47, m48, m49, m50, m51, m52, m53, m54, m55,
                   m56, m57, m58] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33,
                            m34, m35, m36, m37, m38, m39, m40, m41, m42, m43, m44, m45, m46, m47, m48, m49,
                            m50, m51, m52, m53, m54, m55, m56, m57, m58);
        } else if constexpr (N == 60) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33, m34, m35, m36, m37,
                   m38, m39, m40, m41, m42, m43, m44, m45, m46, m47, m48, m49, m50, m51, m52, m53, m54, m55,
                   m56, m57, m58, m59] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33,
                            m34, m35, m36, m37, m38, m39, m40, m41, m42, m43, m44, m45, m46, m47, m48, m49,
                            m50, m51, m52, m53, m54, m55, m56, m57, m58, m59);
        } else if constexpr (N == 61) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33, m34, m35, m36, m37,
                   m38, m39, m40, m41, m42, m43, m44, m45, m46, m47, m48, m49, m50, m51, m52, m53, m54, m55,
                   m56, m57, m58, m59, m60] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33,
                            m34, m35, m36, m37, m38, m39, m40, m41, m42, m43, m44, m45, m46, m47, m48, m49,
                            m50, m51, m52, m53, m54, m55, m56, m57, m58, m59, m60);
        } else if constexpr (N == 62) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33, m34, m35, m36, m37,
                   m38, m39, m40, m41, m42, m43, m44, m45, m46, m47, m48, m49, m50, m51, m52, m53, m54, m55,
                   m56, m57, m58, m59, m60, m61] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33,
                            m34, m35, m36, m37, m38, m39, m40, m41, m42, m43, m44, m45, m46, m47, m48, m49,
                            m50, m51, m52, m53, m54, m55, m56, m57, m58, m59, m60, m61);
        } else if constexpr (N == 63) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33, m34, m35, m36, m37,
                   m38, m39, m40, m41, m42, m43, m44, m45, m46, m47, m48, m49, m50, m51, m52, m53, m54, m55,
                   m56, m57, m58, m59, m60, m61, m62] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33,
                            m34, m35, m36, m37, m38, m39, m40, m41, m42, m43, m44, m45, m46, m47, m48, m49,
                            m50, m51, m52, m53, m54, m55, m56, m57, m58, m59, m60, m61, m62);
        } else if constexpr (N == 64) {
            auto& [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19,
                   m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33, m34, m35, m36, m37,
                   m38, m39, m40, m41, m42, m43, m44, m45, m46, m47, m48, m49, m50, m51, m52, m53, m54, m55,
                   m56, m57, m58, m59, m60, m61, m62, m63] = t;
            return std::tie(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17,
                            m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31, m32, m33,
                            m34, m35, m36, m37, m38, m39, m40, m41, m42, m43, m44, m45, m46, m47, m48, m49,
                            m50, m51, m52, m53, m54, m55, m56, m57, m58, m59, m60, m61, m62, m63);
        }
    }
}
