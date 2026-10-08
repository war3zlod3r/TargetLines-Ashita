#include "targetlines.hpp"

#include "action_packet.hpp"
#include "scenehook/SceneHook.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace
{
    // SceneHook bookkeeping lives outside the plugin object so that DllMain can
    // release the slot even if the plugin is torn down abnormally.
    SceneBus* g_scene_bus = nullptr;
    int g_scene_slot      = -1;

    void SCENEHOOK_ALIGN_STACK __cdecl scene_draw_thunk(void* user, void* /*renderer*/, void* /*device*/)
    {
        auto* plugin = static_cast<targetlines::Plugin*>(user);
        if (plugin != nullptr)
        {
            plugin->scene_draw();
        }
    }

    std::string lower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return text;
    }

    bool parse_float(const std::string& text, float& out)
    {
        char* end     = nullptr;
        const float v = std::strtof(text.c_str(), &end);
        if (end == text.c_str() || (end != nullptr && *end != '\0'))
        {
            return false;
        }
        out = v;
        return true;
    }

    std::string format(const char* fmt, ...)
    {
        char buffer[2048] {};
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        return buffer;
    }

    const char* on_off(bool value)
    {
        return value ? "on" : "off";
    }

    constexpr uint16_t kZoneInPacketId  = 0x000A;
    constexpr uint16_t kZoneOutPacketId = 0x000B;
} // namespace

namespace targetlines
{
    Plugin::Plugin(void)
    {}

    Plugin::~Plugin(void)
    {}

    const char* Plugin::GetName(void) const
    {
        return "TargetLines";
    }

    const char* Plugin::GetAuthor(void) const
    {
        return "MogSafe (Windower original); Ashita v4 port";
    }

    const char* Plugin::GetDescription(void) const
    {
        return "Draws Final Fantasy XII style target lines and AoE indicators during combat.";
    }

    const char* Plugin::GetLink(void) const
    {
        return "https://github.com/MogSafe/TargetLines";
    }

    double Plugin::GetVersion(void) const
    {
        return 1.0;
    }

    double Plugin::GetInterfaceVersion(void) const
    {
        return ASHITA_INTERFACE_VERSION;
    }

    int32_t Plugin::GetPriority(void) const
    {
        return 0;
    }

    uint32_t Plugin::GetFlags(void) const
    {
        return static_cast<uint32_t>(Ashita::PluginFlags::UseCommands | Ashita::PluginFlags::UsePackets | Ashita::PluginFlags::UseDirect3D);
    }

    bool Plugin::Initialize(IAshitaCore* core, ILogManager* logger, const uint32_t id)
    {
        core_ = core;
        log_  = logger;
        id_   = id;
        if (core_ == nullptr)
        {
            return false;
        }

        // Resolve <Ashita>/config/targetlines/ and make sure it exists.
        std::string install = core_->GetInstallPath() != nullptr ? core_->GetInstallPath() : "";
        if (!install.empty() && install.back() != '\\' && install.back() != '/')
        {
            install += '\\';
        }
        const std::string config_root = install + "config\\";
        config_dir_                   = config_root + "targetlines\\";
        CreateDirectoryA(config_root.c_str(), nullptr);
        CreateDirectoryA(config_dir_.c_str(), nullptr);

        settings_path_ = config_dir_ + "targetlines.ini";
        inspect_path_  = config_dir_ + "inspect.log";
        logger_.initialize(log_, config_dir_ + "runtime.log");

        settings_.load(settings_path_);
        settings_.save(settings_path_);

        game_     = std::make_unique<Game>(core_);
        tracker_  = std::make_unique<Tracker>(*game_, settings_, logger_);
        renderer_ = std::make_unique<Renderer>(*game_, logger_);

        device_ = core_->GetDirect3DDevice();

        apply_render_mode();

        logger_.info("loaded enabled=%s aoe_mode=%s regular=%s render_mode=%s scenehook=%s",
            on_off(settings_.enabled), to_string(settings_.aoe_mode), to_string(settings_.regular_attack_mode),
            to_string(settings_.render_mode), scenehook_status_.c_str());
        return true;
    }

    void Plugin::Release(void)
    {
        stop_scenehook();
        if (renderer_)
        {
            renderer_->reset();
        }
        if (!settings_path_.empty())
        {
            settings_.save(settings_path_);
        }
        logger_.info("unloaded");

        renderer_.reset();
        tracker_.reset();
        game_.reset();
        device_ = nullptr;
    }

    bool Plugin::Direct3DInitialize(IDirect3DDevice8* device)
    {
        device_ = device;
        return true;
    }

    void Plugin::Direct3DEndScene(bool isRenderingBackBuffer)
    {
        if (!isRenderingBackBuffer || !tracker_ || !renderer_)
        {
            return;
        }

        const double now = Game::now();
        tick(now);

        if (settings_.enabled && settings_.render_mode == RenderMode::EndScene)
        {
            IDirect3DDevice8* device = device_ != nullptr ? device_ : core_->GetDirect3DDevice();
            renderer_->render(device);
        }
    }

    void Plugin::Direct3DPresent(const RECT*, const RECT*, HWND, const RGNDATA*)
    {
        render_config_window();
    }

    void Plugin::scene_draw(void)
    {
        if (!renderer_ || !settings_.enabled || settings_.render_mode != RenderMode::SceneHook)
        {
            return;
        }

        IDirect3DDevice8* device = device_ != nullptr ? device_ : (core_ != nullptr ? core_->GetDirect3DDevice() : nullptr);
        renderer_->render(device);
    }

    void Plugin::tick(double now)
    {
        if (settings_dirty_ && now - settings_dirty_at_ >= 0.75)
        {
            save_settings();
        }

        if (settings_.render_mode == RenderMode::SceneHook && !scenehook_active_ && now - last_scenehook_try_ >= 5.0)
        {
            last_scenehook_try_ = now;
            start_scenehook();
        }

        if (!settings_.enabled || !game_->logged_in())
        {
            return;
        }

        const bool boneprobe = now < boneprobe_until_;
        if (tracker_->update(now, state_, boneprobe))
        {
            renderer_->set_state(state_);
        }

        const std::string inspect_message = tracker_->maybe_auto_inspect(now, inspect_path_);
        if (!inspect_message.empty())
        {
            print(inspect_message);
        }
    }

    bool Plugin::HandleIncomingPacket(uint16_t id, uint32_t size, const uint8_t* data, uint8_t*, uint32_t, const uint8_t*, bool, bool)
    {
        if (!tracker_)
        {
            return false;
        }

        if (id == kActionPacketId)
        {
            if (!settings_.enabled)
            {
                return false;
            }

            ActionPacket packet {};
            if (parse_action_packet(data, size, packet))
            {
                ++action_packets_;
                tracker_->handle_action(packet);
            }
            else if (settings_.action_debug)
            {
                logger_.runtime("action packet parse failed size=%u", size);
            }
        }
        else if (id == kZoneInPacketId || id == kZoneOutPacketId)
        {
            tracker_->clear_all();
            push_empty_state();
        }

        return false;
    }

    void Plugin::print(const std::string& message) const
    {
        if (core_ == nullptr || core_->GetChatManager() == nullptr)
        {
            return;
        }
        const std::string text = Ashita::Chat::Header("TargetLines") + Ashita::Chat::Message(message);
        core_->GetChatManager()->Write(1, false, text.c_str());
    }

    void Plugin::warn(const std::string& message) const
    {
        if (core_ == nullptr || core_->GetChatManager() == nullptr)
        {
            return;
        }
        const std::string text = Ashita::Chat::Header("TargetLines") + Ashita::Chat::Warning(message);
        core_->GetChatManager()->Write(1, false, text.c_str());
    }

    void Plugin::save_settings(void)
    {
        settings_dirty_ = false;
        if (!settings_path_.empty() && !settings_.save(settings_path_))
        {
            logger_.warn("could not save settings to %s", settings_path_.c_str());
        }
    }

    void Plugin::on_settings_changed(bool save_now)
    {
        if (tracker_)
        {
            tracker_->invalidate();
        }
        if (save_now)
        {
            save_settings();
        }
        else
        {
            settings_dirty_    = true;
            settings_dirty_at_ = Game::now();
        }
    }

    void Plugin::push_empty_state(void)
    {
        state_ = RenderState {};
        if (renderer_)
        {
            renderer_->reset();
        }
    }

    void Plugin::set_enabled(bool enabled)
    {
        settings_.enabled = enabled;
        if (!enabled)
        {
            push_empty_state();
        }
        on_settings_changed(true);
    }

    void Plugin::set_aoe_mode(AoeMode mode)
    {
        settings_.aoe_mode = mode;
        if (tracker_)
        {
            tracker_->clear_aoe_visuals();
        }
        on_settings_changed(true);
    }

    void Plugin::set_regular_mode(RegularMode mode)
    {
        settings_.regular_attack_mode = mode;
        if (tracker_)
        {
            tracker_->clear_seen_pairs();
        }
        on_settings_changed(true);
    }

    void Plugin::set_render_mode(RenderMode mode)
    {
        settings_.render_mode = mode;
        apply_render_mode();
        on_settings_changed(true);
    }

    bool Plugin::set_bool_from_arg(bool& field, const std::string* arg)
    {
        const std::string value = arg != nullptr ? lower(*arg) : std::string();
        if (value == "on" || value == "1" || value == "true" || value == "yes")
        {
            field = true;
        }
        else if (value == "off" || value == "0" || value == "false" || value == "no")
        {
            field = false;
        }
        else
        {
            field = !field;
        }
        on_settings_changed(true);
        return field;
    }

    void Plugin::adjust_slider(float& field, float delta)
    {
        field = Settings::slider(Settings::slider(field) + delta);
        on_settings_changed(true);
    }

    bool Plugin::adjust_scale_command(float& field, const std::string& usage, const std::string* arg, const char* label)
    {
        const std::string value = arg != nullptr ? lower(*arg) : std::string();
        if (value == "+")
        {
            adjust_slider(field, 0.01f);
        }
        else if (value == "-")
        {
            adjust_slider(field, -0.01f);
        }
        else
        {
            float scale = 0.0f;
            if (!parse_float(value, scale) || scale < 0.5f || scale > 2.0f)
            {
                warn(usage);
                return false;
            }
            field = scale;
            on_settings_changed(true);
        }

        print(format("%s set to %d%%.", label, static_cast<int>(Settings::slider(field) * 100.0f + 0.5f)));
        return true;
    }

    void Plugin::apply_render_mode(void)
    {
        if (settings_.render_mode == RenderMode::SceneHook)
        {
            last_scenehook_try_ = Game::now();
            start_scenehook();
        }
        else
        {
            stop_scenehook();
        }
    }

    bool Plugin::start_scenehook(void)
    {
        if (g_scene_slot >= 0 && g_scene_bus != nullptr)
        {
            scenehook_set_enabled(g_scene_bus, g_scene_slot, true);
            scenehook_active_ = true;
            return true;
        }

        if (g_scene_bus == nullptr)
        {
            g_scene_bus = scenehook_attach();
        }
        if (g_scene_bus == nullptr)
        {
            scenehook_status_ = "SceneHook ABI unavailable (another module owns an incompatible bus)";
            scenehook_active_ = false;
            return false;
        }

        if (!scenehook_ensure_hook(g_scene_bus))
        {
            scenehook_status_ = std::string("SceneHook install failed: ") + g_scene_bus->status;
            scenehook_active_ = false;
            logger_.warn("%s", scenehook_status_.c_str());
            return false;
        }

        g_scene_slot = scenehook_register(g_scene_bus, &scene_draw_thunk, this);
        if (g_scene_slot < 0)
        {
            scenehook_status_ = "SceneHook client limit reached";
            scenehook_active_ = false;
            logger_.warn("%s", scenehook_status_.c_str());
            return false;
        }

        char describe[224] {};
        scenehook_describe(g_scene_bus, g_scene_slot, describe, sizeof(describe));
        scenehook_status_ = format("attached (%s; %s)", describe, g_scene_bus->status);
        scenehook_active_ = true;
        logger_.info("SceneHook %s", scenehook_status_.c_str());
        return true;
    }

    void Plugin::stop_scenehook(void)
    {
        if (g_scene_slot >= 0 && g_scene_bus != nullptr)
        {
            scenehook_unregister(g_scene_bus, g_scene_slot, true);
            g_scene_slot = -1;
            logger_.info("SceneHook client released");
        }
        scenehook_active_ = false;
        if (settings_.render_mode != RenderMode::SceneHook)
        {
            scenehook_status_ = "not attached (endscene mode)";
        }
    }

    bool Plugin::HandleCommand(int32_t, const char* command, bool)
    {
        std::vector<std::string> args;
        Ashita::Commands::GetCommandArgs(command, &args);
        if (args.empty())
        {
            return false;
        }

        const std::string root = lower(args[0]);
        if (root != "/tl" && root != "/targetlines")
        {
            return false;
        }

        const std::string sub  = args.size() > 1 ? lower(args[1]) : "help";
        const std::string* a1  = args.size() > 2 ? &args[2] : nullptr;
        const std::string* a2  = args.size() > 3 ? &args[3] : nullptr;
        const std::string arg1 = a1 != nullptr ? lower(*a1) : std::string();

        if (sub == "on")
        {
            set_enabled(true);
            print("Enabled.");
        }
        else if (sub == "off")
        {
            set_enabled(false);
            print("Disabled.");
        }
        else if (sub == "config" || sub == "settings")
        {
            config_visible_ = !config_visible_;
            print(config_visible_ ? "Settings window shown." : "Settings window hidden.");
        }
        else if (sub == "playerlines" || sub == "player")
        {
            print(format("Player lines %s.", on_off(set_bool_from_arg(settings_.show_player_lines, a1))));
        }
        else if (sub == "partylines" || sub == "party" || sub == "trustlines" || sub == "trusts")
        {
            print(format("Party/trust lines %s.", on_off(set_bool_from_arg(settings_.show_party_lines, a1))));
        }
        else if (sub == "petlines" || sub == "pets")
        {
            print(format("Pet lines %s.", on_off(set_bool_from_arg(settings_.show_pet_lines, a1))));
        }
        else if (sub == "enemylines" || sub == "enemy")
        {
            print(format("Enemy lines %s.", on_off(set_bool_from_arg(settings_.show_enemy_lines, a1))));
        }
        else if (sub == "otherpartylines" || sub == "otherparty" || sub == "others")
        {
            print(format("Other party lines %s.", on_off(set_bool_from_arg(settings_.show_other_party_lines, a1))));
        }
        else if (sub == "speciallines" || sub == "specials")
        {
            print(format("Special action lines %s.", on_off(set_bool_from_arg(settings_.show_special_lines, a1))));
        }
        else if (sub == "fanlines" || sub == "fan" || sub == "aoe")
        {
            AoeMode mode = settings_.aoe_mode;
            if (arg1 == "on" || arg1 == "1" || arg1 == "true" || arg1 == "yes")
            {
                mode = mode == AoeMode::Off ? AoeMode::Fan : mode;
            }
            else if (arg1 == "off" || arg1 == "0" || arg1 == "false" || arg1 == "no")
            {
                mode = AoeMode::Off;
            }
            else
            {
                mode = mode == AoeMode::Off ? AoeMode::Fan : AoeMode::Off;
            }
            set_aoe_mode(mode);
            print(format("AoE indicators set to %s.", to_string(mode)));
        }
        else if (sub == "aoemode" || sub == "aoeindicator")
        {
            AoeMode mode = settings_.aoe_mode;
            if (a1 == nullptr || !parse_aoe_mode(*a1, mode))
            {
                warn("Usage: /tl aoemode off|fan|ring1|ring2");
                return true;
            }
            set_aoe_mode(mode);
            print(format("AoE indicators set to %s.", to_string(mode)));
        }
        else if (sub == "colorblind" || sub == "colourblind" || sub == "cbmode")
        {
            print(format("Color blind mode %s.", on_off(set_bool_from_arg(settings_.color_blind_mode, a1))));
        }
        else if (sub == "playeropacity" || sub == "popacity")
        {
            adjust_scale_command(settings_.player_opacity_scale, "Usage: /tl playeropacity <0.5-2>|+|-", a1, "Player opacity");
        }
        else if (sub == "allyopacity" || sub == "aopacity")
        {
            adjust_scale_command(settings_.ally_opacity_scale, "Usage: /tl allyopacity <0.5-2>|+|-", a1, "Ally opacity");
        }
        else if (sub == "enemyopacity" || sub == "eopacity")
        {
            adjust_scale_command(settings_.enemy_opacity_scale, "Usage: /tl enemyopacity <0.5-2>|+|-", a1, "Enemy opacity");
        }
        else if (sub == "opacity")
        {
            if (arg1 == "+" || arg1 == "-")
            {
                adjust_slider(settings_.opacity_scale, arg1 == "+" ? 0.01f : -0.01f);
                print(format("Opacity scale set to %d%%.", static_cast<int>(Settings::slider(settings_.opacity_scale) * 100.0f + 0.5f)));
            }
            else
            {
                float opacity = 0.0f;
                if (!parse_float(arg1, opacity) || opacity < 0.0f || opacity > 1.0f)
                {
                    warn("Usage: /tl opacity <0-1>|+|-");
                    return true;
                }
                settings_.opacity_scale = Settings::slider(opacity / Settings::kBaseOpacity);
                on_settings_changed(true);
                print(format("Opacity set to %.2f.", settings_.effective_opacity()));
            }
        }
        else if (sub == "fade" || sub == "width" || sub == "glow" || sub == "sourceheight" || sub == "sourceht" || sub == "targetheight" || sub == "targetht")
        {
            float* field      = nullptr;
            const char* label = "";
            if (sub == "fade")
            {
                field = &settings_.fade_scale;
                label = "Fade scale";
            }
            else if (sub == "width")
            {
                field = &settings_.width_scale;
                label = "Width scale";
            }
            else if (sub == "glow")
            {
                field = &settings_.glow_scale;
                label = "Glow scale";
            }
            else if (sub == "sourceheight" || sub == "sourceht")
            {
                field = &settings_.source_height_scale;
                label = "Source height scale";
            }
            else
            {
                field = &settings_.target_height_scale;
                label = "Target height scale";
            }

            if (arg1 == "+" || arg1 == "-")
            {
                adjust_slider(*field, arg1 == "+" ? 0.01f : -0.01f);
                print(format("%s set to %d%%.", label, static_cast<int>(Settings::slider(*field) * 100.0f + 0.5f)));
            }
            else
            {
                warn(format("Usage: /tl %s +|-", sub.c_str()));
            }
        }
        else if (sub == "aoeopacity" || sub == "aoeopacityscale" || sub == "fanopacity" || sub == "fanopacityscale")
        {
            const float current = settings_.effective_aoe_opacity();
            if (arg1 == "+")
            {
                settings_.aoe_opacity_scale = Settings::clamp(current + 0.01f, 0.1f, 1.25f);
            }
            else if (arg1 == "-")
            {
                settings_.aoe_opacity_scale = Settings::clamp(current - 0.01f, 0.1f, 1.25f);
            }
            else
            {
                float opacity = 0.0f;
                if (!parse_float(arg1, opacity) || opacity < 0.1f || opacity > 1.25f)
                {
                    warn("Usage: /tl aoeopacity <0.1-1.25>|+|-");
                    return true;
                }
                settings_.aoe_opacity_scale = opacity;
            }
            on_settings_changed(true);
            print(format("AoE opacity set to %d%%.", static_cast<int>(settings_.aoe_opacity_scale * 100.0f + 0.5f)));
        }
        else if (sub == "range")
        {
            float range = 0.0f;
            if (!parse_float(arg1, range) || range <= 0.0f)
            {
                warn("Usage: /tl range <yalms>");
                return true;
            }
            settings_.scan_range = Settings::clamp(range, 1.0f, 500.0f);
            on_settings_changed(true);
            print(format("Range set to %.1f.", settings_.scan_range));
        }
        else if (sub == "timeout")
        {
            float timeout = 0.0f;
            if (!parse_float(arg1, timeout) || timeout <= 0.0f)
            {
                warn("Usage: /tl timeout <seconds>");
                return true;
            }
            settings_.fade_scale = Settings::slider(timeout / Settings::kBaseTimeout);
            on_settings_changed(true);
            print(format("Timeout set to %.2f seconds.", settings_.effective_timeout()));
        }
        else if (sub == "interval")
        {
            float interval = 0.0f;
            if (!parse_float(arg1, interval) || interval < 0.008f)
            {
                warn("Usage: /tl interval <seconds>, e.g. /tl interval 0.016");
                return true;
            }
            settings_.write_interval = Settings::clamp(interval, 0.008f, 5.0f);
            on_settings_changed(true);
            print(format("Update interval set to %.3f seconds.", settings_.write_interval));
        }
        else if (sub == "cooldown")
        {
            float cooldown = 0.0f;
            if (!parse_float(arg1, cooldown) || cooldown < 0.0f)
            {
                warn("Usage: /tl cooldown <seconds>");
                return true;
            }
            settings_.pair_cooldown = cooldown;
            on_settings_changed(true);
            print(format("Pair cooldown set to %.1f seconds.", cooldown));
        }
        else if (sub == "specialcooldown" || sub == "specialcd")
        {
            float cooldown = 0.0f;
            if (!parse_float(arg1, cooldown) || cooldown < 0.0f)
            {
                warn("Usage: /tl specialcooldown <seconds>");
                return true;
            }
            settings_.special_cooldown = cooldown;
            tracker_->clear_special_pairs();
            on_settings_changed(true);
            print(format("Special cooldown set to %.1f seconds.", cooldown));
        }
        else if (sub == "regular")
        {
            RegularMode mode = settings_.regular_attack_mode;
            if (a1 == nullptr || !parse_regular_mode(*a1, mode))
            {
                warn("Usage: /tl regular first|repeat|off");
                return true;
            }
            set_regular_mode(mode);
            switch (mode)
            {
                case RegularMode::First:
                    print("Regular attacks set to once per source-target pair.");
                    break;
                case RegularMode::Repeat:
                    print("Regular attacks set to Repeat After Delay.");
                    break;
                case RegularMode::Off:
                    print("Regular attacks disabled.");
                    break;
            }
        }
        else if (sub == "claim")
        {
            print(format("Claim fallback %s.", on_off(set_bool_from_arg(settings_.claim_fallback, a1))));
        }
        else if (sub == "autoinspect" || sub == "inspectauto")
        {
            if (arg1 == "interval")
            {
                float interval = 0.0f;
                if (a2 == nullptr || !parse_float(*a2, interval) || interval < 30.0f)
                {
                    warn("Usage: /tl autoinspect interval <seconds>, minimum 30");
                    return true;
                }
                settings_.auto_inspect_interval = interval;
                on_settings_changed(true);
                print(format("Auto inspect interval set to %.0f seconds.", interval));
                return true;
            }

            set_bool_from_arg(settings_.auto_inspect, a1);
            print(format("Auto inspect %s. Interval: %.0f seconds.", settings_.auto_inspect ? "enabled" : "disabled", settings_.auto_inspect_interval));
        }
        else if (sub == "actiondebug" || sub == "adebug")
        {
            set_bool_from_arg(settings_.action_debug, a1);
            print(format("Action debug %s. Output: %s", settings_.action_debug ? "enabled" : "disabled", logger_.runtime_log_path().c_str()));
        }
        else if (sub == "clear")
        {
            tracker_->clear_all();
            push_empty_state();
            print("Lines cleared.");
        }
        else if (sub == "boneprobe" || sub == "anchorprobe")
        {
            if (!tracker_->has_probe_line())
            {
                warn("No recent line is available to probe.");
                return true;
            }
            boneprobe_until_ = Game::now() + 1.0;
            tracker_->invalidate();
            print(format("Bone probe requested for the latest line. Output: %s", logger_.runtime_log_path().c_str()));
        }
        else if (sub == "inspect" || sub == "i")
        {
            if (tracker_->write_inspect(inspect_path_, "manual"))
            {
                print("Inspect snapshot written: " + inspect_path_);
            }
            else
            {
                warn("Could not write " + inspect_path_);
            }
        }
        else if (sub == "rendermode" || sub == "renderer")
        {
            RenderMode mode = settings_.render_mode;
            if (a1 == nullptr || !parse_render_mode(*a1, mode))
            {
                warn("Usage: /tl rendermode endscene|scenehook");
                print(format("Current render mode: %s (%s)", to_string(settings_.render_mode), scenehook_status_.c_str()));
                return true;
            }
            set_render_mode(mode);
            print(format("Render mode set to %s. %s", to_string(mode), mode == RenderMode::SceneHook ? scenehook_status_.c_str() : "Drawing from Direct3D EndScene."));
        }
        else if (sub == "status")
        {
            print_status();
        }
        else if (sub == "nativestatus")
        {
            char renderer_status[320] {};
            if (renderer_)
            {
                renderer_->status(renderer_status, sizeof(renderer_status));
            }
            print(format("renderer: %s", renderer_status));
            print(format("render_mode=%s scenehook=%s action_packets=%u", to_string(settings_.render_mode), scenehook_status_.c_str(), action_packets_));
        }
        else
        {
            print_help();
        }

        return true;
    }

    void Plugin::print_status(void)
    {
        print(format("enabled=%s player=%s party=%s pet=%s enemy=%s other_party=%s special=%s aoe_mode=%s color_blind=%s regular=%s repeat_delay=%.1f special_cooldown=%.1f",
            on_off(settings_.enabled), on_off(settings_.show_player_lines), on_off(settings_.show_party_lines), on_off(settings_.show_pet_lines),
            on_off(settings_.show_enemy_lines), on_off(settings_.show_other_party_lines), on_off(settings_.show_special_lines),
            to_string(settings_.aoe_mode), on_off(settings_.color_blind_mode), to_string(settings_.regular_attack_mode),
            settings_.pair_cooldown, settings_.special_cooldown));
        print(format("opacity=%.2f player_opacity=%.2f ally_opacity=%.2f enemy_opacity=%.2f aoe_opacity=%.2f fade=%.2fs width=%.2f glow=%.2f source_height=%.2f target_height=%.2f range=%.1f interval=%.3f",
            settings_.effective_opacity(), Settings::slider(settings_.player_opacity_scale), Settings::slider(settings_.ally_opacity_scale),
            Settings::slider(settings_.enemy_opacity_scale), settings_.effective_aoe_opacity(), settings_.effective_timeout(),
            Settings::slider(settings_.width_scale), Settings::slider(settings_.glow_scale), settings_.effective_source_height(),
            settings_.effective_target_height(), settings_.scan_range, settings_.write_interval));
        print(format("claim=%s action_debug=%s auto_inspect=%s auto_interval=%.0f render_mode=%s logged_in=%s zone=%u lines=%u nearby=%u action_packets=%u",
            on_off(settings_.claim_fallback), on_off(settings_.action_debug), on_off(settings_.auto_inspect), settings_.auto_inspect_interval,
            to_string(settings_.render_mode), on_off(game_ && game_->logged_in()), game_ ? game_->zone_id() : 0u,
            tracker_ ? static_cast<unsigned>(tracker_->visible_line_count()) : 0u, tracker_ ? static_cast<unsigned>(tracker_->nearby_count()) : 0u,
            action_packets_));
    }

    void Plugin::print_help(void) const
    {
        static const char* const lines[] = {
            "/tl on|off - Enable or disable line output.",
            "/tl config - Open or close the settings window.",
            "/tl playerlines|partylines|petlines|enemylines|otherpartylines|speciallines [on|off]",
            "/tl aoemode off|fan|ring1|ring2 - Select the AoE presentation.",
            "/tl aoeopacity <0.1-1.25>|+|- - Adjust AoE opacity.",
            "/tl colorblind [on|off] - Toggle color blind mode.",
            "/tl regular first|repeat|off - Configure regular-attack lines.",
            "/tl playeropacity|allyopacity|enemyopacity <0.5-2>|+|- - Adjust line opacity.",
            "/tl width +|- | fade +|- | glow +|- | opacity +|- - Adjust line style.",
            "/tl sourceheight +|- | targetheight +|- - Adjust anchor heights.",
            "/tl timeout <sec> | range <yalms> | interval <sec> | cooldown <sec> | specialcooldown <sec>",
            "/tl rendermode endscene|scenehook - Choose how lines are drawn.",
            "/tl claim [on|off] | autoinspect [on|off] | autoinspect interval <sec> | actiondebug [on|off]",
            "/tl clear | status | nativestatus | boneprobe | inspect",
        };

        print("Commands (also /targetlines):");
        for (const char* line : lines)
        {
            print(line);
        }
    }

    void Plugin::render_config_window(void)
    {
        if (!config_visible_ || core_ == nullptr)
        {
            return;
        }

        IGuiManager* gui = core_->GetGuiManager();
        if (gui == nullptr)
        {
            return;
        }

        gui->SetNextWindowSize(ImVec2(400.0f, 0.0f), ImGuiCond_FirstUseEver);
        if (gui->Begin("TargetLines Settings", &config_visible_, ImGuiWindowFlags_AlwaysAutoResize))
        {
            bool changed = false;

            bool enabled = settings_.enabled;
            if (gui->Checkbox("Enable Lines", &enabled))
            {
                set_enabled(enabled);
            }

            gui->BeginDisabled(!settings_.enabled);

            gui->SeparatorText("Sources");
            changed |= gui->Checkbox("Player Lines", &settings_.show_player_lines);
            changed |= gui->Checkbox("Party/Trust Lines", &settings_.show_party_lines);
            changed |= gui->Checkbox("Pet Lines", &settings_.show_pet_lines);
            changed |= gui->Checkbox("Enemy Lines", &settings_.show_enemy_lines);
            changed |= gui->Checkbox("Other Party Lines", &settings_.show_other_party_lines);
            changed |= gui->Checkbox("Abilities/Spells", &settings_.show_special_lines);

            gui->SeparatorText("Style");
            static const char* const aoe_items[] = {"Off", "Fan", "Ring (A)", "Ring (B)"};
            int aoe                              = static_cast<int>(settings_.aoe_mode);
            if (gui->Combo("AoE Style", &aoe, aoe_items, 4))
            {
                set_aoe_mode(static_cast<AoeMode>(aoe));
            }
            changed |= gui->Checkbox("Color Blind Mode", &settings_.color_blind_mode);
            changed |= gui->SliderFloat("Line Width", &settings_.width_scale, 0.5f, 2.0f, "%.2fx");
            changed |= gui->SliderFloat("Player Opacity", &settings_.player_opacity_scale, 0.5f, 2.0f, "%.2fx");
            changed |= gui->SliderFloat("Ally Opacity", &settings_.ally_opacity_scale, 0.5f, 2.0f, "%.2fx");
            changed |= gui->SliderFloat("Enemy Opacity", &settings_.enemy_opacity_scale, 0.5f, 2.0f, "%.2fx");
            changed |= gui->SliderFloat("Line Duration", &settings_.fade_scale, 0.5f, 2.0f, "%.2fx");
            gui->SameLine();
            gui->Text("%.2fs", settings_.effective_timeout());
            changed |= gui->SliderFloat("AoE Opacity", &settings_.aoe_opacity_scale, 0.1f, 1.25f, "%.2f");

            static const char* const regular_items[] = {"First Only", "Repeat After Delay", "Off"};
            int regular                              = static_cast<int>(settings_.regular_attack_mode);
            if (gui->Combo("Regular Attacks", &regular, regular_items, 3))
            {
                set_regular_mode(static_cast<RegularMode>(regular));
            }
            if (settings_.regular_attack_mode == RegularMode::Repeat)
            {
                changed |= gui->SliderFloat("Repeat Delay", &settings_.pair_cooldown, 0.0f, 60.0f, "%.1fs");
            }

            if (gui->CollapsingHeader("Advanced"))
            {
                changed |= gui->SliderFloat("Global Opacity", &settings_.opacity_scale, 0.5f, 2.0f, "%.2fx");
                changed |= gui->SliderFloat("Line Glow", &settings_.glow_scale, 0.5f, 2.0f, "%.2fx");
                changed |= gui->SliderFloat("Source Height", &settings_.source_height_scale, 0.5f, 2.0f, "%.2fx");
                changed |= gui->SliderFloat("Target Height", &settings_.target_height_scale, 0.5f, 2.0f, "%.2fx");
                changed |= gui->SliderFloat("Special Cooldown", &settings_.special_cooldown, 0.0f, 30.0f, "%.1fs");
                changed |= gui->SliderFloat("Nearby Scan Range", &settings_.scan_range, 10.0f, 200.0f, "%.0f yalms");

                static const char* const render_items[] = {"EndScene (default)", "SceneHook (draw_scene patch)"};
                int render                              = static_cast<int>(settings_.render_mode);
                if (gui->Combo("Render Mode", &render, render_items, 2))
                {
                    set_render_mode(static_cast<RenderMode>(render));
                }
                gui->TextWrapped("EndScene draws over the whole frame including the game UI. SceneHook patches FFXiMain!draw_scene once so lines render beneath menus and chat, matching the Windower version.");
                if (settings_.render_mode == RenderMode::SceneHook)
                {
                    gui->TextWrapped("SceneHook: %s", scenehook_status_.c_str());
                }

                changed |= gui->Checkbox("Claim Fallback Lines (experimental)", &settings_.claim_fallback);
                changed |= gui->Checkbox("Action Debug Logging", &settings_.action_debug);
            }

            gui->EndDisabled();

            gui->Separator();
            gui->Text("Lines: %u   Nearby NPCs: %u   Packets: %u",
                tracker_ ? static_cast<unsigned>(tracker_->visible_line_count()) : 0u,
                tracker_ ? static_cast<unsigned>(tracker_->nearby_count()) : 0u,
                action_packets_);

            if (changed)
            {
                on_settings_changed(false);
            }
        }
        gui->End();
    }
} // namespace targetlines

__declspec(dllexport) IPlugin* __stdcall expCreatePlugin(const char* args)
{
    UNREFERENCED_PARAMETER(args);
    return new targetlines::Plugin();
}

__declspec(dllexport) void __stdcall expDestroyPlugin(void* instance)
{
    delete static_cast<targetlines::Plugin*>(instance);
}

__declspec(dllexport) double __stdcall expGetInterfaceVersion(void)
{
    return ASHITA_INTERFACE_VERSION;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instance);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        // Backstop for the SceneHook render mode: hand the frame to a surviving
        // client before this image is unmapped. The loader lock is held here, so
        // only lock-free stores are performed (may_wait = false).
        scenehook_unregister(g_scene_bus, g_scene_slot, false);
        g_scene_slot = -1;
    }
    return TRUE;
}
