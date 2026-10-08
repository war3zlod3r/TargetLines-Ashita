// Offline unit tests for the parts of TargetLines that do not need the game:
// the 0x0028 action packet bit parser and the settings file round trip.
//
// Build and run with tests\run_tests.cmd (uses the Visual Studio developer
// environment, host architecture is fine here).

#include "action_packet.hpp"
#include "settings.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
    int g_failures = 0;

    void check(bool condition, const char* what)
    {
        if (!condition)
        {
            ++g_failures;
            std::printf("FAIL: %s\n", what);
        }
    }

    // Little-endian bit packer mirroring the game's action packet encoding.
    struct BitWriter
    {
        std::vector<uint8_t> data;
        uint32_t position = 0;

        void write(uint32_t value, uint32_t bits)
        {
            for (uint32_t i = 0; i < bits; ++i)
            {
                const uint32_t bit  = position + i;
                const uint32_t byte = bit >> 3;
                if (data.size() <= byte)
                {
                    data.resize(byte + 1, 0);
                }
                if ((value >> i) & 1u)
                {
                    data[byte] |= static_cast<uint8_t>(1u << (bit & 7));
                }
            }
            position += bits;
        }
    };

    void write_action(BitWriter& w, uint32_t reaction, uint32_t animation, uint32_t effect, uint32_t stagger, uint32_t knockback, uint32_t param, uint32_t message, uint32_t flags, bool added, bool spike)
    {
        w.write(reaction, 5);
        w.write(animation, 12);
        w.write(effect, 4);
        w.write(stagger, 3);
        w.write(knockback, 3);
        w.write(param, 17);
        w.write(message, 10);
        w.write(flags, 31);
        w.write(added ? 1 : 0, 1);
        if (added)
        {
            w.write(5, 6);    // animation
            w.write(2, 4);    // effect
            w.write(77, 17);  // param
            w.write(161, 10); // message
        }
        w.write(spike ? 1 : 0, 1);
        if (spike)
        {
            w.write(9, 6);    // animation
            w.write(1, 4);    // effect
            w.write(33, 14);  // param
            w.write(44, 10);  // message
        }
    }

    void test_action_packet(void)
    {
        using namespace targetlines;

        BitWriter w;
        w.write(0x28, 9);          // packet id
        w.write(0, 7);             // size (unused by the parser)
        w.write(0, 16);            // sync
        w.write(0, 8);             // byte 4: action size byte
        w.write(0x01234567u, 32);  // actor id
        w.write(2, 6);             // target count
        w.write(0, 4);             // unknown
        w.write(4, 4);             // category: magic finish
        w.write(835, 16);          // param: Stonera II
        w.write(0xBEEF, 16);       // unknown
        w.write(123456, 32);       // recast

        // Target 1: one action with an additional effect.
        w.write(0x010A0B01u, 32);
        w.write(1, 4);
        write_action(w, 8, 100, 3, 0, 1, 1234, 2, 0x7FFFFFFFu, true, false);

        // Target 2: two actions, the second with a spike effect.
        w.write(0x010A0B02u, 32);
        w.write(2, 4);
        write_action(w, 1, 2, 3, 4, 5, 65535, 1023, 0, false, false);
        write_action(w, 31, 4095, 15, 7, 7, 131071, 7, 0x12345678u, false, true);

        ActionPacket packet {};
        const bool ok = parse_action_packet(w.data.data(), static_cast<uint32_t>(w.data.size()), packet);
        check(ok, "action packet parses");
        check(packet.actor_id == 0x01234567u, "actor id");
        check(packet.target_count == 2, "target count");
        check(packet.category == 4, "category");
        check(packet.param == 835, "param");
        check(packet.recast == 123456, "recast");
        check(packet.targets.size() == 2, "two targets decoded");
        if (packet.targets.size() == 2)
        {
            const ActionTarget& t1 = packet.targets[0];
            check(t1.id == 0x010A0B01u, "target 1 id");
            check(t1.actions.size() == 1, "target 1 action count");
            if (t1.actions.size() == 1)
            {
                const ActionEntry& a = t1.actions[0];
                check(a.reaction == 8 && a.animation == 100 && a.effect == 3 && a.stagger == 0 && a.knockback == 1, "target 1 action header fields");
                check(a.param == 1234 && a.message == 2 && a.flags == 0x7FFFFFFFu, "target 1 action param/message/flags");
                check(a.added_effect.present && a.added_effect.animation == 5 && a.added_effect.effect == 2 && a.added_effect.param == 77 && a.added_effect.message == 161, "target 1 added effect");
                check(!a.spike_effect.present, "target 1 no spike effect");
            }

            const ActionTarget& t2 = packet.targets[1];
            check(t2.id == 0x010A0B02u, "target 2 id");
            check(t2.actions.size() == 2, "target 2 action count");
            if (t2.actions.size() == 2)
            {
                check(t2.actions[0].param == 65535 && t2.actions[0].message == 1023 && !t2.actions[0].added_effect.present, "target 2 action 1");
                const ActionEntry& a = t2.actions[1];
                check(a.reaction == 31 && a.animation == 4095 && a.effect == 15 && a.stagger == 7 && a.knockback == 7, "target 2 action 2 header (max values)");
                check(a.param == 131071 && a.message == 7 && a.flags == 0x12345678u, "target 2 action 2 param/message/flags");
                check(a.spike_effect.present && a.spike_effect.animation == 9 && a.spike_effect.effect == 1 && a.spike_effect.param == 33 && a.spike_effect.message == 44, "target 2 spike effect");
            }
        }

        // Truncated buffers must fail cleanly rather than read out of bounds.
        ActionPacket truncated {};
        check(!parse_action_packet(w.data.data(), 0x10, truncated), "too-short packet rejected");
        check(!parse_action_packet(w.data.data(), static_cast<uint32_t>(w.data.size() - 6), truncated), "truncated packet rejected");
        check(!parse_action_packet(nullptr, 64, truncated), "null packet rejected");

        // A melee round with no actions on its single target still parses.
        BitWriter m;
        m.write(0, 32);
        m.write(0, 8);
        m.write(0x01000001u, 32);
        m.write(1, 6);
        m.write(0, 4);
        m.write(1, 4);
        m.write(0, 16);
        m.write(0, 16);
        m.write(0, 32);
        m.write(0x01000002u, 32);
        m.write(0, 4);
        ActionPacket melee {};
        check(parse_action_packet(m.data.data(), static_cast<uint32_t>(m.data.size()), melee), "melee packet parses");
        check(melee.category == 1 && melee.targets.size() == 1 && melee.targets[0].actions.empty(), "melee packet contents");
    }

    void test_settings(void)
    {
        using namespace targetlines;

        char path[512] {};
        std::snprintf(path, sizeof(path), "%s\\targetlines_test_settings.ini", std::getenv("TEMP") ? std::getenv("TEMP") : ".");

        Settings a {};
        a.enabled                = false;
        a.show_other_party_lines = true;
        a.aoe_mode               = AoeMode::Ring2;
        a.regular_attack_mode    = RegularMode::Repeat;
        a.color_blind_mode       = true;
        a.width_scale            = 1.25f;
        a.aoe_opacity_scale      = 0.9f;
        a.pair_cooldown          = 12.5f;
        a.render_mode            = RenderMode::SceneHook;
        a.scan_range             = 75.0f;
        check(a.save(path), "settings save");

        Settings b {};
        check(b.load(path), "settings load");
        check(b.enabled == false, "enabled round trip");
        check(b.show_other_party_lines == true, "other party lines round trip");
        check(b.aoe_mode == AoeMode::Ring2, "aoe mode round trip");
        check(b.regular_attack_mode == RegularMode::Repeat, "regular mode round trip");
        check(b.color_blind_mode == true, "color blind round trip");
        check(b.width_scale > 1.249f && b.width_scale < 1.251f, "width scale round trip");
        check(b.aoe_opacity_scale > 0.899f && b.aoe_opacity_scale < 0.901f, "aoe opacity round trip");
        check(b.pair_cooldown > 12.49f && b.pair_cooldown < 12.51f, "pair cooldown round trip");
        check(b.render_mode == RenderMode::SceneHook, "render mode round trip");
        check(b.scan_range > 74.9f && b.scan_range < 75.1f, "scan range round trip");
        check(b.show_player_lines == true, "untouched defaults survive");

        // Derived values match the Windower defaults.
        Settings d {};
        check(d.effective_opacity() > 0.79f && d.effective_opacity() < 0.81f, "default opacity 0.8");
        check(d.effective_timeout() > 1.49f && d.effective_timeout() < 1.51f, "default timeout 1.5");
        check(d.effective_source_height() < -0.74f && d.effective_source_height() > -0.76f, "default source height -0.75");
        check(d.effective_target_height() < -1.34f && d.effective_target_height() > -1.36f, "default target height -1.35");
        check(d.ring_indicator_style() == 1 && d.ring_indicator_enabled(), "default ring style");

        AoeMode mode = AoeMode::Off;
        check(parse_aoe_mode("ring", mode) && mode == AoeMode::Ring1, "parse 'ring' alias");
        check(parse_aoe_mode("Fan", mode) && mode == AoeMode::Fan, "parse case-insensitive");
        check(!parse_aoe_mode("bogus", mode), "reject unknown aoe mode");
        RegularMode regular = RegularMode::First;
        check(parse_regular_mode("cooldown", regular) && regular == RegularMode::Repeat, "parse regular alias");
        RenderMode render = RenderMode::EndScene;
        check(parse_render_mode("scenehook", render) && render == RenderMode::SceneHook, "parse render mode");

        std::remove(path);
    }
} // namespace

int main(void)
{
    test_action_packet();
    test_settings();

    if (g_failures == 0)
    {
        std::printf("All tests passed.\n");
        return 0;
    }
    std::printf("%d check(s) failed.\n", g_failures);
    return 1;
}
