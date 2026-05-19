#pragma once
// Minimal partitioner stubs for TBB. simple_partitioner / auto_partitioner
// are already declared in parallel_for.h, so we just re-include and add the
// remaining types.

#include "parallel_for.h"

namespace oneapi { namespace tbb {

class affinity_partitioner {};
class static_partitioner {};

}} // namespace oneapi::tbb
