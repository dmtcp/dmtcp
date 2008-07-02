#!/usr/bin/env python
from popen2 import Popen3,Popen4
from random import randint
from time   import sleep
from os     import listdir
import socket 
import os
import sys

#get testconfig
os.system("test -f Makefile || ./configure")
import testconfig

#number of checkpoint/restart cycles
CYCLES=2

#Number of times to try dmtcp_restart
RETRIES=2

#Sleep after each program startup (sec)
S=0.3

#Max time to wait for ckpt/restart to finish (sec)
TIMEOUT=5

#Interval between checks for ckpt/restart complete
INTERVAL=0.1

#Buffers for process i/o
BUFFER_SIZE=4096*8

#False redirects process stderr
VERBOSE=False

#Binaries
BIN="./bin/"

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

stats = [0, 0]

def shouldRunTest(name):
  if len(sys.argv) <= 1:
    return True
  return args.has_key(name)

#make sure dmtcp is built
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
  except:
    raise CheckFailed(cmd[0] + " not found")
  return Popen3(cmd, not VERBOSE, BUFFER_SIZE)

#randomize port and dir, so multiple processes works 
ckptDir="tmp-autotest-%d" % randint(100000000,999999999)
os.mkdir(ckptDir);
os.environ['DMTCP_HOST'] = "localhost"
os.environ['DMTCP_PORT'] = str(randint(2000,10000))
os.environ['DMTCP_CHECKPOINT_DIR'] = os.path.abspath(ckptDir)
os.environ['DMTCP_GZIP'] = "0"
if not VERBOSE:
  os.environ['JALIB_STDERR_PATH'] = "/dev/null"

#launch the coordinator
coordinator = launch(BIN+"dmtcp_coordinator")

#send a command to the coordinator process
def coordinatorCmd(cmd):
  try:
    if VERBOSE and cmd != "s":
      print "COORDINATORCMD(",cmd,")"
    coordinator.tochild.write(cmd+"\n")
    coordinator.tochild.flush()
  except:
    raise CheckFailed("failed to write '%s' to coordinator (pid: %d)" %  (cmd, coordinator.pid))

#clean up after ourselves
def SHUTDOWN():
  try:
    coordinatorCmd('q')
    sleep(S)
  except:
    print "SHUTDOWN() failed"
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
    try:
      line=coordinator.fromchild.readline().strip()
      if line=="Status...":
        break;
      if VERBOSE:
        print "Ignoring line from coordinator: ", line
    except IOError, (errno, strerror):
      if coordinator.poll() >= 0:
        CHECK(False, "coordinator died unexpectedly")
        return (-1, False)
      if errno==4: #Interrupted system call
        continue
      raise CheckFailed("I/O error(%s): %s" % (errno, strerror))

  x,peers=coordinator.fromchild.readline().strip().split("=")
  CHECK(x=="NUM_PEERS", "reading coordinator status")
  x,running=coordinator.fromchild.readline().strip().split("=")
  CHECK(x=="RUNNING", "reading coordinator status")

  if VERBOSE:
    print "STATUS: peers=%s, running=%s" % (peers,running)
  return (int(peers), (running=="yes"))
  
#delete all files in ckpDir
def clearCkptDir():
    #clear checkpoint dir
    for f in listdir(ckptDir):
      os.remove(ckptDir + "/" + f)

#test a given list of commands to see if they checkpoint
def runTest(name, numProcs, cmds):
  #the expected/correct running status
  status=(numProcs, True)
  procs=[]

  def wfMsg(msg):
    #return function to generate error message
    return lambda: msg+", "+str(status[0])+" expected, %d found, running=%d" % getStatus()
  
  def testKill():
    #kill all processes
    coordinatorCmd('k')
    WAITFOR(lambda: getStatus()==(0, False), lambda:"coordinator kill command failed")
    for x in procs:
      #cleanup proc
      try:
        x.tochild.close()
        x.fromchild.close()
        x.childerr.close()
      except:
        None
      os.waitpid(x.pid, os.WNOHANG)
      procs.remove(x)
   
  def testCheckpoint():
    #start checkpoint 
    coordinatorCmd('c')
    
    #wait for files to appear and status to return to original
    WAITFOR(lambda: len(listdir(ckptDir))>0 and status==getStatus(),
            wfMsg("checkpoint error"))
    
    #make sure the right files are there
    numFiles=len(listdir(ckptDir))
    CHECK(numFiles==status[0]*2+1, "unexpected number of checkpoint files, %d procs, %d files" % (status[0], numFiles))
  
  def testRestart():
    #build restart command
    cmd=BIN+"dmtcp_restart"
    for i in listdir(ckptDir):
      if i.endswith(".mtcp"):
        cmd+= " "+ckptDir+"/"+i
    #run restart and test if it worked
    procs.append(launch(cmd))
    WAITFOR(lambda: status==getStatus(), wfMsg("restart error"))
    clearCkptDir()

  try:
    printFixed(name,15)

    if not shouldRunTest(name):
      print "SKIPPED" 
      return

    stats[1]+=1 
    CHECK(getStatus()==(0, False), "coordinator initial state")

    #start user programs
    for cmd in cmds:
      procs.append(launch(BIN+"dmtcp_checkpoint "+cmd))
      sleep(S)
    
    WAITFOR(lambda: status==getStatus(), wfMsg("user program startup error"))
    
    for i in xrange(CYCLES):
      if i!=0 and i%2==0:
        print #newline
        printFixed("",15)
      printFixed("ckpt:")
      testCheckpoint()
      testKill()
      printFixed("PASSED ")

      printFixed("rstr:")
      for i in xrange(RETRIES):
        try:
          testRestart()
          printFixed("PASSED ")
          break
        except CheckFailed, e:
          if i == RETRIES-1:
            raise e
          else:
            printFixed("FAILED retry:")
            testKill()

    testKill()
    print #newline
    stats[0]+=1 

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

  clearCkptDir()

print "== Tests =="

#tmp port
p0=str(randint(2000,10000))
p1=str(randint(2000,10000))
p2=str(randint(2000,10000))
p3=str(randint(2000,10000))

runTest("dmtcp1",        1, ["./test/dmtcp1"])

runTest("shared-fd",     2, ["./test/shared-fd"])

runTest("echoserver",    2, ["./test/echoserver/server "+p0,
                             "./test/echoserver/client localhost "+p0])

runTest("frisbee",       3, ["./test/frisbee "+p1+" localhost "+p2,
                             "./test/frisbee "+p2+" localhost "+p3,
                             "./test/frisbee "+p3+" localhost "+p1+" starter"])

runTest("shared-memory", 2, ["./test/shared-memory"])

runTest("stale-fd",      2, ["./test/stale-fd"])

runTest("forkexec",      2, ["./test/forkexec"])

runTest("gettimeofday",  1, ["./test/gettimeofday"])

os.environ['DMTCP_GZIP'] = "1"
runTest("gzip",          1, ["./test/dmtcp1"])
os.environ['DMTCP_GZIP'] = "0"

runTest("dmtcpaware1",   1, ["./test/dmtcpaware1"])

runTest("perl",          1, ["/usr/bin/perl"])

runTest("python",        1, ["/usr/bin/python"])

if testconfig.HAS_READLINE == "yes":
  runTest("readline",    1,  ["./test/readline"])

if testconfig.HAS_MPICH == "yes":
  runTest("mpd",         1, [testconfig.MPICH_MPD])

  runTest("hellompi-n1", 4, [testconfig.MPICH_MPD,
                             testconfig.MPICH_MPIEXEC+" -n 1 ./test/hellompi"])

  runTest("hellompi-n2", 6, [testconfig.MPICH_MPD,
                             testconfig.MPICH_MPIEXEC+" -n 2 ./test/hellompi"])

  runTest("mpdboot",     1, [testconfig.MPICH_MPDBOOT+" -n 1"])

  #os.system(testconfig.MPICH_MPDCLEANUP)

print "== Summary ==" 
print "%s: %d of %d tests passed" % (socket.gethostname(), stats[0], stats[1])

try:
  SHUTDOWN()
except CheckFailed, e:
  print "Error in SHUTDOWN():", e.value
except:
  print "Error in SHUTDOWN()"


