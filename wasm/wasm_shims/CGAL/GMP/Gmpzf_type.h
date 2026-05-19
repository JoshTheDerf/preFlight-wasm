#pragma once

#if defined(__has_include_next)
#  if __has_include_next(<CGAL/GMP/Gmpzf_type.h>)
#    include_next <CGAL/GMP/Gmpzf_type.h>
#  else
#    error "Expected system CGAL/GMP/Gmpzf_type.h to be available"
#  endif
#else
#  include_next <CGAL/GMP/Gmpzf_type.h>
#endif

namespace CGAL {

inline bool operator<(const Gmpzf& a, double b)
{
  return a < Gmpzf(b);
}

inline bool operator>(const Gmpzf& a, double b)
{
  return Gmpzf(b) < a;
}

inline bool operator<=(const Gmpzf& a, double b)
{
  return !(a > b);
}

inline bool operator>=(const Gmpzf& a, double b)
{
  return !(a < b);
}

inline bool operator<(double a, const Gmpzf& b)
{
  return Gmpzf(a) < b;
}

inline bool operator>(double a, const Gmpzf& b)
{
  return b < Gmpzf(a);
}

inline bool operator<=(double a, const Gmpzf& b)
{
  return !(a > b);
}

inline bool operator>=(double a, const Gmpzf& b)
{
  return !(a < b);
}

} // namespace CGAL
