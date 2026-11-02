// Small deterministic PRNG (xorshift32) so every simulation run is reproducible from its seed.
#pragma once
#include <cstdint>

namespace owr {

class Rng {
    uint32_t s_;
public:
    explicit Rng(uint32_t seed = 42) : s_(seed ? seed : 0x9E3779B9u) { for (int i = 0; i < 4; ++i) next(); }
    uint32_t next() { s_ ^= s_ << 13; s_ ^= s_ >> 17; s_ ^= s_ << 5; return s_; }
    // Uniform integer in [lo, hi] (inclusive).
    int range(int lo, int hi) { return hi <= lo ? lo : lo + (int)(next() % (uint32_t)(hi - lo + 1)); }
    double unit() { return (next() & 0xFFFFFF) / 16777216.0; }
};

}  // namespace owr
