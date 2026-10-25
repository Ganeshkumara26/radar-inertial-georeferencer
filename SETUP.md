# SETUP: ESP32 + Raspberry Pi 4 + Pixhawk 2.4.6 + NUCLEO-F070RB as one pipeline

```
ESP32 (radar-sensor node) --Wi-Fi UDP--> Raspberry Pi 4 (gateway)  --USB serial--> NUCLEO-F070RB (EKF + geo node)
 msglayer frames, t_gen                  netem link shaping, msglayer rx,          original bench firmware, HIL mode
                                          attitude interpolation, CSV log     <--USB-- Pixhawk 2.4.6 (real MAVLink ATTITUDE)
```

## 1. Components

| Item | Notes |
|---|---|
| NUCLEO-F070RB + USB-A to Mini-B cable | already working; keep the ORIGINAL firmware (`hw_bench/prebuilt/*.bin`) |
| Pixhawk 2.4.6 (Pixhawk-1 class, STM32F427) + micro-USB cable | ArduPilot Copter 4.5.7 already flashed. **Bench only, propellers OFF.** |
| Raspberry Pi 4B, power supply, microSD (Raspberry Pi OS), Wi-Fi or Ethernet | gateway + link shaper |
| ESP32 dev board + micro-USB cable | sensor node; Wi-Fi to the Pi |
| Optional: logic analyser (2+ ch), jumper wires | independent timing check |
| USB hub (powered) if the Pi has to power both Nucleo and Pixhawk | prefer a powered hub |

## 2. Connections (nothing else is wired)

| From | To | How |
|---|---|---|
| ESP32 | Pi | Wi-Fi (same network), UDP port 5005. No wire. |
| Pixhawk | Pi | micro-USB to a USB-A port. Appears as /dev/ttyACM0 (Linux) or COM port. MAVLink is on this USB port by default. |
| Nucleo F070RB | Pi | USB-A to Mini-B into the ST-LINK port. Serial = ST-LINK virtual COM port (/dev/ttyACM1, COMx). |
| Logic analyser (optional) | ESP32 GPIO4 (send mark), Nucleo D2 (PA10), common GND | 3.3 V logic on all boards. |

All boards are 3.3 V logic; no level shifters. UART pins are NOT used between boards: the ST-LINK already owns the Nucleo's serial pins.

## 3. Software install (PC and Pi)

`pip install pyserial pymavlink numpy pytest` (Pi: `sudo apt install python3-pip` first). Unzip this project, open a terminal in it.

## 4. Bring-up in order. Do not skip ahead; each phase must pass first.

**Phase 0.** `python -m uavpipe.selftest` -> must print ALL PASS. `python -m uavpipe.sim --out results` reproduces the sweep (`results/sweep.md`).

**Phase 1, Nucleo only.** Flash the prebuilt firmware (`hw_bench/prebuilt/full.bin`, drag the .bin onto NODE_F070RB or flash via ST-LINK). Press RESET so it is not stuck in HIL mode, then:
`python -m uavpipe.doctor` (find the Nucleo port), then
`python -m uavpipe.live --sensor emulate --pixhawk synthetic --estimator nucleo:COM5 --hz 50 --seconds 10`
Expect: 200 results, final position error 0.1236 m (check `results/live/live_log.csv` x0..x2 against `data/truth.csv`), gateway latency about 14 ms or more because the board needs ~12-14 ms per frame. **Keep --hz at 50 or below**: the board cannot sustain 100 Hz. Press RESET on the Nucleo between runs (HIL mode lasts until reset).

**Phase 2, add the Pixhawk (bench, props off).** Connect USB, then `python -m uavpipe.doctor` (it should report a MAVLink heartbeat).
`python -m uavpipe.live --sensor emulate --pixhawk COM7 --estimator nucleo:COM5 --hz 50 --seconds 20`
The gateway asks for ATTITUDE at 100 Hz (MAV_CMD_SET_MESSAGE_INTERVAL 511). If no attitude arrives, set the stream rate on the autopilot instead: ArduPilot `SR0_EXTRA1 = 50` (USB port), PX4 start the stream from the MAVLink shell. Tilt/rotate the board by hand and check `yaw/roll/pitch` columns change.

**Phase 3, add the ESP32 and the link.** Edit SSID/password/Pi IP in `firmware/esp32_sensor/esp32_sensor.ino`, upload with the Arduino IDE (ESP32 core). On the Pi: `python -m uavpipe.live --sensor udp --bind 0.0.0.0:5005 --pixhawk /dev/ttyACM0 --estimator nucleo:/dev/ttyACM1 --hz 50 --seconds 30 --policy deadline`.
Degrade the link on the Pi: `sudo scripts/netem_on.sh wlan0 "delay 20ms 10ms loss 5%"`; undo: `sudo scripts/netem_off.sh wlan0`.
The ESP32 sketch sends telemetry only (policies `raw`/`deadline`). For the `retx` policy (ACK + retries) run `python -m uavpipe.sensor_node --to <pi-ip>:5005 --policy retx` on a PC in place of the ESP32.

**Phase 4, measurement runs.** For each policy (raw, deadline, retx) and each netem profile (none, 5% loss, 20% loss, 20+-20 ms jitter), run 60 s and keep `results/live/live_log.csv` under a named folder (`--out results/<policy>_<profile>`). Compare with the sweep in `results/sweep.md`. Label every result "Wi-Fi link", never 5G.

## 5. What was verified

| Item | Status |
|---|---|
| msglayer reference model, CRC (0x6F91), framing, single-bit-flip rejection, fuzz, delivery ratio vs 1-p^(R+1), deadline rule | tested (11 tests) |
| C core (`msglayer_core.h`) wire-identical to Python (1,500 packs, 900 valid/mutated/random buffers, ASan+UBSan) | tested on x86 |
| Python EKF vs the board's C golden (final error 0.1236 m, states within 5e-3) | tested |
| Nucleo bench (`out/board/` reports): EKF timing, HIL delivery, HIL-vs-host mismatch, clock-map rounding | tested |
| Pipeline runs (results/live/*/live_log.csv): delivered ratio, latency/age percentiles, position error | tested via analyze_live.py |
| Board bench report and resume bullets | tested |

Known limits: clocks are mapped with a minimum-delay filter, so reported ages are "excess over the minimum observed delay" and the attitude/radar alignment uncertainty is about the difference in minimum path delays (milliseconds). The Nucleo firmware uses a fixed dt of 10 ms per radar frame; at 50 Hz the trajectory is replayed time-dilated 2x.
