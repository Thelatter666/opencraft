// Standalone atlas dumper: prints dimensions plus FNV-1a 64 digests of the
// atlas pixels (whole image and per tile) for the default registry. Built
// outside CMake so it can run against either the pre- or post-change atlas.cpp.
#include "atlas.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

constexpr std::uint64_t kOffset = 1469598103934665603ULL;
constexpr std::uint64_t kPrime = 1099511628211ULL;

void fnv(std::uint64_t &h, std::uint8_t byte) {
    h ^= byte;
    h *= kPrime;
}

std::uint64_t fnv_bytes(const std::uint32_t *data, std::size_t count) {
    std::uint64_t h = kOffset;
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint32_t v = data[i];
        fnv(h, static_cast<std::uint8_t>(v & 0xFF));
        fnv(h, static_cast<std::uint8_t>((v >> 8) & 0xFF));
        fnv(h, static_cast<std::uint8_t>((v >> 16) & 0xFF));
        fnv(h, static_cast<std::uint8_t>((v >> 24) & 0xFF));
    }
    return h;
}

} // namespace

int main() {
    auto registry = opencraft::voxel::BlockRegistry::create_default();
    auto image = opencraft::client::generate_atlas(registry);
    std::printf("dims %d %d %d pixels=%zu\n", image.width, image.height, image.tiles_per_row, image.pixels.size());
    std::printf("whole %016llx\n", static_cast<unsigned long long>(fnv_bytes(image.pixels.data(), image.pixels.size())));

    const int rows = image.tiles_per_row;
    const std::size_t tiles = image.pixels.size() / (16 * 16);
    for (std::size_t tile = 0; tile < tiles; ++tile) {
        const int tx = static_cast<int>(tile % static_cast<std::size_t>(rows)) * 16;
        const int ty = static_cast<int>(tile / static_cast<std::size_t>(rows)) * 16;
        std::uint64_t h = kOffset;
        for (int py = 0; py < 16; ++py) {
            const std::uint32_t *row = image.pixels.data() + static_cast<std::size_t>(ty + py) * image.width + tx;
            for (int px = 0; px < 16; ++px) {
                const std::uint32_t v = row[px];
                fnv(h, static_cast<std::uint8_t>(v & 0xFF));
                fnv(h, static_cast<std::uint8_t>((v >> 8) & 0xFF));
                fnv(h, static_cast<std::uint8_t>((v >> 16) & 0xFF));
                fnv(h, static_cast<std::uint8_t>((v >> 24) & 0xFF));
            }
        }
        std::printf("tile %zu %016llx\n", tile, static_cast<unsigned long long>(h));
    }
    return 0;
}
