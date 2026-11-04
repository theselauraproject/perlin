#pragma once
// atlas constants only; the real header pulls in the whole block database
namespace perlin {
    class BlockLibrary {
    public:
        static int TilePx() { return 16; }
        static constexpr int kAtlasTiles = 128;
    };
}
