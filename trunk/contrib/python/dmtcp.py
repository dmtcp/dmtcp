#!/usr/bin/python

# The contents of this file are inspired from the python script dmtcp_ctypes.py
# originally supplied by Neal Becker.

import os
import glob
import subprocess
from ctypes import *
import subprocess

ckptRetVal = 0
sessionList = []
vncserver_addr = -1

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

libdmtcp = CDLL(None)
try:
    isEnabled         = libdmtcp.dmtcpIsEnabled();
    checkpoint        = libdmtcp.dmtcpCheckpoint;
    localStatus       = libdmtcp.dmtcpGetLocalStatus;
    coordinatorStatus = libdmtcp.dmtcpGetCoordinatorStatus;
    delayCkptLock     = libdmtcp.dmtcpDelayCheckpointsLock;
    delayCkptUnlock   = libdmtcp.dmtcpDelayCheckpointsUnlock;
    installHooks      = libdmtcp.dmtcpInstallHooks;
    runCommand        = libdmtcp.dmtcpRunCommand;

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

def checkpoint():
    global ckptRetVal
    if isEnabled:
        ckptRetVal = libdmtcp.dmtcpCheckpoint()
    # sessionId = libdmtcp.dmtcpCheckpoint()

def isResume():
    global ckptRetVal
    return ckptRetVal == 1

def isRestart():
    global ckptRetVal
    return ckptRetVal == 2

def restore(sessionId = 0):
    if sessionId == 0:
        if len(sessionList) == 0:
            createSessionList()
        if len(sessionList) == 0:
            print 'No checkpoint session found'
            return
        print 'Restoring the latest session'
    else:
        if len(sessionList) == 0:
            print 'Please do a listSession to see the list of available sessions'
            return
        if sessionId < 1 or sessionId > len(sessionList):
            return 'Invalid session id'

    session = sessionList[sessionId - 1]
    os.execlp('dmtcp_nocheckpoint', 'sh', session[1])


def createSessionList():
    global sessionList
    restartScripts = glob.glob('dmtcp_restart_script_*.sh')
    for script in restartScripts:
        for line in open(script):
            if 'ckpt_timestamp' in line:
                tstamp = line.split('=')[1][:-1]
                sessionList = [(tstamp, script)] + sessionList
                break;
    sessionList.sort()

def listSessions():
    global sessionList
    if len(sessionList) == 0:
        createSessionList()
    count = 1;
    for session in sessionList:
        print '[%d]' %(count),
        count += 1
        print session

    if len(sessionList) == 0:
        print 'No checkpoint sessions found'

########################################################################
# VNC handling
########################################################################

def startGraphics():
    global vncserver_addr

    if vncserver_addr != -1:
        print 'VNC server already running at: ' + vncserver_addr
        return

    addr = 0
    while True:
        try:
            addr += 1
            out = subprocess.check_output(['vncserver', ':' + str(addr)],
                                          stderr=subprocess.STDOUT)
            os.environ['DISPLAY'] = ':' + str(addr)
            break
        except subprocess.CalledProcessError:
            pass
    vncserver_addr = addr

def showGraphics():
    global vncserver_addr

    if vncserver_addr == -1:
        print "VNC server not running."
        return
    fnull = open(os.devnull, "w")
    saved_display = os.environ['DISPLAY']
    os.environ['DISPLAY'] = os.environ['ORIG_DISPLAY']
    out = subprocess.Popen(['dmtcp_nocheckpoint',
                            'vncviewer',
                            '-passwd', os.environ['HOME'] + '/.vnc/passwd',
                            ':' + str(vncserver_addr)],
                          stdout=fnull, stderr=fnull)
    os.environ['DISPLAY'] = saved_display

def stopGraphics():
    global vncserver_addr

    if vncserver_addr == -1:
        print "VNC server not running."
        return

    try:
        out = subprocess.check_output(['vncserver', '-kill',
                                       ':' + str(vncserver_addr)],
                                      stderr=subprocess.STDOUT)
        del os.environ['DISPLAY']
    except subprocess.CalledProcessError:
        print 'Error killing vncserver: ' + out



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
