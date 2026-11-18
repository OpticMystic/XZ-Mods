#!/bin/sh
# Run manually through the diagnostic shell. Writes only stock proc notices.
echo '--- mounts before ---'
cat /proc/mounts
pid=$(pidof rbp)
case "$pid" in ''|*[!0-9]*) echo 'Expected exactly one running process'; exit 1;; esac
birth=$(sed 's/.*) //' "/proc/$pid/stat" | awk '{print $20}')
attempt=0
while [ "$attempt" -lt 10 ]; do
    attempt=$((attempt + 1))
    current=$(sed 's/.*) //' "/proc/$pid/stat" 2>/dev/null | awk '{print $20}')
    [ -n "$birth" ] && [ "$current" = "$birth" ] || exit 1
    ready=$(ls -l "/proc/$pid/fd" 2>/dev/null | awk '
        $NF == "/proc/udev_usb1" {print "usb1"}
        $NF == "/proc/udev_usb2" {print "usb2"}' | sort -u | tr '\n' ' ')
    echo "USB_COLDPLUG_PROBE: pid=$pid attempt=$attempt ready=[$ready]"
    case " $ready " in *' usb1 usb2 '*) break;; esac
    sleep 1
done
case " $ready " in *' usb1 usb2 '*) ;; *) echo 'Listener not ready; no notices sent'; exit 1;; esac
sort -u /proc/mounts | while read -r source target rest; do
    dev=${source#/dev/}
    case "$source" in /dev/sd*) ;; *) continue;; esac
    case "$dev" in *[!a-z0-9]*) continue;; esac
    case "$target" in
        /media/usb1/"$dev") slot=usb1;;
        /media/usb2/"$dev") slot=usb2;;
        *) continue;;
    esac
    [ -e "/proc/udev_$slot" ] || continue
    if printf 'mount %s' "$target" > "/proc/udev_$slot"; then
        echo "USB_COLDPLUG_PROBE: dev=$dev action=mount slot=$slot result=notified"
    else
        echo "USB_COLDPLUG_PROBE: dev=$dev action=mount slot=$slot result=write-failed"
    fi
done
sleep 2
echo '--- mounts after, expected unchanged ---'
cat /proc/mounts
echo 'Check Browse and both source slots on the display.'
