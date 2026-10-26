// Secret-handling primitives: memory wiping and containers that wipe on free.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <monocypher.h>

namespace cv {

inline void wipe(void* p, size_t n) {
    if (p && n) crypto_wipe(p, n);
}

// Allocator that zeroes memory before releasing it. Used for every container
// that may hold secrets, so reallocations never leave plaintext in the heap.
template <typename T>
struct WipingAllocator {
    using value_type = T;
    WipingAllocator() noexcept = default;
    template <typename U>
    WipingAllocator(const WipingAllocator<U>&) noexcept {}

    T* allocate(size_t n) { return std::allocator<T>{}.allocate(n); }
    void deallocate(T* p, size_t n) noexcept {
        wipe(p, n * sizeof(T));
        std::allocator<T>{}.deallocate(p, n);
    }
    template <typename U>
    bool operator==(const WipingAllocator<U>&) const noexcept { return true; }
    template <typename U>
    bool operator!=(const WipingAllocator<U>&) const noexcept { return false; }
};

using SString = std::basic_string<char, std::char_traits<char>, WipingAllocator<char>>;
using SBytes = std::vector<uint8_t, WipingAllocator<uint8_t>>;
template <typename T>
using SVec = std::vector<T, WipingAllocator<T>>;

// Overwrite the whole buffer (the small-string inline buffer and any bytes
// past size() left by erase/pop_back, not just the live contents), then clear.
inline void wipe(SString& s) {
    s.resize(s.capacity());  // never reallocates: n <= capacity
    if (!s.empty()) wipe(&s[0], s.size());
    s.clear();
    s.shrink_to_fit();
}
inline void wipe(SBytes& b) {
    b.resize(b.capacity());
    wipe(b.data(), b.size());
    b.clear();
    b.shrink_to_fit();
}

// Heap-friendly holder for a secret string that is shared between callbacks
// (e.g. via std::make_shared). Wipes the value, including any inline
// small-string buffer, when the last owner goes away.
struct SecretBox {
    SString value;
    SecretBox() = default;
    explicit SecretBox(const SString& v) : value(v) {}
    SecretBox(const SecretBox&) = delete;
    SecretBox& operator=(const SecretBox&) = delete;
    ~SecretBox() { wipe(value); }
};

// Fixed-size secret that wipes itself when it goes out of scope.
template <size_t N>
struct SecretKey {
    uint8_t bytes[N] = {};
    SecretKey() = default;
    SecretKey(const SecretKey&) = delete;
    SecretKey& operator=(const SecretKey&) = delete;
    ~SecretKey() { wipe(bytes, N); }
    uint8_t* data() { return bytes; }
    const uint8_t* data() const { return bytes; }
    static constexpr size_t size() { return N; }
};

using Key32 = SecretKey<32>;

}  // namespace cv
