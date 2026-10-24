# Automatic standalone four-deck bridge

This beta pairs the XDJ-XZ 1.26 runtime with a RAM-only USB helper and GPL-2.0-only hub module. Connect the rear USB-B socket to top USB1 with a USB-B to USB-A cable. Music stays in USB2. The helper waits for native decks to be stopped before module admission and USB binding.

The module accepts only the exact XZ loopback hub/topology. It can adopt an already enumerated USB1 hub only while unbound and childless. It never changes the upstream hub or music port, and never forces an unload. Physical disconnection restores the owned metadata and releases its references. A configured driver unbind alone is not treated as physical disconnection.

The helper observes each controller boot and music mount instead of using an owner-specific MAC or stick allowlist. It retains mount/root/block descriptors and retires on identity changes. Supported initial media is FAT16/FAT32 or exFAT, first partition on USB2, with an observable USB serial and mount identity. This is not a music-removal or RAM-carry feature. Keep the music USB connected during playback.

The Windows package includes licenses/xz-hub-source.tar.gz with the complete retained kernel source, configuration, local compiler compatibility header, module and build recipes. The same archive accompanies the beta release. Extract it into a fresh directory and run bash build-module.sh using ARM GNU EABI GCC13, GNU binutils, make and Python3. CRCs are derived from recompiling source; live CRC substitution is not used. The kernel build directory is disposable.

build_bridge.py accepts --payload, --module, --sha-source, --zig and --output. Supply your locally prepared supported application/runtime payload, the built module directory and mods/audio/vendor/sha256. It derives exact hashes/ABI pins and checks the ARM32 GLIBC2.4 build with QEMU. Proprietary firmware, application binaries and boot keys are not included in corresponding source.

detach.S is GPL-2.0-only and builds with arm-linux-gnueabi-gcc -nostdlib -static -no-pie -march=armv7-a -marm -Wl,--build-id=none detach.S -o detach. The bridge is MIT-licensed; its SHA256 implementation retains its own public-domain notice under mods/audio/vendor/sha256.

Offline lifecycle and sanitizer tests plus exact on-device read-only qualification do not establish audible playback, kernel scheduling under every workload or complete four-deck performance acceptance. The beta release notes list remaining limitations.
