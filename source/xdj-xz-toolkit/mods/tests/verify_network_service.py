"""Network worker: the real service and the real Linux transaction logic against a recorded OS boundary, host
sanitizers. Apply, rollback, DHCP lease, renewal and expiry, and the periodic check that runs late on a busy unit
(it must not give the lease up: on the unit that restarted the DHCP client about once a minute under load)."""
import argparse, subprocess
from pathlib import Path
p = argparse.ArgumentParser()
p.add_argument("--out", required=True, type=Path)
a = p.parse_args()
a.out.mkdir(parents=True, exist_ok=True)
root = Path(__file__).resolve().parents[1]
def linux(path):
    s = str(path.resolve()).replace("\\", "/")
    return "/mnt/" + s[0].lower() + s[2:]
def run(*args):
    subprocess.run(["wsl", "-d", "Ubuntu", "--cd", linux(root), "--exec", *map(str, args)], check=True)
binary = a.out / "test-network-service"
# -std=c11, not gnu11: the test names a variable "linux". The wraps hold the worker to the monotonic clock.
run("gcc", "-std=c11", "-O1", "-g", "-UNDEBUG", "-pthread", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
    "-I.", "-Wl,--wrap=clock_gettime,--wrap=syscall", "network/tests/test_service.c", "network/service.c",
    "network/linux.c", "network/lease.c", "network/validate.c", "-o", linux(binary))
run(linux(binary))
print("PASS network service host fixtures; a late periodic check keeps the lease, an expired lease is given up")
