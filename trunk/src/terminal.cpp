#include <termios.h>
#include <signal.h>
#include <sys/ioctl.h>
#include "dmtcp.h"
#include "../jalib/jassert.h"

/*************************************************************************
 *
 *  Save and restore terminal settings.
 *
 *************************************************************************/

static int saved_termios_exists = 0;
static struct termios saved_termios;
static struct winsize win;

static void save_term_settings();
static void restore_term_settings();

void dmtcp_Terminal_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
    case DMTCP_EVENT_THREADS_SUSPEND:
      save_term_settings();
      break;

    case DMTCP_EVENT_THREADS_RESUME:
      if (data->resumeInfo.isRestart) {
        restore_term_settings();
      }
      break;

    default:
      break;
  }
}

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
  if (kill(getpid(), SIGWINCH) == -1) {}  /* No remedy if error */
}

