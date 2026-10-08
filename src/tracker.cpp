#include "tracker.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace targetlines
{
    namespace
    {
        constexpr double kDuplicateFinishWindow   = 5.0;  // seconds
        constexpr double kStateMaintenanceInterval = 0.25; // seconds
        constexpr double kMobSnapshotInterval      = 0.25; // seconds

        struct ColorSet
        {
            uint32_t player;
            uint32_t friendly;
            uint32_t enemy;
            uint32_t hostile_support;
            uint32_t npc;
            uint32_t claim_enemy;
        };

        constexpr ColorSet kDefaultColors {
            0xF040F0FF, // player: blue
            0xF088FF9A, // friendly: green
            0xF0FF4A55, // enemy: red
            0xF0FF22F0, // hostile_support: magenta
            0xE800E8FF, // npc
            0xD8FF4A55, // claim_enemy
        };

        constexpr ColorSet kColorBlindColors {
            0xF056B4E9, // sky blue
            0xF0F0E442, // yellow
            0xF0D55E00, // vermilion
            0xF0CC79A7, // reddish purple
            0xE856B4E9,
            0xD8D55E00,
        };

        float ground_distance(const EntityPoint& a, const EntityPoint& b)
        {
            const float dx = a.x - b.x;
            const float dy = a.y - b.y;
            return std::sqrt(dx * dx + dy * dy);
        }

        bool starts_with(const std::string& text, const char* prefix)
        {
            const size_t length = std::strlen(prefix);
            return text.size() >= length && text.compare(0, length, prefix) == 0;
        }

        bool ends_with(const std::string& text, const char* suffix)
        {
            const size_t length = std::strlen(suffix);
            return text.size() >= length && text.compare(text.size() - length, length, suffix) == 0;
        }

        // Matches the spell itself or a tiered version ("Fira", "Fira II"), but
        // not a different spell sharing the prefix ("Firaga").
        bool is_spell_family(const std::string& name, const char* base)
        {
            const size_t length = std::strlen(base);
            if (name.size() < length || name.compare(0, length, base) != 0)
            {
                return false;
            }
            return name.size() == length || name[length] == ' ';
        }

        void prune_expired(std::unordered_map<std::string, double>& table, double now, double window)
        {
            for (auto it = table.begin(); it != table.end();)
            {
                if (now - it->second > window)
                {
                    it = table.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }
    } // namespace

    Tracker::Tracker(Game& game, Settings& settings, Logger& logger)
        : game_(game)
        , settings_(settings)
        , logger_(logger)
    {}

    bool Tracker::is_special_category(uint32_t category)
    {
        switch (category)
        {
            case action_category::WeaponSkillFinish:
            case action_category::MagicFinish:
            case action_category::JobAbility:
            case action_category::WeaponSkillStart:
            case action_category::MagicStart:
            case action_category::MonsterAbility:
            case action_category::PetAbility:
            case action_category::JobAbilityAlt1:
            case action_category::JobAbilityAlt2:
                return true;
            default:
                return false;
        }
    }

    // Resolved (finish) packets carry every affected target, so only these
    // categories qualify for AoE detection.
    bool Tracker::is_aoe_category(uint32_t category)
    {
        switch (category)
        {
            case action_category::WeaponSkillFinish:
            case action_category::MagicFinish:
            case action_category::JobAbility:
            case action_category::MonsterAbility:
            case action_category::PetAbility:
            case action_category::JobAbilityAlt1:
            case action_category::JobAbilityAlt2:
                return true;
            default:
                return false;
        }
    }

    bool Tracker::is_aoe_action(uint32_t category, size_t target_count)
    {
        return target_count > 1 && is_aoe_category(category);
    }

    Tracker::CastFamily Tracker::cast_family(uint32_t category)
    {
        if (category == action_category::MagicFinish || category == action_category::MagicStart)
        {
            return CastFamily::Spell;
        }
        if (category == action_category::WeaponSkillFinish || category == action_category::WeaponSkillStart || category == action_category::MonsterAbility)
        {
            return CastFamily::Ability;
        }
        return CastFamily::None;
    }

    std::string Tracker::cast_key(CastFamily family, uint32_t source_id, uint32_t action_id)
    {
        char buffer[96] {};
        std::snprintf(buffer, sizeof(buffer), "%s:%u:%u", family == CastFamily::Spell ? "spell" : (family == CastFamily::Ability ? "ability" : "action"), source_id, action_id);
        return buffer;
    }

    std::string Tracker::pair_key(uint32_t source_id, uint32_t target_id)
    {
        char buffer[48] {};
        std::snprintf(buffer, sizeof(buffer), "%u>%u", source_id, target_id);
        return buffer;
    }

    uint32_t Tracker::action_spell_id(uint32_t category, const ActionPacket& packet, const ActionTarget* target)
    {
        const ActionEntry* first = (target != nullptr && !target->actions.empty()) ? &target->actions.front() : nullptr;
        if (category == action_category::WeaponSkillStart || category == action_category::MagicStart)
        {
            // Start packets store the spell/weapon skill id in the first action's param.
            if (first != nullptr && first->param != 0)
            {
                return first->param;
            }
            return packet.param;
        }

        if (packet.param != 0)
        {
            return packet.param;
        }
        return first != nullptr ? first->param : 0;
    }

    uint32_t Tracker::scale_color_alpha(uint32_t color, float scale)
    {
        scale               = Settings::clamp(scale, 0.0f, 2.0f);
        const uint32_t alpha = (color >> 24) & 0xFF;
        const uint32_t rgb   = color & 0x00FFFFFF;
        const float scaled   = Settings::clamp(static_cast<float>(alpha) * scale, 0.0f, 255.0f);
        return rgb | (static_cast<uint32_t>(scaled + 0.5f) << 24);
    }

    bool Tracker::is_caster_centered_spell(uint32_t spell_id) const
    {
        if (game_.spell_self_centered_area(spell_id))
        {
            return true;
        }

        const std::string name = game_.spell_name(spell_id);
        if (name.empty())
        {
            return false;
        }

        if (starts_with(name, "Protectra") || starts_with(name, "Shellra") || starts_with(name, "Indi-") || starts_with(name, "Boost-") || starts_with(name, "Gain-"))
        {
            return true;
        }

        // Bar-element/-status -ra spells (Barfira, Barsleepra, ...).
        if (starts_with(name, "Bar") && ends_with(name, "ra") && name.size() > 5)
        {
            return true;
        }

        // Elemental -ra spells are centered on the caster; -ga spells are not.
        static const char* const elemental_ra[] = {"Fira", "Blizzara", "Aera", "Stonera", "Thundara", "Watera"};
        for (const char* base : elemental_ra)
        {
            if (is_spell_family(name, base))
            {
                return true;
            }
        }
        return false;
    }

    Tracker::CenterMode Tracker::aoe_center_mode(uint32_t category, uint32_t action_id, const EntityPoint& source, const EntityPoint* primary) const
    {
        if (primary == nullptr || (source.id != 0 && primary->id != 0 && source.id == primary->id))
        {
            return CenterMode::Source;
        }

        switch (category)
        {
            case action_category::MagicFinish:
                return is_caster_centered_spell(action_id) ? CenterMode::Source : CenterMode::Target;
            case action_category::WeaponSkillFinish:
                return CenterMode::Target;
            case action_category::JobAbility:
            case action_category::MonsterAbility:
            case action_category::PetAbility:
            case action_category::JobAbilityAlt1:
            case action_category::JobAbilityAlt2:
            default:
                return CenterMode::Source;
        }
    }

    std::pair<const char*, uint32_t> Tracker::classify_line(const EntityPoint& source, const EntityPoint& target, const PartySet& party) const
    {
        const ColorSet& colors = settings_.color_blind_mode ? kColorBlindColors : kDefaultColors;
        const bool source_party = party.contains(source);
        const bool target_party = party.contains(target);

        if (source_party && target_party)
        {
            return {"friendly", colors.friendly};
        }
        if (source_party)
        {
            return {"player", colors.player};
        }
        if (source.is_npc && target_party)
        {
            return {"enemy", colors.enemy};
        }
        if (source.is_npc && target.is_npc)
        {
            return {"hostile_support", colors.hostile_support};
        }
        if (source.is_npc)
        {
            return {"npc", colors.npc};
        }
        return {"player", colors.player};
    }

    Tracker::Role Tracker::line_source_role(const EntityPoint& source, const PartySet& party) const
    {
        if (source.id != 0 && party.player_id != 0 && source.id == party.player_id)
        {
            return Role::Player;
        }
        if (party.is_pet(source))
        {
            return Role::Pet;
        }
        if (party.contains(source))
        {
            return Role::Party;
        }
        if (source.is_npc)
        {
            return Role::Enemy;
        }
        return Role::Player;
    }

    bool Tracker::line_allowed(const EntityPoint& source, const EntityPoint& target, const PartySet& party, bool special, bool fan, Role& role, const char*& reason) const
    {
        role = line_source_role(source, party);

        if (fan && settings_.aoe_mode == AoeMode::Off)
        {
            reason = "aoe_indicators_disabled";
            return false;
        }
        if (special && !settings_.show_special_lines)
        {
            reason = "special_lines_disabled";
            return false;
        }
        if (!settings_.show_other_party_lines && !party.contains(source) && !party.contains(target))
        {
            reason = "other_party_lines_disabled";
            return false;
        }

        switch (role)
        {
            case Role::Player:
                if (!settings_.show_player_lines)
                {
                    reason = "player_lines_disabled";
                    return false;
                }
                break;
            case Role::Party:
                if (!settings_.show_party_lines)
                {
                    reason = "party_lines_disabled";
                    return false;
                }
                break;
            case Role::Pet:
                if (!settings_.show_pet_lines)
                {
                    reason = "pet_lines_disabled";
                    return false;
                }
                break;
            case Role::Enemy:
                if (!settings_.show_enemy_lines)
                {
                    reason = "enemy_lines_disabled";
                    return false;
                }
                break;
        }

        reason = "allowed";
        return true;
    }

    float Tracker::role_opacity_scale(Role role) const
    {
        switch (role)
        {
            case Role::Party:
            case Role::Pet:
                return Settings::slider(settings_.ally_opacity_scale);
            case Role::Enemy:
                return Settings::slider(settings_.enemy_opacity_scale);
            case Role::Player:
            default:
                return Settings::slider(settings_.player_opacity_scale);
        }
    }

    std::pair<bool, std::string> Tracker::add_recent_line(const EntityPoint& source, const EntityPoint& target, const std::string& kind, uint32_t color, float timeout, const AddOptions& options)
    {
        if (!source.visible() || !target.visible() || source.id == target.id)
        {
            return {false, "invalid_or_same_entity"};
        }

        const std::string key_pair = pair_key(source.id, target.id);
        const double now           = Game::now();

        if (options.always_draw && options.special_cooldown && !options.ignore_special_cooldown)
        {
            const double cooldown = settings_.special_cooldown;
            if (cooldown > 0.0)
            {
                const auto seen = seen_special_pairs_.find(key_pair);
                if (seen != seen_special_pairs_.end() && now - seen->second < cooldown)
                {
                    return {false, "special_cooldown"};
                }
                seen_special_pairs_[key_pair] = now;
            }
        }

        if (!options.always_draw)
        {
            switch (settings_.regular_attack_mode)
            {
                case RegularMode::Off:
                    return {false, "regular_off"};
                case RegularMode::First:
                    if (seen_pairs_.count(key_pair) != 0)
                    {
                        return {false, "regular_seen"};
                    }
                    break;
                case RegularMode::Repeat:
                {
                    const auto seen = seen_pairs_.find(key_pair);
                    if (seen != seen_pairs_.end() && now - seen->second < settings_.pair_cooldown)
                    {
                        return {false, "regular_repeat_delay"};
                    }
                    break;
                }
            }
            seen_pairs_[key_pair] = now;
        }

        ++line_sequence_;
        std::string key = key_pair;
        if (options.always_draw)
        {
            key += '#';
            key += std::to_string(line_sequence_);
        }

        RecentLine line {};
        line.uid       = line_sequence_;
        line.source    = source;
        line.target    = target;
        line.kind      = kind.empty() ? "player" : kind;
        line.color     = color != 0 ? color : 0xEFFFFFFF;
        line.action_id = options.action_id;
        line.created   = now;
        line.timeout   = timeout > 0.0f ? timeout : settings_.effective_timeout();

        recent_lines_[key] = line;
        probe_line_        = line;
        has_probe_line_    = true;
        dirty_             = true;
        return {true, key};
    }

    bool Tracker::add_recent_ring(const EntityPoint& center, const std::vector<EntityPoint>& targets, const std::string& kind, uint32_t color, float timeout)
    {
        if (!center.visible() || targets.empty())
        {
            return false;
        }

        float radius = 0.0f;
        for (const EntityPoint& target : targets)
        {
            if (target.visible())
            {
                radius = std::max(radius, ground_distance(target, center));
            }
        }
        if (radius <= 0.1f)
        {
            return false;
        }

        ++line_sequence_;
        char key[64] {};
        std::snprintf(key, sizeof(key), "%u#ring#%u", center.id, line_sequence_);

        RecentRing ring {};
        ring.uid             = line_sequence_;
        ring.center          = center;
        ring.targets         = targets;
        ring.radius          = radius;
        ring.kind            = kind.empty() ? "aoe_ring" : kind;
        ring.color           = color != 0 ? color : 0xEFFFFFFF;
        ring.indicator_style = settings_.ring_indicator_style();
        ring.created         = Game::now();
        ring.timeout         = timeout > 0.0f ? timeout : settings_.effective_timeout();

        recent_rings_[key] = std::move(ring);
        dirty_             = true;
        return true;
    }

    void Tracker::handle_action(const ActionPacket& packet)
    {
        EntityPoint source {};
        if (!game_.entity_by_id(packet.actor_id, source))
        {
            return;
        }

        const PartySet party        = game_.party();
        const uint32_t category     = packet.category;
        const bool special          = is_special_category(category);
        const size_t target_count   = packet.targets.size();
        const double now            = Game::now();
        const ActionTarget* first   = packet.targets.empty() ? nullptr : &packet.targets.front();
        const uint32_t action_id    = action_spell_id(category, packet, first);
        const CastFamily family     = cast_family(category);

        EntityPoint primary {};
        bool has_primary = first != nullptr && game_.entity_by_id(first->id, primary);

        std::string active_cast_key;
        bool has_active_cast = false;
        if ((category == action_category::WeaponSkillStart || category == action_category::MagicStart) && family != CastFamily::None)
        {
            active_cast_key = cast_key(family, source.id, action_id);
            Cast cast {};
            cast.created    = now;
            cast.target     = primary;
            cast.has_target = has_primary;
            recent_action_casts_[active_cast_key] = cast;
            has_active_cast                       = true;
        }
        else if ((category == action_category::WeaponSkillFinish || category == action_category::MagicFinish || category == action_category::MonsterAbility) && family != CastFamily::None)
        {
            active_cast_key = cast_key(family, source.id, action_id);
            const auto it   = recent_action_casts_.find(active_cast_key);
            if (it != recent_action_casts_.end() && now - it->second.created <= kDuplicateFinishWindow)
            {
                has_active_cast = true;
                if (it->second.has_target)
                {
                    // The start packet's target is authoritative for where a
                    // target-centered AoE lands; refresh its position.
                    EntityPoint live {};
                    if (game_.resolve(it->second.target.id, it->second.target.index, live))
                    {
                        primary = live;
                    }
                    else
                    {
                        primary = it->second.target;
                    }
                    has_primary = true;
                }
            }
        }

        const bool resolved_aoe = is_aoe_action(category, target_count);
        if (resolved_aoe && has_active_cast)
        {
            // The resolved packet is authoritative. Replace the provisional start
            // line so fast casts cannot leave an overlapping primary-target line.
            const auto it = recent_action_casts_.find(active_cast_key);
            if (it != recent_action_casts_.end() && !it->second.line_key.empty())
            {
                recent_lines_.erase(it->second.line_key);
                dirty_ = true;
            }
        }

        std::vector<EntityPoint> ring_targets;
        std::string ring_kind;
        uint32_t ring_color  = 0;
        bool have_ring_color = false;
        EntityPoint ring_center = source;
        CenterMode center_mode  = CenterMode::Source;
        if (resolved_aoe)
        {
            center_mode = aoe_center_mode(category, action_id, source, has_primary ? &primary : nullptr);
            if (center_mode == CenterMode::Target && has_primary)
            {
                ring_center = primary;
            }
        }

        const AoeMode indicator_mode = settings_.aoe_mode;
        const bool ring_enabled      = settings_.ring_indicator_enabled();

        for (const ActionTarget& action_target : packet.targets)
        {
            EntityPoint target {};
            bool have_target = game_.entity_by_id(action_target.id, target);
            if (!have_target)
            {
                continue;
            }

            const std::string key_pair = pair_key(source.id, target.id);
            std::string spell_key;
            if (category == action_category::MagicFinish || category == action_category::MagicStart)
            {
                spell_key = key_pair + ':' + std::to_string(action_spell_id(category, packet, &action_target));
            }

            if (!resolved_aoe && !spell_key.empty())
            {
                const auto seen = recent_spell_events_.find(spell_key);
                if (seen != recent_spell_events_.end() && now - seen->second <= kDuplicateFinishWindow)
                {
                    // Single-target start/finish packets retain duplicate suppression.
                    action_debug_log("skip", packet, source, &target, &action_target, "repeat_spell_target");
                    have_target = false;
                }
            }
            else if (!resolved_aoe && (category == action_category::WeaponSkillFinish || category == action_category::MonsterAbility))
            {
                const auto seen = recent_ability_starts_.find(key_pair);
                if (seen != recent_ability_starts_.end() && now - seen->second <= kDuplicateFinishWindow)
                {
                    action_debug_log("skip", packet, source, &target, &action_target, "repeat_ability_target");
                    have_target = false;
                }
            }

            Role role = Role::Player;
            if (have_target)
            {
                const char* reason = nullptr;
                if (!line_allowed(source, target, party, special, resolved_aoe, role, reason))
                {
                    if (special || settings_.action_debug)
                    {
                        action_debug_log("skip", packet, source, &target, &action_target, reason);
                    }
                    have_target = false;
                }
            }

            if (!have_target)
            {
                continue;
            }

            auto classified = classify_line(source, target, party);
            std::string kind = classified.first;
            uint32_t color   = scale_color_alpha(classified.second, role_opacity_scale(role));
            if (special)
            {
                kind += "_special";
            }
            if (resolved_aoe)
            {
                kind += "_fan";
                color = scale_color_alpha(color, settings_.effective_aoe_opacity());
                if (ring_enabled)
                {
                    ring_targets.push_back(target);
                    if (ring_kind.empty())
                    {
                        ring_kind = kind + "_ring";
                    }
                    if (!have_ring_color)
                    {
                        ring_color      = color;
                        have_ring_color = true;
                    }
                }
            }

            bool drawn         = true;
            std::string reason = "ring_mode";
            if (!resolved_aoe || indicator_mode == AoeMode::Fan)
            {
                AddOptions options {};
                options.always_draw             = special;
                options.special_cooldown        = special;
                options.ignore_special_cooldown = resolved_aoe;
                options.action_id               = action_id;
                auto result = add_recent_line(source, target, kind, color, settings_.effective_timeout(), options);
                drawn       = result.first;
                reason      = result.second;
            }

            if (has_active_cast && drawn && (category == action_category::WeaponSkillStart || category == action_category::MagicStart))
            {
                const auto it = recent_action_casts_.find(active_cast_key);
                if (it != recent_action_casts_.end())
                {
                    it->second.line_key = reason;
                }
            }

            if (special || settings_.action_debug)
            {
                action_debug_log(drawn ? "draw" : "skip", packet, source, &target, &action_target, drawn ? kind.c_str() : reason.c_str());
            }

            if (category == action_category::MagicFinish)
            {
                recent_spell_starts_[key_pair] = now;
            }
            if (!spell_key.empty())
            {
                recent_spell_events_[spell_key] = now;
            }
            else if (category == action_category::WeaponSkillStart)
            {
                recent_ability_starts_[key_pair] = now;
            }
        }

        if (ring_enabled && !ring_targets.empty())
        {
            add_recent_ring(ring_center, ring_targets, ring_kind, ring_color, settings_.effective_timeout());
            if (settings_.action_debug)
            {
                logger_.runtime("aoe_center cat=%u action=%u mode=%s source=%s center=%s targets=%u",
                    category, action_id, center_mode == CenterMode::Target ? "target" : "source",
                    source.name, ring_center.name, static_cast<unsigned>(ring_targets.size()));
            }
        }

        if (!active_cast_key.empty() && (category == action_category::WeaponSkillFinish || category == action_category::MagicFinish || category == action_category::MonsterAbility))
        {
            recent_action_casts_.erase(active_cast_key);
        }
    }

    void Tracker::clear_all(void)
    {
        recent_lines_.clear();
        recent_rings_.clear();
        seen_pairs_.clear();
        seen_special_pairs_.clear();
        recent_spell_starts_.clear();
        recent_spell_events_.clear();
        recent_ability_starts_.clear();
        recent_action_casts_.clear();
        claim_lines_.clear();
        has_probe_line_ = false;
        last_lines_.clear();
        nearby_.clear();
        active_ids_.clear();
        snapshot_time_   = -1.0;
        next_expiration_ = 0.0;
        dirty_           = true;
    }

    void Tracker::clear_seen_pairs(void)
    {
        seen_pairs_.clear();
        dirty_ = true;
    }

    void Tracker::clear_special_pairs(void)
    {
        seen_special_pairs_.clear();
        dirty_ = true;
    }

    void Tracker::clear_aoe_visuals(void)
    {
        recent_rings_.clear();
        for (auto it = recent_lines_.begin(); it != recent_lines_.end();)
        {
            if (it->second.kind.find("_fan") != std::string::npos)
            {
                it = recent_lines_.erase(it);
            }
            else
            {
                ++it;
            }
        }
        dirty_ = true;
    }

    void Tracker::invalidate(void)
    {
        dirty_ = true;
    }

    bool Tracker::has_probe_line(void) const
    {
        return has_probe_line_;
    }

    size_t Tracker::visible_line_count(void) const
    {
        return last_lines_.size();
    }

    size_t Tracker::nearby_count(void) const
    {
        return nearby_.size();
    }

    void Tracker::world_snapshot(double now, bool force)
    {
        const uint32_t zone = game_.zone_id();
        const float range   = settings_.scan_range;
        if (force || snapshot_time_ < 0.0 || now - snapshot_time_ >= kMobSnapshotInterval || zone != snapshot_zone_ || range != snapshot_range_)
        {
            nearby_.clear();
            active_ids_.clear();
            game_.scan(range, nearby_, active_ids_);
            snapshot_time_  = now;
            snapshot_zone_  = zone;
            snapshot_range_ = range;
        }
    }

    void Tracker::collect_claim_lines(const PartySet& party, double now)
    {
        claim_lines_.clear();
        nearby_.clear();
        active_ids_.clear();
        game_.scan(settings_.scan_range, nearby_, active_ids_);
        snapshot_time_  = now;
        snapshot_zone_  = game_.zone_id();
        snapshot_range_ = settings_.scan_range;

        const ColorSet& colors = settings_.color_blind_mode ? kColorBlindColors : kDefaultColors;
        for (const EntityPoint& mob : nearby_)
        {
            if (mob.claim_id == 0)
            {
                continue;
            }

            // ClaimStatus packs the claimer in its low word; accept either form.
            const bool claimed_by_party = party.ids.count(mob.claim_id) != 0 || party.indices.count(mob.claim_id & 0xFFFF) != 0;
            if (!claimed_by_party)
            {
                continue;
            }

            EntityPoint target {};
            bool have_target = false;
            for (uint32_t index : party.indices)
            {
                EntityPoint member {};
                if (game_.entity_by_index(index, member) && (member.id == mob.claim_id || member.index == (mob.claim_id & 0xFFFF)))
                {
                    target      = member;
                    have_target = true;
                    break;
                }
            }
            if (!have_target && !game_.player_point(target))
            {
                continue;
            }

            RecentLine line {};
            line.uid     = 0;
            line.source  = mob;
            line.target  = target;
            line.kind    = "enemy";
            line.color   = colors.claim_enemy;
            line.created = now;
            line.timeout = settings_.claim_timeout;
            line.claim   = true;
            claim_lines_.push_back(line);
        }
    }

    void Tracker::prune_seen_pairs_for_inactive_entities(const PartySet& party)
    {
        for (auto it = seen_pairs_.begin(); it != seen_pairs_.end();)
        {
            unsigned source_id = 0;
            unsigned target_id = 0;
            const bool parsed  = std::sscanf(it->first.c_str(), "%u>%u", &source_id, &target_id) == 2;
            const bool source_active = parsed && (active_ids_.count(source_id) != 0 || party.ids.count(source_id) != 0);
            const bool target_active = parsed && (active_ids_.count(target_id) != 0 || party.ids.count(target_id) != 0);
            if (!parsed || !source_active || !target_active)
            {
                it = seen_pairs_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void Tracker::collect(double now, std::vector<const RecentLine*>& lines, std::vector<const RecentRing*>& rings)
    {
        lines.clear();
        rings.clear();
        next_expiration_ = 0.0;

        for (auto it = recent_lines_.begin(); it != recent_lines_.end();)
        {
            const double age = now - it->second.created;
            if (age > it->second.timeout)
            {
                it = recent_lines_.erase(it);
                continue;
            }
            lines.push_back(&it->second);
            const double expires_at = it->second.created + it->second.timeout;
            if (next_expiration_ == 0.0 || expires_at < next_expiration_)
            {
                next_expiration_ = expires_at;
            }
            ++it;
        }

        for (auto it = recent_rings_.begin(); it != recent_rings_.end();)
        {
            const double age = now - it->second.created;
            // Keep the ring payload long enough for the expanding wave to reach
            // distant targets and for their one-second halo fade to complete.
            if (age > it->second.timeout + 1.0)
            {
                it = recent_rings_.erase(it);
                continue;
            }
            rings.push_back(&it->second);
            const double expires_at = it->second.created + it->second.timeout + 1.0;
            if (next_expiration_ == 0.0 || expires_at < next_expiration_)
            {
                next_expiration_ = expires_at;
            }
            ++it;
        }

        const PartySet party = game_.party();
        if (settings_.claim_fallback)
        {
            collect_claim_lines(party, now);
            for (const RecentLine& line : claim_lines_)
            {
                lines.push_back(&line);
            }
        }
        else
        {
            world_snapshot(now, false);
        }

        switch (settings_.regular_attack_mode)
        {
            case RegularMode::First:
                prune_seen_pairs_for_inactive_entities(party);
                break;
            case RegularMode::Repeat:
                prune_expired(seen_pairs_, now, settings_.pair_cooldown);
                break;
            case RegularMode::Off:
                break;
        }

        prune_expired(seen_special_pairs_, now, settings_.special_cooldown);
        prune_expired(recent_spell_starts_, now, kDuplicateFinishWindow);
        prune_expired(recent_spell_events_, now, kDuplicateFinishWindow);
        prune_expired(recent_ability_starts_, now, kDuplicateFinishWindow);
        for (auto it = recent_action_casts_.begin(); it != recent_action_casts_.end();)
        {
            if (now - it->second.created > kDuplicateFinishWindow)
            {
                it = recent_action_casts_.erase(it);
            }
            else
            {
                ++it;
            }
        }

        std::sort(lines.begin(), lines.end(), [](const RecentLine* left, const RecentLine* right) {
            return left->created > right->created;
        });
        std::sort(rings.begin(), rings.end(), [](const RecentRing* left, const RecentRing* right) {
            return left->created > right->created;
        });
    }

    bool Tracker::update(double now, RenderState& out, bool boneprobe)
    {
        if (now - last_write_ < settings_.write_interval)
        {
            return false;
        }
        last_write_ = now;

        const bool expiration_due  = next_expiration_ > 0.0 && now >= next_expiration_;
        const bool maintenance_due = now - last_maintenance_ >= kStateMaintenanceInterval;
        const bool rebuild         = dirty_ || expiration_due || maintenance_due || settings_.claim_fallback || boneprobe != last_boneprobe_;
        last_boneprobe_            = boneprobe;
        if (!rebuild)
        {
            return false;
        }

        dirty_            = false;
        last_maintenance_ = now;

        std::vector<const RecentLine*> lines;
        std::vector<const RecentRing*> rings;
        collect(now, lines, rings);

        last_lines_.clear();
        for (const RecentLine* line : lines)
        {
            last_lines_.push_back(*line);
        }

        // The bone probe needs a line to inspect; fall back to the most recent one.
        if (boneprobe && lines.empty() && has_probe_line_)
        {
            lines.push_back(&probe_line_);
        }

        out.settings   = settings_.render_settings();
        out.boneprobe  = boneprobe;
        out.line_count = 0;
        for (const RecentLine* line : lines)
        {
            if (out.line_count >= kMaxLines)
            {
                break;
            }
            LineState& state = out.lines[out.line_count++];
            state            = LineState {};
            fill_line(state, line->source, line->target);
            state.uid     = line->uid;
            state.color   = line->color != 0 ? line->color : 0xEFFFFFFF;
            state.timeout = line->timeout > 0.0f ? line->timeout : settings_.effective_timeout();
        }

        out.ring_count = 0;
        for (const RecentRing* ring : rings)
        {
            if (out.ring_count >= kMaxRings)
            {
                break;
            }
            RingState& state = out.rings[out.ring_count++];
            state            = RingState {};
            state.uid                    = ring->uid;
            state.center_id              = ring->center.id;
            state.center_index           = ring->center.index;
            state.center_x               = ring->center.x;
            state.center_y               = ring->center.y;
            state.center_z               = ring->center.z;
            state.center_model_size      = ring->center.model_size;
            state.center_model_scale     = ring->center.model_scale > 0.0f ? ring->center.model_scale : 1.0f;
            state.center_short_anchor    = ring->center.short_anchor;
            state.center_floating_anchor = ring->center.floating_anchor;
            state.center_is_npc          = ring->center.is_npc;
            state.radius                 = ring->radius;
            state.timeout                = ring->timeout > 0.0f ? ring->timeout : settings_.effective_timeout();
            state.color                  = ring->color != 0 ? ring->color : 0xEFFFFFFF;
            state.indicator_style        = (ring->indicator_style == 10) ? 10 : 1;
            state.target_count           = 0;
            for (const EntityPoint& target : ring->targets)
            {
                if (state.target_count >= kMaxRingTargets)
                {
                    break;
                }
                fill_ring_target(state.targets[state.target_count++], target);
            }
        }

        return true;
    }

    std::string Tracker::describe(const EntityPoint& point) const
    {
        char buffer[512] {};
        std::snprintf(buffer, sizeof(buffer),
            "%s id=%u idx=%u hpp=%u dist=%.1f npc=%s race=%u model=%u size=%.2f scale=%.2f short=%s floating=%s claim=%u pos=(%.2f %.2f %.2f)",
            point.name, point.id, point.index, point.hpp, point.distance, point.is_npc ? "true" : "false",
            point.race, point.model, point.model_size, point.model_scale,
            point.short_anchor ? "true" : "false", point.floating_anchor ? "true" : "false",
            point.claim_id, point.x, point.y, point.z);
        return buffer;
    }

    void Tracker::action_debug_log(const char* event, const ActionPacket& packet, const EntityPoint& source, const EntityPoint* target, const ActionTarget* action_target, const char* reason)
    {
        if (!settings_.action_debug)
        {
            return;
        }

        const ActionEntry* first = (action_target != nullptr && !action_target->actions.empty()) ? &action_target->actions.front() : nullptr;
        logger_.runtime("action_debug event=%s cat=%u param=%u target_count=%u action_count=%u source=%s/%u/%u target=%s/%u/%u action_msg=%u action_param=%u reaction=%u animation=%u reason=%s",
            event, packet.category, packet.param, static_cast<unsigned>(packet.targets.size()),
            action_target != nullptr ? static_cast<unsigned>(action_target->actions.size()) : 0u,
            source.name, source.id, source.index,
            target != nullptr ? target->name : "nil", target != nullptr ? target->id : 0u, target != nullptr ? target->index : 0u,
            first != nullptr ? first->message : 0u, first != nullptr ? first->param : 0u,
            first != nullptr ? first->reaction : 0u, first != nullptr ? first->animation : 0u,
            reason != nullptr ? reason : "none");
    }

    bool Tracker::write_inspect(const std::string& path, const char* reason)
    {
        std::ofstream file(path, std::ios::app);
        if (!file.is_open())
        {
            return false;
        }

        char stamp[32] {};
        const std::time_t now = std::time(nullptr);
        std::tm local {};
        if (localtime_s(&local, &now) == 0)
        {
            std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
        }

        file << "---\n";
        file << "time=" << stamp << " reason=" << (reason != nullptr ? reason : "manual") << '\n';
        file << "enabled=" << (settings_.enabled ? "true" : "false") << " range=" << settings_.scan_range
             << " recent=" << recent_lines_.size() << " visible=" << last_lines_.size() << '\n';

        EntityPoint me {};
        if (game_.player_point(me))
        {
            file << "me=" << describe(me) << '\n';
        }

        int index = 0;
        for (const RecentLine& line : last_lines_)
        {
            ++index;
            file << "line_" << index << '=' << line.kind << ' ' << describe(line.source) << " -> " << describe(line.target)
                 << " age=" << (Game::now() - line.created) << '\n';
        }

        file << "nearby=" << nearby_.size() << '\n';
        const size_t limit = std::min<size_t>(nearby_.size(), 25);
        for (size_t i = 0; i < limit; ++i)
        {
            file << "nearby_" << (i + 1) << '=' << describe(nearby_[i]) << '\n';
        }
        return true;
    }

    std::string Tracker::maybe_auto_inspect(double now, const std::string& path)
    {
        if (!settings_.auto_inspect)
        {
            return std::string();
        }

        const uint32_t zone = game_.zone_id();
        if (zone != auto_inspect_zone_)
        {
            auto_inspect_zone_         = zone;
            auto_inspect_zone_written_ = false;
        }

        const PartySet party = game_.party();
        unsigned enemies     = 0;
        for (const EntityPoint& mob : nearby_)
        {
            if (mob.visible() && mob.is_npc && !party.contains(mob) && !party.is_pet(mob))
            {
                ++enemies;
            }
        }
        if (enemies == 0)
        {
            return std::string();
        }

        char message[160] {};
        if (!auto_inspect_zone_written_)
        {
            if (write_inspect(path, "auto_zone"))
            {
                auto_inspect_last_         = now;
                auto_inspect_zone_written_ = true;
                std::snprintf(message, sizeof(message), "Auto inspect complete: auto_zone, %u nearby enemies.", enemies);
                return message;
            }
            return std::string();
        }

        const double interval = std::max(30.0f, settings_.auto_inspect_interval);
        if (now - auto_inspect_last_ >= interval && write_inspect(path, "auto_interval"))
        {
            auto_inspect_last_ = now;
            std::snprintf(message, sizeof(message), "Auto inspect complete: auto_interval, %u nearby enemies.", enemies);
            return message;
        }
        return std::string();
    }
} // namespace targetlines
