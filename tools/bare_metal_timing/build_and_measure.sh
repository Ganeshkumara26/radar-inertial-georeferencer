#!/bin/bash
# build_and_measure.sh — reproduces the Cortex-M7 instruction-count measurement
# reported in the paper (Section V-G): 9,277 vs 13,904 instructions per
# ekf_update_radar() call, simplified vs Joseph form.
#
# Requires: gcc-arm-none-eabi, qemu-system-arm (both installable via apt on
# Debian/Ubuntu: `apt-get install gcc-arm-none-eabi qemu-system-arm`)
#
# IMPORTANT: this measures REAL Cortex-M7 instructions via QEMU's cortex-m7
# CPU model, but on a GENERIC mps2-an500 reference platform, not a real
# STM32H753. See ../README.md section 4 for full caveats. QEMU's DWT cycle
# counter on this machine does not function (writes succeed, CYCCNT never
# increments) — this script uses QEMU's instruction trace instead, which is
# exact for instruction counts.

set -e
CFLAGS="-mcpu=cortex-m7 -mfloat-abi=hard -mfpu=fpv5-d16 -mthumb -O2 -ffreestanding -nostdlib -fno-builtin -I."

echo "=== Building simplified-form image ==="
arm-none-eabi-gcc $CFLAGS -c startup.c -o startup.o
arm-none-eabi-gcc $CFLAGS -c main.c -o main.o
arm-none-eabi-gcc $CFLAGS -c ekf_simplified.c -o ekf_simplified.o
arm-none-eabi-gcc $CFLAGS -T mps2_m7.ld -specs=rdimon.specs -o simplified.elf \
    startup.o main.o ekf_simplified.o \
    -Wl,--start-group -lc -lrdimon -lm -lgcc -Wl,--end-group

echo "=== Building Joseph-form image ==="
arm-none-eabi-gcc $CFLAGS -c ekf_joseph.c -o ekf_joseph.o
arm-none-eabi-gcc $CFLAGS -T mps2_m7.ld -specs=rdimon.specs -o joseph.elf \
    startup.o main.o ekf_joseph.o \
    -Wl,--start-group -lc -lrdimon -lm -lgcc -Wl,--end-group

echo "=== Sanity-running both images (should print STAGE lines and exit cleanly) ==="
timeout 5 qemu-system-arm -machine mps2-an500 -cpu cortex-m7 -nographic -semihosting -kernel simplified.elf
echo "---"
timeout 5 qemu-system-arm -machine mps2-an500 -cpu cortex-m7 -nographic -semihosting -kernel joseph.elf

echo ""
echo "=== Tracing simplified-form execution (this takes ~15-30s) ==="
timeout 30 qemu-system-arm -machine mps2-an500 -cpu cortex-m7 -nographic -semihosting \
    -kernel simplified.elf -singlestep -d exec -D trace_simplified.log
SIMPLIFIED_COUNT=$(awk '{print $NF}' trace_simplified.log | grep -c '^ekf_update_radar$')

echo "=== Tracing Joseph-form execution (this takes ~15-30s) ==="
timeout 30 qemu-system-arm -machine mps2-an500 -cpu cortex-m7 -nographic -semihosting \
    -kernel joseph.elf -singlestep -d exec -D trace_joseph.log
JOSEPH_COUNT=$(awk '{print $NF}' trace_joseph.log | grep -c '^ekf_update_radar$')

echo ""
echo "=== Results (101 calls: 1 warmup + 100 measured, all identically initialized) ==="
python3 -c "
s = $SIMPLIFIED_COUNT
j = $JOSEPH_COUNT
n = 101
print(f'Simplified form: {s} total instructions / {n} calls = {s/n:.1f} instr/call')
print(f'Joseph form:      {j} total instructions / {n} calls = {j/n:.1f} instr/call')
print(f'Delta: {(j-s)/n:.1f} instr/call ({(j-s)/s*100:.1f}% increase)')
"

echo ""
echo "Expected (from paper): simplified=9277.0, joseph=13904.0, delta=4627.0 (49.9%)"
