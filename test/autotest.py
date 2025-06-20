#!/usr/bin/env python3

# NOTE: .github/workflows/make-check.yml at DMTCP_ROOT calls autotest.py

from random import randint
from time   import sleep
import argparse
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

# FIX for bad path for Java:  Previously, Travis prepended
#     "/usr/bin:/opt/pyenv/libexec:/opt/pyenv/plugins/python-build/bin:/"
# to os.environ['PATH'] on July 31, 2019.  It does this, even though
# "/usr/bin" occurs later in the path.  /usr/bin/java exists as Java-8.
# So, it never finds java (version Java-11) in
#   "/usr/local/lib/jvm/openjdk11/bin", which occurs later in path.
# Unfortunately, 'Makefile' finds javac as verion Javac-11,
# and so the 'java1' test fails, using java (Java-8).
if 'PATH' in os.environ and os.environ['PATH'].startswith('/usr/bin:') \
     and ':/usr/bin:' in os.environ['PATH']:
  os.environ['PATH'] = os.environ['PATH'][len('/usr/bin:'):]

parser = argparse.ArgumentParser()
parser.add_argument('-v', '--verbose',
                    action='store_true',
                    help='Print JTRACE/JLOG/JWARNING messages')
#parser.add_argument('-l', '--list',
#                    action='store_true',
#                    help='List available tests')
parser.add_argument('--retry-once',
                    action='store_true',
                    help='Retry the test in case of failure')
parser.add_argument('--stress',
                    action='store_true',
                    help='Run 100000 ckpt-rst cycles')
parser.add_argument('--slow',
                    action='count',
                    default=0,
                    help='Add additional pause before ckpt-rst')
PARALLEL_JOBS = 5
parser.add_argument('--parallel',
                    action='store_true',
                    default=0,
                    help='Run ' + str(PARALLEL_JOBS) +
                                          ' tests at a time in parallel')
parser.add_argument('tests',
                    nargs='*',
                    metavar='TESTNAME',
                    help='Test to run')

args = parser.parse_args()

# stats[0] is number passed; stats[1] is total number
stats = [0, 0]

failed_tests = []

disabled_tests = [
  # Vfork is not currently supported.
  #"vfork1", # We simulate vfork using fork.
  # vfork1 and vfork2 tests were failing due to a previous bug in vfork.c
  #"vfork2",

  # The emacs test currently fails on github during restart due to huge ckpt
  # image sizes (~500MB). A proper fix would be to diagnose the cause for the
  # unexpectedly large image size. If the large sizes are inevitable, we can
  # edit this script to ignore failures on ckpt images that are >~100MB.
  "emacs",

  # This test needs to be fixed.  It is not really running.
  # ERROR: ld.so: object '/home/gene/dmtcp.git/test/plugin/example-db/dmtcp_example-dbhijack.so' from LD_PRELOAD cannot be preloaded (cannot open shared object file): ignored.
  "dmtcp_example-db"
]

# if 'autotest.py --parallel', then initialize tests and test_dict.
# If '--slow' was also used, background parallel jobs will be in fast
#  mode, but if they fail, they will be tried again in foreground in slow mode.
if args.parallel:
  autotest_path = os.getcwd() + '/' + sys.argv[0]
  if args.tests == []:
    all_tests = subprocess.Popen(
      sys.executable + " " + autotest_path + " NO_SUCH_TEST | grep SKIPPED$",
      shell=True, stdout=subprocess.PIPE)
    all_tests = str(all_tests.communicate()[0].decode("UTF-8"))
    all_tests = all_tests.replace('SKIPPED', '').split()
    tests = [test for test in all_tests]
    test_dict = {tests[i]:i for i in range(len(tests))}
  else:
    args.parallel = False
    print("\n*** autotest.py: '--parallel' ignored;" +
          " requires Python 3.5 or later. ***\n")

if args.stress or args.tests:
  testname_width = 15
else:
  testname_width = 0 # Will use DEFAULT_TESTNAME_WIDTH

def parallel_test(name):
  global tests, test_dict, autotest_path
  num_jobs = 0
  for i in range(test_dict[name]+1, len(tests)):
    if num_jobs >= PARALLEL_JOBS:
      break;
    if isinstance(tests[i], subprocess.Popen) and tests[i].poll() == None:
      num_jobs += 1  # job still executing
    elif isinstance(tests[i], str):
      tests[i] = subprocess.Popen( # Example: exec python3 autotest.py dmtcp1
                   "exec " + sys.executable + " " + autotest_path + " " +
                     tests[i] + " > /dev/null 2>&1",
                   shell=True)
      num_jobs += 1  # new job to execute
  if isinstance(tests[test_dict[name]], subprocess.Popen):
    if tests[test_dict[name]].poll() == 0:
      printFixed(name, DEFAULT_TESTNAME_WIDTH)
      print("ckpt:PASSED; rstr:PASSED -> ckpt:PASSED; rstr:PASSED")
      stats[1] += 1 # one more job done in parallel
      stats[0] += 1 # and this parallel job has passed
      return True  # Test succeeded
    else:
      try:
        tests[test_dict[name]].kill()
        tests[test_dict[name]].wait()
      except OSError:
        pass
  return False  # Test failed

#get testconfig
# This assumes Makefile.in in main dir, but only Makefile in test dir.
try:
  sys.path += [os.getenv("PWD") + '/test', os.getenv('PWD')]
  from autotest_config import *

except ImportError:
  print("\n*** Error importing autotest_config.py: ")
  sys.exit()

if USE_TEST_SUITE == "no":
  print("\n*** DMTCP test suite is disabled. To re-enable the test suite,\n" +
        "***  re-configure _without_ './configure --disable-test-suite'\n")
  sys.exit()

signal.alarm(1800)  # half hour

if sys.version_info[0] < 3:
  print("test/autotest.py works only with Python 3.x")
  print("Change this portion of test/autotest.py if you believe you can run.")
  sys.exit(1)

uname_m = subprocess.Popen(['uname', '-m'],
                           stdout=subprocess.PIPE).communicate()[0]
uname_p = subprocess.Popen(['uname', '-p'],
                           stdout=subprocess.PIPE).communicate()[0]

if USE_TEST_SUITE == "no":
  print("\n*** DMTCP test suite is disabled. To re-enable the test suite,\n" +
          "***  re-configure _without_ './configure --disable-test-suite'\n")
  sys.exit()

#Number of times to try dmtcp_restart
RETRIES=2

# We want the --stress' flag to still print output within 80 columns.
DEFAULT_TESTNAME_WIDTH = testname_width or 20

#Sleep after each program startup (sec)
DEFAULT_S=0.3
if uname_p[0:3] == 'arm':
  DEFAULT_S *= 2

# Sleep before the first ckpt _only_.
DEFAULT_POST_LAUNCH_SLEEP = 0.0
POST_LAUNCH_SLEEP = 0.0

# Sleep after launching restart, but before checking it
DEFAULT_POST_RESTART_SLEEP = 0.0
POST_RESTART_SLEEP = 0.0

uname_m = uname_m.strip() # strip off any whitespace characters
#Allow extra time for slower CPUs
if uname_m in ["i386", "i486", "i586", "i686", "armv7", "armv7l", "aarch64"]:
  DEFAULT_S *= 4

S=DEFAULT_S

#Max time to wait for ckpt/restart to finish (sec)
TIMEOUT=10
# Raise this value when /usr/lib/locale/locale-archive is 100MB.
# This can happen on Red Hat-derived distros.
if os.path.exists("/usr/lib/locale/locale-archive") and \
   os.path.getsize("/usr/lib/locale/locale-archive") > 10e6:
  TIMEOUT *= int( os.path.getsize("/usr/lib/locale/locale-archive") / 10e6 )

#Interval between checks for ckpt/restart complete
INTERVAL=0.1

#Buffers for process i/o
BUFFER_SIZE=4096*8

#Run (most) tests with user default (usually with gzip enable)
GZIP=os.getenv('DMTCP_GZIP') or "1"

#Warn if can't create a file of size:
REQUIRE_MB=50
REQUIRE_TMP_MB=10

#Binaries
BIN="./bin/"

# cmdline string to launch coord: times out after 3 hours to prevent runaways
coordinator_cmdline = BIN+"dmtcp_coordinator --timeout 10800 --daemon"
command_cmdline = BIN+"dmtcp_command" # -p " + str(coordinator_port)

#Checkpoint command to send to coordinator
CKPT_CMD = 'c'

#Appears as S*SLOW in code.  If --slow, then SLOW=5
SLOW = pow(5, args.slow)
TIMEOUT *= SLOW

#number of checkpoint/restart cycles
if args.stress:
    CYCLES = 100000
else:
    CYCLES=2


#TODO:  Install SIGSEGV handler with infinite loop, and add to LD_PRELOAD
#In test/Makefile, build libcatchsigsegv.so
#Add --catchsigsegv  to usage string.
# if args.catch_sigsegv:
#   if os.getenv('LD_PRELOAD'):
#     os.environ['LD_PRELOAD'] += ':libcatchsigsegv.so'
#   else:
#     os.environ['LD_PRELOAD'] = 'libcatchsigsegv.so'

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
  return args.tests == [] or name in args.tests

#make sure we are in github root
if not os.path.isfile('./bin/dmtcp_launch'):
  os.chdir("..")

if not os.path.isfile('./bin/dmtcp_launch'):
  print("bin/dmtcp_launch not found.\n"
        "Please configure and build DMTCP before invoking autotest.py.")
  sys.exit(1)

#pad a string and print/flush it
def printFixed(string, width=1):
  os.write(sys.stdout.fileno(), string.ljust(width).encode("ascii"))
  sys.stdout.flush()

COLOR_RED = "\033[0;31m"
COLOR_RESET = "\033[0m"

def printFixedError(msg):
  os.write(sys.stdout.fileno(), COLOR_RED.encode("ascii"))
  printFixed(msg)
  os.write(sys.stdout.fileno(), COLOR_RESET.encode("ascii"))
  sys.stdout.flush()

def printError(msg):
  os.write(sys.stdout.fileno(),
           (COLOR_RED + msg + COLOR_RESET + "\n").encode("ascii"))
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

def runDmtcpCommand(cmd, waitForOutput=True):
  global command_cmdline

  cmdline = command_cmdline + " " + cmd
  cmdline = cmdline.split()
  #print("Running...", cmdline)

  proc = subprocess.Popen(cmdline, bufsize=BUFFER_SIZE,
                stdin=None, stdout=subprocess.PIPE,
                stderr=None, close_fds=True)

  if waitForOutput:
    output = proc.stdout.readlines()
  else:
    output = None

  return output

#run a child process
# NOTE:  Can eventually migrate to Python 2.7:  subprocess.check_output
devnullFd = os.open(os.devnull, os.O_WRONLY)
def runCmd(cmd):
  global devnullFd

  if args.verbose:
    print("Launching... ", cmd)
  cmd = splitWithQuotes(cmd);
  # Example cmd:  dmtcp_launch screen ...
  ptyMode = False
  for str in cmd:
    # Checkpoint image can be emacs23_x, or whatever emacs is a link to.
    # vim can be vim.gnome, etc.
    if re.search("(_|/|^)(screen|script|vim.*|emacs.*|pty|tcsh|zsh)(_|$)", str):
      ptyMode = True
  try:
    os.stat(cmd[0])
  except:
    raise CheckFailed(cmd[0] + " not found")
  if ptyMode:
    # FOR DEBUGGING:  This can mysteriously fail, causing pty.fork() to fail
    try:
      (fd1, fd2) = os.openpty()
    except OSError as e:
      print("\n\n/dev/ptmx:"); os.system("ls -l /dev/ptmx /dev/pts")
      raise e
    else:
      os.close(fd1); os.close(fd2)
    (pid, fd) = pty.fork()
    if pid == 0:
      # Close all fds except stdin/stdout/stderr
      os.closerange(3,1024)
      signal.alarm(300) # pending alarm inherited across exec, but not a fork
      # Problem:  pty.spawn invokes fork.  alarm() will have no effect.
      pty.spawn(cmd)
      sys.exit(0)
    else:
      return MySubprocess(pid)
  else:
    if cmd[0] == BIN+"dmtcp_coordinator":
      childStdout = subprocess.PIPE
      # Don't mix stderr in with childStdout; need to read stdout
      if args.verbose:
        childStderr = None
      else:
        childStderr = devnullFd
    elif args.verbose:
      childStdout=None  # Inherit child stdout from parent
      childStderr=None  # Inherit child stderr from parent
    else:
      childStdout = devnullFd
      childStderr = subprocess.STDOUT # Mix stderr into stdout file object
    # NOTE:  This might be replaced by shell=True in call to subprocess.Popen
    proc = subprocess.Popen(cmd, bufsize=BUFFER_SIZE,
                 stdin=subprocess.PIPE, stdout=childStdout,
                 stderr=childStderr, close_fds=True)
  return proc

#randomize port and dir, so multiple processes works
ckptDir="dmtcp-autotest-%d" % randint(100000000,999999999)
os.mkdir(ckptDir);
os.environ['DMTCP_COORD_HOST'] = "localhost"
os.environ['DMTCP_COORD_PORT'] = str(randint(2000,10000))
os.environ['DMTCP_CHECKPOINT_DIR'] = os.path.abspath(ckptDir)
#Use default SIGCKPT for test suite.
os.unsetenv('DMTCP_SIGCKPT')
os.unsetenv('MTCP_SIGCKPT')
#No gzip by default.  (Isolate gzip failures from other test failures.)
#But note that dmtcp3, frisbee and gzip tests below still use gzip.
if not args.verbose:
  os.environ['JALIB_STDERR_PATH'] = os.devnull
if args.verbose:
  print("coordinator port:  " + os.environ['DMTCP_COORD_PORT'])

# We'll copy ckptdir to DMTCP_TMPDIR in case of error.
def dmtcp_tmpdir():
  tmpdir = os.getenv('DMTCP_TMPDIR') or os.getenv('TMPDIR') or '/tmp'
  return tmpdir + '/dmtcp-' + os.environ['USER'] + '@' + socket.gethostname()

def free_diskspace(dir):
  s = os.statvfs('.')
  return s.f_bavail * s.f_frsize

# We'll save core dumps in our default directory (usually dmtcp-autotest-*)
# We can use the lesser of half the free disk space of filesystem or 100 MB.
if free_diskspace(ckptDir) > 20*1024*1024:
  oldLimit = resource.getrlimit(resource.RLIMIT_CORE)
  newLimit = [min(free_diskspace(ckptDir)//2, 100*1024*1024), oldLimit[1]]
  if oldLimit[1] != resource.RLIM_INFINITY:  # Keep soft limit below hard limit
    newLimit[0] = min(newLimit[0], oldLimit[1])
  resource.setrlimit(resource.RLIMIT_CORE, newLimit)

# This can be slow.
print("Verifying there is enough disk space ...")
tmpfile=ckptDir + "/dmtcp-freeSpaceTest.tmp"
if os.system("dd if=/dev/zero of=" + tmpfile + " bs=1MB count=" +
             str(REQUIRE_MB) + " 2>/dev/null") != 0:
  GZIP="1"
  print('''

!!!WARNING!!!
Fewer than '''+str(REQUIRE_MB)+'''MB are available on the current volume.
Many of the tests below may fail due to insufficient space.
!!!WARNING!!!

''')
os.system("rm -f "+tmpfile)

tmpfile="/tmp" + "/dmtcp-freeSpaceTest.tmp" # Needed by file2 test and others
if os.system("dd if=/dev/zero of=" + tmpfile + " bs=1MB count=" +
             str(REQUIRE_TMP_MB) + " 2>/dev/null") != 0:
  GZIP="1"
  print('''

!!!WARNING!!!
Fewer than '''+str(REQUIRE_TMP_MB)+'''MB are available in /tmp.
Many of the tests below (e.g., file2) may fail due to insufficient space.
!!!WARNING!!!

''')
os.system("rm -f "+tmpfile)

os.environ['DMTCP_GZIP'] = GZIP
if os.getenv('LD_LIBRARY_PATH'):
    os.environ['LD_LIBRARY_PATH'] += ':' + os.getenv("PWD")+"/lib"
else:
    os.environ['LD_LIBRARY_PATH'] = os.getenv("PWD")+"/lib"

#run the coordinator
coordinator = runCmd(coordinator_cmdline)

#clean up after ourselves
def SHUTDOWN():
  try:
    runDmtcpCommand('q')
    sleep(S*SLOW)
  except:
    print("SHUTDOWN() failed")
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
  output = runDmtcpCommand('s')

  peers = 0
  running = "no"
  for line in output:
      line=str(line.strip().decode("ascii"))

      m = re.search('NUM_PEERS=(\\d+)', line)
      if m != None:
        peers = int(m.group(1))
        continue

      m = re.search('RUNNING=(\\w+)', line)
      if m != None:
        running = m.group(1)
        break

  if args.verbose:
    print("STATUS: peers=%d, running=%s" % (peers,running))

  return (peers, (running=="yes"))

#delete all files in ckptDir
def clearCkptDir():
  for TRIES in range(2):  # Try twice in case ckpt_*_dmtcp.temp is renamed.
    #clear checkpoint dir
    for root, dirs, files in os.walk(ckptDir, topdown=False):
      for name in files:
        try:
          # if name.endswith(".dmtcp") :
          #   import shutil
          #   shutil.copy(os.path.join(root, name), "/home/kapil/dmtcp/ramfs")
          # else:
          #   os.remove(os.path.join(root, name))
          os.remove(os.path.join(root, name))
        except OSError as e:
          if e.errno != errno.ENOENT:  # Maybe ckpt_*_dmtcp.temp was renamed.
            raise e
      for name in dirs:
        os.rmdir(os.path.join(root, name))

def getNumCkptFiles(dir):
  return len([f for f in os.listdir(dir)
                if f.startswith("ckpt_") and f.endswith(".dmtcp")])


# Test a given list of commands to see if they checkpoint
# runTest() sets up a keyboard interrupt handler, and then calls this function.
def runTestRaw(name, numProcs, cmds):
  #the expected/correct running status
#  if USE_M32:
#    def forall(fnc, lst):
#      return reduce(lambda x, y: x and y, map(fnc, lst))
#    if not forall(lambda x: x.startswith("./test/"), cmds):
#      return
  status=(numProcs, True)
  procs=[]

  def doesStatusSatisfy(newStatus,requiredStatus):
    if isinstance(requiredStatus[0], int):
      statRange = [requiredStatus[0]]
    elif isinstance(requiredStatus[0], list):
      statRange = requiredStatus[0]
    else:
      raise NotImplementedError
    return newStatus[0] in statRange and newStatus[1] == requiredStatus[1]

  def wfMsg(msg):
    #return function to generate error message
    return lambda: msg+", "+str(status[0])+ \
                   " expected, %d found, running=%d" % getStatus()

  def testKill():
    #kill all processes
    runDmtcpCommand('k')
    try:
      WAITFOR(lambda: getStatus()==(0, False),
              lambda:"coordinator kill command failed")
    except CheckFailed:
      global coordinator
      runDmtcpCommand('q')
      os.system("kill -9 %d" % coordinator.pid)
      print("Trying to kill old coordinator, and run new one on same port")
      coordinator = runCmd(coordinator_cmdline)
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
      except OSError as e:
        if e.errno != errno.ECHILD:
          raise e
      procs.remove(x)

  def testCheckpoint():
    #start checkpoint
    runDmtcpCommand(CKPT_CMD)

    #wait for files to appear and status to return to original
    # 'Kc' input to dmtcp_coordinator is equivalent to 'dmtcp_command -kc'
    WAITFOR(lambda: getNumCkptFiles(ckptDir)>0 and \
                 (CKPT_CMD == 'Kc' or doesStatusSatisfy(getStatus(), status)),
            wfMsg("checkpoint error"))
    #we now know there was at least one checkpoint file, and the correct number
    #  of processes have restarted;  but they may fail quickly after restert

    if SLOW > 1:
      #wait and give the processes time to write all of the checkpoint files
      sleep(S*SLOW)

    #make sure the right files are there
    numFiles=getNumCkptFiles(ckptDir) # len(os.listdir(ckptDir))
    CHECK(doesStatusSatisfy((numFiles,True),status),
          "unexpected number of checkpoint files, %s procs, %d files"
          % (str(status[0]), numFiles))

    if SLOW > 1 and CKPT_CMD != 'Kc':
      #wait and see if some processes will die shortly after checkpointing
      #but if 'Kc' was requested, processes should die (not resume)
      sleep(S*SLOW)
      CHECK(doesStatusSatisfy(getStatus(), status),
            "error: processes checkpointed, but died upon resume")

  def testRestart():
    #build restart command
    cmd=BIN+"dmtcp_restart --quiet"
    if name == "restartdir":
      cmd+= " --restartdir "+ckptDir
    else:
      for i in os.listdir(ckptDir):
        if i.endswith(".dmtcp"):
          cmd+= " "+ckptDir+"/"+i
    #run restart and test if it worked
    procs.append(runCmd(cmd))
    sleep(POST_RESTART_SLEEP)
    WAITFOR(lambda: doesStatusSatisfy(getStatus(), status),
            wfMsg("restart error"))
    if SLOW > 1:
      #wait and see if process will die shortly after restart
      sleep(S*SLOW)
      CHECK(doesStatusSatisfy(getStatus(), status),
            "error:  processes restarted and then died")
    clearCkptDir()

  try:
    sys.stdout.flush()
    # If verbose mode, maybe JWARNING/KASSERT was interrupted during color print
    if args.verbose:
      os.write(sys.stdout.fileno(), COLOR_RESET.encode("ascii"))
    printFixed(name, DEFAULT_TESTNAME_WIDTH)

    if name in disabled_tests:
      print("Disabled")
      return

    if not shouldRunTest(name):
      print("Skipped")
      return

    stats[1]+=1
    CHECK(getStatus()==(0, False), "coordinator initial state")

    #start user programs
    for cmd in cmds:
      procs.append(runCmd(BIN+"dmtcp_launch "+cmd))

    #TIMEOUT in WAITFOR has also been multiplied by SLOW
    WAITFOR(lambda: doesStatusSatisfy(getStatus(), status),
            wfMsg("user program startup error"))

    # Additional sleep to allow the test to boot.
    sleep(POST_LAUNCH_SLEEP)

    #Will sleep(S*SLOW) in the following for loop.

    for i in range(CYCLES):
      if i!=0 and i%2==0:
        printFixed("\n")
        printFixed("", DEFAULT_TESTNAME_WIDTH)
      printFixed("ckpt:")
      # NOTE:  If this faile, it will throw an exception to CheckFailed
      #  of this function:  testRestart
      #wait for launched processes to settle down, before we try to checkpoint
      sleep(S*SLOW)
      testCheckpoint()
      printFixed("PASSED; ")
      testKill()

      printFixed("rstr:")
      for j in range(RETRIES):
        try:
          testRestart()
          printFixed("PASSED")
          break
        except CheckFailed as e:
          if j == RETRIES-1:
            # Save checkpoint images for later diagnosis.
            if os.path.isdir(dmtcp_tmpdir()) and os.path.isdir(ckptDir):
              if subprocess.call( ("cp -pr " + ckptDir + ' '
                                   + dmtcp_tmpdir()).split() ) == 0:
                printError("\n***** Copied checkpoint images to " +
                           dmtcp_tmpdir() + "/" + ckptDir)
            raise e
          else:
            printFixedError("Failed ")
            (oldpid, oldstatus) = os.waitpid(procs[-1].pid, os.WNOHANG)
            if oldpid == procs[-1].pid:
              if os.WIFEXITED(oldstatus):
                printFixed("(first process exited: oldstatus "
                           + str(os.WEXITSTATUS(oldstatus)) + ")")
              if os.WIFSIGNALED(oldstatus):
                printFixed("(first process rec'd signal "
                           + str(os.WTERMSIG(oldstatus)) + ")")
              if os.WCOREDUMP(oldstatus):
                coredump = "core." + str(oldpid)
                if os.path.isdir(dmtcp_tmpdir()) and os.path.isfile(coredump):
                  if subprocess.call( ("cp -pr " + coredump + ' '
                                   + dmtcp_tmpdir()).split() ) == 0:
                    printFixed(" (" + coredump + " copied to DMTCP_TMPDIR:" +
                               dmtcp_tmpdir() + "/)")
            else:
              printError("\n(Either first process didn't die, or else " +
                         " this long delay\n has been observed due to a slow" +
                         " NFS-based filesystem.)")
            printFixed("; retry:")
            testKill()
      if i != CYCLES - 1:
        printFixed(" -> ")
        if i % 2 == 1:
          printFixed("(cont.)")

    testKill()
    printFixed("\n")
    stats[0]+=1

  except CheckFailed as e:
    printFixedError("Failed")
    printFixed("\n")
    printError("root-pids:" + str([x.pid for x in procs]) +
          "msg:" + str(e.value))
    failed_tests.append(name)
    try:
      testKill()
    except CheckFailed as e:
      printError("CLEANUP ERROR:" + str(e.value))
      SHUTDOWN()
      saveResultsNMI()
      sys.exit(1)
    if args.retry_once:
      clearCkptDir()
      raise e

  clearCkptDir()

def getProcessChildren(pid):
    p = subprocess.Popen("ps --no-headers -o pid --ppid %d" % pid, shell = True,
                         stdout = subprocess.PIPE, stderr = subprocess.PIPE)
    stdout, stderr = p.communicate()
    return [int(pid) for pid in stdout.split()]

# If the user types ^C, then kill all child processes.
def runTest(name, numProcs, cmds):
  # if --parallel and this test already passed, return
  if args.parallel and parallel_test(name):
    return
  for i in range(2):
    try:
      runTestRaw(name, numProcs, cmds)
      break;
    except KeyboardInterrupt:
      runDmtcpCommand('q', waitForOutput=False)
      for pid in getProcessChildren(os.getpid()):
        try:
          os.kill(pid, signal.SIGKILL)
        except OSError: # This happens if pid already died.
          pass
      sys.exit(1)
    except CheckFailed as e:
      if not args.retry_once:
        break
      if i == 0:
        stats[1]-=1
        print(COLOR_RED + "Trying once again" + COLOR_RESET)

def saveResultsNMI():
  if DEBUG == "yes":
    # WARNING:  This can cause a several second delay on some systems.
    host = socket.getfqdn()
    if re.search("^nmi-.*.cs.wisc.edu$", host) or \
       re.search("^nmi-.*.cs.wisconsin.edu$", host):
      tmpdir = os.getenv("TMPDIR", "/tmp") # if "TMPDIR" not set, return "/tmp"
      target = "./dmtcp-" + pwd.getpwuid(os.getuid()).pw_name + \
               "@" + socket.gethostname()
      cmd = "mkdir results; cp -pr " + tmpdir + "/" + target + \
               " ./dmtcp/src/libdmtcp.so" + \
               " ./dmtcp/src/dmtcp_coordinator" + \
               " ./mtcp/libmtcp.so" + \
               " results/"
      os.system(cmd)
      cmd = "tar zcf ../results.tar.gz ./results; rm -rf results"
      os.system(cmd)
      print("\n*** results.tar.gz ("+tmpdir+"/"+target+
                                   ") written to DMTCP_ROOT/.. ***")

print("== Tests ==")

#tmp port
p0=str(randint(2000,10000))
p1=str(randint(2000,10000))
p2=str(randint(2000,10000))
p3=str(randint(2000,10000))

# Use uniform user shell.  Else apps like script have different subprocesses.
os.environ["SHELL"]="/bin/bash"

if USE_MULTILIB:
  runTest("dmtcp1-m32",  1, ["./test/dmtcp1-m32"])

runTest("dmtcp1",        1, ["./test/dmtcp1"])

runTest("dmtcp2",        1, ["./test/dmtcp2"])

runTest("dmtcp3",        1, ["./test/dmtcp3"])

runTest("dmtcp4",        1, ["./test/dmtcp4"])

runTest("alarm",        1, ["./test/alarm"])

runTest("sched_test",    2, ["./test/sched_test"])

# In 32-bit Ubuntu 9.10, the default small stacksize (8 MB) forces
# legacy_va_layout, which places vdso in low memory.  This collides with text
# in low memory (0x110000) in the statically linked mtcp_restart executable.
oldLimit = resource.getrlimit(resource.RLIMIT_STACK)
# oldLimit[1] is old hard limit
if oldLimit[1] == resource.RLIM_INFINITY:
  newCurrLimit = 8*1024*1024
else:
  newCurrLimit = min(8*1024*1024, oldLimit[1])
resource.setrlimit(resource.RLIMIT_STACK, [newCurrLimit, oldLimit[1]])
runTest("dmtcp5",        2, ["./test/dmtcp5"])
resource.setrlimit(resource.RLIMIT_STACK, oldLimit)

runTest("gettid",        1, ["./test/gettid"])

# Test for a bunch of system calls. We want to use the 'Kc' mode for
# (sets exitAfterCkptOnce in src/dmtcp_coordinator.cpp) for
# checkpointing so that the process is killed right after checkpoint. Otherwise
# the syscall-tester could fail in the following case:
#   1. create an open temp file
#   2. close temp file
#   3. ckpt
#   4. unlink temp file
# If the last step is executed before the process is killed after ckpt-resume,
# the file would have been deleted from the disk. However, on restart, the test
# program will try to unlink the file once again, but the unlink operation will
# fail, causing the test to fail.
#   We have now patched syscall-tester.c (see near top) to fix this.
# But this requires --checkpoint-open-files.  A fix for DMTCP is proposed
# there, and that will later need --checkpoint-all-files-as-precious.
old_ckpt_cmd = CKPT_CMD
CKPT_CMD = 'Kc' # Equivalent to 'dmtcp_command -kc'
runTest("syscall-tester",  1, ["--checkpoint-open-files ./test/syscall-tester"])
CKPT_CMD = old_ckpt_cmd

# Test for files opened with WRONLY mode and later unlinked.
runTest("file1",         1, ["./test/file1"])

# Test for files and their directories opened and unlinked
# PREV. NOTE (now fixed?):
#   Currently, we re-create deleted subdirectories when file
#   is mmap'ed, but not yet when file is referenced by open fd.
S=10*DEFAULT_S
runTest("file2",         1, ["./test/file2"])
S=DEFAULT_S

# Test for normal file, /dev/tty, proc file, and illegal pathname
runTest("stat",         1, ["./test/stat"])

runTest("mmap1", 1, ["./test/mmap1"])

if HAS_SELINUX == "yes":
  runTest("selinux1", 1, ["./test/selinux1"])

# FIXME:  Copy test/stack-growsdown from DMTCP-2.6 when PR is ready.
# # Test if it works for stack growing on restart
# runTest("stack-growsdown",         1, ["./test/stack-growsdown"])

runTest("presuspend",   [1, 2], ["./test/presuspend"])

PWD=os.getcwd()
runTest("plugin-sleep2", 1, ["--with-plugin "+
                             PWD+"/test/plugin/sleep1/libdmtcp_sleep1.so:"+
                             PWD+"/test/plugin/sleep2/libdmtcp_sleep2.so "+
                             "./test/dmtcp1"])

# FIXME:  Test does not work.  Try:  AUTOTEST=-v make check-plugin-example-db
# ERROR: ld.so: object '/home/gene/dmtcp.git/test/plugin/example-db/dmtcp_example-dbhijack.so' from LD_PRELOAD cannot be preloaded (cannot open shared object file): ignored.
runTest("plugin-example-db", 2, ["--with-plugin "+
                            PWD+"/test/plugin/example-db/libdmtcp_example-db.so "+
                             "env EXAMPLE_DB_KEY=1 EXAMPLE_DB_KEY_OTHER=2 "+
                             "./test/dmtcp1",
                                 "--with-plugin "+
                            PWD+"/test/plugin/example-db/libdmtcp_example-db.so "+
                             "env EXAMPLE_DB_KEY=2 EXAMPLE_DB_KEY_OTHER=1 "+
                             "./test/dmtcp1"])

runTest("plugin-init", 1, ["--with-plugin "+
                             PWD+"/test/libdmtcp_plugin-init.so "+
                             "./test/dmtcp1"])

# Test special case:  gettimeofday can be handled within VDSO segment.
runTest("gettimeofday",   1, ["./test/gettimeofday"])

runTest("sigchild",       1, ["./test/sigchild"])

runTest("shared-fd1",     2, ["./test/shared-fd1"])

runTest("shared-fd2",     2, ["./test/shared-fd2"])

runTest("stale-fd",       2, ["./test/stale-fd"])

runTest("rlimit-restore", 1, ["./test/rlimit-restore"])

runTest("rlimit-nofile",  2, ["./test/rlimit-nofile"])

runTest("procfd1",       2, ["./test/procfd1"])

# popen1 can have more than one processes
runTest("popen1",          [1,2], ["./test/popen1"])

runTest("poll",          1, ["./test/poll"])

runTest("epoll1",        2, ["./test/epoll1"])

if HAS_EPOLL_CREATE1 == "yes":
  runTest("epoll2",        2, ["./test/epoll1 --use-epoll-create1"])

runTest("environ",       1, ["./test/environ"])

runTest("forkexec",      2, ["./test/forkexec"])

# vfork1 test creates a child process using vfork. The child executes the
# command passed via command line using "sh -c".

# The child process will exit and a new child process will be forked at the end
# of each "ls | wc" execution. Thus, at the time of checkpoint, either:
# a. only vfork1 process is active, or
# b. vfork1 and sh processes are active, or
# c. vfork1, sh, and either or both of ls and wc processes are active.
runTest("vfork1",        [1,2,3,4] , ["./test/vfork1 'ls | wc'"])

# The sh process is permanent, but the number of sh's child processes active at
# the time of checkpoint varies. Either or both of date and sleep might be active.
runTest("vfork2",        [2,3,4] , ["./test/vfork1 'while true; do date; sleep 1; done' "])

runTest("realpath",      1, ["./test/realpath"])
runTest("pthread1",      1, ["./test/pthread1"])
runTest("pthread2",      1, ["./test/pthread2"])

S=0.5*DEFAULT_S
runTest("pthread3",      1, ["./test/pthread2 80"])

runTest("pthread4",      1, ["./test/pthread4"])
runTest("pthread5",      1, ["./test/pthread5"])
runTest("pthread6",      1, ["./test/pthread6"])
S=DEFAULT_S

runTest("mutex1",        1, ["./test/mutex1"])
runTest("mutex2",        1, ["./test/mutex2"])
runTest("mutex3",        1, ["./test/mutex3"])
runTest("mutex4",        1, ["./test/mutex4"])

# FIXME:  pthread_atfork doesn't compile on some architectures.
#         If we add a configure test for pthread_atfork, we can
#           set a Python variable in autotest_config.py.in
if uname_m != "armv7" and uname_m != "armv7l":
  if os.getenv("LD_LIBRARY_PATH"):
    os.environ["LD_LIBRARY_PATH"] += ":./test"
  else:
    os.environ["LD_LIBRARY_PATH"] = "./test"
  runTest("pthread_atfork1",      2, ["./test/pthread_atfork1"])
  runTest("pthread_atfork2",      2, ["./test/pthread_atfork2"])
  if os.environ["LD_LIBRARY_PATH"] == "./test":
    del os.environ["LD_LIBRARY_PATH"]
  else:
    os.environ["LD_LIBRARY_PATH"] = \
      os.getenv("LD_LIBRARY_PATH")[:-len(":./test")]
else:
  print("Skipping pthread_atfork test; doesn't build on ARM/aarch64/glibc/Linux")

if not USE_M32:  # ssh (a 64-bit child process) is forked
  if HAS_SSH_LOCALHOST == "yes":
    S=5*DEFAULT_S
    runTest("ssh1",     4, ["./test/ssh1"])
    S=DEFAULT_S

if not USE_M32:  # waitpid forks a 64-bit child process, /bin/sleep
  S=2*DEFAULT_S
  runTest("waitpid",      2, ["./test/waitpid"])
  S=DEFAULT_S

runTest("client-server", 2, ["./test/client-server"])

# frisbee creates three processes, each with 14 MB, if no gzip is used
os.environ['DMTCP_GZIP'] = "1"
POST_LAUNCH_SLEEP=2
runTest("frisbee",       3, ["./test/frisbee "+p1+" localhost "+p2,
                             "./test/frisbee "+p2+" localhost "+p3,
                             "./test/frisbee "+p3+" localhost "+p1+" starter"])
POST_LAUNCH_SLEEP=DEFAULT_POST_LAUNCH_SLEEP
os.environ['DMTCP_GZIP'] = GZIP

# On an NFS filesystem, a race can manifest late on the second restart,
# due to a slow coordinator.
S=10*DEFAULT_S
runTest("shared-memory1", 2, ["./test/shared-memory1"])
runTest("shared-memory2", 2, ["./test/shared-memory2"])
#runTest("shared-memory3", 2, ["./test/shared-memory3"])
S=DEFAULT_S

runTest("sysv-shm1",     2, ["./test/sysv-shm1"])
runTest("sysv-shm2",     2, ["./test/sysv-shm2"])
runTest("sysv-sem",      2, ["./test/sysv-sem"])
runTest("sysv-msg",      2, ["./test/sysv-msg"])

# Makefile compiles cma only for Linux 3.2 and higher.
if HAS_CMA == "yes":
  runTest("cma",         2, ["./test/cma"])

# ARM glibc 2.16 with Linux kernel 3.0 doesn't support mq_send, etc.
if uname_p[0:3] == 'arm':
  print("Skipping posix-mq1/mq2 tests; ARM/glibc/Linux does not support mq_send")
elif TEST_POSIX_MQ == "yes":
  runTest("posix-mq1",     2, ["./test/posix-mq1"])
  # mq-notify seems to be broken at the moment.
  #runTest("posix-mq2",     2, ["./test/posix-mq2"])

#Invoke this test when we drain/restore data in pty at checkpoint time.
runTest("pty1",   2, ["./test/pty1"])
runTest("pty2",   2, ["./test/pty2"])

#Invoke this test when support for timers is added to DMTCP.
runTest("timer1",   1, ["./test/timer1"])
##########################################################
# In Ubuntu 18.0, bin/dmtcp_launch test/timer2 exits early
# In contrast, gdb --args bin/dmtcp_launch test/timer2 does not fail.
# And the bug is not observed on CentOS 7.6
# Let's omit this test, until we can fix the bug with timer2
##########################################################
## runTest("timer2",   1, ["./test/timer2"])
runTest("clock",   1, ["./test/clock"])

runTest("dlopen1",       1, ["./test/dlopen1"])
if not USE_M32:
  runTest("dlopen2",     1, ["./test/dlopen2"])

# Most of the remaining tests are on 64-bit processes.
if USE_M32:
  sys.exit()

os.environ['DMTCP_GZIP'] = "1"
runTest("gzip",          1, ["./test/dmtcp1"])
os.environ['DMTCP_GZIP'] = GZIP

if HAS_READLINE == "yes":
  runTest("readline",    1,  ["./test/readline"])

POST_LAUNCH_SLEEP = 2  # Don't checkpoint until perl cmd has launched
runTest("perl",          1, ["/usr/bin/perl"])
POST_LAUNCH_SLEEP=DEFAULT_POST_LAUNCH_SLEEP

if HAS_PYTHON == "yes" or HAS_PYTHON3 == "yes":
  POST_LAUNCH_SLEEP = 2  # Don't checkpoint until python cmd has launched
  if HAS_PYTHON == "yes":
    runTest("python",      1, ["/usr/bin/env python"])
  else:
    runTest("python",      1, ["/usr/bin/env python3"])
  POST_LAUNCH_SLEEP=DEFAULT_POST_LAUNCH_SLEEP

os.environ['DMTCP_GZIP'] = "1"
POST_LAUNCH_SLEEP = 2  # Don't checkpoint until bash cmd has launched
runTest("bash",        2, ["/bin/bash --norc -c 'ls; sleep 30; ls'"])
POST_LAUNCH_SLEEP=DEFAULT_POST_LAUNCH_SLEEP
os.environ['DMTCP_GZIP'] = GZIP

if HAS_DASH == "yes":
  os.environ['DMTCP_GZIP'] = "0"
  os.unsetenv('ENV')  # Delete reference to dash initialization file
  POST_LAUNCH_SLEEP = 2  # Don't checkpoint until dash cmd has launched
  runTest("dash",        2, ["/bin/dash -c 'ls; sleep 30; ls'"])
  POST_LAUNCH_SLEEP=DEFAULT_POST_LAUNCH_SLEEP
  os.environ['DMTCP_GZIP'] = GZIP

if HAS_TCSH == "yes":
  os.environ['DMTCP_GZIP'] = "1"
  runTest("tcsh",        2, ["/bin/tcsh -f -c 'ls; sleep 30; ls'"])
  os.environ['DMTCP_GZIP'] = GZIP

if HAS_ZSH == "yes":
  os.environ['DMTCP_GZIP'] = "0"
  S=3*DEFAULT_S
  POST_LAUNCH_SLEEP = 2  # Don't checkpoint until zsh cmd has launched
  runTest("zsh",         2, ["/bin/zsh -f -c 'ls; sleep 30; ls'"])
  POST_LAUNCH_SLEEP=DEFAULT_POST_LAUNCH_SLEEP
  S=DEFAULT_S
  os.environ['DMTCP_GZIP'] = GZIP

if HAS_VIM == "yes":
  # Wait to checkpoint until vim finishes reading its initialization files
  S=10*DEFAULT_S
  # Delete previous vim processes.  Vim behaves poorly with stale processes.
  vimCommand = VIM + " -X -u DEFAULTS -i NONE /etc/passwd +3" # +3 makes cmd line unique
  def killCommand(cmdToKill):
    if os.getenv('USER') == None or HAS_PS == 'no':
      return
    ps = subprocess.Popen(['ps', '-u', os.environ['USER'], '-o',
                           'pid,command'],
                          stdout=subprocess.PIPE).communicate()[0]
    for row in ps.split(b'\n')[1:]:
      cmd = row.split(None, 1) # maxsplit=1
      if len(cmd) > 1 and cmd[1] == cmdToKill:
        os.kill(int(cmd[0]), signal.SIGKILL)
  killCommand(vimCommand)
  runTest("vim",       1,  ["env TERM=vt100 " + vimCommand])
  killCommand(vimCommand)
  S=DEFAULT_S

os.environ['DMTCP_GZIP'] = "0"
#On some systems, "emacs -nw" runs dbus-daemon processes in
#background throwing off the number of processes in the computation.
#So, we expect 1 or 2 processes.
S=40*DEFAULT_S
if HAS_EMACS == "yes":
  # Wait to checkpoint until emacs finishes reading its initialization files
  # Under emacs23, it opens /dev/tty directly in a new fd.
  # To avoid this, consider using emacs --batch -l EMACS-LISTP-CODE ...
  # ... or else a better pty wrapper to capture emacs output to /dev/tty.
  runTest("emacs", [1, 2],  ["env TERM=vt100 /usr/bin/emacs -nw" +
                             " --no-init-file /etc/passwd"])
S=DEFAULT_S
os.environ['DMTCP_GZIP'] = GZIP

if HAS_SCRIPT == "yes":
  S=7*DEFAULT_S
  # NOTE: If 'script' fails, try raising value of S, above, to larger number.
  #  Arguably, there is a bug in glibc, in that locale-archive can be 100 MB.
  #  For example, in Fedora 13 (and other recent Red Hat-derived distros?),
  #  /usr/lib/locale/locale-archive is 100 MB, and yet 'locale -a |wc' shows
  #  only 8KB of content in ASCII.  The 100 MB of locale-archive condenses
  #  to 25 MB _per process_ under gzip, but this can be slow at ckpt time.
  # On some systems, the script test has two `script` processes, while on some
  # other systems, there is only a single `script` process.
  # Newer versions of Bash don't fork when executing a simple command
  # specified with "-c" flag. Instead, the bash process execs into the given
  # command.
  POST_LAUNCH_SLEEP = 2  # Don't checkpoint until script cmd has launched
  runTest("script",    [2,3,4],  ["/usr/bin/script -f" +
                            " -c 'bash -c \"ls; sleep 30\"'" +
                            " dmtcp-test-typescript.tmp"])
  POST_LAUNCH_SLEEP = DEFAULT_POST_LAUNCH_SLEEP
  os.system("rm -f dmtcp-test-typescript.tmp")
  S=DEFAULT_S

# SHOULD HAVE screen RUN SOMETHING LIKE:  bash -c ./test/dmtcp1
# FIXME: Currently fails on dekaksi due to DMTCP not honoring
#        "Async-signal-safe functions" in signal handlers (see man 7 signal)
# Maybe this will work after new pty plugin PR is added.
#   Review whether to include this test then, and make depend on HAS_RECENT_PTY
#   that will be set in 'configure'.
SCREEN_TEST_WORKS = False
if HAS_SCREEN == "yes" and SCREEN_TEST_WORKS:
  S=3*DEFAULT_S
  runTest("screen",    3,  ["env TERM=vt100 " + SCREEN +
                              " -c /dev/null -s /bin/sh"])
  S=DEFAULT_S

if HAS_JAVAC == "yes" and HAS_JAVA == "yes":
  S=10*DEFAULT_S
  os.environ['CLASSPATH'] = './test'
  runTest("java1",         1,  ["java -Xmx5M java1"])
  del os.environ['CLASSPATH']
  S=DEFAULT_S

if HAS_CILK == "yes":
  runTest("cilk1",        1,  ["./test/cilk1 38"])

# SHOULD HAVE gcl RUN LARGE FACTORIAL OR SOMETHING.
if HAS_GCL == "yes":
  S=3*DEFAULT_S
  runTest("gcl",         1,  [GCL])
  S=DEFAULT_S

if HAS_OPENMP == "yes":
  S=3*DEFAULT_S
  runTest("openmp-1",         1,  ["./test/openmp-1"])
  runTest("openmp-2",         1,  ["./test/openmp-2"])
  S=DEFAULT_S

# SHOULD HAVE matlab RUN LARGE FACTORIAL OR SOMETHING.
if HAS_MATLAB == "yes":
  S=10*DEFAULT_S
  runTest("matlab-nodisplay", 1,  [MATLAB+" -nodisplay -nojvm"])
  S=DEFAULT_S

if HAS_MPICH == "yes":
  runTest("hellompich-n1", 3,
          [MPICH_PATH + "/mpirun" + " -np 1 ./test/hellompich"])

  runTest("hellompich-n2", 4,
          [MPICH_PATH + "/mpirun" + " -np 2 ./test/hellompich"])

if HAS_OPENMPI == "yes":
  # Compute:  USES_OPENMPI_ORTED
  if os.path.isfile('./test/openmpi') and \
     0 == os.system(OPENMPI_MPICC +
                    " -o ./test_openmpi test/hellompi.c 2>/dev/null 1>&2"):
    os.system("rm -f ./uses_openmpi_orted")
    # The 'sleep 1' below may not fix the race, creating a runaway test_openmpi.
    os.system('/bin/sh -c "$OPENMPI_MPIRUN -np 2 ./test_openmpi' +
              '   2>/dev/null 1>&2 &'
              ' sleep 1 &&'
              ' ps auxw | grep $USER | grep -v grep | grep -q orted &&'
              ' touch ./uses_openmpi_orted" 2>/dev/null')
    os.system(r"/bin/kill -9 `ps -eo pid,args | grep test_openmpi |" +
              r" sed -e 's%\([0-9]\) .*$%\1%'` 2>/dev/null")
    if os.path.exists('./uses_openmpi_orted'):
      os.system('rm -f ./uses_openmpi_orted')
      USES_OPENMPI_ORTED = "yes"
    else:
      USES_OPENMPI_ORTED = "no"
  else:
    HAS_OPENMPI = "no"
  os.system('rm -f ./test_openmpi')

#Temporarily disabling Open MPI test as it fails on some distros (OpenSUSE 11.4)
if HAS_OPENMPI == "yes":
  numProcesses = 5 + int(USES_OPENMPI_ORTED == "yes")
  # FIXME: Replace "[5,6]" by numProcesses when bug in configure is fixed.
  # /usr/bin/openmpi does not work if /usr/bin is not also in user's PATH
  oldPath = ""
  if 'PATH' not in os.environ:
    oldPath = None
    os.environ['PATH'] = os.path.dirname(OPENMPI_MPIRUN)
  elif (not re.search(os.path.dirname(OPENMPI_MPIRUN),
                     os.environ['PATH'])):
    oldPath = os.environ['PATH']
    os.environ['PATH'] += ":" + os.path.dirname(OPENMPI_MPIRUN)
  S=3*DEFAULT_S
  runTest("openmpi", [5,6], [OPENMPI_MPIRUN + " -np 4" +
                             " ./test/openmpi"])
  S=DEFAULT_S
  if oldPath:
    os.environ['PATH'] = oldPath
  if oldPath == None:
    del os.environ['PATH']

# Test --restartdir
runTest("restartdir", 1, ["./test/dmtcp1"])

# Test DMTCP utilities:
runTest("nocheckpoint",        [1,2], ["./test/nocheckpoint"])

print("== Summary ==")
print("%s: %d of %d tests passed" % (socket.gethostname(), stats[0], stats[1]))
if failed_tests:
  failed_tests = [test for i, test in enumerate(failed_tests)
                       if i == 0 or failed_tests[i-1] != test]
  print("Failed Tests:")
  for f in failed_tests:
    printError("  " + f)

saveResultsNMI()

try:
  SHUTDOWN()
except CheckFailed as e:
  print("Error in SHUTDOWN():", e.value)
except:
  print("Error in SHUTDOWN()")

sys.exit( stats[1] - stats[0] )  # Return code is number of failing tests.
