#include <doctest/doctest.h>

#include "opencraft/noise/noise_sampler.hpp"

#include <cstring>
#include <vector>

namespace {

// Sample a fixed grid in both dimensions so every test compares the same data.
std::vector<float> sample_grid(const opencraft::noise::NoiseSampler &sampler) {
    std::vector<float> out;
    out.reserve(200);
    for (int i = 0; i < 50; ++i) {
        const auto x = static_cast<float>(i) * 0.37f;
        const auto z = static_cast<float>(i) * -0.11f;
        out.push_back(sampler.sample2(x, z));
        out.push_back(sampler.sample3(x, z * 0.5f, -x));
    }
    return out;
}

} // namespace

TEST_CASE("noise sampler is deterministic across instances") {
    const opencraft::noise::SamplerParams params{0.01f, 4, 2.0f, 0.5f, false};
    const opencraft::noise::NoiseSampler a(0x1234567890ABCDEULL, params);
    const opencraft::noise::NoiseSampler b(0x1234567890ABCDEULL, params);

    const auto ga = sample_grid(a);
    const auto gb = sample_grid(b);
    REQUIRE(ga.size() == gb.size());
    // Exact bit equality: no shared state, pure function of (seed, coords).
    REQUIRE(std::memcmp(ga.data(), gb.data(), ga.size() * sizeof(float)) == 0);
}

TEST_CASE("noise sampler different seeds diverge") {
    const opencraft::noise::SamplerParams params{0.01f, 4, 2.0f, 0.5f, false};
    const opencraft::noise::NoiseSampler a(0x1111111111111111ULL, params);
    const opencraft::noise::NoiseSampler b(0x2222222222222222ULL, params);

    const auto ga = sample_grid(a);
    const auto gb = sample_grid(b);
    bool differ = false;
    for (std::size_t i = 0; i < ga.size(); ++i) {
        if (ga[i] != gb[i]) {
            differ = true;
            break;
        }
    }
    REQUIRE(differ);
}

TEST_CASE("noise sampler output stays in range") {
    const opencraft::noise::SamplerParams fbm{0.005f, 5, 2.0f, 0.5f, false};
    const opencraft::noise::SamplerParams ridged{0.005f, 4, 2.0f, 0.5f, true};
    const opencraft::noise::NoiseSampler a(42, fbm);
    const opencraft::noise::NoiseSampler b(43, ridged);

    for (int i = 0; i < 200; ++i) {
        const auto x = static_cast<float>(i) * 7.3f;
        CHECK(a.sample3(x, x * 0.31f, -x) >= -1.001f);
        CHECK(a.sample3(x, x * 0.31f, -x) <= 1.001f);
        CHECK(b.sample2(x, -x) >= -1.001f);
        CHECK(b.sample2(x, -x) <= 1.001f);
    }
}

TEST_CASE("mix64 is stable and input sensitive") {
    using opencraft::noise::mix64;
    CHECK(mix64(0) == mix64(0));
    CHECK(mix64(0xDEADBEEFCAFEBABEULL) == mix64(0xDEADBEEFCAFEBABEULL));
    // Nearby inputs must not collide.
    CHECK(mix64(1) != mix64(2));
    CHECK(mix64(0xABCDEF00ULL) != mix64(0xABCDEF01ULL));
}
