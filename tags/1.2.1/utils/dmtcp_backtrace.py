#!/usr/bin/python

import sys
import os
import subprocess

# Set defaults
host = subprocess.Popen("hostname", shell=True, stdout=subprocess.PIPE)
host = host.stdout.read().rstrip()
dmtcpTmpDir = "/tmp/dmtcp-" + os.environ['USER'] + '@' + host + '/'
(dmtcphijack, tmpBacktrace, tmpProcMaps) = \
  ('dmtcphijack.so', dmtcpTmpDir+'backtrace', dmtcpTmpDir+'proc-maps')

if len(sys.argv) > 1 and (sys.argv[1] == '--help' or sys.argv[1] == '-h'):
  print "USAGE:  dmtcp_backtrace.py [filename [backtrace [proc-maps]]]\n" \
  + "  Default:  filename = dmtcphijack.so\n" \
  + "            backtrace = " + tmpBacktrace + "\n" \
  + "            proc-maps = " + tmpProcMaps + "\n"
  sys.exit(1)

# Override defaults
if len(sys.argv) > 1:
  dmtcphijack = sys.argv[1]
if len(sys.argv) > 2:
  tmpBacktrace = sys.argv[2]
if len(sys.argv) > 3:
  tmpProcMaps = sys.argv[3]

# Expand dmtcphijack.so or other filename to fully qualified pathname
pathname = "CAN'T FIND FILE " + dmtcphijack
if dmtcphijack.find('/') == -1:
  for segment in open(tmpProcMaps).read().splitlines():
    if segment.split()[-1].find('/' + dmtcphijack) != -1:
      pathname = segment.split()[-1]
else:
  pathname = dmtcphijack
if pathname.find("CAN'T FIND FILE") != -1:
  print pathname
  print "Please check " + tmpProcMaps + " to see if the process that crashed"
  print "  was really using:  " + dmtcphijack
  sys.exit(1)
print "Examing stack for call frames from:\n  " + pathname + "\n" \
      + "FORMAT:  FNC: ..., followed by source code line number.\n"

def getOrigOffset(pathname,procMaps):
  # The text segment in memory must always start at a page boundary.
  # But the actual code from file may have started at a non-page boundary.
  # First, we get the start page boundary of .text as given by /proc/PID/maps
  textOffset = 0
  for segment in open(procMaps).read().splitlines():
    if segment.split()[-1].find(pathname) != -1:
      textOffset = '0x' + segment.partition('-')[0]
      break
  if textOffset == 0:
    print pathname + " not found in proc maps: " + procMaps
  # Now we get the offset of the text section in the file.  When the text
  #   section was mapped to memory, in fact all the program header table
  #   and all the sections preceding .text were mapped in.
  #   So, now we need to include the file offset in our calculation.
  fileOffset = subprocess.Popen("objdump -h " + pathname + " | grep .text",
                              shell=True, stdout=subprocess.PIPE)
  # file offset col. of objdump outp
  fileOffset = fileOffset.stdout.read().split()[5] 
  return int(textOffset,16) + int(fileOffset,16)

# Now call addr2line on each call frame:
addr2line = "addr2line -f -C -i -j .text -e " + pathname + " " # + offeset
origOffset = getOrigOffset(pathname, tmpProcMaps)
backtrace = open(tmpBacktrace).read().splitlines()
for callFrame in backtrace:
  if (callFrame.find(dmtcphijack) != -1):  # CHECK THIS
    offset = callFrame.rpartition('[')[2].partition(']')[0]
    hexOffset = hex( int(offset,16) - origOffset ) # returns hex str
    if hexOffset[0] == '-':
      # This happens because backtrace() can ascribe to dmtcphijack
      #  what came from /lib/ld-2.10.1.so
      print callFrame
    else: # This subprocess prints to stdout
      # print callFrame
      # print addr2line + hexOffset
      print "** FNC: ",
      sys.stdout.flush()
      subprocess.call(addr2line + hexOffset, shell=True)
  else:
    print callFrame

# That's it.  We're done.
