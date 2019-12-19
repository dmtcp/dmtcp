#!/usr/bin/env python

# Here are some gdb commands written in python using the gdb Python API
# described here:
#   http://sourceware.org/gdb/current/onlinedocs/gdb/Python-API.html#Python-API

from __future__ import with_statement
import subprocess
import string
import os
import gdb

class ProcSelfFd(gdb.Command):
    """List contents of /proc/<inferior-pid>/fd"""

    def __init__(self):
        gdb.Command.__init__(self, "proc_fd", gdb.COMMAND_SUPPORT)

    def invoke(self, arg, from_tty):
        pid = str(gdb.inferiors()[0].pid)
        print("ls -l /proc/" + pid + "/fd")
        print(gdb.execute("shell ls -l /proc/" + pid + "/fd", True, True))

ProcSelfFd()

class ProcSelfMaps(gdb.Command):
    """Print contents of /proc/<inferior-pid>/maps"""

    def __init__(self):
        gdb.Command.__init__(self, "proc_maps", gdb.COMMAND_SUPPORT)

    def invoke(self, arg, from_tty):
        pid = str(gdb.inferiors()[0].pid)
        print("cat /proc/" + pid + "/maps")
        print(gdb.execute("shell cat /proc/" + pid + "/maps", True, True))

ProcSelfMaps()

class CallerIs (gdb.Function):
    """Return True if the calling function's name is equal to a string.
    This function takes one or two arguments.
    The first argument is the name of a function; if the calling function's
    name is equal to this argument, this function returns True.
    The optional second argument tells this function how many stack frames
    to traverse to find the calling function.  The default is 1."""

    def __init__ (self):
        super (CallerIs, self).__init__ ("caller_is")

    def invoke (self, name, nframes = 1):
        frame = gdb.get_current_frame ()
        while nframes > 0:
            frame = frame.get_prev ()
            nframes = nframes - 1
        return frame.get_name () == name.string ()

CallerIs()


class AddSymbolFile(gdb.Command):
    """ This command will load the debugging information about a dynamic
    library (.so file) using gdb's add-symbol-file command"""

    def __init__(self):
        gdb.Command.__init__(self, "add_symbol_file", gdb.COMMAND_SUPPORT)

    def invoke(self, arg, from_tty):
        pid = str(gdb.inferiors()[0].pid)
        cmd = "cat /proc/" + pid + "/maps | grep "
        res = subprocess.Popen(cmd + arg,
                               shell=True, stdout=subprocess.PIPE).communicate()[0]
        if (len(res) == 0):
            print('**** Library %s not found. ' % arg)
            print('  Do:  cat /proc/%d/maps to see all libs.' % pid)
            return

        lib=subprocess.Popen(cmd + arg + " | head -1 | sed -e 's%[^/]*\\(/.*\\)%\\1%'",
                             shell=True, stdout=subprocess.PIPE).communicate()[0].rstrip()

        segAddr=subprocess.Popen(cmd + lib + "| grep r-xp |"\
                                 "sed -e 's%^\\([0-9a-f]*\\).*%0x\\1%'",
                                 shell=True, stdout=subprocess.PIPE).communicate()[0]
        segDataAddr=subprocess.Popen(cmd + lib +
                                " | grep rw-p | sed -e 's%^\\([0-9a-f]*\\).*%0x\\1%'",
                                 shell=True, stdout=subprocess.PIPE).communicate()[0]

        textOffset=subprocess.Popen("readelf -S " + lib +" | grep '\.text ' | " \
                                    "sed -e 's%^.*text[^0-9a-f]*[0-9a-f]*\\s*\\([0-9a-f]*\\).*%0x\\1%'",
                                    shell=True, stdout=subprocess.PIPE).communicate()[0]
        dataOffset=subprocess.Popen("readelf -S " + lib +" | grep '\.data ' | "\
                                    "sed -e 's%^.*data[^0-9a-f]*[0-9a-f]*\\s*\\([0-9a-f]*\\).*%0x\\1%'",
                                    shell=True, stdout=subprocess.PIPE).communicate()[0]
        bssOffset=subprocess.Popen("readelf -S " + lib +" | grep '\.bss ' | "\
                                   "sed -e 's%^.*bss[^0-9a-f]*[0-9a-f]*\\s*\\([0-9a-f]*\\).*%0x\\1%'",
                                   shell=True, stdout=subprocess.PIPE).communicate()[0]

        gdb.execute("add-symbol-file " + lib + " " + str(long(segAddr, 16) +
                                                         long(textOffset, 16))
                    + " -s .data " + str(long(segDataAddr, 16) + long(dataOffset, 16))
                    + " -s .bss " + str(long(segDataAddr, 16) + long(bssOffset, 16)),
                    True, True) 

AddSymbolFile()
