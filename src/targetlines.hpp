// TargetLines for Ashita v4 - plugin entry point.
//
// Final Fantasy XII style target lines for FFXI. Port of MogSafe's Windower
// TargetLines v2 addon to a single Ashita v4 plugin.

#ifndef TARGETLINES_PLUGIN_HPP_INCLUDED
#define TARGETLINES_PLUGIN_HPP_INCLUDED

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

#include "Ashita.h"

#include "game.hpp"
#include "logger.hpp"
#include "renderer.hpp"
#include "settings.hpp"
#include "state.hpp"
#include "tracker.hpp"

#include <memory>
#include <string>
#include <vector>

namespace targetlines
{
    class Plugin final : public IPlugin
    {
    public:
        Plugin(void);
        ~Plugin(void) override;

        // Properties (Plugin Information)
        const char* GetName(void) const override;
        const char* GetAuthor(void) const override;
        const char* GetDescription(void) const override;
        const char* GetLink(void) const override;
        double GetVersion(void) const override;
        double GetInterfaceVersion(void) const override;
        int32_t GetPriority(void) const override;
        uint32_t GetFlags(void) const override;

        // Methods
        bool Initialize(IAshitaCore* core, ILogManager* logger, uint32_t id) override;
        void Release(void) override;

        // Event Callbacks: ChatManager
        bool HandleCommand(int32_t mode, const char* command, bool injected) override;

        // Event Callbacks: PacketManager
        bool HandleIncomingPacket(uint16_t id, uint32_t size, const uint8_t* data, uint8_t* modified, uint32_t sizeChunk, const uint8_t* dataChunk, bool injected, bool blocked) override;

        // Event Callbacks: Direct3D
        bool Direct3DInitialize(IDirect3DDevice8* device) override;
        void Direct3DEndScene(bool isRenderingBackBuffer) override;
        void Direct3DPresent(const RECT* pSourceRect, const RECT* pDestRect, HWND hDestWindowOverride, const RGNDATA* pDirtyRegion) override;

        // SceneHook draw callback (invoked from inside FFXiMain!draw_scene).
        void scene_draw(void);

    private:
        void tick(double now);
        void render_config_window(void);

        // Chat output.
        void print(const std::string& message) const;
        void warn(const std::string& message) const;

        // Settings helpers.
        void on_settings_changed(bool save_now);
        void save_settings(void);
        void push_empty_state(void);
        void set_enabled(bool enabled);
        void set_aoe_mode(AoeMode mode);
        void set_regular_mode(RegularMode mode);
        void set_render_mode(RenderMode mode);
        bool set_bool_from_arg(bool& field, const std::string* arg);
        void adjust_slider(float& field, float delta);
        bool adjust_scale_command(float& field, const std::string& usage, const std::string* arg, const char* label);

        // Render modes.
        void apply_render_mode(void);
        bool start_scenehook(void);
        void stop_scenehook(void);

        void print_help(void) const;
        void print_status(void);

        IAshitaCore* core_        = nullptr;
        ILogManager* log_         = nullptr;
        IDirect3DDevice8* device_ = nullptr;
        uint32_t id_              = 0;

        Settings settings_ {};
        Logger logger_ {};
        std::unique_ptr<Game> game_;
        std::unique_ptr<Tracker> tracker_;
        std::unique_ptr<Renderer> renderer_;

        std::string config_dir_;
        std::string settings_path_;
        std::string inspect_path_;

        RenderState state_ {};
        bool config_visible_        = false;
        bool settings_dirty_        = false;
        double settings_dirty_at_   = 0.0;
        double boneprobe_until_     = 0.0;
        double last_scenehook_try_  = -100.0;
        bool scenehook_active_      = false;
        std::string scenehook_status_ = "not attached";
        unsigned action_packets_    = 0;
    };
} // namespace targetlines

#endif // TARGETLINES_PLUGIN_HPP_INCLUDED
