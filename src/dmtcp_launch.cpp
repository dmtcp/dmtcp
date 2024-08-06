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
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 11)
# include <sys/personality.h>
# ifndef ADDR_NO_RANDOMIZE
#  define ADDR_NO_RANDOMIZE  0x0040000  /* In case of old glibc, not defined */
# endif
#endif
#include "../jalib/jassert.h"
#include "../jalib/jconvert.h"
#include "../jalib/jfilesystem.h"
#include "constants.h"
#include "coordinatorapi.h"
#include "dmtcpmessagetypes.h"
#include "shareddata.h"
#include "syscallwrappers.h"
#include "util.h"

#define BINARY_NAME "dmtcp_launch"

using namespace dmtcp;

static void processArgs(int *orig_argc, const char ***orig_argv);
static int testMatlab(const char *filename);
static int testJava(const char **argv);
static bool testSetuid(const char *filename);
static void testStaticallyLinked(const char *filename);
static bool testScreen(const char **argv, const char ***newArgv);
static void testFsGsBase();
static void setLDPreloadLibs(bool is32bitElf);

// gcc-4.3.4 -Wformat=2 issues false positives for warnings unless the format
// string has at least one format specifier with corresponding format argument.
// Ubuntu 9.01 uses -Wformat=2 by default.
static const char *theUsage =
  "Usage: dmtcp_launch [OPTIONS] <command> [args...]\n"
  "Start a process under DMTCP control.\n\n"
  "Connecting to the DMTCP Coordinator:\n"
  "  -h, --coord-host HOSTNAME (environment variable DMTCP_COORD_HOST)\n"
  "              Hostname where dmtcp_coordinator is run (default: localhost)\n"
  "  -p, --coord-port PORT_NUM (environment variable DMTCP_COORD_PORT)\n"
  "              Port where dmtcp_coordinator is run (default: "
                                                  STRINGIFY(DEFAULT_PORT) ")\n"
  "  --port-file FILENAME\n"
  "              File to write listener port number.  (Useful with\n"
  "              '--coord-port 0', which is used to assign a random port)\n"
  "  -j, --join-coordinator\n"
  "              Join an existing coordinator, raise error if one doesn't\n"
  "              already exist\n"
  "  --new-coordinator\n"
  "              Create a new coordinator at the given port. Fail if one\n"
  "              already exists on the given port. The port can be specified\n"
  "              with --coord-port, or with environment variable \n"
  "              DMTCP_COORD_PORT.\n"
  "              If no port is specified, start coordinator at a random port\n"
  "              (same as specifying port '0').\n"
  "  --any-coordinator\n"
  "              Use --join-coordinator if possible, but only if port"
                                                            " was specified.\n"
  "              Else use --new-coordinator with specified port (if avail.),\n"
  "                and otherwise with the default port: --port "
                                                  STRINGIFY(DEFAULT_PORT) ")\n"
  "              (This is the default.)\n"
  "  -i, --interval SECONDS (environment variable DMTCP_CHECKPOINT_INTERVAL)\n"
  "              Time in seconds between automatic checkpoints.\n"
  "              0 implies never (manual ckpt only);\n"
  "              if not set and no env var, use default value set in\n"
  "              dmtcp_coordinator or dmtcp_command.\n"
  "              Not allowed if --join-coordinator is specified\n"
  "\n"
  "Checkpoint image generation:\n"
  "  --gzip, --no-gzip, (environment variable DMTCP_GZIP=[01])\n"
  "              Enable/disable compression of checkpoint images (default: 1)\n"
  "              WARNING: gzip adds seconds. Without gzip, ckpt is often < 1s\n"
#ifdef HBICT_DELTACOMP
  "  --hbict, --no-hbict, (environment variable DMTCP_HBICT=[01])\n"
  "              Enable/disable compression of checkpoint images (default: 1)\n"
#endif // ifdef HBICT_DELTACOMP
  "  --ckptdir PATH (environment variable DMTCP_CHECKPOINT_DIR)\n"
  "              Directory to store checkpoint images\n"
  "              (default: curr dir at launch)\n"
  "  --ckpt-open-files\n"
  "  --checkpoint-open-files\n"
  "              Checkpoint open files and restore old working dir.\n"
  "              (default: do neither)\n"
  "  --allow-file-overwrite\n"
  "              If used with --checkpoint-open-files, allows a saved file\n"
  "              to overwrite its existing copy at original location\n"
  "              (default: file overwrites are not allowed)\n"
  "  --ckpt-signal signum\n"
  "              Signal number used internally by DMTCP for checkpointing\n"
  "              (default: SIGUSR2/12).\n"
  "\n"
  "Enable/disable plugins:\n"
  "  --with-plugin (environment variable DMTCP_PLUGIN)\n"
  "              Colon-separated list of DMTCP plugins to be preloaded with\n"
  "              DMTCP.\n"
  "              (Absolute pathnames are required.)\n"
  "  --batch-queue, --rm\n"
  "              Enable support for resource managers (Torque PBS and SLURM).\n"
  "              (default: disabled)\n"
  "  --ptrace\n"
  "              Enable support for PTRACE system call for gdb/strace etc.\n"
  "              (default: disabled)\n"
  "  --modify-env\n"
  "              Update environment variables based on the environment on the\n"
  "              restart host (e.g., DISPLAY=$DISPLAY).\n"
  "              This can be set in a file dmtcp_env.txt.\n"
  "              (default: disabled)\n"
  "  --pathvirt\n"
  "              Update file pathnames based on DMTCP_PATH_PREFIX\n"
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
  "  --tmpdir PATH (environment variable DMTCP_TMPDIR)\n"
  "              Directory to store temp files (default: $TMDPIR or /tmp)\n"
  "              (Behavior is undefined if two launched processes specify\n"
  "               different tmpdirs.)\n"
  "  -q, --quiet (or set environment variable DMTCP_QUIET = 0, 1, or 2)\n"
  "              Skip NOTE messages; if given twice, also skip WARNINGs\n"
  "  --coord-logfile PATH (environment variable DMTCP_COORD_LOG_FILENAME\n"
  "              Coordinator will dump its logs to the given file\n"
  "  --mpi       Use ADDR_NO_RANDOMIZE personality, etc, for MPI applications\n"
  "  --help\n"
  "              Print this message and exit.\n"
  "  --version\n"
  "              Print version information and exit.\n"
  "\n"
  HELP_AND_CONTACT_INFO
  "\n";

// FIXME:  The warnings below should be collected into a single function,
// and also called after a user exec(), not just in dmtcp_launch.
// static const char* theExecFailedMsg =
// "ERROR: Failed to exec(\"%s\"): %s\n"
// "Perhaps it is not in your $PATH?\n"
// "See `dmtcp_launch --help` for usage.\n"
// ;

static bool disableAllPlugins = false;
static bool checkpointOpenFiles = false;

static bool enablePtracePlugin = false;
static bool enableModifyEnvPlugin = false;
static bool enableRMPlugin = false;
static bool explicitSrun = false;

static bool enableIB2TcpPlugin = false;
static bool enableIBPlugin = false;

static bool enableAllocPlugin = true;
static bool enableDlPlugin = true;
static bool enableIPCPlugin = true;
static bool enableSvipcPlugin = true;
static bool enablePathVirtPlugin = false;
static bool enableTimerPlugin = true;

#ifdef UNIQUE_CHECKPOINT_FILENAMES
static bool enableUniqueCkptPlugin = true;
#else // ifdef UNIQUE_CHECKPOINT_FILENAMES
static bool enableUniqueCkptPlugin = false;
#endif // ifdef UNIQUE_CHECKPOINT_FILENAMES

// This is the base library.
static bool enableLibDMTCP = true;

// PID plugin must come last.
static bool enablePIDPlugin = true;

struct PluginInfo {
  bool *enabled;
  const char *lib;
};

static struct PluginInfo pluginInfo[] = {               // Default value
  { &enablePtracePlugin, "libdmtcp_ptrace.so" },        // Disabled
  { &enableModifyEnvPlugin, "libdmtcp_modify-env.so" },  // Disabled
  { &enableUniqueCkptPlugin, "libdmtcp_unique-ckpt.so" }, // Disabled
  { &enableIB2TcpPlugin, "libdmtcp_ib2tcp.so" },        // Disabled
  { &enableIBPlugin, "libdmtcp_infiniband.so" },        // Disabled
  { &enableRMPlugin, "libdmtcp_batch-queue.so" },       // Disabled
  { &enableAllocPlugin, "libdmtcp_alloc.so" },          // Enabled
  { &enableDlPlugin, "libdmtcp_dl.so" },                // Enabled
  { &enableIPCPlugin, "libdmtcp_ipc.so" },              // Enabled
  { &enableSvipcPlugin, "libdmtcp_svipc.so" },          // Enabled
  { &enablePathVirtPlugin,  "libdmtcp_pathvirt.so"},    // Disabled
  { &enableTimerPlugin, "libdmtcp_timer.so" },          // Enabled
  { &enableLibDMTCP, "libdmtcp.so" },                   // Enabled
  // PID plugin must come last.
  { &enablePIDPlugin, "libdmtcp_pid.so" }               // Enabled
};

const size_t numLibs = sizeof(pluginInfo) / sizeof(struct PluginInfo);

static CoordinatorMode allowedModes = COORD_ANY;
string tmpDir = "tmpDir is not set";
string coord_host = "";
int coord_port = UNINITIALIZED_PORT;
static string thePortFile;

// shift args
#define shift argc--, argv++
static void
processArgs(int *orig_argc, const char ***orig_argv)
{
  int argc = *orig_argc;
  const char **argv = *orig_argv;
  const char *tmpdir_arg = NULL;

  if (argc == 1) {
    printf("%s", DMTCP_VERSION_AND_COPYRIGHT_INFO);
    printf("(For help: %s --help)\n\n", argv[0]);
    exit(DMTCP_FAIL_RC);
  }

  // process args
  shift;
  while (true) {
    string s = argc > 0 ? argv[0] : "--help";
    if ((s == "--help") && argc <= 1) {
      printf("%s", theUsage);
      exit(0);
    } else if ((s == "--version") && argc == 1) {
      printf("%s", DMTCP_VERSION_AND_COPYRIGHT_INFO);
      exit(0);
    } else if (s == "-j" || s == "--join-coordinator" || s == "--join") {
      allowedModes = COORD_JOIN;
      shift;
    } else if (s == "--gzip") {
      /* NOTE: If gzip flags not set, default is --gzip */
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
#endif // ifdef HBICT_DELTACOMP
    else if (s == "--new-coordinator") {
      allowedModes = COORD_NEW;
      shift;
    } else if (s == "--any-coordinator") {
      allowedModes = COORD_ANY;
      shift;
    } else if (s == "-i" || s == "--interval") {
      setenv(ENV_VAR_CKPT_INTR, argv[1], 1);
      shift; shift;
    } else if (s == "--coord-logfile") {
      setenv(ENV_VAR_COORD_LOGFILE, argv[1], 1);
      shift; shift;
    } else if (argv[0][0] == '-' && argv[0][1] == 'i' &&
               isdigit(argv[0][2])) { // else if -i5, for example
      setenv(ENV_VAR_CKPT_INTR, argv[0] + 2, 1);
      shift;
    } else if (argc > 1 &&
               (s == "-h" || s == "--coord-host" || s == "--host")) {
      coord_host = argv[1];
      shift; shift;
    } else if (argc > 1 &&
               (s == "-p" || s == "--coord-port" || s == "--port")) {
      coord_port = jalib::StringToInt(argv[1]);
      shift; shift;
    } else if (argv[0][0] == '-' && argv[0][1] == 'p' &&
               isdigit(argv[0][2])) { // else if -p0, for example
      coord_port = jalib::StringToInt(&argv[0][2]);
      shift;
    } else if (argc > 1 && s == "--port-file") {
      thePortFile = argv[1];
      shift; shift;
    } else if (argc > 1 && (s == "-c" || s == "--ckptdir")) {
      setenv(ENV_VAR_CHECKPOINT_DIR, argv[1], 1);
      shift; shift;
    } else if (argc > 1 && (s == "-t" || s == "--tmpdir")) {
      tmpdir_arg = argv[1];
      shift; shift;
    } else if (argc > 1 && s == "--ckpt-signal") {
      setenv(ENV_VAR_SIGCKPT, argv[1], 1);
      shift; shift;
    } else if (s == "--checkpoint-open-files" || s == "--ckpt-open-files") {
      checkpointOpenFiles = true;
      shift;
    } else if (s == "--allow-file-overwrite") {
      setenv(ENV_VAR_ALLOW_OVERWRITE_WITH_CKPTED_FILES, "1", 0);
      shift;
    } else if (s == "--ptrace") {
      enablePtracePlugin = true;
      shift;
    } else if (s == "--modify-env") {
      enableModifyEnvPlugin = true;
      shift;
    } else if (s == "--pathvirt") {
      enablePathVirtPlugin = true;
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
    } else if (s == "--explicit-srun") {
      explicitSrun = true;
      shift;
    } else if (s == "--with-plugin") {
      setenv(ENV_VAR_PLUGIN, argv[1], 1);
      shift; shift;
    } else if (s == "-q" || s == "--quiet") {
      *getenv(ENV_VAR_QUIET) = *getenv(ENV_VAR_QUIET) + 1;

      // Just in case a non-standard version of setenv is being used:
      setenv(ENV_VAR_QUIET, getenv(ENV_VAR_QUIET), 1);
      shift;
    } else if (s == "--mpi") {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 11)
      personality(ADDR_NO_RANDOMIZE);
#endif
      shift;
    } else if ((s.length() > 2 && s.substr(0, 2) == "--") ||
               (s.length() > 1 && s.substr(0, 1) == "-")) {
      printf("Invalid Argument\n%s", theUsage);
      exit(DMTCP_FAIL_RC);
    } else if (argc > 1 && s == "--") {
      shift;
      break;
    } else {
      break;
    }
  }

#ifdef FAST_RST_VIA_MMAP
  // In case of fast restart, we shall not use gzip.
  setenv(ENV_VAR_COMPRESSION, "0", 1);
#endif

#if __aarch64__

  /* FIXME:  Currently, there is a bug exposed by SIGRETURN for aarch64,
   *      when we create a SIGCHLD handler for the gzip process.
   *      So, we're temporarily disabling GZIP for aarch64.
   * NOTE:  This occurs _only_ on second CKPT of 'make check' tests.
   */
  if (getenv(ENV_VAR_COMPRESSION) == NULL /* NULL default => --gzip */ ||
      strcmp(getenv(ENV_VAR_COMPRESSION), "1") == 0) {
    setenv(ENV_VAR_COMPRESSION, "0", 1);
    if (getenv(ENV_VAR_QUIET) != NULL &&
        strcmp(getenv(ENV_VAR_QUIET), "0") == 0) {
      JASSERT_STDERR <<
        "\n*** Turning off gzip compression.  The armv8 CPU support is"
        " still in beta testing.\n*** Gzip not yet supported.\n\n";
    }
  }
#endif // if __aarch64__
  if (coord_port == UNINITIALIZED_PORT &&
      (getenv(ENV_VAR_NAME_PORT) == NULL ||
       getenv(ENV_VAR_NAME_PORT)[0]== '\0') &&
      allowedModes != COORD_NEW) {
    allowedModes = (allowedModes == COORD_ANY) ? COORD_NEW : allowedModes;
    // Use static; some compilers save string const on local stack otherwise.
    static const char *default_port = STRINGIFY(DEFAULT_PORT);
    setenv(ENV_VAR_NAME_PORT, default_port, 1);
    JTRACE("No port specified\n"
           "Setting mode to --new-coordinator --coord-port "
           STRINGIFY(DEFAULT_PORT));
  }
  tmpDir = Util::calcTmpDir(tmpdir_arg);
  *orig_argc = argc;
  *orig_argv = argv;
}

int
main(int argc, const char **argv)
{
  for (size_t fd = PROTECTED_FD_START; fd < PROTECTED_FD_END; fd++) {
    close(fd);
  }

  if (!getenv(ENV_VAR_QUIET)) {
    setenv(ENV_VAR_QUIET, "0", 0);
  }

  // This will change argv to refer to the target application.
  processArgs(&argc, &argv);

  initializeJalib();

  // FIXME:  This was changed in Mar., 2022.
  //         We can remove this msg in 2 or 3 years.
  if (getenv("DMTCP_ABORT_ON_FAILED_ASSERT")) {
    JNOTE("\n\n*********************************************\n"
              "* DMTCP_ABORT_ON_FAILED_ASSERT is obsolete. *\n"
              "* Plesae use DMTCP_ABORT_ON_FAILURE instead.*\n"
              "*********************************************\n");
  }

  UniquePid::ThisProcess(true);

  Util::initializeLogFile(tmpDir.c_str(), "dmtcp_launch");

  DmtcpUniqueProcessId compId;
  CoordinatorInfo coordInfo;
  struct in_addr localIPAddr;

  // Initialize host and port now.  Will be used in low-level functions.
  CoordinatorAPI::getCoordHostAndPort(allowedModes, &coord_host, &coord_port);
  CoordinatorAPI::connectToCoordOnStartup(allowedModes, argv[0],
                                          &compId, &coordInfo,
                                          &localIPAddr);

  // If port was 0, we'll get new random port when coordinator starts up.
  CoordinatorAPI::getCoordHostAndPort(allowedModes, &coord_host, &coord_port);
  Util::writeCoordPortToFile(coord_port, thePortFile.c_str());

  /* We need to initialize SharedData here to make sure that it is
   * initialized with the correct coordinator timestamp.  The coordinator
   * timestamp is updated only during postCkpt callback. However, the
   * SharedData area may be initialized earlier (for example, while
   * recreating threads), causing it to use *older* timestamp.
   */
  SharedData::initialize(tmpDir.c_str(), &compId, &coordInfo, &localIPAddr);


#ifdef FORKED_CHECKPOINTING

  /* When this is robust, add --forked-checkpointing option on command-line,
   * with #ifdef FORKED_CHECKPOINTING around the option, change default of
   * configure.ac, dmtcp/configure.ac, to enable, and change them
   * from enable-forked... to disable-...
   */
  setenv(ENV_VAR_FORKED_CKPT, "1", 1);
#endif // ifdef FORKED_CHECKPOINTING

  // This code will go away when zero-mapped pages are implemented in MTCP.
  struct rlimit rlim;
  getrlimit(RLIMIT_STACK, &rlim);
  if (rlim.rlim_cur > 256 * 1024 * 1024 && rlim.rlim_cur != RLIM_INFINITY) {
    JASSERT_STDERR <<
      "*** WARNING:  RLIMIT_STACK > 1/4 GB.  This causes each thread to"
      "\n***  receive a 1/4 GB stack segment.  Checkpoint/restart will be slow,"
      "\n***  and will potentially break if many threads are created."
      "\n*** Suggest setting (sh/bash):  ulimit -s 10000"
      "\n***                (csh/tcsh):  limit stacksize 10000"
      "\n*** prior to using DMTCP.  (This will be fixed in the future, when"
      "\n*** DMTCP supports restoring zero-mapped pages.)\n\n\n";
  }

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
  // then LD_PRELOAD will work.
  // Otherwise, it will only work if the application does not use setuid and
  // setgid access.  So, we test //   if the application does not use
  // setuid/setgid.  (See 'man ld.so')
  // FIXME:  ALSO DO THIS FOR execwrappers.cpp:dmtcpPrepareForExec()
  // Should pass libdmtcp.so path, and let testSetuid determine
  // if setgid is set for it.  If so, no problem:  continue.
  // If not, call testScreen() and adapt 'screen' to run using
  // Util::patchArgvIfSetuid(argv[0], argv, &newArgv) (which shouldn't
  // will just modify argv[0] to point to /tmp/dmtcp-USER@HOST/screen
  // and other modifications:  doesn't need newArgv).
  // If it's not 'screen' and if no setgid for libdmtcp.so, then testSetuid
  // should issue the warning, unset our LD_PRELOAD, and hope for the best.
  // A program like /usr/libexec/utempter/utempter (Fedora path)
  // is short-lived and can be safely run.  Ideally, we should
  // disable checkpoints while utempter is running, and enable checkpoints
  // when utempter finishes.  See possible model at
  // execwrappers.cpp:execLibProcessAndExit(), since the same applies
  // to running /lib/libXXX.so for running libraries as executables.
  if (testSetuid(argv[0])) {
    const char **newArgv;

    // THIS NEXT LINE IS DANGEROUS.  MOST setuid PROGRAMS CAN'T RUN UNPRIVILEGED
    Util::patchArgvIfSetuid(argv[0], (const char **)argv, &newArgv);
    argv = newArgv;
  }

  // Test and set env var for FSGSBASE. The env var is used by MANA.
#ifdef __x86_64__
  testFsGsBase(); // Fsgsbase is used only on Intel/AMD x86_64.
#endif

  if (argc > 0) {
    JTRACE("dmtcp_launch starting new program:")(argv[0]);
  }

  // set up CHECKPOINT_DIR
  if (getenv(ENV_VAR_CHECKPOINT_DIR) == NULL) {
    const char *ckptDir = get_current_dir_name();
    if (ckptDir != NULL) {
      // copy to private buffer
      static string _buf = ckptDir;
      ckptDir = _buf.c_str();
    } else {
      ckptDir = ".";
    }
    setenv(ENV_VAR_CHECKPOINT_DIR, ckptDir, 0);
    JTRACE("setting " ENV_VAR_CHECKPOINT_DIR)(ckptDir);
  }

  if (checkpointOpenFiles) {
    setenv(ENV_VAR_CKPT_OPEN_FILES, "1", 0);
  } else {
    unsetenv(ENV_VAR_CKPT_OPEN_FILES);
  }

  bool isElf, is32bitElf;
  if (Util::getInterpreterType(argv[0], &isElf, &is32bitElf) == -1 &&
      Util::elfType(argv[0], &isElf, &is32bitElf) == -1) {
    // Couldn't read argv_buf
    // FIXME:  This could have been a symbolic link.  Don't issue an error,
    // unless we're sure that the executable is not readable.
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

  // Unset SESSION_MANAGER environment variable. SESSION_MANAGER is used by the
  // X Window system to save and restore the current state of applications.
  // For example, on a session crash, the X session manager attempts to reopen
  // the applications that were open before the crash.  An application has to
  // create a (socket) connection to the SESSION_MANAGER to communicate the
  // current state.
  // See http://en.wikipedia.org/wiki/X_session_manager for more information.
  //
  // For our purposes, we don't care about session management and thus we
  // should disable the SESSION_MANAGER environment variable to prevent the
  // application from communication with the X session manager.
  if (getenv("SESSION_MANAGER") != NULL) {
    JTRACE("Unsetting SESSION_MANAGER environment variable.");
    unsetenv("SESSION_MANAGER");
  }

  if (explicitSrun) {
    setenv(ENV_VAR_EXPLICIT_SRUN, "1", 1);
  }

  // FIXME:  Unify this code with code prior to execvp in execwrappers.cpp
  // Can use argument to dmtcpPrepareForExec() or getenv("DMTCP_...")
  // from DmtcpWorker constructor, to distinguish the two cases.
  Util::adjustRlimitStack();

  setLDPreloadLibs(is32bitElf);

  // run the user program
  const char **newArgv = NULL;
  if (testScreen(argv, &newArgv)) {
    execvp(newArgv[0], (char* const*) newArgv);
  } else {
    execvp(argv[0], (char* const*) argv);
  }

  // should be unreachable
  JASSERT_STDERR <<
    "ERROR: Failed to exec(\"" << argv[0] << "\"): " << JASSERT_ERRNO << "\n"
                 << "Perhaps it is not in your $PATH?\n"
                 << "See `dmtcp_launch --help` for usage.\n";

  // fprintf(stderr, theExecFailedMsg, argv[0], JASSERT_ERRNO);

  return -1;
}

static int
testMatlab(const char *filename)
{
#ifdef __GNUC__
# if __GNUC__ == 4 && __GNUC_MINOR__ > 1
  static const char *theMatlabWarning =
    "\n**** WARNING:  Earlier Matlab releases (e.g. release 7.4) use an\n"
    "****  older glibc.  Later releases (e.g. release 7.9) have no problem.\n"
    "****\n"
    "****  If you are using an _earlier_ Matlab, please re-compile DMTCP\n"
    "****  with gcc-4.1 and g++-4.1\n"
    "**** env CC=gcc-4.1 CXX=g++-4.1 ./configure\n"
    "**** [ Also modify mtcp/Makefile to:  CC=gcc-4.1 ]\n"
    "**** [ Next, you may need an alternative Java JVM (see QUICK-START.md) ]\n"
    "**** [ Finally, run as:   dmtcp_launch matlab -nodisplay ]\n"
    "**** [   (DMTCP does not yet checkpoint X-Windows applications.) ]\n"
    "**** [ You may see \"Not checkpointing libc-2.7.so\".  This is normal. ]\n"
    "****   (Assuming you have done the above, Will now continue"
    " executing.)\n\n";

  // FIXME:  should expand filename and "matlab" before checking
  if (strcmp(filename, "matlab") == 0 &&
      (getenv(ENV_VAR_QUIET) == NULL || strcmp(getenv(ENV_VAR_QUIET), "0"))) {
    JASSERT_STDERR << theMatlabWarning;
    return -1;
  }
# endif // if __GNUC__ == 4 && __GNUC_MINOR__ > 1
#endif // ifdef __GNUC__
  return 0;
}

// FIXME:  Remove this when DMTCP supports zero-mapped pages
static int
testJava(const char **argv)
{
  static const char *theJavaWarning =
    "\n**** WARNING:  Sun/Oracle Java claims a large amount of memory\n"
    "****  for its heap on startup.  As of DMTCP version 1.2.4, DMTCP _does_\n"
    "****  handle zero-mapped virtual memory, but it may take up to a\n"
    "****  minute.  This will be fixed to be much faster in a future\n"
    "****  version of DMTCP.  In the meantime, if your Java supports it,\n"
    "****  use the -Xmx flag for a smaller heap:  e.g.  java -Xmx64M javaApp\n"
    "****  (Invoke dmtcp_launch with --quiet to avoid this msg.)\n\n";

  if (getenv(ENV_VAR_QUIET) != NULL
      && strcmp(getenv(ENV_VAR_QUIET), "0") != 0) {
    return 0;
  }
  if (strcmp(argv[0], "java") == 0) {
    while (*(++argv) != NULL) {
      if (strncmp(*argv, "-Xmx", sizeof("-Xmx") - 1) == 0) {
        return 0; // The user called java with -Xmx.  No need for warning.
      }
    }
  }

  // If user has more than 4 GB of RAM, warn them that -Xmx is faster.
  int fd;
  char buf[100];
  static const char *meminfoPrefix = "MemTotal:       ";
  if ((fd = open("/proc/meminfo", O_RDONLY)) != -1 &&
      read(fd, buf, sizeof(meminfoPrefix) + 16) == sizeof(meminfoPrefix) + 16 &&
      strncmp(buf, meminfoPrefix, sizeof(meminfoPrefix) + 1) == 0 &&
      atol(buf + sizeof(meminfoPrefix)) > 17000000) { /* units of kB : mem > 4
                                                         GB */
    JASSERT_STDERR << theJavaWarning;
  }
  if (fd != -1) {
    close(fd);
  }
  return -1;
}

static bool
testSetuid(const char *filename)
{
  if (Util::isSetuid(filename) &&
      strcmp(filename, "screen") != 0 && strstr(filename, "/screen") == NULL) {
    static const char *theSetuidWarning =
      "\n"
      "**** WARNING:  This process has the setuid or setgid bit set.  This is\n"
      "***  incompatible with the use by DMTCP of LD_PRELOAD.  The process\n"
      "***  will not be checkpointed by DMTCP.  Continuing and hoping\n"
      "***  for the best.  For some programs, you may wish to\n"
      "***  compile your own private copy, without using setuid permission.\n";

    JASSERT_STDERR << theSetuidWarning;
    sleep(3);
    return true;
  }
  return false;
}

void
testStaticallyLinked(const char *pathname)
{
  if (Util::isStaticallyLinked(pathname)) {
    JASSERT_STDERR <<
      "*** WARNING:  " ELF_INTERPRETER " --verify " << pathname << " returns\n"
                   << "***  nonzero status.\n"
                   << "*** This often means that " << pathname << " is\n"
                   <<
      "*** a statically linked target.  If so, you can confirm this with\n"
                   << "*** the 'file' command.\n"
                   << "***  The standard DMTCP only supports dynamically"
                   << " linked executables.\n"
                   <<
      "*** If you cannot recompile dynamically, please talk to the"
                   << " developers about a\n"
                   <<
      "*** custom DMTCP version for statically linked executables.\n"
                   << "*** Proceeding for now, and hoping for the best.\n\n";
  }
}

// Test for 'screen' program, argvPtr is an in- and out- parameter
static bool
testScreen(const char **argv, const char ***newArgv)
{
  if (Util::isScreen(argv[0])) {
    Util::setScreenDir();
    Util::patchArgvIfSetuid(argv[0], argv, newArgv);
    return true;
  }
  return false;
}

// Test for fsgsbase feature.
static void
testFsGsBase()
{
#ifdef __x86_64__
  pid_t childPid = fork();
  JASSERT(childPid != -1);

  if (childPid == 0) {
    unsigned long fsbase = -1;
    // On systems without FSGSBASE support (Linux kernel < 5.9, this instruction
    // fails with SIGILL).
    asm volatile("rex.W\n rdfsbase %0" : "=r" (fsbase) :: "memory");
    if (fsbase != (unsigned long)-1) {
      exit(0);
    }

    // Also test wrfsbase in case it generates SIGILL as well.
    asm volatile("rex.W\n wrfsbase %0" :: "r" (fsbase) : "memory");
    exit(1);
  }

  int status = 0;
  JASSERT(waitpid(childPid, &status, 0) == childPid);

  if (status == 0) {
    setenv(ENV_VAR_FSGSBASE_ENABLED, "1", 1);
  } else {
    setenv(ENV_VAR_FSGSBASE_ENABLED, "0", 1);
  }
#endif
}

static void
setLDPreloadLibs(bool is32bitElf)
{
  // preloadLibs are to set LD_PRELOAD:
  // LD_PRELOAD=PLUGIN_LIBS:UTILITY_DIR/libdmtcp.so:R_LIBSR_UTILITY_DIR/
  string preloadLibs = "";

  // FIXME:  If the colon-separated elements of ENV_VAR_PLUGIN are not
  // absolute pathnames, then they must be expanded to absolute pathnames.
  // Warn user if an absolute pathname is not valid.
  if (getenv(ENV_VAR_PLUGIN) != NULL) {
    preloadLibs += getenv(ENV_VAR_PLUGIN);
    preloadLibs += ":";
  }
  string preloadLibs32 = preloadLibs;

  // set up Alloc plugin
  if (getenv(ENV_VAR_ALLOC_PLUGIN) != NULL) {
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
  if (getenv(ENV_VAR_DL_PLUGIN) != NULL) {
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
    preloadLibs = Util::getPath("libdmtcp.so");
#if defined(__x86_64__) || defined(__aarch64__)
    preloadLibs32 = Util::getPath("libdmtcp.so", true);
#endif // if defined(__x86_64__) || defined(__aarch64__)
  } else {
    for (size_t i = 0; i < numLibs; i++) {
      struct PluginInfo *p = &pluginInfo[i];
      if (*p->enabled) {
        preloadLibs += Util::getPath(p->lib, is32bitElf);
        preloadLibs += ":";
#if defined(__x86_64__) || defined(__aarch64__)
        preloadLibs32 += Util::getPath(p->lib, true);
        preloadLibs32 += ":";
#endif // if defined(__x86_64__) || defined(__aarch64__)
      }
    }
  }

  setenv(ENV_VAR_HIJACK_LIBS, preloadLibs.c_str(), 1);
#if defined(__x86_64__) || defined(__aarch64__)
  setenv(ENV_VAR_HIJACK_LIBS_M32, preloadLibs32.c_str(), 1);
#endif // if defined(__x86_64__) || defined(__aarch64__)

  // If dmtcp_launch was called with user LD_PRELOAD, and if
  // dmtcp_launch survived the experience, then pass it back to user.
  if (getenv("LD_PRELOAD")) {
    setenv(ENV_VAR_ORIG_LD_PRELOAD, getenv("LD_PRELOAD"), 1);
    preloadLibs = preloadLibs + ":" + getenv("LD_PRELOAD");
#if defined(__x86_64__) || defined(__aarch64__)
    preloadLibs32 = preloadLibs32 + ":" + getenv("LD_PRELOAD");
#endif // if defined(__x86_64__) || defined(__aarch64__)
  }

  setenv("LD_PRELOAD", preloadLibs.c_str(), 1);
#if defined(__x86_64__) || defined(__aarch64__)
  if (is32bitElf) {
    string libdmtcp = Util::getPath("libdmtcp.so", true);
    JWARNING(libdmtcp != "libdmtcp.so") (libdmtcp)
    .Text("You appear to be checkpointing a 32-bit target under 64-bit Linux.\n"
          "DMTCP was unable to find the 32-bit installation.\n"
          "See DMTCP FAQ or try:\n"
          "  ./configure --enable-m32 && make clean && make -j && "
          "make install\n"
          "  ./configure && make clean && make -j && make install\n");
    setenv("LD_PRELOAD", preloadLibs32.c_str(), 1);
  }
#endif // if defined(__x86_64__) || defined(__aarch64__)
  JTRACE("getting value of LD_PRELOAD")
    (getenv("LD_PRELOAD")) (preloadLibs) (preloadLibs32);
}
