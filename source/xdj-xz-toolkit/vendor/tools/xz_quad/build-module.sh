#!/bin/bash
# Offline only. All outputs, including kernel rebuilds, stay in this artifact.
set -euo pipefail
cd "$(dirname "$0")"
artifact=$PWD
mkdir -p "$artifact/tmp"
export TMPDIR="$artifact/tmp"
kernel=$artifact/kernel
out=$artifact/module
mkdir -p "$out"
test "$(arm-linux-gnueabi-gcc -dumpversion | cut -d. -f1)" = 13
cp "$kernel/include/linux/compiler-gcc4.h" "$kernel/include/linux/compiler-gcc13.h"
args=(-C "$kernel" ARCH=arm CROSS_COMPILE=arm-linux-gnueabi-
    HOSTCFLAGS=-fcommon KCFLAGS=-fno-pie LOCALVERSION=-svn2808)
objects=(kernel/module.o kernel/mutex.o kernel/sched.o kernel/params.o
    kernel/printk.o kernel/workqueue.o kernel/timer.o lib/string.o mm/percpu.o
    drivers/usb/core/usb.o drivers/usb/core/notify.o drivers/usb/core/hcd.o
    drivers/base/core.o arch/arm/kernel/unwind.o)
{
    arm-linux-gnueabi-gcc --version
    arm-linux-gnueabi-ld --version
    gcc --version
    make --version
} > "$out/toolchain.txt"
make "${args[@]}" modules_prepare
# Force recompilation: no retained exporting object or prebuilt symvers is
# allowed to supply version data. Remove only exact output files in our copy.
for obj in "${objects[@]}"; do rm -f "$kernel/$obj"; done
make "${args[@]}" -j4 "${objects[@]}"
python3 - "$kernel" "$out" "${objects[@]}" <<'PY'
import hashlib,json,pathlib,re,subprocess,sys
kernel,out=map(pathlib.Path,sys.argv[1:3])
crcs={};objects={}
for obj in sys.argv[3:]:
    objects[obj]=hashlib.sha256((kernel/obj).read_bytes()).hexdigest()
    symbols=subprocess.check_output(['arm-linux-gnueabi-nm',str(kernel/obj)],text=True)
    for crc,name in re.findall(r'^([0-9a-f]{8}) A __crc_(\w+)$',symbols,re.M):
        if name in crcs and crcs[name]!=crc:raise ValueError('conflicting source CRC '+name)
        crcs[name]=crc
assert crcs.get('usb_add_hcd') == '6b15adde', 'retained HCD type CRC mismatch'
text=''.join(f'0x{crc}\t{name}\tvmlinux\tEXPORT_SYMBOL_GPL\n' for name,crc in sorted(crcs.items()))
(out/'rebuilt.symvers').write_text(text)
(kernel/'Module.symvers').write_text(text)
(out/'rebuilt-crcs.json').write_text(json.dumps(crcs,indent=2)+'\n')
(out/'exporting-objects.json').write_text(json.dumps(objects,indent=2)+'\n')
PY
cp xz_hub_depth.c Makefile "$out/"
make "${args[@]}" M="$out" modules
arm-linux-gnueabi-nm -u "$out/xz_hub_depth.ko" > "$out/imports.txt"
arm-linux-gnueabi-readelf -p .modinfo "$out/xz_hub_depth.ko" > "$out/modinfo.txt"
arm-linux-gnueabi-readelf -x __versions "$out/xz_hub_depth.ko" > "$out/versions.txt"
python3 verify-imports.py
