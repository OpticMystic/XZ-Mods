#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p tmp
export TMPDIR="$PWD/tmp"
ulimit -c 0
gcc -std=gnu11 -pthread -g -fsanitize=address,undefined -fno-omit-frame-pointer -no-pie -DBASELINE test_module.c -o test-module-baseline
set +e
./test-module-baseline 0 > baseline-red.log 2>&1
rc=$?
set -e
echo "RAW_EXIT=$rc" >> baseline-red.log
test "$rc" = 134
gcc -std=gnu11 -Wall -Wextra -Werror -pthread -g -fsanitize=address,undefined -fno-omit-frame-pointer -no-pie test_module.c -o test-module-host
arm-linux-gnueabi-gcc -std=gnu11 -Wall -Wextra -Werror -pthread -g -static test_module.c -o test-module-arm
for n in $(seq 0 27); do
    ./test-module-host "$n" > "module-host-$n.log" 2>&1
    qemu-arm ./test-module-arm "$n" > "module-arm-$n.log" 2>&1
    echo "PASS module host+ARM case=$n"
done
for n in $(seq 1 40); do
    ./test-module-host 22 > /dev/null
    ./test-module-host 23 > /dev/null
done
echo 'PASS 80 additional host concurrent ordering runs'
