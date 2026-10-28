#!/bin/bash
set -euo pipefail
source_dir=$(cd "$(dirname "$0")" && pwd)
out=${1:?Supply a new scratch directory outside the source tree}
mkdir "$out"
cp "$source_dir/"{test_module.c,xz_hub_depth.c,port_layout.h,mock_kernel.h,module.baseline.mock.c} "$out/"
cd "$out"
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
for port in 1 2; do
for n in $(seq 0 70); do
    ./test-module-host "$n" "$port" > "module-host-$port-$n.log" 2>&1
    qemu-arm ./test-module-arm "$n" "$port" > "module-arm-$port-$n.log" 2>&1
    echo "PASS module host+ARM port=$port case=$n"
done
for n in $(seq 1 40); do
    ./test-module-host 22 "$port" > /dev/null
    ./test-module-host 23 "$port" > /dev/null
    ./test-module-host 63 "$port" > /dev/null
    ./test-module-host 64 "$port" > /dev/null
done
done
echo 'PASS 320 additional host concurrent ordering runs'
gcc -std=gnu11 -Wall -Wextra -Werror -pthread -g -fsanitize=thread -fno-pie -no-pie test_module.c -o test-module-tsan
for port in 1 2; do
for n in 22 23 24 25 27 60 63 64 65; do
    setarch "$(uname -m)" -R ./test-module-tsan "$n" "$port" > "module-tsan-$port-$n.log" 2>&1
done
done
echo 'PASS both ports under TSan for worker/remove/exit/init/replacement ordering'
