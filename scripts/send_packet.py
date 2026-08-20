#!/usr/bin/env python3
"""
Send one fuzz packet then a termination packet (0xFF).
Usage: python3 send_packet.py <op_hex> <filename> <datafile>
Ops:  01=write 02=read 03=mkdir 04=rename 05=unlink
      06=stat  07=truncate 08=mount_raw FF=exit
"""
import sys, struct

op   = int(sys.argv[1], 16)
name = sys.argv[2].encode()[:32]
data = open(sys.argv[3], 'rb').read()[:4096] if len(sys.argv) > 3 else b''

# main packet
pkt  = bytes([op, len(name)])
pkt += struct.pack('>H', len(data))
pkt += name
pkt += data

# termination packet — tells firmware to exit cleanly
term = bytes([0xFF, 0, 0, 0])

sys.stdout.buffer.write(pkt + term)
sys.stdout.buffer.flush()
