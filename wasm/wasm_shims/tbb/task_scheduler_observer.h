#pragma once
// Minimal stub for tbb::task_scheduler_observer in the WASM build.

namespace oneapi { namespace tbb {

class task_scheduler_observer {
public:
    task_scheduler_observer() = default;
    explicit task_scheduler_observer(bool /*global*/) {}
    virtual ~task_scheduler_observer() = default;

    void observe(bool /*state*/ = true) {}
    virtual void on_scheduler_entry(bool /*worker*/) {}
    virtual void on_scheduler_exit(bool /*worker*/) {}
};

}} // namespace oneapi::tbb

#ifndef ORCA_WASM_TBB_ALIAS_DEFINED
#define ORCA_WASM_TBB_ALIAS_DEFINED
namespace tbb = oneapi::tbb;
#endif
