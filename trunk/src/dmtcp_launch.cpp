/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *  This file is part of DMTCP.                                             *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or                  *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,                *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include <sys/resource.h>
#include  "../jalib/jassert.h"
#include  "../jalib/jfilesystem.h"
#include  "../jalib/jconvert.h"
#include "constants.h"
#include "dmtcpmessagetypes.h"
#include "syscallwrappers.h"
#include "coordinatorapi.h"
#include "shareddata.h"
#include "util.h"

#define BINARY_NAME "dmtcp_launch"

using namespace dmtcp;

static void processArgs(int *orig_argc, char ***orig_argv);
static int testMatlab(const char *filename);
static int testJava(char **argv);
static bool testSetuid(const char *filename);
static void testStaticallyLinked(const char *filename);
static bool testScreen(char **argv, char ***newArgv);
static void setLDPreloadLibs(bool is32bitElf);

// gcc-4.3.4 -Wformat=2 issues false positives for warnings unless the format
// string has at least one format specifier with corresponding format argument.
// Ubuntu 9.01 uses -Wformat=2 by default.
static const char* theUsage =
  "Usage: dmtcp_launch [OPTIONS] <command> [args...]\n"
  "Start a process under DMTCP control.\n\n"
  "Connecting to the DMTCP Coordinator:\n"
  "  -h, --host HOSTNAME (environment variable DMTCP_HOST)\n"
  "              Hostname where dmtcp_coordinator is run (default: localhost)\n"
  "  -p, --port PORT_NUM (environment variable DMTCP_PORT)\n"
  "              Port where dmtcp_coordinator is run (default: 7779)\n"
  "  --port-file FILENAME\n"
  "              File to write listener port number.\n"
  "              (Useful with '--port 0', which is used to assign a random port)\n"
  "  -j, --join\n"
  "              Join an existing coordinator, raise error if one doesn't\n"
  "              already exist\n"
  "  --new-coordinator\n"
  "              Create a new coordinator at the given port. Fail if one \n"
  "              already exists on the given port. The port can be specified\n"
  "              with --port, or with environment variable DMTCP_PORT.  If no\n"
  "              port is specified, start coordinator at a random port (same\n"
  "              as specifying port '0').\n"
  "  --no-coordinator\n"
  "              Execute the process in stand-alone coordinator-less mode.\n"
  "              Use dmtcp_command or --interval to request checkpoints.\n"
  "  -i, -interval SECONDS (environment variable DMTCP_CHECKPOINT_INTERVAL)\n"
  "              Time in seconds between automatic checkpoints.\n"
  "              0 implies never (manual ckpt only); if not set and no env var,\n"
  "              use default value set in dmtcp_coordinator or dmtcp_command.\n"
  "              Not allowed if --join is specified\n"
  "\n"
  "Checkpoint image generation:\n"
  "  --gzip, --no-gzip, (environment variable DMTCP_GZIP=[01])\n"
  "              Enable/disable compression of checkpoint images (default: 1)\n"
  "              WARNING:  gzip adds seconds.  Without gzip, ckpt is often < 1 s\n"
#ifdef HBICT_DELTACOMP
  "  --hbict, --no-hbict, (environment variable DMTCP_HBICT=[01])\n"
  "              Enable/disable compression of checkpoint images (default: 1)\n"
#endif
  "  --ckptdir PATH (environment variable DMTCP_CHECKPOINT_DIR)\n"
  "              Directory to store checkpoint images\n"
  "              (default: curr dir at launch)\n"
  "  --ckpt-open-files\n"
  "  --checkpoint-open-files\n"
  "              Checkpoint open files and restore old working dir.\n"
  "              (default: do neither)\n"
  "  --ckpt-signal signum\n"
  "  --mtcp-checkpoint-signal signum\n"
  "              Signal number used internally by DMTCP for checkpointing\n"
  "              (default: 12). --mtcp-checkpoint-signal is deprecated.\n"
  "\n"
  "Enable/disable plugins:\n"
  "  --with-plugin (environment variable DMTCP_PLUGIN)\n"
  "              Colon-separated list of DMTCP plugins to be preloaded with DMTCP.\n"
  "              (Absolute pathnames are required.)\n"
  "  --batch-queue, --rm\n"
  "              Enable support for resource managers (Torque PBS and SLURM).\n"
  "              (default: disabled)\n"
  "  --ptrace    \n"
  "              Enable support for PTRACE system call for gdb/strace etc.\n"
  "              (default: disabled)\n"
  "  --modify-env\n"
  "              Update environment variables based on the environment on the \n"
  "              restart host (e.g., DISPLAY=$DISPLAY).\n"
  "              This can be set in a file dmtcp_env.txt. \n"
  "              (default: disabled)\n"
  "  --ib, --infiniband\n"
  "              Enable InfiniBand plugin. (default: disabled)\n"
  "  --disable-alloc-plugin: (environment variable DMTCP_ALLOC_PLUGIN=[01])\n"
  "              Disable alloc plugin (default: enabled).\n"
  "  --disable-dl-plugin: (environment variable DMTCP_DL_PLUGIN=[01])\n"
  "              Disable dl plugin (default: enabled).\n"
  "  --disable-all-plugins (EXPERTS ONLY, FOR DEBUGGING)\n"
  "              Disable all plugins.\n"
  "\n"
  "Other options:\n"
  "  --prefix PATH\n"
  "              Prefix where DMTCP is installed on remote nodes.\n"
  "  --tmpdir PATH (environment variable DMTCP_TMPDIR)\n"
  "              Directory to store temporary files \n"
  "              (default: $TMDPIR/dmtcp-$USER@$HOST or /tmp/dmtcp-$USER@$HOST)\n"
  "  -q, --quiet (or set environment variable DMTCP_QUIET = 0, 1, or 2)\n"
  "              Skip NOTE messages; if given twice, also skip WARNINGs\n"
  "  --help\n"
  "              Print this message and exit.\n"
  "  --version\n"
  "              Print version information and exit.\n"
  "\n"
  HELP_AND_CONTACT_INFO
  "\n"
;

// FIXME:  The warnings below should be collected into a single function,
//          and also called after a user exec(), not just in dmtcp_launch.
// static const char* theExecFailedMsg =
//   "ERROR: Failed to exec(\"%s\"): %s\n"
//   "Perhaps it is not in your $PATH?\n"
//   "See `dmtcp_launch --help` for usage.\n"
// ;

static bool disableAllPlugins=false;
static bool isSSHSlave=false;
static bool checkpointOpenFiles=false;

static bool enablePtracePlugin=false;
static bool enableModifyEnvPlugin=false;
static bool enableRMPlugin=false;

static bool enableIB2TcpPlugin=false;
static bool enableIBPlugin=false;

static bool enableAllocPlugin=true;
static bool enableDlPlugin=true;
static bool enableIPCPlugin=true;
static bool enableLibDMTCP=true;
static bool enablePIDPlugin=true;
#ifdef UNIQUE_CHECKPOINT_FILENAMES
static bool enableUniqueCkptPlugin=true;
#else
static bool enableUniqueCkptPlugin=false;
#endif

static dmtcp::string thePortFile;

struct PluginInfo {
  bool *enabled;
  const char *lib;
};

static struct PluginInfo pluginInfo[] = {               // Default value
  {&enablePtracePlugin,     "libdmtcp_ptrace.so"},      // Disabled
  {&enableModifyEnvPlugin,  "libdmtcp_modify-env.so"},  // Disabled
  {&enableUniqueCkptPlugin, "libdmtcp_unique-ckpt.so"}, // Disabled
  {&enableIB2TcpPlugin,     "libdmtcp_ib2tcp.so"},      // Disabled
  {&enableIBPlugin,         "libdmtcp_infiniband.so"},  // Disabled
  {&enableRMPlugin,         "libdmtcp_batch-queue.so"}, // Disabled
  {&enableAllocPlugin,      "libdmtcp_alloc.so"},       // Enabled
  {&enableDlPlugin,         "libdmtcp_dl.so"},          // Enabled
  {&enableIPCPlugin,        "libdmtcp_ipc.so"},         // Enabled
  {&enableLibDMTCP,         "libdmtcp.so"},             // Enabled
  {&enablePIDPlugin,        "libdmtcp_pid.so"}          // Enabled
};

const size_t numLibs = sizeof(pluginInfo) / sizeof (struct PluginInfo);

static CoordinatorAPI::CoordinatorMode allowedModes = CoordinatorAPI::COORD_ANY;

//shift args
#define shift argc--,argv++
static void processArgs(int *orig_argc, char ***orig_argv)
{
  char argc = *orig_argc;
  char **argv = *orig_argv;

  if (argc == 1) {
    printf("%s", DMTCP_VERSION_AND_COPYRIGHT_INFO);
    printf("(For help: %s --help)\n\n", argv[0]);
    exit(DMTCP_FAIL_RC);
  }

  //process args
  shift;
  while (true) {
    dmtcp::string s = argc>0 ? argv[0] : "--help";
    if ((s=="--help") && argc==1) {
      printf("%s", theUsage);
      exit(DMTCP_FAIL_RC);
    } else if ((s=="--version") && argc==1) {
      printf("%s", DMTCP_VERSION_AND_COPYRIGHT_INFO);
      exit(DMTCP_FAIL_RC);
    } else if (s=="--ssh-slave") {
      isSSHSlave = true;
      shift;
    } else if (s == "-j" || s == "--join") {
      allowedModes = dmtcp::CoordinatorAPI::COORD_JOIN;
      shift;
    } else if (s == "--gzip") {
      setenv(ENV_VAR_COMPRESSION, "1", 1);
      shift;
    } else if (s == "--no-gzip") {
      setenv(ENV_VAR_COMPRESSION, "0", 1);
      shift;
    }
#ifdef HBICT_DELTACOMP
    else if (s == "--hbict") {
      setenv(ENV_VAR_DELTACOMPRESSION, "1", 1);
      shift;
    } else if (s == "--no-hbict") {
      setenv(ENV_VAR_DELTACOMPRESSION, "0", 1);
      shift;
    }
#endif
    else if (s == "--new-coordinator") {
      allowedModes = dmtcp::CoordinatorAPI::COORD_NEW;
      shift;
    } else if (s == "--no-coordinator") {
      allowedModes = dmtcp::CoordinatorAPI::COORD_NONE;
      shift;
    } else if (s == "-i" || s == "--interval" ||
             (s.c_str()[0] == '-' && s.c_str()[1] == 'i' &&
              isdigit(s.c_str()[2]) ) ) {
      if (isdigit(s.c_str()[2])) { // if -i5, for example
        setenv(ENV_VAR_CKPT_INTR, s.c_str()+2, 1);
        shift;
      } else { // else -i 5
        setenv(ENV_VAR_CKPT_INTR, argv[1], 1);
        shift; shift;
      }
    } else if (argc>1 && (s == "-h" || s == "--host")) {
      setenv(ENV_VAR_NAME_HOST, argv[1], 1);
      shift; shift;
    } else if (argc>1 && (s == "-p" || s == "--port")) {
      setenv(ENV_VAR_NAME_PORT, argv[1], 1);
      shift; shift;
    } else if (argc>1 && s == "--port-file"){
      thePortFile = argv[1];
      shift; shift;
    } else if (argc>1 && (s == "--prefix")) {
      setenv(ENV_VAR_PREFIX_PATH, argv[1], 1);
      shift; shift;
    } else if (argc>1 && (s == "-c" || s == "--ckptdir")) {
      setenv(ENV_VAR_CHECKPOINT_DIR, argv[1], 1);
      shift; shift;
    } else if (argc>1 && (s == "-t" || s == "--tmpdir")) {
      setenv(ENV_VAR_TMPDIR, argv[1], 1);
      shift; shift;
    } else if (argc>1 && (s == "--mtcp-checkpoint-signal" ||
                          s == "--ckpt-signal")) {
      setenv(ENV_VAR_SIGCKPT, argv[1], 1);
      shift; shift;
    } else if (s == "--checkpoint-open-files" || s == "--ckpt-open-files") {
      checkpointOpenFiles = true;
      shift;
    } else if (s == "--ptrace") {
      enablePtracePlugin = true;
      shift;
    } else if (s == "--modify-env") {
      enableModifyEnvPlugin = true;
      shift;
    } else if (s == "--ib" || s == "--infiniband") {
      enableIBPlugin = true;
      shift;
    } else if (s == "--ib2tcp") {
      enableIB2TcpPlugin = true;
      shift;
    } else if (s == "--disable-alloc-plugin") {
      setenv(ENV_VAR_ALLOC_PLUGIN, "0", 1);
      shift;
    } else if (s == "--disable-dl-plugin") {
      setenv(ENV_VAR_DL_PLUGIN, "0", 1);
      shift;
    } else if (s == "--no-plugins" || s == "--disable-all-plugins") {
      disableAllPlugins = true;
      shift;
    } else if (s == "--rm" || s == "--batch-queue") {
      enableRMPlugin = true;
      shift;
    } else if (s == "--with-plugin") {
      setenv(ENV_VAR_PLUGIN, argv[1], 1);
      shift; shift;
    } else if (s == "-q" || s == "--quiet") {
      *getenv(ENV_VAR_QUIET) = *getenv(ENV_VAR_QUIET) + 1;
      // Just in case a non-standard version of setenv is being used:
      setenv(ENV_VAR_QUIET, getenv(ENV_VAR_QUIET), 1);
      shift;
    } else if ( (s.length()>2 && s.substr(0,2)=="--") ||
              (s.length()>1 && s.substr(0,1)=="-" ) ) {
      printf("Invalid Argument\n%s", theUsage);
      exit(DMTCP_FAIL_RC);
    } else if (argc>1 && s=="--") {
      shift;
      break;
    } else {
      break;
    }
  }
  *orig_argv = argv;
}

int main ( int argc, char** argv )
{
  for (size_t fd = PROTECTED_FD_START; fd < PROTECTED_FD_END; fd++) {
    close(fd);
  }

  if (! getenv(ENV_VAR_QUIET))
    setenv(ENV_VAR_QUIET, "0", 0);

  processArgs(&argc, &argv);

  initializeJalib();
  // If --ssh-slave and --prefix both are present, verify that the prefix-dir
  // of this binary (dmtcp_launch) is same as the one provided with
  // --prefix
  if (isSSHSlave && getenv(ENV_VAR_PREFIX_PATH) != NULL) {
    char buf[PATH_MAX];
    string prefixPath = getenv(ENV_VAR_PREFIX_PATH);
    prefixPath += "/bin/dmtcp_launch";
    JASSERT(realpath(prefixPath.c_str(), buf) != NULL) (prefixPath);
    prefixPath = buf;
    string programPath = jalib::Filesystem::GetProgramPath();
    JASSERT(prefixPath == programPath) (prefixPath) (programPath);
  }

  dmtcp::Util::setTmpDir(getenv(ENV_VAR_TMPDIR));
  dmtcp::UniquePid::ThisProcess(true);
  dmtcp::Util::initializeLogFile();

#ifdef FORKED_CHECKPOINTING
  /* When this is robust, add --forked-checkpointing option on command-line,
   * with #ifdef FORKED_CHECKPOINTING around the option, change default of
   * configure.ac, dmtcp/configure.ac, to enable, and change them
   * from enable-forked... to disable-...
   */
  setenv(ENV_VAR_FORKED_CKPT, "1", 1);
#endif

  // This code will go away when zero-mapped pages are implemented in MTCP.
  struct rlimit rlim;
  getrlimit(RLIMIT_STACK, &rlim);
  if (rlim.rlim_cur > 256*1024*1024 && rlim.rlim_cur != RLIM_INFINITY)
    JASSERT_STDERR <<
      "*** WARNING:  RLIMIT_STACK > 1/4 GB.  This causes each thread to"
      "\n***  receive a 1/4 GB stack segment.  Checkpoint/restart will be slow,"
      "\n***  and will potentially break if many threads are created."
      "\n*** Suggest setting (sh/bash):  ulimit -s 10000"
      "\n***                (csh/tcsh):  limit stacksize 10000"
      "\n*** prior to using DMTCP.  (This will be fixed in the future, when"
      "\n*** DMTCP supports restoring zero-mapped pages.)\n\n\n" ;
  // Remove this when zero-mapped pages are supported.  For segments with
  // no file backing:  Start with 4096 (page) offset and keep doubling offset
  // until finding region of memory segment with many zeroes.
  // Then mark as CS_ZERO_PAGES in MTCP instead of CS_RESTORE (or mark
  // entire segment as CS_ZERO_PAGES and then overwrite with CS_RESTORE
  // region for portion to be read back from checkpoint image.
  // For CS_ZERO_PAGES region, mmap // on restart, but don't write in zeroes.
  // Also, after checkpointing segment, munmap zero pages, and mmap them again.
  // Don't try to find all pages.  The above strategy may increase
  // the non-zero-mapped mapped pages to no more than double the actual
  // non-zero region (assuming that the zero-mapped pages are contiguous).
  // - Gene

  testMatlab(argv[0]);
  testJava(argv);  // Warn that -Xmx flag needed to limit virtual memory size

  // If libdmtcp.so is in standard search path and _also_ has setgid access,
  //   then LD_PRELOAD will work.
  // Otherwise, it will only work if the application does not use setuid and
  //   setgid access.  So, we test //   if the application does not use
  //   setuid/setgid.  (See 'man ld.so')
  // FIXME:  ALSO DO THIS FOR execwrappers.cpp:dmtcpPrepareForExec()
  //   Should pass libdmtcp.so path, and let testSetuid determine
  //     if setgid is set for it.  If so, no problem:  continue.
  //   If not, call testScreen() and adapt 'screen' to run using
  //     Util::patchArgvIfSetuid(argv[0], argv, &newArgv) (which shouldn't
  //     will just modify argv[0] to point to /tmp/dmtcp-USER@HOST/screen
  //     and other modifications:  doesn't need newArgv).
  //   If it's not 'screen' and if no setgid for libdmtcp.so, then testSetuid
  //    should issue the warning, unset our LD_PRELOAD, and hope for the best.
  //    A program like /usr/libexec/utempter/utempter (Fedora path)
  //    is short-lived and can be safely run.  Ideally, we should
  //    disable checkpoints while utempter is running, and enable checkpoints
  //    when utempter finishes.  See possible model at
  //    execwrappers.cpp:execLibProcessAndExit(), since the same applies
  //    to running /lib/libXXX.so for running libraries as executables.
  if (testSetuid(argv[0])) {
    char **newArgv;
    // THIS NEXT LINE IS DANGEROUS.  MOST setuid PROGRAMS CAN'T RUN UNPRIVILEGED
    dmtcp::Util::patchArgvIfSetuid(argv[0], argv, &newArgv);
    argv = newArgv;
  };

  if (argc > 0) {
    JTRACE("dmtcp_launch starting new program:")(argv[0]);
  }

  //set up CHECKPOINT_DIR
  if(getenv(ENV_VAR_CHECKPOINT_DIR) == NULL){
    const char* ckptDir = get_current_dir_name();
    if(ckptDir != NULL ){
      //copy to private buffer
      static dmtcp::string _buf = ckptDir;
      ckptDir = _buf.c_str();
    }else{
      ckptDir=".";
    }
    setenv ( ENV_VAR_CHECKPOINT_DIR, ckptDir, 0 );
    JTRACE("setting " ENV_VAR_CHECKPOINT_DIR)(ckptDir);
  }

  if ( checkpointOpenFiles )
    setenv( ENV_VAR_CKPT_OPEN_FILES, "1", 0 );
  else
    unsetenv( ENV_VAR_CKPT_OPEN_FILES);

  bool isElf, is32bitElf;
  if  (dmtcp::Util::elfType(argv[0], &isElf, &is32bitElf) == -1) {
    // Couldn't read argv_buf
    // FIXME:  This could have been a symbolic link.  Don't issue an error,
    //         unless we're sure that the executable is not readable.
    JASSERT_STDERR <<
      "*** ERROR:  Executable to run w/ DMTCP appears not to be readable,\n"
      "***         or no such executable in path.\n\n"
      << argv[0] << "\n";
    exit(DMTCP_FAIL_RC);
  } else {
    testStaticallyLinked(argv[0]);
  }

  if (getenv("DISPLAY") != NULL) {
    setenv("ORIG_DISPLAY", getenv("DISPLAY"), 1);
    // UNSET DISPLAY environment variable.
    unsetenv("DISPLAY");
  }

// FIXME:  Unify this code with code prior to execvp in execwrappers.cpp
//   Can use argument to dmtcpPrepareForExec() or getenv("DMTCP_...")
//   from DmtcpWorker constructor, to distinguish the two cases.
  dmtcp::Util::adjustRlimitStack();

  // Set DLSYM_OFFSET env var(s).
  dmtcp::Util::prepareDlsymWrapper();

  DmtcpUniqueProcessId compId;
  CoordinatorInfo coordInfo;
  struct in_addr localIPAddr;
  CoordinatorAPI::instance().connectToCoordOnStartup(allowedModes, argv[0],
                                                     &compId, &coordInfo,
                                                     &localIPAddr);
  Util::writeCoordPortToFile(getenv(ENV_VAR_NAME_PORT), thePortFile.c_str());
  /* We need to initialize SharedData here to make sure that it is
   * initialized with the correct coordinator timestamp.  The coordinator
   * timestamp is updated only during postCkpt callback. However, the
   * SharedData area may be initialized earlier (for example, while
   * recreating threads), causing it to use *older* timestamp.
   */
  SharedData::initialize(Util::getTmpDir().c_str(), &compId, &coordInfo,
                         &localIPAddr);

  setLDPreloadLibs(is32bitElf);

  //run the user program
  char **newArgv = NULL;
  if (testScreen(argv, &newArgv))
    execvp ( newArgv[0], newArgv );
  else
    execvp ( argv[0], argv );

  //should be unreachable
  JASSERT_STDERR <<
    "ERROR: Failed to exec(\"" << argv[0] << "\"): " << JASSERT_ERRNO << "\n"
    << "Perhaps it is not in your $PATH?\n"
    << "See `dmtcp_launch --help` for usage.\n";
  //fprintf(stderr, theExecFailedMsg, argv[0], JASSERT_ERRNO);

  return -1;
}

static int testMatlab(const char *filename)
{
#ifdef __GNUC__
# if __GNUC__ == 4 && __GNUC_MINOR__ > 1
  static const char* theMatlabWarning =
    "\n**** WARNING:  Earlier Matlab releases (e.g. release 7.4) use an\n"
    "****  older glibc.  Later releases (e.g. release 7.9) have no problem.\n"
    "****  \n"
    "****  If you are using an _earlier_ Matlab, please re-compile DMTCP\n"
    "****  with gcc-4.1 and g++-4.1\n"
    "**** env CC=gcc-4.1 CXX=g++-4.1 ./configure\n"
    "**** [ Also modify mtcp/Makefile to:  CC=gcc-4.1 ]\n"
    "**** [ Next, you may need an alternative Java JVM (see QUICK-START) ]\n"
    "**** [ Finally, run as:   dmtcp_launch matlab -nodisplay ]\n"
    "**** [   (DMTCP does not yet checkpoint X-Windows applications.) ]\n"
    "**** [ You may see \"Not checkpointing libc-2.7.so\".  This is normal. ]\n"
    "****   (Assuming you have done the above, Will now continue"
	    " executing.)\n\n" ;

  // FIXME:  should expand filename and "matlab" before checking
  if ( strcmp(filename, "matlab") == 0 && getenv(ENV_VAR_QUIET) == NULL) {
    JASSERT_STDERR << theMatlabWarning;
    return -1;
  }
# endif
#endif
  return 0;
}

// FIXME:  Remove this when DMTCP supports zero-mapped pages
static int testJava(char **argv)
{
  static const char* theJavaWarning =
    "\n**** WARNING:  Sun/Oracle Java claims a large amount of memory\n"
    "****  for its heap on startup.  As of DMTCP version 1.2.4, DMTCP _does_\n"
    "****  handle zero-mapped virtual memory, but it may take up to a\n"
    "****  minute.  This will be fixed to be much faster in a future\n"
    "****  version of DMTCP.  In the meantime, if your Java supports it,\n"
    "****  use the -Xmx flag for a smaller heap:  e.g.  java -Xmx64M javaApp\n"
    "****  (Invoke dmtcp_launch with --quiet to avoid this msg.)\n\n" ;

  if (getenv(ENV_VAR_QUIET) != NULL
      && strcmp(getenv(ENV_VAR_QUIET), "0") != 0)
    return 0;
  if ( strcmp(argv[0], "java") == 0 ) {
    while (*(++argv) != NULL)
      if (strncmp(*argv, "-Xmx", sizeof("-Xmx")-1) == 0)
        return 0; // The user called java with -Xmx.  No need for warning.
  }

  // If user has more than 4 GB of RAM, warn them that -Xmx is faster.
  int fd;
  char buf[100];
  static const char *meminfoPrefix = "MemTotal:       ";
  if ( (fd = open("/proc/meminfo", O_RDONLY)) != -1 &&
    read(fd, buf, sizeof(meminfoPrefix) + 16) == sizeof(meminfoPrefix) + 16 &&
    strncmp(buf, meminfoPrefix, sizeof(meminfoPrefix)+1) == 0 &&
    atol(buf + sizeof(meminfoPrefix)) > 17000000) /* units of kB : mem > 4 GB */
      JASSERT_STDERR << theJavaWarning;
  if (fd != -1)
    close(fd);
  return -1;
}

static bool testSetuid(const char *filename)
{
  if (dmtcp::Util::isSetuid(filename) &&
      strcmp(filename, "screen") != 0 && strstr(filename, "/screen") == NULL) {

    static const char* theSetuidWarning =
      "\n**** WARNING:  This process has the setuid or setgid bit set.  This is\n"
      "***  incompatible with the use by DMTCP of LD_PRELOAD.  The process\n"
      "***  will not be checkpointed by DMTCP.  Continuing and hoping\n"
      "***  for the best.  For some programs, you may wish to\n"
      "***  compile your own private copy, without using setuid permission.\n\n" ;

    JASSERT_STDERR << theSetuidWarning;
    sleep(3);
    return true;
  }
  return false;
}

void testStaticallyLinked(const char *pathname)
{
  if (dmtcp::Util::isStaticallyLinked(pathname)) {
    JASSERT_STDERR <<
      "*** WARNING:  " ELF_INTERPRETER " --verify " << pathname << " returns\n"
      << "***  nonzero status.\n"
      << "*** This often means that " << pathname << " is\n"
      << "*** a statically linked target.  If so, you can confirm this with\n"
      << "*** the 'file' command.\n"
      << "***  The standard DMTCP only supports dynamically"
      << " linked executables.\n"
      << "*** If you cannot recompile dynamically, please talk to the"
      << " developers about a\n"
      << "*** custom DMTCP version for statically linked executables.\n"
      << "*** Proceeding for now, and hoping for the best.\n\n";
  }
  return;
}

// Test for 'screen' program, argvPtr is an in- and out- parameter
static bool testScreen(char **argv, char ***newArgv)
{
  if (dmtcp::Util::isScreen(argv[0])) {
    dmtcp::Util::setScreenDir();
    dmtcp::Util::patchArgvIfSetuid(argv[0], argv, newArgv);
    return true;
  }
  return false;
}

static void setLDPreloadLibs(bool is32bitElf)
{
  // preloadLibs are to set LD_PRELOAD:
  //   LD_PRELOAD=PLUGIN_LIBS:UTILITY_DIR/libdmtcp.so:R_LIBSR_UTILITY_DIR/
  dmtcp::string preloadLibs = "";
  // FIXME:  If the colon-separated elements of ENV_VAR_PLUGIN are not
  //     absolute pathnames, then they must be expanded to absolute pathnames.
  //     Warn user if an absolute pathname is not valid.
  if ( getenv(ENV_VAR_PLUGIN) != NULL ) {
    preloadLibs += getenv(ENV_VAR_PLUGIN);
    preloadLibs += ":";
  }
  dmtcp::string preloadLibs32 = preloadLibs;

  // FindHelperUtiltiy requires ENV_VAR_UTILITY_DIR to be set
  dmtcp::string searchDir = jalib::Filesystem::GetProgramDir();
  setenv ( ENV_VAR_UTILITY_DIR, searchDir.c_str(), 0 );

  //set up Alloc plugin
  if (getenv(ENV_VAR_ALLOC_PLUGIN) != NULL){
    const char *ptr = getenv(ENV_VAR_ALLOC_PLUGIN);
    if (strcmp(ptr, "1") == 0) {
      enableAllocPlugin = true;
    } else if (strcmp(ptr, "0") == 0) {
      enableAllocPlugin = false;
    } else {
      JASSERT(false) (getenv(ENV_VAR_ALLOC_PLUGIN))
        .Text("Invalid value for the environment variable.");
    }
  }

  // Setup Dl plugin
  if (getenv(ENV_VAR_DL_PLUGIN) != NULL){
    const char *ptr = getenv(ENV_VAR_DL_PLUGIN);
    if (strcmp(ptr, "1") == 0) {
      enableDlPlugin = true;
    } else if (strcmp(ptr, "0") == 0) {
      enableDlPlugin = false;
    } else {
      JASSERT(false) (getenv(ENV_VAR_DL_PLUGIN))
        .Text("Invalid value for the environment variable.");
    }
  }

  if (disableAllPlugins) {
    preloadLibs = jalib::Filesystem::FindHelperUtility("libdmtcp.so");
#if defined(__x86_64__)
    preloadLibs32 = jalib::Filesystem::FindHelperUtility("libdmtcp.so", true);
#endif
  } else {
    for (size_t i = 0; i < numLibs; i++) {
      struct PluginInfo *p= &pluginInfo[i];
      if (*p->enabled) {
        preloadLibs += jalib::Filesystem::FindHelperUtility(p->lib) + ":";
#if defined(__x86_64__)
        preloadLibs32 +=
          jalib::Filesystem::FindHelperUtility(p->lib, true) + ":";
#endif
      }
    }
  }

  setenv(ENV_VAR_HIJACK_LIBS, preloadLibs.c_str(), 1);
#if defined(__x86_64__)
  setenv(ENV_VAR_HIJACK_LIBS_M32, preloadLibs32.c_str(), 1);
#endif

  // If dmtcp_launch was called with user LD_PRELOAD, and if
  //   if dmtcp_launch survived the experience, then pass it back to user.
  if (getenv("LD_PRELOAD")) {
    setenv(ENV_VAR_ORIG_LD_PRELOAD, getenv("LD_PRELOAD"), 1);
    preloadLibs = preloadLibs + ":" + getenv("LD_PRELOAD");
#if defined(__x86_64__)
    preloadLibs32 = preloadLibs32 + ":" + getenv("LD_PRELOAD");
#endif
  }

  setenv("LD_PRELOAD", preloadLibs.c_str(), 1);
#if defined(__x86_64__)
  if (is32bitElf) {
    string libdmtcp = jalib::Filesystem::FindHelperUtility("libdmtcp.so", true);
    JWARNING(libdmtcp != "libdmtcp.so")
      .Text("You appear to be checkpointing a 32-bit target under 64-bit Linux.\n"
            "DMTCP was unable to find the 32-bit installation.\n"
            "Try configure --enable-m32 ; make clean ; make ; make install");
    setenv("LD_PRELOAD", preloadLibs32.c_str(), 1);
  }
#endif
  JTRACE("getting value of LD_PRELOAD")
    (getenv("LD_PRELOAD")) (preloadLibs) (preloadLibs32);
}
