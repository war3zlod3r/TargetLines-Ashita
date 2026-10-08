# Third-party notices

TargetLines for Ashita v4 is a port of [MogSafe/TargetLines](https://github.com/MogSafe/TargetLines),
a Windower 4 addon authored by MogSafe and distributed under the MIT License
(reproduced in `LICENSE`). The line and ring renderer, action tracking rules,
entity anchor heuristics and command set in this repository derive from that
project.

The components below retain their own licenses and copyright.

## SceneHook (BSD 3-Clause, Broguypal)

`src/scenehook/SceneHook.h` is the byte-identical SceneHook ABI v2 header
published by Broguypal. It is used only when the optional `scenehook` render
mode is selected. The full license text is in `src/scenehook/LICENSE` and is
reproduced below.

```text
BSD 3-Clause License

Copyright (c) 2026 Broguypal

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## Ashita v4 plugin SDK (LGPL v3, Ashita Development Team)

The plugin is compiled against the Ashita v4 plugin SDK headers (`Ashita.h`
and the headers it includes, plus the legacy Direct3D 8 import libraries it
ships). The SDK is not redistributed in this repository; it is read from an
Ashita v4 installation at build time. `cmake/FindAshitaSDK.cmake` is taken from
the Ashita `ExamplePlugin` template, which is distributed under the GNU LGPL v3.
See <https://www.gnu.org/licenses/lgpl-3.0.html>.

## Acknowledgements

TargetLines is inspired by Final Fantasy XII's target-line battle UI and prior
FFXI target-line addon concepts, including Jyouya/targetlines and its forks. No
source code or assets from those projects are included.
