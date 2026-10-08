// TargetLines for Ashita v4 - incoming action packet (0x0028) parser.
//
// Windower exposes a pre-parsed 'action' event; Ashita hands plugins the raw
// packet bytes, so the bit-packed structure is decoded here. The layout follows
// the publicly documented Windower field definitions for packet 0x028.

#ifndef TARGETLINES_ACTION_PACKET_HPP_INCLUDED
#define TARGETLINES_ACTION_PACKET_HPP_INCLUDED

#include <cstdint>
#include <vector>

namespace targetlines
{
    constexpr uint16_t kActionPacketId = 0x0028;

    // Action categories as used by the Windower addon.
    namespace action_category
    {
        constexpr uint32_t Melee             = 1;
        constexpr uint32_t RangedFinish      = 2;
        constexpr uint32_t WeaponSkillFinish = 3;
        constexpr uint32_t MagicFinish       = 4;
        constexpr uint32_t ItemFinish        = 5;
        constexpr uint32_t JobAbility        = 6;
        constexpr uint32_t WeaponSkillStart  = 7;
        constexpr uint32_t MagicStart        = 8;
        constexpr uint32_t ItemStart         = 9;
        constexpr uint32_t MonsterAbility    = 11;
        constexpr uint32_t RangedStart       = 12;
        constexpr uint32_t PetAbility        = 13;
        constexpr uint32_t JobAbilityAlt1    = 14;
        constexpr uint32_t JobAbilityAlt2    = 15;
    } // namespace action_category

    struct ActionEffect
    {
        bool present       = false;
        uint32_t animation = 0;
        uint32_t effect    = 0;
        uint32_t param     = 0;
        uint32_t message   = 0;
    };

    struct ActionEntry
    {
        uint32_t reaction  = 0;
        uint32_t animation = 0;
        uint32_t effect    = 0;
        uint32_t stagger   = 0;
        uint32_t knockback = 0;
        uint32_t param     = 0;
        uint32_t message   = 0;
        uint32_t flags     = 0;
        ActionEffect added_effect {};
        ActionEffect spike_effect {};
    };

    struct ActionTarget
    {
        uint32_t id = 0;
        std::vector<ActionEntry> actions;
    };

    struct ActionPacket
    {
        uint32_t actor_id     = 0;
        uint32_t target_count = 0;
        uint32_t category     = 0;
        uint32_t param        = 0; // Spell / ability / weapon skill id depending on category.
        uint32_t recast       = 0;
        std::vector<ActionTarget> targets;
    };

    // Parses a complete 0x0028 packet (including its 4 byte header). Returns false
    // when the buffer is too short or the bit stream runs out before the declared
    // targets and actions have been read.
    bool parse_action_packet(const uint8_t* data, uint32_t size, ActionPacket& out);
} // namespace targetlines

#endif // TARGETLINES_ACTION_PACKET_HPP_INCLUDED
