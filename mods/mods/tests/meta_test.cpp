#include <perlin/meta.hpp>
#include <string>
#include <vector>
#include <array>
using namespace perlin::meta;
enum class Color : int { Red = 1, Green = 2, Blue = 3 };
enum class Small : uint8_t { A, B, C = 200 };
enum class Bit : uint32_t { One = 1, Two = 2, Four = 4, Big = 1u << 20 };
template <> struct perlin::meta::enable_flags<Bit> : std::true_type {};
struct Inner {
    int a;
    float b;
};
struct Rec {
    int x;
    std::string name;
    std::array<Inner, 3> arr;
    Inner in;
    std::vector<int> v;
    bool flag;
};
static_assert(enum_name(Color::Green) == "Green");
static_assert(enum_count<Color> == 3);
static_assert(enum_cast<Color>("Blue") == Color::Blue);
static_assert(!enum_cast<Color>("Pink"));
static_assert(enum_name(static_cast<Color>(9)).empty());
static_assert(enum_count<Small> == 3);
static_assert(enum_name(Small::C) == "C");
static_assert(enum_count<Bit> == 4);
static_assert(enum_name(Bit::Big) == "Big");
static_assert(enum_cast_nocase<Color>("rEd") == Color::Red);
static_assert(field_count<Inner> == 2);
static_assert(field_count<Rec> == 6);
static_assert(type_name<Inner>().find("Inner") != std::string_view::npos);
constexpr auto fl = Flags<Bit>(Bit::One) | Bit::Four;
static_assert(fl.has(Bit::One) && !fl.has(Bit::Two) && fl.count() == 2);
int main() {
    Rec r{1, "n", {}, {2, 3.f}, {1, 2}, true};
    int n = 0;
    for_each_field(r, [&](auto&) { n++; });
    int sum = 0;
    for (Bit b : fl)
        sum += (int)b;
    return (n == 6 && sum == 5) ? 0 : 1;
}
