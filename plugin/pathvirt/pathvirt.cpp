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

#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <cstring>
#include <cstdlib>
#include <stdarg.h>
#include <dirent.h>
#include <fcntl.h>

#undef open
#undef open64
#undef openat
#undef openat64
#undef readlink
#undef __readlink_chk
#undef realpath

#include "dmtcp.h"
#include "dmtcpplugin.h"
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

/*
 * Helper Functions
 */

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
    while (colon = strchr(element, ':')) {
        /* check if element is a prefix of path. here, colon - element is
           an easy way to calculate the length of the element in the list
           to use as the size parameter to strncmp */
        if (strncmp(path, element, colon - element) == 0) {
            *listPtr = element;
            return index;
        }

        /* move element to point to next element */
        element = colon + 1;

        index++;
    }

    /* process the last element in the list */
    if (strncmp(path, element, strlen(element)) == 0) {
        *listPtr = element;
        return index;
    }

    /* not found */
    return -1;
}

/*
 * clget - returns pointer to element in colonList at index i
 *         and NULL if not found
 */
static char*
clget(const char *colonList, unsigned int i)
{
    int curr_ind = 0;
    char *element = const_cast<char *>(colonList);
    char *colon = NULL;

    /* iterate through elements until last one */
    while (colon = strchr(element, ':')) {
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
    if (ret < -1 /* RESTART_ENV_NOT_FOUND */) {
        JASSERT(ret != RESTART_ENV_TOOLONG).Text("pathvirt: DMTCP_PATH_PREFIX exceeds "
                "maximum size (10kb). Use a shorter environment variable "
                "or increase MAX_ENV_VAR_SIZE and recompile.");

        JASSERT(ret != RESTART_ENV_DMTCP_BUF_TOO_SMALL).Text("dmtcpplugin: DMTCP_PATH_PREFIX exceeds "
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
    int ret = dmtcp_get_restart_env(ENV_NEW_DPP, newPathPrefixList,
                                    sizeof(newPathPrefixList) - 1);
    errCheckGetRestartEnv(ret);

    /* If the user had modified the original buffer prior to checkpointing,
     * we use it now.
     */
    if (tmpBufferModified) {
        snprintf(oldPathPrefixList, sizeof(oldPathPrefixList), "%s", tmpOldPathPrefixList);
        tmpBufferModified = false;
        memset(tmpOldPathPrefixList, 0, sizeof(tmpOldPathPrefixList));
    }

    /* If the user specified the DMTCP_ORIGINAL_PATH_PREFIX on restart,
     * this will overrided previous calls to set the original buffer.
     */
    ret = dmtcp_get_restart_env(ENV_ORIG_DPP, tmp, sizeof(tmp) - 1);
    errCheckGetRestartEnv(ret);
    if (ret == RESTART_ENV_SUCCESS) {
        memset(oldPathPrefixList, 0, sizeof(oldPathPrefixList));
        snprintf(oldPathPrefixList, sizeof(oldPathPrefixList), "%s", tmp);
    }

    /* we should only swap if oldPathPrefixList contains something,
     * meaning DMTCP_PATH_PREFIX was supplied on launch, and
     * newPathPrefixList contains something, meaning DMTCP_PATH_PREFIX
     * was supplied on restart. this line will run whether
     * DMTCP_PATH_PREFIX was given on restart or not (ret == -1), so
     * virtual_to_physical_path can know whether to try to swap or not
     */
    shouldSwap = *oldPathPrefixList && *newPathPrefixList;
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
static dmtcp::string
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
    if (index == -1)
        return virtPathString;

    /* found it in old list, now get a pointer to the new prefix to swap in*/
    char *physPathPtr = clget(newPathPrefixList, index);
    if (physPathPtr == NULL)
        return virtPathString;

    size_t newElementSz = clgetsize(newPathPrefixList, physPathPtr);
    size_t oldElementSz = clgetsize(oldPathPrefixList, oldPathPtr);

    /* temporarily null terminate new element */
    physPathPtr[newElementSz] = '\0';

    /* finally, create full path with the new prefix swapped in */
    physPathString = physPathPtr;
    physPathString += "/";
    physPathString += (virt_path + oldElementSz);

    /* repair the colon list */
    physPathPtr[newElementSz] = ':';

    pthread_rwlock_unlock(&listRwLock);

    return physPathString;
}

EXTERNC void
set_original_path_prefix_list(const char* oldPathPrefix)
{
  pthread_rwlock_wrlock(&listRwLock);
  snprintf(tmpOldPathPrefixList, sizeof(tmpOldPathPrefixList), "%s", oldPathPrefix);
  tmpBufferModified = true;
  pthread_rwlock_unlock(&listRwLock);
}

EXTERNC const char*
get_original_path_prefix_list()
{
  pthread_rwlock_rdlock(&listRwLock);
  return oldPathPrefixList;
  pthread_rwlock_unlock(&listRwLock);
}

EXTERNC const char*
get_new_path_prefix_list()
{
  pthread_rwlock_rdlock(&listRwLock);
  return newPathPrefixList;
  pthread_rwlock_unlock(&listRwLock);
}

/*
 * DMTCP Setup
 */

void
dmtcp_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
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
            snprintf(oldPathPrefixList, sizeof(oldPathPrefixList), "%s", oldEnv);
        }
        pthread_rwlock_init(&listRwLock, NULL);
        break;
    }
    case DMTCP_EVENT_RESTART:
    {
        JTRACE("\n*** The plugin %s is being called after restart. ***");
        break;
    }
    case DMTCP_EVENT_WRITE_CKPT:
        JTRACE("\n*** The plugin %s is being called before checkpointing. ***");
        break;
    case DMTCP_EVENT_RESUME:
        JTRACE("*** The plugin %s has now been checkpointed. ***");
        break;
    default:
    ;
    }

    /* Call this next line in order to pass DMTCP events to later plugins. */
    DMTCP_NEXT_EVENT_HOOK(event, data);
}

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
  //creat() is equivalent to open() with flags equal to O_CREAT|O_WRONLY|O_TRUNC
  return _open_open64_work(_real_open, path, O_CREAT|O_WRONLY|O_TRUNC, mode);
}

extern "C" int creat64(const char *path, mode_t mode)
{
  //creat() is equivalent to open() with flags equal to O_CREAT|O_WRONLY|O_TRUNC
  return _open_open64_work(_real_open64, path, O_CREAT|O_WRONLY|O_TRUNC, mode);
}

static FILE *_fopen_fopen64_work(FILE*(*fn) (const char *path, const char *mode),
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
