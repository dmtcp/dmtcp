#!/usr/bin/python

from __future__ import with_statement
import subprocess
import string
import gdb

class ProcSelfFd(gdb.Command):
    """List contents of /proc/<inferior-pid>/fd"""

    def __init__(self):
        gdb.Command.__init__(self, "proc_fd", gdb.COMMAND_SUPPORT)

    def invoke(self, arg, from_tty):
        pid = str(gdb.inferiors()[0].pid)
        print "ls -l /proc/" + pid + "/fd"
        print gdb.execute("shell ls -l /proc/" + pid + "/fd", True, True)

ProcSelfFd()

class ProcSelfMaps(gdb.Command):
    """Print contents of /proc/<inferior-pid>/maps"""

    def __init__(self):
        gdb.Command.__init__(self, "proc_maps", gdb.COMMAND_SUPPORT)

    def invoke(self, arg, from_tty):
        pid = str(gdb.inferiors()[0].pid)
        print "cat /proc/" + pid + "/maps"
        print gdb.execute("shell cat /proc/" + pid + "/maps", True, True)

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
