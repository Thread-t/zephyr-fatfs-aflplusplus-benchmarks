#!/usr/bin/env python3
"""
Convert a raw AFL input file into a Zephyr shell command sequence.
Usage: python3 input_to_shell_cmd.py <operation> <input_file>

Operations:
  write   → fs write /RAM:/<name> 0 <hex_data>
  read    → fs read /RAM:/<name>
  mkdir   → fs mkdir /RAM:/<name>
  rm      → fs rm /RAM:/<name>
  stat    → fs statvfs /RAM:
  ls      → fs ls /RAM:
  cp      → fs cp /RAM:/<src> /RAM:/<dst>
"""
import sys, os

op        = sys.argv[1]
data      = open(sys.argv[2], 'rb').read()

# first byte controls filename length, next bytes are the name
name_len  = max(1, (data[0] % 12) + 1) if len(data) > 0 else 4
raw_name  = data[1:1+name_len] if len(data) > name_len else b'fuzz'

# sanitize name: keep only printable ASCII, no slashes or spaces
name = ''.join(
    chr(b) if (32 < b < 127 and chr(b) not in '/\\ \t\n\r:*?"<>|') else 'x'
    for b in raw_name
) or 'fuzz'
name = name[:8]  # FAT 8.3 limit

payload   = data[1+name_len:]
hex_data  = payload.hex()[:32] or '41'  # cap at 256 bytes hex, default 'A'

cmds = []

if op == 'write':
    cmds.append(f'fs write /RAM:/{name}.txt 0 {hex_data}')
    cmds.append(f'fs read /RAM:/{name}.txt')   # read back what we wrote

elif op == 'read':
    # write something first so read has data to parse
    cmds.append(f'fs write /RAM:/{name}.txt 0 {hex_data}')
    cmds.append(f'fs read /RAM:/{name}.txt')

elif op == 'mkdir':
    cmds.append(f'fs mkdir /RAM:/{name}')
    cmds.append(f'fs ls /RAM:')

elif op == 'rm':
    cmds.append(f'fs write /RAM:/{name}.txt 0 {hex_data}')
    cmds.append(f'fs rm /RAM:/{name}.txt')
    cmds.append(f'fs ls /RAM:')

elif op == 'stat':
    cmds.append(f'fs statvfs /RAM:')
    cmds.append(f'fs ls /RAM:')

elif op == 'ls':
    cmds.append(f'fs ls /RAM:')

elif op == 'cp':
    dst_name = name[:4] + 'dst'
    cmds.append(f'fs write /RAM:/{name}.txt 0 {hex_data}')
    cmds.append(f'fs cp /RAM:/{name}.txt /RAM:/{dst_name}.txt')

# send commands to stdout, each followed by newline (shell expects Enter)
for cmd in cmds:
    print(cmd)

# give the shell time to process before QEMU is killed
import time
sys.stdout.flush()
