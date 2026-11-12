#ifndef __GOEMON_CONFIG_H__
#define __GOEMON_CONFIG_H__

#include <filesystem>
#include <string_view>
#include "ultramodern/config.hpp"
#include "recomp_input.h"

namespace goemon64 {
    // Own data folder (saves, settings, mods), apart from Goemon 64: Recompiled's (user's choice 2026-11-03, before v0.1.0).
    constexpr std::u8string_view program_id = u8"GGARecompiled";
    constexpr std::string_view program_name = "GGA: Recompiled";

    // TODO: Move loading configs to the runtime once we have a way to allow per-project customization.
    void load_config();
    void save_config();
    
    void reset_input_bindings();
    void reset_cont_input_bindings();
    void reset_kb_input_bindings();
    void reset_single_input_binding(recomp::InputDevice device, recomp::GameInput input);

    std::filesystem::path get_app_folder_path();
    
    bool get_debug_mode_enabled();
    void set_debug_mode_enabled(bool enabled);
    
    enum class AutosaveMode {
        On,
        Off,
        OptionCount
    };

    NLOHMANN_JSON_SERIALIZE_ENUM(goemon64::AutosaveMode, {
        {goemon64::AutosaveMode::On, "On"},
        {goemon64::AutosaveMode::Off, "Off"}
    });

    enum class TargetingMode {
        Switch,
        Hold,
        OptionCount
    };

    NLOHMANN_JSON_SERIALIZE_ENUM(goemon64::TargetingMode, {
        {goemon64::TargetingMode::Switch, "Switch"},
        {goemon64::TargetingMode::Hold, "Hold"}
    });

    TargetingMode get_targeting_mode();
    void set_targeting_mode(TargetingMode mode);

    enum class CameraInvertMode {
        InvertNone,
        InvertX,
        InvertY,
        InvertBoth,
        OptionCount
    };

    NLOHMANN_JSON_SERIALIZE_ENUM(goemon64::CameraInvertMode, {
        {goemon64::CameraInvertMode::InvertNone, "InvertNone"},
        {goemon64::CameraInvertMode::InvertX, "InvertX"},
        {goemon64::CameraInvertMode::InvertY, "InvertY"},
        {goemon64::CameraInvertMode::InvertBoth, "InvertBoth"}
    });

    CameraInvertMode get_camera_invert_mode();
    void set_camera_invert_mode(CameraInvertMode mode);

    CameraInvertMode get_analog_camera_invert_mode();
    void set_analog_camera_invert_mode(CameraInvertMode mode);

    enum class AnalogCamMode {
        On,
        Off,
		OptionCount
    };

    NLOHMANN_JSON_SERIALIZE_ENUM(goemon64::AnalogCamMode, {
        {goemon64::AnalogCamMode::On, "On"},
        {goemon64::AnalogCamMode::Off, "Off"}
    });

    AutosaveMode get_autosave_mode();
    void set_autosave_mode(AutosaveMode mode);

    AnalogCamMode get_analog_cam_mode();
    void set_analog_cam_mode(AnalogCamMode mode);

    // GGA: how far the scenery is drawn (decor culling and projection far plane, src/game/ga_widescreen.cpp).
    enum class DrawDistance {
        Original,
        X2,
        OptionCount
    };

    NLOHMANN_JSON_SERIALIZE_ENUM(goemon64::DrawDistance, {
        {goemon64::DrawDistance::Original, "Original"},
        {goemon64::DrawDistance::X2, "x2"}
    });

    DrawDistance get_draw_distance();
    void set_draw_distance(DrawDistance mode);

    // GGA: Sasuke and Yae playable from the start (character select, src/game/ga_hud.c), like the N64 cheat code.
    enum class UnlockCharacters {
        Off,
        On,
        OptionCount
    };

    NLOHMANN_JSON_SERIALIZE_ENUM(goemon64::UnlockCharacters, {
        {goemon64::UnlockCharacters::Off, "Off"},
        {goemon64::UnlockCharacters::On, "On"}
    });

    UnlockCharacters get_unlock_characters();
    void set_unlock_characters(UnlockCharacters mode);

    // GGA: finished (ruined) castles can be entered again from the world map (src/game/ga_widescreen.cpp, ga.toml).
    enum class ReplayCastles {
        Off,
        On,
        OptionCount
    };

    NLOHMANN_JSON_SERIALIZE_ENUM(goemon64::ReplayCastles, {
        {goemon64::ReplayCastles::Off, "Off"},
        {goemon64::ReplayCastles::On, "On"}
    });

    ReplayCastles get_replay_castles();
    void set_replay_castles(ReplayCastles mode);

    // GGA: START on the world map saves (Enhanced tab; src/game/ga_hud.c ga_map_save, map header's 3rd line).
    enum class SaveAnywhere {
        Off,
        On,
        OptionCount
    };

    NLOHMANN_JSON_SERIALIZE_ENUM(goemon64::SaveAnywhere, {
        {goemon64::SaveAnywhere::Off, "Off"},
        {goemon64::SaveAnywhere::On, "On"}
    });

    SaveAnywhere get_save_anywhere();
    void set_save_anywhere(SaveAnywhere mode);

    // GGA: "Use European Version" (General tab): the launcher shows logo_eur.png instead of logo_us.png
    // (src/ui/ui_launcher.cpp, assets/launcher.rml) and goemon2.eu.z64 is read at boot (src/game/ga_lang_eu.cpp).
    enum class UseEuropeanVersion {
        Off,
        On,
        OptionCount
    };

    NLOHMANN_JSON_SERIALIZE_ENUM(goemon64::UseEuropeanVersion, {
        {goemon64::UseEuropeanVersion::Off, "Off"},
        {goemon64::UseEuropeanVersion::On, "On"}
    });

    UseEuropeanVersion get_use_european_version();
    void set_use_european_version(UseEuropeanVersion mode);

    // GGA: RT64 developer mode (F1 = inspector), read when the renderer is created (src/main/rt64_render_context.cpp).
    enum class Rt64DeveloperMode {
        Off,
        On,
        OptionCount
    };

    NLOHMANN_JSON_SERIALIZE_ENUM(goemon64::Rt64DeveloperMode, {
        {goemon64::Rt64DeveloperMode::Off, "Off"},
        {goemon64::Rt64DeveloperMode::On, "On"}
    });

    Rt64DeveloperMode get_rt64_developer_mode();
    void set_rt64_developer_mode(Rt64DeveloperMode mode);

    void open_quit_game_prompt();
};

#endif
