#pragma once
// MurmurHash3 (external library) wrapped into a 64-bit hash.
#include <cstdint>
#include <string_view>

#include <MurmurHash3.h>

inline std::uint64_t murmur64(std::string_view data, std::uint32_t seed = 0) {
    std::uint64_t out[2];
    MurmurHash3_x64_128(data.data(), static_cast<int>(data.size()), seed, out);
    return out[0];  // lower 64 of the 128-bit result
}
