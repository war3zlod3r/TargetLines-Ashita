// TargetLines for Ashita v4 - game data adapter.
//
// Wraps the Ashita memory managers (entities, party, player, resources) behind
// the small set of queries the tracker needs. This replaces the windower.ffxi
// calls used by the original Lua addon.

#ifndef TARGETLINES_GAME_HPP_INCLUDED
#define TARGETLINES_GAME_HPP_INCLUDED

#include "state.hpp"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

struct IAshitaCore;

namespace targetlines
{
    // Identity sets for the local player's party, alliance, trusts and pets.
    struct PartySet
    {
        std::unordered_set<uint32_t> ids;         // Members, pets and the player.
        std::unordered_set<uint32_t> indices;     // Member and pet target indices.
        std::unordered_set<uint32_t> pet_ids;
        std::unordered_set<uint32_t> pet_indices;
        uint32_t player_id = 0;

        bool contains(const EntityPoint& point) const
        {
            return (point.id != 0 && ids.count(point.id) != 0) || (point.index != 0 && indices.count(point.index) != 0);
        }

        bool is_pet(const EntityPoint& point) const
        {
            return (point.id != 0 && pet_ids.count(point.id) != 0) || (point.index != 0 && pet_indices.count(point.index) != 0);
        }
    };

    class Game final
    {
    public:
        explicit Game(IAshitaCore* core);

        // Entity lookups. Each fills 'out' and returns false when no entity exists.
        bool entity_by_index(uint32_t index, EntityPoint& out) const;
        bool entity_by_id(uint32_t id, EntityPoint& out) const;
        bool resolve(uint32_t id, uint32_t index_hint, EntityPoint& out) const;

        // Local player.
        bool player_point(EntityPoint& out) const;
        uint32_t player_id(void) const;
        uint32_t player_index(void) const;
        bool logged_in(void) const;
        uint32_t zone_id(void) const;

        PartySet party(void) const;

        // Collects visible NPC entities within 'range' yalms (sorted nearest first)
        // and the server ids of every visible entity.
        void scan(float range, std::vector<EntityPoint>& nearby, std::unordered_set<uint32_t>& active_ids) const;

        // Resource lookups.
        std::string spell_name(uint32_t spell_id) const;
        // True when the spell's area shape is a circle around the caster while
        // targeting an enemy (the elemental -ra family).
        bool spell_self_centered_area(uint32_t spell_id) const;

        // Raw actor pointer (CXiSkeletonActor*) for bone anchor resolution.
        uintptr_t actor_pointer(uint32_t index) const;

        // Monotonic seconds.
        static double now(void);

    private:
        void fill(uint32_t index, EntityPoint& out) const;

        IAshitaCore* core_;
    };

    // Anchor heuristics shared with the renderer's height adjustments.
    bool compute_short_anchor(const EntityPoint& point);
    bool compute_floating_anchor(const EntityPoint& point);
} // namespace targetlines

#endif // TARGETLINES_GAME_HPP_INCLUDED
