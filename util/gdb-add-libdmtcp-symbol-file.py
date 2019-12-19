#!/usr/bin/env python
# This file is a hack, meant to overcome the disabling of the logic for
#   'add-symbol-file libdmtcp.so' in mtcp/mtcp_restart.c
# If that logic is restored, this file should be deleted. 
# Without this file or that logic, it is almost impossible to use
#   gdb in debugging mtcp_restart.c and the related functions.

import os
import sys
import re
import subprocess

print("Usage:  "+sys.argv[0]+" <PID> <ADDR> [<LIBMTCP=lib/dmtcp/libdmtcp.so>]")
print("  This assumes that ADDR is in libdmtcp.so.")
print("  In gdb: 'info proc' will provide <PID>; and  'print $pc' will")
print("    provide <ADDR>, providing that PC is currently in libdmtcp.so.")
# print("OR Usage:  "+sys.argv[0]+" <PID> <LIB>")

if len(sys.argv) == 1:
  sys.exit(0)

if len(sys.argv) <= 1:
  pid = 5702
else:
  pid = sys.argv[1]
# SHOULD DISTINGUISH PC (hex) OR LIBRARY
# COULD GREP FOR LIBRARY AND USE AS HEX IF LIBRARY NOT FOUND.
#  BUT SHOULD RECOGNIZE IF VALID HEX  try: int(x); exception ...
if len(sys.argv) <= 2:
  pc = 0x480000
else:
  pc = int(sys.argv[2], 0) # 0 means: guess the base
if len(sys.argv) <= 3:
  if os.getcwd().endswith("/src/mtcp"):
    libdmtcp = "../../lib/dmtcp/libdmtcp.so"
  else:
    libdmtcp = "lib/dmtcp/libdmtcp.so"
else:
  libdmtcp = sys.argv[3]

file = open("/proc/"+str(pid)+"/maps")
gdbCmd = ""
for line in file:
  fields = line.split()[0:2]
  fields = map(lambda(x):int(x,16), fields[0].split("-")) + [fields[1]]
  if fields[0] <= pc and pc < fields[1] and re.match("r.x.", fields[2]):
    readelf = subprocess.Popen(
        "/usr/bin/readelf -S "+libdmtcp+" | grep '\.text'",
        shell=True, stdout=subprocess.PIPE)
    tmp = readelf.stdout.read()
    textOffset = tmp.split()[4]
    readelf.stdout.close()
    readelf.stdout.close()
    gdbCmd = "add-symbol-file "+libdmtcp+" "+hex(fields[0]+int(textOffset,16))
    print("Type this in gdb:")
    print(gdbCmd)
    break
if not gdbCmd:
  print("Couldn't find libdmtcp.so in", "/proc/"+str(pid)+"/maps")
file.close()
sys.exit(0)

# This code not ready.
file = open("/proc/"+str(pid)+"/maps")
for line in file:
  fields = line.split()[0:2]
  fields = map(lambda(x):int(x,16), fields[0].split("-")) + [fields[1]]
  # ACTUALLY, THIS SHOULD BE LINE AFTER text SEGMENT
  if fields[0] < pc and pc < fields[1] and fields[2] == "rw-p":
    print(int(fields[0]))
    readelf = subprocess.Popen(
        "/usr/bin/readelf -S "+libnmtcp+" | grep '\.data'",
        shell=True, stdout=subprocess.PIPE)
    dataOffset = readelf.stdout.read().split()[4]
    readelf.stdout.close()
    print(dataOffset)
    readelf.stdout.close()
    gdbCmd += " -s "+hex(fields[0]+int(dataOffset,16))
    print(gdbCmd)
    break
file.close()

# AND COULD DO SAME FOR .bss
