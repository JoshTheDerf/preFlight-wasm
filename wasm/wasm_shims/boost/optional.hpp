#pragma once

#if defined(__has_include_next)
#  if __has_include_next(<boost/optional.hpp>)
#    include_next <boost/optional.hpp>
#    define ORCA_WASM_USE_SYSTEM_BOOST_OPTIONAL
#  endif
#endif

#ifndef ORCA_WASM_USE_SYSTEM_BOOST_OPTIONAL
#include <type_traits>
#include <utility>

namespace boost {

struct none_t {
	struct init_tag {};
	explicit constexpr none_t(int) {}
};
static constexpr none_t none{0};

template <class T, bool IsRef = std::is_reference<T>::value>
class optional;

// reference specialization: store pointer to the bound object
template <class T>
class optional<T, true> {
	using Raw = typename std::remove_reference<T>::type;
	const Raw* ptr = nullptr;

public:
	optional() = default;
	optional(none_t) {}
	optional(const T value) : ptr(&value) {}

	bool has_value() const { return ptr != nullptr; }
	explicit operator bool() const { return has_value(); }

	const Raw& operator*() const { return *ptr; }
	const Raw& value() const { return *ptr; }
	const Raw* operator->() const { return ptr; }
};

// value specialization
template <class T>
class optional<T, false> {
	bool engaged = false;
	T storage{};

public:
	optional() = default;
	optional(none_t) : engaged(false) {}
	optional(const T& v) : engaged(true), storage(v) {}
	optional(T&& v) : engaged(true), storage(std::move(v)) {}

	template <class... Args>
	explicit optional(Args&&... args)
		: engaged(true), storage(std::forward<Args>(args)...) {}

	bool has_value() const { return engaged; }
	explicit operator bool() const { return has_value(); }

	const T& operator*() const { return storage; }
	T& operator*() { return storage; }

	const T& value() const { return storage; }
	T& value() { return storage; }

	const T* operator->() const { return &storage; }
	T* operator->() { return &storage; }
};

template <class T>
optional<typename std::decay<T>::type> make_optional(T&& value) {
	return optional<typename std::decay<T>::type>(std::forward<T>(value));
}

}  // namespace boost
#endif