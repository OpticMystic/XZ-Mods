#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# XDJ-XZ volatile loader and hardware diagnostic probe (firmware 1.26).

export PATH="/usr/sbin:/usr/bin:/sbin:/bin:/root/pdj:$PATH"

USB="$1"
export XZ_MODS_USB="$USB"
export XZ_MODS_DECK_INPUT_TRACE=1
OUT="$USB/XZ_RUNTIME"
LOG="$OUT/session.txt"
RBP="/root/pdj/rbp"
RBP_NEXT="/root/pdj/rbp.xzmod"
PAYLOAD_ROOT="/dev/shm/xz-boot-payload-$$"
SOURCE_RBP="$PAYLOAD_ROOT/rbp.patched"
EXPECTED_MD5_FILE="$PAYLOAD_ROOT/rbp.patched.md5"
SOURCE_GUI="$PAYLOAD_ROOT/gui/imagedata.dat"
EXPECTED_GUI_MD5_FILE="$PAYLOAD_ROOT/gui/imagedata.dat.md5"
GUI_RAM="/dev/shm/xz_imagedata.dat"
GUI_STAGE="$GUI_RAM.next"
GUI_TARGET="/root/gui/pset/imagedata/imagedata.dat"
SOURCE_FB_TOOL="$PAYLOAD_ROOT/tools/xz-fb-overlay"
FB_TOOL_RAM="/dev/shm/xz-fb-overlay"
SOURCE_HOOK="$PAYLOAD_ROOT/tools/libxz-directfb-hook.so"
EXPECTED_HOOK_MD5_FILE="$PAYLOAD_ROOT/tools/libxz-directfb-hook.so.md5"
HOOK_RAM="/dev/shm/libxz-directfb-hook.so"
SOURCE_MODS="$PAYLOAD_ROOT/tools/libxz-mods.so"
MODS_RAM="/dev/shm/libxz-mods.so"
MODS_MODE=""
SOURCE_GUI_IP_PATCH="$PAYLOAD_ROOT/tools/xz-gui-ip-patch"
EXPECTED_GUI_IP_PATCH_MD5_FILE="$PAYLOAD_ROOT/tools/xz-gui-ip-patch.md5"
GUI_IP_PATCH_RAM="/dev/shm/xz-gui-ip-patch"
NETWORK_TOOLS_RAM="/dev/shm"
APPLYING_FLAG="/tmp/xz_mod_applying"
APPLIED_FLAG="/tmp/xz_mod_applied"
LOADER_LOCK="/tmp/xz_mod_loader_lock"
PAYLOAD_LOOP=""
PAYLOAD_MOUNTED=0
LOADER_LOCK_OWNED=0

mkdir -p "$OUT" 2>/dev/null

log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1" >> "$LOG" 2>/dev/null
}

log_command() {
    title="$1"
    shift
    log "--- $title ---"
    "$@" >> "$LOG" 2>&1 || log "$title unavailable or failed (status $?)."
}

network_snapshot() {
    log_command "ifconfig -a" ifconfig -a
    log_command "route -n" route -n
    log_command "netstat listeners" netstat -lnt
    if [ -r /proc/net/arp ]; then
        log "--- /proc/net/arp ---"
        cat /proc/net/arp >> "$LOG" 2>&1
    fi
}

configure_vj_network() {
    CURRENT_IP=$(ifconfig eth0 2>/dev/null | sed -n 's/.*inet addr:\([0-9.]*\).*/\1/p' | head -1)
    if [ -n "$CURRENT_IP" ]; then
        echo "$CURRENT_IP" > /tmp/xz_vj_ip
    else
        # A cached address alone has no valid mask/gateway contract.
        # Keep the known link-local fallback; NETWORK applies an explicit plan.
        CURRENT_IP="169.254.168.59"
        if ! ifconfig eth0 "$CURRENT_IP" netmask 255.255.0.0 up >> "$LOG" 2>&1; then
            log "FAILED: Could not restore VJ.Tools network address $CURRENT_IP."
            return 1
        fi
    fi
    case "$CURRENT_IP" in
        169.254.*) route add -net 169.254.0.0 netmask 255.255.0.0 dev eth0 >> "$LOG" 2>&1 || true ;;
    esac
    log "VJ_NETWORK_READY: interface=eth0 address=$CURRENT_IP receiver=tcp50005 source=detected-or-restored"
}

usb_gadget_snapshot() {
    log "--- USB gadget/UDC read-only inventory ---"
    log_command "kernel" uname -a
    log_command "loaded USB modules" sh -c "lsmod | grep -Ei 'usb|gadget|udc|arcotg|pmulti|function'"
    log_command "UDC sysfs" sh -c "for u in /sys/class/udc/*; do [ -e \"\$u\" ] || continue; echo UDC=\$u; for f in state current_speed maximum_speed is_otg function; do [ -r \"\$u/\$f\" ] && echo \"\$f=\$(cat \"\$u/\$f\")\"; done; [ -r \"\$u/device/modalias\" ] && echo \"modalias=\$(cat \"\$u/device/modalias\")\"; done"
    log_command "UDC endpoint capabilities" sh -c "for e in /sys/class/udc/*/device/gadget/ep* /sys/kernel/debug/usb/*/ep*; do [ -e \"\$e\" ] || continue; echo ENDPOINT=\$e; [ -r \"\$e/maxpacket\" ] && echo \"maxpacket=\$(cat \"\$e/maxpacket\")\"; [ -r \"\$e/type\" ] && echo \"type=\$(cat \"\$e/type\")\"; done"
    [ -r /sys/kernel/debug/usb/devices ] && log_command "USB topology descriptors" cat /sys/kernel/debug/usb/devices
    [ -r /proc/bus/usb/devices ] && log_command "legacy USB topology descriptors" cat /proc/bus/usb/devices
    log_command "USB sysfs descriptors" sh -c "for d in /sys/bus/usb/devices/*; do [ -r \"\$d/idVendor\" ] || continue; echo DEVICE=\$d; for f in idVendor idProduct bcdUSB speed busnum devnum devpath manufacturer product serial bNumInterfaces; do [ -r \"\$d/\$f\" ] && echo \"\$f=\$(cat \"\$d/\$f\")\"; done; done"
    log_command "gadget modules available" sh -c "find /lib/modules/\$(uname -r) -type f 2>/dev/null | grep -Ei '/(g_|usb_f_|libcomposite|function)'"
    log "USB_GADGET_PROBE_COMPLETE: read_only=1 udc_rebound=0 modules_loaded=0"
}

file_md5() {
    md5sum "$1" 2>/dev/null | awk '{print $1}'
}

cleanup_payload() {
    cd /
    if [ "$LOADER_LOCK_OWNED" = 1 ]; then
        rm -f "$APPLYING_FLAG"
        rmdir "$LOADER_LOCK" 2>/dev/null
    fi
    if [ "$PAYLOAD_MOUNTED" = 1 ]; then
        umount -D "$PAYLOAD_ROOT" 2>>"$LOG" || return
    fi
    [ -z "$PAYLOAD_LOOP" ] || losetup -d "$PAYLOAD_LOOP" 2>>"$LOG"
    rmdir "$PAYLOAD_ROOT" 2>/dev/null
}

pin_own_payload() {
    # Firmware decryptors share /mnt/iso across USB partitions. A script may
    # remain open while a different image replaces that mount. Reopen only
    # this script's USB image at a private read-only mount before any wait.
    [ -f "$USB/autoexec.bin" ] || return 1
    mkdir "$PAYLOAD_ROOT" 2>>"$LOG" || return 1
    PAYLOAD_NOHASH=""
    if losetup 2>&1 | grep -q '\-N'; then PAYLOAD_NOHASH="-N"; fi
    for candidate in /dev/loop[0-7]; do
        losetup "$candidate" >/dev/null 2>&1 && continue
        if cat /usr/local/pdj/aes256.key | losetup -e aes $PAYLOAD_NOHASH -p 0 "$candidate" "$USB/autoexec.bin" >>"$LOG" 2>&1; then
            PAYLOAD_LOOP="$candidate"
            if mount -o ro "$PAYLOAD_LOOP" "$PAYLOAD_ROOT" >>"$LOG" 2>&1; then
                PAYLOAD_MOUNTED=1
                log "PAYLOAD_PINNED: image=$USB/autoexec.bin mount=$PAYLOAD_ROOT loop=$PAYLOAD_LOOP read_only=1"
                return 0
            fi
            losetup -d "$PAYLOAD_LOOP" 2>>"$LOG"
            PAYLOAD_LOOP=""
        fi
    done
    return 1
}

legacy_loader_running() {
    # Early-boot legacy scripts release APPLYING_FLAG before their delayed
    # restart. Wait for that script as well so it cannot restart over us.
    for proc_cmdline in /proc/[0-9]*/cmdline; do
        other_pid=${proc_cmdline#/proc/}
        other_pid=${other_pid%/cmdline}
        [ "$other_pid" != "$$" ] || continue
        [ ! -d "/dev/shm/xz-boot-payload-$other_pid" ] || continue
        arguments=$(tr '\000' '\n' < "$proc_cmdline" 2>/dev/null)
        printf '%s\n' "$arguments" | grep -Eq '(^|/)autoexec\.sh$' && return 0
    done
    return 1
}

acquire_loader_lock() {
    # Older loaders use only APPLYING_FLAG. Wait for them to finish instead
    # of dropping this newer payload, then serialize newer loaders too.
    busy_wait=0
    while [ "$busy_wait" -lt 90 ]; do
        if [ ! -f "$APPLYING_FLAG" ] && ! legacy_loader_running && mkdir "$LOADER_LOCK" 2>/dev/null; then
            LOADER_LOCK_OWNED=1
            touch "$APPLYING_FLAG"
            return 0
        fi
        [ "$busy_wait" != 0 ] || log "Waiting for the active USB loader to finish."
        sleep 1
        busy_wait=$((busy_wait+1))
    done
    log "FAILED: Active USB loader did not finish within 90 seconds; DJ process untouched."
    return 1
}

start_telnet() {
    if [ ! -f "$PAYLOAD_ROOT/enable_telnet" ]; then
        log "Telnet disabled by payload."
        return
    fi

    if netstat -lnt 2>/dev/null | grep -q ':2323 '; then
        log "Telnet already listening on TCP 2323."
        return
    fi

    # decrypt_autoexec.sh must unmount /mnt/iso after this script exits. The
    # daemon must not inherit /mnt/iso as its cwd or the unmount stays busy.
    (
        cd /
        exec telnetd -p 2323 -l /bin/sh
    ) >/tmp/xz_telnetd.log 2>&1 &
    TELNET_PID=$!
    sleep 1
    if kill -0 "$TELNET_PID" 2>/dev/null; then
        log "Telnet daemon alive (PID $TELNET_PID, TCP 2323, bind all interfaces)."
    else
        log "FAILED: telnetd exited; output follows."
        [ -f /tmp/xz_telnetd.log ] && cat /tmp/xz_telnetd.log >> "$LOG" 2>&1
    fi
}


# PRIVATE_AUTO_BRIDGE_BEGIN
stage_auto_bridge() {
    auto_root="/dev/shm/xz-auto-4bb9db00a2ab"
    [ ! -L "$auto_root" ] || return 1
    mkdir -p "$auto_root" || return 1
    [ "$(cd -P "$auto_root" && pwd -P)" = "$auto_root" ] || return 1
    if [ ! -f "$auto_root/xz-auto-bridge" ]; then
        [ "$(file_md5 "$PAYLOAD_ROOT/tools/auto-bridge/xz-auto-bridge")" = a787846871e31e926c048e7eabbf0ae4 ] || return 1
        cp "$PAYLOAD_ROOT/tools/auto-bridge/xz-auto-bridge" "$auto_root/xz-auto-bridge" || return 1
    fi
    [ ! -L "$auto_root/xz-auto-bridge" ] && [ "$(file_md5 "$auto_root/xz-auto-bridge")" = a787846871e31e926c048e7eabbf0ae4 ] || return 1
    if [ ! -f "$auto_root/xz_hub_depth.ko" ]; then
        [ "$(file_md5 "$PAYLOAD_ROOT/tools/auto-bridge/xz_hub_depth.ko")" = a9463514dcf4e2b0c3bef40b11a26dad ] || return 1
        cp "$PAYLOAD_ROOT/tools/auto-bridge/xz_hub_depth.ko" "$auto_root/xz_hub_depth.ko" || return 1
    fi
    [ ! -L "$auto_root/xz_hub_depth.ko" ] && [ "$(file_md5 "$auto_root/xz_hub_depth.ko")" = a9463514dcf4e2b0c3bef40b11a26dad ] || return 1
    if [ ! -f "$auto_root/detach" ]; then
        [ "$(file_md5 "$PAYLOAD_ROOT/tools/auto-bridge/detach")" = ba080b47a9a12ec4dc11eb37807b0533 ] || return 1
        cp "$PAYLOAD_ROOT/tools/auto-bridge/detach" "$auto_root/detach" || return 1
    fi
    [ ! -L "$auto_root/detach" ] && [ "$(file_md5 "$auto_root/detach")" = ba080b47a9a12ec4dc11eb37807b0533 ] || return 1
    chmod 700 "$auto_root/xz-auto-bridge" "$auto_root/detach" || return 1
}

start_auto_bridge() {
    auto_pid="$1"
    case "$auto_pid" in ''|*[!0-9]*) return 1;; esac
    auto_root="/dev/shm/xz-auto-4bb9db00a2ab"
    if [ -f /dev/shm/xz-four-deck/bridge.pid ]; then
        auto_owner=$(cat /dev/shm/xz-four-deck/bridge.pid)
        case "$auto_owner" in ''|*[!0-9]*) return 1;; esac
        if kill -0 "$auto_owner" 2>/dev/null; then
            auto_command=$(tr '\000' ' ' < "/proc/$auto_owner/cmdline")
            if [ "$auto_command" = "$auto_root/xz-auto-bridge --run $auto_pid " ]; then
                log "AUTO_BRIDGE: existing owner $auto_owner retained; see RAM status."
                return 0
            fi
            log "AUTO_BRIDGE: another owner exists; startup refused."
            return 1
        fi
    fi
    stage_auto_bridge || return 1
    printf '%s\n' '#!/bin/sh' "exec nice -n 19 $auto_root/xz-auto-bridge --run $auto_pid > $auto_root/launch.log 2>&1" > "$auto_root/run.sh" || return 1
    "$auto_root/detach" "$auto_root/run.sh" || return 1
    log "AUTO_BRIDGE: requested for native PID=$auto_pid; RAM status is /dev/shm/xz-four-deck/status."
}
# PRIVATE_AUTO_BRIDGE_END

verify_running_rbp() {
    VERIFY_PID="$1"
    VERIFY_EXPECTED="$2"

    if [ -z "$VERIFY_PID" ] || ! kill -0 "$VERIFY_PID" 2>/dev/null; then
        log "FAILED: Patched rbp did not survive the verification delay."
        log_command "rbp launch output" cat /tmp/xz_rbp.log
        log_command "recent kernel messages" dmesg
        return 1
    fi

    EXE_PATH=$(readlink "/proc/$VERIFY_PID/exe" 2>/dev/null)
    EXE_MD5=$(file_md5 "/proc/$VERIFY_PID/exe")
    log "RUNTIME_PROOF: rbp PID=$VERIFY_PID exe=$EXE_PATH md5=$EXE_MD5 expected=$VERIFY_EXPECTED"
    if [ -n "$VERIFY_EXPECTED" ] && [ "$EXE_MD5" != "$VERIFY_EXPECTED" ]; then
        log "FAILED: Running rbp hash does not match the payload."
        return 1
    fi

    log "SUCCESS: Patched rbp survived and its executable hash matches the payload."
    if [ -f "$HOOK_RAM" ]; then
        if grep -F "$HOOK_RAM" "/proc/$VERIFY_PID/maps" >/dev/null 2>&1; then
            log "HOOK_RUNTIME_PROOF: PID=$VERIFY_PID loaded=$HOOK_RAM tcp=50005"
        else
            log "FAILED: VJ.Tools DirectFB hook is staged but not mapped into rbp."
            return 1
        fi
    fi
    if [ -n "$MODS_MODE" ] && ! grep -F "$MODS_RAM" "/proc/$VERIFY_PID/maps" >/dev/null 2>&1; then
        log "FAILED: Requested standalone mod runtime is not mapped."
        return 1
    fi
    start_auto_bridge "$VERIFY_PID" || log "AUTO_BRIDGE: startup refused; native playback retained."
    return 0
}

collect_rbp_options() {
    ti_tsc=$(fw_printenv -n ti_tsc 2>/dev/null)
    tsc_option=""
    [ "$ti_tsc" = "1" ] && tsc_option="-t"

    joglcd=$(fw_printenv -n joglcd 2>/dev/null)
    joglcd_option=""
    [ "$joglcd" = "rt" ] && joglcd_option="-r"

    rootfs=$(fw_printenv -n rootfs 2>/dev/null)
    nfs_options=""
    if [ "$rootfs" = "nfs" ]; then
        host_pc_port=$(fw_printenv -n lanhub_host_port 2>/dev/null)
        nfs_options="-a -h$host_pc_port"
    fi
}

# USB_COLDPLUG_BEGIN
# Replay mount notifications, never block-device events or mount commands.
usb_coldplug_watch() (
    cd / || exit 1
    cp_pid=$1
    cp_proc=${2:-/proc}
    cp_ram=${3:-/dev/shm}
    cp_limit=${4:-45}
    cp_interval=${5:-1}
    case "$cp_pid" in ''|*[!0-9]*) exit 1;; esac
    cp_birth=$(sed 's/.*) //' "$cp_proc/$cp_pid/stat" 2>/dev/null | awk '{print $20}')
    [ -n "$cp_birth" ] || exit 1
    cp_state="$cp_ram/usb-coldplug-$cp_pid-$cp_birth"
    mkdir -p "$cp_state" || exit 1
    mkdir "$cp_state/lock" 2>/dev/null || exit 0
    trap 'rmdir "$cp_state/lock" 2>/dev/null' 0
    cp_attempt=0
    cp_start=$(awk '{printf "%.0f", $1 * 1000}' "$cp_proc/uptime")
    while [ "$cp_attempt" -lt "$cp_limit" ]; do
        cp_attempt=$((cp_attempt + 1))
        cp_current=$(sed 's/.*) //' "$cp_proc/$cp_pid/stat" 2>/dev/null | awk '{print $20}')
        if [ "$cp_current" != "$cp_birth" ]; then
            echo "USB_COLDPLUG: pid=$cp_pid attempt=$cp_attempt result=process-exited"
            exit 0
        fi
        cp_now=$(awk '{printf "%.0f", $1 * 1000}' "$cp_proc/uptime")
        cp_elapsed=$((cp_now - cp_start))
        cp_ready=$(ls -l "$cp_proc/$cp_pid/fd" 2>/dev/null | awk '
            $NF == "/proc/udev_usb1" {printf "usb1 "}
            $NF == "/proc/udev_usb2" {printf "usb2 "}')
        echo "USB_COLDPLUG: pid=$cp_pid attempt=$cp_attempt ready=[$cp_ready] elapsed_ms=$cp_elapsed"
        # Snapshot once per pass. Validate both names before any proc write.
        sort -u "$cp_proc/mounts" | while read -r cp_source cp_mount cp_type cp_options cp_dump cp_pass; do
            cp_dev=${cp_source#/dev/}
            case "$cp_source" in /dev/sd*) ;; *) continue;; esac
            case "$cp_dev" in *[!a-z0-9]*) continue;; esac
            case "$cp_mount" in
                /media/usb1/"$cp_dev") cp_slot=usb1;;
                /media/usb2/"$cp_dev") cp_slot=usb2;;
                *) continue;;
            esac
            case " $cp_ready " in *" $cp_slot "*) ;; *) continue;; esac
            cp_notice="$cp_state/$cp_slot-$cp_dev"
            [ ! -f "$cp_notice" ] || continue
            # Redirection must never create a substitute for a missing proc node.
            [ -e "$cp_proc/udev_$cp_slot" ] || continue
            if printf 'mount %s' "$cp_mount" > "$cp_proc/udev_$cp_slot"; then
                : > "$cp_notice"
                echo "USB_COLDPLUG: dev=$cp_dev action=mount slot=$cp_slot result=notified mounted_after_ms=$cp_elapsed attempt=$cp_attempt"
            else
                echo "USB_COLDPLUG: dev=$cp_dev action=mount slot=$cp_slot result=write-failed mounted_after_ms=$cp_elapsed attempt=$cp_attempt"
            fi
        done
        [ "$cp_attempt" -ge "$cp_limit" ] || sleep "$cp_interval"
    done
    echo "USB_COLDPLUG: pid=$cp_pid result=watch-complete attempts=$cp_attempt"
)

usb_coldplug_start() {
    # Do not hold the udev RUN chain while waiting for later partition mounts.
    (usb_coldplug_watch "$1") </dev/null >>/dev/shm/xz-usb-coldplug.log 2>&1 &
}
# USB_COLDPLUG_END

launch_runtime_rbp() {
    collect_rbp_options
    cd /root/pdj || return 1
    if [ -f "$HOOK_RAM" ]; then
        if [ -n "$MODS_MODE" ]; then
            MODS_OBSERVE=0
            MODS_UI=1
            [ "$MODS_MODE" = observer ] && MODS_OBSERVE=1 && MODS_UI=0
            LD_PRELOAD="$HOOK_RAM:$MODS_RAM" XZ_MODS_ENABLE=1 XZ_MODS_OBSERVER="$MODS_OBSERVE" XZ_MODS_UI="$MODS_UI" XZ_MODS_STEMS=1 \
                XZ_MODS_KEYSHIFT="$MODS_UI" XZ_MODS_NATIVE_VIEW="$MODS_UI" XZ_MODS_SHELL="$MODS_UI" XZ_MODS_SETTINGS_READONLY=1 XZ_MODS_BOOT_RUNTIME="$MODS_EXPECTED:$MODS_MODE" \
                ./rbp $tsc_option $joglcd_option $nfs_options >/tmp/xz_rbp.log 2>&1 &
        else
            LD_PRELOAD="$HOOK_RAM" ./rbp $tsc_option $joglcd_option $nfs_options >/tmp/xz_rbp.log 2>&1 &
        fi
        RBP_LAUNCH_PID=$!
        log "Launched hooked rbp PID=$RBP_LAUNCH_PID flags=[$tsc_option $joglcd_option $nfs_options] receiver=tcp50005."
    else
        ./rbp $tsc_option $joglcd_option $nfs_options >/tmp/xz_rbp.log 2>&1 &
        RBP_LAUNCH_PID=$!
        log "Launched candidate rbp PID=$RBP_LAUNCH_PID flags=[$tsc_option $joglcd_option $nfs_options]."
    fi
    usb_coldplug_start "$RBP_LAUNCH_PID"
    return 0
}

stage_network_tools() {
    # Validate both payload members before publishing either helper.
    for NETWORK_FILE in xz-network-udhcpc xz-network-lease-event; do
        NETWORK_SOURCE="$PAYLOAD_ROOT/tools/$NETWORK_FILE"
        NETWORK_EXPECTED=$(awk 'NR == 1 {print $1}' "$NETWORK_SOURCE.md5" 2>/dev/null)
        [ -n "$NETWORK_EXPECTED" ] && [ "$(file_md5 "$NETWORK_SOURCE")" = "$NETWORK_EXPECTED" ] || return 1
    done
    for NETWORK_FILE in xz-network-udhcpc xz-network-lease-event; do
        NETWORK_SOURCE="$PAYLOAD_ROOT/tools/$NETWORK_FILE"
        NETWORK_EXPECTED=$(awk 'NR == 1 {print $1}' "$NETWORK_SOURCE.md5" 2>/dev/null)
        cp "$NETWORK_SOURCE" "$NETWORK_TOOLS_RAM/$NETWORK_FILE.next" || return 1
        [ "$(file_md5 "$NETWORK_TOOLS_RAM/$NETWORK_FILE.next")" = "$NETWORK_EXPECTED" ] || return 1
        chmod 700 "$NETWORK_TOOLS_RAM/$NETWORK_FILE.next" || return 1
    done
    for NETWORK_FILE in xz-network-udhcpc xz-network-lease-event; do
        mv -f "$NETWORK_TOOLS_RAM/$NETWORK_FILE.next" "$NETWORK_TOOLS_RAM/$NETWORK_FILE" || return 1
    done
    log "NETWORK_TOOLS_STAGED: DHCP is idle until on-device Apply."
}

stage_optional_assets() {
    stage_network_tools || return 1
    if [ -f "$SOURCE_FB_TOOL" ]; then
        if cp "$SOURCE_FB_TOOL" "$FB_TOOL_RAM" 2>>"$LOG"; then
            chmod 755 "$FB_TOOL_RAM"
            log "Staged volatile framebuffer helper: $FB_TOOL_RAM md5=$(file_md5 "$FB_TOOL_RAM")"
        else
            log "FAILED: Could not stage framebuffer helper."
            return 1
        fi
    fi

    if [ -f "$SOURCE_HOOK" ]; then
        HOOK_SOURCE_MD5=$(file_md5 "$SOURCE_HOOK")
        HOOK_EXPECTED_MD5=$(awk 'NR == 1 {print $1}' "$EXPECTED_HOOK_MD5_FILE" 2>/dev/null)
        log "DirectFB hook payload: bytes=$(wc -c < "$SOURCE_HOOK" 2>/dev/null) md5=$HOOK_SOURCE_MD5 expected=$HOOK_EXPECTED_MD5"
        if [ -n "$HOOK_EXPECTED_MD5" ] && [ "$HOOK_SOURCE_MD5" != "$HOOK_EXPECTED_MD5" ]; then
            log "FAILED: DirectFB hook hash does not match its manifest."
            return 1
        fi
        if ! cp "$SOURCE_HOOK" "$HOOK_RAM.next" 2>>"$LOG"; then
            log "FAILED: Could not stage DirectFB hook in volatile RAM."
            return 1
        fi
        chmod 755 "$HOOK_RAM.next"
        if [ "$(file_md5 "$HOOK_RAM.next")" != "$HOOK_SOURCE_MD5" ]; then
            log "FAILED: Staged DirectFB hook hash mismatch."
            return 1
        fi
        mv -f "$HOOK_RAM.next" "$HOOK_RAM" || return 1
        log "SUCCESS: VJ.Tools filmstrip receiver staged in volatile RAM."
    fi

    if [ -f "$SOURCE_MODS" ]; then
        MODS_EXPECTED=$(awk 'NR == 1 {print $1}' "$PAYLOAD_ROOT/tools/libxz-mods.so.md5" 2>/dev/null)
        [ -n "$MODS_EXPECTED" ] && [ "$(file_md5 "$SOURCE_MODS")" = "$MODS_EXPECTED" ] || return 1
        MODS_MODE=$(cat "$PAYLOAD_ROOT/mods-mode" 2>/dev/null)
        case "$MODS_MODE" in observer|experimental) ;; *) return 1;; esac
        [ -f "$HOOK_RAM" ] || return 1
        cp "$SOURCE_MODS" "$MODS_RAM.next" || return 1
        [ "$(file_md5 "$MODS_RAM.next")" = "$MODS_EXPECTED" ] || return 1
        chmod 755 "$MODS_RAM.next" || return 1
        mv -f "$MODS_RAM.next" "$MODS_RAM" || return 1
        log "Standalone runtime staged in RAM: mode=$MODS_MODE md5=$MODS_EXPECTED"
        # Games are separate programs, staged from this USB boot image into RAM.
        # No engine/WAD download or computer connection occurs on the XZ.
        for GAME_FILE in xz-doom doom1.wad xz-updater xz-runtime-smoke xz-pc-wave-receive xz-support; do
            GAME_SOURCE="$PAYLOAD_ROOT/tools/$GAME_FILE"
            if [ -f "$GAME_SOURCE" ]; then
                GAME_EXPECTED=$(awk '{print $1}' "$GAME_SOURCE.md5" 2>/dev/null)
                [ -n "$GAME_EXPECTED" ] && [ "$(file_md5 "$GAME_SOURCE")" = "$GAME_EXPECTED" ] || return 1
                cp "$GAME_SOURCE" "/dev/shm/$GAME_FILE.next" || return 1
                [ "$(file_md5 "/dev/shm/$GAME_FILE.next")" = "$GAME_EXPECTED" ] || return 1
                chmod 700 "/dev/shm/$GAME_FILE.next" || return 1
                mv -f "/dev/shm/$GAME_FILE.next" "/dev/shm/$GAME_FILE" || return 1
                log "DOOM_USB_STAGED: $GAME_FILE md5=$GAME_EXPECTED"
            fi
        done
    fi

    if [ ! -f "$SOURCE_GUI" ]; then
        if [ ! -f "$PAYLOAD_ROOT/branding/apply.sh" ]; then
            log "No custom GUI pack in this payload."
            return 0
        fi
        WAIT_COUNT=0
        while [ ! -f "$GUI_TARGET" ] && [ "$WAIT_COUNT" -lt 45 ]; do
            sleep 1
            WAIT_COUNT=$((WAIT_COUNT + 1))
        done
        [ -f "$GUI_TARGET" ] || return 1
        SOURCE_GUI="$GUI_TARGET"
    fi

    GUI_SOURCE_MD5=$(file_md5 "$SOURCE_GUI")
    GUI_EXPECTED_MD5=$(awk 'NR == 1 {print $1}' "$EXPECTED_GUI_MD5_FILE" 2>/dev/null)
    log "GUI payload: bytes=$(wc -c < "$SOURCE_GUI" 2>/dev/null) md5=$GUI_SOURCE_MD5 expected=$GUI_EXPECTED_MD5"
    if [ -n "$GUI_EXPECTED_MD5" ] && [ "$GUI_SOURCE_MD5" != "$GUI_EXPECTED_MD5" ]; then
        log "FAILED: Mounted GUI payload hash does not match its manifest."
        return 1
    fi
    if ! cp "$SOURCE_GUI" "$GUI_STAGE" 2>>"$LOG"; then
        log "FAILED: Could not stage custom GUI in /dev/shm."
        return 1
    fi
    if [ "$(file_md5 "$GUI_STAGE")" != "$GUI_SOURCE_MD5" ]; then
        log "FAILED: Staged GUI hash mismatch."
        return 1
    fi

    GUI_RUNTIME_MD5="$GUI_SOURCE_MD5"
    if [ -f "$SOURCE_GUI_IP_PATCH" ]; then
        PATCH_SOURCE_MD5=$(file_md5 "$SOURCE_GUI_IP_PATCH")
        PATCH_EXPECTED_MD5=$(awk 'NR == 1 {print $1}' "$EXPECTED_GUI_IP_PATCH_MD5_FILE" 2>/dev/null)
        if [ -n "$PATCH_EXPECTED_MD5" ] && [ "$PATCH_SOURCE_MD5" != "$PATCH_EXPECTED_MD5" ]; then
            log "FAILED: GUI IP patcher hash mismatch."
            return 1
        fi
        cp "$SOURCE_GUI_IP_PATCH" "$GUI_IP_PATCH_RAM" 2>>"$LOG" || return 1
        chmod 755 "$GUI_IP_PATCH_RAM"
        DEVICE_IP=$(ifconfig eth0 2>/dev/null | sed -n 's/.*inet addr:\([0-9.]*\).*/\1/p' | head -1)
        [ -n "$DEVICE_IP" ] || DEVICE_IP="NO-IP"
        if ! "$GUI_IP_PATCH_RAM" "$GUI_STAGE" "$DEVICE_IP" >>"$LOG" 2>&1; then
            log "FAILED: Could not render runtime IP into GUI pack."
            return 1
        fi
        GUI_RUNTIME_MD5=$(file_md5 "$GUI_STAGE")
        log "GUI_IP_RUNTIME_PROOF: address=$DEVICE_IP md5=$GUI_RUNTIME_MD5"
    fi

    if [ -f "$PAYLOAD_ROOT/branding/apply.sh" ]; then
        (cd "$PAYLOAD_ROOT/branding" && md5sum -c MD5SUMS) >>"$LOG" 2>&1 || return 1
        sh "$PAYLOAD_ROOT/branding/apply.sh" "$GUI_STAGE" >>"$LOG" 2>&1 || return 1
        GUI_RUNTIME_MD5=$(file_md5 "$GUI_STAGE")
        log "XZ_MODS_BRANDING: applied original loading artwork in RAM."
    fi

    # On an early USB mount apl_start may still be mounting the UBIFS GUI
    # partition. Wait for that target, then overlay only this file from RAM.
    WAIT_COUNT=0
    while [ ! -f "$GUI_TARGET" ] && [ "$WAIT_COUNT" -lt 45 ]; do
        sleep 1
        WAIT_COUNT=$((WAIT_COUNT + 1))
    done
    if [ ! -f "$GUI_TARGET" ]; then
        log "FAILED: GUI target did not appear after ${WAIT_COUNT}s."
        return 1
    fi

    if ! mv -f "$GUI_STAGE" "$GUI_RAM" 2>>"$LOG"; then
        log "FAILED: Could not publish staged RAM GUI."
        return 1
    fi
    if mount | grep -F " on $GUI_TARGET " >/dev/null 2>&1; then
        umount "$GUI_TARGET" 2>>"$LOG" || return 1
    fi
    if ! mount --bind "$GUI_RAM" "$GUI_TARGET" 2>>"$LOG"; then
        log "FAILED: RAM GUI bind mount failed."
        return 1
    fi
    GUI_BOUND_MD5=$(file_md5 "$GUI_TARGET")
    log "GUI_RUNTIME_PROOF: target=$GUI_TARGET md5=$GUI_BOUND_MD5 expected=$GUI_RUNTIME_MD5"
    if [ "$GUI_BOUND_MD5" != "$GUI_RUNTIME_MD5" ]; then
        log "FAILED: Bound GUI hash does not match the payload."
        return 1
    fi
    log "SUCCESS: Custom GUI is bound from volatile RAM."
    return 0
}

log "=== XDJ-XZ Diagnostic Native Loader v3 (firmware 1.26) ==="
log "USB Mount Path: $USB"
trap cleanup_payload 0
trap 'exit 1' 1 2 15
if ! pin_own_payload; then
    log "FAILED: Could not pin this USB image; DJ process untouched."
    exit 1
fi
acquire_loader_lock || exit 1
stage_auto_bridge || { log "AUTO_BRIDGE: RAM staging failed; native runtime untouched."; exit 1; }
configure_vj_network
network_snapshot
usb_gadget_snapshot
start_telnet

if [ -f "$APPLIED_FLAG" ]; then
    CURRENT_PID=$(pidof rbp 2>/dev/null)
    REQUESTED_MODS=$(awk 'NR == 1 {print $1}' "$PAYLOAD_ROOT/tools/libxz-mods.so.md5" 2>/dev/null)
    REQUESTED_MODE=$(cat "$PAYLOAD_ROOT/mods-mode" 2>/dev/null)
    if [ -n "$CURRENT_PID" ] && [ -n "$REQUESTED_MODS" ] &&
       tr '\000' '\n' < "/proc/$CURRENT_PID/environ" | grep -q "^XZ_MODS_BOOT_RUNTIME=$REQUESTED_MODS:$REQUESTED_MODE$"; then
        log "Exact runtime already applied this boot: $REQUESTED_MODS"
        usb_coldplug_start "$CURRENT_PID"
        verify_running_rbp "$CURRENT_PID" "$(cat "$APPLIED_FLAG" 2>/dev/null)"
        sync
        exit 0
    fi
    log "A different runtime was applied earlier; updating to this payload."
fi

if [ ! -f "$SOURCE_RBP" ]; then
    log "FAILED: $SOURCE_RBP is missing from the mounted payload."
    rm -f "$APPLYING_FLAG"
    sync
    exit 1
fi

SOURCE_MD5=$(file_md5 "$SOURCE_RBP")
EXPECTED_MD5=$(awk 'NR == 1 {print $1}' "$EXPECTED_MD5_FILE" 2>/dev/null)
log "Payload rbp: bytes=$(wc -c < "$SOURCE_RBP" 2>/dev/null) md5=$SOURCE_MD5 expected=$EXPECTED_MD5"

if [ -n "$EXPECTED_MD5" ] && [ "$SOURCE_MD5" != "$EXPECTED_MD5" ]; then
    log "FAILED: Mounted payload hash does not match its build manifest."
    rm -f "$APPLYING_FLAG"
    sync
    exit 1
fi

# Copy to a new inode and atomically rename it over the live executable. This
# avoids ETXTBSY behavior when replacing a currently mapped ELF.
rm -f "$RBP_NEXT"
if ! cp "$SOURCE_RBP" "$RBP_NEXT" 2>>"$LOG"; then
    log "FAILED: Could not stage patched rbp in tmpfs."
    rm -f "$APPLYING_FLAG"
    sync
    exit 1
fi
chmod 755 "$RBP_NEXT"

STAGED_MD5=$(file_md5 "$RBP_NEXT")
if [ "$STAGED_MD5" != "$SOURCE_MD5" ]; then
    log "FAILED: Staged rbp hash mismatch ($STAGED_MD5 != $SOURCE_MD5)."
    rm -f "$RBP_NEXT" "$APPLYING_FLAG"
    sync
    exit 1
fi

if ! mv -f "$RBP_NEXT" "$RBP" 2>>"$LOG"; then
    log "FAILED: Atomic rbp replacement failed."
    rm -f "$RBP_NEXT" "$APPLYING_FLAG"
    sync
    exit 1
fi

INSTALLED_MD5=$(file_md5 "$RBP")
log "Installed rbp in tmpfs: md5=$INSTALLED_MD5"
if [ "$INSTALLED_MD5" != "$SOURCE_MD5" ]; then
    log "FAILED: Installed rbp hash mismatch."
    rm -f "$APPLYING_FLAG"
    sync
    exit 1
fi

if ! stage_optional_assets; then
    rm -f "$APPLYING_FLAG"
    sync
    exit 1
fi

OTA_TRIAL=0
if false; then # XZ Mods: selected version is pinned; skip OTA apply
    if LD_PRELOAD= /dev/shm/xz-updater apply "$USB" >>"$LOG" 2>&1; then
        OTA_TRIAL=1
        log "OTA_TRIAL_READY: verified libraries staged; checking the DJ process before acceptance."
    else
        log "OTA_BASE_OR_RECOVERY: continuing with the USB runtime."
    fi
fi

finish_ota_trial() {
    trial_pid="$1"
    if [ "$OTA_TRIAL" = 1 ]; then
        if verify_running_rbp "$trial_pid" "$SOURCE_MD5" &&
           LD_PRELOAD= /dev/shm/xz-updater accept "$USB" "$trial_pid" >>"$LOG" 2>&1; then
            log "OTA_ACCEPTED: new process, mappings, and RAM hashes verified."
        else
            log "OTA_ROLLBACK: trial failed; restoring the original USB runtime."
            LD_PRELOAD= /dev/shm/xz-updater reject "$USB" >>"$LOG" 2>&1
            [ -n "$trial_pid" ] && kill "$trial_pid" 2>/dev/null
            ota_wait=0
            while kill -0 "$trial_pid" 2>/dev/null && [ "$ota_wait" -lt 5 ]; do sleep 1; ota_wait=$((ota_wait+1)); done
            if kill -0 "$trial_pid" 2>/dev/null; then kill -KILL "$trial_pid" 2>/dev/null; fi
            launch_runtime_rbp
            sleep 5
            configure_vj_network
        fi
    fi
}

OLD_PID=$(pidof rbp 2>/dev/null)
if [ -z "$OLD_PID" ]; then
    log "Early boot: patched rbp installed before apl_start. A delayed verifier is armed."
    echo "$SOURCE_MD5" > "$APPLIED_FLAG"
    rm -f "$APPLYING_FLAG"

    (
        cd /
        sleep 25
        DELAYED_PID=$(pidof rbp 2>/dev/null)
        log "Delayed boot probe found rbp PID(s): $DELAYED_PID"
        if [ -f "$HOOK_RAM" ]; then
            log "Restarting early-boot rbp once so the RAM-only DirectFB hook is loaded."
            [ -n "$DELAYED_PID" ] && killall -9 rbp 2>/dev/null
            sleep 1
    if launch_runtime_rbp; then
        DELAYED_PID="$RBP_LAUNCH_PID"
        sleep 5
        configure_vj_network
            else
                DELAYED_PID=""
            fi
        fi
        usb_coldplug_start "$DELAYED_PID"
        verify_running_rbp "$DELAYED_PID" "$SOURCE_MD5"
        finish_ota_trial "$DELAYED_PID"
        network_snapshot
        sync
    ) &

    sync
    exit 0
fi

log "Live insertion: replacing running rbp PID(s) $OLD_PID."
killall -9 rbp 2>/dev/null
sleep 1

if ! launch_runtime_rbp; then
    log "FAILED: Could not launch patched rbp."
    rm -f "$APPLYING_FLAG"
    sync
    exit 1
fi
NEW_PID="$RBP_LAUNCH_PID"

sleep 5
configure_vj_network
if verify_running_rbp "$NEW_PID" "$SOURCE_MD5"; then
    finish_ota_trial "$NEW_PID"
    echo "$SOURCE_MD5" > "$APPLIED_FLAG"
    STATUS=0
else
    STATUS=1
fi
rm -f "$APPLYING_FLAG"
network_snapshot
log "=== XDJ-XZ Diagnostic Native Loader Complete (status $STATUS) ==="
sync
exit "$STATUS"
