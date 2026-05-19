#pragma once
// Minimal stub for tbb::enumerable_thread_specific in the WASM build.
// Single-threaded: degenerates to a one-element vector.

#include <vector>
#include <functional>
#include <type_traits>
#include <utility>

namespace oneapi { namespace tbb {

template <typename T>
class enumerable_thread_specific {
public:
    using iterator = typename std::vector<T>::iterator;
    using const_iterator = typename std::vector<T>::const_iterator;
    using reference = T&;
    using const_reference = const T&;

    enumerable_thread_specific() : m_values(1) {}
    explicit enumerable_thread_specific(const T &init) : m_values(1, init) {}
    template <typename Init>
    explicit enumerable_thread_specific(Init &&init,
        typename std::enable_if<!std::is_same<typename std::decay<Init>::type, T>::value, int>::type = 0)
        : m_initializer(std::forward<Init>(init)), m_values(1, m_initializer ? m_initializer() : T{}) {}

    T &local() { return m_values.front(); }
    T &local(bool &existed) { existed = true; return m_values.front(); }
    const T &local() const { return m_values.front(); }

    iterator begin() { return m_values.begin(); }
    iterator end()   { return m_values.end(); }
    const_iterator begin() const { return m_values.begin(); }
    const_iterator end()   const { return m_values.end(); }

    size_t size() const { return m_values.size(); }
    bool empty() const  { return m_values.empty(); }
    void clear()        { m_values.assign(1, T{}); }

    template <typename BinaryFunctor>
    T combine(BinaryFunctor /*f*/) const { return m_values.front(); }

private:
    std::function<T()> m_initializer;
    std::vector<T> m_values;
};

}} // namespace oneapi::tbb

#ifndef ORCA_WASM_TBB_ALIAS_DEFINED
#define ORCA_WASM_TBB_ALIAS_DEFINED
namespace tbb = oneapi::tbb;
#endif
