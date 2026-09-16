// T-D4 on-machine evidence tool: read the SAVED world directly and compare it
// with what worldgen produces for the same seed.
//
// This is the independent check behind the card's acceptance 2 ("走回头路，改动
// 仍在"). It does not involve the game, the window, or any input injection: it
// opens the region files the game wrote, deserializes the chunks, and reports,
// for the cells a player edited, what the FILE holds next to what the generator
// would have produced. A dug cell that reads as air while worldgen says solid,
// or a placed block id where worldgen says air, can only come from a persisted
// player edit.
//
// Build (link line reused from the game target so the library order matches):
//   clang++ -std=c++20 -I... save_read.cpp $(libs from link.txt)
// Usage:
//   save_read <saves_root> <world_name> <cell:wx,wy,wz> [...]

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <optional>
#include <string>
#include <vector>

#include "opencraft/core/byte_buffer.hpp"
#include "opencraft/storage/world_save.hpp"
#include "opencraft/voxel/block_registry.hpp"
#include "opencraft/voxel/chunk.hpp"
#include "opencraft/worldgen/terrain_generator.hpp"

namespace {

constexpr std::uint64_t kSeed = 0x4F50454E43524146ULL; // WorldSim::kSeed

struct Cell {
    int x;
    int y;
    int z;
};

std::vector<Cell> parse_cells(int argc, char **argv, int first) {
    std::vector<Cell> cells;
    for (int i = first; i < argc; ++i) {
        Cell cell{};
        if (std::sscanf(argv[i], "%d,%d,%d", &cell.x, &cell.y, &cell.z) != 3) {
            std::fprintf(stderr, "bad cell '%s' (want wx,wy,wz)\n", argv[i]);
            std::exit(2);
        }
        cells.push_back(cell);
    }
    return cells;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: save_read <saves_root> <world_name> <wx,wy,wz> [...]\n");
        return 2;
    }
    const std::vector<Cell> cells = parse_cells(argc, argv, 3);
    opencraft::storage::WorldSave save(argv[1], argv[2]);
    const auto registry = opencraft::voxel::BlockRegistry::create_default();
    opencraft::worldgen::TerrainGenerator generator(kSeed, registry);

    // Deserialize every chunk the requested cells live in, once each.
    struct Loaded {
        int cx;
        int cz;
        std::optional<opencraft::voxel::Chunk> stored;
        opencraft::voxel::Chunk generated;
    };
    std::vector<Loaded> chunks;
    for (const Cell &cell : cells) {
        const auto [cx, cz] = opencraft::voxel::Chunk::chunk_coords(cell.x, cell.z);
        const bool known = [&] {
            for (const Loaded &loaded : chunks) {
                if (loaded.cx == cx && loaded.cz == cz) {
                    return true;
                }
            }
            return false;
        }();
        if (known) {
            continue;
        }
        Loaded loaded{cx, cz, std::nullopt, {}};
        if (const std::optional<std::vector<std::uint8_t>> payload = save.load_chunk(cx, cz); payload.has_value()) {
            opencraft::core::ByteBuffer buffer;
            buffer.write_bytes(payload->data(), payload->size());
            buffer.rewind();
            loaded.stored = opencraft::voxel::Chunk::deserialize(buffer);
        }
        generator.generate_chunk(cx, cz, loaded.generated);
        chunks.push_back(std::move(loaded));
    }

    std::printf("== saved world %s/%s (seed %#llx) ==\n", argv[1], argv[2], static_cast<unsigned long long>(kSeed));
    for (const Loaded &loaded : chunks) {
        std::printf("chunk (%d, %d): %s\n", loaded.cx, loaded.cz,
                    loaded.stored.has_value() ? "present in the region file" : "NOT saved (regenerates from worldgen)");
    }
    std::printf("\ncell                     file                        worldgen (same seed)     verdict\n");
    for (const Cell &cell : cells) {
        const auto [cx, cz] = opencraft::voxel::Chunk::chunk_coords(cell.x, cell.z);
        const Loaded *loaded = nullptr;
        for (const Loaded &candidate : chunks) {
            if (candidate.cx == cx && candidate.cz == cz) {
                loaded = &candidate;
            }
        }
        const int lx = cell.x - cx * opencraft::voxel::Chunk::kSizeX;
        const int lz = cell.z - cz * opencraft::voxel::Chunk::kSizeZ;
        if (loaded == nullptr || cell.y < 0 || cell.y >= opencraft::voxel::Chunk::kSizeY) {
            std::printf("(%4d,%3d,%4d)   out of world\n", cell.x, cell.y, cell.z);
            continue;
        }
        const std::uint16_t generated_id = loaded->generated.get_block(lx, cell.y, lz);
        const std::string generated_name = generated_id == 0 ? "air" : registry.string_of(generated_id);
        std::string file_name = "(not saved)";
        std::string verdict = "needs the file to judge";
        if (loaded->stored.has_value()) {
            const auto &chunk = *loaded->stored;
            const std::uint16_t file_id = chunk.get_block(lx, cell.y, lz);
            file_name = file_id == 0 ? "air" : registry.string_of(file_id);
            const auto fluid = chunk.get_fluid(lx, cell.y, lz);
            if (!fluid.empty()) {
                file_name += fluid.source ? " + water source" : " + flowing water";
            }
            verdict = file_id == generated_id ? "same as worldgen" : "PLAYER EDIT survives on disk";
        }
        std::printf("(%4d,%3d,%4d)   %-27s %-24s %s\n", cell.x, cell.y, cell.z, file_name.c_str(),
                    generated_name.c_str(), verdict.c_str());
    }
    return 0;
}
