#pragma once

// Lightweight WASM-friendly boost::format replacement.
// Provides enough surface area for existing Orca usages without
// pulling in the heavy Boost.Format machinery (which depends on
// locale/thread features unavailable in our target).

#include "boost/format/format_fwd.hpp"

#include <cstddef>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

namespace boost {

template <class Ch, class Tr, class Alloc>
class basic_format {
public:
    using char_type = Ch;
    using traits_type = Tr;
    using allocator_type = Alloc;
    using string_type = std::basic_string<char_type, traits_type, allocator_type>;

    explicit basic_format(const string_type& fmt)
        : m_format(fmt) {}

    explicit basic_format(const char_type* fmt)
        : m_format(fmt) {}

    template <typename T>
    basic_format& operator%(const T& value) {
        std::basic_ostringstream<char_type, traits_type, allocator_type> oss;
        oss << value;
        m_args.push_back(oss.str());
        return *this;
    }

    string_type str() const {
        string_type result = m_format;
        for (std::size_t idx = 0; idx < m_args.size(); ++idx) {
            const string_type placeholder = make_placeholder(idx + 1);
            std::size_t pos = 0;
            while ((pos = result.find(placeholder, pos)) != string_type::npos) {
                result.replace(pos, placeholder.size(), m_args[idx]);
                pos += m_args[idx].size();
            }
        }
        return result;
    }

    operator string_type() const {
        return str();
    }

private:
    string_type make_placeholder(std::size_t index) const {
        std::basic_ostringstream<char_type, traits_type, allocator_type> oss;
        oss << static_cast<char_type>('%');
        oss << index;
        oss << static_cast<char_type>('%');
        return oss.str();
    }

    string_type m_format;
    std::vector<string_type> m_args;
};

inline std::string str(const format& f) {
    return f.str();
}

template <class StreamCh, class StreamTraits, class FmtCh, class FmtTraits, class FmtAlloc>
inline std::basic_ostream<StreamCh, StreamTraits>& operator<<(std::basic_ostream<StreamCh, StreamTraits>& os,
                                                            const basic_format<FmtCh, FmtTraits, FmtAlloc>& fmt) {
    os << fmt.str();
    return os;
}

} // namespace boost
