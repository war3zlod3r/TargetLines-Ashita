#include "action_packet.hpp"

namespace targetlines
{
    namespace
    {
        // FFXI packs action data as a little-endian bit stream: bit n lives in
        // byte n / 8 at bit position n % 8, and multi-bit values are assembled
        // least significant bit first.
        class BitReader
        {
        public:
            BitReader(const uint8_t* data, uint32_t size)
                : data_(data)
                , bit_size_(size * 8)
                , position_(0)
                , overflow_(false)
            {}

            uint32_t read(uint32_t bits)
            {
                if (bits == 0 || bits > 32 || position_ + bits > bit_size_)
                {
                    overflow_ = true;
                    position_ = bit_size_;
                    return 0;
                }

                uint32_t value = 0;
                for (uint32_t i = 0; i < bits; ++i)
                {
                    const uint32_t bit = position_ + i;
                    const uint32_t set = (data_[bit >> 3] >> (bit & 7)) & 1u;
                    value |= set << i;
                }
                position_ += bits;
                return value;
            }

            void skip(uint32_t bits)
            {
                if (position_ + bits > bit_size_)
                {
                    overflow_ = true;
                    position_ = bit_size_;
                    return;
                }
                position_ += bits;
            }

            bool overflow(void) const
            {
                return overflow_;
            }

        private:
            const uint8_t* data_;
            uint32_t bit_size_;
            uint32_t position_;
            bool overflow_;
        };

        void read_effect(BitReader& reader, ActionEffect& effect, uint32_t param_bits)
        {
            effect.present   = true;
            effect.animation = reader.read(6);
            effect.effect    = reader.read(4);
            effect.param     = reader.read(param_bits);
            effect.message   = reader.read(10);
        }
    } // namespace

    bool parse_action_packet(const uint8_t* data, uint32_t size, ActionPacket& out)
    {
        out = ActionPacket {};
        if (data == nullptr || size < 0x14)
        {
            return false;
        }

        BitReader reader(data, size);

        // Byte 0x04 is the packet's own size byte; the bit stream begins at byte 5.
        reader.skip(40);
        out.actor_id = reader.read(32);

        // Target count occupies 10 bits in the Windower definition; the upper 4
        // bits are unknown and have never been observed as part of the count.
        out.target_count = reader.read(6);
        reader.skip(4);

        out.category = reader.read(4);
        out.param    = reader.read(16);
        reader.skip(16); // Unknown.
        out.recast = reader.read(32);

        if (reader.overflow())
        {
            return false;
        }

        const uint32_t target_limit = out.target_count > 64 ? 64 : out.target_count;
        out.targets.reserve(target_limit);

        for (uint32_t t = 0; t < target_limit; ++t)
        {
            ActionTarget target {};
            target.id                   = reader.read(32);
            const uint32_t action_count = reader.read(4);
            if (reader.overflow())
            {
                return false;
            }

            target.actions.reserve(action_count);
            for (uint32_t a = 0; a < action_count; ++a)
            {
                ActionEntry entry {};
                entry.reaction  = reader.read(5);
                entry.animation = reader.read(12);
                entry.effect    = reader.read(4);
                entry.stagger   = reader.read(3);
                entry.knockback = reader.read(3);
                entry.param     = reader.read(17);
                entry.message   = reader.read(10);
                entry.flags     = reader.read(31);

                if (reader.read(1) == 1)
                {
                    read_effect(reader, entry.added_effect, 17);
                }
                if (reader.read(1) == 1)
                {
                    read_effect(reader, entry.spike_effect, 14);
                }

                if (reader.overflow())
                {
                    return false;
                }
                target.actions.push_back(entry);
            }

            out.targets.push_back(std::move(target));
        }

        return !reader.overflow();
    }
} // namespace targetlines
