#!/usr/bin/env python
from popen2 import Popen3,Popen4
from random import randint
from time   import sleep
from os     import listdir
import os
import sys

#number of checkpoint/restart cycles
CYCLES=2

#Sleep after program startup, etc (sec)
S=0.3

#Max time to wait for ckpt/restart to finish (sec)
TIMEOUT=5

#Interval between checks for ckpt/restart complete
INTERVAL=0.2

#Buffers for process i/o
BUFFER_SIZE=4096*8

#False redirects process stderr
VERBOSE=False

#parse program args
args={}
for i in sys.argv:
  args[i]=True
  if i=="-v":
    VERBOSE=True
  if i=="--stress":
    CYCLES=999999999
  if i=="-h" or i=="--help":
    print "USAGE "+sys.argv[0]+" [-v] [testname] [testname...]  "
    sys.exit(1)


def shouldRunTest(name):
  if len(sys.argv) <= 1:
    return True
  return args.has_key(name)

#make sure dmtcp is built
os.system("test -f Makefile || ./configure")
if os.system("make -s --no-print-directory all tests") != 0:
  print "`make all tests` FAILED"
  sys.exit(1)

#pad a string and print/flush it
def printFixed(str, w=1):
  print str.ljust(w),
  sys.stdout.flush()

#make sure we are in svn root
if os.system("test -d bin") is not 0:
  os.chdir("..")
assert os.system("test -d bin") is 0

#exception on failed check
class CheckFailed(Exception):
  def __init__(self, value=""):
    self.value = value

#launch a child process
def launch(cmd):
  if VERBOSE:
    print "Launching... ", cmd
  cmd = cmd.split(" ");
  try:
    os.stat(cmd[0])
  except e:
    raise CheckFailed(cmd[0] + " not found")
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
    CHECK(False, "coordinator died unexpectedly")
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
  procs=[]
  
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
    CHECK(numFiles==status[0]*2+1, "unexpected number of checkpoint files, %d procs, %d files" % (status[0], numFiles))
  
  def testRestart():
    #build restart command
    cmd="./bin/dmtcp_restart"
    n=0
    for i in listdir(ckptDir):
      if i.endswith(".mtcp"):
        cmd+= " "+ckptDir+"/"+i
        n+=1
    #run restart and test if it worked
    procs.append(launch(cmd))
    WAITFOR(lambda: status==getStatus(),
            lambda: "restart error, "+str(n)+" expected, %d found, running=%d" % getStatus())
  try:
    printFixed(name,15)

    if not shouldRunTest(name):
      print "SKIPPED" 
      return
      
    CHECK(getStatus()==(0, False), "coordinator initial state")

    #start user programs
    for cmd in cmds:
      procs.append(launch("./bin/dmtcp_checkpoint "+cmd))
      sleep(S)

    #record status, make sure user programs are running
    status=getStatus()
    n, running = status
    CHECK(running and n>=len(cmds), "user program startup error")
    
    for i in xrange(CYCLES):
      printFixed("ckpt:")
      testCheckpoint()
      testKill()
      printFixed("PASSED ")

      printFixed("rstr:")
      testRestart()
      printFixed("PASSED ")

    testKill()
    print #newline

  except CheckFailed, e:
    print "FAILED"
    printFixed("",15)
    print "root-pids:", map(lambda x: x.pid, procs),"msg:",e.value
    try:
      testKill()
    except CheckFailed, e:
      print "CLEANUP ERROR:", e.value
      SHUTDOWN()
      sys.exit(1)

  #clear checkpoint dir
  for f in listdir(ckptDir):
    os.remove(ckptDir + "/" + f)

print "== Tests ".ljust(80,'=')

#tmp port
p0=str(randint(2000,10000))
p1=str(randint(2000,10000))
p2=str(randint(2000,10000))
p3=str(randint(2000,10000))

runTest("dmtcp1",        ["./test/dmtcp1"])

runTest("shared-fd",     ["./test/shared-fd"])

runTest("echoserver",    ["./test/echoserver/server "+p0,
                          "./test/echoserver/client localhost "+p0])

runTest("frisbee",       ["./test/frisbee "+p1+" localhost "+p2,
                          "./test/frisbee "+p2+" localhost "+p3,
                          "./test/frisbee "+p3+" localhost "+p1+" starter"])

runTest("shared-memory", ["./test/shared-memory"])

runTest("stale-fd",      ["./test/stale-fd"])

runTest("forkexec",      ["./test/forkexec"])

runTest("gettimeofday",  ["./test/gettimeofday"])

runTest("readline",      ["./test/readline"])

os.environ['DMTCP_GZIP'] = "1"
runTest("gzip",          ["./test/dmtcp1"])
os.environ['DMTCP_GZIP'] = "0"

runTest("perl",          ["/usr/bin/perl"])

runTest("python",        ["/usr/bin/python"])

print "".ljust(80,'=')

SHUTDOWN()

