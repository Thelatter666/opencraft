#pragma once

#include <cstdint>
#include <memory>

namespace opencraft::noise {

// splitmix64 finalizer — the one hash used for seed derivation and position
// hashing. Pure integer math, bit-identical on every platform.
[[nodiscard]] std::uint64_t mix64(std::uint64_t value);

// Sampler configuration. Output range of both sample functions is [-1, 1]
// (approximately; fBm/ridged stacking stays within that envelope).
struct SamplerParams {
    float frequency = 1.0f;
    int octaves = 1;
    float lacunarity = 2.0f;
    float gain = 0.5f;
    bool ridged = false; // ridged multifractal instead of fBm
};

// Thin deterministic wrapper around FastNoiseLite (v1.1.1, MIT,
// https://github.com/Auburn/FastNoiseLite) with fBm / ridged fractal stacking
// (docs/02 §2: OpenSimplex2 为主 + 多倍频叠加).
//
// Only FastNoiseLite's const sampling APIs are used; an instance carries no
// mutable state after construction, so sample2/sample3 are thread-safe and
// reproducible run to run. FastNoiseLite seeds internally with a 32-bit
// integer, so the 64-bit world seed is folded down deterministically (see
// noise_sampler.cpp) — callers pass full 64-bit seeds.
class NoiseSampler {
public:
    NoiseSampler() = default;
    NoiseSampler(std::uint64_t seed64, const SamplerParams &params);
    ~NoiseSampler();
    NoiseSampler(NoiseSampler &&other) noexcept;
    NoiseSampler &operator=(NoiseSampler &&other) noexcept;

    [[nodiscard]] float sample2(float x, float y) const;
    [[nodiscard]] float sample3(float x, float y, float z) const;

private:
    struct Impl; // hides FastNoiseLite.h from consumers
    std::unique_ptr<Impl> impl_;
};

} // namespace opencraft::noise
