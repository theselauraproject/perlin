#include "client/screen/screens/CreativeScroll.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
int main() {
    namespace sc = perlin::screen::creative_scroll;
    assert(sc::FirstRow(0.0f, 25) == 0);
    assert(sc::FirstRow(1.0f, 25) == 20);
    assert(sc::Wheel(0.0f, -1, 25) == 0.05f);
    assert(sc::FirstRow(sc::Wheel(0.0f,-1,25),25) == 1);
    assert(sc::Wheel(0.0f, 1, 25) == 0.0f);
    assert(sc::Wheel(1.0f, -1, 25) == 1.0f);
    assert(sc::Wheel(0.5f, -1, 3) == 0.0f);
    assert(sc::Track(20, 20, 100) == 0.0f);
    assert(sc::Track(120, 20, 100) == 1.0f);
    assert(sc::Track(70, 20, 100) == 0.5f);
    std::cout << "creative scrollbar mapping passed\n";
}
