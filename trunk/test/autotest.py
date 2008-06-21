#!/usr/bin/env python
from popen2 import Popen3,Popen4
from random import randint
from time   import sleep
from os     import listdir
import os
import sys

BUFFER_SIZE=4096*8
VERBOSE=False
S=0.3
TIMEOUT=5
INTERVAL=0.2

print "== Build ".ljust(80,'=')
os.system("test -f Makefile || ./configure")
if os.system("make --no-print-directory mtcp dmtcp tests") != 0:
  print "`make all mtcp dmtcp tests` FAILED"
  sys.exit(1)

#pad a string and print/flush it
def printFixed(str, w=1):
  print str.ljust(w),
  sys.stdout.flush()

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
os.environ['DMTCP_HOST'] = "localhost"
os.environ['DMTCP_PORT'] = str(randint(2000,10000))
os.environ['DMTCP_CHECKPOINT_DIR'] = ckptDir
os.environ['DMTCP_GZIP'] = "0"

#launch the coordinator
coordinator = launch("./bin/dmtcp_coordinator")


#send a command to the coordinator process
def coordinatorCmd(cmd):
  coordinator.tochild.write(cmd+"\n")
  coordinator.tochild.flush()

#clean up after ourselves
def SHUTDOWN():
  coordinatorCmd('q')
  sleep(S)
  os.system("kill -9 %d" % coordinator.pid)
  os.system("rm -rf  %s" % ckptDir)

#exception on failed check
class CheckFailed(Exception):
  def __init__(self, value=""):
    self.value = value

#make sure val is true
def CHECK(val, msg):
  if not val:
    raise CheckFailed(msg)

#wait TIMEOUT for test() to be true, or throw erro
def WAITFOR(test, msg):
  left=TIMEOUT/INTERVAL
  while not test():
    if left <= 0:
      CHECK(False, msg())
    left-=1
    sleep(INTERVAL)

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
  status=None
  
  def testKill():
    #kill all processes
    coordinatorCmd('k')
    WAITFOR(lambda: getStatus()==(0, False), lambda:"coordinator kill command failed")
   
  def testCheckpoint():
    #start checkpoint 
    coordinatorCmd('c')
    
    #wait for files to appear and status to return to original
    WAITFOR(lambda: len(listdir(ckptDir))>0 and status==getStatus(),
            lambda: "checkpoint error: %d procs, running=%d" % getStatus())
    
    #make sure the right files are there
    numFiles=len(listdir(ckptDir))
    CHECK(numFiles==status[0]*2+1, "Unexpected number of checkpoint files, %d procs, %d files" % (status[0], numFiles))
  
  def testRestart():
    #build restart command
    cmd="./bin/dmtcp_restart"
    for i in listdir(ckptDir):
      if i.endswith(".mtcp"):
        cmd+= " "+ckptDir+"/"+i
    #run restart and test if it worked
    launch(cmd)
    WAITFOR(lambda: status==getStatus(),
            lambda: "restart error: %d procs, running=%d" % getStatus())
 
  try:
    CHECK(getStatus()==(0, False), "coordinator initial state")

    #start user programs
    for cmd in cmds:
      prog = launch("./bin/dmtcp_checkpoint "+cmd)
      sleep(S)

    #record status, make sure user programs are running
    status=getStatus()
    n, running = status
    CHECK(running and n>=len(cmds), "user program startup error")

    printFixed(name,15)
    printFixed("ckpt:")
    testCheckpoint()
    testKill()

    printFixed("PASSED, ")
    printFixed("rstr:")
    testRestart()

    printFixed("PASSED, ")
    printFixed("ckpt:")
    testCheckpoint()
    testKill()

    printFixed("PASSED, ")
    printFixed("rstr:")
    testRestart()
    testKill()
    print "PASSED"

  except CheckFailed, e:
    print "FAILED"
    printFixed("",15)
    print "(%s)" % e.value
    testKill()

  #clear checkpoint dir
  for f in listdir(ckptDir):
    os.remove(ckptDir + "/" + f)

print "== Tests ".ljust(80,'=')

#tmp port
p=str(randint(2000,10000))

runTest("dmtcp1",        ["./test/dmtcp1"])

runTest("dmtcp1x2",      ["./test/dmtcp1", "./test/dmtcp1"])

runTest("echoserver",    ["./dmtcp/examples/01.echoserver/server "+p,
                          "./dmtcp/examples/01.echoserver/client localhost "+p])

runTest("shared-fd",     ["./test/shared-fd"])

os.environ['DMTCP_GZIP'] = "1"
runTest("gzip",     ["./test/shared-fd"])
os.environ['DMTCP_GZIP'] = "0"

runTest("shared-memory", ["./test/shared-memory"])

print "".ljust(80,'=')

SHUTDOWN()

