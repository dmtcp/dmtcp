#!/usr/bin/env python
from random import randint
from time   import sleep
from os     import listdir
import subprocess
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
TIMEOUT=15

#Interval between checks for ckpt/restart complete
INTERVAL=0.1

#Buffers for process i/o
BUFFER_SIZE=4096*8

#False redirects process stderr
VERBOSE=False

#Run (most) tests with gzip enable 
GZIP="1"

#Warn cant create a file of size:
REQUIRE_MB=50

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
    print "USAGE "+sys.argv[0]+" [-v] [--stress] [testname] [testname...]  "
    sys.exit(1)

stats = [0, 0]

def shouldRunTest(name):
  if len(sys.argv) <= 1:
    return True
  return args.has_key(name)

#make sure we are in svn root
if os.system("test -d bin") is not 0:
  os.chdir("..")
assert os.system("test -d bin") is 0

#make sure dmtcp is built
if os.system("make -s --no-print-directory all tests") != 0:
  print "`make all tests` FAILED"
  sys.exit(1)

#pad a string and print/flush it
def printFixed(str, w=1):
  print str.ljust(w),
  sys.stdout.flush()

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
  proc = subprocess.Popen(cmd, bufsize=BUFFER_SIZE,
		 stdin=subprocess.PIPE, stdout=subprocess.PIPE,
		 stderr=subprocess.PIPE if not VERBOSE else None,
		 close_fds=True)
  return proc

#randomize port and dir, so multiple processes works
ckptDir="dmtcp-autotest-%d" % randint(100000000,999999999)
os.mkdir(ckptDir);
os.environ['DMTCP_HOST'] = "localhost"
os.environ['DMTCP_PORT'] = str(randint(2000,10000))
os.environ['DMTCP_CHECKPOINT_DIR'] = os.path.abspath(ckptDir)
#Use default SIGCKPT for test suite.
os.unsetenv('DMTCP_SIGCKPT')
os.unsetenv('MTCP_SIGCKPT')
#No gzip by default.  (Isolate gzip failures from other test failures.)
#But note that dmtcp3, frisbee and gzip tests below still use gzip.
if not VERBOSE:
  os.environ['JALIB_STDERR_PATH'] = "/dev/null"

#verify there is enough free space
tmpfile=ckptDir + "/freeSpaceTest.tmp"
if os.system("dd if=/dev/zero of="+tmpfile+" bs=1MB count="+str(REQUIRE_MB)+" 2>/dev/null") != 0:
  GZIP="1"
  print '''

!!!WARNING!!!
Fewer than '''+str(REQUIRE_MB)+'''MB are available on the current volume.
Many of the tests below may fail due to insufficient space.
!!!WARNING!!!

'''
os.system("rm -f "+tmpfile)

os.environ['DMTCP_GZIP'] = GZIP

#launch the coordinator
coordinator = launch(BIN+"dmtcp_coordinator")

#send a command to the coordinator process
def coordinatorCmd(cmd):
  try:
    if VERBOSE and cmd != "s":
      print "COORDINATORCMD(",cmd,")"
    coordinator.stdin.write(cmd+"\n")
    coordinator.stdin.flush()
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

#wait TIMEOUT for test() to be true, or throw error
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
      line=coordinator.stdout.readline().strip()
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

  x,peers=coordinator.stdout.readline().strip().split("=")
  CHECK(x=="NUM_PEERS", "reading coordinator status")
  x,running=coordinator.stdout.readline().strip().split("=")
  CHECK(x=="RUNNING", "reading coordinator status")

  if VERBOSE:
    print "STATUS: peers=%s, running=%s" % (peers,running)
  return (int(peers), (running=="yes"))

#delete all files in ckpDir
def clearCkptDir():
    #clear checkpoint dir
    for root, dirs, files in os.walk(ckptDir, topdown=False):
      for name in files:
        os.remove(os.path.join(root, name))
      for name in dirs:
        os.rmdir(os.path.join(root, name))

def getNumCkptFiles(dir):
  return len(filter(lambda f: f.startswith("ckpt_") and f.endswith(".dmtcp"), listdir(dir)))


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
	x.stdin.close()
        x.stdout.close()
        if x.stderr:
          x.stderr.close()
        os.waitpid(x.pid, os.WNOHANG)
      except:
        None
      procs.remove(x)

  def testCheckpoint():
    #start checkpoint
    coordinatorCmd('c')

    #wait for files to appear and status to return to original
    WAITFOR(lambda: getNumCkptFiles(ckptDir)>0 and status==getStatus(),
            wfMsg("checkpoint error"))

    #make sure the right files are there
    numFiles=getNumCkptFiles(ckptDir) # len(listdir(ckptDir))
    CHECK(numFiles==status[0], "unexpected number of checkpoint files, %d procs, %d files" % (status[0], numFiles))

  def testRestart():
    #build restart command
    cmd=BIN+"dmtcp_restart --quiet"
    for i in listdir(ckptDir):
      if i.endswith(".dmtcp"):
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

runTest("dmtcp2",        1, ["./test/dmtcp2"])

# dmtcp3 creates 10 threads; Keep checkpoint image small by using gzip
# Also, it needs some extra time to startup
S=3
os.environ['DMTCP_GZIP'] = "1" 
runTest("dmtcp3",        1, ["./test/dmtcp3"])
os.environ['DMTCP_GZIP'] = GZIP
S=0.3

runTest("dmtcp4",        1, ["./test/dmtcp4"])

runTest("shared-fd",     2, ["./test/shared-fd"])

# frisbee creates three processes, each with 14 MB, if no gzip is used
os.environ['DMTCP_GZIP'] = "1"
runTest("frisbee",       3, ["./test/frisbee "+p1+" localhost "+p2,
                             "./test/frisbee "+p2+" localhost "+p3,
                             "./test/frisbee "+p3+" localhost "+p1+" starter"])
os.environ['DMTCP_GZIP'] = "0"

runTest("shared-memory", 2, ["./test/shared-memory"])

runTest("stale-fd",      2, ["./test/stale-fd"])

runTest("forkexec",      2, ["./test/forkexec"])

runTest("gettimeofday",  1, ["./test/gettimeofday"])

os.environ['DMTCP_GZIP'] = "1"
runTest("gzip",          1, ["./test/dmtcp1"])
os.environ['DMTCP_GZIP'] = GZIP

runTest("dmtcpaware1",   1, ["./test/dmtcpaware1"])

runTest("perl",          1, ["/usr/bin/perl"])

runTest("python",        1, ["/usr/bin/python"])

os.environ['DMTCP_GZIP'] = "0"
runTest("bash",          1, ["/bin/bash"])
os.environ['DMTCP_GZIP'] = GZIP

#if testconfig.HAS_GCL == "yes":
#  runTest("gcl",         1,  ["/usr/bin/gcl"])

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


