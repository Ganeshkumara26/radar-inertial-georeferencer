# Devlog: Version 8 (Fault Resilience & Adversarial Packets)

## What I'm Trying to Do
With the full architecture (DMA, Hardware CRC, RTOS Queues) established, the pipeline is fully functional under ideal conditions. However, aerospace telemetry systems operate in highly noisy RF environments where packets can suffer severe bit-flips despite CRC checks (or malicious actors could attempt adversarial packet injection).
My goal in `v8` is to stress-test the RTOS parsing tasks against deliberately malformed telemetry frames to ensure the system drops invalid data without triggering a hard reset or executing arbitrary code.

## Attempt 1: The Stack Smashing Trap
I created an adversarial `mavlink_packet_t` where the `len` (payload length) field was deliberately corrupted to declare `200` bytes, even though the parsing task only allocated a 32-byte local stack buffer (`uint8_t secure_payload_buffer[32]`) for orientation data.

The parser task blindly trusted the header length and invoked a memory copy routine to extract the payload.

```bash
make clean && make simulate
```
### The Output (Fatal Hardware Fault)
```text
--- [EDP v8_fault_resilience] Adversarial Packet Stress Test ---
[MAIN] Passing adversarial packet to parser...
[PARSER] Extracting payload from packet...
[PARSER] Payload successfully extracted into stack buffer.
```
Notice what is critically missing? The function never returned to `main()`. 
Because the ARM Cortex-M7 stack grows downwards in memory, sequentially copying 200 bytes into a 32-byte local buffer wrote *upwards* into the caller's stack frame. This obliterated the saved Link Register (`LR`), replacing the valid Return Address with our adversarial payload bytes (`0xDEDEDEDE`). When the parser function finished and attempted to branch back to `main()`, the CPU jumped to an invalid, non-executable memory region, triggering an immediate and unrecoverable `UsageFault` / `HardFault`. 

## Attempt 2: Defensive Length Verification
To guarantee resilience, the firmware cannot trust *any* data originating from the RF interface. I implemented a strict, hardware-bound length verification check *before* initiating memory operations:
```c
if (packet->len > sizeof(secure_payload_buffer)) {
    usart1_print("[PARSER] SECURITY FAULT: Adversarial packet length exceeds buffer!\r\n");
    return;
}
```

### The Output (System Survived)
```text
--- [EDP v8_fault_resilience] Adversarial Packet Stress Test ---
[MAIN] Passing adversarial packet to parser...
[PARSER] Extracting payload from packet...
[PARSER] SECURITY FAULT: Adversarial packet length exceeds buffer!
[PARSER] Packet dropped safely. System resilient.
[MAIN] System survived! Executing next cycle.
```
The adversarial packet was caught and safely dropped. The system gracefully returned to the main execution loop, guaranteeing structural integrity against buffer overflow attacks and RF corruption. Proceeding to `v9_power_wfi`.
