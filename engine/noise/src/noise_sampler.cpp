#include "opencraft/noise/noise_sampler.hpp"

#include <FastNoiseLite.h>

namespace opencraft::noise {

std::uint64_t mix64(std::uint64_t value) {
    value += 0x9E3779B97F4A7C15ULL; // golden-ratio increment (splitmix64)
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31);
}

struct NoiseSampler::Impl {
    FastNoiseLite noise;

    Impl(std::uint64_t seed64, const SamplerParams &params) {
        // FastNoiseLite's seed slot is 32-bit (SetSeed(int)); fold the 64-bit
        // world seed through mix64 first so different 64-bit seeds do not
        // collide in the low 32 bits unnoticed.
        noise.SetSeed(static_cast<int>(static_cast<std::uint32_t>(mix64(seed64))));
        noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        noise.SetFrequency(params.frequency);
        if (params.octaves > 1) {
            noise.SetFractalType(params.ridged ? FastNoiseLite::FractalType_Ridged : FastNoiseLite::FractalType_FBm);
            noise.SetFractalOctaves(params.octaves);
            noise.SetFractalLacunarity(params.lacunarity);
            noise.SetFractalGain(params.gain);
        }
    }
};

NoiseSampler::NoiseSampler(std::uint64_t seed64, const SamplerParams &params)
    : impl_(std::make_unique<Impl>(seed64, params)) {
}

NoiseSampler::~NoiseSampler() = default;

NoiseSampler::NoiseSampler(NoiseSampler &&other) noexcept = default;
NoiseSampler &NoiseSampler::operator=(NoiseSampler &&other) noexcept = default;

float NoiseSampler::sample2(float x, float y) const {
    return impl_->noise.GetNoise(x, y);
}

float NoiseSampler::sample3(float x, float y, float z) const {
    return impl_->noise.GetNoise(x, y, z);
}

} // namespace opencraft::noise
