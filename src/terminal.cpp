#include <termios.h>
#include <signal.h>
#include <sys/ioctl.h>
#include "config.h"
#ifdef HAS_PR_SET_PTRACER
# include <sys/prctl.h>
#endif
#include "dmtcp.h"
#include "config.h"
#include "../jalib/jassert.h"

/*************************************************************************
 *
 *  Save and restore terminal settings.
 *
 *************************************************************************/
namespace dmtcp {

static int saved_termios_exists = 0;
static struct termios saved_termios;
static struct winsize win;

static void save_term_settings();
static void restore_term_settings();

static void save_term_settings()
{
  /* Drain stdin and stdout before checkpoint */
  tcdrain(STDOUT_FILENO);
  tcdrain(STDERR_FILENO);

  saved_termios_exists = ( isatty(STDIN_FILENO)
  		           && tcgetattr(STDIN_FILENO, &saved_termios) >= 0 );
  if (saved_termios_exists)
    ioctl (STDIN_FILENO, TIOCGWINSZ, (char *) &win);
}

static int safe_tcsetattr(int fd, int optional_actions,
                          const struct termios *termios_p)
{
  struct termios old_termios, new_termios;
  /* We will compare old and new, and we don't want uninitialized data */
  memset(&new_termios, 0, sizeof(new_termios));
  /* tcgetattr returns success as long as at least one of requested
   * changes was executed.  So, repeat until no more changes.
   */
  do {
    memcpy(&old_termios, &new_termios, sizeof(new_termios));
    if (tcsetattr(fd, TCSANOW, termios_p) == -1) return -1;
    if (tcgetattr(fd, &new_termios) == -1) return -1;
  } while (memcmp(&new_termios, &old_termios, sizeof(new_termios)) != 0);
  return 0;
}

// FIXME: Handle Virtual Pids
static void restore_term_settings()
{
  if (saved_termios_exists){
    /* First check if we are in foreground. If not, skip this and print
     *   warning.  If we try to call tcsetattr in background, we will hang up.
     */
    int foreground = (tcgetpgrp(STDIN_FILENO) == getpgrp());
    JTRACE("restore terminal attributes, check foreground status first")
      (foreground);
    if (foreground) {
      if ( ( ! isatty(STDIN_FILENO)
             || safe_tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios) == -1) )
        JWARNING(false) .Text("failed to restore terminal");
      else {
        struct winsize cur_win;
        JTRACE("restored terminal");
        ioctl (STDIN_FILENO, TIOCGWINSZ, (char *) &cur_win);
	/* ws_row/ws_col was probably not 0/0 prior to checkpoint.  We change
	 * it back to last known row/col prior to checkpoint, and then send a
	 * SIGWINCH (see below) to notify process that window might have changed
	 */
        if (cur_win.ws_row == 0 && cur_win.ws_col == 0)
          ioctl (STDIN_FILENO, TIOCSWINSZ, (char *) &win);
      }
    } else {
      JWARNING(false)
        .Text(":skip restore terminal step -- we are in BACKGROUND");
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

static void checkpoint()
{
  save_term_settings();
}

static void restart()
{
  restore_term_settings();
  /* If DMTCP_RESTART_PAUSE2 set, sleep 15 seconds to allow gdb attach.*/
  if (getenv("MTCP_RESTART_PAUSE2") || getenv("DMTCP_RESTART_PAUSE2")) {
#ifdef HAS_PR_SET_PTRACER
    prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0); //For: gdb attach
#endif
    struct timespec delay = {15, 0}; /* 15 seconds */
    printf("Pausing 15 seconds. Do:  gdb <PROGNAME> %d\n",
        dmtcp_virtual_to_real_pid(getpid()));
    nanosleep(&delay, NULL);
#ifdef HAS_PR_SET_PTRACER
    prctl(PR_SET_PTRACER, 0, 0, 0, 0); // Revert permission to default.
#endif
  }
}

static DmtcpBarrier terminalBarriers[] = {
  {DMTCP_PRIVATE_BARRIER_PRE_CKPT, checkpoint, "checkpoint"},
  {DMTCP_PRIVATE_BARRIER_RESTART, restart, "restart"}
};

static DmtcpPluginDescriptor_t terminalPlugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "terminal",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Terminal plugin",
  DMTCP_DECL_BARRIERS(terminalBarriers),
  NULL
};


DmtcpPluginDescriptor_t dmtcp_Terminal_PluginDescr()
{
  return terminalPlugin;
}

}
