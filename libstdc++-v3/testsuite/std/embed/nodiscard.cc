// { dg-do compile { target c++2d } }
// { dg-require-cpp-feature-test "__cpp_lib_embed" }

#define STR_PREFIX_(a, b) a##b
#define STR_PREFIX(a, b) STR_PREFIX_(a, b)

#include <embed>

consteval bool test()
{
  // make sure to test each individual overload
  std::embed(__FILE__);
  // { dg-warning "ignoring return value of function declared with 'nodiscard' attribute" .-1 }
  std::embed<1>(__FILE__);
  // { dg-warning "ignoring return value of function declared with 'nodiscard' attribute" .-1 }

  std::embed(STR_PREFIX(L, __FILE__));
  // { dg-warning "ignoring return value of function declared with 'nodiscard' attribute" .-1 }
  std::embed<1>(STR_PREFIX(L, __FILE__));
  // { dg-warning "ignoring return value of function declared with 'nodiscard' attribute" .-1 }

  std::embed(STR_PREFIX(u8, __FILE__));
  // { dg-warning "ignoring return value of function declared with 'nodiscard' attribute" .-1 }
  std::embed<1>(STR_PREFIX(u8, __FILE__));
  // { dg-warning "ignoring return value of function declared with 'nodiscard' attribute" .-1 }
  return true;
}

int main (int, char*[])
{
  static_assert(test());
  return 0;
}
