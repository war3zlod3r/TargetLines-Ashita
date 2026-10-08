// TargetLines for Ashita v4 - action tracker.
//
// Port of the decision logic from the Windower TargetLines.lua addon: which
// actions produce lines or AoE rings, how they are classified and coloured,
// duplicate suppression for start/finish packet pairs, regular-attack memory,
// and the periodic rebuild of the render snapshot.

#ifndef TARGETLINES_TRACKER_HPP_INCLUDED
#define TARGETLINES_TRACKER_HPP_INCLUDED

#include "action_packet.hpp"
#include "game.hpp"
#include "logger.hpp"
#include "settings.hpp"
#include "state.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace targetlines
{
    class Tracker final
    {
    public:
        Tracker(Game& game, Settings& settings, Logger& logger);

        void handle_action(const ActionPacket& packet);

        // Forget every line, ring and memory table (zone change, /tl clear).
        void clear_all(void);
        // Regular attack memory (changes to the regular attack mode).
        void clear_seen_pairs(void);
        void clear_special_pairs(void);
        // Rings and fan lines (changes to the AoE style).
        void clear_aoe_visuals(void);
        // Mark the snapshot stale so the next update rebuilds it.
        void invalidate(void);

        // Rebuilds 'out' when the write interval has elapsed and something changed,
        // expired or periodic maintenance is due. Returns true when 'out' was written.
        bool update(double now, RenderState& out, bool boneprobe);

        bool has_probe_line(void) const;
        size_t visible_line_count(void) const;
        size_t nearby_count(void) const;

        bool write_inspect(const std::string& path, const char* reason);
        // Returns a non-empty message when a snapshot was written.
        std::string maybe_auto_inspect(double now, const std::string& path);

    private:
        struct RecentLine
        {
            uint32_t uid = 0;
            EntityPoint source {};
            EntityPoint target {};
            std::string kind;
            uint32_t color     = 0xEFFFFFFF;
            uint32_t action_id = 0;
            double created     = 0.0;
            float timeout      = 1.5f;
            bool claim         = false;
        };

        struct RecentRing
        {
            uint32_t uid = 0;
            EntityPoint center {};
            std::vector<EntityPoint> targets;
            float radius = 0.0f;
            std::string kind;
            uint32_t color      = 0xEFFFFFFF;
            int indicator_style = 1;
            double created      = 0.0;
            float timeout       = 1.5f;
        };

        struct Cast
        {
            double created  = 0.0;
            EntityPoint target {};
            bool has_target = false;
            std::string line_key;
        };

        struct AddOptions
        {
            bool always_draw             = false;
            bool special_cooldown        = false;
            bool ignore_special_cooldown = false;
            uint32_t action_id           = 0;
        };

        enum class Role
        {
            Player,
            Party,
            Pet,
            Enemy,
        };

        enum class CenterMode
        {
            Source,
            Target,
        };

        enum class CastFamily
        {
            None,
            Spell,
            Ability,
        };

        static bool is_special_category(uint32_t category);
        static bool is_aoe_category(uint32_t category);
        static bool is_aoe_action(uint32_t category, size_t target_count);
        static CastFamily cast_family(uint32_t category);
        static std::string cast_key(CastFamily family, uint32_t source_id, uint32_t action_id);
        static std::string pair_key(uint32_t source_id, uint32_t target_id);
        static uint32_t action_spell_id(uint32_t category, const ActionPacket& packet, const ActionTarget* target);
        static uint32_t scale_color_alpha(uint32_t color, float scale);

        bool is_caster_centered_spell(uint32_t spell_id) const;
        CenterMode aoe_center_mode(uint32_t category, uint32_t action_id, const EntityPoint& source, const EntityPoint* primary) const;

        std::pair<const char*, uint32_t> classify_line(const EntityPoint& source, const EntityPoint& target, const PartySet& party) const;
        Role line_source_role(const EntityPoint& source, const PartySet& party) const;
        bool line_allowed(const EntityPoint& source, const EntityPoint& target, const PartySet& party, bool special, bool fan, Role& role, const char*& reason) const;
        float role_opacity_scale(Role role) const;

        std::pair<bool, std::string> add_recent_line(const EntityPoint& source, const EntityPoint& target, const std::string& kind, uint32_t color, float timeout, const AddOptions& options);
        bool add_recent_ring(const EntityPoint& center, const std::vector<EntityPoint>& targets, const std::string& kind, uint32_t color, float timeout);

        void world_snapshot(double now, bool force);
        void collect_claim_lines(const PartySet& party, double now);
        void prune_seen_pairs_for_inactive_entities(const PartySet& party);
        void collect(double now, std::vector<const RecentLine*>& lines, std::vector<const RecentRing*>& rings);

        void action_debug_log(const char* event, const ActionPacket& packet, const EntityPoint& source, const EntityPoint* target, const ActionTarget* action_target, const char* reason);
        std::string describe(const EntityPoint& point) const;

        Game& game_;
        Settings& settings_;
        Logger& logger_;

        std::unordered_map<std::string, RecentLine> recent_lines_;
        std::unordered_map<std::string, RecentRing> recent_rings_;
        std::unordered_map<std::string, double> seen_pairs_;
        std::unordered_map<std::string, double> seen_special_pairs_;
        std::unordered_map<std::string, double> recent_spell_starts_;
        std::unordered_map<std::string, double> recent_spell_events_;
        std::unordered_map<std::string, double> recent_ability_starts_;
        std::unordered_map<std::string, Cast> recent_action_casts_;
        std::vector<RecentLine> claim_lines_;
        uint32_t line_sequence_ = 0;

        RecentLine probe_line_ {};
        bool has_probe_line_ = false;

        std::vector<EntityPoint> nearby_;
        std::unordered_set<uint32_t> active_ids_;
        double snapshot_time_  = -1.0;
        uint32_t snapshot_zone_ = 0;
        float snapshot_range_   = 0.0f;

        std::vector<RecentLine> last_lines_;

        double last_write_       = 0.0;
        double last_maintenance_ = 0.0;
        double next_expiration_  = 0.0;
        bool dirty_              = true;
        bool last_boneprobe_     = false;

        double auto_inspect_last_       = 0.0;
        uint32_t auto_inspect_zone_     = 0xFFFFFFFF;
        bool auto_inspect_zone_written_ = false;
    };
} // namespace targetlines

#endif // TARGETLINES_TRACKER_HPP_INCLUDED
