#pragma once

#include <cereal/cereal.hpp>
#include <utility>

#ifdef __has_include
#  if __has_include(<optional>) && (__cplusplus >= 201703L)
#    include <optional>
#    define CEREAL_SHIM_HAS_STD_OPTIONAL 1
#  endif
#  if __has_include(<boost/optional.hpp>)
#    include <boost/optional.hpp>
#    define CEREAL_SHIM_HAS_BOOST_OPTIONAL 1
#  endif
#endif

#ifndef CEREAL_SHIM_HAS_STD_OPTIONAL
#  define CEREAL_SHIM_HAS_STD_OPTIONAL 0
#endif
#ifndef CEREAL_SHIM_HAS_BOOST_OPTIONAL
#  define CEREAL_SHIM_HAS_BOOST_OPTIONAL 0
#endif

namespace cereal {

#if CEREAL_SHIM_HAS_STD_OPTIONAL
template <class Archive, class T>
inline void save(Archive &ar, const std::optional<T> &opt)
{
    const bool has_value = opt.has_value();
    ar(has_value);
    if (has_value)
        ar(*opt);
}

template <class Archive, class T>
inline void load(Archive &ar, std::optional<T> &opt)
{
    bool has_value = false;
    ar(has_value);
    if (has_value) {
        T value{};
        ar(value);
        opt = std::move(value);
    } else {
        opt.reset();
    }
}
#endif // CEREAL_SHIM_HAS_STD_OPTIONAL

#if CEREAL_SHIM_HAS_BOOST_OPTIONAL
template <class Archive, class T>
inline void save(Archive &ar, const boost::optional<T> &opt)
{
    const bool has_value = static_cast<bool>(opt);
    ar(has_value);
    if (has_value)
        ar(*opt);
}

template <class Archive, class T>
inline void load(Archive &ar, boost::optional<T> &opt)
{
    bool has_value = false;
    ar(has_value);
    if (has_value) {
        T value{};
        ar(value);
        opt = std::move(value);
    } else {
        opt = boost::none;
    }
}
#endif // CEREAL_SHIM_HAS_BOOST_OPTIONAL

} // namespace cereal
