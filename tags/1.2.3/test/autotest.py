#!/usr/bin/env python
from random import randint
from time   import sleep
from os     import listdir
import subprocess
import pty
import socket
import os
import sys
import errno
import signal
import resource
import pwd
import stat
import re

signal.alarm(900)

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
#ckpt or restart, this does not guarantee that the ptrace-related work
#(that is needed at resume or restart) is over. The ptrace related work happens
#in the signal handler. Proceeding while still being inside the signal handler,
#can lead to bad consquences. To play it on the safe side, PTRACE_SLEEP was
#set at 2 seconds.  (Until this is fixed, --enable-ptrace-support will
#remain experimental.)
if testconfig.PTRACE_SUPPORT == "yes":
  PTRACE_SLEEP=2

#Max time to wait for ckpt/restart to finish (sec)
# Consider raising this value when /usr/lib/locale/locale-archive is 100MB.
# This can happen on Red Hat-derived distros.
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

def master_read(fd):
  os.read(fd, 4096)
  return ''

#launch a child process
# NOTE:  Can eventually migrate to Python 2.7:  subprocess.check_output
devnullFd = os.open(os.devnull, os.O_WRONLY)
def launch(cmd):
  global devnullFd
  global master_read
  if VERBOSE:
    print "Launching... ", cmd
  cmd = splitWithQuotes(cmd);
  # Example cmd:  dmtcp_checkpoint screen ...
  ptyMode = False
  for str in cmd:
    # Checkpoint image can be emacs23_x, or whatever emacs is a link to.
    # vim can be vim.gnome, etc.
    if re.search("(_|/|^)(screen|script|vim.*|emacs.*|pty)(_|$)", str):
      ptyMode = True
  try:
    os.stat(cmd[0])
  except:
    raise CheckFailed(cmd[0] + " not found")
  if ptyMode:
    (pid, fd) = pty.fork()
    if pid == 0:
      signal.alarm(300) # pending alarm inherited across an exec, but not a fork
      pty.spawn(cmd, master_read)
      sys.exit(0)
    else:
      return MySubprocess(pid)
  else:
    if cmd[0] == BIN+"dmtcp_coordinator":
      childStdout = subprocess.PIPE
      # Don't mix stderr in with childStdout; need to read stdout
      if VERBOSE:
        childStderr = None
      else:
        childStderr = devnullFd
    elif VERBOSE:
      childStdout=None  # Inherit child stdout from parent
      childStderr=None  # Inherit child stderr from parent
    else:
      childStdout = devnullFd
      childStderr = subprocess.STDOUT # Mix stderr into stdout file object
    # NOTE:  This might be replaced by shell=True in call to subprocess.Popen
    # FIXME:  Should call signal.alarm(300), but it would be reset on fork().
    #         We could rewrite Popen in terms of fork() and exec().
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

# Temporary hack until DMTCP cleans up when using --enable-ptrace-support
def deletePtraceFiles():
  tmpdir = os.getenv("TMPDIR", "/tmp")  # if "TMPDIR" not set, return "/tmp"
  tmpdir += "/dmtcp-" + pwd.getpwuid(os.getuid()).pw_name + \
            "@" + socket.gethostname()
  os.system("cd "+tmpdir+"; "+
	    "rm -f ptrace_shared.txt ptrace_setoptions.txt \
	     ptrace_ckpthreads.txt ptrace_shared.txt ptrace_setoptions.txt \
	     ptrace_ckpthreads.txt new_ptrace_shared.txt ckpt_leader_file.txt")

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
  os.close(devnullFd)

#make sure val is true
def CHECK(val, msg):
  if not val:
    raise CheckFailed(msg)

#wait TIMEOUT for test() to be true, or throw error
def WAITFOR(test, msg):
  left=TIMEOUT*(S/DEFAULT_S)/INTERVAL
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

#delete all files in ckptDir
def clearCkptDir():
  for TRIES in range(2):  # Try twice in case ckpt_*_dmtcp.temp is renamed.
    #clear checkpoint dir
    for root, dirs, files in os.walk(ckptDir, topdown=False):
      for name in files:
        try:
          os.remove(os.path.join(root, name))
        except OSError, e:
	  if e.errno != errno.ENOENT:  # Maybe ckpt_*_dmtcp.temp was renamed.
	    raise e
      for name in dirs:
        os.rmdir(os.path.join(root, name))

def getNumCkptFiles(dir):
  return len(filter(lambda f: f.startswith("ckpt_") and f.endswith(".dmtcp"), listdir(dir)))


#test a given list of commands to see if they checkpoint
def runTest(name, numProcs, cmds):
  #the expected/correct running status
  if testconfig.USE_M32 == "1":
    def forall(fnc, lst):
      return reduce(lambda x, y: x and y, map(fnc, lst))
    if not forall(lambda x: x.startswith("./test/"), cmds):
      return
  status=(numProcs, True)
  procs=[]

  def doesStatusSatisfy(newStatus,requiredStatus):
    if isinstance(requiredStatus[0], int):
      statRange = [requiredStatus[0]]
    elif isinstance(requiredStatus[0], str):
      statRange = eval(requiredStatus[0])
    else:
      raise NotImplementedError
    return newStatus[0] in statRange and newStatus[1] == requiredStatus[1]

  def wfMsg(msg):
    #return function to generate error message
    return lambda: msg+", "+str(status[0])+" expected, %d found, running=%d" % getStatus()

  def testKill():
    #kill all processes
    coordinatorCmd('k')
    try:
      WAITFOR(lambda: getStatus()==(0, False),
	      lambda:"coordinator kill command failed")
    except CheckFailed:
      global coordinator
      coordinatorCmd('q')
      os.system("kill -9 %d" % coordinator.pid)
      print "Trying to kill old coordinator, and launch new one on same port"
      coordinator = launch(BIN+"dmtcp_coordinator")
    for x in procs:
      #cleanup proc
      try:
        if isinstance(x.stdin,int):
          os.close(x.stdin)
        elif x.stdin:
          x.stdin.close()
        if isinstance(x.stdout,int):
          os.close(x.stdout)
        elif x.stdout:
          x.stdout.close()
        if isinstance(x.stderr,int):
          os.close(x.stderr)
        elif x.stderr:
          x.stderr.close()
      except:
        None
      try:
        os.waitpid(x.pid, os.WNOHANG)
      except OSError, e:
	if e.errno != errno.ECHILD:
	  raise e
      procs.remove(x)

  def testCheckpoint():
    #start checkpoint
    coordinatorCmd('c')

    #wait for files to appear and status to return to original
    WAITFOR(lambda: getNumCkptFiles(ckptDir)>0 and \
                    doesStatusSatisfy(getStatus(), status),
            wfMsg("checkpoint error"))

    #make sure the right files are there
    numFiles=getNumCkptFiles(ckptDir) # len(listdir(ckptDir))
    CHECK(doesStatusSatisfy((numFiles,True),status),
          "unexpected number of checkpoint files, %s procs, %d files"
          % (str(status[0]), numFiles))

  def testRestart():
    #build restart command
    cmd=BIN+"dmtcp_restart --quiet"
    for i in listdir(ckptDir):
      if i.endswith(".dmtcp"):
        cmd+= " "+ckptDir+"/"+i
    #run restart and test if it worked
    procs.append(launch(cmd))
    WAITFOR(lambda: doesStatusSatisfy(getStatus(), status),
            wfMsg("restart error"))
    if testconfig.HBICT_DELTACOMP == "no":
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

    WAITFOR(lambda: doesStatusSatisfy(getStatus(), status),
            wfMsg("user program startup error"))

    for i in range(CYCLES):
      if i!=0 and i%2==0:
        print #newline
        printFixed("",15)
      printFixed("ckpt:")
      # NOTE:  If this faile, it will throw an exception to CheckFailed
      #  of this function:  testRestart
      testCheckpoint()
      printFixed("PASSED ")
      if testconfig.PTRACE_SUPPORT == "yes":
        sleep(PTRACE_SLEEP)
      testKill()

      printFixed("rstr:")
      for j in range(RETRIES):
        try:
          testRestart()
          printFixed("PASSED")
          if testconfig.PTRACE_SUPPORT == "yes":
            sleep(PTRACE_SLEEP)
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

runTest("dmtcpaware1",   1, ["./test/dmtcpaware1"])

runTest("module-sleep2", 1, ["--with-module "+
			     "$PWD/module/sleep1/dmtcp_sleep1hijack.so:"+
			     "$PWD/module/sleep2/dmtcp_sleep2hijack.so "+
			     "./test/dmtcp1"])

runTest("module-example-db", 2, ["--with-module "+
			    "$PWD/module/example-db/dmtcp_example-dbhijack.so "+
			     "env EXAMPLE_DB_KEY=1 EXAMPLE_DB_KEY_OTHER=2 "+
			     "./test/dmtcp1",
			         "--with-module "+
			    "$PWD/module/example-db/dmtcp_example-dbhijack.so "+
			     "env EXAMPLE_DB_KEY=2 EXAMPLE_DB_KEY_OTHER=1 "+
			     "./test/dmtcp1"])

# Test special case:  gettimeofday can be handled within VDSO segment.
runTest("gettimeofday",  1, ["./test/gettimeofday"])

runTest("sigchild",      1, ["./test/sigchild"])

runTest("shared-fd",     2, ["./test/shared-fd"])

runTest("stale-fd",      2, ["./test/stale-fd"])

runTest("forkexec",      2, ["./test/forkexec"])

if testconfig.PID_VIRTUALIZATION == "yes":
  runTest("waitpid",      2, ["./test/waitpid"])

runTest("client-server", 2, ["./test/client-server"])

# frisbee creates three processes, each with 14 MB, if no gzip is used
os.environ['DMTCP_GZIP'] = "1"
runTest("frisbee",       3, ["./test/frisbee "+p1+" localhost "+p2,
                             "./test/frisbee "+p2+" localhost "+p3,
                             "./test/frisbee "+p3+" localhost "+p1+" starter"])
os.environ['DMTCP_GZIP'] = "0"

runTest("shared-memory", 2, ["./test/shared-memory"])

runTest("sysv-shm",      2, ["./test/sysv-shm"])

#Invoke this test when we drain/restore data in pty at checkpoint time.
# runTest("pty",   2, ["./test/pty"])

old_ld_library_path = os.getenv("LD_LIBRARY_PATH")
os.environ['LD_LIBRARY_PATH'] = os.getenv("PWD")+"/test:"+os.getenv("PWD")
runTest("dlopen",        1, ["./test/dlopen"])
if old_ld_library_path:
  os.environ['LD_LIBRARY_PATH'] = old_ld_library_path
else:
  del os.environ['LD_LIBRARY_PATH']

os.environ['DMTCP_GZIP'] = "1"
runTest("gzip",          1, ["./test/dmtcp1"])
os.environ['DMTCP_GZIP'] = GZIP

if testconfig.HAS_READLINE == "yes":
  runTest("readline",    1,  ["./test/readline"])

runTest("perl",          1, ["/usr/bin/perl"])

if testconfig.HAS_PYTHON == "yes":
  runTest("python",      1, ["/usr/bin/python"])

if testconfig.PID_VIRTUALIZATION == "yes":
  os.environ['DMTCP_GZIP'] = "0"
  runTest("bash",        2, ["/bin/bash --norc -c 'ls; sleep 30; ls'"])
  os.environ['DMTCP_GZIP'] = GZIP

if testconfig.HAS_DASH == "yes":
  os.environ['DMTCP_GZIP'] = "0"
  os.unsetenv('ENV')  # Delete reference to dash initialization file
  runTest("dash",        2, ["/bin/dash -c 'ls; sleep 30; ls'"])
  os.environ['DMTCP_GZIP'] = GZIP

if testconfig.HAS_TCSH == "yes":
  os.environ['DMTCP_GZIP'] = "0"
  runTest("tcsh",        2, ["/bin/tcsh -f -c 'ls; sleep 30; ls'"])
  os.environ['DMTCP_GZIP'] = GZIP

if testconfig.HAS_ZSH == "yes":
  os.environ['DMTCP_GZIP'] = "0"
  runTest("zsh",         2, ["/bin/zsh -f -c 'ls; sleep 30; ls'"])
  os.environ['DMTCP_GZIP'] = GZIP

if testconfig.HAS_VIM == "yes" and testconfig.PID_VIRTUALIZATION == "yes":
  # Wait to checkpoint until vim finishes reading its initialization files
  S=3
  if sys.version_info[0:2] >= (2,6):
    runTest("vim",       1,  ["env TERM=vt100 /usr/bin/vim /etc/passwd"])
  S=DEFAULT_S

if testconfig.HAS_EMACS == "yes" and testconfig.PID_VIRTUALIZATION == "yes":
  # Wait to checkpoint until emacs finishes reading its initialization files
  S=4
  if sys.version_info[0:2] >= (2,6):
    # Under emacs23, it opens /dev/tty directly in a new fd.
    # To avoid this, consider using emacs --batch -l EMACS-LISTP-CODE ...
    # ... or else a better pty wrapper to capture emacs output to /dev/tty.
    runTest("emacs",     1,  ["env TERM=vt100 /usr/bin/emacs -nw" +
                              " --no-init-file /etc/passwd"])
  S=DEFAULT_S

if testconfig.HAS_SCRIPT == "yes" and testconfig.PID_VIRTUALIZATION == "yes":
  S=2
  if sys.version_info[0:2] >= (2,6):
    # NOTE:  If 'script' fails, try raising value of S, above, to larger number.
    #   Arguably, there is a bug in glibc, in that locale-archive can be 100 MB.
    #   For example, in Fedora 13 (and other recent Red Hat-derived distros?),
    #   /usr/lib/locale/locale-archive is 100 MB, and yet 'locale -a |wc' shows
    #   only 8KB of content in ASCII.  The 100 MB of locale-archive condenses
    #   to 25 MB _per process_ under gzip, but this can be slow at ckpt time.
    runTest("script",    4,  ["/usr/bin/script -f" +
    			      " -c 'bash -c \"ls; sleep 30\"'" +
    			      " dmtcp-test-typescript.tmp"])
  os.system("rm -f dmtcp-test-typescript.tmp")
  S=DEFAULT_S

# SHOULD HAVE screen RUN SOMETHING LIKE:  bash -c ./test/dmtcp1
if testconfig.HAS_SCREEN == "yes" and testconfig.PID_VIRTUALIZATION == "yes":
  S=1
  if sys.version_info[0:2] >= (2,6):
    host = socket.getfqdn()
    runTest("screen",    3,  ["env TERM=vt100 " + testconfig.SCREEN +
                                " -c /dev/null -s /bin/sh"])
  S=DEFAULT_S

if testconfig.PTRACE_SUPPORT == "yes" and \
   (testconfig.HAS_STRACE == "yes" or testconfig.HAS_GDB == "yes"):
  print "  Deleting files in /tmp/dmtcp-USER@HOST before ptrace tests.  (Until"
  print "  this is fixed, --enable-ptrace-support will remain experimental.)"
  deletePtraceFiles()
  if testconfig.HAS_STRACE == "yes" and testconfig.PTRACE_SUPPORT == "yes":
    S=1
    if sys.version_info[0:2] >= (2,6):
      runTest("strace",    2,  ["strace test/dmtcp2"])
    S=DEFAULT_S

  deletePtraceFiles()
  if testconfig.HAS_GDB == "yes" and testconfig.PTRACE_SUPPORT == "yes":
    os.system("echo 'run' > dmtcp-gdbinit.tmp")
    S=2
    if sys.version_info[0:2] >= (2,6):
      runTest("gdb",       2,  ["gdb -n -batch -x dmtcp-gdbinit.tmp test/dmtcp1"])
    S=DEFAULT_S
    os.system("rm -f dmtcp-gdbinit.tmp")

# SHOULD HAVE gcl RUN LARGE FACTORIAL OR SOMETHING.
if testconfig.HAS_GCL == "yes":
  S=1
  runTest("gcl",         1,  [testconfig.GCL])
  S=DEFAULT_S

# SHOULD HAVE matlab RUN LARGE FACTORIAL OR SOMETHING.
if testconfig.HAS_MATLAB == "yes":
  S=3
  if sys.version_info[0:2] >= (2,6):
    runTest("matlab-nodisplay", 1,  [testconfig.MATLAB+" -nodisplay -nojvm"])
  S=DEFAULT_S

if testconfig.HAS_MPICH == "yes":
  runTest("mpd",         1, [testconfig.MPICH_MPD])

  runTest("hellompich-n1", 4, [testconfig.MPICH_MPD,
                           testconfig.MPICH_MPIEXEC+" -n 1 ./test/hellompich"])

  runTest("hellompich-n2", 6, [testconfig.MPICH_MPD,
                           testconfig.MPICH_MPIEXEC+" -n 2 ./test/hellompich"])

  runTest("mpdboot",     1, [testconfig.MPICH_MPDBOOT+" -n 1"])

  #os.system(testconfig.MPICH_MPDCLEANUP)

# Temporarily disabling OpenMPI test as it fails on some distros (OpenSUSE 11.4)
if testconfig.HAS_OPENMPI == "yes":
  numProcesses = 5 + int(testconfig.USES_OPENMPI_ORTED == "yes")
  # FIXME: Replace "[5,6]" by numProcesses when bug in configure is fixed.
  runTest("openmpi", "[5,6]", [testconfig.OPENMPI_MPIRUN + " -np 4" +
			     " ./test/openmpi"])

print "== Summary =="
print "%s: %d of %d tests passed" % (socket.gethostname(), stats[0], stats[1])

if testconfig.DEBUG == "yes":
  host = socket.getfqdn()
  if re.search("^nmi-.*.cs.wisc.edu$", host) or \
     re.search("^nmi-.*.cs.wisconsin.edu$", host):
    tmpdir = os.getenv("TMPDIR", "/tmp")  # if "TMPDIR" not set, return "/tmp"
    target = "./dmtcp-" + pwd.getpwuid(os.getuid()).pw_name + \
             "@" + socket.gethostname()
    cmd = "mkdir results; cp -pr " + tmpdir + "/" + target + \
	     " ./dmtcp/src/dmtcphijack.so" + " ./mtcp/libmtcp.so" + " results/"
    os.system(cmd)
    cmd = "tar zcf ../results.tar.gz ./results; rm -rf results"
    os.system(cmd)
    print "\n*** results.tar.gz ("+tmpdir+"/"+target+ \
					      ") written to DMTCP_ROOT/.. ***"

try:
  SHUTDOWN()
except CheckFailed, e:
  print "Error in SHUTDOWN():", e.value
except:
  print "Error in SHUTDOWN()"

sys.exit( stats[1] - stats[0] )  # Return code is number of failing tests.
