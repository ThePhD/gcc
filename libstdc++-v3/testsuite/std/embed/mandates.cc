// { dg-do compile { target c++2d } }
// { dg-require-cpp-feature-test "__cpp_lib_embed" }

#include <embed>

consteval bool test()
{
  (void)std::embed<signed char>(__FILE__);
  // { dg-error "embed type must be unqualified 'char', 'unsigned char', or 'std::byte'" .-1 }
  (void)std::embed("praise the sun!");
  // { dg-error "file not found" .-1 }
  (void)std::embed<signed char>("a.txt");
  // { dg-error "file found but not appropriately '#depend ...'ed on" .-1 }
  (void)std::embed<1234>("a.txt");
  // { dg-error "the fixed-size span extent is larger than the resource data size" .-1 }
  return true;
}

int main (int, char*[])
{
  static_assert(test());
  return 0;
}
