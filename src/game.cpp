#include "game.hpp"

#include "Ashita.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace targetlines
{
    namespace
    {
        // Models whose anchors should stay at the default height even though the
        // size heuristics below would classify them as short.
        constexpr uint32_t kNormalAnchorModels[] = {
            272,  // Death Jacket
            3110, // Shantotto II / Shantotto-style trust model
        };

        constexpr uint32_t kShortAnchorModels[] = {
            268,  // Sand Hare
            276,  // Thread Leech
            340,  // Brutal Sheep
            348,  // Beach Pugil
            352,  // Beach Monk
            356,  // Snipper / crab-style models
            494,  // Goblin Gambler
            497,  // Goblin Bounty Hunter
            572,  // Ghoul
            960,  // Barnacled Box
            1299, // Houu the Shoalwader
        };

        bool contains_model(const uint32_t* models, size_t count, uint32_t model)
        {
            for (size_t i = 0; i < count; ++i)
            {
                if (models[i] == model)
                {
                    return true;
                }
            }
            return false;
        }

        std::string lower_name(const char* name)
        {
            std::string text(name != nullptr ? name : "");
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return text;
        }

        bool contains(const std::string& text, const char* needle)
        {
            return text.find(needle) != std::string::npos;
        }

        IEntity* entities(IAshitaCore* core)
        {
            if (core == nullptr || core->GetMemoryManager() == nullptr)
            {
                return nullptr;
            }
            return core->GetMemoryManager()->GetEntity();
        }

        IParty* party_manager(IAshitaCore* core)
        {
            if (core == nullptr || core->GetMemoryManager() == nullptr)
            {
                return nullptr;
            }
            return core->GetMemoryManager()->GetParty();
        }

        uint32_t entity_limit(IEntity* entity)
        {
            if (entity == nullptr)
            {
                return 0;
            }
            const uint32_t size = entity->GetEntityMapSize();
            return size == 0 ? kMaxEntityIndex : std::min(size, kMaxEntityIndex);
        }
    } // namespace

    bool compute_short_anchor(const EntityPoint& point)
    {
        if (contains_model(kNormalAnchorModels, std::size(kNormalAnchorModels), point.model))
        {
            return false;
        }
        if (contains_model(kShortAnchorModels, std::size(kShortAnchorModels), point.model))
        {
            return true;
        }
        if (point.race == 5 || point.race == 6)
        {
            return true;
        }

        const std::string name = lower_name(point.name);
        if (contains(name, "automaton"))
        {
            return true;
        }
        if (contains(name, "sabertooth") || contains(name, "tiger") || contains(name, "smilodon"))
        {
            return true;
        }
        if (contains(name, "crawler") || contains(name, "caterpillar"))
        {
            return true;
        }

        return point.is_npc && point.model_size > 0.0f && point.model_size <= 1.0f;
    }

    bool compute_floating_anchor(const EntityPoint& point)
    {
        if (!point.is_npc)
        {
            return false;
        }

        const std::string name = lower_name(point.name);
        return contains(name, "bee") || contains(name, "wasp") || contains(name, "vespo");
    }

    Game::Game(IAshitaCore* core)
        : core_(core)
    {}

    void Game::fill(uint32_t index, EntityPoint& out) const
    {
        IEntity* entity = entities(core_);
        out             = EntityPoint {};
        if (entity == nullptr)
        {
            return;
        }

        out.index = index;
        out.id    = entity->GetServerId(index);

        const char* name = entity->GetName(index);
        if (name != nullptr)
        {
            strncpy_s(out.name, sizeof(out.name), name, _TRUNCATE);
        }

        out.x   = entity->GetLocalPositionX(index);
        out.y   = entity->GetLocalPositionY(index);
        out.z   = entity->GetLocalPositionZ(index);
        out.hpp = entity->GetHPPercent(index);

        // Spawn flags: 0x01 = PC, 0x02 = NPC, 0x10 = Mob, 0x0D = local player.
        out.is_npc = (entity->GetSpawnFlags(index) & 0x01) == 0;
        out.race   = entity->GetRace(index);
        out.model  = entity->GetLookHair(index);

        out.model_size    = entity->GetModelHitboxSize(index);
        const float scale = entity->GetModelSize(index);
        out.model_scale   = scale > 0.0f ? scale : 1.0f;

        const float distance_sq = entity->GetDistance(index);
        out.distance            = distance_sq > 0.0f ? std::sqrt(distance_sq) : 0.0f;
        out.claim_id            = entity->GetClaimStatus(index);

        out.short_anchor    = compute_short_anchor(out);
        out.floating_anchor = compute_floating_anchor(out);
    }

    bool Game::entity_by_index(uint32_t index, EntityPoint& out) const
    {
        IEntity* entity = entities(core_);
        if (entity == nullptr || index == 0 || index >= entity_limit(entity))
        {
            return false;
        }
        if (entity->GetServerId(index) == 0)
        {
            return false;
        }

        fill(index, out);
        return true;
    }

    bool Game::entity_by_id(uint32_t id, EntityPoint& out) const
    {
        IEntity* entity = entities(core_);
        if (entity == nullptr || id == 0)
        {
            return false;
        }

        const uint32_t limit = entity_limit(entity);

        // NPC and monster ids embed their target index in the low 12 bits.
        const uint32_t guess = id & 0xFFF;
        if (guess != 0 && guess < limit && entity->GetServerId(guess) == id)
        {
            fill(guess, out);
            return true;
        }

        for (uint32_t index = 1; index < limit; ++index)
        {
            if (entity->GetServerId(index) == id)
            {
                fill(index, out);
                return true;
            }
        }
        return false;
    }

    bool Game::resolve(uint32_t id, uint32_t index_hint, EntityPoint& out) const
    {
        if (index_hint != 0 && entity_by_index(index_hint, out) && (id == 0 || out.id == id))
        {
            return true;
        }
        return entity_by_id(id, out);
    }

    uint32_t Game::player_index(void) const
    {
        IParty* party = party_manager(core_);
        if (party == nullptr || party->GetMemberIsActive(0) == 0 || party->GetMemberServerId(0) == 0)
        {
            return 0;
        }
        return party->GetMemberTargetIndex(0);
    }

    uint32_t Game::player_id(void) const
    {
        IParty* party = party_manager(core_);
        if (party == nullptr || party->GetMemberIsActive(0) == 0)
        {
            return 0;
        }
        return party->GetMemberServerId(0);
    }

    bool Game::player_point(EntityPoint& out) const
    {
        const uint32_t index = player_index();
        return index != 0 && entity_by_index(index, out);
    }

    bool Game::logged_in(void) const
    {
        if (core_ == nullptr || core_->GetMemoryManager() == nullptr)
        {
            return false;
        }

        IPlayer* player = core_->GetMemoryManager()->GetPlayer();
        if (player != nullptr && player->GetIsZoning() != 0)
        {
            return false;
        }
        return player_index() != 0;
    }

    uint32_t Game::zone_id(void) const
    {
        IParty* party = party_manager(core_);
        if (party == nullptr)
        {
            return 0;
        }
        return party->GetMemberZone(0);
    }

    PartySet Game::party(void) const
    {
        PartySet set {};
        IParty* party   = party_manager(core_);
        IEntity* entity = entities(core_);
        if (party == nullptr || entity == nullptr)
        {
            return set;
        }

        const uint32_t limit = entity_limit(entity);
        for (uint32_t member = 0; member < 18; ++member)
        {
            if (party->GetMemberIsActive(member) == 0)
            {
                continue;
            }

            const uint32_t server_id = party->GetMemberServerId(member);
            const uint32_t index     = party->GetMemberTargetIndex(member);
            if (server_id != 0)
            {
                set.ids.insert(server_id);
            }
            if (index == 0 || index >= limit)
            {
                continue;
            }
            set.indices.insert(index);

            const uint32_t companions[] = {entity->GetPetTargetIndex(index), entity->GetFellowTargetIndex(index)};
            for (uint32_t companion : companions)
            {
                if (companion == 0 || companion >= limit)
                {
                    continue;
                }
                set.indices.insert(companion);
                set.pet_indices.insert(companion);
                const uint32_t pet_id = entity->GetServerId(companion);
                if (pet_id != 0)
                {
                    set.ids.insert(pet_id);
                    set.pet_ids.insert(pet_id);
                }
            }
        }

        set.player_id = player_id();
        if (set.player_id != 0)
        {
            set.ids.insert(set.player_id);
        }
        return set;
    }

    void Game::scan(float range, std::vector<EntityPoint>& nearby, std::unordered_set<uint32_t>& active_ids) const
    {
        IEntity* entity = entities(core_);
        if (entity == nullptr)
        {
            return;
        }

        const uint32_t limit = entity_limit(entity);
        for (uint32_t index = 1; index < limit; ++index)
        {
            const uint32_t id = entity->GetServerId(index);
            if (id == 0 || entity->GetHPPercent(index) == 0)
            {
                continue;
            }

            active_ids.insert(id);

            if ((entity->GetSpawnFlags(index) & 0x01) != 0)
            {
                continue;
            }

            const float distance_sq = entity->GetDistance(index);
            const float distance    = distance_sq > 0.0f ? std::sqrt(distance_sq) : 0.0f;
            if (distance > range)
            {
                continue;
            }

            EntityPoint point {};
            fill(index, point);
            if (point.visible())
            {
                nearby.push_back(point);
            }
        }

        std::sort(nearby.begin(), nearby.end(), [](const EntityPoint& left, const EntityPoint& right) {
            return left.distance < right.distance;
        });
    }

    std::string Game::spell_name(uint32_t spell_id) const
    {
        if (core_ == nullptr || core_->GetResourceManager() == nullptr)
        {
            return std::string();
        }

        const ISpell* spell = core_->GetResourceManager()->GetSpellById(spell_id);
        if (spell == nullptr)
        {
            return std::string();
        }

        const char* name = spell->Name[2] != nullptr ? spell->Name[2] : spell->Name[0];
        return name != nullptr ? std::string(name) : std::string();
    }

    bool Game::spell_self_centered_area(uint32_t spell_id) const
    {
        if (core_ == nullptr || core_->GetResourceManager() == nullptr)
        {
            return false;
        }

        const ISpell* spell = core_->GetResourceManager()->GetSpellById(spell_id);
        // AreaShapeType 3: circle on self while targeting an enemy (the -ra family).
        return spell != nullptr && spell->AreaShapeType == 3;
    }

    uintptr_t Game::actor_pointer(uint32_t index) const
    {
        IEntity* entity = entities(core_);
        if (entity == nullptr || index == 0 || index >= entity_limit(entity))
        {
            return 0;
        }
        return entity->GetActorPointer(index);
    }

    double Game::now(void)
    {
        static LARGE_INTEGER frequency = [] {
            LARGE_INTEGER value {};
            QueryPerformanceFrequency(&value);
            return value;
        }();

        LARGE_INTEGER counter {};
        QueryPerformanceCounter(&counter);
        if (frequency.QuadPart == 0)
        {
            return static_cast<double>(GetTickCount64()) / 1000.0;
        }
        return static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
    }
} // namespace targetlines
