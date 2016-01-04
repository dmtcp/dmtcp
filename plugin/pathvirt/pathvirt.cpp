#include <stdio.h>
#include <sys/time.h>
#include <cstring>
#include <cstdlib>
#include "dmtcp.h"
#include "jassert.h"

#define ENV_DPP            "DMTCP_PATH_PREFIX"
#define MAX_ENV_VAR_SIZE   10*1024

/* paths should only be swapped on restarts (not on initial run), so this flag
   is set on restart */
static int shouldSwap;

/* NOTE: DMTCP_PATH_PREFIX env variables cannot exceed MAX_ENV_VAR_SIZE
   characters in length */
static char oldPathPrefixList[MAX_ENV_VAR_SIZE];
static char newPathPrefixList[MAX_ENV_VAR_SIZE];

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

/*
 * virtual_to_physical_path - translate virtual to physical path
 *
 * Returns a const char* for the corresponding physical path to the given
 * virtual path. If no path translation occurred, the given virtual path
 * will simply be returned. If path translation *did* occur, the translated
 * physical path will also be available as a dmtcp::string from the second
 * parameter.
 */
const char *
virtual_to_physical_path(const char *virt_path,         // IN
                         dmtcp::string &physPathString) // OUT
{
    char *oldPathPtr = NULL;

    /* quickly return if no swap */
    if (!shouldSwap) {
        return virt_path;
    }

    /* yes, should swap */

    /* check if path is in list of registered paths to swap out */
    int index = clfind(oldPathPrefixList, virt_path, &oldPathPtr);
    if (index == -1)
        return virt_path;

    /* found it in old list, now get a pointer to the new prefix to swap in*/
    char *physPathPtr = clget(newPathPrefixList, index);
    if (physPathPtr == NULL)
        return virt_path;

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

    return physPathString.c_str();
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
        char *oldEnv = getenv(ENV_DPP);
        if (oldEnv) {
            /* if so, save it to buffer */
            snprintf(oldPathPrefixList, sizeof(oldPathPrefixList), "%s",
                     oldEnv);
        }
        break;
    }
    case DMTCP_EVENT_RESTART:
    {
        /* necessary since we don't know how many bytes dmtcp_get_restart_env
           will write */
        memset(newPathPrefixList, 0, sizeof(newPathPrefixList));

        /* Try to get the value of ENV_DPP from new environment variables,
         * passed in on restart */
        int ret = dmtcp_get_restart_env(ENV_DPP, newPathPrefixList,
                                        sizeof(newPathPrefixList) - 1);

        /* ret == -1 is fine; everything else is not */
        if (ret < -1) {
            JASSERT(ret != -2).Text("pathvirt: DMTCP_PATH_PREFIX exceeds "
                    "maximum size (10kb). Use a shorter environment variable "
                    "or increase MAX_ENV_VAR_SIZE and recompile.");

            JASSERT(ret != -3).Text("dmtcpplugin: DMTCP_PATH_PREFIX exceeds "
                    "dmtcp_get_restart_env()'s MAXSIZE. Use a shorter "
                    "environment variable or increase MAXSIZE and recompile.");

            /* all other errors */
            JASSERT(ret >= 0).Text("Fatal error retrieving DMTCP_PATH_PREFIX "
                    "environment variable.");
        }

        /* we should only swap if oldPathPrefixList contains something,
         * meaning DMTCP_PATH_PREFIX was supplied on launch, and
         * newPathPrefixList contains something, meaning DMTCP_PATH_PREFIX
         * was supplied on restart. this line will run whether
         * DMTCP_PATH_PREFIX was given on restart or not (ret == -1), so
         * virtual_to_physical_path can know whether to try to swap or not
         */
        shouldSwap = *oldPathPrefixList && *newPathPrefixList;
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
