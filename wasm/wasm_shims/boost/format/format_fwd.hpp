#pragma once

#include <memory>
#include <string>

namespace boost {

template <class Ch, class Tr = std::char_traits<Ch>, class Alloc = std::allocator<Ch>>
class basic_format;

typedef basic_format<char> format;

std::string str(const format& f);

} // namespace boost
