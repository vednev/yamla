#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>

// ------------------------------------------------------------
//  DimensionMask — packed uint64 bitmask for one filter dimension
//
//  Bit i = 1 means entry i passes this dimension's filter.
//  all_pass = true means no filter active (skip during AND).
//  Word index = i / 64, bit index = i % 64.
// ------------------------------------------------------------
struct DimensionMask {
    std::vector<uint64_t> bits;  // bit i = entry i passes this dimension
    bool all_pass = true;        // skip AND when no filter on this dimension

    void resize(size_t n_entries) {
        bits.assign((n_entries + 63) / 64, ~uint64_t(0)); // all pass initially
        all_pass = true;
    }

    void clear_all() {
        bits.assign(bits.size(), ~uint64_t(0));
        all_pass = true;
    }

    void set(size_t idx, bool pass) {
        size_t w = idx / 64, b = idx % 64;
        if (pass) bits[w] |=  (uint64_t(1) << b);
        else      bits[w] &= ~(uint64_t(1) << b);
    }

    bool test(size_t idx) const {
        return (bits[idx / 64] >> (idx % 64)) & 1;
    }
};

// AND all active dimension bitmasks into a combined result.
// Returns packed bitmask; caller iterates set bits to build filtered_indices_.
inline std::vector<uint64_t> and_masks(
    const std::vector<const DimensionMask*>& dims,
    size_t word_count)
{
    std::vector<uint64_t> combined(word_count, ~uint64_t(0));
    for (const DimensionMask* d : dims) {
        if (!d || d->all_pass) continue;
        for (size_t w = 0; w < word_count; ++w)
            combined[w] &= d->bits[w];
    }
    return combined;
}
