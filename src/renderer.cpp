#include "renderer.hpp"

#include "game.hpp"
#include "logger.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace targetlines
{
    namespace
    {
        constexpr float kTwoPi = 6.28318530718f;

        bool is_readable_page(DWORD protect)
        {
            if (protect & (PAGE_GUARD | PAGE_NOACCESS))
            {
                return false;
            }

            const DWORD base = protect & 0xFF;
            return base == PAGE_READONLY || base == PAGE_READWRITE || base == PAGE_WRITECOPY || base == PAGE_EXECUTE_READ || base == PAGE_EXECUTE_READWRITE || base == PAGE_EXECUTE_WRITECOPY;
        }

        bool is_readable_range(std::uintptr_t address, std::size_t size)
        {
            if (address == 0 || size == 0)
            {
                return false;
            }

            MEMORY_BASIC_INFORMATION mbi {};
            if (!VirtualQuery(reinterpret_cast<const void*>(address), &mbi, sizeof(mbi)))
            {
                return false;
            }

            const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
            const std::uintptr_t end  = base + mbi.RegionSize;
            return mbi.State == MEM_COMMIT && is_readable_page(mbi.Protect) && address >= base && address + size <= end;
        }

        template<typename T>
        bool read_memory(std::uintptr_t address, T& value)
        {
            if (!is_readable_range(address, sizeof(T)))
            {
                return false;
            }

            SIZE_T bytes_read = 0;
            return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), &value, sizeof(T), &bytes_read) && bytes_read == sizeof(T);
        }

        bool finite_world(float x, float y, float z)
        {
            return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::fabs(x) <= 10000.0f && std::fabs(y) <= 10000.0f && std::fabs(z) <= 10000.0f;
        }

        // Actor root position. The game stores X, height, ground-Y; the Lua
        // convention names height z and ground y.
        bool read_actor_root(std::uintptr_t actor, float& lua_x, float& lua_y, float& lua_z)
        {
            float root_x = 0.0f;
            float root_z = 0.0f;
            float root_y = 0.0f;
            if (!read_memory(actor + 0x678, root_x) || !read_memory(actor + 0x67C, root_z) || !read_memory(actor + 0x680, root_y))
            {
                return false;
            }
            if (!finite_world(root_x, root_y, root_z))
            {
                return false;
            }

            lua_x = root_x;
            lua_y = root_y;
            lua_z = root_z;
            return true;
        }

        bool read_bone_anchor(std::uintptr_t actor, int bone, float& lua_x, float& lua_y, float& lua_z, char* detail, std::size_t detail_size)
        {
            float root_x = 0.0f;
            float root_z = 0.0f;
            float root_y = 0.0f;
            std::uint32_t skeleton_base   = 0;
            std::uint32_t skeleton_offset = 0;
            std::uint32_t skeleton        = 0;
            std::uint16_t bone_count      = 0;

            if (!read_memory(actor + 0x678, root_x) || !read_memory(actor + 0x67C, root_z) || !read_memory(actor + 0x680, root_y) || !read_memory(actor + 0x6B8, skeleton_base) || !read_memory(static_cast<std::uintptr_t>(skeleton_base) + 0x0C, skeleton_offset) || !read_memory(static_cast<std::uintptr_t>(skeleton_offset), skeleton) || !read_memory(static_cast<std::uintptr_t>(skeleton) + 0x32, bone_count))
            {
                if (detail && detail_size > 0)
                {
                    std::snprintf(detail, detail_size, "bone_read_failed");
                }
                return false;
            }

            if (!finite_world(root_x, root_y, root_z))
            {
                if (detail && detail_size > 0)
                {
                    std::snprintf(detail, detail_size, "invalid_root=(%.3f %.3f %.3f)", root_x, root_y, root_z);
                }
                return false;
            }

            if (bone_count == 0 || bone_count > 256 || bone < 0 || bone >= static_cast<int>(bone_count))
            {
                if (detail && detail_size > 0)
                {
                    std::snprintf(detail, detail_size, "invalid_bone_count=%u skeleton=%08lx bone=%d", bone_count, static_cast<unsigned long>(skeleton), bone);
                }
                return false;
            }

            const std::uintptr_t generators = static_cast<std::uintptr_t>(skeleton) + 0x30 + 0x04 + 0x1E * bone_count + 4;
            const std::uintptr_t bone_base  = generators + static_cast<std::uintptr_t>(bone) * 0x1A + 0x0E;
            float bone_x = 0.0f;
            float bone_z = 0.0f;
            float bone_y = 0.0f;
            if (!read_memory(bone_base + 0x0, bone_x) || !read_memory(bone_base + 0x4, bone_z) || !read_memory(bone_base + 0x8, bone_y))
            {
                if (detail && detail_size > 0)
                {
                    std::snprintf(detail, detail_size, "bone_offset_read_failed bone_count=%u skeleton=%08lx generators=%08lx", bone_count, static_cast<unsigned long>(skeleton), static_cast<unsigned long>(generators));
                }
                return false;
            }

            if (!std::isfinite(bone_x) || !std::isfinite(bone_y) || !std::isfinite(bone_z) || std::fabs(bone_x) > 100.0f || std::fabs(bone_y) > 100.0f || std::fabs(bone_z) > 100.0f)
            {
                if (detail && detail_size > 0)
                {
                    std::snprintf(detail, detail_size, "invalid_bone_offset bone_count=%u skeleton=%08lx bone=(%.3f %.3f %.3f)", bone_count, static_cast<unsigned long>(skeleton), bone_x, bone_y, bone_z);
                }
                return false;
            }

            lua_x = root_x + bone_x;
            lua_y = root_y + bone_y;
            lua_z = root_z + bone_z;
            if (!std::isfinite(lua_x) || !std::isfinite(lua_y) || !std::isfinite(lua_z))
            {
                if (detail && detail_size > 0)
                {
                    std::snprintf(detail, detail_size, "invalid_anchor");
                }
                return false;
            }

            if (detail && detail_size > 0)
            {
                std::snprintf(detail, detail_size, "bone_count=%u skeleton=%08lx root=(%.3f %.3f %.3f) bone=(%.3f %.3f %.3f)", bone_count, static_cast<unsigned long>(skeleton), root_x, root_y, root_z, bone_x, bone_y, bone_z);
            }
            return true;
        }

        DWORD scale_alpha(DWORD color, float scale)
        {
            const DWORD alpha        = (color >> 24) & 0xFF;
            const DWORD scaled_alpha = static_cast<DWORD>(std::fmax(0.0f, std::fmin(255.0f, static_cast<float>(alpha) * scale)));
            return (color & 0x00FFFFFF) | (scaled_alpha << 24);
        }

        DWORD darken_color(DWORD color)
        {
            const DWORD alpha = color & 0xFF000000;
            const DWORD red   = ((color >> 16) & 0xFF) / 3;
            const DWORD green = ((color >> 8) & 0xFF) / 3;
            const DWORD blue  = (color & 0xFF) / 3;
            return alpha | (red << 16) | (green << 8) | blue;
        }

        DWORD saturate_color(DWORD color)
        {
            const DWORD alpha = color & 0xFF000000;
            int red           = static_cast<int>((color >> 16) & 0xFF);
            int green         = static_cast<int>((color >> 8) & 0xFF);
            int blue          = static_cast<int>(color & 0xFF);
            const int gray    = (red * 30 + green * 59 + blue * 11) / 100;

            red   = gray + (red - gray) * 7 / 5;
            green = gray + (green - gray) * 7 / 5;
            blue  = gray + (blue - gray) * 7 / 5;

            red   = red < 0 ? 0 : (red > 255 ? 255 : red);
            green = green < 0 ? 0 : (green > 255 ? 255 : green);
            blue  = blue < 0 ? 0 : (blue > 255 ? 255 : blue);
            return alpha | (static_cast<DWORD>(red) << 16) | (static_cast<DWORD>(green) << 8) | static_cast<DWORD>(blue);
        }

        DWORD tint_white_color(DWORD color, float amount)
        {
            const DWORD alpha = color & 0xFF000000;
            DWORD red         = (color >> 16) & 0xFF;
            DWORD green       = (color >> 8) & 0xFF;
            DWORD blue        = color & 0xFF;
            amount            = std::fmax(0.0f, std::fmin(amount, 1.0f));

            red   = static_cast<DWORD>(static_cast<float>(red) + (255.0f - static_cast<float>(red)) * amount);
            green = static_cast<DWORD>(static_cast<float>(green) + (255.0f - static_cast<float>(green)) * amount);
            blue  = static_cast<DWORD>(static_cast<float>(blue) + (255.0f - static_cast<float>(blue)) * amount);
            return alpha | (red << 16) | (green << 8) | blue;
        }

        std::uint32_t mix_u32(std::uint32_t value)
        {
            value ^= value >> 16;
            value *= 0x7feb352dU;
            value ^= value >> 15;
            value *= 0x846ca68bU;
            value ^= value >> 16;
            return value;
        }
    } // namespace

    Renderer::Renderer(Game& game, Logger& logger)
        : game_(game)
        , logger_(logger)
    {
        InitializeCriticalSection(&state_lock_);
        line_batch_vertices_ = new DrawVertex[max_line_batch_vertices_];
        initialize_geometry_tables();
        log("renderer created");
    }

    Renderer::~Renderer(void)
    {
        end_draw_state();
        delete[] line_batch_vertices_;
        line_batch_vertices_ = nullptr;
        DeleteCriticalSection(&state_lock_);
    }

    void Renderer::log(const char* message)
    {
        logger_.runtime("native: %s", message);
    }

    void Renderer::initialize_geometry_tables(void)
    {
        for (int i = 0; i <= head_marker_slices_; ++i)
        {
            const float angle = kTwoPi * static_cast<float>(i) / static_cast<float>(head_marker_slices_);
            head_unit_x_[i]   = std::cos(angle);
            head_unit_y_[i]   = std::sin(angle);
        }

        for (int i = 0; i <= round_cap_slices_; ++i)
        {
            const float angle = -1.57079632679f + 3.14159265359f * static_cast<float>(i) / static_cast<float>(round_cap_slices_);
            cap_unit_x_[i]    = std::cos(angle);
            cap_unit_y_[i]    = std::sin(angle);
        }
    }

    void Renderer::set_state(const RenderState& state)
    {
        EnterCriticalSection(&state_lock_);
        pending_state_ = state;
        InterlockedIncrement(&pending_generation_);
        LeaveCriticalSection(&state_lock_);
    }

    void Renderer::consume_pending_state(void)
    {
        const LONG generation = InterlockedCompareExchange(&pending_generation_, 0, 0);
        if (generation == consumed_generation_)
        {
            return;
        }

        EnterCriticalSection(&state_lock_);
        state_               = pending_state_;
        consumed_generation_ = InterlockedCompareExchange(&pending_generation_, 0, 0);
        LeaveCriticalSection(&state_lock_);
        ++state_updates_;

        apply_settings(state_.settings);
        boneprobe_requested_ = state_.boneprobe;
    }

    void Renderer::apply_settings(const RenderSettings& settings)
    {
        if (settings.opacity >= 0.0f && settings.opacity <= 1.0f)
        {
            opacity_scale_ = settings.opacity;
        }
        if (settings.width >= 0.5f && settings.width <= 2.0f)
        {
            width_scale_ = settings.width;
        }
        if (settings.glow >= 0.5f && settings.glow <= 2.0f)
        {
            glow_scale_ = settings.glow;
        }
        if (settings.source_height >= -5.0f && settings.source_height <= 5.0f)
        {
            source_height_offset_ = settings.source_height;
        }
        if (settings.target_height >= -5.0f && settings.target_height <= 5.0f)
        {
            target_height_offset_ = settings.target_height;
        }
    }

    void Renderer::reset(void)
    {
        EnterCriticalSection(&state_lock_);
        pending_state_ = RenderState {};
        InterlockedIncrement(&pending_generation_);
        LeaveCriticalSection(&state_lock_);

        active_line_count_  = 0;
        active_ring_count_  = 0;
        anchor_cache_count_ = 0;
    }

    void Renderer::status(char* output, size_t output_size) const
    {
        if (!output || output_size == 0)
        {
            return;
        }
        std::snprintf(output, output_size, "lines=%d rings=%d device=%s render_calls=%lu state_updates=%ld active_lines=%d active_rings=%d",
            state_.line_count, state_.ring_count, d3d_device_ ? "ready" : "pending", render_calls_, state_updates_, active_line_count_, active_ring_count_);
    }

    unsigned long Renderer::render_calls(void) const
    {
        return render_calls_;
    }

    void Renderer::render(IDirect3DDevice8* device)
    {
        if (device == nullptr)
        {
            return;
        }

        d3d_device_                = device;
        projection_matrices_valid_ = false;
        ++render_calls_;

        consume_pending_state();
        draw_lines();
    }

    void Renderer::draw_lines(void)
    {
        const int line_count = state_.line_count;
        const int ring_count = state_.ring_count;
        if (line_count <= 0 && ring_count <= 0)
        {
            return;
        }

        if (boneprobe_requested_ && line_count > 0)
        {
            boneprobe_requested_ = false;
            const DWORD now_ms   = GetTickCount();
            if (now_ms - last_boneprobe_ms_ > 1000)
            {
                last_boneprobe_ms_ = now_ms;
                run_boneprobe(state_.lines[0]);
            }
        }

        D3DVIEWPORT8 viewport {};
        if (!refresh_projection_matrices())
        {
            return;
        }
        if (FAILED(d3d_device_->GetViewport(&viewport)))
        {
            viewport.Width  = 1024;
            viewport.Height = 768;
        }

        anchor_cache_count_ = 0;

        const DWORD now_ms = GetTickCount();
        bool spread_sources[kMaxLines] {};
        bool spread_targets[kMaxLines] {};
        prepare_new_line_relationships(state_.lines, line_count, spread_sources, spread_targets);

        if (!begin_draw_state())
        {
            return;
        }

        begin_line_batch();
        for (int i = 0; i < ring_count; ++i)
        {
            draw_aoe_ring(state_.rings[i], viewport, now_ms);
        }

        for (int i = 0; i < line_count; ++i)
        {
            ActiveLine* active = nullptr;
            float progress     = 1.0f;
            float arc_settle   = 0.0f;
            float settle       = 0.0f;
            float tail         = 0.0f;
            DWORD color        = state_.lines[i].color;
            if (!prepare_animated_line(state_.lines[i], now_ms, spread_sources[i], spread_targets[i], active, progress, arc_settle, settle, tail, color))
            {
                continue;
            }

            const bool spread_source = active && active->spread_source;
            const bool spread_target = active && active->spread_target;
            draw_line_curve(state_.lines[i], viewport, spread_source, spread_target, progress, arc_settle, settle, tail, color);
        }

        end_line_batch();
        end_draw_state();
        prune_active_lines(now_ms);
        prune_active_rings(now_ms);
    }

    bool Renderer::prepare_animated_line(const LineState& line, DWORD now_ms, bool spread_source, bool spread_target, ActiveLine*& active, float& progress, float& arc_settle, float& settle, float& tail, DWORD& color)
    {
        const unsigned long long key = line_key(line);
        active                       = find_active_line(key);
        if (!active)
        {
            active = allocate_active_line(key, now_ms);
            if (active)
            {
                active->spread_source = spread_source;
                active->spread_target = spread_target;
            }
        }

        if (!active)
        {
            return false;
        }

        active->last_seen_ms = now_ms;
        const float age      = static_cast<float>(now_ms - active->start_ms) / 1000.0f;
        const float timeout  = std::fmax(line.timeout, 0.1f);
        if (age > timeout)
        {
            return false;
        }

        const float travel_time     = std::fmin(0.32f, timeout * 0.25f);
        const float recede_start    = timeout * 0.61f;
        const float recede_duration = std::fmax(timeout - recede_start, 0.1f);
        progress                    = std::fmax(0.0f, std::fmin(age / std::fmax(travel_time, 0.05f), 1.0f));
        const float arc_duration    = std::fmax(timeout - travel_time, 0.1f);
        const float arc_t           = std::fmax(0.0f, std::fmin((age - travel_time) / arc_duration, 1.0f));
        arc_settle                  = arc_t * arc_t * (3.0f - 2.0f * arc_t);
        settle                      = 0.0f;
        if (age > recede_start)
        {
            settle = std::fmax(0.0f, std::fmin((age - recede_start) / recede_duration, 1.0f));
        }
        tail             = settle * settle * (3.0f - 2.0f * settle);
        const float fade = settle <= 0.0f ? 1.0f : std::sqrt(std::fmax(0.0f, 1.0f - settle));
        color            = scale_alpha(line.color, fade * opacity_scale_);
        return progress > 0.01f;
    }

    void Renderer::draw_line_curve(const LineState& line, const D3DVIEWPORT8& viewport, bool spread_source, bool spread_target, float progress, float arc_settle, float settle, float tail, DWORD color)
    {
        constexpr int segments         = 38;
        const float core_thickness     = 10.5f * width_scale_;
        const float border_thickness   = 12.5f * width_scale_;
        const float haze_thickness     = 16.5f * width_scale_ * (0.75f + glow_scale_ * 0.25f);
        const float head_radius        = core_thickness * 0.85f;
        const float head_outer_radius  = head_radius * 1.16f;
        const float head_inner_radius  = head_radius * 0.72f;
        const float head_hot_radius    = head_radius * 0.38f;
        DrawVertex haze_vertices[segments * 6] {};
        DrawVertex border_vertices[segments * 6] {};
        DrawVertex core_vertices[segments * 6] {};
        DrawVertex shine_vertices[segments * 6] {};
        int haze_vertex_count   = 0;
        int border_vertex_count = 0;
        int core_vertex_count   = 0;
        int shine_vertex_count  = 0;
        float head_x            = 0.0f;
        float head_y            = 0.0f;
        bool have_head          = false;
        const DWORD saturated_color = saturate_color(color);
        const DWORD haze_color      = scale_alpha(saturated_color, 0.24f * (0.75f + glow_scale_ * 0.25f));
        const DWORD border_color    = scale_alpha(darken_color(saturated_color), 0.82f);
        const DWORD core_color      = scale_alpha(tint_white_color(saturated_color, 0.50f), 1.25f);
        const DWORD shine_color     = scale_alpha(tint_white_color(saturated_color, 0.94f), 1.18f);

        const float source_height = model_adjusted_height(source_height_offset_, line.source_model_size, line.source_model_scale, line.source_short_anchor, line.source_floating_anchor, line.source_is_npc);
        const float target_height = model_adjusted_height(target_height_offset_, line.target_model_size, line.target_model_scale, line.target_short_anchor, line.target_floating_anchor, line.target_is_npc);
        float p0_x = line.source_x;
        float p0_y = line.source_y;
        float p0_z = line.source_z + source_height;
        float p2_x = line.target_x;
        float p2_y = line.target_y;
        float p2_z = line.target_z + target_height;
        resolve_live_anchor_cached(line.source_is_npc, line.source_index, anchor_bone_for_entity(line.source_race, line.source_model, line.source_is_npc), source_height, p0_x, p0_y, p0_z);
        resolve_live_anchor_cached(line.target_is_npc, line.target_index, anchor_bone_for_entity(line.target_race, line.target_model, line.target_is_npc), target_height, p2_x, p2_y, p2_z);
        if (spread_source)
        {
            apply_source_spread(line, p0_x, p0_y);
        }
        if (spread_target)
        {
            apply_target_spread(line, p2_x, p2_y);
        }
        const float dx               = p2_x - p0_x;
        const float dy               = p2_y - p0_y;
        const float distance         = std::sqrt(dx * dx + dy * dy);
        const float arc_height_scale = std::fmax(0.50f, 1.0f - arc_settle * 0.30f - settle * 0.20f);
        const float arc_lift         = (arc_height_offset_ - std::fmin(distance * 0.10f, 1.5f)) * arc_height_scale;
        const float p1_x             = (p0_x + p2_x) * 0.5f;
        const float p1_y             = (p0_y + p2_y) * 0.5f;
        const float p1_z             = (p0_z + p2_z) * 0.5f + arc_lift;

        const float end_t         = std::fmax(0.0f, std::fmin(progress, 1.0f));
        const float start_t       = std::fmax(0.0f, std::fmin(tail * 0.985f, end_t - 0.015f));
        const float visible_range = end_t - start_t;
        if (visible_range <= 0.01f)
        {
            return;
        }

        float screen_xs[segments + 1] {};
        float screen_ys[segments + 1] {};
        float segment_alphas[segments + 1] {};
        bool screen_valid[segments + 1] {};

        for (int i = 0; i <= segments; ++i)
        {
            const float local_t = static_cast<float>(i) / static_cast<float>(segments);
            const float t       = start_t + visible_range * local_t;
            const float inv     = 1.0f - t;
            const float world_x = inv * inv * p0_x + 2.0f * inv * t * p1_x + t * t * p2_x;
            const float world_y = inv * inv * p0_y + 2.0f * inv * t * p1_y + t * t * p2_y;
            const float world_z = inv * inv * p0_z + 2.0f * inv * t * p1_z + t * t * p2_z;
            float screen_x      = 0.0f;
            float screen_y      = 0.0f;

            if (!live_world_to_screen(world_x, world_y, world_z, viewport, screen_x, screen_y))
            {
                continue;
            }

            screen_xs[i]                = screen_x;
            screen_ys[i]                = screen_y;
            const float tail_fade       = 1.0f - (1.0f - local_t) * (1.0f - local_t);
            const float tail_soft_start = std::fmin(local_t / 0.12f, 1.0f);
            segment_alphas[i]           = tail_soft_start * (0.28f + 0.72f * tail_fade);
            screen_valid[i]             = true;
            head_x                      = screen_x;
            head_y                      = screen_y;
            have_head                   = true;
        }

        if (!have_head)
        {
            return;
        }

        float cap_dir_x = 1.0f;
        float cap_dir_y = 0.0f;
        for (int i = segments - 1; i >= 0; --i)
        {
            if (!screen_valid[i])
            {
                continue;
            }

            const float ddx    = head_x - screen_xs[i];
            const float ddy    = head_y - screen_ys[i];
            const float length = std::sqrt(ddx * ddx + ddy * ddy);
            if (length > 0.01f)
            {
                cap_dir_x = ddx / length;
                cap_dir_y = ddy / length;
                break;
            }
        }

        for (int i = 1; i <= segments; ++i)
        {
            if (!screen_valid[i - 1] || !screen_valid[i])
            {
                continue;
            }

            const float segment_alpha     = segment_alphas[i];
            const float x1                = screen_xs[i - 1];
            const float y1                = screen_ys[i - 1];
            const float x2                = screen_xs[i];
            const float y2                = screen_ys[i];
            const float segment_dx        = x2 - x1;
            const float segment_dy        = y2 - y1;
            const float segment_length_sq = segment_dx * segment_dx + segment_dy * segment_dy;
            if (segment_length_sq <= 0.0001f)
            {
                continue;
            }
            const float segment_length = std::sqrt(segment_length_sq);
            const float normal_x       = -segment_dy / segment_length;
            const float normal_y       = segment_dx / segment_length;

            append_segment_quad_with_normal(haze_vertices, haze_vertex_count, x1, y1, x2, y2, normal_x, normal_y, haze_thickness, scale_alpha(haze_color, segment_alpha));
            append_segment_quad_clipped_to_circle(border_vertices, border_vertex_count, x1, y1, x2, y2, segment_dx, segment_dy, segment_length, segment_length_sq, normal_x, normal_y, border_thickness, scale_alpha(border_color, segment_alpha), head_x, head_y, head_outer_radius * 0.72f);
            append_segment_quad_clipped_to_circle(core_vertices, core_vertex_count, x1, y1, x2, y2, segment_dx, segment_dy, segment_length, segment_length_sq, normal_x, normal_y, core_thickness, scale_alpha(core_color, segment_alpha), head_x, head_y, head_inner_radius * 0.82f);
            append_segment_quad_clipped_to_circle(shine_vertices, shine_vertex_count, x1, y1, x2, y2, segment_dx, segment_dy, segment_length, segment_length_sq, normal_x, normal_y, std::fmax(2.5f, core_thickness * 0.42f), scale_alpha(shine_color, segment_alpha), head_x, head_y, head_hot_radius * 0.92f);
        }

        if (haze_vertex_count > 0)
        {
            draw_vertices(D3DPT_TRIANGLELIST, haze_vertex_count / 3, haze_vertices, sizeof(DrawVertex));
        }

        const float head_entry_delay   = 0.08f;
        const float head_entry_t       = std::fmax(0.0f, std::fmin((arc_settle - head_entry_delay) / (1.0f - head_entry_delay), 1.0f));
        const float head_landing_t     = head_entry_t * head_entry_t * (3.0f - 2.0f * head_entry_t);
        const float head_alpha         = std::fmax(0.0f, 1.0f - settle * 0.85f);
        const float forward_cap_alpha  = head_alpha * (1.0f - head_landing_t * 0.72f);

        if (forward_cap_alpha > 0.01f)
        {
            draw_round_line_cap(head_x, head_y, cap_dir_x, cap_dir_y, haze_thickness * 0.5f, scale_alpha(haze_color, 0.80f * forward_cap_alpha));
        }

        if (border_vertex_count > 0)
        {
            draw_vertices(D3DPT_TRIANGLELIST, border_vertex_count / 3, border_vertices, sizeof(DrawVertex));
        }

        if (core_vertex_count > 0)
        {
            draw_vertices(D3DPT_TRIANGLELIST, core_vertex_count / 3, core_vertices, sizeof(DrawVertex));
        }

        if (shine_vertex_count > 0)
        {
            draw_vertices(D3DPT_TRIANGLELIST, shine_vertex_count / 3, shine_vertices, sizeof(DrawVertex));
        }

        if (head_alpha > 0.01f)
        {
            draw_head_marker(head_x, head_y, head_radius, core_color, shine_color, cap_dir_x, cap_dir_y, head_landing_t, head_alpha);
        }
    }

    unsigned long long Renderer::line_key(const LineState& line) const
    {
        const DWORD source = line.source_id ? line.source_id : static_cast<DWORD>(std::fabs(line.source_x * 100.0f));
        const DWORD target = line.target_id ? line.target_id : static_cast<DWORD>(std::fabs(line.target_x * 100.0f));
        const DWORD uid    = line.uid ? line.uid : (source ^ (target << 1));
        return (static_cast<unsigned long long>(uid) << 32) ^ (static_cast<unsigned long long>(source) << 16) ^ target;
    }

    unsigned long long Renderer::ring_key(const RingState& ring) const
    {
        const DWORD center = ring.center_id ? ring.center_id : static_cast<DWORD>(std::fabs(ring.center_x * 100.0f) + std::fabs(ring.center_y * 100.0f));
        const DWORD uid    = ring.uid ? ring.uid : center;
        return (static_cast<unsigned long long>(uid) << 32) ^ center;
    }

    void Renderer::draw_aoe_ring(const RingState& ring, const D3DVIEWPORT8& viewport, DWORD now_ms)
    {
        if (ring.radius <= 0.1f)
        {
            return;
        }

        const unsigned long long key = ring_key(ring);
        ActiveRing* active           = find_active_ring(key);
        if (!active)
        {
            active = allocate_active_ring(key, now_ms);
        }

        if (!active)
        {
            return;
        }

        active->last_seen_ms        = now_ms;
        const float timeout         = std::fmax(ring.timeout, 0.1f);
        const float age             = static_cast<float>(now_ms - active->start_ms) / 1000.0f;
        const float pulse_duration  = std::fmin(0.80f, timeout * 0.55f);
        const float visual_lifetime = std::fmax(timeout, pulse_duration + ring_marker_duration_);
        if (age > visual_lifetime)
        {
            return;
        }

        const float fade_start = timeout * 0.55f;
        float fade             = 1.0f;
        if (age > fade_start)
        {
            fade = 1.0f - std::fmax(0.0f, std::fmin((age - fade_start) / std::fmax(timeout - fade_start, 0.1f), 1.0f));
        }

        const float pulse_t_raw     = std::fmax(0.0f, std::fmin(age / std::fmax(pulse_duration, 0.05f), 1.0f));
        const float inverse_t       = 1.0f - pulse_t_raw;
        const float pulse_t         = 1.0f - std::pow(inverse_t, 1.5f);
        constexpr float pulse_start_scale = 0.08f;
        const float pulse_radius    = ring.radius * (pulse_start_scale + (1.0f - pulse_start_scale) * pulse_t);
        const float thickness       = 10.5f * width_scale_;
        const float haze_thickness  = 16.5f * width_scale_ * (0.75f + glow_scale_ * 0.25f);
        const float outer_attack_t  = std::fmax(0.0f, std::fmin(age / 0.20f, 1.0f));
        const float outer_attack    = outer_attack_t * outer_attack_t * (3.0f - 2.0f * outer_attack_t);
        const DWORD outer_color      = scale_alpha(ring.color, 0.38f * outer_attack * fade * opacity_scale_);
        const DWORD pulse_color      = scale_alpha(ring.color, (0.82f - pulse_t * 0.18f) * fade * opacity_scale_);
        const DWORD outer_haze_color = scale_alpha(saturate_color(ring.color), 0.16f * outer_attack * fade * opacity_scale_);
        const DWORD pulse_haze_color = scale_alpha(saturate_color(ring.color), 0.24f * fade * opacity_scale_);

        const float center_x      = ring.center_x;
        const float center_y      = ring.center_y;
        const float center_height = model_adjusted_height(source_height_offset_, ring.center_model_size, ring.center_model_scale, ring.center_short_anchor, ring.center_floating_anchor, ring.center_is_npc);
        const float center_z      = ring.center_z + center_height;
        // Keep the AoE footprint at the action-time position. Impact markers
        // still resolve their targets live so they remain attached to affected
        // entities while the stationary ring shows where the effect occurred.

        if (age <= timeout)
        {
            draw_projected_ring(viewport, center_x, center_y, center_z, ring.radius, haze_thickness, outer_haze_color);
            draw_projected_ring(viewport, center_x, center_y, center_z, ring.radius, thickness, outer_color);
            draw_projected_ring(viewport, center_x, center_y, center_z, std::fmax(0.05f, pulse_radius), haze_thickness, pulse_haze_color);
            draw_projected_ring(viewport, center_x, center_y, center_z, std::fmax(0.05f, pulse_radius), thickness, pulse_color);
        }

        for (int i = 0; i < ring.target_count; ++i)
        {
            const RingTargetState& target     = ring.targets[i];
            const bool center_target_by_id    = ring.center_id != 0 && target.id != 0 && ring.center_id == target.id;
            const bool center_target_by_index = ring.center_index != 0 && target.index != 0 && ring.center_index == target.index && ring.center_is_npc == target.is_npc;
            draw_ring_impact_marker(target, viewport, center_x, center_y, ring.radius, age, pulse_duration, ring.color, ring.indicator_style, center_target_by_id || center_target_by_index);
        }
    }

    void Renderer::draw_projected_ring(const D3DVIEWPORT8& viewport, float center_x, float center_y, float center_z, float radius, float thickness, DWORD color)
    {
        constexpr int segments = 72;
        DrawVertex vertices[segments * 6] {};
        int vertex_count = 0;
        float previous_x = 0.0f;
        float previous_y = 0.0f;
        bool previous_ok = false;

        for (int i = 0; i <= segments; ++i)
        {
            const float angle   = (static_cast<float>(i) / static_cast<float>(segments)) * kTwoPi;
            const float world_x = center_x + std::cos(angle) * radius;
            const float world_y = center_y + std::sin(angle) * radius;
            float screen_x      = 0.0f;
            float screen_y      = 0.0f;
            const bool ok       = live_world_to_screen(world_x, world_y, center_z, viewport, screen_x, screen_y);
            if (ok && previous_ok && vertex_count <= (segments * 6 - 6))
            {
                const float segment_dx     = screen_x - previous_x;
                const float segment_dy     = screen_y - previous_y;
                const float segment_length = std::sqrt(segment_dx * segment_dx + segment_dy * segment_dy);
                if (segment_length > 0.01f)
                {
                    append_segment_quad_with_normal(vertices, vertex_count, previous_x, previous_y, screen_x, screen_y, -segment_dy / segment_length, segment_dx / segment_length, thickness, color);
                }
            }

            previous_x  = screen_x;
            previous_y  = screen_y;
            previous_ok = ok;
        }

        if (vertex_count > 0)
        {
            draw_vertices(D3DPT_TRIANGLELIST, vertex_count / 3, vertices, sizeof(DrawVertex));
        }
    }

    void Renderer::draw_projected_comet_arc(const D3DVIEWPORT8& viewport, float center_x, float center_y, float center_z, float radius, float thickness, DWORD glow_color, DWORD backing_color, DWORD core_color, DWORD shine_color, float head_phase, float tail_extent, float head_extent)
    {
        constexpr int max_segments = 72;
        const float total_extent   = std::fmax(tail_extent + head_extent, 0.02f);
        const float tail_fraction  = std::fmax(0.0f, tail_extent) / total_extent;
        const int segments         = std::max(8, std::min(max_segments, static_cast<int>(std::ceil(total_extent / kTwoPi * static_cast<float>(max_segments)))));
        DrawVertex glow_vertices[max_segments * 6] {};
        DrawVertex backing_vertices[max_segments * 6] {};
        DrawVertex core_vertices[max_segments * 6] {};
        DrawVertex shine_vertices[max_segments * 6] {};
        int glow_vertex_count    = 0;
        int backing_vertex_count = 0;
        int core_vertex_count    = 0;
        int shine_vertex_count   = 0;

        for (int i = 0; i < segments; ++i)
        {
            const float u0       = static_cast<float>(i) / static_cast<float>(segments);
            const float u1       = static_cast<float>(i + 1) / static_cast<float>(segments);
            const float middle_u = (u0 + u1) * 0.5f;
            const float angle0   = head_phase - total_extent + total_extent * u0;
            const float angle1   = head_phase - total_extent + total_extent * u1;
            float screen_x0      = 0.0f;
            float screen_y0      = 0.0f;
            float screen_x1      = 0.0f;
            float screen_y1      = 0.0f;
            if (!live_world_to_screen(center_x + std::cos(angle0) * radius, center_y + std::sin(angle0) * radius, center_z, viewport, screen_x0, screen_y0) || !live_world_to_screen(center_x + std::cos(angle1) * radius, center_y + std::sin(angle1) * radius, center_z, viewport, screen_x1, screen_y1))
            {
                continue;
            }

            float intensity   = 1.0f;
            float width_scale = 1.0f;
            if (middle_u < tail_fraction)
            {
                const float tail_t = middle_u / tail_fraction;
                intensity          = 0.10f + 0.90f * tail_t * tail_t;
                width_scale        = 0.28f + 0.72f * tail_t;
            }

            const float segment_dx     = screen_x1 - screen_x0;
            const float segment_dy     = screen_y1 - screen_y0;
            const float segment_length = std::sqrt(segment_dx * segment_dx + segment_dy * segment_dy);
            if (segment_length <= 0.01f)
            {
                continue;
            }
            const float normal_x = -segment_dy / segment_length;
            const float normal_y = segment_dx / segment_length;
            append_segment_quad_with_normal(glow_vertices, glow_vertex_count, screen_x0, screen_y0, screen_x1, screen_y1, normal_x, normal_y, thickness * 2.35f * width_scale, scale_alpha(glow_color, intensity));
            append_segment_quad_with_normal(backing_vertices, backing_vertex_count, screen_x0, screen_y0, screen_x1, screen_y1, normal_x, normal_y, thickness * 1.70f * width_scale, scale_alpha(backing_color, intensity));
            append_segment_quad_with_normal(core_vertices, core_vertex_count, screen_x0, screen_y0, screen_x1, screen_y1, normal_x, normal_y, thickness * width_scale, scale_alpha(core_color, intensity));
            if (middle_u >= 0.86f)
            {
                const float shine_t = (middle_u - 0.86f) / 0.14f;
                append_segment_quad_with_normal(shine_vertices, shine_vertex_count, screen_x0, screen_y0, screen_x1, screen_y1, normal_x, normal_y, std::fmax(1.25f, thickness * 0.32f), scale_alpha(shine_color, shine_t));
            }
        }

        if (glow_vertex_count > 0)
        {
            draw_vertices(D3DPT_TRIANGLELIST, glow_vertex_count / 3, glow_vertices, sizeof(DrawVertex));
        }
        if (backing_vertex_count > 0)
        {
            draw_vertices(D3DPT_TRIANGLELIST, backing_vertex_count / 3, backing_vertices, sizeof(DrawVertex));
        }
        if (core_vertex_count > 0)
        {
            draw_vertices(D3DPT_TRIANGLELIST, core_vertex_count / 3, core_vertices, sizeof(DrawVertex));
        }
        if (shine_vertex_count > 0)
        {
            draw_vertices(D3DPT_TRIANGLELIST, shine_vertex_count / 3, shine_vertices, sizeof(DrawVertex));
        }
    }

    void Renderer::draw_ring_impact_marker(const RingTargetState& target, const D3DVIEWPORT8& viewport, float center_x, float center_y, float ring_radius, float ring_age, float pulse_duration, DWORD color, int indicator_style, bool center_target)
    {
        float target_x            = target.x;
        float target_y            = target.y;
        const float target_height = model_adjusted_height(target_height_offset_, target.model_size, target.model_scale, target.short_anchor, target.floating_anchor, target.is_npc);
        float target_z            = target.z + target_height;
        resolve_live_anchor_cached(target.is_npc, target.index, anchor_bone_for_entity(target.race, target.model, target.is_npc), target_height, target_x, target_y, target_z);

        const float target_scale   = target.model_scale > 0.0f ? target.model_scale : 1.0f;
        const float effective_size = std::fmax(0.0f, target.model_size * target_scale);
        const float halo_radius    = std::fmax(0.80f, std::fmin(0.75f + effective_size * 0.18f, 1.35f));

        const float dx             = target_x - center_x;
        const float dy             = target_y - center_y;
        const float distance_ratio = std::fmax(0.0f, std::fmin(std::sqrt(dx * dx + dy * dy) / std::fmax(ring_radius, 0.1f), 1.0f));
        float marker_age           = 0.0f;
        if (center_target)
        {
            // Play the center marker last so it does not compound visually with
            // the inner pulse while that pulse is expanding from the same point.
            marker_age = ring_age - pulse_duration;
        }
        else
        {
            constexpr float pulse_start_scale = 0.08f;
            const float wave_ratio = std::fmax(0.0f, std::fmin((distance_ratio - pulse_start_scale) / (1.0f - pulse_start_scale), 1.0f));
            const float arrival_t  = 1.0f - std::pow(std::fmax(0.0f, 1.0f - wave_ratio), 2.0f / 3.0f);
            marker_age             = ring_age - pulse_duration * arrival_t;
        }
        if (marker_age < 0.0f || marker_age > ring_marker_duration_)
        {
            return;
        }

        const float marker_t   = std::fmax(0.0f, std::fmin(marker_age / ring_marker_duration_, 1.0f));
        const float attack     = std::fmin(marker_age / 0.10f, 1.0f);
        const float fade_start = ring_marker_duration_ * 0.55f;
        const float fade       = marker_age <= fade_start ? 1.0f : 1.0f - std::fmax(0.0f, std::fmin((marker_age - fade_start) / std::fmax(ring_marker_duration_ - fade_start, 0.1f), 1.0f));
        const float alpha      = attack * fade * opacity_scale_;
        if (alpha <= 0.01f)
        {
            return;
        }

        const float line_core_thickness = 10.5f * width_scale_;
        const DWORD saturated_color     = saturate_color(color);

        const float halo_phase          = marker_t * 3.49065850399f;
        const DWORD halo_backing_color  = scale_alpha(darken_color(saturated_color), 0.82f * alpha);
        const DWORD halo_glow_color     = scale_alpha(saturated_color, 0.26f * alpha);
        const DWORD halo_core_color     = scale_alpha(saturated_color, 1.10f * alpha);
        const DWORD halo_shine_color    = scale_alpha(tint_white_color(saturated_color, 0.72f), 1.08f * alpha);

        const float halo_thickness = std::fmax(4.0f, line_core_thickness * 0.52f);
        if (indicator_style == 10)
        {
            const float contract_t      = marker_t * marker_t * (3.0f - 2.0f * marker_t);
            const float contract_radius = halo_radius * (1.45f - 0.75f * contract_t);
            draw_projected_ring(viewport, target_x, target_y, target_z, contract_radius, halo_thickness * 1.65f, halo_glow_color);
            draw_projected_ring(viewport, target_x, target_y, target_z, contract_radius, halo_thickness * 1.22f, halo_backing_color);
            draw_projected_ring(viewport, target_x, target_y, target_z, contract_radius, halo_thickness * 0.62f, halo_core_color);
        }
        else
        {
            draw_projected_comet_arc(viewport, target_x, target_y, target_z, halo_radius, halo_thickness, halo_glow_color, halo_backing_color, halo_core_color, halo_shine_color, halo_phase, 5.58505360638f, 0.69813170080f);
        }
    }

    bool Renderer::same_target(const LineState& left, const LineState& right) const
    {
        if (left.target_id != 0 && right.target_id != 0)
        {
            return left.target_id == right.target_id;
        }

        if (left.target_index != 0 && right.target_index != 0)
        {
            return left.target_index == right.target_index;
        }

        return std::fabs(left.target_x - right.target_x) <= 0.01f && std::fabs(left.target_y - right.target_y) <= 0.01f && std::fabs(left.target_z - right.target_z) <= 0.01f;
    }

    bool Renderer::source_matches_target(const LineState& source_line, const LineState& target_line) const
    {
        if (source_line.source_id != 0 && target_line.target_id != 0)
        {
            return source_line.source_id == target_line.target_id;
        }

        if (source_line.source_index != 0 && target_line.target_index != 0)
        {
            return source_line.source_index == target_line.target_index;
        }

        return std::fabs(source_line.source_x - target_line.target_x) <= 0.01f && std::fabs(source_line.source_y - target_line.target_y) <= 0.01f && std::fabs(source_line.source_z - target_line.target_z) <= 0.01f;
    }

    bool Renderer::line_is_newer_than(const LineState& line, const LineState& other, int index, int other_index) const
    {
        if (line.uid != 0 && other.uid != 0)
        {
            return line.uid > other.uid;
        }

        return index < other_index;
    }

    void Renderer::prepare_new_line_relationships(const LineState* lines, int line_count, bool* spread_sources, bool* spread_targets)
    {
        if (!lines || !spread_sources || !spread_targets || line_count <= 0)
        {
            return;
        }

        for (int index = 0; index < line_count; ++index)
        {
            if (find_active_line(line_key(lines[index])))
            {
                continue;
            }

            for (int other_index = 0; other_index < line_count; ++other_index)
            {
                if (other_index == index || !line_is_newer_than(lines[index], lines[other_index], index, other_index))
                {
                    continue;
                }

                if (!spread_sources[index] && source_matches_target(lines[index], lines[other_index]))
                {
                    spread_sources[index] = true;
                }
                if (!spread_targets[index] && same_target(lines[index], lines[other_index]))
                {
                    spread_targets[index] = true;
                }
                if (spread_sources[index] && spread_targets[index])
                {
                    break;
                }
            }
        }
    }

    void Renderer::apply_target_spread(const LineState& line, float& target_x, float& target_y) const
    {
        std::uint32_t seed = line.uid ? line.uid : (line.source_id ^ (line.target_id << 1));
        seed ^= line.source_id * 0x9e3779b9U;
        seed ^= line.target_id * 0x85ebca6bU;
        seed ^= line.source_index * 0xc2b2ae35U;
        seed ^= line.target_index * 0x27d4eb2fU;
        const std::uint32_t hash = mix_u32(seed);
        const float angle        = (static_cast<float>(hash & 0xFFFFU) / 65535.0f) * kTwoPi;
        const float radius       = 0.10f + (static_cast<float>((hash >> 16) & 0xFFU) / 255.0f) * 0.12f;
        target_x += std::cos(angle) * radius;
        target_y += std::sin(angle) * radius;
    }

    void Renderer::apply_source_spread(const LineState& line, float& source_x, float& source_y) const
    {
        std::uint32_t seed = line.uid ? line.uid : (line.source_id ^ (line.target_id << 1));
        seed ^= line.source_id * 0x165667b1U;
        seed ^= line.target_id * 0xd3a2646cU;
        seed ^= line.source_index * 0xfd7046c5U;
        seed ^= line.target_index * 0xb55a4f09U;
        const std::uint32_t hash = mix_u32(seed);
        const float angle        = (static_cast<float>(hash & 0xFFFFU) / 65535.0f) * kTwoPi;
        const float radius       = 0.10f + (static_cast<float>((hash >> 16) & 0xFFU) / 255.0f) * 0.12f;
        source_x += std::cos(angle) * radius;
        source_y += std::sin(angle) * radius;
    }

    float Renderer::model_adjusted_height(float manual_offset, float model_size, float model_scale, bool short_anchor, bool floating_anchor, bool is_npc) const
    {
        if (!is_npc)
        {
            return manual_offset;
        }

        if (floating_anchor)
        {
            return manual_offset - 1.05f;
        }

        if (short_anchor)
        {
            return manual_offset + 0.65f;
        }

        if (model_size <= 0.0f)
        {
            return manual_offset;
        }

        const float scale          = model_scale > 0.0f ? model_scale : 1.0f;
        const float effective_size = model_size * scale;
        if (effective_size < 1.15f)
        {
            const float auto_lower = std::fmin((1.15f - effective_size) * 1.25f, 0.65f);
            return manual_offset + auto_lower;
        }

        if (effective_size <= 1.2f)
        {
            return manual_offset;
        }

        // In this coordinate mapping, more negative z offsets raise the screen anchor.
        const float auto_raise = std::fmin((effective_size - 1.2f) * 0.45f, 1.15f);
        return manual_offset - auto_raise;
    }

    int Renderer::anchor_bone_for_entity(DWORD race, DWORD model, bool is_npc) const
    {
        // Auto mode uses the visually verified torso bone for each humanoid
        // skeleton. Mithra (race 7) maps bone 21 near the head; bone 39 is the
        // centered upper-chest equivalent. A few unique trust models have the
        // same visual mismatch despite reporting other race data.
        const bool model_uses_bone_39 =
            (race == 2 && model == 3033) || // Najelith
            (race == 0 && model == 3041) || // Cid
            (race == 2 && model == 3071);   // Margret
        if (is_npc && dynamic_bone_ == 21 && (race == 7 || model_uses_bone_39))
        {
            return 39;
        }
        return dynamic_bone_;
    }

    bool Renderer::resolve_live_anchor(bool is_npc, DWORD index, int bone, float& lua_x, float& lua_y, float& lua_z, bool& uses_height_offset)
    {
        if (index == 0 || index >= kMaxEntityIndex)
        {
            return false;
        }

        const std::uintptr_t actor = game_.actor_pointer(index);
        if (actor == 0 || !is_readable_range(actor, 0x700))
        {
            return false;
        }

        float actor_x = 0.0f;
        float actor_y = 0.0f;
        float actor_z = 0.0f;
        if (!read_actor_root(actor, actor_x, actor_y, actor_z))
        {
            return false;
        }

        if (is_npc && bone >= 0)
        {
            float bone_x = 0.0f;
            float bone_y = 0.0f;
            float bone_z = 0.0f;
            if (read_bone_anchor(actor, bone, bone_x, bone_y, bone_z, nullptr, 0))
            {
                lua_x              = bone_x;
                lua_y              = bone_y;
                lua_z              = bone_z;
                uses_height_offset = false;
                return true;
            }
        }

        lua_x              = actor_x;
        lua_y              = actor_y;
        lua_z              = actor_z;
        uses_height_offset = true;
        return true;
    }

    bool Renderer::resolve_live_anchor_cached(bool is_npc, DWORD index, int bone, float height_offset, float& lua_x, float& lua_y, float& lua_z)
    {
        if (index == 0 || index >= kMaxEntityIndex)
        {
            return false;
        }

        for (int i = 0; i < anchor_cache_count_; ++i)
        {
            const AnchorCacheEntry& entry = anchor_cache_[i];
            if (entry.index != index || entry.bone != bone || entry.is_npc != is_npc)
            {
                continue;
            }

            if (entry.resolved)
            {
                lua_x = entry.x;
                lua_y = entry.y;
                lua_z = entry.z + (entry.uses_height_offset ? height_offset : 0.0f);
            }
            return entry.resolved;
        }

        float resolved_x        = lua_x;
        float resolved_y        = lua_y;
        float resolved_z        = lua_z - height_offset;
        bool uses_height_offset = true;
        const bool resolved     = resolve_live_anchor(is_npc, index, bone, resolved_x, resolved_y, resolved_z, uses_height_offset);
        if (anchor_cache_count_ < max_anchor_cache_entries_)
        {
            AnchorCacheEntry& entry  = anchor_cache_[anchor_cache_count_++];
            entry.index              = index;
            entry.bone               = bone;
            entry.is_npc             = is_npc;
            entry.resolved           = resolved;
            entry.uses_height_offset = uses_height_offset;
            entry.x                  = resolved_x;
            entry.y                  = resolved_y;
            entry.z                  = resolved_z;
        }

        if (resolved)
        {
            lua_x = resolved_x;
            lua_y = resolved_y;
            lua_z = resolved_z + (uses_height_offset ? height_offset : 0.0f);
        }
        return resolved;
    }

    void Renderer::run_boneprobe(const LineState& line)
    {
        struct Side
        {
            const char* label;
            DWORD id;
            DWORD index;
            DWORD race;
            DWORD model;
            bool is_npc;
            float x;
            float y;
            float z;
        };

        const Side sides[] = {
            {"source", line.source_id, line.source_index, line.source_race, line.source_model, line.source_is_npc, line.source_x, line.source_y, line.source_z},
            {"target", line.target_id, line.target_index, line.target_race, line.target_model, line.target_is_npc, line.target_x, line.target_y, line.target_z},
        };

        for (const Side& side : sides)
        {
            char message[1024] {};
            const std::uintptr_t actor = game_.actor_pointer(side.index);
            float root_x = 0.0f;
            float root_y = 0.0f;
            float root_z = 0.0f;
            const bool root_ok = actor != 0 && is_readable_range(actor, 0x700) && read_actor_root(actor, root_x, root_y, root_z);
            std::snprintf(message, sizeof(message), "boneprobe %s id=%lu index=%lu race=%lu model=%lu npc=%s snapshot=(%.3f %.3f %.3f) actor=%08lx root_ok=%s root=(%.3f %.3f %.3f)",
                side.label, static_cast<unsigned long>(side.id), static_cast<unsigned long>(side.index), static_cast<unsigned long>(side.race), static_cast<unsigned long>(side.model),
                side.is_npc ? "true" : "false", side.x, side.y, side.z, static_cast<unsigned long>(actor), root_ok ? "true" : "false", root_x, root_y, root_z);
            log(message);

            if (!root_ok)
            {
                continue;
            }

            const int bones[] = {anchor_bone_for_entity(side.race, side.model, side.is_npc), 2, 21, 39};
            for (int bone : bones)
            {
                float bone_x = 0.0f;
                float bone_y = 0.0f;
                float bone_z = 0.0f;
                char detail[256] {};
                const bool bone_ok = read_bone_anchor(actor, bone, bone_x, bone_y, bone_z, detail, sizeof(detail));
                std::snprintf(message, sizeof(message), "boneprobe %s bone=%d ok=%s anchor=(%.3f %.3f %.3f) %s", side.label, bone, bone_ok ? "true" : "false", bone_x, bone_y, bone_z, detail);
                log(message);
            }
        }
    }

    Renderer::ActiveLine* Renderer::find_active_line(unsigned long long key)
    {
        for (int i = 0; i < active_line_count_; ++i)
        {
            if (active_lines_[i].key == key)
            {
                return &active_lines_[i];
            }
        }

        return nullptr;
    }

    Renderer::ActiveLine* Renderer::allocate_active_line(unsigned long long key, DWORD now_ms)
    {
        if (active_line_count_ < max_active_lines_)
        {
            ActiveLine& active  = active_lines_[active_line_count_++];
            active.key          = key;
            active.start_ms     = now_ms;
            active.last_seen_ms = now_ms;
            return &active;
        }

        int oldest = 0;
        for (int i = 1; i < active_line_count_; ++i)
        {
            if (active_lines_[i].last_seen_ms < active_lines_[oldest].last_seen_ms)
            {
                oldest = i;
            }
        }

        ActiveLine& active  = active_lines_[oldest];
        active.key          = key;
        active.start_ms     = now_ms;
        active.last_seen_ms = now_ms;
        return &active;
    }

    Renderer::ActiveRing* Renderer::find_active_ring(unsigned long long key)
    {
        for (int i = 0; i < active_ring_count_; ++i)
        {
            if (active_rings_[i].key == key)
            {
                return &active_rings_[i];
            }
        }

        return nullptr;
    }

    Renderer::ActiveRing* Renderer::allocate_active_ring(unsigned long long key, DWORD now_ms)
    {
        if (active_ring_count_ < max_active_rings_)
        {
            ActiveRing& active  = active_rings_[active_ring_count_++];
            active.key          = key;
            active.start_ms     = now_ms;
            active.last_seen_ms = now_ms;
            return &active;
        }

        int oldest = 0;
        for (int i = 1; i < active_ring_count_; ++i)
        {
            if (active_rings_[i].last_seen_ms < active_rings_[oldest].last_seen_ms)
            {
                oldest = i;
            }
        }

        ActiveRing& active  = active_rings_[oldest];
        active.key          = key;
        active.start_ms     = now_ms;
        active.last_seen_ms = now_ms;
        return &active;
    }

    void Renderer::prune_active_lines(DWORD now_ms)
    {
        int write = 0;
        for (int read = 0; read < active_line_count_; ++read)
        {
            if (now_ms - active_lines_[read].last_seen_ms <= 5000)
            {
                if (write != read)
                {
                    active_lines_[write] = active_lines_[read];
                }
                ++write;
            }
        }

        active_line_count_ = write;
    }

    void Renderer::prune_active_rings(DWORD now_ms)
    {
        int write = 0;
        for (int read = 0; read < active_ring_count_; ++read)
        {
            if (now_ms - active_rings_[read].last_seen_ms <= 5000)
            {
                if (write != read)
                {
                    active_rings_[write] = active_rings_[read];
                }
                ++write;
            }
        }

        active_ring_count_ = write;
    }

    float Renderer::directional_head_fade(float unit_x, float unit_y, float direction_x, float direction_y, float target_absorb) const
    {
        const float dot         = unit_x * direction_x + unit_y * direction_y;
        const float target_side = std::fmax(0.0f, std::fmin((dot + 1.0f) * 0.5f, 1.0f));
        const float front_fade  = 1.0f - target_absorb * (0.30f + target_side * 0.68f);
        return std::fmax(0.02f, std::fmin(front_fade, 1.0f));
    }

    void Renderer::draw_head_marker(float center_x, float center_y, float radius, DWORD core_color, DWORD shine_color, float direction_x, float direction_y, float landing_t, float base_alpha)
    {
        DrawVertex vertices[head_marker_slices_ * 9] {};
        int vertex_count         = 0;
        const DWORD outer_color  = scale_alpha(core_color, 0.86f * base_alpha);
        const DWORD inner_color  = scale_alpha(core_color, 1.16f * base_alpha);
        const DWORD hot_color    = scale_alpha(tint_white_color(shine_color, 0.35f), 1.20f * base_alpha);
        const float outer_radius = radius * 1.16f;
        const float inner_radius = radius * 0.72f;
        const float hot_radius   = radius * 0.38f;
        const float target_absorb = landing_t * landing_t * (3.0f - 2.0f * landing_t);
        const float center_fade   = directional_head_fade(0.0f, 0.0f, direction_x, direction_y, target_absorb);
        const DWORD outer_center_color = scale_alpha(outer_color, center_fade);
        const DWORD inner_center_color = scale_alpha(inner_color, center_fade);
        const DWORD hot_center_color   = scale_alpha(hot_color, center_fade);
        float outer_x[head_marker_slices_ + 1] {};
        float outer_y[head_marker_slices_ + 1] {};
        float inner_x[head_marker_slices_ + 1] {};
        float inner_y[head_marker_slices_ + 1] {};
        float hot_x[head_marker_slices_ + 1] {};
        float hot_y[head_marker_slices_ + 1] {};
        DWORD outer_edge_color[head_marker_slices_ + 1] {};
        DWORD inner_edge_color[head_marker_slices_ + 1] {};
        DWORD hot_edge_color[head_marker_slices_ + 1] {};

        for (int i = 0; i <= head_marker_slices_; ++i)
        {
            const float unit_x  = head_unit_x_[i];
            const float unit_y  = head_unit_y_[i];
            const float fade    = directional_head_fade(unit_x, unit_y, direction_x, direction_y, target_absorb);
            outer_x[i]          = center_x + unit_x * outer_radius;
            outer_y[i]          = center_y + unit_y * outer_radius;
            inner_x[i]          = center_x + unit_x * inner_radius;
            inner_y[i]          = center_y + unit_y * inner_radius;
            hot_x[i]            = center_x + unit_x * hot_radius;
            hot_y[i]            = center_y + unit_y * hot_radius;
            outer_edge_color[i] = scale_alpha(outer_color, 0.76f * fade);
            inner_edge_color[i] = scale_alpha(inner_color, fade);
            hot_edge_color[i]   = scale_alpha(hot_color, fade);
        }

        for (int i = 0; i < head_marker_slices_; ++i)
        {
            vertices[vertex_count++] = DrawVertex {center_x, center_y, 0.0f, 1.0f, outer_center_color};
            vertices[vertex_count++] = DrawVertex {outer_x[i], outer_y[i], 0.0f, 1.0f, outer_edge_color[i]};
            vertices[vertex_count++] = DrawVertex {outer_x[i + 1], outer_y[i + 1], 0.0f, 1.0f, outer_edge_color[i + 1]};

            vertices[vertex_count++] = DrawVertex {center_x, center_y, 0.0f, 1.0f, inner_center_color};
            vertices[vertex_count++] = DrawVertex {inner_x[i], inner_y[i], 0.0f, 1.0f, inner_edge_color[i]};
            vertices[vertex_count++] = DrawVertex {inner_x[i + 1], inner_y[i + 1], 0.0f, 1.0f, inner_edge_color[i + 1]};

            vertices[vertex_count++] = DrawVertex {center_x, center_y, 0.0f, 1.0f, hot_center_color};
            vertices[vertex_count++] = DrawVertex {hot_x[i], hot_y[i], 0.0f, 1.0f, hot_edge_color[i]};
            vertices[vertex_count++] = DrawVertex {hot_x[i + 1], hot_y[i + 1], 0.0f, 1.0f, hot_edge_color[i + 1]};
        }

        draw_vertices(D3DPT_TRIANGLELIST, vertex_count / 3, vertices, sizeof(DrawVertex));
    }

    void Renderer::draw_round_line_cap(float center_x, float center_y, float direction_x, float direction_y, float radius, DWORD color)
    {
        DrawVertex vertices[round_cap_slices_ * 3] {};
        int vertex_count   = 0;
        const float length = std::sqrt(direction_x * direction_x + direction_y * direction_y);
        if (radius <= 0.1f || length <= 0.001f)
        {
            return;
        }

        const float unit_direction_x = direction_x / length;
        const float unit_direction_y = direction_y / length;

        for (int i = 0; i < round_cap_slices_; ++i)
        {
            const float x0 = unit_direction_x * cap_unit_x_[i] - unit_direction_y * cap_unit_y_[i];
            const float y0 = unit_direction_y * cap_unit_x_[i] + unit_direction_x * cap_unit_y_[i];
            const float x1 = unit_direction_x * cap_unit_x_[i + 1] - unit_direction_y * cap_unit_y_[i + 1];
            const float y1 = unit_direction_y * cap_unit_x_[i + 1] + unit_direction_x * cap_unit_y_[i + 1];
            vertices[vertex_count++] = DrawVertex {center_x, center_y, 0.0f, 1.0f, color};
            vertices[vertex_count++] = DrawVertex {center_x + x0 * radius, center_y + y0 * radius, 0.0f, 1.0f, color};
            vertices[vertex_count++] = DrawVertex {center_x + x1 * radius, center_y + y1 * radius, 0.0f, 1.0f, color};
        }

        draw_vertices(D3DPT_TRIANGLELIST, vertex_count / 3, vertices, sizeof(DrawVertex));
    }

    bool Renderer::refresh_projection_matrices(void)
    {
        if (!d3d_device_)
        {
            return false;
        }

        if (FAILED(d3d_device_->GetTransform(D3DTS_VIEW, &cached_view_)) || FAILED(d3d_device_->GetTransform(D3DTS_PROJECTION, &cached_projection_)))
        {
            projection_matrices_valid_ = false;
            return false;
        }

        for (int row = 0; row < 4; ++row)
        {
            for (int column = 0; column < 4; ++column)
            {
                cached_view_projection_.m[row][column] =
                    cached_view_.m[row][0] * cached_projection_.m[0][column] + cached_view_.m[row][1] * cached_projection_.m[1][column] + cached_view_.m[row][2] * cached_projection_.m[2][column] + cached_view_.m[row][3] * cached_projection_.m[3][column];
            }
        }

        projection_matrices_valid_ = true;
        return true;
    }

    bool Renderer::live_world_to_screen(float lua_x, float lua_y, float lua_z, const D3DVIEWPORT8& viewport, float& screen_x, float& screen_y) const
    {
        if (!projection_matrices_valid_)
        {
            return false;
        }

        const D3DMATRIX& view_projection = cached_view_projection_;

        // FFXI positions use x/y as ground-plane coordinates and z as height.
        // Direct3D's model translations map those to x/z ground-plane and y height.
        const float world_x = lua_x;
        const float world_y = lua_z;
        const float world_z = lua_y;

        const float clip_x = world_x * view_projection.m[0][0] + world_y * view_projection.m[1][0] + world_z * view_projection.m[2][0] + view_projection.m[3][0];
        const float clip_y = world_x * view_projection.m[0][1] + world_y * view_projection.m[1][1] + world_z * view_projection.m[2][1] + view_projection.m[3][1];
        const float clip_w = world_x * view_projection.m[0][3] + world_y * view_projection.m[1][3] + world_z * view_projection.m[2][3] + view_projection.m[3][3];

        if (std::fabs(clip_w) <= 0.0001f)
        {
            return false;
        }

        const float ndc_x = clip_x / clip_w;
        const float ndc_y = clip_y / clip_w;
        if (clip_w < 0.0f || ndc_x < -4.0f || ndc_x > 4.0f || ndc_y < -4.0f || ndc_y > 4.0f)
        {
            return false;
        }

        screen_x = static_cast<float>(viewport.X) + (ndc_x + 1.0f) * static_cast<float>(viewport.Width) * 0.5f;
        screen_y = static_cast<float>(viewport.Y) + (1.0f - ndc_y) * static_cast<float>(viewport.Height) * 0.5f;
        return true;
    }

    void Renderer::append_segment_quad_with_normal(DrawVertex* vertices, int& vertex_count, float x1, float y1, float x2, float y2, float normal_x, float normal_y, float thickness, DWORD color)
    {
        const float nx = normal_x * thickness * 0.5f;
        const float ny = normal_y * thickness * 0.5f;

        const DrawVertex a {x1 + nx, y1 + ny, 0.0f, 1.0f, color};
        const DrawVertex b {x1 - nx, y1 - ny, 0.0f, 1.0f, color};
        const DrawVertex c {x2 + nx, y2 + ny, 0.0f, 1.0f, color};
        const DrawVertex d {x2 - nx, y2 - ny, 0.0f, 1.0f, color};

        vertices[vertex_count++] = a;
        vertices[vertex_count++] = b;
        vertices[vertex_count++] = c;
        vertices[vertex_count++] = c;
        vertices[vertex_count++] = b;
        vertices[vertex_count++] = d;
    }

    void Renderer::append_segment_quad_clipped_to_circle(DrawVertex* vertices, int& vertex_count, float x1, float y1, float x2, float y2, float segment_dx, float segment_dy, float segment_length, float segment_length_sq, float normal_x, float normal_y, float thickness, DWORD color, float circle_x, float circle_y, float radius)
    {
        if (radius <= 0.0f)
        {
            append_segment_quad_with_normal(vertices, vertex_count, x1, y1, x2, y2, normal_x, normal_y, thickness, color);
            return;
        }

        const float sx                = x1 - circle_x;
        const float sy                = y1 - circle_y;
        const float ex                = x2 - circle_x;
        const float ey                = y2 - circle_y;
        const float start_distance_sq = sx * sx + sy * sy;
        const float end_distance_sq   = ex * ex + ey * ey;
        const float radius_sq         = radius * radius;

        if (start_distance_sq <= radius_sq && end_distance_sq <= radius_sq)
        {
            return;
        }

        if (start_distance_sq > radius_sq && end_distance_sq > radius_sq)
        {
            append_segment_quad_with_normal(vertices, vertex_count, x1, y1, x2, y2, normal_x, normal_y, thickness, color);
            return;
        }

        const float a            = segment_length_sq;
        const float b            = 2.0f * (sx * segment_dx + sy * segment_dy);
        const float c            = start_distance_sq - radius_sq;
        const float discriminant = b * b - 4.0f * a * c;
        if (a <= 0.0001f || discriminant < 0.0f)
        {
            return;
        }

        const float root = std::sqrt(discriminant);
        const float t0   = (-b - root) / (2.0f * a);
        const float t1   = (-b + root) / (2.0f * a);
        float t          = -1.0f;
        if (t0 >= 0.0f && t0 <= 1.0f)
        {
            t = t0;
        }
        if (t1 >= 0.0f && t1 <= 1.0f && (t < 0.0f || t1 < t))
        {
            t = t1;
        }
        if (t < 0.0f)
        {
            return;
        }

        const float ix = x1 + segment_dx * t;
        const float iy = y1 + segment_dy * t;
        if (start_distance_sq > radius_sq)
        {
            if (segment_length * t > 0.01f)
            {
                append_segment_quad_with_normal(vertices, vertex_count, x1, y1, ix, iy, normal_x, normal_y, thickness, color);
            }
        }
        else
        {
            if (segment_length * (1.0f - t) > 0.01f)
            {
                append_segment_quad_with_normal(vertices, vertex_count, ix, iy, x2, y2, normal_x, normal_y, thickness, color);
            }
        }
    }

    bool Renderer::begin_draw_state(void)
    {
        if (draw_state_active_)
        {
            return true;
        }

        if (!d3d_device_)
        {
            return false;
        }

        saved_texture_       = nullptr;
        saved_stream_        = nullptr;
        saved_stream_stride_ = 0;

        const HRESULT shader_result   = d3d_device_->GetVertexShader(&saved_shader_);
        const HRESULT alpha_result    = d3d_device_->GetRenderState(D3DRS_ALPHABLENDENABLE, &saved_alpha_);
        const HRESULT src_result      = d3d_device_->GetRenderState(D3DRS_SRCBLEND, &saved_src_);
        const HRESULT dest_result     = d3d_device_->GetRenderState(D3DRS_DESTBLEND, &saved_dest_);
        const HRESULT z_result        = d3d_device_->GetRenderState(D3DRS_ZENABLE, &saved_z_);
        const HRESULT lighting_result = d3d_device_->GetRenderState(D3DRS_LIGHTING, &saved_lighting_);
        const HRESULT cull_result     = d3d_device_->GetRenderState(D3DRS_CULLMODE, &saved_cull_);
        const HRESULT texture_result  = d3d_device_->GetTexture(0, &saved_texture_);
        const HRESULT stream_result   = d3d_device_->GetStreamSource(0, &saved_stream_, &saved_stream_stride_);

        const bool captured = SUCCEEDED(shader_result) && SUCCEEDED(alpha_result) && SUCCEEDED(src_result) && SUCCEEDED(dest_result) && SUCCEEDED(z_result) && SUCCEEDED(lighting_result) && SUCCEEDED(cull_result) && SUCCEEDED(texture_result) && SUCCEEDED(stream_result);
        if (!captured)
        {
            if (saved_texture_)
            {
                saved_texture_->Release();
                saved_texture_ = nullptr;
            }
            if (saved_stream_)
            {
                saved_stream_->Release();
                saved_stream_ = nullptr;
            }
            return false;
        }

        // DrawPrimitiveUP clears stream zero, so stream state must be restored
        // alongside the render states that TargetLines changes explicitly.
        draw_state_active_         = true;
        const HRESULT texture_set  = d3d_device_->SetTexture(0, nullptr);
        const HRESULT alpha_set    = d3d_device_->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        const HRESULT src_set      = d3d_device_->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        const HRESULT dest_set     = d3d_device_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        const HRESULT z_set        = d3d_device_->SetRenderState(D3DRS_ZENABLE, FALSE);
        const HRESULT lighting_set = d3d_device_->SetRenderState(D3DRS_LIGHTING, FALSE);
        const HRESULT cull_set     = d3d_device_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        const HRESULT shader_set   = d3d_device_->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);

        const bool configured = SUCCEEDED(texture_set) && SUCCEEDED(alpha_set) && SUCCEEDED(src_set) && SUCCEEDED(dest_set) && SUCCEEDED(z_set) && SUCCEEDED(lighting_set) && SUCCEEDED(cull_set) && SUCCEEDED(shader_set);
        if (!configured)
        {
            end_draw_state();
            return false;
        }
        return true;
    }

    void Renderer::end_draw_state(void)
    {
        if (!draw_state_active_)
        {
            return;
        }

        if (d3d_device_)
        {
            d3d_device_->SetTexture(0, saved_texture_);
            d3d_device_->SetRenderState(D3DRS_ALPHABLENDENABLE, saved_alpha_);
            d3d_device_->SetRenderState(D3DRS_SRCBLEND, saved_src_);
            d3d_device_->SetRenderState(D3DRS_DESTBLEND, saved_dest_);
            d3d_device_->SetRenderState(D3DRS_ZENABLE, saved_z_);
            d3d_device_->SetRenderState(D3DRS_LIGHTING, saved_lighting_);
            d3d_device_->SetRenderState(D3DRS_CULLMODE, saved_cull_);
            d3d_device_->SetVertexShader(saved_shader_);
            d3d_device_->SetStreamSource(0, saved_stream_, saved_stream_stride_);
        }
        if (saved_texture_)
        {
            saved_texture_->Release();
            saved_texture_ = nullptr;
        }
        if (saved_stream_)
        {
            saved_stream_->Release();
            saved_stream_ = nullptr;
        }
        saved_stream_stride_ = 0;
        draw_state_active_   = false;
    }

    void Renderer::begin_line_batch(void)
    {
        line_batch_vertex_count_ = 0;
        line_batch_active_       = true;
    }

    void Renderer::flush_line_batch(void)
    {
        if (line_batch_vertex_count_ <= 0)
        {
            return;
        }

        submit_vertices(D3DPT_TRIANGLELIST, static_cast<UINT>(line_batch_vertex_count_ / 3), line_batch_vertices_, sizeof(DrawVertex));
        line_batch_vertex_count_ = 0;
    }

    void Renderer::end_line_batch(void)
    {
        flush_line_batch();
        line_batch_active_ = false;
    }

    void Renderer::draw_vertices(D3DPRIMITIVETYPE primitive_type, UINT primitive_count, const DrawVertex* vertices, UINT stride)
    {
        if (line_batch_active_ && line_batch_vertices_ && primitive_type == D3DPT_TRIANGLELIST && stride == sizeof(DrawVertex))
        {
            const UINT vertex_count = primitive_count * 3;
            if (vertex_count > static_cast<UINT>(max_line_batch_vertices_))
            {
                flush_line_batch();
                submit_vertices(primitive_type, primitive_count, vertices, stride);
                return;
            }

            if (line_batch_vertex_count_ + static_cast<int>(vertex_count) > max_line_batch_vertices_)
            {
                flush_line_batch();
            }
            std::memcpy(line_batch_vertices_ + line_batch_vertex_count_, vertices, static_cast<std::size_t>(vertex_count) * sizeof(DrawVertex));
            line_batch_vertex_count_ += static_cast<int>(vertex_count);
            return;
        }

        submit_vertices(primitive_type, primitive_count, vertices, stride);
    }

    void Renderer::submit_vertices(D3DPRIMITIVETYPE primitive_type, UINT primitive_count, const DrawVertex* vertices, UINT stride)
    {
        const bool owns_draw_state = !draw_state_active_;
        if (owns_draw_state && !begin_draw_state())
        {
            return;
        }

        d3d_device_->DrawPrimitiveUP(primitive_type, primitive_count, vertices, stride);

        if (owns_draw_state)
        {
            end_draw_state();
        }
    }
} // namespace targetlines
