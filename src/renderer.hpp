// TargetLines for Ashita v4 - Direct3D 8 line and ring renderer.
//
// Port of the TargetLines v2 native renderer (TargetLinesRenderer.cpp). The
// JSON state transport and Windower-specific entity discovery were removed; the
// tracker hands over RenderState structures directly and live entity anchors
// come from Ashita's entity table.
//
// Every Direct3D state the renderer changes is captured before drawing and
// restored afterwards so the game and other plugins see the device untouched.

#ifndef TARGETLINES_RENDERER_HPP_INCLUDED
#define TARGETLINES_RENDERER_HPP_INCLUDED

#include "Ashita.h"

#include "state.hpp"

#include <cstddef>
#include <cstdint>

namespace targetlines
{
    class Game;
    class Logger;

    class Renderer final
    {
    public:
        Renderer(Game& game, Logger& logger);
        ~Renderer(void);

        Renderer(const Renderer&)            = delete;
        Renderer& operator=(const Renderer&) = delete;

        // Publishes a complete snapshot. Safe to call from any thread.
        void set_state(const RenderState& state);

        // Draws the current snapshot. Must be called between BeginScene and EndScene
        // on the device's thread.
        void render(IDirect3DDevice8* device);

        // Forgets animation and anchor caches (zone changes, /tl clear).
        void reset(void);

        void status(char* output, size_t output_size) const;
        unsigned long render_calls(void) const;

    private:
        struct DrawVertex
        {
            float x;
            float y;
            float z;
            float rhw;
            DWORD color;
        };

        struct ActiveLine
        {
            unsigned long long key = 0;
            DWORD start_ms         = 0;
            DWORD last_seen_ms     = 0;
            bool spread_source     = false;
            bool spread_target     = false;
        };

        struct ActiveRing
        {
            unsigned long long key = 0;
            DWORD start_ms         = 0;
            DWORD last_seen_ms     = 0;
        };

        struct AnchorCacheEntry
        {
            DWORD index             = 0;
            int bone                = -1;
            bool is_npc             = false;
            bool resolved           = false;
            bool uses_height_offset = true;
            float x                 = 0.0f;
            float y                 = 0.0f;
            float z                 = 0.0f;
        };

        static constexpr int head_marker_slices_        = 36;
        static constexpr int round_cap_slices_          = 24;
        static constexpr int max_line_batch_vertices_   = 32760;
        static constexpr int max_anchor_cache_entries_  = 256;
        static constexpr int max_active_lines_          = 128;
        static constexpr int max_active_rings_          = 32;
        static constexpr float ring_marker_duration_    = 1.00f;

        void initialize_geometry_tables(void);
        void consume_pending_state(void);
        void apply_settings(const RenderSettings& settings);

        void draw_lines(void);
        bool prepare_animated_line(const LineState& line, DWORD now_ms, bool spread_source, bool spread_target, ActiveLine*& active, float& progress, float& arc_settle, float& settle, float& tail, DWORD& color);
        void draw_line_curve(const LineState& line, const D3DVIEWPORT8& viewport, bool spread_source, bool spread_target, float progress, float arc_settle, float settle, float tail, DWORD color);
        void draw_aoe_ring(const RingState& ring, const D3DVIEWPORT8& viewport, DWORD now_ms);
        void draw_projected_ring(const D3DVIEWPORT8& viewport, float center_x, float center_y, float center_z, float radius, float thickness, DWORD color);
        void draw_projected_comet_arc(const D3DVIEWPORT8& viewport, float center_x, float center_y, float center_z, float radius, float thickness, DWORD glow_color, DWORD backing_color, DWORD core_color, DWORD shine_color, float head_phase, float tail_extent, float head_extent);
        void draw_ring_impact_marker(const RingTargetState& target, const D3DVIEWPORT8& viewport, float center_x, float center_y, float ring_radius, float ring_age, float pulse_duration, DWORD color, int indicator_style, bool center_target);
        void draw_head_marker(float center_x, float center_y, float radius, DWORD core_color, DWORD shine_color, float direction_x, float direction_y, float landing_t, float base_alpha);
        void draw_round_line_cap(float center_x, float center_y, float direction_x, float direction_y, float radius, DWORD color);
        float directional_head_fade(float unit_x, float unit_y, float direction_x, float direction_y, float target_absorb) const;

        unsigned long long line_key(const LineState& line) const;
        unsigned long long ring_key(const RingState& ring) const;
        bool same_target(const LineState& left, const LineState& right) const;
        bool source_matches_target(const LineState& source_line, const LineState& target_line) const;
        bool line_is_newer_than(const LineState& line, const LineState& other, int index, int other_index) const;
        void prepare_new_line_relationships(const LineState* lines, int line_count, bool* spread_sources, bool* spread_targets);
        void apply_target_spread(const LineState& line, float& target_x, float& target_y) const;
        void apply_source_spread(const LineState& line, float& source_x, float& source_y) const;

        float model_adjusted_height(float manual_offset, float model_size, float model_scale, bool short_anchor, bool floating_anchor, bool is_npc) const;
        int anchor_bone_for_entity(DWORD race, DWORD model, bool is_npc) const;
        bool resolve_live_anchor(bool is_npc, DWORD index, int bone, float& lua_x, float& lua_y, float& lua_z, bool& uses_height_offset);
        bool resolve_live_anchor_cached(bool is_npc, DWORD index, int bone, float height_offset, float& lua_x, float& lua_y, float& lua_z);
        void run_boneprobe(const LineState& line);

        ActiveLine* find_active_line(unsigned long long key);
        ActiveLine* allocate_active_line(unsigned long long key, DWORD now_ms);
        ActiveRing* find_active_ring(unsigned long long key);
        ActiveRing* allocate_active_ring(unsigned long long key, DWORD now_ms);
        void prune_active_lines(DWORD now_ms);
        void prune_active_rings(DWORD now_ms);

        bool refresh_projection_matrices(void);
        bool live_world_to_screen(float lua_x, float lua_y, float lua_z, const D3DVIEWPORT8& viewport, float& screen_x, float& screen_y) const;

        void append_segment_quad_with_normal(DrawVertex* vertices, int& vertex_count, float x1, float y1, float x2, float y2, float normal_x, float normal_y, float thickness, DWORD color);
        void append_segment_quad_clipped_to_circle(DrawVertex* vertices, int& vertex_count, float x1, float y1, float x2, float y2, float segment_dx, float segment_dy, float segment_length, float segment_length_sq, float normal_x, float normal_y, float thickness, DWORD color, float circle_x, float circle_y, float radius);

        bool begin_draw_state(void);
        void end_draw_state(void);
        void begin_line_batch(void);
        void flush_line_batch(void);
        void end_line_batch(void);
        void draw_vertices(D3DPRIMITIVETYPE primitive_type, UINT primitive_count, const DrawVertex* vertices, UINT stride);
        void submit_vertices(D3DPRIMITIVETYPE primitive_type, UINT primitive_count, const DrawVertex* vertices, UINT stride);

        void log(const char* message);

        Game& game_;
        Logger& logger_;

        // Snapshot transport.
        CRITICAL_SECTION state_lock_ {};
        RenderState pending_state_ {};
        RenderState state_ {};
        volatile LONG pending_generation_ = 0;
        LONG consumed_generation_         = 0;
        LONG state_updates_               = 0;

        // Device and projection.
        IDirect3DDevice8* d3d_device_ = nullptr;
        D3DMATRIX cached_view_ {};
        D3DMATRIX cached_projection_ {};
        D3DMATRIX cached_view_projection_ {};
        bool projection_matrices_valid_ = false;
        unsigned long render_calls_     = 0;

        // Captured device state.
        DWORD saved_shader_   = 0;
        DWORD saved_alpha_    = 0;
        DWORD saved_src_      = 0;
        DWORD saved_dest_     = 0;
        DWORD saved_z_        = 0;
        DWORD saved_lighting_ = 0;
        DWORD saved_cull_     = 0;
        IDirect3DBaseTexture8* saved_texture_  = nullptr;
        IDirect3DVertexBuffer8* saved_stream_  = nullptr;
        UINT saved_stream_stride_              = 0;
        bool draw_state_active_                = false;

        // Line batching.
        DrawVertex* line_batch_vertices_ = nullptr;
        int line_batch_vertex_count_     = 0;
        bool line_batch_active_          = false;

        // Visual settings (from RenderState::settings).
        float source_height_offset_ = -0.75f;
        float target_height_offset_ = -1.35f;
        float arc_height_offset_    = -2.0f;
        float opacity_scale_        = 0.8f;
        float width_scale_          = 1.0f;
        float glow_scale_           = 1.0f;
        int dynamic_bone_           = 21;

        // Diagnostics.
        bool boneprobe_requested_ = false;
        DWORD last_boneprobe_ms_  = 0;

        // Animation and anchor caches.
        AnchorCacheEntry anchor_cache_[max_anchor_cache_entries_] {};
        int anchor_cache_count_ = 0;
        float head_unit_x_[head_marker_slices_ + 1] {};
        float head_unit_y_[head_marker_slices_ + 1] {};
        float cap_unit_x_[round_cap_slices_ + 1] {};
        float cap_unit_y_[round_cap_slices_ + 1] {};
        ActiveLine active_lines_[max_active_lines_] {};
        int active_line_count_ = 0;
        ActiveRing active_rings_[max_active_rings_] {};
        int active_ring_count_ = 0;
    };
} // namespace targetlines

#endif // TARGETLINES_RENDERER_HPP_INCLUDED
