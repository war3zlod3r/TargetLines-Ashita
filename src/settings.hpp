// TargetLines for Ashita v4 - persistent settings.
//
// Settings are stored as a plain INI file under Ashita's config folder:
//   <Ashita>/config/targetlines/targetlines.ini

#ifndef TARGETLINES_SETTINGS_HPP_INCLUDED
#define TARGETLINES_SETTINGS_HPP_INCLUDED

#include "state.hpp"

#include <string>

namespace targetlines
{
    struct Settings
    {
        // Master toggle and source categories.
        bool enabled                = true;
        bool show_player_lines      = true;
        bool show_party_lines       = true;
        bool show_pet_lines         = true;
        bool show_enemy_lines       = true;
        bool show_other_party_lines = false;
        bool show_special_lines     = true;

        AoeMode aoe_mode                 = AoeMode::Ring1;
        RegularMode regular_attack_mode  = RegularMode::First;
        bool color_blind_mode            = false;

        // Visual scales. All but aoe_opacity_scale are clamped to 0.5 - 2.0.
        float opacity_scale        = 1.00f;
        float player_opacity_scale = 0.70f;
        float ally_opacity_scale   = 0.70f;
        float enemy_opacity_scale  = 0.70f;
        float fade_scale           = 1.00f;
        float width_scale          = 1.00f;
        float glow_scale           = 1.00f;
        float source_height_scale  = 1.00f;
        float target_height_scale  = 1.00f;
        float aoe_opacity_scale    = 0.55f; // 0.1 - 1.25

        // Timing and tracking.
        float scan_range       = 50.0f;  // yalms
        float write_interval   = 0.016f; // seconds between state rebuild checks
        float pair_cooldown    = 8.0f;   // seconds, regular attack repeat delay
        float special_cooldown = 0.0f;   // seconds, special action repeat delay

        // Experimental and diagnostic options.
        bool claim_fallback         = false;
        float claim_timeout         = 0.5f;
        bool action_debug           = false;
        bool auto_inspect           = false;
        float auto_inspect_interval = 120.0f;

        RenderMode render_mode = RenderMode::EndScene;

        static constexpr float kBaseOpacity = 0.8f;
        static constexpr float kBaseTimeout = 1.5f;
        static constexpr float kNormalWidth = 0.8f;

        static float clamp(float value, float minimum, float maximum)
        {
            return value < minimum ? minimum : (value > maximum ? maximum : value);
        }

        static float slider(float value)
        {
            return clamp(value, 0.5f, 2.0f);
        }

        float effective_opacity(void) const
        {
            return clamp(kBaseOpacity * slider(opacity_scale), 0.0f, 1.0f);
        }

        float effective_timeout(void) const
        {
            return kBaseTimeout * slider(fade_scale);
        }

        float effective_source_height(void) const
        {
            return -0.75f * slider(source_height_scale);
        }

        float effective_target_height(void) const
        {
            return -1.35f * slider(target_height_scale);
        }

        float effective_aoe_opacity(void) const
        {
            return clamp(aoe_opacity_scale, 0.1f, 1.25f);
        }

        int ring_indicator_style(void) const
        {
            return aoe_mode == AoeMode::Ring2 ? 10 : 1;
        }

        bool ring_indicator_enabled(void) const
        {
            return aoe_mode == AoeMode::Ring1 || aoe_mode == AoeMode::Ring2;
        }

        RenderSettings render_settings(void) const
        {
            RenderSettings out {};
            out.opacity       = effective_opacity();
            out.timeout       = effective_timeout();
            out.width         = slider(width_scale);
            out.glow          = slider(glow_scale);
            out.source_height = effective_source_height();
            out.target_height = effective_target_height();
            return out;
        }

        // Loads the file if it exists. Missing keys keep their defaults.
        bool load(const std::string& path);
        bool save(const std::string& path) const;
    };

    const char* to_string(AoeMode mode);
    const char* to_string(RegularMode mode);
    const char* to_string(RenderMode mode);
    bool parse_aoe_mode(const std::string& text, AoeMode& out);
    bool parse_regular_mode(const std::string& text, RegularMode& out);
    bool parse_render_mode(const std::string& text, RenderMode& out);
} // namespace targetlines

#endif // TARGETLINES_SETTINGS_HPP_INCLUDED
