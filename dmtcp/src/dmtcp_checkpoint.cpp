/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *   This file is part of the dmtcp/src module of DMTCP (DMTCP:dmtcp/src).  *
 *                                                                          *
 *  DMTCP:dmtcp/src is free software: you can redistribute it and/or        *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP:dmtcp/src is distributed in the hope that it will be useful,      *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include <unistd.h>
#include <stdlib.h>
#include <string>
#include <stdio.h>
#include  "../jalib/jassert.h"
#include <ctype.h>
#include  "../jalib/jfilesystem.h"
#include  "../jalib/jconvert.h"
#include "constants.h"
#include "dmtcpworker.h"
#include "dmtcpmessagetypes.h"
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/personality.h>

// gcc-4.3.4 -Wformat=2 issues false positives for warnings unless the format
// string has at least one format specifier with corresponding format argument.
// Ubuntu 9.01 uses -Wformat=2 by default.
static const char* theUsage =
  "USAGE: \n"
  "  dmtcp_checkpoint [OPTIONS] <command> [args...]\n\n"
  "OPTIONS:\n"
  "  --host, -h, (environment variable DMTCP_HOST):\n"
  "      Hostname where dmtcp_coordinator is run (default: localhost)\n"
  "  --port, -p, (environment variable DMTCP_PORT):\n"
  "      Port where dmtcp_coordinator is run (default: 7779)\n"
  "  --gzip, --no-gzip, (environment variable DMTCP_GZIP=[01]):\n"
  "      Enable/disable compression of checkpoint images (default: 1)\n"
  "  --ckptdir, -c, (environment variable DMTCP_CHECKPOINT_DIR):\n"
  "      Directory to store checkpoint images (default: ./)\n"
  "  --tmpdir, -t, (environment variable DMTCP_TMPDIR):\n"
  "      Directory to store temporary files \n"
  "        (default: $TMDPIR/dmtcp-$USER@$HOST or /tmp/dmtcp-$USER@$HOST)\n"
  "  --join, -j:\n"
  "      Join an existing coordinator, raise error if one already exists\n"
  "  --new, -n:\n"
  "      Create a new coordinator, raise error if one already exists\n"
  "  --new-coordinator:\n"
  "      Create a new coordinator even if one already exists\n"
  "  --batch, -b:\n"
  "      Enable batch mode i.e. start the coordinator on the same node on\n"
  "        a randomly assigned port (if no port is specified by --port)\n"
  "  --interval, -i, (environment variable DMTCP_CHECKPOINT_INTERVAL):\n"
  "      Time in seconds between automatic checkpoints.\n"
  "      Not allowed if --join is specified\n"
  "      --batch implies -i 3600, unless otherwise specified.\n"
  "  --no-check:\n"
  "      Skip check for valid coordinator and never start one automatically\n"
  "  --checkpoint-open-files:\n"
  "      Checkpoint open files\n"
  "  --mtcp-checkpoint-signal:\n"
  "      Signal number used internally by MTCP for checkpointing (default: 12)\n"
  "  --quiet, -q, (or set environment variable DMTCP_QUIET = 0, 1, or 2):\n"
  "      Skip banner and NOTE messages; if given twice, also skip WARNINGs\n\n"
  "See http://dmtcp.sf.net/ for more information.\n"
;

static const char* theBanner =
  "DMTCP/MTCP  Copyright (C) 2006-2008  Jason Ansel, Michael Rieker,\n"
  "                                       Kapil Arya, and Gene Cooperman\n"
  "This program comes with ABSOLUTELY NO WARRANTY.\n"
  "This is free software, and you are welcome to redistribute it\n"
  "under certain conditions; see COPYING file for details.\n"
  "(Use flag \"-q\" to hide this message.)\n\n"
;

static const char* theExecFailedMsg =
  "ERROR: Failed to exec(\"%s\"): %s\n"
  "Perhaps it is not in your $PATH?\n"
  "See `dmtcp_checkpoint --help` for usage.\n"
;

static const char* theMatlabWarning =
  "\n**** WARNING:  Earlier Matlab releases (e.g. release 7.4) use an\n"
  "****  older glibc.  Later releases (e.g. release 7.9 have no problem.\n"
  "****  \n"
  "****  If you are using an _earlier_ Matlab, please re-compile DMTCP/MTCP\n"
  "****  with gcc-4.1 and g++-4.1\n"
  "**** env CC=gcc-4.1 CXX=g++-4.1 ./configure\n"
  "**** [ Also modify mtcp/Makefile to:  CC=gcc-4.1 ]\n"
  "**** [ Next, you may need an alternative Java JVM (see QUICK-START) ]\n"
  "**** [ Finally, run as:   dmtcp_checkpoint matlab -nodisplay ]\n"
  "**** [   (DMTCP does not yet checkpoint X-Windows applications.) ]\n"
  "**** [ You may see \"Not checkpointing libc-2.7.so\".  This is normal. ]\n"
  "****   (Assuming you have done the above, Will now continue executing.)\n\n"
;
static dmtcp::string _stderrProcPath()
{
  return "/proc/" + jalib::XToString ( getpid() ) + "/fd/" + jalib::XToString ( fileno ( stderr ) );
}

//shift args
#define shift argc--,argv++
int main ( int argc, char** argv )
{
  bool isSSHSlave=false;
  bool autoStartCoordinator=true;
  bool checkpointOpenFiles=false;
  int allowedModes = dmtcp::DmtcpWorker::COORD_ANY;

#ifdef ENABLE_MALLOC_WRAPPER
  long mallocOff, callocOff, freeOff, reallocOff;

  void *baseAddr = (void*)&toupper;
  mallocOff  = (char*)&malloc  - (char*)baseAddr;
  callocOff  = (char*)&calloc  - (char*)baseAddr;
  reallocOff = (char*)&realloc - (char*)baseAddr;
  freeOff    = (char*)&free    - (char*)baseAddr;

  setenv ( ENV_VAR_MALLOC_OFFSET, jalib::XToString ( mallocOff ).c_str(), 1 );
  setenv ( ENV_VAR_CALLOC_OFFSET, jalib::XToString ( callocOff ).c_str(), 1 );
  setenv ( ENV_VAR_REALLOC_OFFSET, jalib::XToString ( reallocOff ).c_str(), 1 );
  setenv ( ENV_VAR_FREE_OFFSET, jalib::XToString ( freeOff ).c_str(), 1 );
#endif

  if (! getenv(ENV_VAR_QUIET))
    setenv(ENV_VAR_QUIET, "0", 0);

  //process args
  shift;
  while(true){
    dmtcp::string s = argc>0 ? argv[0] : "--help";
    if(s=="--help" || s=="-h" && argc==1){
      JASSERT_STDERR << theUsage;
      //fprintf(stderr, theUsage, "");
      return 1;
    }else if(s=="--ssh-slave"){
      isSSHSlave = true;
      shift;
    }else if(s == "--no-check"){
      autoStartCoordinator = false;
      shift;
    }else if(s == "-j" || s == "--join"){
      allowedModes = dmtcp::DmtcpWorker::COORD_JOIN;
      shift;
    }else if(s == "--gzip"){
      setenv(ENV_VAR_COMPRESSION, "1", 1);
      shift;
    }else if(s == "--no-gzip"){
      setenv(ENV_VAR_COMPRESSION, "0", 1);
      shift;
    }else if(s == "-n" || s == "--new"){
      allowedModes = dmtcp::DmtcpWorker::COORD_NEW;
      shift;
    }else if(s == "--new-coordinator"){
      allowedModes = dmtcp::DmtcpWorker::COORD_FORCE_NEW;
      shift;
    }else if(s == "-b" || s == "--batch"){
      allowedModes = dmtcp::DmtcpWorker::COORD_BATCH;
      shift;
    }else if(s == "-i" || s == "--interval"){
      setenv(ENV_VAR_CKPT_INTR, argv[1], 1);
      shift; shift;
    }else if(argc>1 && (s == "-h" || s == "--host")){
      setenv(ENV_VAR_NAME_ADDR, argv[1], 1);
      shift; shift;
    }else if(argc>1 && (s == "-p" || s == "--port")){
      setenv(ENV_VAR_NAME_PORT, argv[1], 1);
      shift; shift;
    }else if(argc>1 && (s == "-c" || s == "--ckptdir")){
      setenv(ENV_VAR_CHECKPOINT_DIR, argv[1], 1);
      shift; shift;
    }else if(argc>1 && (s == "-t" || s == "--tmpdir")){
      setenv(ENV_VAR_TMPDIR, argv[1], 1);
      shift; shift;
    }else if(argc>1 && s == "--mtcp-checkpoint-signal"){
      setenv(ENV_VAR_SIGCKPT, argv[1], 1);
      shift; shift;
    }else if(s == "--checkpoint-open-files"){
      checkpointOpenFiles = true;
      shift;
    }else if(s == "-q" || s == "--quiet"){
      *getenv(ENV_VAR_QUIET) = *getenv(ENV_VAR_QUIET) + 1;
      // Just in case a non-standard version of setenv is being used:
      setenv(ENV_VAR_QUIET, getenv(ENV_VAR_QUIET), 1);
      shift;
    }else if( (s.length()>2 && s.substr(0,2)=="--") ||
              (s.length()>1 && s.substr(0,1)=="-" ) ) {
      JASSERT_STDERR << "Invalid Argument\n";
      JASSERT_STDERR << theUsage;
      return 1;
    }else if(argc>1 && s=="--"){
      shift;
      break;
    }else{
      break;
    }
  }

  dmtcp::UniquePid::setTmpDir(getenv(ENV_VAR_TMPDIR));

  jassert_quiet = *getenv(ENV_VAR_QUIET) - '0';

#ifdef FORKED_CHECKPOINTING
  /* When this is robust, add --forked-checkpointing option on command-line,
   * with #ifdef FORKED_CHECKPOINTING around the option, change default of
   * configure.ac, dmtcp/configure.ac, to enable, and change them
   * from enable-forked... to disable-...
   */
  setenv(ENV_VAR_FORKED_CKPT, "1", 1);
#endif

  if (jassert_quiet == 0)
    JASSERT_STDERR << theBanner;
    //printf("DMTCP/MTCP  Copyright (C) 2006-2008  Jason Ansel, Michael Rieker,\n"
           //"                                       Kapil Arya, and Gene Cooperman\n"
           //"This program comes with ABSOLUTELY NO WARRANTY.\n"
           //"This is free software, and you are welcome to redistribute it\n"
	   //"under certain conditions; see COPYING file for details.\n"
	   //"(Use flag \"-q\" to hide this message.)\n\n");

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
  // region for portion to be read back fom checkpoint image.
  // For CS_ZERO_PAGES region, mmap // on restart, but don't write in zeroes.
  // Also, after checkpointing segment, munmap zero pages, and mmap them again.
  // Don't try to find all pages.  The above strategy may increase
  // the non-zero-mapped mapped pages to no more than double the actual
  // non-zero region (assuming that the zero-mapped pages are contiguous).
  // - Gene

#ifdef __GNUC__
# if __GNUC__ == 4 && __GNUC_MINOR__ > 1
  if ( strcmp(argv[0], "matlab") == 0 )
    JASSERT_STDERR << theMatlabWarning;
//    printf(
//   "\n**** WARNING:  matlab release 7 uses older glibc.  Compile DMTCP/MTCP\n"
//   "****  with gcc-4.1 and g++-4.1\n"
//   "**** env CC=gcc-4.1 CXX=g++-4.1 ./configure\n"
//   "**** [ Also modify mtcp/Makefile to:  CC=gcc-4.1 ]\n"
//   "**** [ Next, you may need an alternative Java JVM (see QUICK-START) ]\n"
//   "**** [ Finally, run as:   dmtcp_checkpoint matlab -nodisplay ]\n"
//   "**** [   (DMTCP does not yet checkpoint X-Windows applications.) ]\n"
//   "**** [ You may see \"Not checkpointing libc-2.7.so\".  This is normal. ]\n"
//   "****   (Will try to execute anyway with current compiler version.)\n\n"
//   );
# endif
#endif

  if(autoStartCoordinator) dmtcp::DmtcpWorker::startCoordinatorIfNeeded(allowedModes);

  //Detect important paths
  dmtcp::string dmtcphjk = jalib::Filesystem::FindHelperUtility ( "dmtcphijack.so" );
  dmtcp::string searchDir = jalib::Filesystem::GetProgramDir();

  // Initialize JASSERT library here
  JASSERT_INIT( dmtcp::UniquePid::getTmpDir() );

  //setup CHECKPOINT_DIR
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

  dmtcp::string stderrDevice = jalib::Filesystem::ResolveSymlink ( _stderrProcPath() );

  //TODO:
  // When stderr is a pseudo terminal for IPC between parent/child processes,
  // this logic fails and JASSERT may write data to FD 2 (stderr)
  // this will cause problems in programs that use FD 2 (stderr) for algorithmic things...
  if ( stderrDevice.length() > 0
          && jalib::Filesystem::FileExists ( stderrDevice ) )
    setenv ( ENV_VAR_STDERR_PATH,stderrDevice.c_str(), 0 );
  else// if( isSSHSlave )
    setenv ( ENV_VAR_STDERR_PATH, "/dev/null", 0 );

  // If dmtcp_checkpoint was called with user LD_PRELOAD, and if
  //   if dmtcp_checkpoint survived the experience, then pass it back to user.
  if (getenv("LD_PRELOAD"))
    dmtcphjk = dmtcphjk + ":" + getenv("LD_PRELOAD");
  setenv ( "LD_PRELOAD", dmtcphjk.c_str(), 1 );
  setenv ( ENV_VAR_HIJACK_LIB, dmtcphjk.c_str(), 0 );
  setenv ( ENV_VAR_UTILITY_DIR, searchDir.c_str(), 0 );
  if ( getenv(ENV_VAR_SIGCKPT) != NULL )
    setenv ( "MTCP_SIGCKPT", getenv(ENV_VAR_SIGCKPT), 1);
  else
    unsetenv("MTCP_SIGCKPT");

  if ( checkpointOpenFiles )
    setenv( ENV_VAR_CKPT_OPEN_FILES, "1", 0 );
  else
    unsetenv( ENV_VAR_CKPT_OPEN_FILES);

#ifdef PID_VIRTUALIZATION
  setenv( ENV_VAR_ROOT_PROCESS, "1", 1 );
#endif

  //copy args into new structure
  //char** newArgs = new char* [argc];
  //memset ( newArgs, 0, sizeof ( char* ) *argc );
  //for ( int i=0; i<argc-startArg; ++i )
  //  newArgs[i] = argv[i+startArg];

  dmtcp::string magic_elf32 = "\177ELF\001"; // Magic number for ELF 32-bit
  char argv_buf[5];
  int fd = open(argv[0], O_RDONLY);
  if (fd == -1 || 5 != read(fd, argv_buf, 5)) {
    // If can't open or read file, "exec failed" message will handle it.
    // Once we know this is safe, delete this comment and exit after
    // JASSERT_STDERR, without continuing and trying the execvp anyway.
    // Consider also checking these things after call to exec(),
    //   perhaps in DmtcpWorker constructor.
    // FIXME:  This could have been a symbolic link.  Don't issue an error,
    //         unless we're sure that the executable is not readable.
    // JASSERT_STDERR <<
    //   "*** ERROR:  Executable to run w/ DMTCP appears not to be readable.\n\n";
  } else {
    bool is32bit = false;
    char * ld_preload = getenv("LD_PRELOAD");
    is32bit = (0 == memcmp(magic_elf32.c_str(), argv_buf, 5));
#if defined(__x86_64__) && !defined(CONFIG_M32)
    if (is32bit)
      JASSERT_STDERR <<
        "*** ERROR:  You appear to be checkpointing "
        << "a 32-bit target under 64-bit Linux.\n"
        << "***  If this fails, then please try re-configuring DMTCP:\n"
        << "***  configure --enable-m32 ; make clean ; make\n\n";
#endif
    dmtcp::string cmd = is32bit ? "/lib/ld-linux.so.2 --verify "
			        : "/lib64/ld-linux-x86-64.so.2 --verify " ;
    cmd = cmd + argv[0] + " > /dev/null";
    unsetenv ( "LD_PRELOAD" );
    if ( system(cmd.c_str()) )
      JASSERT_STDERR <<
        "*** ERROR:  You appear to be checkpointing "
        << "a statically linked target:\n"
	<< "***    " << argv[0] << " .\n"
        << "***  You can confirm this with the 'file' command.\n"
        << "***  The standard DMTCP only supports dynamically"
	<< " linked executables.\n"
	<< "*** If you cannot recompile dynamically, please talk to the"
	<< " developers about a\n"
	<< "*** custom DMTCP version for statically linked executables.\n"
        << "*** Proceeding for now, but this DMTCP will probably fail.\n\n";
    setenv ( "LD_PRELOAD", dmtcphjk.c_str(), 1 );
  }
  if (fd != -1)
    close (fd);

// FIXME:  Unify this code with code prior to execvp in execwrappers.cpp
//   Can use argument to dmtcpPrepareForExec() or getenv("DMTCP_...")
//   from DmtcpWorker constructor, to distinguish the two cases.
#ifdef __i386__
  // This is needed in 32-bit Ubuntu 9.10, to fix bug with test/dmtcp5.c
  // NOTE:  Setting personality() is cleanest way to force legacy_va_layout,
  //   but there's currently a bug on restart in the sequence:
  //   checkpoint -> restart -> checkpoint -> restart
# if 0
  { unsigned long oldPersonality = personality(0xffffffffL);
    if ( ! (oldPersonality & ADDR_COMPAT_LAYOUT) ) {
      // Force ADDR_COMPAT_LAYOUT for libs in high mem, to avoid vdso conflict
      personality(oldPersonality & ADDR_COMPAT_LAYOUT);
      JTRACE( "setting ADDR_COMPAT_LAYOUT" );
      setenv("DMTCP_ADDR_COMPAT_LAYOUT", "temporarily is set", 1);
    }
  }
# else
  { struct rlimit rlim;
    getrlimit(RLIMIT_STACK, &rlim);
    if (rlim.rlim_cur != RLIM_INFINITY) {
      char buf[100];
      sprintf(buf, "%lu", rlim.rlim_cur); // "%llu" for BSD/Mac OS
      JTRACE( "setting rlim_cur for RLIMIT_STACK" ) ( rlim.rlim_cur );
      setenv("DMTCP_RLIMIT_STACK", buf, 1);
      // Force kernel's internal compat_va_layout to 0; Force libs to high mem.
      rlim.rlim_cur = rlim.rlim_max;
      // FIXME: if rlim.rlim_cur != RLIM_INFINITY, then we should warn the user.
      setrlimit(RLIMIT_STACK, &rlim);
      // After exec, process will restore DMTCP_RLIMIT_STACK in DmtcpWorker()
    }
  }
# endif
#endif

  //run the user program
  execvp ( argv[0], argv );

  //should be unreachable
  JASSERT_STDERR <<
    "ERROR: Failed to exec(\"" << argv[0] << "\"): " << JASSERT_ERRNO << "\n"
    << "Perhaps it is not in your $PATH?\n"
    << "See `dmtcp_checkpoint --help` for usage.\n";
  //fprintf(stderr, theExecFailedMsg, argv[0], JASSERT_ERRNO);

  return -1;
}

