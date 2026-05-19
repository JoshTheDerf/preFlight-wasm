#pragma once
#include "../cereal.hpp"

namespace cereal {

template <class Base, class Derived = Base>
class base_class {
public:
  explicit base_class(Derived*) {}
  explicit base_class(const Derived*) {}

  template <class Archive>
  void serialize(Archive&) {}
};

}  // namespace cereal
