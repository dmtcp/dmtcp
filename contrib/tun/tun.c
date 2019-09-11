/* NOTE: This file contains the dmtcp plugin to handle the checkpoint restart
 * of a process that uses tap/tun driver. This can correctly handle a process
 * which creates just one connection to a tap/tun interface.
 */

#include <linux/version.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/if_tun.h>
#include <net/if.h>

#include <sys/ioctl.h>

#include <fcntl.h>
#include "config.h"
#include "dmtcp.h"

#define DEBUG_SIGNATURE "DEBUG [TUN Plugin]: "

/* Enable this for debugging
 * #define TUN_PLUGIN_DEBUG
 */

#ifdef TUN_PLUGIN_DEBUG
# define DPRINTF(fmt, ...) \
  do { fprintf(stderr, DEBUG_SIGNATURE fmt, ## __VA_ARGS__); } while (0)
#else /* ifdef TUN_PLUGIN_DEBUG */
# define DPRINTF(fmt, ...) \
  do {} while (0)
#endif /* ifdef TUN_PLUGIN_DEBUG */

#define NUM_TUN_REQUEST_TYPES   14
#define MAX_ERROR_STRING_LENGTH 50

/* NOTE: This size is very specific to QEMU; need to find a more generic way */
#define MAX_BUF_SIZE            (4096 + 65536)
#define MAX_NUM_OF_READS        50
#define TUN_PLUGIN_COOKIE_STR   "{{<<TTUUNN10"

/*============================================================================*/
/*============================= START GLOBAL DATA ============================*/
/*============================================================================*/


static int g_tun_fd = -1; /* Stores the fd to the last opened tap/tun fd */
static struct ifreq g_ifreq;
static struct tun_filter g_tun_filter;
static int g_sndbuf;
static int g_vnet_hdr_sz;
static struct ifreq g_queue;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)
static struct sock_fprog g_sock_fprog;
#endif /* if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */

/* Stores a request type and the corresponding argument */
struct ioctl_request {
  int request;
  void *arg;
};

typedef struct ioctl_request ioctl_request;

/* This table stores the ioctl calls, and the corresponding arguments */
static ioctl_request g_request_table[NUM_TUN_REQUEST_TYPES];

/* Index of the last ioctl request stored on the g_request_table */
static int g_last_req_idx = -1;

/* Table of set request types on tap/tun fd */
static char *request_names[NUM_TUN_REQUEST_TYPES] = { "TUNSETNOCSUM", /*
                                                                         Unimplemented
                                                                         as of
                                                                         kernel
                                                                         ver.
                                                                         3.8 */
                                                      "TUNSETDEBUG",
                                                      "TUNSETIFF",
                                                      "TUNSETPERSIST",
                                                      "TUNSETOWNER",
                                                      "TUNSETLINK",
                                                      "TUNSETGROUP",
                                                      "TUNSETOFFLOAD",
                                                      "TUNSETTXFILTER", /* Can
                                                                           only
                                                                           be
                                                                           set
                                                                           for
                                                                           TAP
                                                                           */
                                                      "TUNSETSNDBUF",
                                                      "TUNATTACHFILTER", /* Can
                                                                            only
                                                                            be
                                                                            set
                                                                            for
                                                                            TAP
                                                                            */
                                                      "TUNSETVNETHDRSZ",
                                                      "TUNSETQUEUE" };

static char g_drained_data[MAX_BUF_SIZE];
static int g_tunfd_flags;
static int g_bytes_read = 0;

/*============================================================================*/
/*============================= END GLOBAL DATA ==============================*/
/*============================================================================*/

/*============================================================================*/
/*========================= START PRIVATE FUNCTIONS ==========================*/
/*============================================================================*/

/* Returns the index into the request_names array for an ioctl() on g_tun_fd
 *  Useful for debugging.
 */
static int
get_request_name_idx(int request)
{
  int idx = -1;

  switch (request) {
  case TUNSETNOCSUM:
    idx = 0; break;
  case TUNSETDEBUG:
    idx = 1; break;
  case TUNSETIFF:
    idx = 2; break;
  case TUNSETPERSIST:
    idx = 3; break;
  case TUNSETOWNER:
    idx = 4; break;
  case TUNSETLINK:
    idx = 5; break;
  case TUNSETGROUP:
    idx = 6; break;
  case TUNSETOFFLOAD:
    idx = 7; break;
  case TUNSETTXFILTER:
    idx = 8; break;
  case TUNSETSNDBUF:
    idx = 9; break;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)
  case TUNATTACHFILTER:
    idx = 10; break;
  case TUNSETVNETHDRSZ:
    idx = 11; break;
  case TUNSETQUEUE:
    idx = 12; break;
#endif /* if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */
  default:
    break;
  }
  return idx;
}

static void
inc_last_req_idx()
{
  g_last_req_idx += 1;
}

static void
dec_last_req_idx()
{
  g_last_req_idx -= 1;
}

/* Returns 1 if the failure of an ioctl for a request type is a fatal */
static int
is_fatal(int request)
{
  int fatal = 0;

  /* TODO: Determine all fatal errors */
  switch (request) {
  case TUNSETNOCSUM:
    fatal = 0; break;
  case TUNSETDEBUG:
    fatal = 0; break;
  case TUNSETIFF:
    fatal = 1; break;
  case TUNSETPERSIST:
    fatal = 0; break;
  case TUNSETOWNER:
    fatal = 0; break;
  case TUNSETLINK:
    fatal = 0; break;
  case TUNSETGROUP:
    fatal = 0; break;
  case TUNSETOFFLOAD:
    fatal = 0; break;
  case TUNSETTXFILTER:
    fatal = 0; break;
  case TUNSETSNDBUF:
    fatal = 0; break;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)
  case TUNATTACHFILTER:
    fatal = 0; break;
  case TUNSETVNETHDRSZ:
    fatal = 0; break;
  case TUNSETQUEUE:
    fatal = 0; break;
#endif /* if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */
  default:
    break;
  }
  return fatal;
}

static void *
get_arg(int request, void *arg)
{
  void *p_arg = NULL;

  switch (request) {
  case TUNSETNOCSUM:
    break;   /* Disable/enable checksum: unimplemented in kernel ver. 3.8 */
  case TUNSETDEBUG:
    p_arg = arg; break;   /* type: int. Save the debug level */
  case TUNSETIFF:
    p_arg = memcpy(&g_ifreq, arg, sizeof(struct ifreq));   /* type: struct ifreq
                                                              */
    break;
  case TUNSETPERSIST:
    p_arg = arg; break;   /* type: ??. Save the persistence param */
  case TUNSETOWNER:
    p_arg = arg; break;   /* type: int. Save the uid of the owner */
  case TUNSETLINK:
    p_arg = arg; break;   /* type: int. Setting of link type can only be done
                             when the if is down */
  case TUNSETGROUP:
    p_arg = arg; break;   /* type: int. Save the gid of the owner */
  case TUNSETOFFLOAD:
    p_arg = arg; break;   /* type: int. Save the offload param */
  case TUNSETTXFILTER:
    p_arg = memcpy(&g_tun_filter, arg, sizeof(struct tun_filter));   /* type:
                                                                        struct
                                                                        tun_filter
                                                                        */
    break;
  case TUNSETSNDBUF:
    p_arg = memcpy(&g_sndbuf, arg, sizeof(g_sndbuf));
    break;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)
  case TUNATTACHFILTER:
    p_arg = memcpy(&g_sock_fprog, arg, sizeof(struct sock_fprog));   /* type:
                                                                        struct
                                                                        sock_fprog
                                                                        */
    break;
  case TUNSETVNETHDRSZ:
    p_arg = memcpy(&g_vnet_hdr_sz, arg, sizeof(g_vnet_hdr_sz));
    break;
  case TUNSETQUEUE:
    p_arg = memcpy(&g_queue, arg, sizeof(struct ifreq));   /* type: struct ifreq
                                                              */
    break;
#endif /* if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */
  default:
    break;
  }
  return p_arg;
}

static int
set_non_blocking(int fd)
{
  int flags;

  if (-1 == (flags = fcntl(fd, F_GETFL, 0))) {
    flags = 0;
  }
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int
get_flags(int fd)
{
  return fcntl(fd, F_GETFL);
}

static int
set_flags(int fd, int flags)
{
  return fcntl(fd, F_SETFL, flags);
}

/*============================================================================*/
/*=========================== END PRIVATE FUNCTIONS ==========================*/
/*============================================================================*/

/*============================================================================*/
/*========================== START WRAPPER FUNCTIONS =========================*/
/*============================================================================*/

/* This is the wrapper for open()
 *  Used for capturing the fd to a tap/tun interface
 */
int
open64(const char *pathname, int flags, ...)
{
  va_list argp;
  static int (*next_fnc)() = NULL; /* Same type signature as open */
  mode_t mode = 0;
  int result;

  if (flags & O_CREAT) {
    va_list arg;
    va_start(arg, flags);
    mode = va_arg(arg, int);
    va_end(arg);
  }

  result = NEXT_FNC(open)(pathname, flags, mode);

  /* Check if the process is opening a connection to the tun driver */
  if (!strncmp(pathname, "/dev/net/tun", 13)) {
    /* Save the file descriptor if it is the tun driver
     * NOTE: This will overwrite the last saved fd (if any).
     */
    g_tun_fd = result;
    DPRINTF("[%s:%d]: PARAMS: pathname: %s, flags:%d; Result: %d\n",
            __FUNCTION__, __LINE__, pathname, flags, g_tun_fd);
  }
  return result;
}

/* TODO: Fix this duplicate */
int
open(const char *pathname, int flags, ...)
{
  va_list argp;
  static int (*next_fnc)() = NULL; /* Same type signature as open */
  mode_t mode = 0;
  int result;

  if (flags & O_CREAT) {
    va_list arg;
    va_start(arg, flags);
    mode = va_arg(arg, int);
    va_end(arg);
  }

  result = NEXT_FNC(open)(pathname, flags, mode);

  /* Check if the process is opening a connection to the tun driver */
  if (!strncmp(pathname, "/dev/net/tun", 13)) {
    /* Save the file descriptor if it is the tun driver
     * NOTE: This will overwrite the last saved fd (if any).
     */
    g_tun_fd = result;
    DPRINTF("[%s:%d]: PARAMS: pathname: %s, flags:%d; Result: %d\n",
            __FUNCTION__, __LINE__, pathname, flags, g_tun_fd);
  }
  return result;
}

/* This is the wrapper for ioctl()
 *  Used for saving the requests and the corresponding arguments
 *  Assumption: QEMU only makes three argument ioctl() calls
 */

// int ioctl(int fd, unsigned long int request, void* arg)
int
ioctl(int fd, unsigned long int request, ...)
{
  va_list argp;
  static int (*next_fnc)() = NULL; /* Same type signature as ioctl */
  int idx = -1;
  char *request_name = "NULL";
  int result;
  void *arg;

  va_start(argp, request);
  arg = va_arg(argp, void *);
  va_end(argp);
  result = NEXT_FNC(ioctl)(fd, request, arg);

  /* Check if this is the saved tun fd */
  if (fd == g_tun_fd) {
    idx = get_request_name_idx(request);
    request_name = (idx != -1) ? request_names[idx] : "UNKNOWN";

    /* Capture arguments of ioctl() */
    DPRINTF("[%s:%d]: PARAMS: fd: %d, request:%s, arg:%p; Result: %d\n",
            __FUNCTION__, __LINE__, fd, request_name, arg, result);
    inc_last_req_idx();
    g_request_table[g_last_req_idx].request = request;
    g_request_table[g_last_req_idx].arg = get_arg(request, arg);
  }
  return result;
}

/*============================================================================*/
/*=========================== END WRAPPER FUNCTIONS ==========================*/
/*============================================================================*/

/*
 * On ckpt:
 *  - Flush a cookie down the tap/tun interface
 *  - Do a timed wait (of MAX_READ_WAIT_TIME milliseconds) for any incoming data
 *  - Store the data, if any,
 * On resume:
 *  - Do nothing
 * On restart:
 *  - Assumption: The fd to tap/tun interface has been created by the file plugin
 *  - Replay the sequence of ioctl's
 *  - Write the saved data, if any, back to the tun fd
 */
static void
pre_ckpt()
{
  int i, request, idx, ret, flags, count;
  char *request_name;
  char error_string[MAX_ERROR_STRING_LENGTH];
  void *arg;

  DPRINTF("\n*** The plugin is being called before checkpointing. ***\n");

  /* TODO: flush(g_tun_fd)?? */
  fsync(g_tun_fd);

  /* Try to drain the data from the tap/tun fd; later, we'll put the data
   * back "on the wire".
   */

  /* But first, save the flags, and make the reads non-blocking */
  g_tunfd_flags = get_flags(g_tun_fd);
  DPRINTF("Setting tunfd to non-blocking\n");
  ret = set_non_blocking(g_tun_fd);
  count = 0;

  DPRINTF("Draining the tunfd(%d)\n", g_tun_fd);

  /* NOTE: This is a heuristic, should work for now. */
  while (count < MAX_NUM_OF_READS) {
    ret = read(g_tun_fd, (g_drained_data + g_bytes_read),
               (MAX_BUF_SIZE - g_bytes_read));
    if (ret > 0) {
      g_bytes_read += ret;
    }
    count += 1;
  }
  DPRINTF("Read %d bytes from the tunfd(%d)\n", g_bytes_read, g_tun_fd);

  /* Restore the flags */
  ret = set_flags(g_tun_fd, g_tunfd_flags);
}

static void
refill_tun_fd()
{
  if (g_bytes_read > 0) {
    /* It's time to put the captured data back on the wire */
    DPRINTF("Writing %d bytes back to the tunfd(%d)\n", g_bytes_read, g_tun_fd);

    /* Again, first save the flags, and make the write non-blocking */
    g_tunfd_flags = get_flags(g_tun_fd);
    int ret = set_non_blocking(g_tun_fd);

    if ((ret = write(g_tun_fd, g_drained_data, g_bytes_read)) < 0) {
      perror("ERROR: Unable to put the capture data back on the wire.");
    }

    /* Restore the flags */
    ret = set_flags(g_tun_fd, g_tunfd_flags);
  }
}

static void
resume()
{
  DPRINTF("The process is now resuming after checkpoint.\n");
  refill_tun_fd();
}

static void
restart()
{
  int i, request, idx, ret, flags, count;
  char *request_name;
  char error_string[MAX_ERROR_STRING_LENGTH];
  void *arg;

  DPRINTF("The plugin is now restarting from checkpointing.\n");
  if (g_tun_fd != -1) {
    /* Replay the sequence of ioctls */
    DPRINTF("Replaying %d ioctls on tunfd(%d)\n", g_last_req_idx + 1, g_tun_fd);
    for (i = 0; i <= g_last_req_idx; i++) {
      request = g_request_table[i].request;
      idx = get_request_name_idx(request);
      request_name = (idx != -1) ? request_names[idx] : "UNKNOWN";
      DPRINTF("REQUEST #%d: %s\n", i, request_name);
      arg = g_request_table[i].arg;
      ret = NEXT_FNC(ioctl)(g_tun_fd, request, arg);
      if (ret < 0) {
        snprintf(error_string, MAX_ERROR_STRING_LENGTH,
                 "ERROR: ioctl(%s)\n", request_name);
        perror(error_string);
        if (is_fatal(request)) {
          DPRINTF("FATAL ERROR: Cannot continue!\n");
          exit(-1);
        }
      }
    }
  } else {
    DPRINTF("ERROR: Cannot restore tap/tun connection. g_tun_fd is -1.\n");
  }

  refill_tun_fd();
}

static void
tun_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  int i, request, idx, ret, flags, count;
  char *request_name;
  char error_string[MAX_ERROR_STRING_LENGTH];
  void *arg;

  /* NOTE:  See warning in plugin/README about calls to printf here. */
  switch (event) {
  case DMTCP_EVENT_INIT:
  {
    DPRINTF("The plugin containing %s has been initialized.\n", __FILE__);
    break;
  }
  case DMTCP_EVENT_EXIT:
    DPRINTF("The plugin is being called before exiting.\n");
    break;

  case DMTCP_EVENT_PRECHECKPOINT:
    pre_ckpt()
    break;

  case DMTCP_EVENT_RESUME:
    resume();
    break;

  case DMTCP_EVENT_RESTART:
    restart();
    break;

  default:
    break;
  }
}

DmtcpPluginDescriptor_t tun_plugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "tun",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "TUN plugin",
  tun_event_hook
};

DMTCP_DECL_PLUGIN(tun_plugin);
