// { dg-do compile { target c++23 } }
// { dg-options "--embed-dir=${srcdir}/c-c++-common/cpp/embed-dir" }

namespace std
{
  enum byte : unsigned char {};
  typedef decltype(sizeof(0)) size_t;
}

#depend __FILE__
// { dg-warning "'#depend' is a C++29 extension" .-1 }

consteval void f ()
{

  unsigned int locus = 0;
  int status = 0;
  std::size_t size = 0;
  const char* ptrc = 0;
  std::size_t offset = 0;
  std::size_t limit = 0;
  struct illegal_t { unsigned char c; } illegal = {};

  (void)__builtin_std_embed(locus, status, size, ptrc, 0, ptrc, offset, limit, illegal);
  // { dg-error "too many arguments to function '__builtin_std_embed'" .-1 }
  (void)__builtin_std_embed(locus, status, size);
  // { dg-error "too few arguments to function '__builtin_std_embed'" .-1 }
  (void)__builtin_std_embed(locus, status, size, ptrc, 0, ptrc);
  // { dg-error "too few arguments to function '__builtin_std_embed'" .-1 }
  (void)__builtin_std_embed(illegal, status, size, ptrc, 0, ptrc, offset, limit);
  // { dg-error "cannot convert 'f()::illegal_t' to 'unsigned int'" .-1 }
  (void)__builtin_std_embed(locus, illegal, size, ptrc, 0, ptrc, offset, limit);
  // { dg-error "invalid initialization of reference of type 'int&' from expression of type 'f()::illegal_t'" .-1 }
  // size_t might change between platforms: fix the type in-place
  (void)__builtin_std_embed(locus, status, illegal, ptrc, 0, ptrc, offset, limit);
  // { dg-error "invalid initialization of reference of type " .-1 }
  (void)__builtin_std_embed(locus, status, size, illegal, 0, ptrc, offset, limit);
  // { dg-error "argument 4 to '__builtin_std_embed' must be a pointer to a 'const' integer or enumeration type of 'sizeof' and 'alignof' equal to 1" .-1 }
  (void)__builtin_std_embed(locus, status, size, ptrc, illegal, ptrc, offset, limit);
  // { dg-error "argument 5 to function '__builtin_std_embed' must be an integral type" .-1 }
  (void)__builtin_std_embed(locus, status, size, ptrc, 0, illegal, offset, limit);
  // { dg-error "argument 6 to function '__builtin_std_embed' must be a pointer to 'char', 'wchar_t',  or 'char8_t'" .-1 }
  (void)__builtin_std_embed(locus, status, size, ptrc, 0, ptrc, illegal, limit);
  // { dg-error "argument 7 to function '__builtin_std_embed' must be an integral type" .-1 }
  (void)__builtin_std_embed(locus, status, size, ptrc, 0, ptrc, offset, illegal);
  // { dg-error "argument 8 to function '__builtin_std_embed' must be an integral type" .-1 }
}
