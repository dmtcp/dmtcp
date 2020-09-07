#!/usr/bin/env python

# Usage:  python THIS_FILE PID > GDB_SCRIPT
#    OR:  THIS_FILE PID > GDB_SCRIPT
#         Invoke this on a running process, prior to checkpoint.
#         This creates a GDB_SCRIPT file that can restore the debugging
#            information during restart.  On restart, do:
#         (gdb) source GDB_SCRIPT

import sys
import subprocess
import re
import os

def is_executable(filename):
  # 16 bytes for ELF magic number; then 2 bytes (short) for ELF type
  header = open(filename, "rb")
  elf_magic_number = header.read(16)
  elf_type = header.read(2)
  # Is it little-endian or big-endian
  elf_type = elf_type[0] if sys.byteorder == "little" else elf_type[1]
  # Handle both Python2.7 and Python3: type 2 is executable; type 3 is .so file
  elf_type = elf_type if isinstance(elf_type, int) else ord(elf_type)
  return elf_type == 2

# FROM: https://stackoverflow.com/questions/33049201/gdb-add-symbol-file-all-sections-and-load-address
def relocatesections(filename):
  p = subprocess.Popen(["readelf", "-S", filename], stdout = subprocess.PIPE)
  sections = []
  textaddr = '0'
  for line in p.stdout.readlines():
    line = line.decode("utf-8").strip()
    if not line.startswith('['):
      continue
    if line.startswith('[ 0]') or line.startswith('[Nr]'):
      continue
    line = line.replace("[ ", "[", 1)

    fieldsvalue = line.split()
    fieldsname = ['number', 'name', 'type', 'addr', 'offset', 'size',
                  'entsize', 'flags', 'link', 'info', 'addralign']
    sec = dict(zip(fieldsname, fieldsvalue))
    if not sec['name'].startswith("."):
      continue
    if ".note" in sec['name']:
      continue
    sections.append(sec)
    if sec['name'] == '.text':
      textaddr = sec['addr']
  return (textaddr, sections)


def writeSymbolFileToScript(filename_substring):
  (filename, base_addr) = memory_region(filename_substring)
  if is_executable(filename):
    base_addr = 0  # ELF executables already hard-wired absolute address
  (textaddr, sections) = relocatesections(filename)
  cmd = "add-symbol-file %s 0x%x" % (filename, int(textaddr, 16) + base_addr)
  for s in sections:
    addr = int(s['addr'], 16)
    if s['name'] == '.text' or addr == 0:
      continue
    cmd += " -s %s 0x%x" % (s['name'], addr + base_addr)
  print(cmd + "\n")


def saveSymbolFilesToGdbScript():
  if len(sys.argv) != 2:
    sys.stderr.write("Usage: %s PID > gdb_script_file\n" % sys.argv[0])
    sys.exit(1)
  procmaps_file = "/proc/" + getpid() + "/maps"
  if (not os.path.isfile(procmaps_file)):
    sys.stderr.write("No such file: " + procmaps_file + "\n")
    sys.exit(1)
  if (not os.access(procmaps_file, os.R_OK)):
    sys.stderr.write("No read permission on file: " + procmaps_file + "\n")
    sys.exit(1)

  print("# GDB script; Either 'gdb -x THIS_FILE' or: (gdb) source THIS_FILE\n")
  for (filename, _) in memory_regions():
    writeSymbolFileToScript(filename)

# Helper functions for writeSymbolFileToScript
def getpid():
  return sys.argv[1]

# This returns a pair: (FILENAME_OR_LIBNAME, ADDRESS)
def procmap_filename_address(line):
  return ("/"+line.split(" /")[-1], int("0x"+line.split("-")[0], 16))
def memory_regions():
  p = subprocess.Popen(["cat", "/proc/"+getpid()+"/maps"],
                       stdout = subprocess.PIPE)
  procmap_lines = [line.decode("utf-8").strip()
                   for line in p.stdout.readlines()]
  return [procmap_filename_address(memory) for memory in procmap_lines
                                           if " /" in memory and "r-x" in memory]

def memory_region(filename_substring):
  regions = memory_regions()
  return [region for region in regions if filename_substring in region[0]][0]

saveSymbolFilesToGdbScript()
