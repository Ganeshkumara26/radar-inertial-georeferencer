"""List serial ports and identify which is the Nucleo (ST-LINK VCP) and which is the Pixhawk (MAVLink heartbeat). Run first on the Pi/PC.
  python -m uavpipe.doctor"""
import sys, time
def main():
    from serial.tools import list_ports
    ports = list(list_ports.comports())
    if not ports: print('No serial ports found. Check USB cables / drivers (ST-LINK driver on Windows).'); return 1
    for p in ports:
        vid = p.vid or 0; d = (p.description or '') + ' ' + (p.manufacturer or '') + ' ' + (p.product or '')
        guess = 'Nucleo F070RB (ST-LINK VCP)' if vid == 0x0483 else 'probably Pixhawk/ArduPilot/PX4' if (vid in (0x26AC, 0x2DAE, 0x1209, 0x3162) or any(k in d for k in ('PX4', 'ArduPilot', 'Pixhawk', 'FMU', '3D Robotics'))) else 'unknown'
        print(f'{p.device:14s} vid={vid:#06x} pid={(p.pid or 0):#06x}  {d.strip()[:60]:60s} -> {guess}')
    print('\nProbing for a MAVLink heartbeat (3 s per candidate)...')
    try: from pymavlink import mavutil
    except ImportError: print('pymavlink missing: pip install pymavlink'); return 0
    for p in ports:
        if (p.vid or 0) == 0x0483: continue
        try:
            m = mavutil.mavlink_connection(p.device, baud=115200); hb = m.wait_heartbeat(timeout=3)
            print(f'{p.device}: MAVLink heartbeat from system {m.target_system} (type {hb.type}, autopilot {hb.autopilot})' if hb else f'{p.device}: no heartbeat'); m.close()
        except Exception as e: print(f'{p.device}: {e}')
    return 0
if __name__ == '__main__': sys.exit(main())
