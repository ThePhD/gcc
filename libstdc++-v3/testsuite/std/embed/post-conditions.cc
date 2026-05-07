// { dg-do compile { target c++2d } }
// { dg-require-cpp-feature-test "__cpp_lib_embed" }

#depend __FILE__
#depend "a"
#depend "empty"

#include <embed>

consteval bool test()
{
  constexpr auto v0 = std::embed("a.txt");
  static_assert(v0.data() != nullptr);
  static_assert(v0.size() == 1);
  static_assert(v0[0] == (std::byte)u8'a');

  constexpr auto v1 = std::embed(__FILE__);
  static_assert(v1.size() == 1284);
  static_assert(v1[0] == (std::byte)'/');
  static_assert(v1[1] == (std::byte)'/');
  static_assert(v1[2] == (std::byte)' ');
  static_assert(v1[3] == (std::byte)'{');

  constexpr auto v2 = std::embed(__FILE__, 0x123456);
  static_assert(v2.data() == nullptr);
  static_assert(v2.size() == 0);

  constexpr auto v3 = std::embed(__FILE__, 0, 0);
  static_assert(v3.data() == nullptr);
  static_assert(v3.size() == 0);

  constexpr auto v4 = std::embed<0>(__FILE__);
  static_assert(v4.data() == nullptr);
  static_assert(v4.size() == 0);

  constexpr auto v5 = std::embed("empty");
  static_assert(v5.data() == nullptr);
  static_assert(v5.size() == 0);

  constexpr auto v6 = std::embed(__FILE__, 3, 1);
  static_assert(v6.size() == 1);
  static_assert(v6[0] == (std::byte)u8'{');

  return true;
}

int main (int, char*[])
{
  static_assert(test());
  return 0;
}
