/* On some machines (e.g. SLES 10), readlink has conflicting return types
 * (ssize_t and int).
 *     In general, we rename the functions below, since any type declarations
 * may vary on different systems, and so we ignore these type declarations.
*/
#define open open_always_inline
#define open64 open64_always_inline
#define openat openat_always_inline
#define openat64 openat64_always_inline
#define readlink readlink_always_inline
#define __readlink_chk _ret__readlink_chk
#define realpath realpath_always_inline

#include <dirent.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <cstring>
#include <cstdlib>

#undef open
#undef open64
#undef openat
#undef openat64
#undef readlink
#undef __readlink_chk
#undef realpath

#include "config.h"
#include "dmtcp.h"
#include "jassert.h"
#include "pathvirt.h"

#define ENV_ORIG_DPP       "DMTCP_ORIGINAL_PATH_PREFIX"
#define ENV_NEW_DPP        "DMTCP_NEW_PATH_PREFIX"
#define MAX_ENV_VAR_SIZE   10*1024

#define VIRTUAL_TO_PHYSICAL_PATH(virt) virtual_to_physical_path(virt)

#define _real_open       NEXT_FNC(open)
#define _real_open64     NEXT_FNC(open64)
#define _real_fopen      NEXT_FNC(fopen)
#define _real_fopen64    NEXT_FNC(fopen64)
#define _real_freopen    NEXT_FNC(freopen)
#define _real_openat     NEXT_FNC(openat)
#define _real_openat64   NEXT_FNC(openat64)
#define _real_opendir    NEXT_FNC(opendir)
#define _real_xstat      NEXT_FNC(__xstat)
#define _real_xstat64    NEXT_FNC(__xstat64)
#define _real_lxstat     NEXT_FNC(__lxstat)
#define _real_lxstat64   NEXT_FNC(__lxstat64)
#define _real_readlink   NEXT_FNC(readlink)
#define _real_realpath   NEXT_FNC(realpath)
#define _real_access     NEXT_FNC(access)
#define _real_truncate   NEXT_FNC(truncate)
#define _real_rename     NEXT_FNC(rename)
#define _real_mkdir      NEXT_FNC(mkdir)
#define _real_chmod      NEXT_FNC(chmod)
#define _real_unlink     NEXT_FNC(unlink)
#define _real_chdir      NEXT_FNC(chdir)
#define _real_remove     NEXT_FNC(remove)
#define _real_rmdir      NEXT_FNC(rmdir)
#define _real_link       NEXT_FNC(link)
#define _real_symlink    NEXT_FNC(symlink)
#define _real_pathconf   NEXT_FNC(pathconf)
#define _real_statfs     NEXT_FNC(statfs)

/* paths should only be swapped on restarts (not on initial run), so this flag
   is set on restart */
static int shouldSwap;

/* NOTE: DMTCP_PATH_PREFIX env variables cannot exceed MAX_ENV_VAR_SIZE
   characters in length */
static char oldPathPrefixList[MAX_ENV_VAR_SIZE];
static char tmpOldPathPrefixList[MAX_ENV_VAR_SIZE];
static char newPathPrefixList[MAX_ENV_VAR_SIZE];

static bool tmpBufferModified = false;
static pthread_rwlock_t  listRwLock;

static dmtcp::string
virtual_to_physical_path(const char *virt_path);

EXTERNC int dmtcp_pathvirt_enabled() { return 1; }

/*
 * Helper Functions
 */

/*
 * Compare the input path and the path delimited by start and end.
 * Return true if the latter is the prefix of path.
 * */

static bool
pathsCmp(const char *path,
         const char *start,
         const char *end)
{
  return (end - start > 0 &&
          strncmp(path, start, end - start) == 0 &&
          (path[end - start] == '\0' || // they are equal
           path[end - start] == '/'));  // path has additional sub dirs
}

/*
 * clfind - returns first index in colonList which is a prefix for path
 *          modifies the @listPtr to point to the element in colonList
 */
static int
clfind(const char *colonList,  // IN
       const char *path,       // IN
       char **listPtr)         // OUT
{
    int index = 0;
    char *element = const_cast<char *>(colonList);
    char *colon = NULL;

    /* while there is a colon present, loop */
    while ((colon = strchr(element, ':'))) {
        /* check if element is a prefix of path. here, colon - element is
           an easy way to calculate the length of the element in the list
           to use as the size parameter to strncmp */
        if (pathsCmp(path, element, colon)) {
            JTRACE("Prefix match for path") (element) (path);
            *listPtr = element;
            return index;
        }

        /* move element to point to next element */
        element = colon + 1;

        index++;
    }

    /* process the last element in the list */
    if (pathsCmp(path, element, element + strlen(element))) {
        JTRACE("Prefix match for path") (element) (path);
        *listPtr = element;
        return index;
    }

    /* not found */
    JTRACE("No match found") (colonList) (path);
    return -1;
}

/*
 * clget - returns pointer to element in colonList at index i
 *         and NULL if not found
 *
 * NOTE: argument `int i` is declared signed (versus unsigned) because we
 * commonly expect this argument to be the return value of a `clfind` call. If
 * the `clfind` caller did not check the return value and passes in a negative
 * int, this will harmless fail in a "clean" manner, typewise, rather than
 * "harmlessly" failing due to an underlying signed->unsigned type cast.
 */
static char*
clget(const char *colonList, int i)
{
    int curr_ind = 0;
    char *element = const_cast<char *>(colonList);
    char *colon = NULL;

    /* iterate through elements until last one */
    while ((colon = strchr(element, ':'))) {
        /* if we are at the request index, return pointer to start of element */
        if (curr_ind == i)
            return element;

        /* otherwise, advance pointer to next element and bump current index */
        element = colon + 1;
        curr_ind++;
    }

    /* last element */
    if (curr_ind == i)
        return element;

    /* not found */
    return NULL;
}

/*
 * clgetsize - returns size of an element pointed to by @element in the
 *             list
 */
static size_t
clgetsize(const char *colonList, const char *element)
{
    /* either calculate the element's length, or call
     * strlen if element was last one */
    const char *colon = strchr(element, ':');
    return colon ? colon - element : strlen(element);
}

static void
errCheckGetRestartEnv(int ret)
{
    /* ret == -1 is fine; everything else is not */
    if (ret < RESTART_ENV_NOTFOUND) {
        JASSERT(ret != RESTART_ENV_TOOLONG).Text("pathvirt: DMTCP_PATH_PREFIX "
                "exceeds maximum size (12kB). Use a shorter environment "
                "variable or increase MAX_ENV_VAR_SIZE and recompile.");

        JASSERT(ret != RESTART_ENV_DMTCP_BUF_TOO_SMALL)
               .Text("dmtcpplugin: DMTCP_PATH_PREFIX exceeds "
                "dmtcp_get_restart_env()'s MAXSIZE. Use a shorter "
                "environment variable or increase MAXSIZE and recompile.");

        /* all other errors */
        JASSERT(ret >= 0).Text("Fatal error retrieving DMTCP_PATH_PREFIX "
                "environment variable.");
    }
}

/* This function gets called on every restart. Since no user threads are
 * active at that point, it's safe to access the shared data without
 * locks.
 */
void
pathvirtInitialize()
{
    char tmp[MAX_ENV_VAR_SIZE] = {0};

    /* necessary since we don't know how many bytes dmtcp_get_restart_env
       will write */
    memset(newPathPrefixList, 0, sizeof(newPathPrefixList));
    /* Try to get the value of ENV_DPP from new environment variables,
     * passed in on restart */
    DmtcpGetRestartEnvErr_t ret = dmtcp_get_restart_env(ENV_NEW_DPP,
                                    newPathPrefixList,
                                    sizeof(newPathPrefixList) - 1);
    JTRACE("New prefix list") (newPathPrefixList);
    errCheckGetRestartEnv(ret);
    if (ret == RESTART_ENV_SUCCESS) {
      setenv(ENV_NEW_DPP, newPathPrefixList, 1);
    }

    /* If the user had modified the original buffer prior to checkpointing,
     * we use it now.
     */
    if (tmpBufferModified) {
        snprintf(oldPathPrefixList, sizeof(oldPathPrefixList),
                 "%s", tmpOldPathPrefixList);
        tmpBufferModified = false;
        memset(tmpOldPathPrefixList, 0, sizeof(tmpOldPathPrefixList));
    }

    /* If the user specified the DMTCP_ORIGINAL_PATH_PREFIX on restart,
     * this will override previous calls to set the original buffer.
     */
    ret = dmtcp_get_restart_env(ENV_ORIG_DPP, tmp, sizeof(tmp) - 1);
    JTRACE("Temp prefix list") (tmp);
    errCheckGetRestartEnv(ret);
    if (ret == RESTART_ENV_SUCCESS) {
        memset(oldPathPrefixList, 0, sizeof(oldPathPrefixList));
        snprintf(oldPathPrefixList, sizeof(oldPathPrefixList), "%s", tmp);
        setenv(ENV_ORIG_DPP, tmp, 1);
    }
    JTRACE("Old prefix list") (oldPathPrefixList);

    /* we should only swap if oldPathPrefixList contains something,
     * meaning DMTCP_PATH_PREFIX was supplied on launch, and
     * newPathPrefixList contains something, meaning DMTCP_PATH_PREFIX
     * was supplied on restart. this line will run whether
     * DMTCP_PATH_PREFIX was given on restart or not (ret == -1), so
     * virtual_to_physical_path can know whether to try to swap or not
     */
    shouldSwap = *oldPathPrefixList && *newPathPrefixList;
}

EXTERNC void
set_original_path_prefix_list(const char* oldPathPrefix)
{
  pthread_rwlock_wrlock(&listRwLock);
  snprintf(tmpOldPathPrefixList, sizeof(tmpOldPathPrefixList),
           "%s", oldPathPrefix);
  tmpBufferModified = true;
  pthread_rwlock_unlock(&listRwLock);
}

EXTERNC const char*
get_original_path_prefix_list()
{
  const char *tmp = NULL;
  pthread_rwlock_rdlock(&listRwLock);
  tmp = oldPathPrefixList;
  pthread_rwlock_unlock(&listRwLock);
  return tmp;
}

EXTERNC const char*
get_new_path_prefix_list()
{
  const char *tmp = NULL;
  pthread_rwlock_rdlock(&listRwLock);
  tmp = newPathPrefixList;
  pthread_rwlock_unlock(&listRwLock);
  return tmp;
}

EXTERNC const char*
get_virtual_to_physical_path(const char *virt_path)
{
  static dmtcp::string temp;
  temp = VIRTUAL_TO_PHYSICAL_PATH(virt_path);
  return temp.c_str();
}

/*
 * DMTCP Setup
 */

void
pathvirt_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
    /* NOTE:  See warning in plugin/README about calls to printf here. */
    switch (event) {
    case DMTCP_EVENT_INIT:
    {
        /* On init, check if they've specified paths to virtualize via
           DMTCP_PATH_PREFIX env */
        char *oldEnv = getenv(ENV_ORIG_DPP);
        if (oldEnv) {
            /* if so, save it to buffer */
            snprintf(oldPathPrefixList, sizeof(oldPathPrefixList),
                     "%s", oldEnv);
        }
        pthread_rwlock_init(&listRwLock, NULL);
        break;
    }
    case DMTCP_EVENT_PRE_EXEC:
    {
      if (shouldSwap) {
          setenv(ENV_NEW_DPP, newPathPrefixList, 0);
      }
      break;
    }
    case DMTCP_EVENT_POST_EXEC:
    {
       /* We need to use getenv() here instead of dmtcp_get_restart_env()
        * because the latter is activated only after a restart.
        *
        * Also, it seems like there's no clean way to distinguish a process
        * that's fork-ed and exec-ed prior to ckpt-ing from a process
        * that's fork-ed and exec-ed after a restart, other than
        * creating a side-effect on the filesystem. And so, for now, we
        * delegate the responsibility of error checking on the user. The
        * implication is that if a user, by accident or by intention,
        * were to set the two env. vars prior to the first checkpoint,
        * the pathvirt plugin would get activated.
        */
       char *newPrefixList = getenv(ENV_NEW_DPP);
       char *oldPrefixList = getenv(ENV_ORIG_DPP);
       if (newPrefixList && oldPrefixList ) {
           snprintf(oldPathPrefixList, sizeof(oldPathPrefixList),
                    "%s", oldPrefixList);
           snprintf(newPathPrefixList, sizeof(newPathPrefixList),
                    "%s", newPrefixList);
           shouldSwap = *oldPathPrefixList && *newPathPrefixList;
       }
       break;
    }

    case DMTCP_EVENT_RESTART:
      pathvirtInitialize();
      break;

    default:
       break;
    }
}

DmtcpPluginDescriptor_t pathvirt_plugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "pathvirt",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Pathvirt plugin",
  pathvirt_EventHook
};

DMTCP_DECL_PLUGIN(pathvirt_plugin);

/*
 * Pathvirt Libc Wrappers
 *
 * FIXME: There are currently several known limitations of these wrappers.
 *
 * 1. Virtualization is not supported for select path prefixes, such as
 *    `/proc` due to libc wrappers that occur earlier than pathvirt in the
 *    LD_PRELOAD sequence. (example: `/proc/self/exe`, in file plugin
 *    `readlink` wrapper)
 *
 * 2. In many cases, these wrappers should ideally make a test "real" syscall
 *    in order to properly fail if given bad addresses, etc. Currently, this
 *    is only implemented for the `xstat` family of wrappers.
 */

static int _open_open64_work(int(*fn) (const char *path, int flags, ...),
                             const char *path, int flags, mode_t mode)
{
  dmtcp::string temp = VIRTUAL_TO_PHYSICAL_PATH(path);
  const char *phys_path = temp.c_str();

  int fd = -1;
  fd = (*fn)(phys_path, flags, mode);

  return fd;
}

extern "C" int open(const char *path, int flags, ...)
{
  mode_t mode = 0;
  // Handling the variable number of arguments
  if (flags & O_CREAT) {
    va_list arg;
    va_start(arg, flags);
    mode = va_arg(arg, int);
    va_end(arg);
  }
  return _open_open64_work(_real_open, path, flags, mode);
}

extern "C" int __open_2(const char *path, int flags)
{
  return _open_open64_work(_real_open, path, flags, 0);
}

extern "C" int open64(const char *path, int flags, ...)
{
  mode_t mode = 0;
  // Handling the variable number of arguments
  if (flags & O_CREAT) {
    va_list arg;
    va_start(arg, flags);
    mode = va_arg(arg, int);
    va_end(arg);
  }
  return _open_open64_work(_real_open64, path, flags, mode);
}

extern "C" int __open64_2(const char *path, int flags)
{
  return _open_open64_work(_real_open64, path, flags, 0);
}

extern "C" int creat(const char *path, mode_t mode)
{
  // creat() is equivalent to open() with flags equal to
  // O_CREAT|O_WRONLY|O_TRUNC
  return _open_open64_work(_real_open, path, O_CREAT|O_WRONLY|O_TRUNC, mode);
}

extern "C" int creat64(const char *path, mode_t mode)
{
  // creat() is equivalent to open() with flags equal to
  // O_CREAT|O_WRONLY|O_TRUNC
  return _open_open64_work(_real_open64, path, O_CREAT|O_WRONLY|O_TRUNC, mode);
}

static FILE *_fopen_fopen64_work(FILE*(*fn) (const char *path,
                                             const char *mode),
                                 const char *path, const char *mode)
{
  dmtcp::string temp = VIRTUAL_TO_PHYSICAL_PATH(path);
  const char *phys_path = temp.c_str();

  FILE* file = NULL;
  file = (*fn)(phys_path, mode);

  return file;
}

extern "C" FILE *fopen(const char* path, const char* mode)
{
  return _fopen_fopen64_work(_real_fopen, path, mode);
}

extern "C" FILE *fopen64(const char* path, const char* mode)
{
  return _fopen_fopen64_work(_real_fopen64, path, mode);
}

extern "C" FILE *freopen(const char *path, const char *mode, FILE *stream)
{
  dmtcp::string temp = VIRTUAL_TO_PHYSICAL_PATH(path);
  const char *phys_path = temp.c_str();
  FILE *file = _real_freopen(phys_path, mode, stream);

  return file;
}

extern "C" int openat(int dirfd, const char *path, int flags, ...)
{
  va_list arg;
  va_start(arg, flags);
  mode_t mode = va_arg(arg, int);
  va_end(arg);
  dmtcp::string temp = VIRTUAL_TO_PHYSICAL_PATH(path);
  const char *phys_path = temp.c_str();
  int fd = _real_openat(dirfd, phys_path, flags, mode);
  return fd;
}

extern "C" int openat_2(int dirfd, const char *path, int flags)
{
  return openat(dirfd, path, flags, 0);
}

extern "C" int __openat_2(int dirfd, const char *path, int flags)
{
  return openat(dirfd, path, flags, 0);
}

extern "C" int openat64(int dirfd, const char *path, int flags, ...)
{
  va_list arg;
  va_start(arg, flags);
  mode_t mode = va_arg(arg, int);
  va_end(arg);
  dmtcp::string temp = VIRTUAL_TO_PHYSICAL_PATH(path);
  const char *phys_path = temp.c_str();
  int fd = _real_openat64(dirfd, phys_path, flags, mode);
  return fd;
}

extern "C" int openat64_2(int dirfd, const char *path, int flags)
{
  return openat64(dirfd, path, flags, 0);
}

extern "C" int __openat64_2(int dirfd, const char *path, int flags)
{
  return openat64(dirfd, path, flags, 0);
}

extern "C" DIR *opendir(const char *name)
{
  dmtcp::string temp = VIRTUAL_TO_PHYSICAL_PATH(name);
  const char *phys_path = temp.c_str();
  DIR *dir = _real_opendir(phys_path);
  return dir;
}

extern "C" int __xstat(int vers, const char *path, struct stat *buf)
{
  int retval = _real_xstat(vers, path, buf);
  if (retval == -1 && errno == EFAULT) {
    // EFAULT means path or buf was a bad address.  So, we're done.  Return.
  } else {
    dmtcp::string temp = VIRTUAL_TO_PHYSICAL_PATH(path);
    const char *phys_path = temp.c_str();
    retval = _real_xstat(vers, phys_path, buf); // Re-do it with correct path.
  }
  return retval;
}

extern "C" int __xstat64(int vers, const char *path, struct stat64 *buf)
{
  int retval = _real_xstat64(vers, path, buf);
  if (retval == -1 && errno == EFAULT) {
    // EFAULT means path or buf was a bad address.  So, we're done.  Return.
  } else {
    dmtcp::string temp = VIRTUAL_TO_PHYSICAL_PATH(path);
    const char *phys_path = temp.c_str();
    retval = _real_xstat64(vers, phys_path, buf);
  }
  return retval;
}

extern "C" int __lxstat(int vers, const char *path, struct stat *buf)
{
  int retval = _real_lxstat(vers, path, buf);
  if (retval == -1 && errno == EFAULT) {
    // EFAULT means path or buf was a bad address.  So, we're done.  Return.
  } else {
    dmtcp::string temp = VIRTUAL_TO_PHYSICAL_PATH(path);
    const char *phys_path = temp.c_str();
    retval = _real_lxstat(vers, phys_path, buf);
  }
  return retval;
}

extern "C" int __lxstat64(int vers, const char *path, struct stat64 *buf)
{
  int retval = _real_lxstat64(vers, path, buf);
  if (retval == -1 && errno == EFAULT) {
    // EFAULT means path or buf was a bad address.  So, we're done.  Return.
  } else {
    dmtcp::string temp = VIRTUAL_TO_PHYSICAL_PATH(path);
    const char *phys_path = temp.c_str();
    retval = _real_lxstat64(vers, phys_path, buf);
  }
  return retval;
}

extern "C" ssize_t readlink(const char *path, char *buf, size_t bufsiz)
{
  dmtcp::string temp = VIRTUAL_TO_PHYSICAL_PATH(path);
  const char *phys_path = temp.c_str();
  ssize_t retval = _real_readlink(phys_path, buf, bufsiz);
  return retval;
}

extern "C" ssize_t __readlink_chk(const char *path, char *buf,
                                  size_t bufsiz, size_t buflen)
{
  return readlink(path, buf, bufsiz);
}

extern "C" char *realpath(const char *path, char *resolved_path)
{
  dmtcp::string temp = VIRTUAL_TO_PHYSICAL_PATH(path);
  const char *phys_path = temp.c_str();
  char *ret = _real_realpath(phys_path, resolved_path);
  return ret;
}

extern "C" char *__realpath(const char *path, char *resolved_path)
{
  return realpath(path, resolved_path);
}

extern "C" char *__realpath_chk(const char *path, char *resolved_path,
                                size_t resolved_len)
{
  return realpath(path, resolved_path);
}

extern "C" char *canonicalize_file_name(const char *path)
{
  return realpath(path, NULL);
}

extern "C" int access(const char *path, int mode)
{
  dmtcp::string temp = VIRTUAL_TO_PHYSICAL_PATH(path);
  const char *phys_path = temp.c_str();

  return _real_access(phys_path, mode);
}

extern "C" int truncate(const char *path, off_t length)
{
  dmtcp::string temp = VIRTUAL_TO_PHYSICAL_PATH(path);
  const char *phys_path = temp.c_str();

  return _real_truncate(phys_path, length);
}

extern "C" int rename(const char *oldpath, const char *newpath)
{
  dmtcp::string temp1 = VIRTUAL_TO_PHYSICAL_PATH(oldpath);
  dmtcp::string temp2 = VIRTUAL_TO_PHYSICAL_PATH(newpath);
  const char *old_phys_path = temp1.c_str();
  const char *new_phys_path = temp2.c_str();

  return _real_rename(old_phys_path, new_phys_path);
}

extern "C" int mkdir(const char *path, mode_t mode)
{
  dmtcp::string temp = VIRTUAL_TO_PHYSICAL_PATH(path);
  const char *phys_path = temp.c_str();

  return _real_mkdir(phys_path, mode);
}

extern "C" int chmod(const char *path, mode_t mode)
{
  dmtcp::string temp = VIRTUAL_TO_PHYSICAL_PATH(path);
  const char *phys_path = temp.c_str();

  return _real_chmod(phys_path, mode);
}

extern "C" int unlink(const char *path)
{
  dmtcp::string temp = VIRTUAL_TO_PHYSICAL_PATH(path);
  const char *phys_path = temp.c_str();

  return _real_unlink(phys_path);
}

extern "C" int chdir(const char *path)
{
  dmtcp::string temp = VIRTUAL_TO_PHYSICAL_PATH(path);
  const char *phys_path = temp.c_str();

  return _real_chdir(phys_path);
}

extern "C" int remove(const char *path)
{
  dmtcp::string temp = VIRTUAL_TO_PHYSICAL_PATH(path);
  const char *phys_path = temp.c_str();

  return _real_remove(phys_path);
}

extern "C" int rmdir(const char *path)
{
  dmtcp::string temp = VIRTUAL_TO_PHYSICAL_PATH(path);
  const char *phys_path = temp.c_str();

  return _real_rmdir(phys_path);
}

extern "C" int link(const char *oldpath, const char *newpath)
{
  dmtcp::string temp1 = VIRTUAL_TO_PHYSICAL_PATH(oldpath);
  dmtcp::string temp2 = VIRTUAL_TO_PHYSICAL_PATH(newpath);
  const char *old_phys_path = temp1.c_str();
  const char *new_phys_path = temp2.c_str();

  return _real_link(old_phys_path, new_phys_path);
}

extern "C" int symlink(const char *oldpath, const char *newpath)
{
  dmtcp::string temp1 = VIRTUAL_TO_PHYSICAL_PATH(oldpath);
  dmtcp::string temp2 = VIRTUAL_TO_PHYSICAL_PATH(newpath);
  const char *old_phys_path = temp1.c_str();
  const char *new_phys_path = temp2.c_str();

  return _real_symlink(old_phys_path, new_phys_path);
}

extern "C" long pathconf(const char *path, int name)
{
  dmtcp::string temp = VIRTUAL_TO_PHYSICAL_PATH(path);
  const char *phys_path = temp.c_str();

  return _real_pathconf(phys_path, name);
}

extern "C" int statfs(const char *path, struct statfs *buf)
{
  dmtcp::string temp = VIRTUAL_TO_PHYSICAL_PATH(path);
  const char *phys_path = temp.c_str();

  return _real_statfs(phys_path, buf);
}

/*
 * Resolve the path if path is a symbolic link
 *
 * path should be a physical path.
 */
static dmtcp::string
resolve_symlink(const char *path)
{
  struct stat statBuf;
  if (_real_lxstat(_STAT_VER, path, &statBuf) == 0
      && S_ISLNK(statBuf.st_mode)) {
    char buf[PATH_MAX];
    memset(buf, 0, sizeof(buf));
    JASSERT(_real_readlink(path, buf, sizeof(buf) - 1) != -1);
    return virtual_to_physical_path(buf);
  }

  return path;
}

/*
 * virtual_to_physical_path - translate virtual to physical path
 *
 * Returns a dmtcp::string for the corresponding physical path to the given
 * virtual path. If no path translation occurred, the given virtual path
 * will simply be returned as a dmtcp::string.
 *
 * Conceptually, an original path prior to the first checkpoint is considered a
 * "virtual path".  After a restart, it will be substituted using the latest
 * list of registered paths.  Hence, a newly registered path to be substituted
 * is a "physical path".  Internally, DMTCP works with the original "virtual
 * path" as the canonical name.  But in any system calls, it must translate the
 * virtual path to the latest "physical path", which will correspond to the
 * current, post-restart filesystem.
 */
dmtcp::string
virtual_to_physical_path(const char *virt_path)
{
    char *oldPathPtr = NULL;
    dmtcp::string virtPathString(virt_path?virt_path:"");
    dmtcp::string physPathString;

    /* quickly return if no swap or NULL path */
    if (!shouldSwap || !virt_path) {
        return virtPathString;
    }

    /* yes, should swap */

    pthread_rwlock_rdlock(&listRwLock);
    /* check if path is in list of registered paths to swap out */
    int index = clfind(oldPathPrefixList, virt_path, &oldPathPtr);
    if (index == -1) {
      pthread_rwlock_unlock(&listRwLock);
      return resolve_symlink(virtPathString.c_str());
    }

    /* found it in old list, now get a pointer to the new prefix to swap in*/
    char *physPathPtr = clget(newPathPrefixList, index);
    if (physPathPtr == NULL) {
        pthread_rwlock_unlock(&listRwLock);
        return virtPathString;
    }

    size_t newElementSz = clgetsize(newPathPrefixList, physPathPtr);
    size_t oldElementSz = clgetsize(oldPathPrefixList, oldPathPtr);

    /* temporarily null terminate new element */
    physPathPtr[newElementSz] = '\0';

    /* finally, create full path with the new prefix swapped in */
    physPathString = physPathPtr;
    physPathString += "/";
    physPathString += (virt_path + oldElementSz);
    JTRACE("Matching virtual path to real path")
      (virtPathString) (physPathString);

    /* repair the colon list */
    physPathPtr[newElementSz] = ':';

    pthread_rwlock_unlock(&listRwLock);

    return resolve_symlink(physPathString.c_str());
}
