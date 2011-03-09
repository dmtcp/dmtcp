#!/usr/bin/python

# The contents of this file are inspired from the python script dmtcp_ctypes.py
# originally supplied by Neal Becker.

from ctypes import *

class CoordinatorStatus (Structure):
    _fields_ =  [('numProcesses', c_int),
                 ('isRunning',    c_int)]
    #FIXME: Add other fields such as: ComputationID

class LocalStatus (Structure):
    _fields_ =  [('numCheckpoints', c_int),
                 ('numRestarts',    c_int),
                 ('checkpointFilename', c_char_p),
                 ('uniquePidStr',       c_char_p)]
    #FIXME: Add other fields such as: ComputationID

libdmtcphijack = CDLL(None)
try:
    isEnabled         = libdmtcphijack.dmtcpIsEnabled();
    checkpoint        = libdmtcphijack.dmtcpCheckpoint;
    localStatus       = libdmtcphijack.dmtcpGetLocalStatus;
    coordinatorStatus = libdmtcphijack.dmtcpGetCoordinatorStatus;
    delayCkptLock     = libdmtcphijack.dmtcpDelayCheckpointsLock;
    delayCkptUnlock   = libdmtcphijack.dmtcpDelayCheckpointsUnlock;
    installHooks      = libdmtcphijack.dmtcpInstallHooks;
    runCommand        = libdmtcphijack.dmtcpRunCommand;

    coordinatorStatus.restype = POINTER(CoordinatorStatus)
    localStatus.restype = POINTER(LocalStatus)

except AttributeError:
    isEnabled = False;

def numProcesses():
    if isEnabled:
        return coordinatorStatus().contents.numProcesses
    return -1

def isRunning():
    if isEnabled:
        return coordinatorStatus().contents.isRunning
    return False

def numCheckpoints():
    if isEnabled:
        return localStatus().contents.numCheckpoints
    return -1 

def numRestarts():
    if isEnabled:
        return localStatus().contents.numRestarts
    return -1

def checkpointFilename():
    if isEnabled:
        return localStatus().contents.checkpointFilename
    return ""

def checkpointFilesDir():
    if isEnabled:
        return checkpointFilename().replace('.dmtcp', '_files')
    return ""

def uniquePidStr():
    if isEnabled:
        return localStatus().contents.uniquePidStr
    return ""


if __name__ == '__main__':
    if isEnabled:
        print 'DMTCP Status: Enabled'
        if isRunning():
            print '    isRunning: YES'
        else:
            print '    isRunning: NO'
        print '    numProcesses: ', numProcesses()
        print '    numCheckpoints: ', numCheckpoints()
        print '    numRestarts: ', numRestarts()
        print '    checkpointFilename: ', checkpointFilename()
    else:
        print 'DMTCP Status: Disabled'
