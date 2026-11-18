# xz-link-peer: what has to ship with it

`xz-link-peer` is a separate program that contains the official Ableton Link library. This is a reading of the
licence texts, not legal advice. Reviewed 2026-11-15 for 0.5.1-Beta.3; the open question of the first draft
(shared memory between the GPL helper and the player) is answered in the last section.

## The binary is GPL-2.0-or-later

Ableton Link (tag `Link-4.1.1`, commit `5ba907ae027a8c2ec8824463e37296dcefc5e9be`) is licensed "under the terms of
the GNU General Public License as published by the Free Software Foundation, either version 2 of the License, or
(at your option) any later version" (`licenses/Ableton-Link-LICENSE.md`). The helper binary is a work based on
Link, so the binary as a whole is distributed under GPL-2.0-or-later.

The helper's own files (`src/xz_link_peer.cpp`, `src/xz_link_shm.h`, `src/xz_link_probe.c`,
`src/qemu_setsockopt_shim.c`, `build.py`, `build.sh`) are MIT. MIT is GPL-compatible; recipients of the binary
still get GPL rights over all of it.

Everything else inside the shipped binary (`out/arm-musl/xz-link-peer`, fully static):

| Component | Licence | Notice in `licenses/` | GPL-2.0 compatible |
|---|---|---|---|
| asio-standalone 1.38.2 (`8806a68…`) | Boost Software License 1.0 | `asio-LICENSE_1_0.txt`, `asio-COPYING` | yes |
| musl 1.2.5 (from Zig 0.16.0) | MIT | `musl-COPYRIGHT` | yes |
| libc++, libc++abi, libunwind (from Zig 0.16.0) | Apache-2.0 WITH LLVM-exception | `libcxx-LICENSE.TXT`, `libcxxabi-LICENSE.TXT`, `libunwind-LICENSE.TXT` | yes: the LLVM exception names GPLv2 combinations and lets the conflicting Apache sections be deemed waived |
| compiler-rt (Zig 0.16.0's, written in Zig) | MIT | `zig-compiler-rt-LICENSE.txt` | yes |

The dynamic glibc build (`out/arm-glibc213/`) is not shipped: the static musl build is the one that ran on the
unit (r29, kernel 3.0.101), and it is the only one the release uses.

`licenses/` in this folder holds every notice byte for byte as upstream ships it; `build.py` checks each one against
the pinned Link commit, the asio archive and the pinned Zig before copying them to `out/`.

## What must ship in every release that contains the binary

1. **The binary**, staged by the loader as `tools/xz-link-peer` with its `.md5` (the USB payload).
2. **The licence texts with it**: the whole `licenses/` folder plus this file (`out/licenses/`). GPL v2 sections 1
   and 2 require the copyright notice, the no-warranty notice and the licence text to travel with the program.
   They go into the app's licence folder (the About page's "Open license notices" opens it) and, small as they
   are, beside the binary on the USB stick (`licenses/xz-link-peer/`).
3. **The complete corresponding source** (GPL v2 section 3(a)): `out/xz-link-peer-source.tar.gz` (about 10.5 MB),
   which `build.py` assembles reproducibly from
   - Ableton Link at the pinned commit (`git archive`), with asio-standalone at its pinned commit unpacked in
     `modules/asio-standalone/`, and the asio archive itself in `tools/`;
   - the helper source (`src/`), the scripts used to control compilation (`build.py`, `build.sh`) and this file;
   - `toolchain-runtime/`: the sources of musl, libc++, libc++abi, libunwind and compiler-rt exactly as the pinned
     Zig archive carries them, with Zig's licence. These are linked into the static binary, so they are shipped
     rather than left to the system-library exception.
   The compiler is named, not shipped: Zig 0.16.0, `zig-x86_64-linux-0.16.0.tar.xz`, sha256
   `70e49664a74374b48b51e6f3fdfbf437f6395d42509050588bd49abe52ba3d00`.
   Checked on 2026-11-15: the bundle unpacked on its own and built with `bash build.sh <zig>` gives
   `out/arm-musl/xz-link-peer` byte-identical to the shipped one (md5 `a7c084688c9590871cb18410d7749e34`, the binary
   that ran on the unit).
   Ship the bundle as a release asset in the same release as the installer (section 3, last paragraph: equivalent
   access from the same place counts as distributing the source) and inside the app's licence folder
   (`licenses/source/`, beside the existing pycdlib source). The alternative, section 3(b), a written offer valid
   for three years, is not needed.
4. The loader staging (`orchestrator.sh`, `stage_link_helper`) is the installation script for this program; it is
   MIT and already ships in the runtime source.

## Name and badge (Ableton Link Guidelines v1.1, in the Link repository)

- Page 8: "The name Ableton Link must always be used in full when displayed in text. Typeset Ableton Link as two
  words using an uppercase A and uppercase L followed by lowercase letters. Ableton Link can be set in all
  uppercase only when conforming to an established UI type style. The state of Ableton Link should always be
  displayed within the settings list. This language should always be consistent, clearly stating whether Ableton
  Link is Enabled or Disabled."
- Page 2: developers are not to create, edit or modify any brand elements relating to Ableton Link. Pages 9-14: the
  button and badge are Ableton's artwork; never "AbletonLink" as one word.

How the XZ applies them: the NETWORK page row reads `ABLETON LINK: ENABLED` or `ABLETON LINK: DISABLED` (the XZ
sets every label in upper case, the "established UI type style" exception). Help text, release notes and documents
write "Ableton Link". No badge or button artwork is used.

## The GPL helper next to the rest of XZ Mods: licence-clean as built

The player library (`libxz-mods.so`) and the helper exchange one 256-byte numeric record in `/dev/shm`
(`src/xz_link_shm.h`: tempo, beat, phase, peer count, flags, one command). The helper is started by `execve`, has
its own address space, links no player code, and the player links and loads no Link code. That is two programs
communicating, the separate-program case of the GPL FAQ ("pipes, sockets and command-line arguments are
communication mechanisms normally used between two separate programs"); a small documented record that any program
can implement does not make them one program.

The conclusion does not depend on that reading. `libxz-mods.so` is not an MIT-only binary today: it already links
GPL-2.0-only source files (`mods/extra_decks.c`, `mods/extra_player.c`, `mods/audio/usb_transport/`, and others,
30 files with that SPDX tag), and every release ships its complete source (`runtime/source.zip`, on the USB as
`licenses/xz-quad-source.zip`). Its other parts are MIT, "MIT OR Apache-2.0" (taken as MIT) and BSD/CC0, all
GPL-2.0 compatible. So even if a court treated the player and the helper as one work, that work would be
GPL-2.0-only (Link's "or later" allows version 2), every part of it is GPL-2.0 compatible, and its complete source
ships. No exception or proprietary Link licence is needed.

Conditions that keep it clean: ship the source bundle with every release that contains the binary (item 3), keep
the notices, keep the shared record free of Link code, and do not add Apache-2.0-only code to `libxz-mods.so`.
