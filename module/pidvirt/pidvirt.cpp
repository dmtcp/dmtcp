#include <sys/types.h>
#include <dlfcn.h>
#include "jalloc.h"
#include "jassert.h"
#include "jconvert.h"
#include "jfilesystem.h"
#include "pidwrappers.h"
#include "virtualpidtable.h"
#include "dmtcpmodule.h"

using namespace dmtcp;
//to allow linking without ptrace module
extern "C" int __attribute__ ((weak)) mtcp_is_ptracing() { return FALSE; }

static bool pthread_atfork_initialized = false;

static void pidVirt_pthread_atfork_child()
{
  dmtcpResetPidPpid();
  dmtcpResetTid(getpid());
  dmtcp::VirtualPidTable::instance().resetOnFork();
}

void __attribute__((constructor)) _initialize_pthread_atfork()
{
  if (!pthread_atfork_initialized) {
    pthread_atfork(NULL, NULL, pidVirt_pthread_atfork_child);
    pthread_atfork_initialized = true;
  }
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

void pidVirt_Init(void *data)
{
//   if ( getenv( ENV_VAR_ROOT_PROCESS ) != NULL ) {
//     JTRACE("Root of processes tree");
//     dmtcp::VirtualPidTable::instance().setRootOfProcessTree();
//     _dmtcp_unsetenv(ENV_VAR_ROOT_PROCESS);
//   }
  //pthread_atfork(NULL, pidVirt_pthread_atfork_parent, pidVirt_pthread_atfork_child);
}

dmtcp::string pidVirt_PidTableFilename()
{
  static int count = 0;
  dmtcp::ostringstream os;

  os << dmtcp_get_tmpdir() << "/dmtcpPidTable." << dmtcp_get_uniquepid_str()
     << '_' << jalib::XToString ( count++ );
  return os.str();
}

void pidVirt_ResetOnFork(void *data)
{
  dmtcp::VirtualPidTable::instance().resetOnFork();
}

void pidVirt_PrepareForExec(void *data)
{
  jalib::JBinarySerializeWriter *wr = (jalib::JBinarySerializeWriter*) data;
  dmtcp::VirtualPidTable::instance().serialize ( *wr );
}

void pidVirt_PostExec(void *data)
{
  jalib::JBinarySerializeReader *rd = (jalib::JBinarySerializeReader*) data;
  dmtcp::VirtualPidTable::instance().serialize ( *rd );
  dmtcp::VirtualPidTable::instance().refresh();
}

void pidVirt_PostRestart(void *data)
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

  dmtcp::VirtualPidTable::instance().postRestart();
}

void pidVirt_PostRestartRefill(void *data)
{
  dmtcp::VirtualPidTable::instance().writePidMapsToFile();
}

void pidVirt_PostRestartResume(void *data)
{
  dmtcp::VirtualPidTable::instance().readPidMapsFromFile();
}

void pidVirt_ThreadExit(void *data)
{
  /* This thread has finished its execution, do some cleanup on our part.
   *  erasing the original_tid entry from virtualpidtable
   *  FIXME: What if the process gets checkpointed after erase() but before the
   *  thread actually exits?
   */
  pid_t tid = gettid();
  dmtcp::VirtualPidTable::instance().erase(tid);
}

extern "C" void dmtcp_process_event(DmtcpEvent_t event, void* data)
{
  switch (event) {
    case DMTCP_EVENT_INIT:
    case DMTCP_EVENT_WAIT_FOR_SUSPEND_MSG:
    case DMTCP_EVENT_GOT_SUSPEND_MSG:
    case DMTCP_EVENT_START_PRE_CKPT_CB:
    case DMTCP_EVENT_CKPT_THREAD_START:
    case DMTCP_EVENT_PRE_SUSPEND_USER_THREAD:
    case DMTCP_EVENT_RESUME_USER_THREAD:
    case DMTCP_EVENT_SEND_STOP_SIGNAL:
    case DMTCP_EVENT_WRITE_CKPT_PREFIX:
    case DMTCP_EVENT_PRE_EXIT:
    case DMTCP_EVENT_PRE_CKPT:
    case DMTCP_EVENT_POST_LEADER_ELECTION:
    case DMTCP_EVENT_POST_DRAIN:
    case DMTCP_EVENT_POST_CKPT:
    case DMTCP_EVENT_POST_CKPT_RESUME:
      break;

    case DMTCP_EVENT_RESET_ON_FORK:
      pidVirt_ResetOnFork(data);
      break;

    case DMTCP_EVENT_PREPARE_FOR_EXEC:
      pidVirt_PrepareForExec(data);
      break;

    case DMTCP_EVENT_POST_EXEC:
      pidVirt_PostExec(data);
      break;

    case DMTCP_EVENT_POST_RESTART:
      pidVirt_PostRestart(data);
      break;

    case DMTCP_EVENT_POST_RESTART_REFILL:
      pidVirt_PostRestartRefill(data);
      break;

    case DMTCP_EVENT_POST_RESTART_RESUME:
      pidVirt_PostRestartResume(data);
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
