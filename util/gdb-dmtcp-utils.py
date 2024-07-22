#/*****************************************************************************
# * Copyright (C) 2020, 2022-2024 Gene Cooperman <gene@ccs.neu.edu>           *
# *                                                                           *
# * DMTCP is free software: you can redistribute it and/or                    *
# * modify it under the terms of the GNU Lesser General Public License as     *
# * published by the Free Software Foundation, either version 3 of the        *
# * License, or (at your option) any later version.                           *
# *                                                                           *
# * DMTCP is distributed in the hope that it will be useful,                  *
# * but WITHOUT ANY WARRANTY; without even the implied warranty of            *
# * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             *
# * GNU Lesser General Public License for more details.                       *
# *                                                                           *
# * You should have received a copy of the GNU Lesser General Public          *
# * License along with DMTCP.  If not, see <http://www.gnu.org/licenses/>.    *
# *****************************************************************************/

import subprocess
import re
import sys
import os
import textwrap

try:
  gdb.selected_thread()
except:
  print("\n*** USAGE:  source THIS_FILE  (from inside GDB)\n")
  sys.exit(1)

# This adds a GDB command:  add-symbol-files-all    (no arguments)
# To use it, either do:
#     gdb -x gdb-dmtcp-utils TARGET_PROGRAM PID
# OR:
#     gdb attach PID
#     (gdb) source gdb-dmtcp-utils
#       [ TIP: You can also add 'source gdb-dmtcp-utils' to ~/gdbinit ]
# Then you can try either of:
#     (gdb) add-symbol-files-all  # Better for older GDB versions
#     (gdb) load-symbols  # Better for newer GDB versions
#     (gdb) load-symbols-library ADDR_OR_FILE  # Better for newer GDB versions
#            [Deletes prev. symbols; add-symbols-files-all will no longer work]

# This also adds GDB commands:
#     (gdb) dmtcp [or DMTCP]  # Prints out a list of available GDB commands for DMTCP.
#     (gdb) procmaps
#     (gdb) procfd
#     (gdb) procfdinfo
#     (gdb) procenviron
#     (gdb) pstree
#     (gdb) lsof
#     (gdb) rlimit
#     (gdb) signals
#     (gdb) load-symbols [FILENAME]
#     (gdb) load-symbols-library [FILENAME_OR_ADDRESS]
#     (gdb) add-symbol-file-from-filename-and-address FILENAME ADDRESS
#         (Needed if FILENAME not listed in procmaps;
#          Better version of add-symbol-file; ADDRESS anywhere in memory range)
#     (gdb) show-filename-at-address MEMORY_ADDRESS (or whereis-address)
#     (gdb) add-symbol-file-from-substring FILENAME_SUBSTRING
#                                   (e.g., add-symbol-file-from-substring libc)
#     (gdb) add-symbol-file-at-address MEMORY_ADDRESS
#                                   (e.g., add-symbol-file-at-address 0x400000)
#     (gdb) ...
## To interactively test and modify these commands:  (gdb) python-interactive
## Executing GDB commands:  gdb.execute(), gdb.parse_and_eval()

dmtcp_commands = "load-symbols, load-symbols-library, " +\
  "add-symbol-files-all, add-symbol-file-from-filename-and-address, " +\
  "add-symbol-file-from-substring, add-symbol-file-at-address, " +\
  "show-filename-at-address (OR: whereis-address), " +\
  "procmaps, procfd, procfdinfo, procenviron, pstree, lsof, rlimit, signals"

def print_dmtcp_commands():
  print("GDB commands available for DMTCP:")
  print("  ** USAGE:  'dmtcp' for commands; 'help' on each command:\n" +
        "  **   (gdb) dmtcp  # Show this message\n" +
        "  **   (gdb) help <COMMAND>\n" +
        "  COMMANDS:")
  print(textwrap.fill(dmtcp_commands, break_on_hyphens=False,
                      initial_indent="    ", subsequent_indent="    "))

def is_recent_gdb():
  if not hasattr(is_recent_gdb, "value"):
    if "[-o OFF]" in gdb.execute("help symbol-file", False, True):
      is_recent_gdb.value = True
    else:
      is_recent_gdb.value = False
  return is_recent_gdb.value
# This should be used only for executabble binaries.
def load_symbols(exec_file=None):
  if not is_recent_gdb():
    print("Older GDB; use add-symbol-files-all (This GDB is version: " +
          gdb.VERSION + ")")
    return

  if exec_file:
    exec_files = [exec_file]
  else:
    exec_files = []
    for (filename, _, _, _) in memory_regions_executable():
      exec_files += [filename]
    exec_files = [filename for filename in exec_files
                           if is_exec_file(filename)]

  if len(exec_files) == 1:
    # Accept the first matching address even if not the text segment:
    exec_address = next(address
                        for (filename, address, _, _) in memory_regions()
                        if filename == exec_files[0])
    gdb.execute("file " + exec_files[0])
    gdb.execute("symbol-file -o " + str(exec_address) + " " + exec_files[0])
  elif len(exec_files) > 1:
    print("Multiple exec-files found: " + str(exec_files) + "\n")
    print("Call 'load-symbols EXEC_FILE' for the primary executable file.\n")
  else: # else len(exec_files) == 0
    print("No exec-files found\n")
def load_symbols_library(filename_or_address):
  if not is_recent_gdb():
    print("Older GDB; use add-symbol-file-from-substring (GDB is version: " +
          gdb.VERSION + ")")
    return
  filename = filename_or_address
  if cast_to_memory_address(filename) != None: # if this is a valid address:
    (filename, _, _, _) = memory_region_at_address(filename)
  # If filename is a substring, search for full filename in /proc/self/maps
  candidates = [(f, addr) for (f, addr, _, _) in memory_regions()
                          if filename in f]
  if candidates: # If we have a valid filename.
    (filename, start_addr) = candidates[0]
    if is_exec_file(filename):
      # ELF executables already have hard-wired absolute address
      print("EXECUTABLE FILE:")
      load_symbols(filename)
    else:
      gdb.execute("add-symbol-file -o " + str(start_addr) + " " + filename)
  else:
    print("No matching FILENAME_OR_ADDRESS found\n")


def is_exec_file(filename):
  if not os.path.exists(filename):
    print("**** path");print("\n")
    return False
  tmp = filename
  tmp = tmp[0 if "/" not in tmp else tmp.rindex("/")+1:] 
  if (tmp.startswith("lib") or tmp.startswith("ld")) and ".so" in tmp:
    return False  # This is a library
  return os.access(filename, os.X_OK)

  #### FIXME:  Remove the rest when this is working.
  # 16 bytes for ELF magic number; then 2 bytes (short) for ELF type
  header = open(filename, "rb")
  elf_magic_number = header.read(16)
  elf_type = header.read(2)
  # Is it little-endian or big-endian
  elf_type = elf_type[0] if sys.byteorder == "little" else elf_type[1]
  # Handle both Python2.7 and Python3: type 2 is executable; type 3 is .so file
  elf_type = elf_type if isinstance(elf_type, int) else ord(elf_type)
  header.read(6) # Skip next 6 bytes
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


class dmtcp(gdb.Command):
  """dmtcp [prints list of GDB commands for DMTCP]"""

  def __init__(self):
    super(dmtcp,
          self).__init__("dmtcp", gdb.COMMAND_FILES, gdb.COMPLETE_FILENAME)
    self.dont_repeat()

  def invoke(self, dummy_args, from_tty):
    print_dmtcp_commands()
# This will add the new gdb command: dmtcp
dmtcp()

gdb.execute("dmtcp")
# try:
#   gdb.execute("alias DMTCP=dmtcp")
# except:
#   pass

class AddSymbolFileFromSubstring(gdb.Command):
  """add-symbol-file-from-substring FILENAME_SUBSTRING"""

  def __init__(self):
    super(AddSymbolFileFromSubstring,
          self).__init__("add-symbol-file-from-substring",
                         gdb.COMMAND_FILES, gdb.COMPLETE_FILENAME)
    self.dont_repeat()

  def invoke(self, filename_substring, from_tty):
    (filename, base_addr, _, _) = memory_region(filename_substring)
    if filename and base_addr:
      add_symbol_files_from_filename(filename, base_addr)
    else:
      print("Memory segment not found")
# This will add the new gdb command: add-symbol-file-from-substring FILENAME
AddSymbolFileFromSubstring()


class AddSymbolFileFromFilenameAndAddress(gdb.Command):
  """add-symbol-file-from-filename-and-address FILENAME ADDRESS
     Better version of add-symbol-file; ADDRESS is anywhere in memory range"""

  def __init__(self):
    super(AddSymbolFileFromFilenameAndAddress,
          self).__init__("add-symbol-file-from-filename-and-address",
                         gdb.COMMAND_FILES, gdb.COMPLETE_FILENAME)
    self.dont_repeat()

  def invoke(self, filename_and_address, from_tty):
    (filename, address) = filename_and_address.split()
    (_, base_addr, _, _) = memory_region_at_address(address)
    add_symbol_files_from_filename(filename, base_addr)
# This will add the new gdb command:
#   add-symbol-file-from-filename-base-address FILENAME BASE_ADDRESS
AddSymbolFileFromFilenameAndAddress()


class ShowFilenameAtAddress(gdb.Command):
    """show-filename-at-address/whereis-address MEMORY_ADDRESS"""

    def __init__(self):
        super(ShowFilenameAtAddress,
              self).__init__("show-filename-at-address", gdb.COMMAND_STATUS)
        self.dont_repeat()

    def invoke(self, memory_address, from_tty):
        # Remove existing symbol files
        if getpid() == 0:
          gdb.execute('print "Process not yet started"', False, False)
        else:
          memory_region = "%s 0x%x-0x%x (%s)" % \
                          memory_region_at_address(memory_address)
          #GDB-8:  For some unknown reason (e.g., after: 'bt' and 'frame 4'
          #  in MANA), the 'gdb.execute' reports '"' not allowed,
          #  and (') doesn't work.  Perhaps it's an interaction
          #  between GDB and the Python API.
          #WAS: gdb.execute('print "' + memory_region + '"', False, False)
          print(memory_region)
# This will add the new gdb command: show-filename-at-address MEMORY_ADDRESS
ShowFilenameAtAddress()
# try:
#   gdb.execute("whereis-address 0x0")
# except gdb.error:
#   gdb.execute("alias whereis-address=show-filename-at-address")
try:
  gdb.execute("alias whereis-address=show-filename-at-address")
except:
  pass


class AddSymbolFileAtAddress(gdb.Command):
  """add-symbol-file-at-address MEMORY_ADDRESS"""

  def __init__(self):
    super(AddSymbolFileAtAddress,
          self).__init__("add-symbol-file-at-address", gdb.COMMAND_FILES)
    self.dont_repeat()

  def invoke(self, address, from_tty):
    if getpid() == 0:
      gdb.execute('print "Process not yet started"', False, False)
    else:
      (filename, base_addr, _, _) = memory_region_at_address(address)
      if filename.startswith("NOT_FOUND"):
        gdb.execute('print "*** Memory address ' + address + ' not found"',
                    False, False)
      else:
        add_symbol_files_from_filename(filename, base_addr)
# This will add the new gdb command: add-symbol-file-at-address MEMORY_ADDRESS
AddSymbolFileAtAddress()


class AddSymbolFilesAll(gdb.Command):
    """add-symbol-files-all (adds all symbols of files in /proc/self/maps)"""

    def __init__(self):
        super(AddSymbolFilesAll,
              self).__init__("add-symbol-files-all", gdb.COMMAND_FILES)
        self.dont_repeat()

    def invoke(self, dummy_args, from_tty):
        # Remove existing symbol files
        gdb.execute("symbol-file", False, True)
        # for (filename, _, _, _) in memory_regions():
        #   gdb.execute("add-symbol-file-from-substring " + filename)
        # This form preferred, in case the same filename appears more than once:
        for (filename, address, _, _) in memory_regions_executable():
          gdb.execute("add-symbol-file-at-address " + hex(address))
# This will add the new gdb command: add-symbol-files-all
AddSymbolFilesAll()


class LoadSymbols(gdb.Command):
    """load-symbols [FILENAME] (load fresh symbols from /proc/self/maps)

If there is more than one executable binary loaded (e.g., split processes),
then you may need to use load-symbols-library, and select as an argument
the filename (or address in /proc/*/maps) for the specific binary desired."""

    def __init__(self):
        super(LoadSymbols,
              self).__init__("load-symbols",
                             gdb.COMMAND_FILES, gdb.COMPLETE_FILENAME)
        self.dont_repeat()

    def invoke(self, filename, from_tty):
        if filename:
          filename = os.path.abspath(filename)
          load_symbols(filename.split()[0])
        else:
          load_symbols()
# This will add the new gdb command: load-symbols
LoadSymbols()


class LoadSymbolsLibrary(gdb.Command):
    """load-symbols-library [FILENAME-OR-ADDRESS] (load symbols from library)"""

    def __init__(self):
        super(LoadSymbolsLibrary,
              self).__init__("load-symbols-library",
                             gdb.COMMAND_FILES, gdb.COMPLETE_FILENAME)
        self.dont_repeat()

    def invoke(self, filename_or_address, from_tty):
        load_symbols_library(filename_or_address.split()[0])
# This will add the new gdb command: load-symbols-library
LoadSymbolsLibrary()


def add_symbol_files_from_filename(filename, base_addr):
  if not os.path.exists(filename):
    return

  if not is_exec_file(filename):
    return

  base_addr = 0  # ELF executables already have hard-wired absolute address
  (textaddr, sections) = relocatesections(filename)
  cmd = "add-symbol-file %s 0x%x" % (filename, int(textaddr, 16) + base_addr)
  for s in sections:
    addr = int(s['addr'], 16)
    if s['name'] == '.text' or addr == 0:
      continue
    cmd += " -s %s 0x%x" % (s['name'], addr + base_addr)
  gdb.execute(cmd)

# Helper functions for AddSymbolFileFromSubstring
def getpid():
  return gdb.selected_inferior().pid

def usingCore():
  # In Linux 4.12, gdb 8.3, our getpid()==1,
  #   /proc/1/maps has read permission, but "Permission denied"
  return (not os.path.exists("/proc/" + str(getpid()) + "/maps") or
          getpid() == 1)

class Procmaps(gdb.Command):
  """procmaps (same as:  shell cat /proc/INFERIOR_PID/maps)"""
  def __init__(self):
    super(Procmaps,
          self).__init__("procmaps", gdb.COMMAND_STATUS)
    self.dont_repeat()
  def invoke(self, dummy_args, from_tty):
    if getpid() == 0:
      gdb.execute('print "Process not yet started; type \'run\'"', False, False)
    elif usingCore():
      gdb.execute("info proc mappings")
    else:
      gdb.execute("shell cat /proc/" + str(getpid()) + "/maps | less",
                  False, True)
Procmaps()

class Procfd(gdb.Command):
  """procfd (same as:  shell ls -l /proc/INFERIOR_PID/fd)"""
  def __init__(self):
    super(Procfd,
          self).__init__("procfd", gdb.COMMAND_STATUS)
    self.dont_repeat()
  def invoke(self, dummy_args, from_tty):
    if getpid() == 0:
      gdb.execute('print "Process not yet started; type \'run\'"', False, False)
    else:
      gdb.execute("shell ls -l /proc/" + str(getpid()) + "/fd", False, True)
Procfd()

class Procfdinfo(gdb.Command):
  """procfdinfo (same as:  shell ls -l /proc/INFERIOR_PID/fdinfo/FD)"""
  def __init__(self):
    super(Procfdinfo,
          self).__init__("procfdinfo", gdb.COMMAND_STATUS)
    self.dont_repeat()
  def invoke(self, fd_number, from_tty):
    if getpid() == 0:
      gdb.execute('print "Process not yet started; type \'run\'"', False, False)
    else:
      gdb.execute("shell ls -l /proc/" + str(getpid()) + "/fd/" + fd_number,
                  False, True)
      gdb.execute("shell cat /proc/" + str(getpid()) + "/fdinfo/" + fd_number,
                  False, True)
Procfdinfo()

class Procenviron(gdb.Command):
  """procenviron (same as:  cat /proc/INFERIOR_PID/environ | tr '\\0' '\\n' | less)"""
  def __init__(self):
    super(Procenviron,
          self).__init__("procenviron", gdb.COMMAND_STATUS)
    self.dont_repeat()
  def invoke(self, dummy_args, from_tty):
    if getpid() == 0:
      gdb.execute('print "Process not yet started; type \'run\'"', False, False)
    else:
      gdb.execute("shell cat /proc/" + str(getpid()) + "/environ | tr '\\0' '\\n' | less",
                  False, True)
Procenviron()

class Pstree(gdb.Command):
  """pstree (same as:  shell pstree -plnu <$USER>)"""
  def __init__(self):
    super(Pstree,
          self).__init__("pstree", gdb.COMMAND_STATUS)
    self.dont_repeat()
  def invoke(self, dummy_args, from_tty):
    gdb.execute("shell pstree -plnu " + str(os.getenv("USER")), False, True)
Pstree()

class Lsof(gdb.Command):
  """pstree (same as:  shell shell lsof -w [for this username])"""
  def __init__(self):
    super(Lsof,
          self).__init__("lsof", gdb.COMMAND_STATUS)
    self.dont_repeat()
  def invoke(self, dummy_args, from_tty):
    gdb.execute("shell lsof -w | grep '^[^ ]*  *[0-9][0-9]*  *" + str(os.getenv("USER")) +
                "  *[0-9]' | grep -v ^lsof | grep -v ^less | grep -v ^grep | less",
                False, True)
Lsof()

class Rlimit(gdb.Command):
  """rlimit (info on rlimit/ulimit)"""
  def __init__(self):
    super(Rlimit,
          self).__init__("rlimit", gdb.COMMAND_STATUS)
    self.dont_repeat()
  def invoke(self, dummy_args, from_tty):
    if getpid() == 0:
      gdb.execute('print "Process not yet started; type \'run\'"', False, False)
    else:
      gdb.execute("shell sh -c '(echo +++ Soft resource limits +++; ulimit -S -a;" +
                  " echo \"\"; echo +++ Hard resource limits +++; ulimit -H -a)' | less",
                  False, True)
Rlimit()

class Signals(gdb.Command):
  """signals (info from /proc/self/status: SigPnd, SigBlk, SigIgn, SigCgt)"""
  def __init__(self):
    super(Signals,
          self).__init__("signals", gdb.COMMAND_STATUS)
    self.dont_repeat()
  def invoke(self, dummy_args, from_tty):
    if getpid() == 0:
      gdb.execute('print "Process not yet started; type \'run\'"', False, False)
    else:
      print_signals()
Signals()

# For /proc/*/maps line: ADDR1-ADDR2 ... FILE,
#   Returns (FILE, ADDR1, ADDR2, PERMISSIONS)
def procmap_filename_address(line):
  quad = line.split()
  if line[0] == ' ':  # if this came from (gdb) info proc mapping
    if quad[-1] == "0x0":
      quad[-1] = "[NO_LABEL]"
    if '/' in quad[-1]:
      return (quad[-1],) + (int(quad[0], 16),) + (int(quad[1], 16),) + \
             (quad[1],)
    else:
      return ("NO_LABEL",) + (int(quad[0], 16),) + (int(quad[1], 16),) + \
             (quad[1],)
  if quad[-1] == "0":
    quad[-1] = "[NO_LABEL]"
  if '/' in quad[-2]:  # if procmaps line ends in "/tmp/a.out (deleted)"
    quad[-1] = quad[-2] + ' ' + quad[-1]
  return (quad[-1],) + \
         tuple([int("0x"+elt, 16) for elt in quad[0].split("-")]) + (quad[1],)
def is_text_segment(memory):
  (file, _, _, permission) = memory
  return "/dev/" not in file and "/locale/" not in file and "r-x" in permission and '/' in file
def memory_regions():
  if getpid() == 0:
    sys.stderr.write("\n*** Process not running! ***\n")
    sys.exit(1)
  if usingCore():
    lines = gdb.execute("info proc mappings", False, True).split('\n')
    lines = [procmap_filename_address(line) for line in lines
                                  if " 0x" in line and "/dev/" not in line and "/locale/" not in line]
  else:
    p = subprocess.Popen(["cat", "/proc/"+str(getpid())+"/maps"],
                         stdout = subprocess.PIPE)
    lines = (line.decode("utf-8").strip() for line in p.stdout.readlines())
    lines = [procmap_filename_address(line) for line in lines]
  return lines
def memory_regions_executable():
  if not usingCore():
    return [memory for memory in memory_regions() if is_text_segment(memory)]
  else:
    unique_filenames = []
    unique_regions = []
    for memory in memory_regions():
      if memory[0] not in unique_filenames:
        unique_filenames.append(memory[0])
        unique_regions.append(memory)
    return unique_regions

# Returns quad:  (filename, base_address, end_address, permissions)
def memory_region(filename_substring):
  regions = memory_regions_executable()
  tmp = [region for region in regions if filename_substring in region[0]]
  if tmp:
    return tmp[0]
  else:
    return ("", 0, 0, "")


def cast_to_memory_address(memory_address):
  if type(memory_address) == int:
    memory_address = hex(memory_address)  # This converts it to a hex string.
  elif "0x" in memory_address:
    memory_address = hex(int(str(gdb.parse_and_eval(memory_address))))
  elif set(str(memory_address)).issubset("0123456789abcdef"):
    if set(str(memory_address)) & set("abcdef"):
      print("Assuming " + memory_address + " is hexadecimal.")
      memory_address = "0x" + memory_address
    else:
      print("Assuming " + memory_address + " is decimal. Prepend '0x' for hex.")
  else:
    return None
  return int(memory_address, 0)

def memory_region_at_address(memory_address):
  memory_address = cast_to_memory_address(memory_address)
  regions = memory_regions()
  match = [region for region in regions
           if memory_address >= region[1] and memory_address < region[2]]
  if match:
    return match[0]
  else:
    return ("NOT_FOUND (Did you intend the address in hex?)", 0, 0, "????")

def print_signals():
  signals_x86_arm = [
    "SIGNONE", "SIGHUP", "SIGINT", "SIGQUIT", "SIGILL", "SIGTRAP",
    "SIGABRT/SIGIOT", "SIGBUS", "SIGFPE", "SIGKILL", "SIGUSR1", "SIGSEGV",
    "SIGUSR2", "SIGPIPE", "SIGALRM", "SIGTERM", "SIGSTKFLT", "SIGCHLD",
    "SIGCONT", "SIGSTOP", "SIGTSTP", "SIGTTIN", "SIGTTOU", "SIGURG",
    "SIGXCPU", "SIGXFSZ", "SIGVTALRM", "SIGPROF", "SIGWINCH", "SIGIO/SIGPPOLL",
    "SIGPWR", "SIGSYS/SIGUNUSED"
  ]
  procfile = open("/proc/" + str(getpid()) + "/status", "r")
  status = [line.strip().split(":\t") for line in procfile.readlines()
                        if line.startswith("Sig") or line.startswith("ShdPnd")]
  signals = dict(status)
  del signals["SigQ"]
  for item in signals:  # Convert hex to bits
    signals[item] = bin(int("0x1" + signals[item], 0))[3:]
  def sigbits(sigset):  # Convert bit string to signal names
    # reversed and '0' added: e.g., "000111"->"0111000"
    bits = zip( "0" + signals[sigset][::-1],
                signals_x86_arm + ["SIGRT_"+str(i) for i in range(0,32)] )
    return [b[1] for b in bits if b[0] == '1']
  gdb.execute('print "' + "pending per-thread signals: " +
              str(sigbits("SigPnd")) + '"', False, False)
  gdb.execute('print "' + "pending per-process signals: " +
              str(sigbits("ShdPnd")) + '"', False, False)
  gdb.execute('print "' + "blocked signals: " + str(sigbits("SigBlk")) + '"',
              False, False)
  gdb.execute('print "' + "ignored signals: " + str(sigbits("SigIgn")) + '"',
              False, False)
  gdb.execute('print "' + "signals w/ handlers: " +
              str(sigbits("SigCgt")) + '"', False, False)
