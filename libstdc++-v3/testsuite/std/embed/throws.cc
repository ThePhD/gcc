// { dg-do compile { target c++2d } }
// { dg-require-cpp-feature-test "__cpp_lib_embed" }

#include <embed>

consteval bool test()
{
  try
    {
      (void)std::embed("praise the sun!");
    }
   catch (const std::file_not_found_error&)
    {
      // continue;
    }
  catch (...)
    {
      return false;
    }

  try
    {
      (void)std::embed(__FILE__);
    }
   catch (const std::dependency_error&)
    {
      // continue;
    }
  catch (...)
    {
      return false;
    }
  
  return true;
}

int main (int, char*[])
{
  static_assert(test());
  return 0;
}
