#include "recomp_ui.h"
#include "goemon_config.h"
#include "goemon_support.h"
#include "librecomp/game.hpp"
#include "ultramodern/ultramodern.hpp"
#include "RmlUi/Core.h"
#include "nfd.h"
#include <filesystem>
#include <fstream>
#include <chrono>

static std::string version_string;

Rml::DataModelHandle model_handle;
bool mm_rom_valid = false;

extern std::vector<recomp::GameEntry> supported_games;

void select_rom() {
    nfdnchar_t* native_path = nullptr;
    goemon64::open_file_dialog([](bool success, const std::filesystem::path& path) {
        if (success) {
            recomp::RomValidationError rom_error = recomp::select_rom(path, supported_games[0].game_id);
            switch (rom_error) {
                case recomp::RomValidationError::Good:
                    mm_rom_valid = true;
                    model_handle.DirtyVariable("mm_rom_valid");
                    break;
                case recomp::RomValidationError::FailedToOpen:
                    recompui::message_box("Failed to open ROM file.");
                    break;
                case recomp::RomValidationError::NotARom:
                    recompui::message_box("This is not a valid ROM file.");
                    break;
                case recomp::RomValidationError::IncorrectRom:
                    recompui::message_box("This ROM is not the correct game.");
                    break;
                case recomp::RomValidationError::NotYet:
                    recompui::message_box("This game isn't supported yet.");
                    break;
                case recomp::RomValidationError::IncorrectVersion:
                    recompui::message_box(
                            "This ROM is the correct game, but the wrong version.\nThis project requires the NTSC-U N64 version of the game.");
                    break;
                case recomp::RomValidationError::OtherError:
                    recompui::message_box("An unknown error has occurred.");
                    break;
            }
        }
    });
}

// GGA: "Use European Version" changed (ui_config.cpp)
void gga_launcher_logo_changed() {
    if (model_handle) {
        model_handle.DirtyVariable("use_european_version");
    }
}

recompui::ContextId launcher_context;

// GGA: launcher intro = the animations of launcher.rml (white fade, logo, menu), ui_state.cpp blocks the menus meanwhile.
static bool intro_skipped = false; // class gga-skip: animations cut, everything shown
static bool intro_over = false;

bool gga_launcher_intro_active() {
    using clock = std::chrono::steady_clock;
    constexpr auto intro_length = std::chrono::milliseconds{4600}; // end of the menu fade-in (gga-menu-in, launcher.rml)
    static clock::time_point start{};
    if (intro_skipped || intro_over || !recompui::is_context_shown(launcher_context)) {
        return false;
    }
    if (start == clock::time_point{}) {
        start = clock::now(); // first frame with the launcher shown: the animations start now
    }
    intro_over = clock::now() - start >= intro_length;
    return !intro_over;
}

void gga_launcher_intro_skip() {
    intro_skipped = true;
    if (model_handle) {
        model_handle.DirtyVariable("intro_skipped");
    }
}

recompui::ContextId recompui::get_launcher_context_id() {
	return launcher_context;
}

class LauncherMenu : public recompui::MenuController {
public:
    LauncherMenu() {
        mm_rom_valid = recomp::is_rom_valid(supported_games[0].game_id);
    }
    ~LauncherMenu() override {

    }
    void load_document() override {
        // GGA: launcher art (PNG, not supported by the SVG loader), referenced as ?/gga/<name> in launcher.rml
        for (const char* name : { "background", "logo_us", "logo_eur" }) {
            std::ifstream file(goemon64::get_asset_path((std::string(name) + ".png").c_str()), std::ios::binary);
            std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
            if (!bytes.empty()) {
                recompui::queue_image_from_bytes_file(std::string("?/gga/") + name, bytes);
            }
        }
		launcher_context = recompui::create_context(goemon64::get_asset_path("launcher.rml"));
    }
    void register_events(recompui::UiEventListenerInstancer& listener) override {
        recompui::register_event(listener, "select_rom",
            [](const std::string& param, Rml::Event& event) {
                select_rom();
            }
        );
        recompui::register_event(listener, "rom_selected",
            [](const std::string& param, Rml::Event& event) {
                mm_rom_valid = true;
                model_handle.DirtyVariable("mm_rom_valid");
            }
        );
        recompui::register_event(listener, "start_game",
            [](const std::string& param, Rml::Event& event) {
                recomp::start_game(supported_games[0].game_id);
                recompui::hide_all_contexts();
            }
        );
        recompui::register_event(listener, "open_controls",
            [](const std::string& param, Rml::Event& event) {
                recompui::set_config_tab(recompui::ConfigTab::Controls);
                recompui::hide_all_contexts();
                recompui::show_context(recompui::get_config_context_id(), "");
            }
        );
        recompui::register_event(listener, "open_settings",
            [](const std::string& param, Rml::Event& event) {
                recompui::set_config_tab(recompui::ConfigTab::General);
                recompui::hide_all_contexts();
                recompui::show_context(recompui::get_config_context_id(), "");
            }
        );
        recompui::register_event(listener, "open_mods",
            [](const std::string &param, Rml::Event &event) {
                recompui::set_config_tab(recompui::ConfigTab::Mods);
                recompui::hide_all_contexts();
                recompui::show_context(recompui::get_config_context_id(), "");
            }
        );
        recompui::register_event(listener, "exit_game",
            [](const std::string& param, Rml::Event& event) {
                ultramodern::quit();
            }
        );
    }
    void make_bindings(Rml::Context* context) override {
        Rml::DataModelConstructor constructor = context->CreateDataModel("launcher_model");

        constructor.Bind("mm_rom_valid", &mm_rom_valid);
        constructor.Bind("intro_skipped", &intro_skipped);
        constructor.BindFunc("use_european_version",[](Rml::Variant& out) {
            out = goemon64::get_use_european_version() == goemon64::UseEuropeanVersion::On;
        });

        version_string = recomp::get_project_version().to_string();
        constructor.Bind("version_number", &version_string);

        model_handle = constructor.GetModelHandle();
    }
};

std::unique_ptr<recompui::MenuController> recompui::create_launcher_menu() {
    return std::make_unique<LauncherMenu>();
}
