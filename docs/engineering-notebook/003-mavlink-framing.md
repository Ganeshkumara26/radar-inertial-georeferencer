# Devlog: Version 2 (Byte-Stream Framing & The Struct Padding Trap)

## What I'm Trying to Do
Now that `v1_nvic_uart_isr` successfully buffers incoming characters asynchronously, I need to parse those raw byte streams into structured C types. The firmware must interpret incoming 20-byte binary packets (modeled after MAVLink v1/v2 orientation frames) containing a 6-byte header (`magic`, `length`, `seq`, `sysid`, `compid`, `msgid`) followed by three 32-bit floating-point payload fields (`roll`, `pitch`, `yaw`) and a 16-bit CRC checksum. 

A common, intuitive (but highly dangerous) embedded pattern is to accumulate incoming bytes into a linear `uint8_t` array, and once a full packet arrives, cast that buffer pointer directly to a C struct representing the message payload.

## Attempt 1: The Direct Struct Casting Trap
I defined `mavlink_attitude_t` as a standard C struct containing the 8-bit header fields sequentially followed by the 32-bit float payloads. In `USART1_IRQHandler`, we accumulate exactly 20 bytes from the UART. In `main()`, we overlay the struct onto the buffer (`mavlink_attitude_t *msg = (mavlink_attitude_t *)rx_buffer;`) and attempt to read the `msg->roll` floating point value.

Under simulation, Renode injects a perfectly valid 20-byte Little-Endian packet where the `roll` value is precisely `1.0f` (IEEE 754 Hex: `0x3F800000`, wire bytes: `0x00 0x00 0x80 0x3F` at offset 6).

```bash
make clean && make simulate
```

### The Output (Silent Data Corruption)
```text
--- [EDP v2_mavlink_framing] MAVLink Parser ---
[EVENT] 20-byte packet received.
[DEBUG] Expected Magic: 0xFD, Parsed: 0xFD
[DEBUG] Expected Roll Bits: 0x3F800000 (1.0f)
[DEBUG] Parsed Roll Bits  : 0x00003F80
[ERROR] Struct padding offset shift detected! Data is corrupted.
```

### My Mistake & Root Cause Analysis
The floating-point bits printed out completely corrupted (`0x00003F80` instead of `0x3F800000`). This wasn't a serial transmission error; this was a fundamental C compiler architectural shifting error.
Because the ARM Cortex-M7 is a 32-bit architecture, the GCC compiler automatically aligns 32-bit fields (like `float roll`) to 4-byte memory boundaries to prevent unaligned access penalties or hardware traps.
Our struct header is 6 bytes long (`magic` to `msgid`). The compiler silently injected **2 bytes of invisible padding** after `msgid` to push the `roll` field to offset 8.
When I cast the contiguous 20-byte wire protocol buffer onto the padded struct, the memory layouts didn't align. The struct expected `roll` to begin at byte 8 of the buffer, so it read the last two bytes of the transmitted roll data (`0x80`, `0x3F`) and the first two bytes of the transmitted pitch data (`0x00`, `0x00`), reading `0x00003F80` in little-endian space.

---

## Attempt 2: Explicit Compiler Packing Attributes
To fix this, I instructed the GCC compiler to eliminate structural padding by adding the `__attribute__((packed))` directive to the struct definition. This forces the memory offsets of the struct to perfectly match the contiguous layout of the serial byte stream, despite the unaligned penalty.

### The Output (Verified Execution)
```bash
$ make clean && make simulate
rm -rf build uart_output.log renode_trace.log
...
--- simulation completed, output logs: ---

--- [EDP v2_mavlink_framing] MAVLink Parser ---
[EVENT] 20-byte packet received.
[DEBUG] Expected Magic: 0xFD, Parsed: 0xFD
[DEBUG] Expected Roll Bits: 0x3F800000 (1.0f)
[DEBUG] Parsed Roll Bits  : 0x3F800000
[SUCCESS] Struct packing verified! Floating point payload decoded flawlessly.
```

### Final Result
The `float roll` payload parsed identically to the injected simulation bytes (`0x3F800000`), verifying that the C structure now perfectly overlays the incoming byte stream without shifting. Ready to transition to `v3_dma_coherency`.
