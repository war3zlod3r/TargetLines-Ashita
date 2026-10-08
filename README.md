# TargetLines for Ashita v4

TargetLines draws Final Fantasy XII style target lines during combat in Final
Fantasy XI. Lines show the flow of actions between players, party members,
trusts, pets and enemies; supported area actions can be shown with ring or fan
AoE indicators.

This repository is a port of [MogSafe/TargetLines](https://github.com/MogSafe/TargetLines)
(a Windower 4 addon plus native module) to a single **Ashita v4 plugin**
(`targetlines.dll`, built against the 4.30 plugin interface). The Direct3D 8
line and ring renderer is carried over nearly verbatim; the Lua action tracking
logic was rewritten in C++ against the Ashita plugin SDK.

## Installation

1. Copy `bin/targetlines.dll` into your Ashita `plugins` folder.
2. Load it in game:

```text
/load targetlines
```

3. To load it automatically, add `/load targetlines` to your boot script
   (for example `scripts/default.txt`).

Settings are written to `config/targetlines/targetlines.ini` and diagnostics to
`config/targetlines/runtime.log` (rotated at 5 MiB).

The plugin needs the Microsoft Visual C++ 2015-2022 x86 runtime, which Ashita
itself already requires.

## Why a plugin rather than an addon

Ashita v4 addons are Lua, but the heart of TargetLines is a native renderer
that reads the game's view and projection matrices, resolves skeleton bone
anchors from actor memory, and submits pre-transformed geometry each frame. A
plugin gives that code direct access to the Direct3D device, incoming packets,
the entity and party tables, ImGui and the configuration folder with no
cross-language transport, so the whole feature set lives in one DLL that
loads and unloads cleanly.

## Features

- Regular attacks draw once per source-target pair by default.
- Spells, job abilities, weapon skills, monster TP moves and pet actions draw
  as they happen, with start/finish duplicate suppression.
- Player, party/trust, pet, enemy and unrelated-party lines can be toggled
  independently.
- Area actions can be shown as Ring (A), Ring (B), Fan or Off.
- Color blind palette, independent opacity per source category, line width,
  glow and duration controls.
- Lines follow moving entities using live actor positions and, for NPCs, the
  torso bone (bone 21, or bone 39 for Mithra and a few trust models).
- ImGui settings window (`/tl config`) replacing the Windower text-box panel.

### Line colors

- Blue: player, party, trust or pet actions against enemies.
- Green: friendly support actions within the party or trust group.
- Red: enemy actions targeting the player, party, trusts or pets.
- Magenta: NPC-to-NPC actions, including enemy-to-enemy actions.

Color blind mode uses sky blue, yellow, vermilion and reddish purple.

## Render modes

| Mode | Where it draws | Notes |
| --- | --- | --- |
| `endscene` (default) | Ashita's `Direct3DEndScene` callback for the back buffer | No game code is patched. Lines are drawn after the game UI, so they can cross menus and the chat log. |
| `scenehook` | Inside `FFXiMain!draw_scene` via Broguypal's SceneHook ABI v2 | Matches the Windower version exactly: lines render beneath menus and chat. Installs a single 9-byte jump patch in `draw_scene` that stays for the life of the game process (by design, so modules can load and unload in any order). |

Switch with `/tl rendermode endscene|scenehook` or from the Advanced section of
the settings window. If SceneHook cannot find the `draw_scene` prologue it
fails closed and the status is reported by `/tl nativestatus`.

## Commands

Aliases: `/tl` and `/targetlines`.

```text
/tl on | off                     Enable or disable line output.
/tl config | settings            Open or close the settings window.
/tl playerlines [on|off]         Toggle player-origin lines.
/tl partylines [on|off]          Toggle party/trust-origin lines.
/tl petlines [on|off]            Toggle allied pet-origin lines.
/tl enemylines [on|off]          Toggle enemy-origin lines.
/tl otherpartylines [on|off]     Toggle unrelated-party lines.
/tl speciallines [on|off]        Toggle abilities, spells, WS and TP moves.
/tl aoemode off|fan|ring1|ring2  Select the AoE presentation.
/tl aoeopacity <0.1-1.25>|+|-    Adjust AoE opacity.
/tl colorblind [on|off]          Toggle color blind mode.
/tl regular first|repeat|off     Configure regular-attack lines.
/tl playeropacity <0.5-2>|+|-    Adjust player line opacity.
/tl allyopacity <0.5-2>|+|-      Adjust ally line opacity.
/tl enemyopacity <0.5-2>|+|-     Adjust enemy line opacity.
/tl width +|-                    Adjust line width.
/tl fade +|-                     Adjust line duration.
/tl clear                        Clear active lines and attack memory.
/tl status                       Print plugin status.
```

Advanced and diagnostic commands:

```text
/tl opacity <0-1>|+|-            Global opacity.
/tl glow +|-                     Line glow.
/tl sourceheight +|-             Source anchor height.
/tl targetheight +|-             Target anchor height.
/tl timeout <seconds>            Line duration directly.
/tl range <yalms>                Nearby scan range.
/tl interval <seconds>           State update interval (default 0.016).
/tl cooldown <seconds>           Regular-attack repeat delay.
/tl specialcooldown <seconds>    Special-action repeat delay.
/tl rendermode endscene|scenehook
/tl claim [on|off]               Experimental claim fallback lines.
/tl autoinspect [on|off]         Automatic inspect snapshots.
/tl autoinspect interval <sec>   Inspect interval, minimum 30 seconds.
/tl actiondebug [on|off]         Action-packet diagnostic logging to runtime.log.
/tl boneprobe                    Log the anchors used by the latest line.
/tl inspect                      Write an entity snapshot to inspect.log.
/tl nativestatus                 Renderer and SceneHook status.
```

## Building

Requirements:

- Visual Studio 2022 or newer with the "Desktop development with C++" workload
  (MSVC x86 toolset). Visual Studio's bundled CMake and Ninja are used.
- The Ashita v4 plugin SDK: the `plugins/sdk` folder of an Ashita v4 install,
  or [AshitaXI/Ashita-v4beta](https://github.com/AshitaXI/Ashita-v4beta)
  `plugins/sdk`.

```powershell
.\build.ps1 -SdkPath "C:\Ashita\plugins\sdk"
```

The script enters an x86 developer environment, configures CMake with Ninja and
writes `bin\targetlines.dll`. Use `-Config Debug` for a debug build and
`-Clean` to discard the previous build directory. Plain CMake also works with
`ASHITA4_SDK_PATH` set in the environment (see `CMakeLists.txt`).

Offline tests for the action packet parser and settings round trip:

```powershell
tests\run_tests.cmd
```

## Differences from the Windower version

- Settings live in an INI file instead of `data/settings.xml`; the v1
  migration logic and the legacy Windower plugin handover were dropped.
- The settings panel is an ImGui window.
- Caster-centered AoE detection additionally uses the spell resource's area
  shape, which correctly classifies every elemental `-ra` spell (the original
  name match only caught Stonera).
- `debug`, `show`, `nativestart` and `nativestop` commands were removed; use
  `/tl status`, `/tl nativestatus` and `/tl rendermode` instead.
- Diagnostics go to `config/targetlines/runtime.log` and to Ashita's log.

See `docs/PORTING_NOTES.md` for the mapping between the Windower APIs and the
Ashita SDK calls used here.

## Troubleshooting

- **No lines appear.** Run `/tl status` and check `logged_in=on` and that
  `action_packets` increases during combat. If packets arrive but nothing is
  drawn, run `/tl nativestatus`: `render_calls` should climb every frame.
- **Lines are drawn in the wrong place.** Try `/tl rendermode scenehook`, which
  reads the camera matrices at the same point in the frame as the Windower
  version did.
- **SceneHook reports `draw_scene not found` or `already patched`.** Another
  module patched the same prologue, or the client build changed. Restart the
  client and use `/tl rendermode endscene`.
- **Anchors sit too high or low on some models.** `/tl sourceheight` and
  `/tl targetheight` adjust the global offsets; `/tl boneprobe` logs the
  resolved bone positions for the most recent line.

## License

TargetLines code is distributed under the MIT License (see `LICENSE`, which
carries MogSafe's copyright for the original work). SceneHook is BSD 3-Clause
by Broguypal and the Ashita SDK is LGPL v3; see `THIRD_PARTY_NOTICES.md`.
