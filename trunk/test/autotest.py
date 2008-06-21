#!/usr/bin/env python
from popen2 import Popen3,Popen4
from random import randint
from time   import sleep
from os     import listdir
import os
import sys

BUFFER_SIZE=4096*8
VERBOSE=False
S=1

os.system("test -f Makefile || ./configure")
os.system("make all tests")

#make sure we are in svn root
if os.system("test -d bin") is not 0:
  os.chdir("..")
assert os.system("test -d bin") is 0



#launch a child process
def launch(cmd):
  if VERBOSE:
    print "Launching... ", cmd
  cmd = cmd.split(" ");
  return Popen3(cmd, not VERBOSE, BUFFER_SIZE)

#randomize port and dir, so multiple processes works 
ckptDir="tmp-autotest-%d" % randint(100000000,999999999)
os.mkdir(ckptDir);
os.environ['DMTCP_PORT'] = str(randint(2000,10000))
os.environ['DMTCP_CHECKPOINT_DIR'] = ckptDir

#launch the coordinator
coordinator = launch("./bin/dmtcp_coordinator")


#send a command to the coordinator process
def coordinatorCmd(cmd):
  coordinator.tochild.write(cmd+"\n")
  coordinator.tochild.flush()

#clean up after ourselves
def SHUTDOWN():
  coordinatorCmd('q')
  sleep(S/2.0)
  os.system("kill -9 %d" % coordinator.pid)
  os.system("rm -rf  %s" % ckptDir)

def CHECK(val, msg):
  if not val:
    print "FAILED (%s)" % msg
    SHUTDOWN()
    sys.exit(1)

#extract (NUM_PEERS, RUNNING) from coordinator
def getStatus():
  coordinatorCmd('s')

  if coordinator.poll() >= 0:
    return (-1, False)
  
  while True:
    line=coordinator.fromchild.readline().strip()
    if line=="Status...":
      break;
    if VERBOSE:
      print "Ignoring line from coordinator: ", line

  x,peers=coordinator.fromchild.readline().strip().split("=")
  CHECK(x=="NUM_PEERS", "reading coordinator status")
  x,running=coordinator.fromchild.readline().strip().split("=")
  CHECK(x=="RUNNING", "reading coordinator status")

  return (int(peers), (running=="yes"))

#test a given list of commands to see if they checkpoint
def runTest(name, cmds):
  CHECK(getStatus()==(0, False), "coordinator initial state")

  #start user programs
  for cmd in cmds:
    prog = launch("./bin/dmtcp_checkpoint "+cmd)
    sleep(S)

  #record status, make sure user programs are running
  status=getStatus()
  n, running = status
  CHECK(running and n>=len(cmds), "user program startup error")

  def testCheckpoint():
    #start checkpoint 
    coordinatorCmd('c')
    sleep(S)

    #make sure status hasn't changed
    CHECK(status==getStatus(), "checkpoint error")

    #make sure the files are there
    numFiles=len(listdir(ckptDir))
    CHECK(numFiles==status[0]*2+1, "Unexpected number of checkpoint files, %d procs, %d files" % (status[0], numFiles))
  
  def testKill():
    #kill all processes
    coordinatorCmd('k')
    sleep(S)
    CHECK(getStatus()==(0, False), "coordinator kill command failed")

  def testRestart():
    #build restart command
    cmd="./bin/dmtcp_restart"
    for i in listdir(ckptDir):
      if i.endswith(".mtcp"):
        cmd+= " "+ckptDir+"/"+i
    #run restart and test if it worked
    launch(cmd)
    sleep(S)
    newStatus=getStatus()
    CHECK(status==newStatus, "restart error: %d of %d procs, running=%d" % (newStatus[0], status[0],newStatus[1]))

  print name, "checkpoint 1:",
  testCheckpoint()
  testKill()
  print "PASSED"
  print name, "restart 1:   ",
  testRestart()
  print "PASSED"
  testKill()
# print name, "checkpoint 2:",
# testCheckpoint()
# testKill()
# print "PASSED"
# print name, "restart 2:   ",
# testRestart()
# testKill()
# print "PASSED"

  #clear checkpoint dir
  for f in listdir(ckptDir):
    os.remove(ckptDir + "/" + f)

runTest("dmtcp1",        ["./test/dmtcp1"])
runTest("shared-fd",     ["./test/shared-fd"])
runTest("shared-memory", ["./test/shared-memory"])

SHUTDOWN()

