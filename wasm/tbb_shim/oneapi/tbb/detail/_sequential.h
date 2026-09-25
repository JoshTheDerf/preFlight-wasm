// Sequential oneTBB replacement for the single-threaded WebAssembly build.
//
// This is NOT a stub: every construct keeps the observable semantics a
// correct TBB program relies on when all work happens to run on one thread.
//
//  * parallel_for / parallel_reduce honour partitioners: simple_partitioner
//    splits the range recursively down to its grain size (TBB guarantees the
//    body never sees a chunk larger than the grain), other partitioners pass
//    the whole range. Chunks are visited in order.
//  * parallel_pipeline runs each token through all stages before pulling the
//    next one (a legal TBB schedule for any filter mode); the input filter's
//    flow_control::stop() ends the pipeline WITHOUT pushing the value it
//    returned downstream. filter<I,O> is type-erased like the real one, so
//    code that stores and composes filters (GCode.cpp) works unmodified.
//  * task_group::run executes the task immediately but defers any exception
//    to wait(), as TBB does.
//  * enumerable_thread_specific / combinable create their single local lazily
//    from the exemplar / finit functor, and clear() really empties them.
//  * concurrent_vector keeps element addresses stable across growth
//    (std::deque storage), like the real container.
//  * Mutexes detect self-deadlock (the only way to block with one thread)
//    and terminate instead of hanging.
//
// Only this shim is used for TBB; no TBB library is linked (see
// cubby-slicer/docs/ENGINE-CONTRACT.md "Build invariants").
#pragma once

#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <limits>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#define TBB_VERSION_MAJOR 2021
#define TBB_VERSION_MINOR 13
#define TBB_VERSION_PATCH 0
#define TBB_INTERFACE_VERSION 12130
#define TBB_SEQUENTIAL_SHIM 1

namespace oneapi {
namespace tbb {

// ---------------------------------------------------------------- ranges
class split {};
class proportional_split {
public:
    proportional_split(std::size_t l, std::size_t r) : m_left(l), m_right(r) {}
    std::size_t left() const { return m_left; }
    std::size_t right() const { return m_right; }
private:
    std::size_t m_left, m_right;
};

template <typename Value>
class blocked_range {
public:
    using const_iterator = Value;
    using size_type = std::size_t;

    blocked_range(Value begin_, Value end_, size_type grainsize_ = 1)
        : my_end(end_), my_begin(begin_), my_grainsize(grainsize_ ? grainsize_ : 1) {}

    // Splitting constructor: `r` keeps the first half, *this takes the second.
    blocked_range(blocked_range &r, split)
        : my_end(r.my_end), my_begin(do_split(r)), my_grainsize(r.my_grainsize) {}
    blocked_range(blocked_range &r, proportional_split &)
        : blocked_range(r, split()) {}

    const_iterator begin() const { return my_begin; }
    const_iterator end() const { return my_end; }
    size_type size() const { return size_type(my_end - my_begin); }
    size_type grainsize() const { return my_grainsize; }
    bool empty() const { return !(my_begin < my_end); }
    bool is_divisible() const { return my_grainsize < size(); }

private:
    static Value do_split(blocked_range &r)
    {
        Value middle = r.my_begin + (r.my_end - r.my_begin) / 2u;
        r.my_end = middle;
        return middle;
    }
    Value my_end;
    Value my_begin;
    size_type my_grainsize;
};

template <typename RowValue, typename ColValue = RowValue>
class blocked_range2d {
public:
    using row_range_type = blocked_range<RowValue>;
    using col_range_type = blocked_range<ColValue>;

    blocked_range2d(RowValue row_begin, RowValue row_end, std::size_t row_grain,
                    ColValue col_begin, ColValue col_end, std::size_t col_grain)
        : my_rows(row_begin, row_end, row_grain), my_cols(col_begin, col_end, col_grain) {}
    blocked_range2d(RowValue row_begin, RowValue row_end, ColValue col_begin, ColValue col_end)
        : my_rows(row_begin, row_end), my_cols(col_begin, col_end) {}
    blocked_range2d(blocked_range2d &r, split) : my_rows(r.my_rows), my_cols(r.my_cols)
    {
        // Split the longer (relative to grain) dimension, like TBB.
        if (my_rows.size() * double(my_cols.grainsize()) < my_cols.size() * double(my_rows.grainsize()))
            my_cols = col_range_type(r.my_cols, split());
        else
            my_rows = row_range_type(r.my_rows, split());
    }

    bool empty() const { return my_rows.empty() || my_cols.empty(); }
    bool is_divisible() const { return my_rows.is_divisible() || my_cols.is_divisible(); }
    const row_range_type &rows() const { return my_rows; }
    const col_range_type &cols() const { return my_cols; }

private:
    row_range_type my_rows;
    col_range_type my_cols;
};

// ------------------------------------------------------------ partitioners
class auto_partitioner {};
class simple_partitioner {};
class static_partitioner {};
class affinity_partitioner {};

class task_group_context {
public:
    enum kind_type { isolated, bound };
    task_group_context(kind_type = bound) {}
    bool cancel_group_execution() { m_cancelled = true; return true; }
    bool is_group_execution_cancelled() const { return m_cancelled; }
    void reset() { m_cancelled = false; }
private:
    bool m_cancelled = false;
};

namespace detail {
template <typename P> struct splits_to_grain : std::false_type {};
template <> struct splits_to_grain<simple_partitioner> : std::true_type {};

// Visit `range` in order, splitting divisible ranges first (simple_partitioner).
template <typename Range, typename Fn>
void visit_split(Range &range, Fn &fn)
{
    if (range.is_divisible()) {
        Range second(range, split());
        visit_split(range, fn);
        visit_split(second, fn);
    } else if (!range.empty()) {
        fn(static_cast<const Range &>(range));
    }
}

template <typename Range, typename Body, typename Partitioner>
void run_for(const Range &range, const Body &body, const Partitioner &)
{
    if constexpr (splits_to_grain<std::decay_t<Partitioner>>::value) {
        Range r(range);
        auto call = [&body](const Range &sub) { body(sub); };
        visit_split(r, call);
    } else {
        if (!range.empty())
            body(range);
    }
}
} // namespace detail

// ------------------------------------------------------------ parallel_for
template <typename Range, typename Body>
void parallel_for(const Range &range, const Body &body)
{ detail::run_for(range, body, auto_partitioner()); }

template <typename Range, typename Body>
void parallel_for(const Range &range, const Body &body, const simple_partitioner &p)
{ detail::run_for(range, body, p); }
template <typename Range, typename Body>
void parallel_for(const Range &range, const Body &body, const auto_partitioner &p)
{ detail::run_for(range, body, p); }
template <typename Range, typename Body>
void parallel_for(const Range &range, const Body &body, const static_partitioner &p)
{ detail::run_for(range, body, p); }
template <typename Range, typename Body>
void parallel_for(const Range &range, const Body &body, affinity_partitioner &p)
{ detail::run_for(range, body, p); }
template <typename Range, typename Body>
void parallel_for(const Range &range, const Body &body, task_group_context &)
{ detail::run_for(range, body, auto_partitioner()); }
template <typename Range, typename Body, typename Partitioner>
void parallel_for(const Range &range, const Body &body, const Partitioner &p, task_group_context &)
{ detail::run_for(range, body, p); }

// Index forms: parallel_for(first, last[, step], f[, partitioner]).
template <typename Index, typename Function,
          typename = std::enable_if_t<std::is_integral_v<Index>>>
void parallel_for(Index first, Index last, const Function &f)
{
    for (Index i = first; i < last; ++i)
        f(i);
}
template <typename Index, typename Function, typename Partitioner,
          typename = std::enable_if_t<std::is_integral_v<Index> &&
                                      (std::is_same_v<std::decay_t<Partitioner>, simple_partitioner> ||
                                       std::is_same_v<std::decay_t<Partitioner>, auto_partitioner> ||
                                       std::is_same_v<std::decay_t<Partitioner>, static_partitioner> ||
                                       std::is_same_v<std::decay_t<Partitioner>, affinity_partitioner>)>>
void parallel_for(Index first, Index last, const Function &f, Partitioner &&)
{
    for (Index i = first; i < last; ++i)
        f(i);
}
template <typename Index, typename Function,
          typename = std::enable_if_t<std::is_integral_v<Index>>, typename = void>
void parallel_for(Index first, Index last, Index step, const Function &f)
{
    if (step <= 0)
        throw std::invalid_argument("tbb::parallel_for: step must be positive");
    for (Index i = first; i < last; i += step)
        f(i);
}

// -------------------------------------------------------- parallel_for_each
template <typename Item> class feeder {
public:
    explicit feeder(std::deque<Item> &q) : m_q(q) {}
    void add(const Item &item) { m_q.push_back(item); }
    void add(Item &&item) { m_q.push_back(std::move(item)); }
private:
    std::deque<Item> &m_q;
};

template <typename InputIterator, typename Body>
void parallel_for_each(InputIterator first, InputIterator last, const Body &body)
{
    using Item = typename std::iterator_traits<InputIterator>::value_type;
    if constexpr (std::is_invocable_v<const Body &, decltype(*first), feeder<Item> &>) {
        std::deque<Item> extra;
        feeder<Item> fd(extra);
        for (; first != last; ++first)
            body(*first, fd);
        while (!extra.empty()) {
            Item it = std::move(extra.front());
            extra.pop_front();
            body(it, fd);
        }
    } else {
        for (; first != last; ++first)
            body(*first);
    }
}
template <typename Container, typename Body>
void parallel_for_each(Container &c, const Body &body)
{ parallel_for_each(std::begin(c), std::end(c), body); }
template <typename Container, typename Body>
void parallel_for_each(const Container &c, const Body &body)
{ parallel_for_each(std::begin(c), std::end(c), body); }

// ----------------------------------------------------------- parallel_invoke
template <typename... Fs>
void parallel_invoke(Fs &&...fs)
{
    (static_cast<void>(std::forward<Fs>(fs)()), ...);
}

// ---------------------------------------------------------- parallel_reduce
namespace detail {
template <typename T> struct is_partitioner : std::false_type {};
template <> struct is_partitioner<simple_partitioner> : std::true_type {};
template <> struct is_partitioner<auto_partitioner> : std::true_type {};
template <> struct is_partitioner<static_partitioner> : std::true_type {};
template <> struct is_partitioner<affinity_partitioner> : std::true_type {};

template <typename Range, typename Value, typename RealBody, typename Reduction, typename Partitioner>
Value run_reduce(const Range &range, const Value &identity, const RealBody &real_body,
                 const Reduction &reduction, const Partitioner &)
{
    if constexpr (splits_to_grain<std::decay_t<Partitioner>>::value) {
        Range r(range);
        std::optional<Value> acc;
        auto call = [&](const Range &sub) {
            Value part = real_body(sub, identity);
            acc = acc ? Value(reduction(*acc, part)) : std::move(part);
        };
        visit_split(r, call);
        return acc ? std::move(*acc) : identity;
    } else {
        (void) reduction;
        if (range.empty())
            return identity;
        return real_body(range, identity);
    }
}

template <typename Range, typename Body, typename Partitioner>
void run_reduce_imperative(const Range &range, Body &body, const Partitioner &)
{
    if constexpr (splits_to_grain<std::decay_t<Partitioner>>::value) {
        Range r(range);
        auto call = [&body](const Range &sub) { body(sub); };
        visit_split(r, call);
    } else {
        if (!range.empty())
            body(range);
    }
}
} // namespace detail

// Functional form.
template <typename Range, typename Value, typename RealBody, typename Reduction>
Value parallel_reduce(const Range &range, const Value &identity, const RealBody &real_body, const Reduction &reduction)
{ return detail::run_reduce(range, identity, real_body, reduction, auto_partitioner()); }

template <typename Range, typename Value, typename RealBody, typename Reduction, typename Partitioner,
          typename = std::enable_if_t<detail::is_partitioner<std::decay_t<Partitioner>>::value>>
Value parallel_reduce(const Range &range, const Value &identity, const RealBody &real_body,
                      const Reduction &reduction, Partitioner &&p)
{ return detail::run_reduce(range, identity, real_body, reduction, p); }

// Imperative form: body(range) accumulates into body; join is never needed.
template <typename Range, typename Body>
void parallel_reduce(const Range &range, Body &body)
{ detail::run_reduce_imperative(range, body, auto_partitioner()); }

template <typename Range, typename Body, typename Partitioner,
          typename = std::enable_if_t<detail::is_partitioner<std::decay_t<Partitioner>>::value>>
void parallel_reduce(const Range &range, Body &body, Partitioner &&p)
{ detail::run_reduce_imperative(range, body, p); }

template <typename Range, typename Value, typename RealBody, typename Reduction>
Value parallel_deterministic_reduce(const Range &range, const Value &identity, const RealBody &real_body,
                                    const Reduction &reduction)
{ return detail::run_reduce(range, identity, real_body, reduction, simple_partitioner()); }

// -------------------------------------------------------------- pipeline
enum class filter_mode { parallel = 1, serial_in_order = 3, serial_out_of_order = 2 };

class flow_control {
public:
    void stop() { m_stopped = true; }
    bool is_stopped() const { return m_stopped; }
private:
    bool m_stopped = false;
};

namespace detail {
struct unit {};
template <typename T> using token_t = std::conditional_t<std::is_void_v<T>, unit, T>;
template <typename I, typename O> struct filter_fn { using type = std::function<O(I)>; };
template <typename O> struct filter_fn<void, O> { using type = std::function<std::optional<O>(flow_control &)>; };
} // namespace detail

// Type-erased filter chain. For Input = void the chain is a source: it
// returns std::nullopt once the first stage called flow_control::stop().
template <typename Input, typename Output>
class filter {
    using Out = detail::token_t<Output>;
public:
    using Fn = typename detail::filter_fn<Input, Out>::type;
    filter() = default;
    filter(filter_mode mode, Fn fn) : m_mode(mode), m_fn(std::move(fn)) {}
    explicit operator bool() const { return bool(m_fn); }
    void clear() { m_fn = nullptr; }

    // Internal API used by composition and parallel_pipeline.
    const Fn &fn() const { return m_fn; }
    filter_mode mode() const { return m_mode; }

private:
    filter_mode m_mode = filter_mode::serial_in_order;
    Fn m_fn;
};

template <typename Input, typename Output, typename Body>
filter<Input, Output> make_filter(filter_mode mode, const Body &body)
{
    using Out = detail::token_t<Output>;
    if constexpr (std::is_void_v<Input>) {
        return filter<Input, Output>(mode, [body](flow_control &fc) -> std::optional<Out> {
            if constexpr (std::is_void_v<Output>) {
                body(fc);
                if (fc.is_stopped())
                    return std::nullopt;
                return Out{};
            } else {
                Out v = body(fc);
                if (fc.is_stopped())
                    return std::nullopt; // the value returned alongside stop() is discarded
                return std::optional<Out>(std::move(v));
            }
        });
    } else {
        return filter<Input, Output>(mode, [body](Input in) -> Out {
            if constexpr (std::is_void_v<Output>) {
                body(std::move(in));
                return Out{};
            } else {
                return body(std::move(in));
            }
        });
    }
}

template <typename T, typename V, typename U>
filter<T, U> operator&(const filter<T, V> &left, const filter<V, U> &right)
{
    static_assert(!std::is_void_v<V>, "sequential tbb shim: void token between stages is not supported");
    using Out = detail::token_t<U>;
    auto lf = left.fn();
    auto rf = right.fn();
    if constexpr (std::is_void_v<T>) {
        return filter<T, U>(left.mode(), [lf, rf](flow_control &fc) -> std::optional<Out> {
            std::optional<V> mid = lf(fc);
            if (!mid)
                return std::nullopt;
            return std::optional<Out>(rf(std::move(*mid)));
        });
    } else {
        return filter<T, U>(left.mode(), [lf, rf](T in) -> Out { return rf(lf(std::move(in))); });
    }
}

inline void parallel_pipeline(std::size_t max_number_of_live_tokens, const filter<void, void> &f)
{
    if (max_number_of_live_tokens == 0)
        throw std::invalid_argument("tbb::parallel_pipeline: max_number_of_live_tokens must be > 0");
    if (!f)
        return;
    flow_control fc;
    while (f.fn()(fc)) {
    }
}
inline void parallel_pipeline(std::size_t n, const filter<void, void> &f, task_group_context &)
{ parallel_pipeline(n, f); }

// ------------------------------------------------------------ task_group
enum task_group_status { not_complete, complete, canceled };

class task_group {
public:
    task_group() = default;
    explicit task_group(task_group_context &) {}
    task_group(const task_group &) = delete;
    task_group &operator=(const task_group &) = delete;
    ~task_group() = default;

    template <typename F> void run(F &&f)
    {
        if (m_cancelled)
            return;
        try {
            std::forward<F>(f)();
        } catch (...) {
            if (!m_exception)
                m_exception = std::current_exception();
            m_cancelled = true;
        }
    }
    template <typename F> task_group_status run_and_wait(F &&f)
    {
        run(std::forward<F>(f));
        return wait();
    }
    task_group_status wait()
    {
        bool was_cancelled = m_cancelled;
        m_cancelled = false;
        if (m_exception) {
            std::exception_ptr e = std::move(m_exception);
            m_exception = nullptr;
            std::rethrow_exception(e);
        }
        return was_cancelled ? canceled : complete;
    }
    void cancel() { m_cancelled = true; }
    bool is_canceling() const { return m_cancelled; }

private:
    std::exception_ptr m_exception;
    bool m_cancelled = false;
};

// ------------------------------------------------- arenas / global control
class task_arena {
public:
    static constexpr int automatic = -1;
    explicit task_arena(int = automatic, unsigned = 1) {}
    void initialize() {}
    void initialize(int, unsigned = 1) {}
    void terminate() {}
    bool is_active() const { return true; }
    int max_concurrency() const { return 1; }
    template <typename F> decltype(auto) execute(F &&f) { return std::forward<F>(f)(); }
    template <typename F> void enqueue(F &&f) { std::forward<F>(f)(); }
};

namespace this_task_arena {
inline int max_concurrency() { return 1; }
inline int current_thread_index() { return 0; }
template <typename F> decltype(auto) isolate(F &&f) { return std::forward<F>(f)(); }
} // namespace this_task_arena

class global_control {
public:
    enum parameter { max_allowed_parallelism, thread_stack_size, terminate_on_exception, parameter_max };
    global_control(parameter, std::size_t) {}
    ~global_control() = default;
    static std::size_t active_value(parameter p)
    {
        switch (p) {
        case max_allowed_parallelism: return 1;
        case thread_stack_size: return 64u << 20;
        default: return 0;
        }
    }
};

class task_scheduler_init {
public:
    static constexpr int automatic = -1;
    static constexpr int deferred = -2;
    explicit task_scheduler_init(int = automatic, std::size_t = 0) {}
    void initialize(int = automatic) {}
    void terminate() {}
    bool is_active() const { return true; }
    static int default_num_threads() { return 1; }
};

inline int info_default_concurrency() { return 1; }
namespace info {
inline int default_concurrency() { return 1; }
} // namespace info

// There are no worker threads, so observers never see a scheduler entry.
class task_scheduler_observer {
public:
    task_scheduler_observer() = default;
    explicit task_scheduler_observer(task_arena &) {}
    virtual ~task_scheduler_observer() = default;
    void observe(bool state = true) { m_observing = state; }
    bool is_observing() const { return m_observing; }
    virtual void on_scheduler_entry(bool /*is_worker*/) {}
    virtual void on_scheduler_exit(bool /*is_worker*/) {}
private:
    bool m_observing = false;
};

// --------------------------------------------------------------- mutexes
namespace detail {
class checked_mutex {
public:
    checked_mutex() = default;
    checked_mutex(const checked_mutex &) = delete;
    checked_mutex &operator=(const checked_mutex &) = delete;
    void lock()
    {
        if (m_locked)
            std::terminate(); // self-deadlock: would hang forever with one thread
        m_locked = true;
    }
    bool try_lock()
    {
        if (m_locked)
            return false;
        m_locked = true;
        return true;
    }
    void unlock() { m_locked = false; }
    // Reader/writer API (spin_rw_mutex / queuing_rw_mutex).
    void lock_shared() { lock(); }
    bool try_lock_shared() { return try_lock(); }
    void unlock_shared() { unlock(); }

private:
    bool m_locked = false;
};

template <typename M> class scoped_lock_base {
public:
    scoped_lock_base() = default;
    explicit scoped_lock_base(M &m, bool = true) { acquire(m); }
    scoped_lock_base(const scoped_lock_base &) = delete;
    scoped_lock_base &operator=(const scoped_lock_base &) = delete;
    ~scoped_lock_base() { release(); }
    void acquire(M &m, bool = true)
    {
        release();
        m.lock();
        m_mutex = &m;
    }
    bool try_acquire(M &m, bool = true)
    {
        release();
        if (!m.try_lock())
            return false;
        m_mutex = &m;
        return true;
    }
    bool upgrade_to_writer() { return true; }
    bool downgrade_to_reader() { return true; }
    void release()
    {
        if (m_mutex) {
            m_mutex->unlock();
            m_mutex = nullptr;
        }
    }

private:
    M *m_mutex = nullptr;
};
} // namespace detail

#define TBB_SHIM_MUTEX(NAME)                                                      \
    class NAME : public detail::checked_mutex {                                   \
    public:                                                                       \
        using scoped_lock = detail::scoped_lock_base<NAME>;                       \
        static constexpr bool is_rw_mutex = false;                                \
        static constexpr bool is_recursive_mutex = false;                         \
        static constexpr bool is_fair_mutex = false;                              \
    };
TBB_SHIM_MUTEX(spin_mutex)
TBB_SHIM_MUTEX(mutex)
TBB_SHIM_MUTEX(queuing_mutex)
TBB_SHIM_MUTEX(speculative_spin_mutex)
TBB_SHIM_MUTEX(spin_rw_mutex)
TBB_SHIM_MUTEX(queuing_rw_mutex)
TBB_SHIM_MUTEX(rw_mutex)
#undef TBB_SHIM_MUTEX

class recursive_mutex {
public:
    using scoped_lock = detail::scoped_lock_base<recursive_mutex>;
    void lock() { ++m_depth; }
    bool try_lock() { ++m_depth; return true; }
    void unlock() { if (m_depth) --m_depth; }
private:
    std::size_t m_depth = 0;
};

// ------------------------------------------------------ thread-local storage
enum ets_key_usage_type { ets_key_per_instance, ets_no_key, ets_suspend_aware };

template <typename T, typename Allocator = std::allocator<T>, ets_key_usage_type = ets_no_key>
class enumerable_thread_specific {
    using storage_t = std::deque<T>; // at most one element; deque never relocates it
public:
    using value_type = T;
    using reference = T &;
    using const_reference = const T &;
    using pointer = T *;
    using size_type = std::size_t;
    using iterator = typename storage_t::iterator;
    using const_iterator = typename storage_t::const_iterator;
    using range_type = blocked_range<iterator>;
    using const_range_type = blocked_range<const_iterator>;

    enumerable_thread_specific() : m_make([] { return T(); }) {}

    template <typename Finit,
              typename = std::enable_if_t<std::is_invocable_r_v<T, std::decay_t<Finit> &> &&
                                          !std::is_same_v<std::decay_t<Finit>, T> &&
                                          !std::is_same_v<std::decay_t<Finit>, enumerable_thread_specific>>>
    explicit enumerable_thread_specific(Finit finit)
        : m_make([f = std::move(finit)]() mutable -> T { return f(); }) {}

    explicit enumerable_thread_specific(const T &exemplar) : m_make([exemplar] { return exemplar; }) {}
    explicit enumerable_thread_specific(T &&exemplar)
        : m_make([ex = std::make_shared<T>(std::move(exemplar))] { return T(*ex); }) {}

    template <typename P1, typename... P,
              typename = std::enable_if_t<!std::is_invocable_v<std::decay_t<P1> &> &&
                                          !std::is_same_v<std::decay_t<P1>, T> &&
                                          !std::is_same_v<std::decay_t<P1>, enumerable_thread_specific>>>
    enumerable_thread_specific(P1 &&p1, P &&...args)
        : m_make([tup = std::make_tuple(std::forward<P1>(p1), std::forward<P>(args)...)]() -> T {
              return std::make_from_tuple<T>(tup);
          }) {}

    enumerable_thread_specific(const enumerable_thread_specific &o) : m_make(o.m_make), m_values(o.m_values) {}
    enumerable_thread_specific(enumerable_thread_specific &&o) = default;
    enumerable_thread_specific &operator=(const enumerable_thread_specific &o)
    {
        if (this != &o) { m_make = o.m_make; m_values = o.m_values; }
        return *this;
    }
    enumerable_thread_specific &operator=(enumerable_thread_specific &&o) = default;

    reference local()
    {
        bool exists;
        return local(exists);
    }
    reference local(bool &exists)
    {
        exists = !m_values.empty();
        if (!exists)
            m_values.emplace_back(m_make());
        return m_values.front();
    }

    size_type size() const { return m_values.size(); }
    bool empty() const { return m_values.empty(); }
    void clear() { m_values.clear(); }

    iterator begin() { return m_values.begin(); }
    iterator end() { return m_values.end(); }
    const_iterator begin() const { return m_values.begin(); }
    const_iterator end() const { return m_values.end(); }
    range_type range(std::size_t grainsize = 1) { return range_type(begin(), end(), grainsize); }
    const_range_type range(std::size_t grainsize = 1) const { return const_range_type(begin(), end(), grainsize); }

    template <typename Combine> T combine(Combine f_combine)
    {
        if (m_values.empty())
            return m_make();
        T result = m_values.front();
        for (auto it = std::next(m_values.begin()); it != m_values.end(); ++it)
            result = f_combine(result, *it);
        return result;
    }
    template <typename Combine> void combine_each(Combine f_combine)
    {
        for (auto &v : m_values)
            f_combine(v);
    }

private:
    std::function<T()> m_make;
    storage_t m_values;
};

template <typename T> class combinable {
public:
    combinable() = default;
    template <typename Finit> explicit combinable(Finit finit) : m_ets(std::move(finit)) {}
    void clear() { m_ets.clear(); }
    T &local() { return m_ets.local(); }
    T &local(bool &exists) { return m_ets.local(exists); }
    template <typename Combine> T combine(Combine f) { return m_ets.combine(f); }
    template <typename Combine> void combine_each(Combine f) { m_ets.combine_each(f); }
private:
    enumerable_thread_specific<T> m_ets;
};

// ------------------------------------------------------------ containers
// concurrent_vector never relocates elements on growth; std::deque gives the
// same guarantee (std::vector would not).
template <typename T, typename Allocator = std::allocator<T>>
class concurrent_vector : public std::deque<T, Allocator> {
    using base = std::deque<T, Allocator>;
public:
    using base::base;
    using typename base::iterator;
    using typename base::size_type;

    iterator push_back(const T &v) { base::push_back(v); return std::prev(base::end()); }
    iterator push_back(T &&v) { base::push_back(std::move(v)); return std::prev(base::end()); }
    template <typename... Args> iterator emplace_back(Args &&...args)
    {
        base::emplace_back(std::forward<Args>(args)...);
        return std::prev(base::end());
    }
    iterator grow_by(size_type n)
    {
        size_type old = base::size();
        base::resize(old + n);
        return base::begin() + old;
    }
    iterator grow_by(size_type n, const T &v)
    {
        size_type old = base::size();
        base::resize(old + n, v);
        return base::begin() + old;
    }
    iterator grow_to_at_least(size_type n)
    {
        if (base::size() < n)
            base::resize(n);
        return base::begin() + n;
    }
    void reserve(size_type) {}
    size_type capacity() const { return base::size(); }
    blocked_range<iterator> range(std::size_t grainsize = 1) { return {base::begin(), base::end(), grainsize}; }
};

template <typename Key, typename T, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>,
          typename Allocator = std::allocator<std::pair<const Key, T>>>
class concurrent_unordered_map : public std::unordered_map<Key, T, Hash, KeyEqual, Allocator> {
    using base = std::unordered_map<Key, T, Hash, KeyEqual, Allocator>;
public:
    using base::base;
    template <typename K> auto unsafe_erase(const K &k) { return base::erase(k); }
    auto unsafe_erase(typename base::const_iterator it) { return base::erase(it); }
};

template <typename Key, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>,
          typename Allocator = std::allocator<Key>>
class concurrent_unordered_set : public std::unordered_set<Key, Hash, KeyEqual, Allocator> {
    using base = std::unordered_set<Key, Hash, KeyEqual, Allocator>;
public:
    using base::base;
    template <typename K> auto unsafe_erase(const K &k) { return base::erase(k); }
    auto unsafe_erase(typename base::const_iterator it) { return base::erase(it); }
};

// ------------------------------------------------------------ allocators
template <typename T> class scalable_allocator {
public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using propagate_on_container_move_assignment = std::true_type;
    using is_always_equal = std::true_type;
    template <typename U> struct rebind { using other = scalable_allocator<U>; };

    scalable_allocator() noexcept = default;
    template <typename U> scalable_allocator(const scalable_allocator<U> &) noexcept {}

    T *allocate(std::size_t n)
    {
        if (n > std::numeric_limits<std::size_t>::max() / sizeof(T))
            throw std::bad_array_new_length();
        return std::allocator<T>().allocate(n);
    }
    void deallocate(T *p, std::size_t n) noexcept { std::allocator<T>().deallocate(p, n); }
};
template <typename T, typename U>
bool operator==(const scalable_allocator<T> &, const scalable_allocator<U> &) { return true; }
template <typename T, typename U>
bool operator!=(const scalable_allocator<T> &, const scalable_allocator<U> &) { return false; }

template <typename T> using cache_aligned_allocator = scalable_allocator<T>;
template <typename T> using tbb_allocator = scalable_allocator<T>;

} // namespace tbb
} // namespace oneapi

namespace tbb = oneapi::tbb;
