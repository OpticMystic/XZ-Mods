"""Lists syscall numbers loaded into r7 before each `svc` in an ARM objdump -d listing.
Usage: arm-linux-gnueabi-objdump -d BIN | python3 -I syscall_sites.py"""
import re
import sys

NAMES = {}
for line in open("/usr/arm-linux-gnueabi/include/asm/unistd-eabi.h", encoding="utf8"):
    m = re.match(r"#define __NR_(\w+)\s+\(__NR_SYSCALL_BASE\s*\+\s*(\d+)\)", line)
    if m:
        NAMES[int(m.group(2))] = m.group(1)

func, window, found, unknown = "?", [], {}, []
for line in sys.stdin:
    m = re.match(r"^[0-9a-f]+ <(.+)>:", line)
    if m:
        func, window = m.group(1), []
        continue
    parts = line.split("\t")
    if len(parts) < 3:
        continue
    ins = (parts[2] + " " + (parts[3] if len(parts) > 3 else "")).strip()
    if ins.startswith("svc"):
        nr = None
        for prev in reversed(window[-12:]):
            m = re.match(r"(?:mov|movw|movs)\s+r7,\s*#(\d+)", prev)
            if m:
                nr = int(m.group(1))
                break
            if re.match(r"\w+\s+r7,", prev):
                break
        if nr is None:
            unknown.append(func)
        else:
            found.setdefault(nr, set()).add(func)
    window.append(ins)

for nr in sorted(found):
    print(f"{nr:4d} {NAMES.get(nr, '?'):24s} {', '.join(sorted(found[nr]))[:110]}")
print("svc sites with r7 set from a register:", ", ".join(sorted(set(unknown))))
