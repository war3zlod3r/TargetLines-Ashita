# Porting notes: Windower TargetLines v2 to Ashita v4

This document records how each part of the Windower addon maps onto the Ashita
plugin so future changes to either project are easy to carry across.

## Architecture

| Windower v2 | Ashita port |
| --- | --- |
| `TargetLines.lua` (addon): packets, settings, classification, JSON snapshot encoding | `src/tracker.cpp`, `src/settings.cpp`, `src/targetlines.cpp` |
| `_TargetLines.dll` Lua C module: JSON parse, SceneHook client, D3D8 renderer | `src/renderer.cpp` (renderer), `src/targetlines.cpp` (render mode plumbing) |
| JSON string passed through `native.replace_state()` | `RenderState` struct (`src/state.hpp`) passed to `Renderer::set_state()` |
| `windower.packets.parse_action` | `src/action_packet.cpp` bit parser for packet 0x0028 |
| `windower.ffxi.get_mob_by_*`, `get_party`, `get_player`, `get_info` | `src/game.cpp` over `IEntity`, `IParty`, `IPlayer` |
| `config` / `texts` libraries | INI file in `config/targetlines/` and an ImGui window |
| `prerender` event | `IPlugin::Direct3DEndScene(isRenderingBackBuffer == true)` |
| SceneHook `draw_scene` callback | Optional `scenehook` render mode using the same vendored header |

## Entity field mapping

| Lua mob field | Ashita call | Notes |
| --- | --- | --- |
| `id` | `IEntity::GetServerId` | |
| `index` | entity table index | Lookups by id try `id & 0xFFF` first (NPC ids embed the index), then scan. |
| `x`, `y`, `z` | `GetLocalPositionX/Y/Z` | Same axis names: x/y ground plane, z height. The renderer maps to D3D x/z ground and y height. |
| `hpp` | `GetHPPercent` | Entities with 0 HP% are treated as not visible, as in the addon. |
| `is_npc` | `(GetSpawnFlags & 0x01) == 0` | 0x01 PC, 0x0D local player, 0x02 NPC, 0x10 mob. |
| `race` | `GetRace` | |
| `models[1]` | `GetLookHair` | First look slot; carries the model id for fixed-model NPCs. |
| `model_size` | `GetModelHitboxSize` | Packet-sourced hitbox size (humanoids about 1.0). |
| `model_scale` | `GetModelSize` | -1 when unset; treated as 1.0. |
| `distance` | `sqrt(GetDistance)` | Ashita stores the squared distance. |
| `claim_id` | `GetClaimStatus` | Low word holds the claimer; only used by the experimental claim fallback. |
| `pet_index`, `fellow_index` | `GetPetTargetIndex`, `GetFellowTargetIndex` | |
| party `p0..a25` | `IParty` members 0..17 with `GetMemberIsActive` | Trusts appear as ordinary members. |
| `info.zone`, `info.logged_in` | `IParty::GetMemberZone(0)`, member 0 active and `IPlayer::GetIsZoning() == 0` | |

## Action packet (0x0028)

Bit offsets follow the Windower field definitions: actor id at bit 40, a 6-bit
target count plus 4 unknown bits at bit 72, 4-bit category, 16-bit param, 16
unknown bits, 32-bit recast, then per target a 32-bit id and 4-bit action
count. Each action is reaction 5, animation 12, effect 4, stagger 3, knockback
3, param 17, message 10, flags 31, then optional added effect (1 + 6/4/17/10)
and spike effect (1 + 6/4/14/10). `tests/test_main.cpp` round-trips a packed
packet through the parser.

Category numbers used by the tracker are unchanged: 1 melee, 2 ranged, 3 WS
finish, 4 magic finish, 6 JA, 7 WS start, 8 magic start, 11 monster TP, 13 pet
ability, 14/15 JA variants.

## Zone changes

The Lua `zone change` event is replaced by incoming packets 0x000A (zone in)
and 0x000B (zone out); both clear every tracked line and the renderer caches.

## Renderer changes

- JSON parsing, the LuaCore mob-array signature scan and the Windower device
  discovery were removed. Actor pointers come from `IEntity::GetActorPointer`
  and the device from `IPlugin::Direct3DInitialize`.
- Bone anchor offsets (`actor + 0x678` root, `actor + 0x6B8` skeleton chain,
  bone stride 0x1A) are unchanged because they describe FFXiMain structures,
  not launcher structures.
- Diagnostic probes other than `boneprobe` were dropped.
- The large vertex batch buffer is heap allocated instead of being a class
  array.

## Rendering point

The archived v1 Windower plugin drew from Windower's `PostRender` hook and
read `D3DTS_VIEW`/`D3DTS_PROJECTION` there, which is evidence the camera
matrices are intact after the scene is drawn. The default `endscene` mode
relies on the same property. The `scenehook` mode reproduces the v2 behaviour
of drawing inside `draw_scene`, beneath the game UI.

## Things worth checking in game

1. `GetModelHitboxSize` versus `GetModelSize` as the `model_size`/`model_scale`
   pair. Only the automatic anchor height heuristics depend on this.
2. Whether `IEntity::GetFellowTargetIndex` and `GetPetTargetIndex` are populated
   for party members other than the local player.
3. Behaviour of `isRenderingBackBuffer` when the game renders to textures
   (shadow or map passes) within the same frame.
