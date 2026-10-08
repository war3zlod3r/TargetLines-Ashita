#include "settings.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

namespace targetlines
{
    namespace
    {
        std::string lower(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return text;
        }

        std::string trim(const std::string& text)
        {
            const auto begin = text.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos)
            {
                return std::string();
            }
            const auto end = text.find_last_not_of(" \t\r\n");
            return text.substr(begin, end - begin + 1);
        }

        using Values = std::map<std::string, std::string>;

        bool read_bool(const Values& values, const char* key, bool fallback)
        {
            const auto it = values.find(key);
            if (it == values.end())
            {
                return fallback;
            }
            const std::string v = lower(it->second);
            if (v == "true" || v == "1" || v == "yes" || v == "on")
            {
                return true;
            }
            if (v == "false" || v == "0" || v == "no" || v == "off")
            {
                return false;
            }
            return fallback;
        }

        float read_float(const Values& values, const char* key, float fallback)
        {
            const auto it = values.find(key);
            if (it == values.end())
            {
                return fallback;
            }
            char* end     = nullptr;
            const float v = std::strtof(it->second.c_str(), &end);
            if (end == it->second.c_str())
            {
                return fallback;
            }
            return v;
        }

        template<typename T, typename Parser>
        T read_enum(const Values& values, const char* key, T fallback, Parser parser)
        {
            const auto it = values.find(key);
            if (it == values.end())
            {
                return fallback;
            }
            T parsed = fallback;
            return parser(it->second, parsed) ? parsed : fallback;
        }

        void write_bool(std::ostream& out, const char* key, bool value)
        {
            out << key << '=' << (value ? "true" : "false") << '\n';
        }

        void write_float(std::ostream& out, const char* key, float value)
        {
            char buffer[64] {};
            std::snprintf(buffer, sizeof(buffer), "%.4f", value);
            out << key << '=' << buffer << '\n';
        }
    } // namespace

    const char* to_string(AoeMode mode)
    {
        switch (mode)
        {
            case AoeMode::Off:
                return "off";
            case AoeMode::Fan:
                return "fan";
            case AoeMode::Ring1:
                return "ring1";
            case AoeMode::Ring2:
                return "ring2";
        }
        return "ring1";
    }

    const char* to_string(RegularMode mode)
    {
        switch (mode)
        {
            case RegularMode::First:
                return "first";
            case RegularMode::Repeat:
                return "repeat";
            case RegularMode::Off:
                return "off";
        }
        return "first";
    }

    const char* to_string(RenderMode mode)
    {
        switch (mode)
        {
            case RenderMode::EndScene:
                return "endscene";
            case RenderMode::SceneHook:
                return "scenehook";
        }
        return "endscene";
    }

    bool parse_aoe_mode(const std::string& text, AoeMode& out)
    {
        const std::string v = lower(trim(text));
        if (v == "off" || v == "none" || v == "0" || v == "false")
        {
            out = AoeMode::Off;
            return true;
        }
        if (v == "fan")
        {
            out = AoeMode::Fan;
            return true;
        }
        if (v == "ring1" || v == "ring" || v == "ringa" || v == "ring_a")
        {
            out = AoeMode::Ring1;
            return true;
        }
        if (v == "ring2" || v == "ringb" || v == "ring_b")
        {
            out = AoeMode::Ring2;
            return true;
        }
        return false;
    }

    bool parse_regular_mode(const std::string& text, RegularMode& out)
    {
        const std::string v = lower(trim(text));
        if (v == "first" || v == "once")
        {
            out = RegularMode::First;
            return true;
        }
        if (v == "repeat" || v == "cooldown" || v == "delay")
        {
            out = RegularMode::Repeat;
            return true;
        }
        if (v == "off" || v == "none")
        {
            out = RegularMode::Off;
            return true;
        }
        return false;
    }

    bool parse_render_mode(const std::string& text, RenderMode& out)
    {
        const std::string v = lower(trim(text));
        if (v == "endscene" || v == "end" || v == "default")
        {
            out = RenderMode::EndScene;
            return true;
        }
        if (v == "scenehook" || v == "scene" || v == "hook")
        {
            out = RenderMode::SceneHook;
            return true;
        }
        return false;
    }

    bool Settings::load(const std::string& path)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            return false;
        }

        Values values;
        std::string line;
        while (std::getline(file, line))
        {
            line = trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[')
            {
                continue;
            }
            const auto equals = line.find('=');
            if (equals == std::string::npos)
            {
                continue;
            }
            values[lower(trim(line.substr(0, equals)))] = trim(line.substr(equals + 1));
        }

        enabled                = read_bool(values, "enabled", enabled);
        show_player_lines      = read_bool(values, "show_player_lines", show_player_lines);
        show_party_lines       = read_bool(values, "show_party_lines", show_party_lines);
        show_pet_lines         = read_bool(values, "show_pet_lines", show_pet_lines);
        show_enemy_lines       = read_bool(values, "show_enemy_lines", show_enemy_lines);
        show_other_party_lines = read_bool(values, "show_other_party_lines", show_other_party_lines);
        show_special_lines     = read_bool(values, "show_special_lines", show_special_lines);
        aoe_mode               = read_enum(values, "aoe_mode", aoe_mode, parse_aoe_mode);
        regular_attack_mode    = read_enum(values, "regular_attack_mode", regular_attack_mode, parse_regular_mode);
        color_blind_mode       = read_bool(values, "color_blind_mode", color_blind_mode);

        opacity_scale        = slider(read_float(values, "opacity_scale", opacity_scale));
        player_opacity_scale = slider(read_float(values, "player_opacity_scale", player_opacity_scale));
        ally_opacity_scale   = slider(read_float(values, "ally_opacity_scale", ally_opacity_scale));
        enemy_opacity_scale  = slider(read_float(values, "enemy_opacity_scale", enemy_opacity_scale));
        fade_scale           = slider(read_float(values, "fade_scale", fade_scale));
        width_scale          = slider(read_float(values, "width_scale", width_scale));
        glow_scale           = slider(read_float(values, "glow_scale", glow_scale));
        source_height_scale  = slider(read_float(values, "source_height_scale", source_height_scale));
        target_height_scale  = slider(read_float(values, "target_height_scale", target_height_scale));
        aoe_opacity_scale    = clamp(read_float(values, "aoe_opacity_scale", aoe_opacity_scale), 0.1f, 1.25f);

        scan_range       = clamp(read_float(values, "scan_range", scan_range), 1.0f, 500.0f);
        write_interval   = clamp(read_float(values, "write_interval", write_interval), 0.008f, 5.0f);
        pair_cooldown    = clamp(read_float(values, "pair_cooldown", pair_cooldown), 0.0f, 3600.0f);
        special_cooldown = clamp(read_float(values, "special_cooldown", special_cooldown), 0.0f, 3600.0f);

        claim_fallback        = read_bool(values, "claim_fallback", claim_fallback);
        claim_timeout         = clamp(read_float(values, "claim_timeout", claim_timeout), 0.1f, 60.0f);
        action_debug          = read_bool(values, "action_debug", action_debug);
        auto_inspect          = read_bool(values, "auto_inspect", auto_inspect);
        auto_inspect_interval = clamp(read_float(values, "auto_inspect_interval", auto_inspect_interval), 30.0f, 86400.0f);

        render_mode = read_enum(values, "render_mode", render_mode, parse_render_mode);
        return true;
    }

    bool Settings::save(const std::string& path) const
    {
        std::ofstream file(path, std::ios::trunc);
        if (!file.is_open())
        {
            return false;
        }

        file << "; TargetLines for Ashita v4 settings. Edit while the plugin is unloaded, or use /tl commands.\n";
        file << "[targetlines]\n";
        write_bool(file, "enabled", enabled);
        write_bool(file, "show_player_lines", show_player_lines);
        write_bool(file, "show_party_lines", show_party_lines);
        write_bool(file, "show_pet_lines", show_pet_lines);
        write_bool(file, "show_enemy_lines", show_enemy_lines);
        write_bool(file, "show_other_party_lines", show_other_party_lines);
        write_bool(file, "show_special_lines", show_special_lines);
        file << "aoe_mode=" << to_string(aoe_mode) << '\n';
        file << "regular_attack_mode=" << to_string(regular_attack_mode) << '\n';
        write_bool(file, "color_blind_mode", color_blind_mode);
        file << '\n';
        write_float(file, "opacity_scale", opacity_scale);
        write_float(file, "player_opacity_scale", player_opacity_scale);
        write_float(file, "ally_opacity_scale", ally_opacity_scale);
        write_float(file, "enemy_opacity_scale", enemy_opacity_scale);
        write_float(file, "fade_scale", fade_scale);
        write_float(file, "width_scale", width_scale);
        write_float(file, "glow_scale", glow_scale);
        write_float(file, "source_height_scale", source_height_scale);
        write_float(file, "target_height_scale", target_height_scale);
        write_float(file, "aoe_opacity_scale", aoe_opacity_scale);
        file << '\n';
        write_float(file, "scan_range", scan_range);
        write_float(file, "write_interval", write_interval);
        write_float(file, "pair_cooldown", pair_cooldown);
        write_float(file, "special_cooldown", special_cooldown);
        file << '\n';
        write_bool(file, "claim_fallback", claim_fallback);
        write_float(file, "claim_timeout", claim_timeout);
        write_bool(file, "action_debug", action_debug);
        write_bool(file, "auto_inspect", auto_inspect);
        write_float(file, "auto_inspect_interval", auto_inspect_interval);
        file << '\n';
        file << "; endscene (default) draws from Ashita's Direct3D EndScene callback.\n";
        file << "; scenehook draws inside FFXiMain!draw_scene (beneath the game UI) using the shared SceneHook patch.\n";
        file << "render_mode=" << to_string(render_mode) << '\n';
        return file.good();
    }
} // namespace targetlines
