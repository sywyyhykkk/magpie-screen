"""Line-delimited JSON bridge. Credentials travel in memory; never log commands."""
import json, os, select, sys, time
import serial
from serial.tools import list_ports
port = None
last_scan = 0
while True:
    if port is None and time.time() - last_scan > 2:
        last_scan = time.time()
        candidates = [p.device for p in list_ports.comports() if p.vid == 0x303a and p.pid == 0x1001]
        if len(candidates) != 1: candidates = []
        if candidates:
            try:
                port = serial.Serial(candidates[0], 115200, timeout=0, write_timeout=2)
                buffer = b''
            except serial.SerialException:
                pass
    ready, _, _ = select.select([sys.stdin], [], [], .01)
    if ready:
        chunk = os.read(sys.stdin.fileno(), 65536)
        if not chunk: break
        if port:
            try: port.write(chunk)
            except (OSError, serial.SerialException):
                port.close(); port = None
    if port:
        try:
            buffer += port.read(16384)
            while b'\n' in buffer:
                line, buffer = buffer.split(b'\n', 1)
                try:
                    message = json.loads(line)
                    if isinstance(message, dict):
                        print(json.dumps(message), flush=True)
                except (ValueError, UnicodeError): pass
            if len(buffer) > 65536: buffer = b''
        except (OSError, serial.SerialException):
            port.close(); port = None
