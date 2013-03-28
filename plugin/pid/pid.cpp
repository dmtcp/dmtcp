#include <sys/types.h>
#include "jalloc.h"
#include "jassert.h"
#include "jconvert.h"
#include "jfilesystem.h"
#include "pidwrappers.h"
#include "virtualpidtable.h"
#include "dmtcpplugin.h"
#include "protectedfds.h"

using namespace dmtcp;

extern "C" pid_t dmtcp_update_ppid();
extern "C"
pid_t dmtcp_real_to_virtual_pid(pid_t realPid)
{
  return REAL_TO_VIRTUAL_PID(realPid);
}

extern "C"
pid_t dmtcp_virtual_to_real_pid(pid_t virtualPid)
{
  return VIRTUAL_TO_REAL_PID(virtualPid);
}

//static void pidVirt_pthread_atfork_parent()
//{
//  dmtcp::VirtualPidTable::instance().insert(child_pid, child);
//}
//
//static void pidVirt_pthread_atfork_child()
//{
//  pidVirt_resetOnFork
//}

void pidVirt_Init(DmtcpEventData_t *data)
{
//   if ( getenv( ENV_VAR_ROOT_PROCESS ) != NULL ) {
//     JTRACE("Root of processes tree");
//     dmtcp::VirtualPidTable::instance().setRootOfProcessTree();
//     _dmtcp_unsetenv(ENV_VAR_ROOT_PROCESS);
//   }
  //pthread_atfork(NULL, pidVirt_pthread_atfork_parent, pidVirt_pthread_atfork_child);
}

void pidVirt_ResetOnFork(DmtcpEventData_t *data)
{
  dmtcp::VirtualPidTable::instance().resetOnFork();
}

void pidVirt_PrepareForExec(DmtcpEventData_t *data)
{
  JASSERT(data != NULL);
  jalib::JBinarySerializeWriterRaw wr ("", data->serializerInfo.fd);
  dmtcp::VirtualPidTable::instance().serialize(wr);
}

void pidVirt_PostExec(DmtcpEventData_t *data)
{
  JASSERT(data != NULL);
  jalib::JBinarySerializeReaderRaw rd ("", data->serializerInfo.fd);
  dmtcp::VirtualPidTable::instance().serialize(rd);
  dmtcp::VirtualPidTable::instance().refresh();
}

static int openSharedFile(dmtcp::string name, int flags)
{
  int fd;
  // try to create, truncate & open file
  if ((fd = _real_open(name.c_str(), O_EXCL|O_CREAT|O_TRUNC | flags, 0600)) >= 0) {
    return fd;
  }
  if (fd < 0 && errno == EEXIST) {
    errno = 0;
    if ((fd = _real_open(name.c_str(), flags, 0600)) >= 0) {
      return fd;
    }
  }
  // unable to create & open OR open
  JASSERT(false)(name)(strerror(errno)).Text("Cannot open file");
  return -1;
}

static void openOriginalToCurrentMappingFiles()
{
  int fd;
  dmtcp::ostringstream pidMapFile;
  pidMapFile << dmtcp_get_tmpdir() << "/dmtcpPidMap."
             << dmtcp_get_computation_id_str() << "."
             << std::hex << dmtcp_get_coordinator_timestamp();
  // Open and create pidMapFile if it doesn't exist.
  JTRACE("Open dmtcpPidMapFile")(pidMapFile.str());
  if (!dmtcp::Util::isValidFd(PROTECTED_PIDMAP_FD)) {
    fd = openSharedFile(pidMapFile.str(), O_RDWR);
    JASSERT (fd != -1);
    JASSERT (dup2 (fd, PROTECTED_PIDMAP_FD) == PROTECTED_PIDMAP_FD)
      (pidMapFile.str());
    close (fd);
  }
}

void pidVirt_PostRestart(DmtcpEventData_t *data)
{
  if ( jalib::Filesystem::GetProgramName() == "screen" )
    send_sigwinch = 1;
  // With hardstatus (bottom status line), screen process has diff. size window
  // Must send SIGWINCH to adjust it.
  // MTCP will send SIGWINCH to process on restart.  This will force 'screen'
  // to execute ioctl wrapper.  The wrapper will report a changed winsize,
  // so that 'screen' must re-initialize the screen (scrolling regions, etc.).
  // The wrapper will also send a second SIGWINCH.  Then 'screen' will
  // call ioctl and get the correct window size and resize again.
  // We can't just send two SIGWINCH's now, since window size has not
  // changed yet, and 'screen' will assume that there's nothing to do.

  dmtcp_update_ppid();
  openOriginalToCurrentMappingFiles();
  dmtcp::VirtualPidTable::instance().writeMapsToFile(PROTECTED_PIDMAP_FD);
}

void pidVirt_PostRestartRefill(DmtcpEventData_t *data)
{
  dmtcp::VirtualPidTable::instance().readMapsFromFile(PROTECTED_PIDMAP_FD);
  dmtcp_close_protected_fd(PROTECTED_PIDMAP_FD);
}

void pidVirt_ThreadExit(DmtcpEventData_t *data)
{
  /* This thread has finished its execution, do some cleanup on our part.
   *  erasing the original_tid entry from virtualpidtable
   *  FIXME: What if the process gets checkpointed after erase() but before the
   *  thread actually exits?
   */
  pid_t tid = gettid();
  dmtcp::VirtualPidTable::instance().erase(tid);
}

extern "C" void dmtcp_process_event(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
    case DMTCP_EVENT_RESET_ON_FORK:
      pidVirt_ResetOnFork(data);
      break;

    case DMTCP_EVENT_PRE_EXEC:
      pidVirt_PrepareForExec(data);
      break;

    case DMTCP_EVENT_POST_EXEC:
      pidVirt_PostExec(data);
      break;

    case DMTCP_EVENT_RESTART:
      pidVirt_PostRestart(data);
      break;

    case DMTCP_EVENT_REFILL:
      if (data->refillInfo.isRestart) {
        pidVirt_PostRestartRefill(data);
      }
      break;

    case DMTCP_EVENT_PTHREAD_RETURN:
    case DMTCP_EVENT_PTHREAD_EXIT:
      pidVirt_ThreadExit(data);
      break;

    default:
      break;
  }

  NEXT_DMTCP_PROCESS_EVENT(event, data);
  return;
}
