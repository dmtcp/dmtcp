#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include "config.h"
#ifdef HAS_PR_SET_PTRACER
#include <sys/prctl.h>
#endif  // ifdef HAS_PR_SET_PTRACER
#include "../jalib/jassert.h"
#include "config.h"
#include "syscallwrappers.h"
#include "dmtcp.h"

/*************************************************************************
 *
 *  Save and restore terminal settings.
 *
 *************************************************************************/
namespace dmtcp
{
static int saved_termios_exists = 0;
static struct termios saved_termios;
static struct winsize win;
static bool pgrp_is_foreground = false;

static void save_term_settings();
static void restore_term_settings();

static void
save_term_settings()
{
  /* Drain stdin and stdout before checkpoint */
  tcdrain(STDOUT_FILENO);
  tcdrain(STDERR_FILENO);

  saved_termios_exists = (isatty(STDIN_FILENO)
                          && tcgetattr(STDIN_FILENO, &saved_termios) >= 0);
  if (saved_termios_exists) {
    ioctl(STDIN_FILENO, TIOCGWINSZ, (char *)&win);
    // NOTE:  There can be at most one foreground process for a terminal.
    //        Normally, the foreground process group leader is also pgrp leader.
    //        It can be in this DMTCP computation group, or outside of DMTCP.
    pgrp_is_foreground = (_real_tcgetpgrp(STDIN_FILENO) == _libc_getpgrp());
  }
}

static int
safe_tcsetattr(int fd, int optional_actions, const struct termios *termios_p)
{
  struct termios old_termios, new_termios;

  /* We will compare old and new, and we don't want uninitialized data */
  memset(&new_termios, 0, sizeof(new_termios));

  /* tcgetattr returns success as long as at least one of requested
   * changes was executed.  So, repeat until no more changes.
   */
  do {
    memcpy(&old_termios, &new_termios, sizeof(new_termios));
    if (tcsetattr(fd, TCSANOW, termios_p) == -1) {
      return -1;
    }
    if (tcgetattr(fd, &new_termios) == -1) {
      return -1;
    }
  } while (memcmp(&new_termios, &old_termios, sizeof(new_termios)) != 0);
  return 0;
}

static int
get_parent_pid(int pid)
{
  char buf[8192];
  // /proc/PID/stat is:  PID (COMMAND) STATE PPID ...
  snprintf(buf, sizeof(buf), "/proc/%d/stat", pid);
  FILE* fp = fopen(buf, "r");
  JASSERT(fp != NULL)(pid)(JASSERT_ERRNO).Text("fopen");
  int rc = -1;
  if (fp) {
    size_t size = fread(buf, 1, sizeof(buf), fp);
    if (size > 0 && strstr(buf, ") ")) {
      rc = strtol(strstr(strstr(buf, ") ")+2, " "), NULL, 10);
    }
  }
  JASSERT(rc != -1)(pid).Text("parent pid not found in /proc/*/stat");
  return rc;
}

// See:  info libc -> Job Control -> Implementing a Shell ->
//                                               Foreground and Background
static void
restore_term_settings()
{
  if (saved_termios_exists) {
    /* First check if we are in foreground. If not, skip this and print
     *   warning.  If we try to call tcsetattr in background, we will hang up.
     * In some shells, the shell itself will be a pgrp that includes is
     *   first child process, instead of the first child forming a pgrp.
     */
    JTRACE("restore terminal attributes, check foreground status first")
          (pgrp_is_foreground)
          (_libc_getpgrp()) (_real_tcgetpgrp(STDIN_FILENO))
          (get_parent_pid(_real_tcgetpgrp(STDIN_FILENO)));
    if (pgrp_is_foreground && isatty(STDIN_FILENO)) {
      // Set login shell (not under DMTCP control) to be the foreground process,
      // instead of this process.
      JASSERT(tcsetpgrp(STDIN_FILENO, getpgrp()) == 0)(JASSERT_ERRNO);
      JTRACE("The process group ID of this process is the\n"
             "  the process group ID for the foreground process group.\n"
             "Make current process group the new foreground process group.");
      if (safe_tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios) != -1) {
        struct winsize cur_win;
        JTRACE("restored terminal");
        ioctl(STDIN_FILENO, TIOCGWINSZ, (char *)&cur_win);

        /* ws_row/ws_col was probably not 0/0 prior to checkpoint.  We change
         * it back to last known row/col prior to checkpoint, and then send a
         * SIGWINCH (see below) to notify process that window might have changed
         */
        if (cur_win.ws_row == 0 && cur_win.ws_col == 0) {
          ioctl(STDIN_FILENO, TIOCSWINSZ, (char *)&win);
        }
      } else {
        JWARNING(false).Text("failed to restore terminal attributes");
      }
    } else if (pgrp_is_foreground) {
      JWARNING(false).Text("failed to restore terminal. STDIN not a tty");
    } else {
      JWARNING(false)
      .Text("skip restore terminal step -- we are in BACKGROUND");
    }
  }

  /*
   * NOTE:
   * Apache, when running in debug mode (-X), uses SIGWINCH
   * as a signal for stopping gracefully. Please comment out
   * the next line to prevent DMTCP from sending a SIGWINCH
   * on restart when testing with Apache.
   *
   * TODO:
   * This should be done automatically by wrapping it in an ifdef
   * or if condition that disables the SIGWINCH using configure or
   * a runtime option (--no-sigwinch).
   */
  if (kill(getpid(), SIGWINCH) == -1) {}  /* No remedy if error */
}

static void
terminal_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
  case DMTCP_EVENT_PRECHECKPOINT:
    save_term_settings();
    break;

  case DMTCP_EVENT_RESTART:
    restore_term_settings();
    break;

  default:  // other events are not registered
    break;
  }
}

static DmtcpPluginDescriptor_t terminalPlugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "terminal",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Terminal plugin",
  terminal_EventHook
};


DmtcpPluginDescriptor_t
dmtcp_Terminal_PluginDescr()
{
  return terminalPlugin;
}
}
