#include <memory>
#include <cstring>
#include <variant>
#include <algorithm>

#define HLSL_CPU
#include "hle/rt64_application.h"
#include "rt64_render_hooks.h"
#include "overloaded.h"

#include "ultramodern/ultramodern.hpp"
#include "ultramodern/config.hpp"

#include "goemon_render.h"
#include "goemon_config.h"
#include "recomp_ui.h"
#include "concurrentqueue.h"

static RT64::UserConfiguration::Antialiasing device_max_msaa = RT64::UserConfiguration::Antialiasing::None;
static bool sample_positions_supported = false;
static bool high_precision_fb_enabled = false;

static uint8_t DMEM[0x1000];
static uint8_t IMEM[0x1000];

struct TexturePackEnableAction {
    std::string mod_id;
};

struct TexturePackDisableAction {
    std::string mod_id;
};

struct TexturePackSecondaryEnableAction {
    std::string mod_id;
};

struct TexturePackSecondaryDisableAction {
    std::string mod_id;
};

struct TexturePackUpdateAction {
};

using TexturePackAction = std::variant<TexturePackEnableAction, TexturePackDisableAction, TexturePackSecondaryEnableAction, TexturePackSecondaryDisableAction, TexturePackUpdateAction>;

static moodycamel::ConcurrentQueue<TexturePackAction> texture_pack_action_queue;

unsigned int MI_INTR_REG = 0;

unsigned int DPC_START_REG = 0;
unsigned int DPC_END_REG = 0;
unsigned int DPC_CURRENT_REG = 0;
unsigned int DPC_STATUS_REG = 0;
unsigned int DPC_CLOCK_REG = 0;
unsigned int DPC_BUFBUSY_REG = 0;
unsigned int DPC_PIPEBUSY_REG = 0;
unsigned int DPC_TMEM_REG = 0;

void dummy_check_interrupts() {}

RT64::UserConfiguration::Antialiasing compute_max_supported_aa(plume::RenderSampleCounts bits) {
    if (bits & plume::RenderSampleCount::Bits::COUNT_2) {
        if (bits & plume::RenderSampleCount::Bits::COUNT_4) {
            if (bits & plume::RenderSampleCount::Bits::COUNT_8) {
                return RT64::UserConfiguration::Antialiasing::MSAA8X;
            }
            return RT64::UserConfiguration::Antialiasing::MSAA4X;
        }
        return RT64::UserConfiguration::Antialiasing::MSAA2X;
    };
    return RT64::UserConfiguration::Antialiasing::None;
}

RT64::UserConfiguration::AspectRatio to_rt64(ultramodern::renderer::AspectRatio option) {
    switch (option) {
    case ultramodern::renderer::AspectRatio::Original:
        return RT64::UserConfiguration::AspectRatio::Original;
    case ultramodern::renderer::AspectRatio::Expand:
        return RT64::UserConfiguration::AspectRatio::Expand;
    case ultramodern::renderer::AspectRatio::Manual:
        return RT64::UserConfiguration::AspectRatio::Manual;
    case ultramodern::renderer::AspectRatio::OptionCount:
        return RT64::UserConfiguration::AspectRatio::OptionCount;
    }
}

RT64::UserConfiguration::Antialiasing to_rt64(ultramodern::renderer::Antialiasing option) {
    switch (option) {
    case ultramodern::renderer::Antialiasing::None:
        return RT64::UserConfiguration::Antialiasing::None;
    case ultramodern::renderer::Antialiasing::MSAA2X:
        return RT64::UserConfiguration::Antialiasing::MSAA2X;
    case ultramodern::renderer::Antialiasing::MSAA4X:
        return RT64::UserConfiguration::Antialiasing::MSAA4X;
    case ultramodern::renderer::Antialiasing::MSAA8X:
        return RT64::UserConfiguration::Antialiasing::MSAA8X;
    case ultramodern::renderer::Antialiasing::OptionCount:
        return RT64::UserConfiguration::Antialiasing::OptionCount;
    }
}

RT64::UserConfiguration::RefreshRate to_rt64(ultramodern::renderer::RefreshRate option) {
    switch (option) {
    case ultramodern::renderer::RefreshRate::Original:
        return RT64::UserConfiguration::RefreshRate::Original;
    case ultramodern::renderer::RefreshRate::Display:
        return RT64::UserConfiguration::RefreshRate::Display;
    case ultramodern::renderer::RefreshRate::Manual:
        return RT64::UserConfiguration::RefreshRate::Manual;
    case ultramodern::renderer::RefreshRate::OptionCount:
        return RT64::UserConfiguration::RefreshRate::OptionCount;
    }
}

RT64::UserConfiguration::InternalColorFormat to_rt64(ultramodern::renderer::HighPrecisionFramebuffer option) {
    switch (option) {
    case ultramodern::renderer::HighPrecisionFramebuffer::Off:
        return RT64::UserConfiguration::InternalColorFormat::Standard;
    case ultramodern::renderer::HighPrecisionFramebuffer::On:
        return RT64::UserConfiguration::InternalColorFormat::High;
    case ultramodern::renderer::HighPrecisionFramebuffer::Auto:
        return RT64::UserConfiguration::InternalColorFormat::Automatic;
    case ultramodern::renderer::HighPrecisionFramebuffer::OptionCount:
        return RT64::UserConfiguration::InternalColorFormat::OptionCount;
    }
}

void set_application_user_config(RT64::Application* application, const ultramodern::renderer::GraphicsConfig& config) {
    switch (config.res_option) {
    default:
    case ultramodern::renderer::Resolution::Auto:
        application->userConfig.resolution = RT64::UserConfiguration::Resolution::WindowIntegerScale;
        application->userConfig.downsampleMultiplier = 1;
        break;
    case ultramodern::renderer::Resolution::Original:
        application->userConfig.resolution = RT64::UserConfiguration::Resolution::Manual;
        application->userConfig.resolutionMultiplier = std::max(config.ds_option, 1);
        application->userConfig.downsampleMultiplier = std::max(config.ds_option, 1);
        break;
    case ultramodern::renderer::Resolution::Original2x:
        application->userConfig.resolution = RT64::UserConfiguration::Resolution::Manual;
        application->userConfig.resolutionMultiplier = 2.0 * std::max(config.ds_option, 1);
        application->userConfig.downsampleMultiplier = std::max(config.ds_option, 1);
        break;
    }

    switch (config.hr_option) {
    default:
    case ultramodern::renderer::HUDRatioMode::Original:
        application->userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Original;
        break;
    case ultramodern::renderer::HUDRatioMode::Clamp16x9:
        application->userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Manual;
        application->userConfig.extAspectTarget = 16.0/9.0;
        break;
    case ultramodern::renderer::HUDRatioMode::Full:
        application->userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Expand;
        break;
    }
    // GGA (2026-11-08): always Full. The only extended LEFT / RIGHT origins in GGA are the panorama's scissor (ga_pano_begin,
    // ga_widescreen.cpp), which must reach the window edges at any aspect (ultrawide); with Clamp16x9 (the default, not in
    // the menus) it stopped at the 16:9 center. Nothing else uses those origins (HUD: origins NONE).
    application->userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Expand;

    application->userConfig.aspectRatio = to_rt64(config.ar_option);
    application->userConfig.antialiasing = to_rt64(config.msaa_option);
    application->userConfig.refreshRate = to_rt64(config.rr_option);
    application->userConfig.refreshRateTarget = config.rr_manual_value;
    application->userConfig.internalColorFormat = to_rt64(config.hpfb_option);
    application->userConfig.displayBuffering = RT64::UserConfiguration::DisplayBuffering::Triple;
}

ultramodern::renderer::SetupResult map_setup_result(RT64::Application::SetupResult rt64_result) {
    switch (rt64_result) {
    case RT64::Application::SetupResult::Success:
        return ultramodern::renderer::SetupResult::Success;
    case RT64::Application::SetupResult::DynamicLibrariesNotFound:
        return ultramodern::renderer::SetupResult::DynamicLibrariesNotFound;
    case RT64::Application::SetupResult::InvalidGraphicsAPI:
        return ultramodern::renderer::SetupResult::InvalidGraphicsAPI;
    case RT64::Application::SetupResult::GraphicsAPINotFound:
        return ultramodern::renderer::SetupResult::GraphicsAPINotFound;
    case RT64::Application::SetupResult::GraphicsDeviceNotFound:
        return ultramodern::renderer::SetupResult::GraphicsDeviceNotFound;
    }

    fprintf(stderr, "Unhandled `RT64::Application::SetupResult` ?\n");
    assert(false);
    std::exit(EXIT_FAILURE);
}

ultramodern::renderer::GraphicsApi map_graphics_api(RT64::UserConfiguration::GraphicsAPI api) {
    switch (api) {
    case RT64::UserConfiguration::GraphicsAPI::D3D12:
        return ultramodern::renderer::GraphicsApi::D3D12;
    case RT64::UserConfiguration::GraphicsAPI::Vulkan:
        return ultramodern::renderer::GraphicsApi::Vulkan;
    case RT64::UserConfiguration::GraphicsAPI::Metal:
        return ultramodern::renderer::GraphicsApi::Metal;
    case RT64::UserConfiguration::GraphicsAPI::Automatic:
        return ultramodern::renderer::GraphicsApi::Auto;
    }

    fprintf(stderr, "Unhandled `RT64::UserConfiguration::GraphicsAPI` ?\n");
    assert(false);
    std::exit(EXIT_FAILURE);
}

goemon64::renderer::RT64Context::RT64Context(uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool debug) {
    static unsigned char dummy_rom_header[0x40];
    recompui::set_render_hooks();

    // Set up the RT64 application core fields.
    RT64::Application::Core appCore{};
#if defined(_WIN32)
    appCore.window = window_handle.window;
#elif defined(__linux__) || defined(__ANDROID__)
    appCore.window = window_handle;
#elif defined(__APPLE__)
    appCore.window.window = window_handle.window;
    appCore.window.view = window_handle.view;
#endif

    appCore.checkInterrupts = dummy_check_interrupts;

    appCore.HEADER = dummy_rom_header;
    appCore.RDRAM = rdram;
    appCore.DMEM = DMEM;
    appCore.IMEM = IMEM;

    appCore.MI_INTR_REG = &MI_INTR_REG;

    appCore.DPC_START_REG = &DPC_START_REG;
    appCore.DPC_END_REG = &DPC_END_REG;
    appCore.DPC_CURRENT_REG = &DPC_CURRENT_REG;
    appCore.DPC_STATUS_REG = &DPC_STATUS_REG;
    appCore.DPC_CLOCK_REG = &DPC_CLOCK_REG;
    appCore.DPC_BUFBUSY_REG = &DPC_BUFBUSY_REG;
    appCore.DPC_PIPEBUSY_REG = &DPC_PIPEBUSY_REG;
    appCore.DPC_TMEM_REG = &DPC_TMEM_REG;

    ultramodern::renderer::ViRegs* vi_regs = ultramodern::renderer::get_vi_regs();

    appCore.VI_STATUS_REG = &vi_regs->VI_STATUS_REG;
    appCore.VI_ORIGIN_REG = &vi_regs->VI_ORIGIN_REG;
    appCore.VI_WIDTH_REG = &vi_regs->VI_WIDTH_REG;
    appCore.VI_INTR_REG = &vi_regs->VI_INTR_REG;
    appCore.VI_V_CURRENT_LINE_REG = &vi_regs->VI_V_CURRENT_LINE_REG;
    appCore.VI_TIMING_REG = &vi_regs->VI_TIMING_REG;
    appCore.VI_V_SYNC_REG = &vi_regs->VI_V_SYNC_REG;
    appCore.VI_H_SYNC_REG = &vi_regs->VI_H_SYNC_REG;
    appCore.VI_LEAP_REG = &vi_regs->VI_LEAP_REG;
    appCore.VI_H_START_REG = &vi_regs->VI_H_START_REG;
    appCore.VI_V_START_REG = &vi_regs->VI_V_START_REG;
    appCore.VI_V_BURST_REG = &vi_regs->VI_V_BURST_REG;
    appCore.VI_X_SCALE_REG = &vi_regs->VI_X_SCALE_REG;
    appCore.VI_Y_SCALE_REG = &vi_regs->VI_Y_SCALE_REG;

    // Set up the RT64 application configuration fields.
    RT64::ApplicationConfiguration appConfig;
    appConfig.useConfigurationFile = false;

    // Create the RT64 application.
    app = std::make_unique<RT64::Application>(appCore, appConfig);

    // Set initial user config settings based on the current settings.
    auto& cur_config = ultramodern::renderer::get_graphics_config();
    set_application_user_config(app.get(), cur_config);
    app->userConfig.developerMode = debug;
    // Prevent the upscaling of texture rectangles.
    app->userConfig.upscale2D = RT64::UserConfiguration::Upscale2D::Original;
    // Force gbi depth branches to prevent LODs from kicking in.
    app->enhancementConfig.f3dex.forceBranch = true;
    // Scale LODs based on the output resolution.
    app->enhancementConfig.textureLOD.scale = true;
    // Do not copy with GPU as it breaks effects in menus and the quality loss is negligible.
	// TODO: Fix the underlying issue in RT64 and re-enable GPU copying as it can cause issues in Goemon's Great Adventure's underwater sections (low resolution), 
    // TODO: Separate configs for the two games maybe?
    // GGA (2026-10-21, tested by the user): GPU copying ON. With CPU copies, the underwater wave effect (the frame copied
    // then redrawn distorted) was 320 x 240, at the game's frame rate and only in the 4:3 area, and the frame rate dropped near
    // water; with GPU copies it is full resolution, interpolated and full width. GGA_GPU_COPY=0 goes back to CPU copies (the
    // comment above: effects in menus may break; not seen so far).
    app->emulatorConfig.framebuffer.copyWithGPU = true;
    if (const char* gpu_copy = std::getenv("GGA_GPU_COPY"); gpu_copy != nullptr && gpu_copy[0] == '0') {
        app->emulatorConfig.framebuffer.copyWithGPU = false;
    }
    // Pick an API if the user has set an override.
    switch (cur_config.api_option) {
    case ultramodern::renderer::GraphicsApi::D3D12:
        app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::D3D12;
        break;
    case ultramodern::renderer::GraphicsApi::Vulkan:
        app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Vulkan;
        break;
    case ultramodern::renderer::GraphicsApi::Metal:
        app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Metal;
        break;
    case ultramodern::renderer::GraphicsApi::Auto:
        app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Automatic;
        break;
    }

    // Set up the RT64 application.
    uint32_t thread_id = 0;
#ifdef _WIN32
    thread_id = window_handle.thread_id;
#endif
    setup_result = map_setup_result(app->setup(thread_id));
    // Get the API that RT64 chose.
    chosen_api = map_graphics_api(app->chosenGraphicsAPI);
    if (setup_result != ultramodern::renderer::SetupResult::Success) {
        app = nullptr;
        return;
    }

    // Set the application's fullscreen state.
    app->setFullScreen(cur_config.wm_option == ultramodern::renderer::WindowMode::Fullscreen);

    // Check if the selected device actually supports MSAA sample positions and MSAA for for the formats that will be used
    // and downgrade the configuration accordingly.
    if (app->device->getCapabilities().sampleLocations) {
        plume::RenderSampleCounts color_sample_counts = app->device->getSampleCountsSupported(plume::RenderFormat::R8G8B8A8_UNORM);
        plume::RenderSampleCounts depth_sample_counts = app->device->getSampleCountsSupported(plume::RenderFormat::D32_FLOAT);
        plume::RenderSampleCounts common_sample_counts = color_sample_counts & depth_sample_counts;
        device_max_msaa = compute_max_supported_aa(common_sample_counts);
        sample_positions_supported = true;
    }
    else {
        device_max_msaa = RT64::UserConfiguration::Antialiasing::None;
        sample_positions_supported = false;
    }

    high_precision_fb_enabled = app->shaderLibrary->usesHDR;

    // GGA (2026-10-21): GGA_DUMP_TEXTURES=<folder> makes RT64 dump every texture used, named by its replacement hash (as its
    // inspector's "Start dumping textures"). Used by tools/hud_art.py pack to find the hashes of the HUD art proxies.
    if (const char* dump_dir = std::getenv("GGA_DUMP_TEXTURES"); dump_dir != nullptr && dump_dir[0] != '\0') {
        std::error_code ec;
        std::filesystem::create_directories(dump_dir, ec);
        app->state->dumpingTexturesDirectory = std::filesystem::path(dump_dir);
        fprintf(stderr, "[GGA DUMP] textures dumped to %s\n", dump_dir);
    }
}

goemon64::renderer::RT64Context::~RT64Context() = default;

// GGA diagnostic: walk an F3DEX2 display list the way RT64 does (G_DL push / branch, G_ENDDL, segments set by
// G_MOVEWORD G_MW_SEGMENT). If it leaves the 8 MB RDRAM or does not end, print the path taken to stderr (see --log).
// RT64 itself does not check this and runs off the end of memory (crash in Interpreter::processDisplayLists).
// Counters of the last walk (commands, G_LOAD_UCODE 0xDD, G_BRANCH_Z 0x04, S2DEX end 0xB8), printed per task.
static uint32_t gga_dl_steps, gga_dl_dd, gga_dl_04, gga_dl_b8;


static uint64_t gga_check_dl(uint8_t* rdram, uint32_t start, bool report = true) {
    gga_dl_steps = gga_dl_dd = gga_dl_04 = gga_dl_b8 = 0;
    static int reports = 0;
    uint64_t hash = 1469598103934665603ull;
    struct Hop { uint32_t from, to, w1; };
    uint32_t seg[16] = {};
    uint32_t stack[18];
    int sp = 0;
    Hop hops[48];
    int nh = 0;
    uint32_t last[16][3];
    uint32_t nl = 0;
    uint32_t pc = start;
    const char* why = "no G_ENDDL after 2000000 commands";
    for (uint32_t steps = 0; steps < 2000000; steps++) {
        if (pc + 8 > 0x00800000u) {
            why = "left the 8 MB RDRAM";
            break;
        }
        const uint32_t w0 = *reinterpret_cast<const uint32_t*>(rdram + pc);
        const uint32_t w1 = *reinterpret_cast<const uint32_t*>(rdram + pc + 4);
        last[nl % 16][0] = pc; last[nl % 16][1] = w0; last[nl % 16][2] = w1; nl++;
        hash = (hash ^ w0) * 1099511628211ull;
        hash = (hash ^ w1) * 1099511628211ull;
        const uint8_t op = w0 >> 24;
        gga_dl_steps++;
        gga_dl_dd += (op == 0xDD);
        gga_dl_04 += (op == 0x04);
        gga_dl_b8 += (op == 0xB8);
        if (op == 0xDF) { // G_ENDDL
            if (sp == 0) {
                return hash | 1; // ends normally (never 0)
            }
            pc = stack[--sp];
            continue;
        }
        if (op == 0xDB && ((w0 >> 16) & 0xFF) == 6) { // G_MOVEWORD G_MW_SEGMENT
            seg[(w0 >> 2) & 0xF] = w1;
        }
        if (op == 0xDE) { // G_DL
            const uint32_t to = (seg[(w1 >> 24) & 0xF] + (w1 & 0x00FFFFFF)) & 0x00FFFFF8;
            if (nh < 48) { hops[nh++] = { pc, to, w1 }; }
            if (((w0 >> 16) & 0xFF) == 0 && sp < 18) {
                stack[sp++] = pc + 8;
            }
            pc = to;
            continue;
        }
        pc += 8;
    }
    if (!report || reports >= 2) {
        return 0;
    }
    reports++;
    fprintf(stderr, "[GGA DL] display list 0x%08X does not end: %s (pc 0x%08X)\n", start, why, pc);
    for (int i = 0; i < 16; i++) {
        if (seg[i] != 0) { fprintf(stderr, "[GGA DL]   segment %X = 0x%08X\n", i, seg[i]); }
    }
    for (int i = 0; i < nh; i++) {
        fprintf(stderr, "[GGA DL]   G_DL at 0x%08X w1=0x%08X -> 0x%08X\n", hops[i].from, hops[i].w1, hops[i].to);
    }
    for (uint32_t i = (nl > 16 ? nl - 16 : 0); i < nl; i++) {
        fprintf(stderr, "[GGA DL]   0x%08X: %08X %08X\n", last[i % 16][0], last[i % 16][1], last[i % 16][2]);
    }
    return 0;
}

void goemon64::renderer::RT64Context::send_dl(const OSTask* task) {
    check_texture_pack_actions();
    const uint32_t dl_start = task->t.data_ptr & 0x3FFFFFF;
    const uint64_t before = gga_check_dl(app->core.RDRAM, dl_start);
    app->state->rsp->reset();
    app->interpreter->loadUCodeGBI(task->t.ucode & 0x3FFFFFF, task->t.ucode_data & 0x3FFFFFF, true);
    static uint32_t logged = 0;
    if (logged < 40) {
        logged++;
        const RT64::GBI* gbi = app->interpreter->hleGBI;
        fprintf(stderr, "[GGA TASK] type %u flags %X ucode %08X (size %X) data %08X dl %08X (size %X) -> RT64 GBI %d | walk: %u cmds, LOAD_UCODE %u, BRANCH_Z %u, 0xB8 %u, %s\n",
            task->t.type, task->t.flags, (uint32_t)task->t.ucode, task->t.ucode_size, (uint32_t)task->t.ucode_data,
            (uint32_t)task->t.data_ptr, task->t.data_size, gbi ? (int)gbi->ucode : -1,
            gga_dl_steps, gga_dl_dd, gga_dl_04, gga_dl_b8, before ? "ends" : "DOES NOT END");
    }
    app->processDisplayLists(app->core.RDRAM, dl_start, 0, true);
    // GGA diagnostic: the runtime tells the game the RSP is done BEFORE this pass; did the game rewrite the list meanwhile?
    static uint32_t frames = 0, changed = 0;
    frames++;
    const uint64_t after = gga_check_dl(app->core.RDRAM, dl_start, false);
    if (before != after) {
        changed++;
        if (changed <= 20 || (changed % 100) == 0) {
            fprintf(stderr, "[GGA DL] list 0x%08X CHANGED during RT64's pass (%u of %u lists so far)%s\n",
                dl_start, changed, frames, after == 0 ? ", and no longer ends" : "");
        }
    }
    if ((frames % 300) == 0) {
        fprintf(stderr, "[GGA DL] %u lists processed, %u changed during the pass\n", frames, changed);
    }
}

void goemon64::renderer::RT64Context::update_screen() {
    app->updateScreen();
}

void goemon64::renderer::RT64Context::shutdown() {
    if (app != nullptr) {
        app->end();
    }
}

bool goemon64::renderer::RT64Context::update_config(const ultramodern::renderer::GraphicsConfig& old_config, const ultramodern::renderer::GraphicsConfig& new_config) {
    if (old_config == new_config) {
        return false;
    }

    if (new_config.wm_option != old_config.wm_option) {
        app->setFullScreen(new_config.wm_option == ultramodern::renderer::WindowMode::Fullscreen);
    }

    set_application_user_config(app.get(), new_config);

    app->updateUserConfig(true);

    if (new_config.msaa_option != old_config.msaa_option) {
        app->updateMultisampling();
    }
    return true;
}

void goemon64::renderer::RT64Context::enable_instant_present() {
    // Enable the present early presentation mode for minimal latency.
    app->enhancementConfig.presentation.mode = RT64::EnhancementConfiguration::Presentation::Mode::PresentEarly;

    app->updateEnhancementConfig();
}

uint32_t goemon64::renderer::RT64Context::get_display_framerate() const {
    return app->presentQueue->ext.sharedResources->swapChainRate;
}

float goemon64::renderer::RT64Context::get_resolution_scale() const {
    constexpr int ReferenceHeight = 240;
    switch (app->userConfig.resolution) {
    case RT64::UserConfiguration::Resolution::WindowIntegerScale:
        if (app->sharedQueueResources->swapChainHeight > 0) {
            return std::max(float((app->sharedQueueResources->swapChainHeight + ReferenceHeight - 1) / ReferenceHeight), 1.0f);
        }
        else {
            return 1.0f;
        }
    case RT64::UserConfiguration::Resolution::Manual:
        return float(app->userConfig.resolutionMultiplier);
    case RT64::UserConfiguration::Resolution::Original:
    default:
        return 1.0f;
    }
}

void goemon64::renderer::RT64Context::check_texture_pack_actions() {
    bool packs_changed = false;
    TexturePackAction cur_action;
    while (texture_pack_action_queue.try_dequeue(cur_action)) {
        std::visit(overloaded{
            [&](TexturePackDisableAction &to_disable) {
                enabled_texture_packs.erase(to_disable.mod_id);
                packs_changed = true;
            },
            [&](TexturePackEnableAction &to_enable) {
                enabled_texture_packs.insert(to_enable.mod_id);
                packs_changed = true;
            },
            [&](TexturePackSecondaryDisableAction &to_override_disable) {
                secondary_disabled_texture_packs.insert(to_override_disable.mod_id);
                packs_changed = true;
            },
            [&](TexturePackSecondaryEnableAction &to_override_enable) {
                secondary_disabled_texture_packs.erase(to_override_enable.mod_id);
                packs_changed = true;
            },
            [&](TexturePackUpdateAction &) {
                packs_changed = true;
            }
            }, cur_action);
    }

    // If any packs were disabled, unload all packs and load all the active ones.
    if (packs_changed) {
        // Sort the enabled texture packs in reverse order so that earlier ones override later ones.
        std::vector<std::string> sorted_texture_packs{};
        sorted_texture_packs.reserve(enabled_texture_packs.size());
        for (const std::string& mod : enabled_texture_packs) {
            if (!secondary_disabled_texture_packs.contains(mod)) {
                sorted_texture_packs.emplace_back(mod);
            }
        }

        std::sort(sorted_texture_packs.begin(), sorted_texture_packs.end(),
            [](const std::string& lhs, const std::string& rhs) {
                return recomp::mods::get_mod_order_index(lhs) > recomp::mods::get_mod_order_index(rhs);
            }
        );

        // Build the path list from the sorted mod list.
        std::vector<RT64::ReplacementDirectory> replacement_directories;
        replacement_directories.reserve(enabled_texture_packs.size());
        for (const std::string &mod_id : sorted_texture_packs) {
            replacement_directories.emplace_back(RT64::ReplacementDirectory(recomp::mods::get_mod_filename(mod_id)));
        }

        if (!replacement_directories.empty()) {
            app->textureCache->loadReplacementDirectories(replacement_directories);
        }
        else {
            app->textureCache->clearReplacementDirectories();
        }
    }
}

RT64::UserConfiguration::Antialiasing goemon64::renderer::RT64MaxMSAA() {
    return device_max_msaa;
}

std::unique_ptr<ultramodern::renderer::RendererContext> goemon64::renderer::create_render_context(uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool developer_mode) {
    // GGA (2026-11-03): RT64 developer mode (F1 = inspector) from the General menu option "RT64 Developer Mode": developer_mode
    // from graphics.json does not reach the running app (cause not searched).
    const bool dev = developer_mode || goemon64::get_rt64_developer_mode() == goemon64::Rt64DeveloperMode::On;
    return std::make_unique<goemon64::renderer::RT64Context>(rdram, window_handle, dev);
}

bool goemon64::renderer::RT64SamplePositionsSupported() {
    return sample_positions_supported;
}

bool goemon64::renderer::RT64HighPrecisionFBEnabled() {
    return high_precision_fb_enabled;
}

void goemon64::renderer::trigger_texture_pack_update() {
    texture_pack_action_queue.enqueue(TexturePackUpdateAction{});
}

void goemon64::renderer::enable_texture_pack(const recomp::mods::ModContext& context, const recomp::mods::ModHandle& mod) {
    texture_pack_action_queue.enqueue(TexturePackEnableAction{mod.manifest.mod_id});

    // Check for the texture pack enabled config option.
    const recomp::mods::ConfigSchema& config_schema = context.get_mod_config_schema(mod.manifest.mod_id);
    auto find_it = config_schema.options_by_id.find(goemon64::renderer::special_option_texture_pack_enabled);
    if (find_it != config_schema.options_by_id.end()) {
        const recomp::mods::ConfigOption& config_option = config_schema.options[find_it->second];

        if (is_texture_pack_enable_config_option(config_option, false)) {
            recomp::mods::ConfigValueVariant value_variant = context.get_mod_config_value(mod.manifest.mod_id, config_option.id);
            uint32_t value;
            if (uint32_t* value_ptr = std::get_if<uint32_t>(&value_variant)) {
                value = *value_ptr;
            }
            else {
                value = 0;
            }

            if (value) {
                goemon64::renderer::secondary_enable_texture_pack(mod.manifest.mod_id);
            }
            else {
                goemon64::renderer::secondary_disable_texture_pack(mod.manifest.mod_id);
            }
        }
    }
}

void goemon64::renderer::disable_texture_pack(const recomp::mods::ModHandle& mod) {
    texture_pack_action_queue.enqueue(TexturePackDisableAction{mod.manifest.mod_id});
}

void goemon64::renderer::secondary_enable_texture_pack(const std::string& mod_id) {
    texture_pack_action_queue.enqueue(TexturePackSecondaryEnableAction{mod_id});
}

void goemon64::renderer::secondary_disable_texture_pack(const std::string& mod_id) {
    texture_pack_action_queue.enqueue(TexturePackSecondaryDisableAction{mod_id});
}


// HD texture enable option. Must be an enum with two options.
// The first option is treated as disabled and the second option is treated as enabled.
bool goemon64::renderer::is_texture_pack_enable_config_option(const recomp::mods::ConfigOption& option, bool show_errors) {
    if (option.id == goemon64::renderer::special_option_texture_pack_enabled) {
        if (option.type != recomp::mods::ConfigOptionType::Enum) {
            if (show_errors) {
                recompui::message_box(("Mod has the special config option id for enabling an HD texture pack (\"" + goemon64::renderer::special_option_texture_pack_enabled + "\"), but the config option is not an enum.").c_str());
            }
            return false;
        }

        const recomp::mods::ConfigOptionEnum &option_enum = std::get<recomp::mods::ConfigOptionEnum>(option.variant);
        if (option_enum.options.size() != 2) {
            if (show_errors) {
                recompui::message_box(("Mod has the special config option id for enabling an HD texture pack (\"" + goemon64::renderer::special_option_texture_pack_enabled + "\"), but the config option doesn't have exactly 2 values.").c_str());
            }
            return false;
        }

        return true;
    }
    return false;
}
