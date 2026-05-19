#pragma once

#include <cereal/cereal.hpp>
#include <vector>

namespace cereal {

template <class Archive, class T>
inline void save(Archive& ar, const std::vector<T>& vec)
{
    const std::size_t size = vec.size();
    ar(size);
    for (const auto& value : vec)
        ar(value);
}

template <class Archive, class T>
inline void load(Archive& ar, std::vector<T>& vec)
{
    std::size_t size = 0;
    ar(size);
    vec.clear();
    vec.resize(size);
    for (auto& value : vec)
        ar(value);
}

} // namespace cereal
