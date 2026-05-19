#pragma once

#include <cereal/cereal.hpp>
#include <string>

namespace cereal {

template <class Archive>
inline void save(Archive &ar, const std::string &value)
{
    ar(value);
}

template <class Archive>
inline void load(Archive &ar, std::string &value)
{
    ar(value);
}

} // namespace cereal
