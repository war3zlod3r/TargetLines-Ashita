// TargetLines for Ashita v4 - shared state structures.
//
// These structures are the contract between the action tracker (which decides
// what should be drawn) and the renderer (which draws it). The Windower v2
// addon serialised this data as JSON between Lua and a native module; inside a
// single Ashita plugin it is passed directly.
//
// Coordinate convention: positions use the same axis names as Windower's Lua
// mob tables and Ashita's IEntity getters. x and y are the ground plane, z is
// height. The renderer maps these onto Direct3D's x/z ground plane and y height
// when projecting to the screen.

#ifndef TARGETLINES_STATE_HPP_INCLUDED
#define TARGETLINES_STATE_HPP_INCLUDED

#include <cstdint>

namespace targetlines
{
    constexpr int kMaxLines       = 16;
    constexpr int kMaxRings       = 8;
    constexpr int kMaxRingTargets = 32;
    constexpr uint32_t kMaxEntityIndex = 0x900;

    enum class RegularMode : int
    {
        First  = 0, // Draw a regular attack once per source/target pair.
        Repeat = 1, // Draw again after the pair cooldown elapses.
        Off    = 2, // Never draw regular attacks.
    };

    enum class AoeMode : int
    {
        Off   = 0,
        Fan   = 1,
        Ring1 = 2, // Expanding ring with orbiting (comet) target indicators.
        Ring2 = 3, // Expanding ring with contracting target indicators.
    };

    enum class RenderMode : int
    {
        EndScene  = 0, // Draw from IPlugin::Direct3DEndScene (default, no code patches).
        SceneHook = 1, // Draw inside FFXiMain!draw_scene via the shared SceneHook patch.
    };

    // A snapshot of an entity at the moment an action was observed.
    struct EntityPoint
    {
        uint32_t id    = 0;
        uint32_t index = 0;
        char name[32] {};
        float x = 0.0f; // Ground plane (IEntity::GetLocalPositionX)
        float y = 0.0f; // Ground plane (IEntity::GetLocalPositionY)
        float z = 0.0f; // Height       (IEntity::GetLocalPositionZ)
        uint8_t hpp  = 0;
        bool is_npc  = false;
        uint32_t race  = 0;
        uint32_t model = 0;
        float model_size  = 0.0f; // Hitbox size (Windower model_size)
        float model_scale = 1.0f; // Model scale  (Windower model_scale)
        float distance    = 0.0f; // Yalms from the local player
        uint32_t claim_id = 0;
        bool short_anchor    = false;
        bool floating_anchor = false;

        bool visible(void) const
        {
            return id != 0 && index != 0 && hpp > 0;
        }
    };

    struct LineState
    {
        uint32_t uid           = 0;
        uint32_t source_id     = 0;
        uint32_t target_id     = 0;
        uint32_t source_index  = 0;
        uint32_t target_index  = 0;
        uint32_t source_race   = 0;
        uint32_t target_race   = 0;
        uint32_t source_model  = 0;
        uint32_t target_model  = 0;
        float source_x         = 0.0f;
        float source_y         = 0.0f;
        float source_z         = 0.0f;
        float target_x         = 0.0f;
        float target_y         = 0.0f;
        float target_z         = 0.0f;
        float source_model_size  = 0.0f;
        float source_model_scale = 1.0f;
        float target_model_size  = 0.0f;
        float target_model_scale = 1.0f;
        bool source_short_anchor    = false;
        bool target_short_anchor    = false;
        bool source_floating_anchor = false;
        bool target_floating_anchor = false;
        bool source_is_npc          = false;
        bool target_is_npc          = false;
        float timeout  = 1.5f;
        uint32_t color = 0xEFFFFFFF;
    };

    struct RingTargetState
    {
        uint32_t id    = 0;
        uint32_t index = 0;
        uint32_t race  = 0;
        uint32_t model = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float model_size  = 0.0f;
        float model_scale = 1.0f;
        bool short_anchor    = false;
        bool floating_anchor = false;
        bool is_npc          = false;
    };

    struct RingState
    {
        uint32_t uid          = 0;
        uint32_t center_id    = 0;
        uint32_t center_index = 0;
        float center_x        = 0.0f;
        float center_y        = 0.0f;
        float center_z        = 0.0f;
        float center_model_size  = 0.0f;
        float center_model_scale = 1.0f;
        bool center_short_anchor    = false;
        bool center_floating_anchor = false;
        bool center_is_npc          = false;
        float radius    = 0.0f;
        float timeout   = 1.5f;
        uint32_t color  = 0xEFFFFFFF;
        int indicator_style = 1; // 1 = comet (Ring A), 10 = contracting (Ring B)
        int target_count    = 0;
        RingTargetState targets[kMaxRingTargets] {};
    };

    struct RenderSettings
    {
        float opacity       = 0.8f;   // 0..1
        float timeout       = 1.5f;   // seconds
        float width         = 1.0f;   // 0.5..2
        float glow          = 1.0f;   // 0.5..2
        float source_height = -0.75f; // -5..5
        float target_height = -1.35f; // -5..5
    };

    // Complete, immutable snapshot handed to the renderer.
    struct RenderState
    {
        RenderSettings settings {};
        bool boneprobe = false;
        int line_count = 0;
        LineState lines[kMaxLines] {};
        int ring_count = 0;
        RingState rings[kMaxRings] {};
    };

    inline void fill_ring_target(RingTargetState& out, const EntityPoint& point)
    {
        out.id              = point.id;
        out.index           = point.index;
        out.race            = point.race;
        out.model           = point.model;
        out.x               = point.x;
        out.y               = point.y;
        out.z               = point.z;
        out.model_size      = point.model_size;
        out.model_scale     = point.model_scale > 0.0f ? point.model_scale : 1.0f;
        out.short_anchor    = point.short_anchor;
        out.floating_anchor = point.floating_anchor;
        out.is_npc          = point.is_npc;
    }

    inline void fill_line(LineState& out, const EntityPoint& source, const EntityPoint& target)
    {
        out.source_id              = source.id;
        out.source_index           = source.index;
        out.source_race            = source.race;
        out.source_model           = source.model;
        out.source_x               = source.x;
        out.source_y               = source.y;
        out.source_z               = source.z;
        out.source_model_size      = source.model_size;
        out.source_model_scale     = source.model_scale > 0.0f ? source.model_scale : 1.0f;
        out.source_short_anchor    = source.short_anchor;
        out.source_floating_anchor = source.floating_anchor;
        out.source_is_npc          = source.is_npc;
        out.target_id              = target.id;
        out.target_index           = target.index;
        out.target_race            = target.race;
        out.target_model           = target.model;
        out.target_x               = target.x;
        out.target_y               = target.y;
        out.target_z               = target.z;
        out.target_model_size      = target.model_size;
        out.target_model_scale     = target.model_scale > 0.0f ? target.model_scale : 1.0f;
        out.target_short_anchor    = target.short_anchor;
        out.target_floating_anchor = target.floating_anchor;
        out.target_is_npc          = target.is_npc;
    }
} // namespace targetlines

#endif // TARGETLINES_STATE_HPP_INCLUDED
