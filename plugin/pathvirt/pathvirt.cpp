/* NOTE:  if you just want to insert your own code at the time of checkpoint
 *  and restart, there are two simpler additional mechanisms:
 *  dmtcpaware, and the MTCP special hook functions:
 *    mtcpHookPreCheckpoint, mtcpHookPostCheckpoint, mtcpHookRestart
 */

#include <stdio.h>
#include <sys/time.h>
#include <cstring>
#include <cstdlib>
#include "dmtcp.h"
#include "jassert.h"

#define ENV_DPP            "DMTCP_PATH_PREFIX"
#define MAX_ENV_VAR_SIZE   4096

/* paths should only be swapped on restarts (not on initial run), so this flag */
/* is set on restart */
static int should_swap;

// NOTE: DMTCP_PATH_PREFIX env variables cannot exceed MAX_ENV_VAR_SIZE characters in length
static char old_path_prefix_list[MAX_ENV_VAR_SIZE];
static char new_path_prefix_list[MAX_ENV_VAR_SIZE];

/*
 * Helper Functions
 */

/*
 * clfind - returns first index in colonlist which is a prefix for path
 *          modifies the @listPtr to point to the element in colonlist
 */
static int
clfind(const char *colonlist,  // IN
       const char *path,       // IN
       char **listPtr)         // OUT
{
    int index = 0;
    char *element = const_cast<char *>(colonlist);
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
 * clget - returns pointer to element in colonlist at index i
 *         and NULL if not found
 */
char*
clget(const char *colonlist, unsigned int i)
{
    int curr_ind = 0;
    char *element = const_cast<char *>(colonlist);
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
 * clgetsize_ptr - returns size of an element pointed to by @element in the
 *                 list
 */
static size_t
clgetsize_ptr(const char *colonlist, const char *element)
{
    /* either calculate the element's length, or call
     * strlen if element was last one */
    const char *colon = strchr(element, ':');
    JASSERT(colon >= element) (colonlist) (element);
    return colon ? colon - element : strlen(element);
}

/*
 * clgetsize - returns size of an element at index i in colonlist
 *             and -1 if not found
 */
static ssize_t clgetsize_ind(const char *colonlist, const unsigned int i)
{
    /* get pointer to element at index i */
    char *element = clget(colonlist, i);
    if (element) {
        /* now that we have a pointer, we can use clgetsize_ptr */
        return clgetsize_ptr(colonlist, element);
    }

    /* not found */
    return -1;
}

/*
 * dynamic_path_swap - given old path, return new path or NULL
 *
 * Returns NULL if no swap is to be done and the original path value should
 * be used. Returns a malloc'd pointer to the new string if a swap should
 * happen.
 *
 * If didn't return NULL, the returned pointer must be freed.
 */
static dmtcp::string
dynamic_path_swap(const char *path)
{
    char *oldPathPtr = NULL;
    /* quickly return NULL if no swap */
    if (!should_swap) {
        return "";
    }

    /* yes, should swap */

    /* check if path is in list of registered paths to swap out */
    int index = clfind(old_path_prefix_list, path, &oldPathPtr);
    if (index == -1)
        return "";

    /* found it in old list, now get a pointer to the new prefix to swap in*/
    char *newPathPtr = clget(new_path_prefix_list, index);
    if (newPathPtr == NULL)
        return "";

    size_t new_element_sz = clgetsize_ptr(new_path_prefix_list, newPathPtr);
    size_t old_element_sz = clgetsize_ptr(old_path_prefix_list, oldPathPtr);

    /* temporarily null terminate new element */
    newPathPtr[new_element_sz] = '\0';

    /* finally, create full path with the new prefix swapped in */

    /* plus 1 is for safety slash we include between the new prefix and the
       unchanged rest of the path. this is in case their environment
       variable doesn't end with a slash. in the "worst" case,
       there will be two extra slashes if the new prefix ends with a slash
       and the old one doesn't. plus 1 for NULL */
    size_t newpathsize = (strlen(path) - old_element_sz) + new_element_sz + 1 + 1;
    dmtcp::string newpath (newPathPtr);
    newpath += "/";
    newpath += (path + old_element_sz);

    /* repair the colon list */
    newPathPtr[new_element_sz] = ':';

    return newpath;
}

/*
 * Libc Hooks (for all path related functions)
 */

extern "C"
FILE* fopen64(const char *path, const char *mode)
{
    const char *hook_path = dynamic_path_swap(path).c_str();

    /* hook_path was NULL, not swapping */
    if (!hook_path)
        return NEXT_FNC(fopen64)(path, mode);

    /* swapping */
    FILE* filePtr = NEXT_FNC(fopen64)(hook_path, mode);

    /* dynamic_path_swap's return val needs to be free'd */

    return filePtr;
}

extern "C"
int open(const char *path, int oflag, mode_t mode)
{
    const char *hook_path = dynamic_path_swap(path).c_str();

    /* hook_path was NULL, not swapping */
    if (!hook_path)
        return NEXT_FNC(open)(path, oflag, mode);

    /* swapping */
    int fd = NEXT_FNC(open)(hook_path, oflag, mode);

    /* dynamic_path_swap's return val needs to be free'd */

    return fd;
}

/*
 * DMTCP Setup
 */

void dmtcp_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
    /* NOTE:  See warning in plugin/README about calls to printf here. */
    switch (event) {
    case DMTCP_EVENT_INIT:
    {
        /* On init, check if they've specified paths to virtualize via
           DMTCP_PATH_PREFIX env */
        char *old_env = getenv(ENV_DPP);
        if (old_env) {

            /* if so, save it to buffer */
            snprintf(old_path_prefix_list, sizeof(old_path_prefix_list), "%s",
                     old_env);
        }

        break;
    }
    case DMTCP_EVENT_RESTART:
    {
        /* necessary since we don't know how many bytes dmtcp_get_restart_env
           will write */
        memset(new_path_prefix_list, 0, sizeof(new_path_prefix_list));

        /* Try to get the value of ENV_DPP from new environment variables,
         * passed in on restart */
        int ret = dmtcp_get_restart_env(ENV_DPP, new_path_prefix_list,
                                        sizeof(new_path_prefix_list) - 1);

        /* see below comment for why ret == -1 isn't checked here */

        if (ret == -2) {
#if 0
            /* it found the env var, but we need to allocate more memory and
             * retry
             */

            /* loop until dmtcp_get_restart_env works */
            while (ret == -2) {

                /* double buffer size */
                prefix_list_sz *= 2;
                new_path_prefix_list = realloc(new_path_prefix_list,
                                               prefix_list_sz);

                if (new_path_prefix_list == NULL) {
                    /* TODO handle error, jassert or something */
                }

                /* redo stuff at the beginning */
                memset(new_path_prefix_list, 0, prefix_list_sz);

                ret = dmtcp_get_restart_env(ENV_DPP, new_path_prefix_list,
                                                prefix_list_sz - 1);

                /* This will not infinite loop because a limitation of
                 * dmtcp_get_restart_env is that the name=value pair
                 * can only be a maximum of 2999 bytes long. As soon
                 * as prefix_list_sz exceeds 3000 bytes, dmtcp_get_restart_env
                 * cannot fail with -2 (buffer too small) because the buffer we
                 * provide it will be larger than its maximum output size.
                 */
            }

#endif
        }

        /* we should only swap if old_path_prefix_list contians something,
         * meaning DMTCP_PATH_PREFIX was supplied on launch, and
         * new_path_prefix_list contains something, meaning DMTCP_PATH_PREFIX
         * was supplied on restart. this line will run whether
         * DMTCP_PATH_PREFIX was given on restart or not (ret == -1), so
         * dynamic_path_swap can know whether to try to swap or not
         */
        should_swap = *old_path_prefix_list && *new_path_prefix_list;

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
