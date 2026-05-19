// Minimal headless preFlight WASM bridge
// Adapted from orcaslicer-wasm/bridge/wasm_wrap.cpp.
// C entry points are kept as orc_* for web-frontend compatibility with the orcaslicer-wasm UI.
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <unistd.h>
#include <dirent.h>
#include <new>
#include <errno.h>
#include <cinttypes>
#include <limits>
#include <optional>
#include <map>
#include <sstream>
#include <ctime>

#include <chrono>

#include <nlohmann/json.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/heap.h>
#include <emscripten/stack.h>
#include <malloc.h>
#endif

#ifdef __EMSCRIPTEN__
extern "C" char __heap_base;
#endif

static double now_ms()
{
#ifdef __EMSCRIPTEN__
    return emscripten_get_now();
#else
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double, std::milli>(clock::now().time_since_epoch()).count();
#endif
}

static void log_memory_usage(const char* label)
{
#ifdef __EMSCRIPTEN__
    const size_t heap_bytes = static_cast<size_t>(emscripten_get_heap_size());
    // mallinfo() has been observed to trap under allocator corruption, so we rely on
    // sbrk() to approximate the active heap span even though it overestimates usage.
    const intptr_t heap_base = reinterpret_cast<intptr_t>(&__heap_base);
    const intptr_t heap_break = reinterpret_cast<intptr_t>(sbrk(0));
    size_t used_bytes = 0;
    if (heap_break > heap_base) {
        used_bytes = static_cast<size_t>(heap_break - heap_base);
    }
    const size_t slack_bytes = heap_bytes > used_bytes ? heap_bytes - used_bytes : 0;
    const size_t reported_free = slack_bytes; // best effort estimate without mallinfo
    fprintf(stderr,
        "[pflt_slice] memory %s: heap=%zu used=%zu slack=%zu fordblks=%zu\n",
        label,
        heap_bytes,
        used_bytes,
        slack_bytes,
        reported_free);
    fflush(stderr);
#else
    (void)label;
#endif
}

#ifdef __EMSCRIPTEN__
// Instrument global new/delete so we can observe failing allocations at runtime.
// DISABLED: see comment on the operator new section below. The tracking + the
// global new/delete overrides interfered with libcxxabi's exception machinery.
#if 0
namespace {

static constexpr std::size_t kLargeAllocLogThreshold = std::numeric_limits<std::size_t>::max(); // Disable alloc attempt spam in release
static constexpr unsigned int kMaxLoggedAllocations = 0;
static constexpr std::size_t kAllocTableSize = 1 << 17; // 131072 entries for pointer tracking
static constexpr std::size_t kAllocTableMask = kAllocTableSize - 1;

struct AllocationRecord {
    void* ptr;
    std::size_t size;
    std::size_t alignment;
    unsigned long long id;
    const char* kind;
};

static AllocationRecord g_alloc_table[kAllocTableSize] = {};
static unsigned long long g_allocation_sequence = 0;
static unsigned int g_logged_large_allocations = 0;

static inline std::size_t hash_pointer(const void* ptr)
{
    return (static_cast<std::size_t>(reinterpret_cast<uintptr_t>(ptr) >> 4) & kAllocTableMask);
}

static void record_allocation(void* ptr, std::size_t size, std::size_t alignment, unsigned long long id, const char* kind)
{
    // Tracker disabled: the bridge's `operator new` is not on every alloc path
    // (libcxx/libcxxabi internals, malloc-via-C-stdlib, placement-new, vector
    // reserve from cached blocks etc. all bypass it). The result was tens of
    // thousands of false-positive "untracked pointer free" / "pointer reused
    // without free" warnings per slice — both useless and a major perf drag.
    // Allocations themselves still happen; we just don't book-keep them here.
    (void)ptr; (void)size; (void)alignment; (void)id; (void)kind;
    return;
    if (ptr == nullptr) {
        return;
    }
    const std::size_t start = hash_pointer(ptr);
    for (std::size_t probe = 0; probe < kAllocTableSize; ++probe) {
        const std::size_t idx = (start + probe) & kAllocTableMask;
        AllocationRecord& slot = g_alloc_table[idx];
        if (slot.ptr == nullptr) {
            slot.ptr = ptr;
            slot.size = size;
            slot.alignment = alignment;
            slot.id = id;
            slot.kind = kind;
            return;
        }
        if (slot.ptr == ptr) {
            fprintf(stderr, "[pflt_alloc] warning: pointer reused without free ptr=%p old_id=%" PRIu64 " new_id=%" PRIu64 " size=%zu\n",
                ptr,
                slot.id,
                id,
                size);
            slot.size = size;
            slot.alignment = alignment;
            slot.id = id;
            slot.kind = kind;
            return;
        }
    }
    fprintf(stderr, "[pflt_alloc] error: allocation tracking table exhausted for ptr=%p size=%zu id=%" PRIu64 "\n",
        ptr,
        size,
        id);
}

static void record_free(void* ptr, const char* kind)
{
    (void)ptr; (void)kind;
    return;  // see record_allocation comment
    if (ptr == nullptr) {
        return;
    }
    const std::size_t start = hash_pointer(ptr);
    for (std::size_t probe = 0; probe < kAllocTableSize; ++probe) {
        const std::size_t idx = (start + probe) & kAllocTableMask;
        AllocationRecord& slot = g_alloc_table[idx];
        if (slot.ptr == ptr) {
            slot.ptr = nullptr;
            slot.size = 0;
            slot.id = 0;
            slot.alignment = 0;
            slot.kind = nullptr;
            return;
        }
        if (slot.ptr == nullptr) {
            fprintf(stderr, "[pflt_alloc] warning: %s freeing untracked pointer ptr=%p\n", kind, ptr);
            return;
        }
    }
    fprintf(stderr, "[pflt_alloc] warning: %s freeing pointer not found in tracking table ptr=%p\n", kind, ptr);
}

static void dump_allocation_summary(unsigned long long failure_id)
{
    struct TopEntry {
        std::size_t size;
        unsigned long long id;
        void* ptr;
        const char* kind;
        std::size_t alignment;
    };

    TopEntry top[8] = {};
    std::size_t active = 0;
    std::size_t total_bytes = 0;
    unsigned long long oldest_id = std::numeric_limits<unsigned long long>::max();
    unsigned long long newest_id = 0;

    struct KindStats {
        const char* kind;
        std::size_t count;
        std::size_t bytes;
    };

    KindStats kind_stats[16] = {};
    std::size_t kind_stats_used = 0;

    for (std::size_t idx = 0; idx < kAllocTableSize; ++idx) {
        const AllocationRecord& slot = g_alloc_table[idx];
        if (slot.ptr == nullptr) {
            continue;
        }

        ++active;
        total_bytes += slot.size;
        if (slot.id < oldest_id) {
            oldest_id = slot.id;
        }
        if (slot.id > newest_id) {
            newest_id = slot.id;
        }

        const TopEntry candidate{slot.size, slot.id, slot.ptr, slot.kind, slot.alignment};
        for (std::size_t pos = 0; pos < sizeof(top) / sizeof(top[0]); ++pos) {
            if (candidate.size > top[pos].size) {
                for (std::size_t shift = (sizeof(top) / sizeof(top[0])) - 1; shift > pos; --shift) {
                    top[shift] = top[shift - 1];
                }
                top[pos] = candidate;
                break;
            }
        }

        if (slot.kind != nullptr) {
            std::size_t kind_idx = 0;
            for (; kind_idx < kind_stats_used; ++kind_idx) {
                if (kind_stats[kind_idx].kind == slot.kind) {
                    break;
                }
            }
            if (kind_idx == kind_stats_used && kind_stats_used < (sizeof(kind_stats) / sizeof(kind_stats[0]))) {
                kind_stats[kind_idx].kind = slot.kind;
                kind_stats[kind_idx].count = 0;
                kind_stats[kind_idx].bytes = 0;
                ++kind_stats_used;
            }
            if (kind_idx < kind_stats_used) {
                kind_stats[kind_idx].count += 1;
                kind_stats[kind_idx].bytes += slot.size;
            }
        }
    }

    const double load = static_cast<double>(active) * 100.0 / static_cast<double>(kAllocTableSize);
    fprintf(stderr,
        "[pflt_alloc] failure summary #%" PRIu64 ": active=%zu total_bytes=%zu table_load=%.2f%% oldest_id=%" PRIu64 " newest_id=%" PRIu64 "\n",
        failure_id,
        active,
        total_bytes,
        load,
        (oldest_id == std::numeric_limits<unsigned long long>::max()) ? 0 : oldest_id,
        newest_id);

    if (active > 0) {
        fprintf(stderr, "[pflt_alloc] outstanding allocations by kind:\n");
        for (std::size_t i = 0; i < kind_stats_used; ++i) {
            fprintf(stderr, "  kind=%s count=%zu bytes=%zu\n",
                kind_stats[i].kind != nullptr ? kind_stats[i].kind : "(unknown)",
                kind_stats[i].count,
                kind_stats[i].bytes);
        }

        fprintf(stderr, "[pflt_alloc] largest outstanding allocations:\n");
        for (std::size_t i = 0; i < sizeof(top) / sizeof(top[0]); ++i) {
            if (top[i].size == 0) {
                break;
            }
            fprintf(stderr,
                "  #%zu ptr=%p size=%zu align=%zu kind=%s id=%" PRIu64 "\n",
                i,
                top[i].ptr,
                top[i].size,
                top[i].alignment,
                top[i].kind != nullptr ? top[i].kind : "(unknown)",
                top[i].id);
        }
    }
    fflush(stderr);
}

static void* instrumented_allocate(std::size_t size, std::size_t alignment, bool nothrow, const char* kind)
{
    const unsigned long long alloc_id = ++g_allocation_sequence;
    if (size >= kLargeAllocLogThreshold && g_logged_large_allocations < kMaxLoggedAllocations) {
        ++g_logged_large_allocations;
        fprintf(stderr, "[pflt_alloc] #%" PRIu64 " attempt size=%zu align=%zu kind=%s nothrow=%s\n",
            alloc_id,
            size,
            alignment,
            kind,
            nothrow ? "true" : "false");
        char pre_stack_buffer[2048] = {0};
        int attempt_written = emscripten_get_callstack(EM_LOG_C_STACK | EM_LOG_JS_STACK, pre_stack_buffer, static_cast<int>(sizeof(pre_stack_buffer)));
        if (attempt_written > 0) {
            pre_stack_buffer[sizeof(pre_stack_buffer) - 1] = '\0';
            fprintf(stderr, "[pflt_alloc] #%" PRIu64 " callstack (attempt):\n%s\n", alloc_id, pre_stack_buffer);
        }
        log_memory_usage("before large alloc");
    }

    void* ptr = nullptr;
    if (alignment > 0) {
        ptr = memalign(alignment, size);
    } else {
        ptr = std::malloc(size);
    }

    if (ptr == nullptr) {
        fprintf(stderr, "[pflt_alloc] #%" PRIu64 " %s failed size=%zu align=%zu\n", alloc_id, kind, size, alignment);
        log_memory_usage("alloc failure");
        dump_allocation_summary(alloc_id);
        char stack_buffer[4096] = {0};
        int written = emscripten_get_callstack(EM_LOG_C_STACK | EM_LOG_JS_STACK, stack_buffer, static_cast<int>(sizeof(stack_buffer)));
        if (written > 0) {
            stack_buffer[sizeof(stack_buffer) - 1] = '\0';
            fprintf(stderr, "[pflt_alloc] #%" PRIu64 " callstack (failure):\n%s\n", alloc_id, stack_buffer);
        }
        errno = ENOMEM;
        if (!nothrow) {
            throw std::bad_alloc();
        }
    }

    record_allocation(ptr, size, alignment, alloc_id, kind);

    return ptr;
}

static void instrumented_deallocate(void* ptr, const char* kind) noexcept
{
    record_free(ptr, kind);
    std::free(ptr);
}

} // namespace

// The global operator new/delete overrides below were a debug aid that intercepted every C++
// allocation/deallocation. Combined with `-sEMULATE_FUNCTION_POINTER_CASTS=1` and `-fexceptions`,
// they appear to interfere with libcxxabi's exception-handling machinery: __cxa_throw allocates
// the exception object via global operator new (→ our hook → malloc), but the unwinder later
// reaches landing-pad code through indirect function-pointer thunks, and one of those thunks
// resolves to function table index 0 (the trap thunk) — surfacing as the "null function" trap at
// the 50% G-code generation boundary. Disabling the overrides lets libcxxabi route through its
// canonical operator new/delete from emscripten's libc++, which is what the exception machinery
// expects.
#if 0
void* operator new(std::size_t size)
{
    return instrumented_allocate(size, 0, false, "operator new");
}

void* operator new[](std::size_t size)
{
    return instrumented_allocate(size, 0, false, "operator new[]");
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    return instrumented_allocate(size, 0, true, "operator new (nothrow)");
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    return instrumented_allocate(size, 0, true, "operator new[] (nothrow)");
}

#if defined(__cpp_aligned_new)
void* operator new(std::size_t size, std::align_val_t alignment)
{
    return instrumented_allocate(size, static_cast<std::size_t>(alignment), false, "operator new aligned");
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return instrumented_allocate(size, static_cast<std::size_t>(alignment), false, "operator new[] aligned");
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return instrumented_allocate(size, static_cast<std::size_t>(alignment), true, "operator new aligned (nothrow)");
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return instrumented_allocate(size, static_cast<std::size_t>(alignment), true, "operator new[] aligned (nothrow)");
}
#endif

void operator delete(void* ptr) noexcept
{
    instrumented_deallocate(ptr, "operator delete");
}

void operator delete[](void* ptr) noexcept
{
    instrumented_deallocate(ptr, "operator delete[]");
}

#if defined(__cpp_sized_deallocation)
void operator delete(void* ptr, std::size_t) noexcept
{
    instrumented_deallocate(ptr, "operator delete sized");
}

void operator delete[](void* ptr, std::size_t) noexcept
{
    instrumented_deallocate(ptr, "operator delete[] sized");
}
#endif

#if defined(__cpp_aligned_new)
void operator delete(void* ptr, std::align_val_t) noexcept
{
    instrumented_deallocate(ptr, "operator delete aligned");
}

void operator delete[](void* ptr, std::align_val_t) noexcept
{
    instrumented_deallocate(ptr, "operator delete[] aligned");
}

#if defined(__cpp_sized_deallocation)
void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept
{
    instrumented_deallocate(ptr, "operator delete sized aligned");
}

void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept
{
    instrumented_deallocate(ptr, "operator delete[] sized aligned");
}
#endif
#endif // __cpp_aligned_new
#endif // 0 — operator new/delete overrides disabled (see comment above)
#endif // 0 — instrumented_allocate/record_allocation helpers also disabled

#endif // __EMSCRIPTEN__

// Include preFlight slicer headers
// libslic3r_version.h is generated by the preflight build into the build tree; include if available.
#if __has_include("libslic3r/libslic3r_version.h")
#include "libslic3r/libslic3r_version.h"
#endif
#include "../../preflight/src/libslic3r/TriangleMesh.hpp"
#include "../../preflight/src/libslic3r/Model.hpp"
#include "../../preflight/src/libslic3r/Print.hpp"
#include "../../preflight/src/libslic3r/PrintConfig.hpp"
#include "../../preflight/src/libslic3r/GCode.hpp"
#include "../../preflight/src/libslic3r/GCode/GCodeProcessor.hpp"
#include "../../preflight/src/libslic3r/GCode/GCodeObject.hpp"
#include "../../preflight/src/libslic3r/Format/STL.hpp"
#include "../../preflight/src/libslic3r/PresetBundle.hpp"
#include "../../preflight/src/libslic3r/BoundingBox.hpp"
#include "../../preflight/src/libslic3r/Utils.hpp"

using namespace Slic3r;
using json = nlohmann::json;

static bool g_dump_config = false;
static std::optional<json> g_last_slice_payload;

static void ensure_resources_initialized()
{
    static bool initialized = false;
    if (initialized) {
        return;
    }

    set_resources_dir("/resources");
    set_var_dir("/resources/images");
    set_local_dir("/resources/i18n");
    set_sys_shapes_dir("/resources/shapes");
    set_custom_gcodes_dir("/resources/custom_gcodes");
    // preFlight does not expose set_temporary_dir; /tmp is the default writable mount in Emscripten MEMFS.

    initialized = true;
}

static bool payload_requests_config_dump(const uint8_t *cfg, int len)
{
    if (cfg == nullptr || len <= 0) {
        return false;
    }
    try {
        std::string payload(reinterpret_cast<const char*>(cfg), static_cast<size_t>(len));
        if (payload.find("dumpConfig") != std::string::npos) {
            return payload.find("true") != std::string::npos || payload.find('1') != std::string::npos;
        }
        if (payload.find("dump-config") != std::string::npos) {
            return true;
        }
    } catch (...) {
        // Ignore malformed payloads; fallback to env below.
    }
    return false;
}

static const char* option_type_name(ConfigOptionType type)
{
    switch (type) {
    case coNone: return "coNone";
    case coFloat: return "coFloat";
    case coFloats: return "coFloats";
    case coInt: return "coInt";
    case coInts: return "coInts";
    case coString: return "coString";
    case coStrings: return "coStrings";
    case coPercent: return "coPercent";
    case coPercents: return "coPercents";
    case coFloatOrPercent: return "coFloatOrPercent";
    case coFloatsOrPercents: return "coFloatsOrPercents";
    case coPoint: return "coPoint";
    case coPoints: return "coPoints";
    case coPoint3: return "coPoint3";
    case coBool: return "coBool";
    case coBools: return "coBools";
    case coEnum: return "coEnum";
    case coEnums: return "coEnums";
    default: return "unknown";
    }
}

// Simple helper for STL loading
static bool load_stl_from_buffer(const uint8_t* data, size_t len, Model& model) {
    try {
        // For now, write to temp location - in production we'd use memory stream
        std::string temp_path = "/tmp/model.stl";
        FILE* f = fopen(temp_path.c_str(), "wb");
        if (!f) return false;
        fwrite(data, 1, len, f);
        fclose(f);

        return Slic3r::load_stl(temp_path.c_str(), &model);
    } catch (...) {
        return false;
    }
}

// Emit a concise config summary focused on tweaks applied for the WASM build.
static void log_config(const DynamicPrintConfig& config) {
    fprintf(stderr, "[pflt_slice] config dump begin\n");
    std::vector<std::string> keys = config.keys();
    std::sort(keys.begin(), keys.end());
    for (const std::string &key : keys) {
        if (const ConfigOption* opt = config.optptr(key)) {
            std::string serialized = opt->serialize();
            fprintf(stderr, "  %s = %s\n", key.c_str(), serialized.c_str());
        }
    }
    fprintf(stderr, "[pflt_slice] config dump end\n");
    fflush(stderr);
}

template <typename OptionT, typename ValueT>
static void assign_vector_values(OptionT *opt, const std::vector<ValueT> &values, ValueT fallback)
{
    auto &target = opt->values;
    if (values.empty()) {
        if (target.empty()) {
            target.assign(1, fallback);
        } else {
            std::fill(target.begin(), target.end(), fallback);
        }
    } else if (values.size() == 1 && target.size() > 1) {
        std::fill(target.begin(), target.end(), values.front());
    } else {
        target = values;
    }
    if (target.empty()) {
        target.assign(1, fallback);
    }
}

static bool set_bool_option(DynamicPrintConfig &config, const char *key, bool value)
{
    if (auto *opt = config.opt<ConfigOptionBool>(key, true)) {
        opt->value = value;
        return true;
    }
    unsigned char stored = value ? 1 : 0;
    if (auto *opt_vec = config.opt<ConfigOptionBools>(key, true)) {
        auto &target = opt_vec->values;
        if (target.empty()) {
            target.assign(1, stored);
        } else {
            std::fill(target.begin(), target.end(), stored);
        }
        return true;
    }
    if (auto *opt_vec_nullable = config.opt<ConfigOptionBoolsNullable>(key, true)) {
        auto &target = opt_vec_nullable->values;
        if (target.empty()) {
            target.assign(1, stored);
        } else {
            std::fill(target.begin(), target.end(), stored);
        }
        return true;
    }
    return false;
}

static bool set_int_vector_option(DynamicPrintConfig &config, const char *key, const std::vector<int> &values)
{
    if (auto *opt = config.opt<ConfigOptionInts>(key, true)) {
        assign_vector_values(opt, values, 0);
        return true;
    }
    if (auto *opt_nullable = config.opt<ConfigOptionIntsNullable>(key, true)) {
        assign_vector_values(opt_nullable, values, 0);
        return true;
    }
    if (values.size() == 1) {
        if (auto *opt_scalar = config.opt<ConfigOptionInt>(key, true)) {
            opt_scalar->value = values.front();
            return true;
        }
    }
    return false;
}

static bool set_int_option(DynamicPrintConfig &config, const char *key, int value)
{
    return set_int_vector_option(config, key, std::vector<int>{value});
}

static bool set_float_vector_option(DynamicPrintConfig &config, const char *key, const std::vector<double> &values)
{
    if (auto *opt = config.opt<ConfigOptionFloats>(key, true)) {
        assign_vector_values(opt, values, 0.0);
        return true;
    }
    if (auto *opt_nullable = config.opt<ConfigOptionFloatsNullable>(key, true)) {
        assign_vector_values(opt_nullable, values, 0.0);
        return true;
    }
    if (values.size() == 1) {
        if (auto *opt_scalar = config.opt<ConfigOptionFloat>(key, true)) {
            opt_scalar->value = values.front();
            return true;
        }
    }
    return false;
}

static bool set_float_option(DynamicPrintConfig &config, const char *key, double value)
{
    if (auto *opt = config.opt<ConfigOptionFloat>(key, true)) {
        opt->value = value;
        return true;
    }
    return set_float_vector_option(config, key, std::vector<double>{value});
}

static bool set_percent_option(DynamicPrintConfig &config, const char *key, double value)
{
    if (auto *opt = config.opt<ConfigOptionPercent>(key, true)) {
        opt->value = value;
        return true;
    }
    if (auto *opt_vec = config.opt<ConfigOptionPercents>(key, true)) {
        assign_vector_values(opt_vec, std::vector<double>{value}, 0.0);
        return true;
    }
    if (auto *opt_vec_nullable = config.opt<ConfigOptionPercentsNullable>(key, true)) {
        assign_vector_values(opt_vec_nullable, std::vector<double>{value}, 0.0);
        return true;
    }
    return set_float_option(config, key, value);
}

template <typename EnumT>
static bool set_enum_option(DynamicPrintConfig &config, const char *key, EnumT value)
{
    if (auto *opt = config.opt<ConfigOptionEnum<EnumT>>(key, true)) {
        opt->value = value;
        return true;
    }
    if (auto *opt_generic = config.opt<ConfigOptionEnumGeneric>(key, true)) {
        opt_generic->value = static_cast<int>(value);
        return true;
    }
    return false;
}

static bool set_float_or_percent_option(DynamicPrintConfig &config, const char *key, double value, bool percent = false)
{
    if (auto *opt = config.opt<ConfigOptionFloatOrPercent>(key, true)) {
        opt->value = value;
        opt->percent = percent;
        return true;
    }
    const FloatOrPercent fp{value, percent};
    if (auto *opt_vec = config.opt<ConfigOptionFloatsOrPercents>(key, true)) {
        auto &target = opt_vec->values;
        if (target.empty()) {
            target.assign(1, fp);
        } else {
            std::fill(target.begin(), target.end(), fp);
        }
        return true;
    }
    if (auto *opt_vec_nullable = config.opt<ConfigOptionFloatsOrPercentsNullable>(key, true)) {
        auto &target = opt_vec_nullable->values;
        if (target.empty()) {
            target.assign(1, fp);
        } else {
            std::fill(target.begin(), target.end(), fp);
        }
        return true;
    }
    return false;
}

static bool set_string_option(DynamicPrintConfig &config, const char *key, const std::string &value)
{
    if (auto *opt = config.opt<ConfigOptionString>(key, true)) {
        opt->value = value;
        return true;
    }
    if (auto *opt_vec = config.opt<ConfigOptionStrings>(key, true)) {
        auto &target = opt_vec->values;
        if (target.empty()) {
            target.assign(1, value);
        } else {
            std::fill(target.begin(), target.end(), value);
        }
        return true;
    }
    return false;
}

static std::string config_option_type_to_string(ConfigOptionType type)
{
    switch (type) {
    case coNone: return "none";
    case coFloat: return "float";
    case coFloats: return "floats";
    case coInt: return "int";
    case coInts: return "ints";
    case coString: return "string";
    case coStrings: return "strings";
    case coPercent: return "percent";
    case coPercents: return "percents";
    case coFloatOrPercent: return "float_or_percent";
    case coFloatsOrPercents: return "floats_or_percents";
    case coPoint: return "point";
    case coPoints: return "points";
    case coPoint3: return "point3";
    case coBool: return "bool";
    case coBools: return "bools";
    case coEnum: return "enum";
    case coEnums: return "enums";
    default: return "unknown";
    }
}

static std::string config_option_mode_to_string(ConfigOptionMode mode)
{
    switch (mode) {
    case comSimple: return "simple";
    case comAdvanced: return "advanced";
    case comExpert: return "expert";
    default: return "simple";
    }
}

static std::string config_option_gui_type_to_string(ConfigOptionDef::GUIType gui_type)
{
    using GUIType = ConfigOptionDef::GUIType;
    switch (gui_type) {
    case GUIType::undefined: return "undefined";
    case GUIType::i_enum_open: return "i_enum_open";
    case GUIType::f_enum_open: return "f_enum_open";
    case GUIType::color: return "color";
    case GUIType::select_open: return "select_open";
    case GUIType::slider: return "slider";
    case GUIType::legend: return "legend";
    case GUIType::one_string: return "one_string";
    default: return "undefined";
    }
}

static bool parse_float_or_percent(const json &value, FloatOrPercent &out)
{
    if (value.is_object()) {
        auto it_val = value.find("value");
        if (it_val == value.end() || !it_val->is_number()) {
            return false;
        }
        out.value = it_val->get<double>();
        auto it_percent = value.find("percent");
        out.percent = (it_percent != value.end() && it_percent->is_boolean()) ? it_percent->get<bool>() : false;
        return true;
    }
    if (value.is_number()) {
        out.value = value.get<double>();
        out.percent = false;
        return true;
    }
    if (value.is_string()) {
        const std::string serialized = value.get<std::string>();
        if (serialized.empty()) {
            out.value = 0.0;
            out.percent = false;
            return true;
        }
        bool percent = false;
        std::string token = serialized;
        if (!token.empty() && token.back() == '%') {
            percent = true;
            token.pop_back();
        }
        try {
            const double parsed = std::stod(token);
            out.value = parsed;
            out.percent = percent;
            return true;
        } catch (...) {
            return false;
        }
    }
    return false;
}

static bool parse_float_or_percent_array(const json &value, std::vector<FloatOrPercent> &out)
{
    if (!value.is_array()) {
        return false;
    }
    std::vector<FloatOrPercent> parsed;
    parsed.reserve(value.size());
    for (const json &entry : value) {
        FloatOrPercent fop{0.0, false};
        if (!parse_float_or_percent(entry, fop)) {
            return false;
        }
        parsed.push_back(fop);
    }
    out = std::move(parsed);
    return true;
}

static bool apply_config_value(DynamicPrintConfig &config, const ConfigOptionDef &def, const std::string &key, const json &value)
{
    const char *ckey = key.c_str();

    switch (def.type) {
    case coFloat:
        if (!value.is_number()) {
            return false;
        }
        return set_float_option(config, ckey, value.get<double>());
    case coFloats: {
        std::vector<double> numbers;
        if (value.is_array()) {
            numbers.reserve(value.size());
            for (const json &entry : value) {
                if (!entry.is_number()) {
                    return false;
                }
                numbers.push_back(entry.get<double>());
            }
        } else if (value.is_number()) {
            numbers.push_back(value.get<double>());
        } else {
            return false;
        }
        return set_float_vector_option(config, ckey, numbers);
    }
    case coInt:
        if (!value.is_number()) {
            return false;
        }
        return set_int_option(config, ckey, static_cast<int>(std::llround(value.get<double>())));
    case coInts: {
        std::vector<int> numbers;
        if (value.is_array()) {
            numbers.reserve(value.size());
            for (const json &entry : value) {
                if (!entry.is_number()) {
                    return false;
                }
                numbers.push_back(static_cast<int>(std::llround(entry.get<double>())));
            }
        } else if (value.is_number()) {
            numbers.push_back(static_cast<int>(std::llround(value.get<double>())));
        } else {
            return false;
        }
        return set_int_vector_option(config, ckey, numbers);
    }
    case coString:
        if (!value.is_string()) {
            return false;
        }
        return set_string_option(config, ckey, value.get<std::string>());
    case coStrings: {
        if (!value.is_array()) {
            return false;
        }
        std::vector<std::string> strings;
        strings.reserve(value.size());
        for (const json &entry : value) {
            if (!entry.is_string()) {
                return false;
            }
            strings.push_back(entry.get<std::string>());
        }
        if (auto *opt = config.opt<ConfigOptionStrings>(ckey, true)) {
            opt->values = strings;
            return true;
        }
        return false;
    }
    case coPercent:
        if (value.is_number()) {
            return set_percent_option(config, ckey, value.get<double>());
        }
        if (value.is_string()) {
            std::string token = value.get<std::string>();
            bool percent = false;
            if (!token.empty() && token.back() == '%') {
                percent = true;
                token.pop_back();
            }
            try {
                const double parsed = std::stod(token);
                return set_percent_option(config, ckey, percent ? parsed : parsed);
            } catch (...) {
                return false;
            }
        }
        return false;
    case coPercents: {
        std::vector<double> percents;
        if (value.is_array()) {
            percents.reserve(value.size());
            for (const json &entry : value) {
                if (!entry.is_number()) {
                    return false;
                }
                percents.push_back(entry.get<double>());
            }
        } else if (value.is_number()) {
            percents.push_back(value.get<double>());
        } else {
            return false;
        }
        if (auto *opt = config.opt<ConfigOptionPercents>(ckey, true)) {
            assign_vector_values(opt, percents, 0.0);
            return true;
        }
        if (auto *opt_nullable = config.opt<ConfigOptionPercentsNullable>(ckey, true)) {
            assign_vector_values(opt_nullable, percents, 0.0);
            return true;
        }
        if (percents.size() == 1) {
            return set_percent_option(config, ckey, percents.front());
        }
        return false;
    }
    case coFloatOrPercent: {
        FloatOrPercent parsed{0.0, false};
        if (!parse_float_or_percent(value, parsed)) {
            return false;
        }
        if (auto *opt = config.opt<ConfigOptionFloatOrPercent>(ckey, true)) {
            opt->value = parsed.value;
            opt->percent = parsed.percent;
            return true;
        }
        return false;
    }
    case coFloatsOrPercents: {
        std::vector<FloatOrPercent> parsed;
        if (!parse_float_or_percent_array(value, parsed)) {
            return false;
        }
        if (auto *opt = config.opt<ConfigOptionFloatsOrPercents>(ckey, true)) {
            assign_vector_values(opt, parsed, FloatOrPercent{0.0, false});
            return true;
        }
        if (auto *opt_nullable = config.opt<ConfigOptionFloatsOrPercentsNullable>(ckey, true)) {
            assign_vector_values(opt_nullable, parsed, FloatOrPercent{0.0, false});
            return true;
        }
        return false;
    }
    case coPoint: {
        if (!value.is_array() || value.size() != 2) {
            return false;
        }
        if (!value[0].is_number() || !value[1].is_number()) {
            return false;
        }
        if (auto *opt = config.opt<ConfigOptionPoint>(ckey, true)) {
            opt->value = Vec2d(value[0].get<double>(), value[1].get<double>());
            return true;
        }
        return false;
    }
    case coPoints: {
        if (!value.is_array()) {
            return false;
        }
        std::vector<Vec2d> points;
        points.reserve(value.size());
        for (const json &entry : value) {
            if (!entry.is_array() || entry.size() != 2) {
                return false;
            }
            if (!entry[0].is_number() || !entry[1].is_number()) {
                return false;
            }
            points.emplace_back(entry[0].get<double>(), entry[1].get<double>());
        }
        if (auto *opt = config.opt<ConfigOptionPoints>(ckey, true)) {
            opt->values = points;
            return true;
        }
        return false;
    }
    case coPoint3: {
        if (!value.is_array() || value.size() != 3) {
            return false;
        }
        if (!value[0].is_number() || !value[1].is_number() || !value[2].is_number()) {
            return false;
        }
        if (auto *opt = config.opt<ConfigOptionPoint3>(ckey, true)) {
            opt->value = Vec3d(value[0].get<double>(), value[1].get<double>(), value[2].get<double>());
            return true;
        }
        return false;
    }
    case coBool:
        if (!value.is_boolean()) {
            return false;
        }
        return set_bool_option(config, ckey, value.get<bool>());
    case coBools: {
        if (!value.is_array()) {
            return false;
        }
        std::vector<unsigned char> bools;
        bools.reserve(value.size());
        for (const json &entry : value) {
            if (!entry.is_boolean()) {
                return false;
            }
            bools.push_back(entry.get<bool>() ? 1 : 0);
        }
        if (auto *opt = config.opt<ConfigOptionBools>(ckey, true)) {
            auto &target = opt->values;
            target = bools;
            return true;
        }
        if (auto *opt_nullable = config.opt<ConfigOptionBoolsNullable>(ckey, true)) {
            auto &target = opt_nullable->values;
            target = bools;
            return true;
        }
        return false;
    }
    case coEnum: {
        // preflight wraps enum metadata in ConfigOptionEnumDef with private
        // members; resolve strings via the public value_to_index() helper.
        int enum_value = 0;
        if (value.is_number_integer()) {
            enum_value = value.get<int>();
        } else if (value.is_string() && def.enum_def) {
            auto idx = def.enum_def->value_to_index(value.get<std::string>());
            if (!idx) {
                return false;
            }
            enum_value = *idx;
        } else {
            return false;
        }
        if (auto *opt = config.opt<ConfigOptionEnumGeneric>(ckey, true)) {
            opt->value = enum_value;
            return true;
        }
        return false;
    }
    case coEnums: {
        if (!value.is_array()) {
            return false;
        }
        std::vector<int> enums;
        enums.reserve(value.size());
        for (const json &entry : value) {
            if (entry.is_number_integer()) {
                enums.push_back(entry.get<int>());
            } else if (entry.is_string() && def.enum_def) {
                auto idx = def.enum_def->value_to_index(entry.get<std::string>());
                if (!idx) {
                    return false;
                }
                enums.push_back(*idx);
            } else {
                return false;
            }
        }
        if (auto *opt = config.opt<ConfigOptionEnumsGeneric>(ckey, true)) {
            opt->values = enums;
            return true;
        }
        if (auto *opt_nullable = config.opt<ConfigOptionEnumsGenericNullable>(ckey, true)) {
            opt_nullable->values = enums;
            return true;
        }
        return false;
    }
    default:
        break;
    }
    return false;
}

static bool apply_config_value_by_key(DynamicPrintConfig &config, const ConfigDef &defs, const std::string &key, const json &value)
{
    const ConfigOptionDef *def = defs.get(key);
    if (def == nullptr) {
        return false;
    }
    return apply_config_value(config, *def, key, value);
}

// Simple default config
static DynamicPrintConfig get_default_config() {
    fprintf(stderr, "[pflt_schema] get_default_config start\n");
    fflush(stderr);
    // Seed with the full preset so overrides match real option types.
    DynamicPrintConfig config;
    config.apply(FullPrintConfig::defaults());
    config.set_num_extruders(1);
    // preFlight does not expose set_num_filaments; multi-material is driven by extruder count only.

    auto ensure = [&](bool ok, const char *key) {
        if (!ok) {
            const DynamicPrintConfig &cfg_const = config;
            const ConfigOption *existing = cfg_const.option(key);
            if (existing == nullptr) {
                const auto &defaults = FullPrintConfig::defaults();
                existing = defaults.option(key);
            }
            const char *type_name = existing ? option_type_name(existing->type()) : "missing";
            fprintf(stderr, "[pflt_slice] warning: failed to override %s (type=%s)\n", key, type_name);
        }
    };

    ensure(set_float_option(config, "layer_height", 0.2), "layer_height");
    ensure(set_percent_option(config, "sparse_infill_density", 0.0), "sparse_infill_density");
    ensure(set_int_option(config, "wall_loops", 2), "wall_loops");
    ensure(set_int_option(config, "top_shell_layers", 0), "top_shell_layers");
    ensure(set_int_option(config, "bottom_shell_layers", 0), "bottom_shell_layers");
    ensure(set_bool_option(config, "enable_support", false), "enable_support");
    ensure(set_int_option(config, "skirt_loops", 0), "skirt_loops");
    ensure(set_float_option(config, "brim_width", 0.0), "brim_width");
    // PerimeterGeneratorType / EnsureVerticalShellThickness enum names differ
    // between preflight and orca; rely on the FullPrintConfig defaults here.
    ensure(set_bool_option(config, "precise_outer_wall", false), "precise_outer_wall");
    ensure(set_bool_option(config, "thick_internal_bridges", false), "thick_internal_bridges");

    fprintf(stderr, "[pflt_schema] get_default_config done\n");
    fflush(stderr);
    return config;
}

static std::optional<InfillPattern> parse_infill_pattern(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value == "grid")      return InfillPattern::ipGrid;
    if (value == "gyroid")    return InfillPattern::ipGyroid;
    if (value == "cubic")     return InfillPattern::ipCubic;
    if (value == "triangles") return InfillPattern::ipTriangles;
    if (value == "rectilinear") return InfillPattern::ipRectilinear;
    if (value == "lightning") return InfillPattern::ipLightning;
    return std::nullopt;
}

static void apply_model_rotation(Model &model, const json &payload)
{
    if (!payload.is_object()) {
        return;
    }
    auto rot_it = payload.find("rotation_deg");
    if (rot_it == payload.end() || !rot_it->is_object()) {
        return;
    }
    constexpr double kDegToRad = 0.01745329251994329576923690768489;
    const json &rotation = *rot_it;
    bool rotated = false;
    auto apply_axis = [&](const char *key, Axis axis) {
        auto axis_it = rotation.find(key);
        if (axis_it == rotation.end() || !axis_it->is_number()) {
            return;
        }
        double angle_deg = axis_it->get<double>();
        if (std::abs(angle_deg) < 1e-6) {
            return;
        }
        const double angle_rad = angle_deg * kDegToRad;
        for (ModelObject *object : model.objects) {
            if (object != nullptr) {
                object->rotate(angle_rad, axis);
            }
        }
        rotated = true;
    };

    apply_axis("x", Axis::X);
    apply_axis("y", Axis::Y);
    apply_axis("z", Axis::Z);

    if (rotated) {
        for (ModelObject *object : model.objects) {
            if (object != nullptr) {
                object->ensure_on_bed(false);
            }
        }
    }
}

static void apply_config_overrides(DynamicPrintConfig &config, const json &payload)
{
    if (!payload.is_object()) {
        return;
    }

    const ConfigDef *defs = config.def();
    if (defs == nullptr) {
        fprintf(stderr, "[pflt_slice] warning: print configuration metadata unavailable; overrides skipped\n");
        fflush(stderr);
        return;
    };

    auto warn_override = [](const std::string &key) {
        fprintf(stderr, "[pflt_slice] warning: failed to apply override for %s\n", key.c_str());
        fflush(stderr);
    };

    static const std::map<std::string, std::vector<std::string>> kLegacyAliasMap = {
        {"supports_enabled", {"enable_support"}},
        {"cooling_fan_speed", {"fan_max_speed", "fan_min_speed"}},
        {"nozzle_temperature_initial", {"nozzle_temperature_initial_layer", "first_layer_temperature"}},
        {"bed_temperature_initial", {"bed_temperature_initial_layer", "first_layer_bed_temperature"}},
        {"first_layer_height", {"first_layer_height", "initial_layer_print_height"}},
        {"infill_pattern", {"sparse_infill_pattern"}}
    };

    auto apply_alias = [&](const std::string &key, const json &value) -> bool {
        auto alias_it = kLegacyAliasMap.find(key);
        if (alias_it == kLegacyAliasMap.end()) {
            return false;
        }
        bool applied_any = false;
        for (const std::string &target : alias_it->second) {
            if (apply_config_value_by_key(config, *defs, target, value)) {
                applied_any = true;
                continue;
            }
            if (target == "sparse_infill_pattern" && value.is_string()) {
                if (auto mapped = parse_infill_pattern(value.get<std::string>())) {
                    if (set_enum_option(config, "sparse_infill_pattern", *mapped)) {
                        applied_any = true;
                    }
                }
            }
        }
        return applied_any;
    };

    auto apply_entry = [&](const std::string &key, const json &value) {
        if (key == "rotation_deg" || key == "config") {
            return;
        }
        if (apply_config_value_by_key(config, *defs, key, value)) {
            return;
        }
        if (apply_alias(key, value)) {
            return;
        }
        warn_override(key);
    };

    if (auto cfg_it = payload.find("config"); cfg_it != payload.end() && cfg_it->is_object()) {
        for (const auto &entry : cfg_it->items()) {
            apply_entry(entry.key(), entry.value());
        }
    }

    for (const auto &entry : payload.items()) {
        const std::string &key = entry.key();
        if (key == "config" || key == "rotation_deg") {
            continue;
        }
        apply_entry(key, entry.value());
    }
}

static std::string slugify_identifier(const std::string &label)
{
    std::string id;
    id.reserve(label.size());
    for (unsigned char ch : label) {
        if (std::isalnum(ch)) {
            id.push_back(static_cast<char>(std::tolower(ch)));
        } else if (ch == ' ' || ch == '-' || ch == '_') {
            if (!id.empty() && id.back() != '-') {
                id.push_back('-');
            }
        }
    }
    if (id.empty()) {
        id = "general";
    }
    while (!id.empty() && id.back() == '-') {
        id.pop_back();
    }
    return id;
}

static std::string iso8601_now_utc()
{
    using clock = std::chrono::system_clock;
    const clock::time_point now = clock::now();
    const std::time_t tt = clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    char buffer[32] = {0};
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm) == 0) {
        return "1970-01-01T00:00:00Z";
    }
    return std::string(buffer);
}

static json build_config_schema()
{
    fprintf(stderr, "[pflt_schema] build start\n");
    fflush(stderr);
    DynamicPrintConfig config = get_default_config();
    fprintf(stderr, "[pflt_schema] defaults acquired\n");
    fflush(stderr);
    const ConfigDef *defs = config.def();
    json result = json::object();
    if (defs == nullptr) {
        result["categories"] = json::array();
        result["generatedAt"] = iso8601_now_utc();
        return result;
    }

    struct OptionEntry {
        std::string category;
        json data;
        size_t ordinal;
    };

    std::vector<OptionEntry> entries;
    entries.reserve(defs->options.size());

    size_t ordinal_counter = 0;
    for (const auto &kv : defs->options) {
        const std::string &key = kv.first;
        const ConfigOptionDef &def = kv.second;

        if (def.readonly) {
            continue;
        }
        if (def.printer_technology != ptAny && def.printer_technology != ptFFF && def.printer_technology != ptUnknown) {
            continue;
        }

        // Minimal schema entry — preflight's ConfigOptionDef diverged from
        // orca's (enum metadata moved into ConfigOptionEnumDef etc.) so we
        // omit the optional metadata blocks instead of fighting API drift.
        json option = json::object();
        option["key"] = key;
        option["label"] = def.label;
        option["type"] = config_option_type_to_string(def.type);
        option["mode"] = config_option_mode_to_string(def.mode);
        option["nullable"] = def.nullable;
        option["isVector"] = !def.is_scalar();
        option["category"] = def.category.empty() ? "General" : def.category;
        option["guiType"] = config_option_gui_type_to_string(def.gui_type);
        if (!def.tooltip.empty()) {
            option["tooltip"] = def.tooltip;
        }
        if (!def.sidetext.empty()) {
            option["unit"] = def.sidetext;
        }
        if (def.default_value) {
            if (def.is_scalar()) {
                option["default"] = def.default_value->serialize();
            } else if (const auto *vector_option = dynamic_cast<const ConfigOptionVectorBase *>(def.default_value.get())) {
                option["default"] = vector_option->vserialize();
            }
        }

        const size_t ordinal = ordinal_counter++;
        option["serializationOrdinal"] = ordinal;
        entries.push_back(OptionEntry{
            def.category.empty() ? std::string("General") : def.category,
            std::move(option),
            ordinal
        });
    }

    std::sort(entries.begin(), entries.end(), [](const OptionEntry &a, const OptionEntry &b) {
        if (a.ordinal != b.ordinal) {
            return a.ordinal < b.ordinal;
        }
        return a.data["key"].get<std::string>() < b.data["key"].get<std::string>();
    });

    struct CategoryBucket {
        std::string label;
        std::string id;
        json options = json::array();
        size_t firstOrdinal = std::numeric_limits<size_t>::max();
    };

    std::vector<CategoryBucket> buckets;
    std::map<std::string, size_t> bucket_index;

    for (auto &entry : entries) {
        const std::string &label = entry.category;
        auto it = bucket_index.find(label);
        if (it == bucket_index.end()) {
            CategoryBucket bucket;
            bucket.label = label;
            bucket.id = slugify_identifier(label);
            bucket.options = json::array();
            bucket.firstOrdinal = entry.ordinal;
            buckets.push_back(std::move(bucket));
            bucket_index[label] = buckets.size() - 1;
            it = bucket_index.find(label);
        }
        CategoryBucket &bucket = buckets[it->second];
        bucket.options.push_back(entry.data);
        if (entry.ordinal < bucket.firstOrdinal) {
            bucket.firstOrdinal = entry.ordinal;
        }
    }

    std::sort(buckets.begin(), buckets.end(), [](const CategoryBucket &a, const CategoryBucket &b) {
        if (a.firstOrdinal != b.firstOrdinal) {
            return a.firstOrdinal < b.firstOrdinal;
        }
        return a.label < b.label;
    });

    json categories = json::array();
    for (const CategoryBucket &bucket : buckets) {
        categories.push_back(json{
            {"id", bucket.id},
            {"label", bucket.label},
            {"options", bucket.options}
        });
    }

    result["generatedAt"] = iso8601_now_utc();
    result["categories"] = categories;
    result["optionCount"] = entries.size();
    fprintf(stderr, "[pflt_schema] build done optionCount=%zu\n", entries.size());
    fflush(stderr);
    return result;
}

// Helper function to create STL from memory buffer using Orca's STL loader
bool load_stl_from_memory(const uint8_t* data, size_t len, Model* model, std::string& error) {
    try {
        // Write data to a temporary file since Orca's STL loader expects a file path
        // In a production version, we'd extend Orca to support memory buffers
        std::string temp_filename = "/tmp/temp_model.stl";

        FILE* temp_file = fopen(temp_filename.c_str(), "wb");
        if (!temp_file) {
            error = "Failed to create temporary file";
            return false;
        }

        size_t written = fwrite(data, 1, len, temp_file);
        fclose(temp_file);

        if (written != len) {
            error = "Failed to write complete file data";
            return false;
        }

        // Use Orca's built-in STL loader
        bool success = load_stl(temp_filename.c_str(), model, "imported_object");

        // Clean up temporary file
        unlink(temp_filename.c_str());

        if (!success) {
            error = "Failed to load STL using Orca's loader";
            return false;
        }

        return true;

    } catch (const std::exception& e) {
        error = std::string("Exception: ") + e.what();
        return false;
    } catch (...) {
        error = "Unknown exception during STL loading";
        return false;
    }
}

extern "C" {

__attribute__((used)) int orc_describe_config(uint8_t **json_out, int *json_len)
{
    if (json_out == nullptr || json_len == nullptr) {
        return -1;
    }
    ensure_resources_initialized();
    try {
        json schema = build_config_schema();
        const std::string dump = schema.dump();
        if (dump.empty()) {
            *json_out = nullptr;
            *json_len = 0;
            return 0;
        }
        uint8_t *buffer = static_cast<uint8_t *>(std::malloc(dump.size()));
        if (buffer == nullptr) {
            *json_out = nullptr;
            *json_len = 0;
            return -2;
        }
        std::memcpy(buffer, dump.data(), dump.size());
        *json_out = buffer;
        *json_len = static_cast<int>(dump.size());
        return 0;
    } catch (const std::exception &ex) {
        fprintf(stderr, "[pflt_slice] error: describe_config exception %s\n", ex.what());
        fflush(stderr);
        *json_out = nullptr;
        *json_len = 0;
        return -3;
    } catch (...) {
        fprintf(stderr, "[pflt_slice] error: describe_config unknown exception\n");
        fflush(stderr);
        *json_out = nullptr;
        *json_len = 0;
        return -3;
    }
}

// Optional: capture config (JSON/TOML) once
__attribute__((used)) int orc_init(const uint8_t* cfg, int len) {
    ensure_resources_initialized();
    g_dump_config = payload_requests_config_dump(cfg, len);
    if (!g_dump_config && std::getenv("ORC_DUMP_CONFIG")) {
        g_dump_config = true;
    }
    if (cfg != nullptr && len > 0) {
        try {
            std::string payload(reinterpret_cast<const char*>(cfg), static_cast<size_t>(len));
            if (!payload.empty()) {
                g_last_slice_payload = json::parse(payload, nullptr, true, true);
            } else {
                g_last_slice_payload.reset();
            }
        } catch (const std::exception &ex) {
            fprintf(stderr, "[pflt_slice] warning: failed to parse config payload: %s\n", ex.what());
            fflush(stderr);
            g_last_slice_payload.reset();
        }
    } else {
        g_last_slice_payload.reset();
    }
    return 0;
}

// Slice: model bytes in, gcode out
__attribute__((used)) int orc_slice(const uint8_t* model, int len,
                                   uint8_t** gcode_out, int* gcode_len) {
    ensure_resources_initialized();
    try {
        fprintf(stderr, "[pflt_slice] start len=%d\n", len);
        fflush(stderr);
        // 1) Load model from buffer
        Model preflight_model;
        if (!load_stl_from_buffer(model, len, preflight_model)) {
            fprintf(stderr, "[pflt_slice] load_stl_from_buffer failed\n");
            fflush(stderr);
            return -1; // Failed to load
        }

        if (preflight_model.objects.empty()) {
            fprintf(stderr, "[pflt_slice] model empty\n");
            fflush(stderr);
            return -2; // No objects in model
        }

        if (g_last_slice_payload) {
            apply_model_rotation(preflight_model, *g_last_slice_payload);
        }

        const BoundingBoxf3 bbox = preflight_model.bounding_box_exact();
        const Vec3d dims = bbox.size();
        const double min_dim = std::min({dims.x(), dims.y(), dims.z()});
        if (min_dim > 0.0 && min_dim < 0.5) {
            const double target = 20.0;
            const double scale_factor = target / std::max(min_dim, 1e-3);
            fprintf(stderr, "[pflt_slice] auto-scaling model by %.3fx to reach %.1fmm min dimension\n", scale_factor, target);
            fflush(stderr);
            for (ModelObject *object : preflight_model.objects) {
                if (object != nullptr) {
                    object->scale(scale_factor);
                }
            }
        }

        if (!preflight_model.add_default_instances()) {
            fprintf(stderr, "[pflt_slice] add_default_instances failed\n");
            fflush(stderr);
            return -2;
        }

        // 2) Create print with default config
        DynamicPrintConfig config = get_default_config();
        auto update_object_counts = [&]() {
            int printable_objects = 0;
            int printable_instances = 0;
            for (ModelObject *object : preflight_model.objects) {
                if (object == nullptr) {
                    continue;
                }
                bool has_printable_instance = false;
                for (ModelInstance *instance : object->instances) {
                    if (instance != nullptr && instance->is_printable()) {
                        has_printable_instance = true;
                        ++printable_instances;
                    }
                }
                if (has_printable_instance) {
                    ++printable_objects;
                }
            }
            if (!set_int_option(config, "num_objects", printable_objects)) {
                fprintf(stderr, "[pflt_slice] warning: failed to seed num_objects option (value=%d)\n", printable_objects);
            }
            if (!set_int_option(config, "num_instances", printable_instances)) {
                fprintf(stderr, "[pflt_slice] warning: failed to seed num_instances option (value=%d)\n", printable_instances);
            }
        };
        update_object_counts();
        if (g_last_slice_payload) {
            apply_config_overrides(config, *g_last_slice_payload);
        }
        const bool dump_config = g_dump_config || (std::getenv("ORC_DUMP_CONFIG") != nullptr);
        if (dump_config) {
            log_config(config);
        }
        Print print;
        print.set_status_callback([](const PrintBase::SlicingStatus& status) {
            if (status.percent >= 0) {
                fprintf(stderr, "[pflt_slice] status %d%% %s\n", status.percent, status.text.c_str());
            } else {
                fprintf(stderr, "[pflt_slice] status %s\n", status.text.c_str());
            }
            fflush(stderr);
        });
    fprintf(stderr, "[pflt_slice] applying config\n");
        fflush(stderr);
    log_memory_usage("before apply");
    print.apply(preflight_model, config);
    log_memory_usage("after apply");

        // 3) Process (slice)
        fprintf(stderr, "[pflt_slice] processing\n");
        fflush(stderr);
    log_memory_usage("before process");
    const double process_start_ms = now_ms();
        print.process();
    const double process_ms = now_ms() - process_start_ms;
        fprintf(stderr, "[pflt_slice] process complete\n");
        fflush(stderr);
    log_memory_usage("after process");
    fprintf(stderr, "[pflt_slice] process wall_time_ms=%.2f\n", process_ms);
    fflush(stderr);

        // 4) Generate G-code into a temporary file and read it back
        // preFlight uses GCodeGenerator (PrusaSlicer naming) rather than Orca's GCode class.
        // preFlight also has no multi-plate concept, so no per-plate origin offset to apply.
        GCodeGenerator gcode_generator;
        fprintf(stderr, "[pflt_slice] exporting gcode\n");
        fflush(stderr);
    log_memory_usage("before export");
    const double export_start_ms = now_ms();

        // preFlight's do_export uses memory-based processing — it never writes a file
        // when called without a result pointer. Pass a GCodeProcessorResult so we get
        // back the in-memory GCodeObject's text_buffer().
        const std::string temp_gcode_path = "/tmp/wasm_output.gcode";
        GCodeProcessorResult gcode_result;
        gcode_generator.do_export(&print, temp_gcode_path.c_str(), &gcode_result);
    const double export_ms = now_ms() - export_start_ms;
    fprintf(stderr, "[pflt_slice] export complete wall_time_ms=%.2f\n", export_ms);
    fflush(stderr);
    log_memory_usage("after export");

        std::string gcode_str;
        if (gcode_result.gcode_object) {
            gcode_str = gcode_result.gcode_object->text_buffer();
            fprintf(stderr, "[pflt_slice] retrieved gcode from in-memory object, size=%zu\n", gcode_str.size()); fflush(stderr);
        } else {
            fprintf(stderr, "[pflt_slice] no gcode_object on result; falling back to file read\n"); fflush(stderr);
            FILE* gcode_file = fopen(temp_gcode_path.c_str(), "rb");
            if (gcode_file) {
                if (fseek(gcode_file, 0, SEEK_END) == 0) {
                    long file_length = ftell(gcode_file);
                    if (file_length > 0) {
                        rewind(gcode_file);
                        gcode_str.resize(static_cast<size_t>(file_length));
                        (void)fread(gcode_str.data(), 1, gcode_str.size(), gcode_file);
                    }
                }
                fclose(gcode_file);
                unlink(temp_gcode_path.c_str());
            } else {
                fprintf(stderr, "[pflt_slice] fopen failed for %s errno=%d\n", temp_gcode_path.c_str(), errno); fflush(stderr);
                *gcode_out = nullptr;
                return -3;
            }
        }

        *gcode_len = static_cast<int>(gcode_str.size());
        if (*gcode_len == 0) {
            *gcode_out = nullptr;
            return 0;
        }

        uint8_t* buf = static_cast<uint8_t*>(malloc(gcode_str.size()));
        if (!buf) {
            *gcode_out = nullptr;
            return -3;
        }

        std::memcpy(buf, gcode_str.data(), gcode_str.size());
        *gcode_out = buf;

        return 0; // Success

    } catch (const std::exception& e) {
        fprintf(stderr, "[pflt_slice] exception: %s\n", e.what());
        fflush(stderr);
        return -4; // Exception
    } catch (...) {
        fprintf(stderr, "[pflt_slice] unknown exception\n");
        fflush(stderr);
        return -4; // Exception
    }
}

__attribute__((used)) void orc_free(void* p) {
    free(p);
}

__attribute__((used)) const char* orc_decode_exception(void* exception_ptr)
{
    static std::string last_exception_message;
    if (exception_ptr == nullptr) {
        last_exception_message = "(null exception)";
        return last_exception_message.c_str();
    }

    try {
        const std::exception* ex = reinterpret_cast<const std::exception*>(exception_ptr);
        if (ex != nullptr) {
            last_exception_message = ex->what();
            if (last_exception_message.empty()) {
                last_exception_message = "(empty exception message)";
            }
        } else {
            last_exception_message = "(exception pointer not std::exception)";
        }
    } catch (...) {
        last_exception_message = "(failed to decode exception)";
    }

    return last_exception_message.c_str();
}

}
