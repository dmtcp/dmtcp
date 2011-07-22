#!/usr/bin/python
# This file is a hack, meant to overcome the disabling of the logic for
#   'add-symbol-file libmtcp.so' in mtcp/mtcp_restart.c
# If that logic is restored, this file should be deleted. 
# Without this file or that logic, it is almost impossible to use
#   gdb in debugging mtcp_restart.c and the related functions.

import os
import sys
import re
import subprocess

print "Usage:  "+sys.argv[0]+" <PID> <ADDR> [<LIBMTCP=mtcp/libmtcp.so>]"
print "  This assumes that ADDR is in libmtcp.so."
# print "OR Usage:  "+sys.argv[0]+" <PID> <LIB>"

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

libmtcp = "/home/gene/dmtcp-vanilla/mtcp/libmtcp.so"

file = open("/proc/"+str(pid)+"/maps")
for line in file:
  fields = line.split()[0:2]
  fields = map(lambda(x):int(x,16), fields[0].split("-")) + [fields[1]]
  if fields[0] < pc and pc < fields[1] and re.match("r.x.", fields[2]):
    print int(fields[0])
    readelf = subprocess.Popen(
        "/usr/bin/readelf -S "+libmtcp+" | grep '\.text'",
        shell=True, stdout=subprocess.PIPE)
    textOffset = readelf.stdout.read().split()[4]
    readelf.stdout.close()
    print textOffset
    readelf.stdout.close()
    gdbCmd = "add-symbol-file "+libmtcp+" "+hex(fields[0]+int(textOffset,16))
    print gdbCmd
    break
file.close()
sys.exit(0)

# This code not ready.
file = open("/proc/"+str(pid)+"/maps")
for line in file:
  fields = line.split()[0:2]
  fields = map(lambda(x):int(x,16), fields[0].split("-")) + [fields[1]]
  # ACTUALLY, THIS SHOULD BE LINE AFTER text SEGMENT
  if fields[0] < pc and pc < fields[1] and fields[2] == "rw-p":
    print int(fields[0])
    readelf = subprocess.Popen(
        "/usr/bin/readelf -S "+libmtcp+" | grep '\.data'",
        shell=True, stdout=subprocess.PIPE)
    dataOffset = readelf.stdout.read().split()[4]
    readelf.stdout.close()
    print dataOffset
    readelf.stdout.close()
    gdbCmd += " -s "+hex(fields[0]+int(dataOffset,16))
    print gdbCmd
    break
file.close()

# AND COULD DO SAME FOR .bss
