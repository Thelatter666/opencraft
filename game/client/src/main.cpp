#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "asset_atlas.hpp"
#include "atlas.hpp"
#include "bitmap_font.hpp"
#include "block_colors.hpp"
#include "camera_spring.hpp"
#include "chunk_renderer.hpp"
#include "client_config.hpp"
#include "cube_geometry.hpp"
#include "death_screen.hpp"
#include "fov.hpp"
#include "hud.hpp"
#include "interaction.hpp"
#include "mob_mesh.hpp"
#include "mob_model.hpp"
#include "mob_pose.hpp"
#include "mob_render.hpp"
#include "opencraft/core/log.hpp"
#include "opencraft/core/tick_clock.hpp"
#include "opencraft/core/version.hpp"
#include "opencraft/game/mining.hpp"
#include "opencraft/game/placement.hpp"
#include "opencraft/game/raycast.hpp"
#include "opencraft/physics/auto_jump.hpp"
#include "opencraft/physics/input_state.hpp"
#include "opencraft/physics/player_physics.hpp"
#include "opencraft/physics/player_state.hpp"
#include "opencraft/render/mesher.hpp"
#include "opencraft/render/rhi.hpp"
#include "opencraft/sim/world_sim.hpp"
#include "opencraft/storage/level_file.hpp"
#include "opencraft/storage/world_save.hpp"
#include "particles.hpp"
#include "pause_menu.hpp"
#include "player_life.hpp"
#include "shaders.hpp"
#include "tick.hpp"
#include "world.hpp"

namespace render = opencraft::render; // short alias used by the GPU glue below
namespace phy = opencraft::physics;
namespace gam = opencraft::game;
namespace client = opencraft::client;
namespace srv = opencraft::server;

namespace {

void error_callback(int error_code, const char *description) {
    OC_LOG_ERROR("GLFW error {}: {}", error_code, description);
}

} // namespace

int main() {
    opencraft::core::log::init();
    OC_LOG_INFO("{} starting", opencraft::core::version_string());

    glfwSetErrorCallback(error_callback);
    if (glfwInit() != GLFW_TRUE) {
        OC_LOG_CRITICAL("glfwInit failed");
        return 1;
    }

    // 4.1 core is the highest portable floor (macOS tops out at 4.1).
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    GLFWwindow *window = glfwCreateWindow(client::kWindowWidth, client::kWindowHeight, "OpenCraft", nullptr, nullptr);
    if (window == nullptr) {
        OC_LOG_CRITICAL("glfwCreateWindow failed");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    const int gl_version = gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress));
    if (gl_version < GLAD_MAKE_VERSION(4, 1)) {
        OC_LOG_CRITICAL("OpenGL 4.1 not available (got {:x})", gl_version);
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    OC_LOG_INFO("GL {} / glad loaded", reinterpret_cast<const char *>(glGetString(GL_VERSION)));

    // ── save + level ────────────────────────────────────────────────────────
    // World directory fixed to "world" (T009 contract); saves/ is relative to
    // the working directory (./build/opencraft -> build/saves/world).
    opencraft::storage::WorldSave save("saves", "world");
    const std::optional<opencraft::storage::LevelData> stored_level = save.try_read_level();
    const std::uint64_t world_seed = stored_level.has_value() ? stored_level->seed : srv::WorldSim::kSeed;
    std::uint64_t game_ticks = stored_level.has_value() ? stored_level->tick_count : 0;
    if (stored_level.has_value()) {
        OC_LOG_INFO("save: loaded level.ocd (seed={:#x}, ticks={}, player=({:.2f}, {:.2f}, {:.2f}), hp={:.1f})",
                    stored_level->seed, stored_level->tick_count, stored_level->player_x, stored_level->player_y,
                    stored_level->player_z, stored_level->health);
    } else {
        OC_LOG_INFO("save: no level.ocd, new world with seed {:#x}", world_seed);
    }

    // ── authoritative side + client view (T-A1) ─────────────────────────────
    // The world simulation - and with it the only write access to the world -
    // lives in server::WorldSim. The client gets a read-only view of it plus
    // the request channel; both halves run in this process (docs/03 §1:
    // 客户端+内嵌服务端), which is what M3 turns into a real connection.
    srv::WorldSim authority(world_seed);
    authority.attach_save(&save);
    client::WorldSource world(authority);

    // Startup burst: the 5x5 around the origin in one streaming call, so the
    // 3x3 spawn meshes have lit neighbors; everything farther streams in within
    // the frame budget. This is the one call allowed to ignore the budget - the
    // spawn scan below reads surface columns out of that square, and there is no
    // frame to budget against yet.
    const gam::StreamRequest startup_stream{
        .center_cx = 0, .center_cz = 0, .generate_radius = 2, .unload_radius = 2, .generate_budget = 25};
    const auto startup_t0 = std::chrono::steady_clock::now();
    const gam::StreamResult startup = authority.stream(startup_stream);
    const double startup_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startup_t0).count();
    OC_LOG_INFO("startup gen: {} chunks, total {:.1f} ms, avg {:.2f} ms/chunk", startup.loaded_chunks.size(),
                startup_ms,
                startup.loaded_chunks.empty() ? 0.0 : startup_ms / static_cast<double>(startup.loaded_chunks.size()));

    // Spawn: scan 5x5 surface columns inside the square just generated (T009
    // card item) - or reuse the persisted player position.
    //
    // T-D45 splits two things that used to be one (card §2.4):
    //   * `spawn_pos` is where the SESSION starts - still "resume where you
    //     quit", which is why it cannot be the respawn point;
    //   * `life.respawn_pos` is where a death sends the player back. It is the
    //     persisted `spawn_x/y/z` - a field level_file.hpp has had all along,
    //     which the client used to overwrite with the player position on every
    //     save (that is what made it drift and useless) - and a new world fills
    //     it from the same scan the session start uses.
    client::PlayerLife life;
    const bool has_save = stored_level.has_value() && stored_level->has_player;
    // The scan is the fallback in both cases, so it runs only when there is no
    // usable persisted point to prefer.
    glm::dvec3 spawn_scan{0.0, 0.0, 0.0};
    if (!has_save || stored_level->spawn_y <= 0.0) {
        spawn_scan = authority.find_spawn();
        OC_LOG_INFO("spawn scan: surface at ({:.1f}, {:.1f}, {:.1f})", spawn_scan.x, spawn_scan.y, spawn_scan.z);
    }
    life.respawn_pos =
        client::choose_respawn_point(has_save, has_save ? *stored_level : opencraft::storage::LevelData{}, spawn_scan);
    const glm::dvec3 spawn_pos =
        has_save ? glm::dvec3(stored_level->player_x, stored_level->player_y, stored_level->player_z) : spawn_scan;
    OC_LOG_INFO("respawn point: ({:.1f}, {:.1f}, {:.1f})", life.respawn_pos.x, life.respawn_pos.y, life.respawn_pos.z);

    // ── atlas + font ────────────────────────────────────────────────────────
    const opencraft::client::AtlasImage atlas_image = opencraft::client::generate_atlas(world.registry());
    const render::Texture2D atlas(atlas_image.width, atlas_image.height, atlas_image.pixels.data());
    const float tiles_per_row = static_cast<float>(atlas_image.tiles_per_row);
    const float texel = 1.0f / static_cast<float>(atlas_image.width);
    const std::uint16_t crack_base = opencraft::client::crack_tile_base(world.registry().size());
    OC_LOG_INFO("atlas: {}x{} px, {} tiles/row, crack tiles at {}", atlas_image.width, atlas_image.height,
                atlas_image.tiles_per_row, crack_base);

    const client::FontImage font_image = client::build_font_texture();
    const render::Texture2D font(font_image.width, font_image.height, font_image.pixels.data());

    // Block main colors for break particles (needs the generated atlas).
    std::vector<glm::vec3> block_colors = client::block_main_colors(atlas_image, world.registry().size());

    // ── overlay geometry ─────────────────────────────────────────────────────
    const client::CubeGeometry cube = client::build_cube_geometry();

    render::VertexArray wire_vao;
    wire_vao.bind();
    render::Buffer wire_vbo(render::Buffer::Target::Vertex, cube.edge_vertices.data(),
                            cube.edge_vertices.size() * sizeof(glm::vec3), render::Buffer::Usage::Static);
    wire_vbo.bind();
    wire_vao.set_attribute(0, 3, GL_FLOAT, sizeof(glm::vec3), 0);

    render::VertexArray crack_vao;
    crack_vao.bind();
    render::Buffer crack_pos_vbo(render::Buffer::Target::Vertex, cube.face_vertices.data(),
                                 cube.face_vertices.size() * sizeof(glm::vec3), render::Buffer::Usage::Static);
    crack_pos_vbo.bind();
    crack_vao.set_attribute(0, 3, GL_FLOAT, sizeof(glm::vec3), 0);
    render::Buffer crack_uv_vbo(render::Buffer::Target::Vertex, cube.face_uv.data(),
                                cube.face_uv.size() * sizeof(glm::vec2), render::Buffer::Usage::Static);
    crack_uv_vbo.bind();
    crack_vao.set_attribute(1, 2, GL_FLOAT, sizeof(glm::vec2), 0);

    render::VertexArray crosshair_vao;
    crosshair_vao.bind();
    const std::array<glm::vec3, 4> crosshair_verts = {glm::vec3(-0.018f, 0.0f, 0.0f), glm::vec3(0.018f, 0.0f, 0.0f),
                                                      glm::vec3(0.0f, -0.03f, 0.0f), glm::vec3(0.0f, 0.03f, 0.0f)};
    render::Buffer crosshair_vbo(render::Buffer::Target::Vertex, crosshair_verts.data(),
                                 crosshair_verts.size() * sizeof(glm::vec3), render::Buffer::Usage::Static);
    crosshair_vbo.bind();
    crosshair_vao.set_attribute(0, 3, GL_FLOAT, sizeof(glm::vec3), 0);

    // Break particles: dynamic point cloud (pos + rgba per point, T009).
    render::VertexArray particle_vao;
    particle_vao.bind();
    render::Buffer particle_vbo(render::Buffer::Target::Vertex, nullptr, 0, render::Buffer::Usage::Dynamic);
    particle_vbo.bind();
    particle_vao.set_attribute(0, 3, GL_FLOAT, sizeof(glm::vec4) + sizeof(glm::vec3), 0);
    particle_vao.set_attribute(1, 4, GL_FLOAT, sizeof(glm::vec4) + sizeof(glm::vec3), sizeof(glm::vec3));

    // ── shaders ─────────────────────────────────────────────────────────────
    const render::Shader shader(client::kVertexShader, client::kFragmentShader);
    shader.use();
    glUniform1i(shader.uniform_location("u_atlas"), 0);
    glUniform1f(shader.uniform_location("u_tiles_per_row"), tiles_per_row);
    glUniform1f(shader.uniform_location("u_texel"), texel);
    atlas.bind(0);

    const render::Shader wire_shader(client::kWireVertexShader, client::kWireFragmentShader);

    const render::Shader crack_shader(client::kCrackVertexShader, client::kCrackFragmentShader);
    crack_shader.use();
    glUniform1i(crack_shader.uniform_location("u_atlas"), 0);
    glUniform1f(crack_shader.uniform_location("u_tiles_per_row"), tiles_per_row);
    glUniform1f(crack_shader.uniform_location("u_texel"), texel);

    const render::Shader particle_shader(client::kParticleVertexShader, client::kParticleFragmentShader);

    const render::Shader ui_flat_shader(client::kUiFlatVertexShader, client::kUiFlatFragmentShader);

    const render::Shader ui_text_shader(client::kUiTextVertexShader, client::kUiTextFragmentShader);
    ui_text_shader.use();
    glUniform1i(ui_text_shader.uniform_location("u_font"), 1);

    const render::Shader mob_shader(client::kMobVertexShader, client::kMobFragmentShader);

    // ── mob models (T-B1) ───────────────────────────────────────────────────
    // assets/mobs/<mob_id>.vox, found through the same asset root the block
    // tiles use (resolve_assets_root, no second convention). The roster comes
    // from the registry rather than a list here, so the denominator of the
    // startup line stays right when a mob is added.
    //
    // A fresh checkout has no assets/mobs/ at all: the line reads "mobs: 0/3"
    // and every mob keeps the two-box stand-in it has had since T-M2
    // (research/12 §6.5, first row).
    std::vector<std::string> mob_ids;
    for (std::uint16_t type = 0; type < authority.entity_types().size(); ++type) {
        if (authority.mobs().find(type) != nullptr) {
            mob_ids.push_back(authority.entity_types().string_of(type));
        }
    }
    const std::filesystem::path assets_root = client::resolve_assets_root();
    const std::vector<client::MobModel> mob_models = client::load_mob_models(assets_root, mob_ids);
    std::vector<client::MobModelAsset> mob_assets;
    mob_assets.reserve(mob_models.size());
    for (const client::MobModel &model : mob_models) {
        const gam::MobDef *def = authority.mobs().find_by_id(model.id);
        // The model is sized to the mob's own collision height, so the drawn
        // mob and the simulated box are the same size by construction and no
        // scale number has to be repeated in two places.
        const float height = def != nullptr ? static_cast<float>(def->physics.height) : 1.0f;
        client::MobModelAsset asset;
        asset.id = model.id;
        asset.voxels = model.vox.voxels.size();
        asset.mesh = client::build_mob_mesh(model.vox, height);
        asset.palette = model.palette;
        const std::string palette_note =
            model.palette_from_png ? ", palette from palettes/" + asset.id + ".png" : std::string();
        OC_LOG_INFO("mob model {}: {} voxels, {} triangles, {} joints, {} blocks tall{}", asset.id, asset.voxels,
                    asset.mesh.vertices.size() / 3, asset.mesh.parts.size(), height, palette_note);
        mob_assets.push_back(std::move(asset));
    }
    const client::MobRenderer mob_renderer(mob_assets, mob_shader);

    // ── GL state ────────────────────────────────────────────────────────────
    int fb_width = client::kWindowWidth;
    int fb_height = client::kWindowHeight;
    glfwGetFramebufferSize(window, &fb_width, &fb_height);
    glViewport(0, 0, fb_width, fb_height);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    // gl_PointSize only takes effect in a core profile when this is enabled;
    // without it the break particles collapse to single pixels (T009).
    glEnable(GL_PROGRAM_POINT_SIZE);
    glClearColor(0.52f, 0.62f, 0.72f, 1.0f);

    // ── player + simulation state ───────────────────────────────────────────
    phy::PlayerState prev_state;
    prev_state.position = spawn_pos;
    prev_state.on_ground = true;
    prev_state.fall_peak_y = prev_state.position.y;
    phy::PlayerState curr_state = prev_state;
    if (stored_level.has_value() && stored_level->has_player) {
        curr_state.position = glm::dvec3(stored_level->player_x, stored_level->player_y, stored_level->player_z);
        curr_state.velocity = glm::dvec3(stored_level->player_vx, stored_level->player_vy, stored_level->player_vz);
        curr_state.health = stored_level->health;
        curr_state.fall_peak_y = stored_level->fall_peak_y;
        curr_state.fall_distance = stored_level->fall_distance;
        curr_state.pose = static_cast<phy::Pose>(stored_level->pose & 1U);
        curr_state.on_ground = stored_level->on_ground;
        // ── T-D45 §2.4: a 0-health save is a death nobody resolved ──────────
        // Health is persisted but the inventory is not, and nothing heals, so
        // restoring health 0 verbatim started the session as a corpse that could
        // not recover. The load path treats it as a RESPAWN instead: full health
        // at the respawn point. (Not a drop - the inventory is not in the file,
        // and the launch kit is granted below.)
        if (client::save_needs_respawn(curr_state.health)) {
            client::respawn_player(curr_state, life);
            OC_LOG_WARN("save: stored health {:.1f}; respawning at ({:.1f}, {:.1f}, {:.1f}) at full health",
                        stored_level->health, life.respawn_pos.x, life.respawn_pos.y, life.respawn_pos.z);
        }
    }
    prev_state = curr_state;

    double view_yaw = stored_level.has_value() && stored_level->has_player ? stored_level->yaw : 0.0;
    double view_pitch = stored_level.has_value() && stored_level->has_player ? stored_level->pitch : 0.0;
    bool paused = false;
    // T-D14: Auto-Jump master switch. In-memory only (card §4: 不要求落盘);
    // default ON, matching MC's client option. Toggled from the pause menu.
    bool auto_jump_enabled = true;
    // Last frame's primary-button state, for click-EDGE detection in the
    // pause menu (the non-idempotent AUTO-JUMP toggle button).
    bool prev_menu_clicked = false;
    bool prev_esc = false;
    // T-D45: the death screen's click-edge state, and last frame's life state -
    // the cursor mode is handed over on the edge, not every frame.
    bool prev_death_clicked = false;
    bool prev_dead = false;
    // The gamerule skeleton's one rule (gamerule.hpp): what a death leaves the
    // player holding. Read by the death drop and by the respawn button.
    const client::GameRules rules{};
    // ── items + inventory (T-I2) ────────────────────────────────────────────
    // ONE item registry per process, and since T-E1 it lives in the authority
    // (world.items()): the authority decides what a broken block drops, so the
    // item id space has to be the world's. Until this card both ends built
    // their own create_default() and agreed only because the table is
    // deterministic. The reference is a view, not an owner - the authority is
    // constructed above and outlives the interaction state below.
    const gam::ItemRegistry &item_registry = world.items();
    // Input edges, inventory, targeting and the T-D1 sprint-jump QA counters
    // live in one object (see interaction.hpp). The launch kit gives the
    // player something to see on the first frame.
    client::InteractionState interact(item_registry);
    client::fill_starting_inventory(interact.inventory);
    interact.refresh_selection();

    if (stored_level.has_value() && stored_level->has_player && stored_level->selected_block != 0) {
        // Restore the persisted selection to the cell whose item places that
        // block. The field is a block id, so it cannot name a vessel or any
        // other non-placeable item; those fall back to the first cell.
        const std::uint16_t saved_block = stored_level->selected_block;
        for (int slot = 0; slot < client::kHotbarSlots; ++slot) {
            if (client::placed_block_of(item_registry, interact.inventory.slot(slot)) == saved_block) {
                interact.select_slot(slot);
                break;
            }
        }
    }

    gam::MiningTracker mining(world.registry());
    opencraft::core::TickClock tick_clock;
    std::vector<std::pair<int, int>> dirty_chunks;
    client::ChunkRenderableMap renderables;
    double last_mesh_ms = 0.0;

    // Break particles (T009 mining feedback).
    std::vector<client::Particle> particles;

    // Chunk offsets sorted by distance, out to the generation radius (view + 1),
    // used to pace the client's own meshing: a chunk at the view edge may only
    // be meshed against lit neighbors once the wider ring exists. T-D4 moved
    // the *generation* that fills that ring into the authority (stream()); this
    // list stayed because the mesh order is the client's pacing decision.
    std::vector<std::pair<int, int>> gen_offsets;
    for (int dx = -(client::kViewRadius + 1); dx <= client::kViewRadius + 1; ++dx) {
        for (int dz = -(client::kViewRadius + 1); dz <= client::kViewRadius + 1; ++dz) {
            gen_offsets.emplace_back(dx, dz);
        }
    }
    std::sort(gen_offsets.begin(), gen_offsets.end(), [](const auto &a, const auto &b) {
        return a.first * a.first + a.second * a.second < b.first * b.first + b.second * b.second;
    });

    // Pointer lock.
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (glfwRawMouseMotionSupported() == GLFW_TRUE) {
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_RAW_MOUSE_MOTION);
    }

    // Initial 3x3 meshes (5x5 generated above, so neighbors are lit).
    for (int cx = -1; cx <= 1; ++cx) {
        for (int cz = -1; cz <= 1; ++cz) {
            client::mesh_chunk(renderables, world, cx, cz, last_mesh_ms);
        }
    }

    // Everything a logic tick reads or writes, as references into the locals
    // above (nothing is copied - see tick.hpp).
    const client::TickContext tick_ctx{
        .world = world,
        .authority = authority,
        .save = save,
        .block_colors = block_colors,
        .window = window,
        .world_seed = world_seed,
        .game_ticks = game_ticks,
        .view_yaw = view_yaw,
        .view_pitch = view_pitch,
        .auto_jump_enabled = auto_jump_enabled,
        .prev_state = prev_state,
        .curr_state = curr_state,
        .mining = mining,
        .dirty_chunks = dirty_chunks,
        .particles = particles,
        .state = interact,
        .life = life,
        .rules = rules,
    };

    // HUD inputs that outlive the frame loop; every referenced object is a
    // const local declared above.
    const client::HudResources hud_res{world, shader, ui_flat_shader, ui_text_shader, font};
    const client::PauseMenuResources pause_res{window, ui_flat_shader, ui_text_shader, font};
    // T-D45: the death screen draws through the same four GL handles, and is a
    // separate module because every responsive click coordinate in the
    // repository is bound to the pause menu's layout.
    const client::DeathScreenResources death_res{window, ui_flat_shader, ui_text_shader, font};

    // ── main loop ───────────────────────────────────────────────────────────
    double last_frame = glfwGetTime();
    double fps_timer = last_frame;
    double last_particle_time = last_frame;
    int fps_frames = 0;
    double last_cursor_x = 0.0;
    double last_cursor_y = 0.0;
    bool cursor_anchored = false;
    float fov = client::kBaseFov;
    int stream_meshed = 0;
    // T-B1: per-mob walk phase (research/12 §4.4 - a presentation quantity the
    // simulation does not carry, advanced by the simulated speed).
    client::MobAnimClock mob_anim;

    // T-D13: render-camera vertical spring. The camera's Y follows the eye
    // target through a critically damped filter while X/Z pass through exactly,
    // so the single-tick step-assist lift stops reading as a teleport. Seeded
    // from the same position the physics starts at, so frame 1 has no transient.
    client::CameraFilter camera;
    camera.reset(curr_state.position.x, curr_state.position.y, curr_state.position.z,
                 curr_state.pose == phy::Pose::Sneaking ? client::kEyeSneaking : client::kEyeStanding);
    double camera_last_time = last_frame;
    // Physics ticks executed by the frame currently being rendered. The spring
    // uses these to bend its per-frame ramp where the physics tick landed.
    int ticks_this_frame = 0;
    double tick_kink_dt = 0.0;
    double tick_kink_eye_y = 0.0;

    while (glfwWindowShouldClose(window) == GLFW_FALSE) {
        glfwPollEvents();

        // ── T-D45: coming back / handing over the cursor ────────────────────
        // The death screen is modal exactly like the pause menu: it owns the
        // pointer (its button needs one) and it cannot be paused over. The hand
        // over happens on the EDGE, so nothing re-locks the cursor every frame
        // the player spends dead.
        if (life.dead != prev_dead) {
            if (life.dead) {
                mining.reset();
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            } else {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                cursor_anchored = false;
                // The teleport must not sweep the camera across the world (the
                // spring follows a target, and the target just moved hundreds of
                // blocks); seeding it at the respawn point makes the first frame
                // after a respawn start where the player is.
                camera.reset(curr_state.position.x, curr_state.position.y, curr_state.position.z,
                             curr_state.pose == phy::Pose::Sneaking ? client::kEyeSneaking : client::kEyeStanding);
            }
            prev_dead = life.dead;
        }

        // ESC edge: toggle pause in both directions. A dead player cannot pause
        // - the death screen is the modal state, and pausing under it would put
        // two overlays on top of each other with two live button sets.
        const bool esc_down = client::key_pressed(window, GLFW_KEY_ESCAPE) != 0;
        if (esc_down && !prev_esc && !life.dead) {
            paused = !paused;
            if (paused) {
                mining.reset();
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            } else {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                tick_clock.reset();
                cursor_anchored = false;
            }
        }
        prev_esc = esc_down;

        // ── mouse look ──────────────────────────────────────────────────────
        // T-D45: not while dead - the cursor is loose so the RESPAWN button can
        // be clicked, and a loose cursor that still turns the view is the "the
        // camera is spinning while I am dead" bug. The ticks below keep running
        // (the world does not stop for a death screen); only the looking stops.
        if (!paused && !life.dead) {
            if (!cursor_anchored) {
                glfwGetCursorPos(window, &last_cursor_x, &last_cursor_y);
                cursor_anchored = true;
            } else {
                double x = 0.0;
                double y = 0.0;
                glfwGetCursorPos(window, &x, &y);
                view_yaw -= (x - last_cursor_x) * client::kMouseSensitivity;
                view_pitch += (y - last_cursor_y) * client::kMouseSensitivity;
                view_pitch = std::clamp(view_pitch, -client::kMaxPitch, client::kMaxPitch);
                last_cursor_x = x;
                last_cursor_y = y;
            }
        }
        if (!paused) {
            // ── fixed-step simulation ───────────────────────────────────────────
            const double now = glfwGetTime();
            // T-D13: the partial-tick LERP's ramp bends where this frame's tick
            // lands. alpha_before is how far into the pending tick we already
            // are, and the eye target at the bend is the one the player state
            // holds *before* step_player runs; both are read here, before
            // advance()/run_tick() consume them.
            const double alpha_before = tick_clock.alpha();
            const int ticks = tick_clock.advance((now - last_frame) * 1000.0);
            last_frame = now;
            ticks_this_frame = ticks;
            if (ticks > 0) {
                tick_kink_dt = (1.0 - alpha_before) * (opencraft::core::TickClock::kTickDurationMs / 1000.0);
                tick_kink_eye_y =
                    curr_state.position.y +
                    (curr_state.pose == phy::Pose::Sneaking ? client::kEyeSneaking : client::kEyeStanding);
            } else {
                tick_kink_dt = 0.0;
            }
            for (int i = 0; i < ticks; ++i) {
                client::run_tick(tick_ctx);
                ++game_ticks;
            }

            // ── streaming ───────────────────────────────────────────────────────
            // The authority owns the streaming decision (T-D4): the client says
            // where the viewer is and what the frame may spend, and gets back
            // what it loaded and what it released. Generation, release and the
            // persist-before-release rule all live behind that one call.
            const auto [pcx, pcz] =
                opencraft::voxel::Chunk::chunk_coords(static_cast<int>(std::floor(curr_state.position.x)),
                                                      static_cast<int>(std::floor(curr_state.position.z)));
            const gam::StreamResult streamed =
                authority.stream(client::make_stream_request(curr_state.position, client::kGenPerFrame));
            if (!streamed.unloaded_chunks.empty()) {
                // The released chunks' world data is gone (dirty ones were
                // written to the save first). Whatever the client owns for them
                // - here, the GPU mesh - goes with it.
                for (const auto &[cx, cz] : streamed.unloaded_chunks) {
                    renderables.erase(client::chunk_key(cx, cz));
                }
                OC_LOG_INFO("stream: released {} chunk(s), {} resident, {} meshed", streamed.unloaded_chunks.size(),
                            streamed.loaded_total, renderables.size());
            }

            if (!dirty_chunks.empty()) {
                // The fluid simulation can name the same chunk hundreds of
                // times in one frame; remesh each chunk once.
                std::sort(dirty_chunks.begin(), dirty_chunks.end());
                dirty_chunks.erase(std::unique(dirty_chunks.begin(), dirty_chunks.end()), dirty_chunks.end());
                const auto t0 = std::chrono::steady_clock::now();
                for (const auto &[cx, cz] : dirty_chunks) {
                    if (world.chunk_ready(cx, cz)) {
                        client::mesh_chunk(renderables, world, cx, cz, last_mesh_ms);
                    }
                }
                OC_LOG_INFO("remeshed {} chunk(s) in {:.2f} ms (last mesh {:.2f} ms)", dirty_chunks.size(),
                            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(),
                            last_mesh_ms);
                dirty_chunks.clear();
            }
            int mesh_left = client::kNewMeshPerFrame;
            for (const auto &[dx, dz] : gen_offsets) {
                if (mesh_left == 0) {
                    break;
                }
                if (dx * dx + dz * dz > client::kViewRadius * client::kViewRadius) {
                    continue; // mesh only within the view radius
                }
                const int cx = pcx + dx;
                const int cz = pcz + dz;
                // A chunk released by stream() this frame is not resident, so
                // neighbors_ready() is false for it and its neighbors: nothing
                // gets meshed against a chunk that is no longer there.
                if (world.neighbors_ready(cx, cz) && renderables.find(client::chunk_key(cx, cz)) == renderables.end()) {
                    client::mesh_chunk(renderables, world, cx, cz, last_mesh_ms);
                    ++stream_meshed;
                    --mesh_left;
                }
            }
        } // !paused
        const double now = glfwGetTime();
        last_frame = now;

        // ── camera (partial-tick interpolation) ─────────────────────────────
        // X/Z are the interpolated physics position, unfiltered. Only the Y
        // component runs through the T-D13 vertical spring, which turns the
        // step-assist's single-tick 0.6 lift into a ~0.2 s S-curve instead of a
        // one-frame jump. `now - camera_last_time` is the real frame duration;
        // the spring clamps it internally.
        const double alpha = tick_clock.alpha();
        const glm::dvec3 cam_pos = prev_state.position + (curr_state.position - prev_state.position) * alpha;
        const double eye_height = curr_state.pose == phy::Pose::Sneaking ? client::kEyeSneaking : client::kEyeStanding;
        const double frame_dt = std::max(0.0, now - camera_last_time);
        camera_last_time = now;
        // The LERP ramp bends where this frame's tick landed (see the kink
        // capture above). Only inside a simulated frame is it a real bend;
        // otherwise the single-piece path applies. A paused frame runs no ticks,
        // so any captured kink is stale and must not be used.
        if (!paused && ticks_this_frame > 0 && tick_kink_dt > 0.0 && tick_kink_dt < frame_dt) {
            camera.update(cam_pos.x, cam_pos.y, cam_pos.z, eye_height, frame_dt, tick_kink_eye_y, tick_kink_dt);
        } else {
            camera.update(cam_pos.x, cam_pos.y, cam_pos.z, eye_height, frame_dt);
        }
        const glm::dvec3 eye(camera.x(), camera.y(), camera.z());

        glfwGetFramebufferSize(window, &fb_width, &fb_height);
        glViewport(0, 0, fb_width, fb_height);

        // Sprint FOV (T-D1): driven by the physics sprint state — double-tap
        // and Ctrl both stretch the view. Constants and easing live in
        // fov.hpp so the transition is unit-testable.
        fov = client::fov_step(fov, curr_state.sprinting);

        const glm::mat4 projection = glm::perspective(
            glm::radians(fov), static_cast<float>(fb_width) / static_cast<float>(fb_height), 0.05f, 600.0f);
        const glm::vec3 eye_f(eye);
        const glm::mat4 view =
            glm::lookAt(eye_f, eye_f + glm::vec3(client::view_dir(view_yaw, view_pitch)), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 mvp = projection * view;

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        shader.use();
        glUniformMatrix4fv(shader.uniform_location("u_mvp"), 1, GL_FALSE, &mvp[0][0]);

        std::vector<const client::ChunkRenderable *> order;
        client::draw_chunk_opaque_pass(renderables, shader, eye_f, order);
        client::draw_chunk_translucent_pass(renderables, shader, eye_f, order);

        // ── dropped items (T-E1) ────────────────────────────────────────────
        // A drop is drawn as its block's cube at 1/4 scale through the crack
        // shader - the same three per-face-group draws the held item uses, so
        // the atlas, the shader and the geometry are all reused and nothing in
        // engine/render had to change.
        //
        // Both animations come from the entity's AGE, i.e. from simulation
        // state, so a frozen drop (chunk unloaded) also stops animating:
        // research/11 §4.5 - rotation 1 rad/s, vertical bob Y ∈ [0.0625, 0.2625]
        // over π s. The physical position never moves for them; this is the
        // render layer's effect alone.
        {
            const srv::EntityStore &drops = authority.entities();
            const gam::EntityTypeRegistry &entity_types = authority.entity_types();
            std::vector<float> item_points; // the no-block-form fallback (one tinted point each)
            bool item_pass_ready = false;
            drops.for_each_entity([&](const srv::Entity &drop) {
                if (drop.stack.empty()) {
                    return;
                }
                const float ticks = static_cast<float>(drop.age) + static_cast<float>(alpha);
                const float bob = 0.1625f + 0.1f * std::sin(ticks * 0.1f); // centre of [0.0625, 0.2625]
                const glm::vec3 centre = glm::vec3(drop.position) + glm::vec3(0.0f, bob, 0.0f);
                const std::uint16_t block = item_registry.def_of(drop.stack.item).block;
                if (block == gam::kNoBlock) {
                    // Food, tools and armour have no generated texture yet
                    // (T-I2's stand-in tint). One coloured point keeps them
                    // visible without inventing an icon set in a physics card.
                    const glm::vec3 tint = client::item_tint(item_registry.string_of(drop.stack.item));
                    item_points.insert(item_points.end(), {centre.x, centre.y, centre.z, tint.r, tint.g, tint.b, 1.0f});
                    return;
                }
                if (!item_pass_ready) {
                    crack_shader.use();
                    glUniform1i(crack_shader.uniform_location("u_atlas"), 0);
                    glUniform1f(crack_shader.uniform_location("u_tiles_per_row"), tiles_per_row);
                    glUniform1f(crack_shader.uniform_location("u_texel"), texel);
                    glUniform3f(crack_shader.uniform_location("u_offset"), 0.0f, 0.0f, 0.0f);
                    // The cube geometry is [0,1]^3, so it is shifted to its own
                    // centre before the rotation.
                    glDisable(GL_CULL_FACE); // overlay winding is mirrored (see the crack pass)
                    crack_vao.bind();
                    item_pass_ready = true;
                }
                // The shader takes one scalar scale, and the only drop type is a
                // cube (0.25^3, research/11 §4.2); a non-cubic entity kind would
                // need a scale vector here.
                const float side = static_cast<float>(entity_types.def_of(drop.type).height);
                glUniform1f(crack_shader.uniform_location("u_scale"), side);
                // The cube geometry is [0,1]^3 and the shader scales a_pos BEFORE
                // u_mvp, so what must be centred is the SCALED cube: translate by
                // half of `side`, not by 0.5 (0.5 put the cube a third of a block
                // under the floor, where it was invisible - found on-machine).
                const glm::mat4 model = glm::translate(glm::mat4(1.0f), centre) *
                                        glm::rotate(glm::mat4(1.0f), ticks * 0.05f, glm::vec3(0.0f, 1.0f, 0.0f)) *
                                        glm::translate(glm::mat4(1.0f), glm::vec3(-0.5f * side));
                const glm::mat4 item_mvp = projection * view * model;
                glUniformMatrix4fv(crack_shader.uniform_location("u_mvp"), 1, GL_FALSE, &item_mvp[0][0]);
                // build_cube_geometry face order: f0 top, f1 bottom, f2..f5 sides.
                glUniform1f(crack_shader.uniform_location("u_tile"), static_cast<float>(block * 3 + 1));
                glDrawArrays(GL_TRIANGLES, 12, 24); // sides
                glUniform1f(crack_shader.uniform_location("u_tile"), static_cast<float>(block * 3 + 0));
                glDrawArrays(GL_TRIANGLES, 0, 6); // top
                glUniform1f(crack_shader.uniform_location("u_tile"), static_cast<float>(block * 3 + 2));
                glDrawArrays(GL_TRIANGLES, 6, 6); // bottom
            });
            if (item_pass_ready) {
                glEnable(GL_CULL_FACE);
            }
            if (!item_points.empty()) {
                glDepthMask(GL_FALSE);
                particle_shader.use();
                glUniformMatrix4fv(particle_shader.uniform_location("u_mvp"), 1, GL_FALSE, &mvp[0][0]);
                glUniform1f(particle_shader.uniform_location("u_point_px"),
                            7.0f * static_cast<float>(fb_height) / 720.0f);
                particle_vao.bind();
                particle_vbo.bind();
                glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(item_points.size() * sizeof(float)),
                             item_points.data(), GL_STREAM_DRAW);
                glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(item_points.size() / 7));
                glDepthMask(GL_TRUE);
            }
        }

        // ── mobs without a model (T-M2) ─────────────────────────────────────
        // Two boxes per mob, through the same crack shader the drops use: a body
        // of the mob's own collision box and a head cube offset along its facing.
        // The head is what makes the AI visible - "the mob turned to look at me"
        // is the LookAtPlayer goal's only output, and it is a simulation value
        // (Entity::ai::yaw), not a render-side animation.
        //
        // ⚠ The look is a STAND-IN: the atlas tiles come from existing blocks
        // (there is no mob texture set) and the body is a box. See
        // client::mob_skin.
        //
        // T-B1 makes this the FALLBACK path rather than the only one: a mob with
        // a .vox model is skipped here and drawn by the model pass below. The
        // two loops are mutually exclusive and this one is unchanged, so with no
        // model files present the frame is byte-for-byte what it was before the
        // model channel existed (research/12 §6.4, §6.5).
        {
            const srv::EntityStore &mobs_store = authority.entities();
            const gam::EntityTypeRegistry &entity_types = authority.entity_types();
            const gam::MobRegistry &mob_roster = authority.mobs();
            const auto &blocks = authority.registry();
            bool mob_pass_ready = false;
            mobs_store.for_each_entity([&](const srv::Entity &mob) {
                const gam::MobDef *def = mob_roster.find(mob.type);
                if (def == nullptr) {
                    return;
                }
                if (mob_renderer.has(entity_types.string_of(mob.type))) {
                    return; // has a model: the model pass owns it
                }
                if (!mob_pass_ready) {
                    crack_shader.use();
                    glUniform1i(crack_shader.uniform_location("u_atlas"), 0);
                    glUniform1f(crack_shader.uniform_location("u_tiles_per_row"), tiles_per_row);
                    glUniform1f(crack_shader.uniform_location("u_texel"), texel);
                    glUniform3f(crack_shader.uniform_location("u_offset"), 0.0f, 0.0f, 0.0f);
                    glDisable(GL_CULL_FACE); // overlay winding is mirrored (see the crack pass)
                    crack_vao.bind();
                    mob_pass_ready = true;
                }
                const float yaw = static_cast<float>(mob.ai.yaw);
                const glm::vec3 facing = glm::vec3(client::view_dir(yaw, 0.0));
                const client::MobSkin skin = client::mob_skin(entity_types.string_of(mob.type));
                const auto body_id = static_cast<float>(blocks.id_of(skin.body_block));
                const auto head_id = static_cast<float>(blocks.id_of(skin.head_block));

                const float half = static_cast<float>(def->physics.half_width);
                const float height = static_cast<float>(def->physics.height);
                // 1.0 because the shader's u_scale is a single scalar and a mob's
                // box is not a cube: the non-uniform size goes in the model
                // matrix instead.
                glUniform1f(crack_shader.uniform_location("u_scale"), 1.0f);

                const glm::vec3 feet = glm::vec3(mob.position);
                const glm::vec3 body_centre = feet + glm::vec3(0.0f, height * 0.5f, 0.0f);
                const glm::mat4 body_model = glm::translate(glm::mat4(1.0f), body_centre) *
                                             glm::scale(glm::mat4(1.0f), glm::vec3(half * 2.0f, height, half * 2.0f)) *
                                             glm::translate(glm::mat4(1.0f), glm::vec3(-0.5f));
                const glm::mat4 body_mvp = projection * view * body_model;
                glUniformMatrix4fv(crack_shader.uniform_location("u_mvp"), 1, GL_FALSE, &body_mvp[0][0]);
                glUniform1f(crack_shader.uniform_location("u_tile"), body_id * 3.0f + 1.0f);
                glDrawArrays(GL_TRIANGLES, 12, 24); // sides only: a mob has no top/bottom face texture

                const float head_side = static_cast<float>(skin.head_scale) * half * 2.0f;
                const glm::vec3 head_centre = feet + glm::vec3(0.0f, height - head_side * 0.5f, 0.0f) +
                                              facing * static_cast<float>(skin.head_forward) * half * 2.0f;
                const glm::mat4 head_model = glm::translate(glm::mat4(1.0f), head_centre) *
                                             glm::scale(glm::mat4(1.0f), glm::vec3(head_side)) *
                                             glm::translate(glm::mat4(1.0f), glm::vec3(-0.5f));
                const glm::mat4 head_mvp = projection * view * head_model;
                glUniformMatrix4fv(crack_shader.uniform_location("u_mvp"), 1, GL_FALSE, &head_mvp[0][0]);
                glUniform1f(crack_shader.uniform_location("u_tile"), head_id * 3.0f + 0.0f);
                glDrawArrays(GL_TRIANGLES, 0, 6); // top
                glUniform1f(crack_shader.uniform_location("u_tile"), head_id * 3.0f + 1.0f);
                glDrawArrays(GL_TRIANGLES, 12, 24); // sides
            });
            if (mob_pass_ready) {
                glEnable(GL_CULL_FACE);
            }
        }

        // ── mob models (T-B1) ───────────────────────────────────────────────
        // The other half of the routing in research/12 §6.4: exactly the mobs
        // the loop above skipped. Position and yaw come straight from the
        // simulation; the pose module only adds shape at that position and
        // facing (§4.4). The order of the two loops does not matter to the
        // picture - both draw depth-tested opaque geometry.
        //
        // Nothing here runs when no model was loaded, so a checkout with no
        // assets/mobs/ cannot have changed a pixel.
        if (mob_renderer.size() > 0) {
            const srv::EntityStore &mobs_store = authority.entities();
            const gam::EntityTypeRegistry &entity_types = authority.entity_types();
            const gam::MobRegistry &mob_roster = authority.mobs();
            // The walk cycle is the renderer's own clock (§4.4): the simulation
            // has no stride phase, but the RATE it advances at is the simulated
            // speed.
            const float pose_dt = static_cast<float>(std::min(frame_dt, 0.1));
            mob_renderer.begin_pass();
            mobs_store.for_each_entity([&](const srv::Entity &mob) {
                if (mob_roster.find(mob.type) == nullptr) {
                    return;
                }
                const std::string &mob_id = entity_types.string_of(mob.type);
                if (!mob_renderer.has(mob_id)) {
                    return;
                }
                client::MobPoseInput in;
                in.yaw = mob.ai.yaw;
                in.pitch = mob.ai.pitch;
                in.speed = std::sqrt(mob.velocity.x * mob.velocity.x + mob.velocity.z * mob.velocity.z);
                in.on_ground = mob.on_ground;
                in.hurt_cooldown = mob.ai.hurt_cooldown;
                in.fuse = mob.ai.fuse;
                in.baby = mob.ai.baby;
                const float phase = mob_anim.advance(mob.id, in.speed, pose_dt);
                const client::MobPose pose = client::mob_pose(in, phase);
                // Feet-centred, like every other entity (Entity::position).
                // rotate_y(yaw) is the same facing the two-box head used:
                // view_dir(yaw, 0) == (-sin yaw, 0, -cos yaw), which is where
                // glm::rotate maps the model's -Z.
                const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(mob.position)) *
                                        glm::rotate(glm::mat4(1.0f), pose.yaw, glm::vec3(0.0f, 1.0f, 0.0f));
                mob_renderer.draw(mob_id, mvp, model, pose);
            });
            mob_renderer.end_pass();
            mob_anim.end_frame();
        }

        // ── break particles (T009): depth-tested points, no depth writes ────
        {
            const double particles_now = glfwGetTime();
            const float dt = static_cast<float>(std::min(particles_now - last_particle_time, 0.1));
            last_particle_time = particles_now;
            client::update_particles(particles, dt);
            if (!particles.empty()) {
                std::vector<float> point_data;
                point_data.reserve(particles.size() * 7);
                for (const client::Particle &p : particles) {
                    point_data.insert(point_data.end(),
                                      {p.pos.x, p.pos.y, p.pos.z, p.color.r, p.color.g, p.color.b, p.color.a});
                }
                glDepthMask(GL_FALSE);
                particle_shader.use();
                glUniformMatrix4fv(particle_shader.uniform_location("u_mvp"), 1, GL_FALSE, &mvp[0][0]);
                glUniform1f(particle_shader.uniform_location("u_point_px"),
                            7.0f * static_cast<float>(fb_height) / 720.0f);
                particle_vao.bind();
                particle_vbo.bind();
                glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(point_data.size() * sizeof(float)),
                             point_data.data(), GL_STREAM_DRAW);
                glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(particles.size()));
                glDepthMask(GL_TRUE);
            }
        }

        // ── selection wireframe + crack overlay ─────────────────────────────
        if (interact.has_target) {
            wire_shader.use();
            glUniformMatrix4fv(wire_shader.uniform_location("u_mvp"), 1, GL_FALSE, &mvp[0][0]);
            glUniform3f(wire_shader.uniform_location("u_offset"), static_cast<float>(interact.target_pos.x),
                        static_cast<float>(interact.target_pos.y), static_cast<float>(interact.target_pos.z));
            glUniform1f(wire_shader.uniform_location("u_scale"), 1.002f);
            glUniform4f(wire_shader.uniform_location("u_color"), 0.05f, 0.05f, 0.05f, 1.0f);
            wire_vao.bind();
            glDrawArrays(GL_LINES, 0, 24);
        }
        if (interact.crack_stage >= 0) {
            crack_shader.use();
            glUniformMatrix4fv(crack_shader.uniform_location("u_mvp"), 1, GL_FALSE, &mvp[0][0]);
            glUniform3f(crack_shader.uniform_location("u_offset"), static_cast<float>(interact.crack_pos.x) - 0.001f,
                        static_cast<float>(interact.crack_pos.y) - 0.001f,
                        static_cast<float>(interact.crack_pos.z) - 0.001f);
            glUniform1f(crack_shader.uniform_location("u_scale"), 1.002f);
            glUniform1f(crack_shader.uniform_location("u_tile"), static_cast<float>(crack_base + interact.crack_stage));
            // Overlay cube winding is mirrored vs the mesher's CCW convention;
            // draw double-sided so culling cannot swallow the overlay.
            glDisable(GL_CULL_FACE);
            crack_vao.bind();
            glDrawArrays(GL_TRIANGLES, 0, 36);
            glEnable(GL_CULL_FACE);
        }
        glDepthMask(GL_TRUE);

        // ── held block with swing animation (T009) ──────────────────────────
        // Drawn last against cleared depth so it always sits over the world.
        // The crack shader does the textured-cube job: per-face tile uniform,
        // so the top/bottom/side faces get three draw calls. T-I2: the cube is
        // the held stack's stand-in block (a water vessel shows the water it
        // carries), so an empty hand and the items with no cube yet draw
        // nothing.
        {
            double swing_t = (glfwGetTime() - interact.swing_start) / 0.25;
            if (swing_t >= 1.0) {
                swing_t = 0.0;
                interact.swinging = false;
            }
            const float s = interact.swinging ? std::sin(static_cast<float>(swing_t) * 3.14159265f) : 0.0f;
            const client::StandInVisual held = client::stand_in_visual_of(item_registry, interact.selected_stack,
                                                                          world.water_block_id(), interact.vessels);
            if (held.block != gam::kNoBlock) {
                glClear(GL_DEPTH_BUFFER_BIT);
                glDisable(GL_CULL_FACE);
                crack_shader.use();
                glUniformMatrix4fv(crack_shader.uniform_location("u_mvp"), 1, GL_FALSE, &projection[0][0]);
                glUniform3f(crack_shader.uniform_location("u_offset"), 0.42f - s * 0.16f, -0.42f - s * 0.14f,
                            -0.80f - s * 0.12f);
                glUniform1f(crack_shader.uniform_location("u_scale"), 0.32f);
                crack_vao.bind();
                // build_cube_geometry face order: f0 top, f1 bottom, f2..f5 sides.
                glUniform1f(crack_shader.uniform_location("u_tile"), static_cast<float>(held.block * 3 + 1));
                glDrawArrays(GL_TRIANGLES, 12, 24); // 4 side faces
                glUniform1f(crack_shader.uniform_location("u_tile"), static_cast<float>(held.block * 3 + 0));
                glDrawArrays(GL_TRIANGLES, 0, 6); // top
                glUniform1f(crack_shader.uniform_location("u_tile"), static_cast<float>(held.block * 3 + 2));
                glDrawArrays(GL_TRIANGLES, 6, 6); // bottom
                glEnable(GL_CULL_FACE);
            }
        }

        // ── crosshair ───────────────────────────────────────────────────────
        glDisable(GL_DEPTH_TEST);
        wire_shader.use();
        static const glm::mat4 kIdentity(1.0f);
        glUniformMatrix4fv(wire_shader.uniform_location("u_mvp"), 1, GL_FALSE, &kIdentity[0][0]);
        glUniform3f(wire_shader.uniform_location("u_offset"), 0.0f, 0.0f, 0.0f);
        glUniform1f(wire_shader.uniform_location("u_scale"), 1.0f);
        glUniform4f(wire_shader.uniform_location("u_color"), 0.92f, 0.92f, 0.92f, 0.85f);
        crosshair_vao.bind();
        glDrawArrays(GL_LINES, 0, 4);
        glEnable(GL_DEPTH_TEST);

        // ── HUD: hotbar (the inventory's 9 cells + item name + counts), the
        //    attack charge bar (T-D59) and health hearts (T009); hidden while
        //    dead (T-D45) ─────────────────────────────────────────────────────
        const client::HudState hud_state{
            .fb_width = fb_width,
            .fb_height = fb_height,
            .hotbar = std::span<const gam::ItemStack, gam::kHotbarSlots>(interact.inventory.slots().data(),
                                                                         gam::kHotbarSlots),
            .items = &item_registry,
            .vessels = interact.vessels,
            .selected_slot = interact.selected_slot,
            .health = curr_state.health,
            .attack_charge = interact.attack_charge(),
            .dead = life.dead,
        };
        client::draw_hud(hud_res, hud_state);

        // ── the death screen (T-D45) ────────────────────────────────────────
        // Modal: it replaces the pause menu while the player is dead (ESC is
        // ignored in that state, above). The click is edge-detected so a held
        // button cannot respawn the player repeatedly - and the whole 动作 runs
        // here, in one place: respawn, then the inventory rule (§5.6/§2.6), then
        // the client-side presentation that belonged to the corpse.
        if (life.dead) {
            const client::DeathScreenOut screen =
                client::draw_death_screen(death_res, prev_death_clicked, fb_width, fb_height);
            prev_death_clicked = screen.clicked;
            if (screen.respawn_clicked) {
                client::respawn_player(curr_state, life);
                client::respawn_clear_inventory(interact.inventory, rules);
                interact.refresh_selection();
                // The teleport must not leave the crosshair's wireframe around a
                // block hundreds of blocks away (interaction.hpp).
                interact.clear_live_state();
                prev_state = curr_state;
                prev_death_clicked = true; // the button is still held this frame
                OC_LOG_INFO("respawned at ({:.2f}, {:.2f}, {:.2f}); health {:.1f}, {} slot(s) in hand",
                            curr_state.position.x, curr_state.position.y, curr_state.position.z, curr_state.health,
                            interact.inventory.used_slots());
            }
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);
            glDisable(GL_BLEND);
        } else if (paused) {
            const client::PauseMenuOut menu =
                client::draw_pause_menu(pause_res, auto_jump_enabled, prev_menu_clicked, fb_width, fb_height);
            if (menu.resume) {
                paused = false;
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                tick_clock.reset();
                cursor_anchored = false;
            }
            auto_jump_enabled = menu.auto_jump_enabled;
            prev_menu_clicked = menu.clicked;
            if (menu.quit) {
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);
            glDisable(GL_BLEND);
        }
        glDisable(GL_BLEND);

        glfwSwapBuffers(window);

        ++fps_frames;
        if (now - fps_timer >= 2.0) {
            OC_LOG_INFO("fps {:.1f} | pos ({:.2f}, {:.2f}, {:.2f}) | chunks {} | stream-meshed {}",
                        fps_frames / (now - fps_timer), curr_state.position.x, curr_state.position.y,
                        curr_state.position.z, renderables.size(), stream_meshed);
            fps_frames = 0;
            fps_timer = now;
        }
    }

    glfwDestroyWindow(window);

    // ── exit: force flush (T009: 退出时强制 flush，QUIT 与窗口关闭共用此路径) ──
    // The persist window in request form (T-D4): the same request the frame loop
    // sends, with no generation budget, plus `persist`. The release window in it
    // is the normal one around the player, so this is a write-back of what is
    // still dirty - not a teardown of the world.
    gam::StreamRequest exit_flush = client::make_stream_request(curr_state.position);
    exit_flush.persist = true;
    static_cast<void>(authority.stream(exit_flush));
    save.write_level_now(client::make_level_data(tick_ctx));
    save.flush();
    OC_LOG_INFO("save: flushed on exit (ticks={}, chunks cached={})", game_ticks, save.cached_region_count());

    glfwTerminate();
    OC_LOG_INFO("clean shutdown");
    return 0;
}
