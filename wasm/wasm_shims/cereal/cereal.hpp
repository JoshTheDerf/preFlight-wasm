#pragma once
// Minimal cereal shim for WASM builds.
// Provides stubs for the subset of the API exercised by the bridge build.

#include <cstddef>
#include <utility>

namespace cereal {

// --- Binary blob wrapper -------------------------------------------------
template <typename T>
struct BinaryData {
    T data;
    std::size_t size;
};
template <typename T>
inline BinaryData<T> binary_data(T data, std::size_t size) { return {data, size}; }


// --- Archive stubs -------------------------------------------------------

class BinaryOutputArchive {
public:
    template <typename T>
    BinaryOutputArchive& operator&(const T&) { return *this; }

    template <typename T>
    BinaryOutputArchive& operator()(const T&) { return *this; }
};

class BinaryInputArchive {
public:
    template <typename T>
    BinaryInputArchive& operator&(T&) { return *this; }

    template <typename T>
    BinaryInputArchive& operator()(T&) { return *this; }
};

// --- Access helpers ------------------------------------------------------

class access {
public:
    template <class Archive, class T>
    static void serialize(Archive&, T&) {}

    template <class Archive, class T>
    static void save(Archive&, const T&) {}

    template <class Archive, class T>
    static void load(Archive&, T&) {}
};

template <class T>
class construct {
    T instance{};

public:
    template <class... Args>
    void operator()(Args&&... args) {
        instance = T(std::forward<Args>(args)...);
    }

    T* ptr() { return &instance; }
};

namespace specialization {
enum specialization_type {
    member_load_save,
    member_serialize,
    non_member_load_save,
    non_member_serialize,
    member_load_save_minimal,
    non_member_load_save_minimal,
    member_serialize_minimal,
    non_member_serialize_minimal,
    not_specialized
};
}  // namespace specialization

template <class Archive, class T,
                    specialization::specialization_type Spec = specialization::not_specialized>
struct specialize {
    static constexpr specialization::specialization_type value = Spec;
};

// --- Macros --------------------------------------------------------------

#define CEREAL_NVP(name, value) value
#define CEREAL_SERIALIZE_FUNCTION_NAME serialize

}  // namespace cereal

#define CEREAL_CLASS_VERSION(cls, version)
#define CEREAL_REGISTER_TYPE(type)
#define CEREAL_REGISTER_POLYMORPHIC_RELATION(base, derived)