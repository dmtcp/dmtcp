#!/usr/bin/env python
from random import randint
from time   import sleep
from os     import listdir
import subprocess
import pty
import socket
import os
import sys
import resource
import stat
import re

if sys.version_info[0] != 2 or sys.version_info[0:2] < (2,4):
  print "test/autotest.py works only with Python 2.x for 2.x greater than 2.3"
  print "Change the beginning of test/autotest.py if you believe you can run."
  sys.exit(1)

#get testconfig
# This assumes Makefile.in in main dir, but only Makefile in test dir.
os.system("test -f Makefile || ./configure")
import testconfig

#number of checkpoint/restart cycles
CYCLES=2

#Number of times to try dmtcp_restart
RETRIES=2

#Sleep after each program startup (sec)
DEFAULT_S=0.3
S=DEFAULT_S
#Appears as S*SLOW in code.  If --slow, then SLOW=5
SLOW=1
#In the case of gdb, even if both gdb and the inferior are running after
#ckpt or restart, this does not guarantee that the ptrace related work
#(that is needed at resume or restart) is over. The ptrace related work happens
#in the signal handler. Proceeding while still being inside the signal handler,
#can lead to bad consquences. To play it on the safe side, GDB_SLEEP was
#set at 2 seconds.
if testconfig.PTRACE_SUPPORT == "yes":
  GDB_SLEEP=2 

#Max time to wait for ckpt/restart to finish (sec)
TIMEOUT=10

#Interval between checks for ckpt/restart complete
INTERVAL=0.1

#Buffers for process i/o
BUFFER_SIZE=4096*8

#False redirects process stderr
VERBOSE=False

#Run (most) tests with user default (usually with gzip enable)
GZIP=os.getenv('DMTCP_GZIP') or "1"

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
    CYCLES=100000
  if i=="--slow":
    SLOW=5
  if i=="-h" or i=="--help":
    print ("USAGE "+sys.argv[0]+
      " [-v] [--stress] [--slow] [testname] [testname ...]")
    sys.exit(1)

stats = [0, 0]

# NOTE:  This might be replaced by shell=True in call to subprocess.Popen
def xor(bool1, bool2):
  return (bool1 or bool2) and (not bool1 or not bool2)

def replaceChar(string, index, char):
  return string[0:index] + char + string[index+1:len(string)]

def splitWithQuotes(string):
  inSingleQuotes = False
  inDoubleQuotes = False
  isOuter = False
  escapeChar = False
  for i in range(len(string)):
    if escapeChar:
      escapeChar = False
      continue
    if string[i] == "\\":
      escapeChar = True
      # Remove one level of escaping if same quoting char as isOuter
      string = replaceChar(string, i, '#')
      continue
    if string[i] == "'":
      inSingleQuotes = not inSingleQuotes
    if string[i] == '"':
      inDoubleQuotes = not inDoubleQuotes
    # Remove outermost quotes: 'bash -c "sleep 30"' => ['bash','-c','sleep 30']
    if string[i] == "'" or string[i] == '"':
      # This triggers twice in:  '"..."'  (on first ' and second ")
      if xor(inSingleQuotes, inDoubleQuotes) and not isOuter: # if beg. of quote
	isOuter = string[i]
        string = replaceChar(string, i, '#')
      elif isOuter == string[i]:  # if end of quote
	isOuter = False
        string = replaceChar(string, i, '#')
    if not inSingleQuotes and not inDoubleQuotes and string[i] == ' ':
      # FIXME (Is there any destructive way to do this?)
      string = replaceChar(string, i, '%')
  string = string.replace('#', '')
  return string.split('%')

def shouldRunTest(name):
  # FIXME:  This is a hack.  We should have created var, testNaems and use here
  if len(sys.argv) <= 1+(VERBOSE==True)+(SLOW!=1)+(CYCLES!=2):
    return True
  return name in sys.argv

#make sure we are in svn root
if os.system("test -d bin") is not 0:
  os.chdir("..")
assert os.system("test -d bin") is 0

#make sure dmtcp is built
if os.system("make -s --no-print-directory tests") != 0:
  print "`make all tests` FAILED"
  sys.exit(1)

#pad a string and print/flush it
def printFixed(str, w=1):
  # The comma at end of print prevents a "newline", but still adds space.
  print str.ljust(w),
  sys.stdout.flush()

#exception on failed check
class CheckFailed(Exception):
  def __init__(self, value=""):
    self.value = value

class MySubprocess:
  "dummy class: same fields as from subprocess module"
  def __init__(self, pid):
    self.pid = pid
    self.stdin = os.open(os.devnull, os.O_RDONLY)
    self.stdout = os.open(os.devnull, os.O_WRONLY)
    self.stderr = os.open(os.devnull, os.O_WRONLY)

#launch a child process
# NOTE:  Can eventually migrate to Python 2.7:  subprocess.check_output
childStdoutDevNull = False
def launch(cmd):
  global childStdoutDevNull
  if VERBOSE:
    print "Launching... ", cmd
  cmd = splitWithQuotes(cmd);
  # Example cmd:  dmtcp_checkpoint screen ...
  ptyMode = False
  for str in cmd:
    if re.search("(_|/|^)(screen|script)(_|$)", str):
      ptyMode = True
  try:
    os.stat(cmd[0])
  except:
    raise CheckFailed(cmd[0] + " not found")
  if ptyMode:
    (pid, fd) = os.forkpty()
    if pid == 0:
      # replace stdout; child might otherwise block on writing to stdout
      os.close(1)
      os.open(os.devnull, os.O_WRONLY | os.O_APPEND)
      os.close(2)
      os.open(os.devnull, os.O_WRONLY | os.O_APPEND)
      os.execvp(cmd[0], cmd)
      # os.system( reduce(lambda x,y:x+y, cmd) )
      # os.exit(0)
    else:
      return MySubprocess(pid)
  else:
    childStderr = subprocess.STDOUT # Mix stderr into stdout file object
    if cmd[0] == BIN+"dmtcp_coordinator":
      childStdout = subprocess.PIPE
      childStderr = subprocess.PIPE  # Don't mix stderr in; need to read stdout
    elif VERBOSE:
      childStdout=None  # Inherit child stdout from parent
    else:
      if childStdoutDevNull:
        os.close(childStdoutDevNull)
      childStdout = os.open(os.devnull, os.O_WRONLY)
      childStdoutDevNull = childStdout
    proc = subprocess.Popen(cmd, bufsize=BUFFER_SIZE,
		 stdin=subprocess.PIPE, stdout=childStdout,
		 stderr=childStderr, close_fds=True)
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
  os.environ['JALIB_STDERR_PATH'] = os.devnull

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
    sleep(S*SLOW)
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
        if x.stdin:
	  x.stdin.close()
        if x.stdout:
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
      sleep(S*SLOW)

    WAITFOR(lambda: status==getStatus(), wfMsg("user program startup error"))

    for i in range(CYCLES):
      if i!=0 and i%2==0:
        print #newline
        printFixed("",15)
      printFixed("ckpt:")
      testCheckpoint()
      printFixed("PASSED ")
      if name == "gdb" and testconfig.PTRACE_SUPPORT == "yes":
        sleep(GDB_SLEEP)
      testKill()

      printFixed("rstr:")
      for j in range(RETRIES):
        try:
          testRestart()
          printFixed("PASSED")
          if name == "gdb" and testconfig.PTRACE_SUPPORT == "yes":
            sleep(GDB_SLEEP)
          break
        except CheckFailed, e:
          if j == RETRIES-1:
            raise e
          else:
            printFixed("FAILED retry:")
            testKill()
      if i != CYCLES - 1:
	printFixed(";")

    testKill()
    print #newline
    stats[0]+=1

  except CheckFailed, e:
    print "FAILED"
    printFixed("",15)
    print "root-pids:", map(lambda x: x.pid, procs), "msg:", e.value
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

# Use uniform user shell.  Else apps like script have different subprocesses.
os.environ["SHELL"]="/bin/bash"

runTest("dmtcp1",        1, ["./test/dmtcp1"])

runTest("dmtcp2",        1, ["./test/dmtcp2"])

# dmtcp3 creates 10 threads; Keep checkpoint image small by using gzip
# Also, it needs some extra time to startup
S=2
os.environ['DMTCP_GZIP'] = "1" 
runTest("dmtcp3",        1, ["./test/dmtcp3"])
os.environ['DMTCP_GZIP'] = GZIP
S=DEFAULT_S

runTest("dmtcp4",        1, ["./test/dmtcp4"])

# In 32-bit Ubuntu 9.10, the default small stacksize (8 MB) forces
# legacy_va_layout, which places vdso in low memory.  This collides with text
# in low memory (0x110000) in the statically linked mtcp_restart executable.
oldLimit = resource.getrlimit(resource.RLIMIT_STACK)
# oldLimit[1] is old hard limit
if oldLimit[1] == -1L:
  newCurrLimit = 8L*1024*1024
else:
  newCurrLimit = min(8L*1024*1024, oldLimit[1])
resource.setrlimit(resource.RLIMIT_STACK, [newCurrLimit, oldLimit[1]])
runTest("dmtcp5",        2, ["./test/dmtcp5"])
resource.setrlimit(resource.RLIMIT_STACK, oldLimit)

runTest("shared-fd",     2, ["./test/shared-fd"])

# frisbee creates three processes, each with 14 MB, if no gzip is used
os.environ['DMTCP_GZIP'] = "1"
runTest("frisbee",       3, ["./test/frisbee "+p1+" localhost "+p2,
                             "./test/frisbee "+p2+" localhost "+p3,
                             "./test/frisbee "+p3+" localhost "+p1+" starter"])
os.environ['DMTCP_GZIP'] = "0"

runTest("shared-memory", 2, ["./test/shared-memory"])

runTest("sysv-shm",      2, ["./test/sysv-shm"])

#runTest("stale-fd",      2, ["./test/stale-fd"])

runTest("forkexec",      2, ["./test/forkexec"])

if testconfig.PID_VIRTUALIZATION == "yes":
  runTest("waitpid",      2, ["./test/waitpid"])

runTest("gettimeofday",  1, ["./test/gettimeofday"])

if testconfig.HAS_READLINE == "yes":
  runTest("readline",    1,  ["./test/readline"])

os.environ['DMTCP_GZIP'] = "1"
runTest("gzip",          1, ["./test/dmtcp1"])
os.environ['DMTCP_GZIP'] = GZIP

runTest("dmtcpaware1",   1, ["./test/dmtcpaware1"])

#Invoke this test when we drain/restore data in pty at checkpoint time.
# runTest("pty",   2, ["./test/pty"])

runTest("perl",          1, ["/usr/bin/perl"])

runTest("python",        1, ["/usr/bin/python"])

if testconfig.PID_VIRTUALIZATION == "yes":
  os.environ['DMTCP_GZIP'] = "0"
  runTest("bash",          2, ["/bin/bash --norc -c 'ls; sleep 30; ls'"])
  os.environ['DMTCP_GZIP'] = GZIP

if testconfig.HAS_DASH == "yes":
  os.environ['DMTCP_GZIP'] = "0"
  os.unsetenv('ENV')  # Delete reference to dash initialization file
  runTest("dash",          2, ["/bin/dash -c 'ls; sleep 30; ls'"])
  os.environ['DMTCP_GZIP'] = GZIP

if testconfig.HAS_TCSH == "yes":
  os.environ['DMTCP_GZIP'] = "0"
  runTest("tcsh",          2, ["/bin/tcsh -f -c 'ls; sleep 30; ls'"])
  os.environ['DMTCP_GZIP'] = GZIP

if testconfig.HAS_ZSH == "yes":
  os.environ['DMTCP_GZIP'] = "0"
  runTest("zsh",          2, ["/bin/zsh -f -c 'ls; sleep 30; ls'"])
  os.environ['DMTCP_GZIP'] = GZIP

# runTest("dlopen",          1, ["./test/dlopen"])

if testconfig.HAS_SCRIPT == "yes" and testconfig.PID_VIRTUALIZATION == "yes":
  S=2
  if sys.version_info[0:2] >= (2,6):
    runTest("script",      4,  ["/usr/bin/script -f" +
    			      " -c 'bash -c \"ls; sleep 30\"'" +
    			      " dmtcp-test-typescript.tmp"])
  os.system("rm -f dmtcp-test-typescript.tmp")
  S=DEFAULT_S

# SHOULD HAVE screen RUN SOMETHING LIKE:  bash -c ./test/dmtcp1
# BUT screen -s CMD works only when CMD is single word.
# *** Works manually, but not yet in autotest ***
if testconfig.HAS_SCREEN == "yes" and testconfig.PID_VIRTUALIZATION == "yes":
  S=1
  if sys.version_info[0:2] >= (2,6):
    runTest("screen",      3,  [testconfig.SCREEN + " -c /dev/null -s /bin/sh"])
  S=DEFAULT_S

# SHOULD HAVE gcl RUN LARGE FACTORIAL OR SOMETHING.
if testconfig.HAS_GCL == "yes":
  S=1
  runTest("gcl",         1,  [testconfig.GCL])
  S=DEFAULT_S

# SHOULD HAVE matlab RUN LARGE FACTORIAL OR SOMETHING.
if testconfig.HAS_MATLAB == "yes":
  S=5
  if sys.version_info[0:2] >= (2,6):
    runTest("matlab -nodisplay", 1,  [testconfig.MATLAB+" -nodisplay -nojvm"])
  S=DEFAULT_S

if testconfig.PTRACE_SUPPORT == "yes":
  os.system("echo 'run' > dmtcp-gdbinit.tmp")
  S=2
  if sys.version_info[0:2] >= (2,6):
    runTest("gdb", 2,  ["gdb -n -batch -x dmtcp-gdbinit.tmp test/dmtcp1"])
  S=DEFAULT_S
  os.system("rm -f dmtcp-gdbinit.tmp")

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

exit( stats[1] - stats[0] )  # Return code is number of failing tests.
