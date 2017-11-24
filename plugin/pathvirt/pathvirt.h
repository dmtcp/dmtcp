#ifndef PATHVIRT_H
#define PATHVIRT_H

#ifndef EXTERNC
# ifdef __cplusplus
#  define EXTERNC extern "C"
# else
#  define EXTERNC
# endif
#endif

/*  dmtcp_set_original_path_prefix() can be called at any time
 *  (at checkpoint time, within a user's plugin wrapper function, etc.).
 *  However, this will have no effect until the next restart.
 *  The user must then set the environment variable DMTCP_NEW_PATH_PREFIX
 *  prior to restart.
 *  [ POSSIBLE USE CASE:  An application is launched, and then talks
 *      to a server to discover what directory to use.  Hence,
 *      DMTCP_ORIGINAL_PATH_PREFIX cannot be fully specified until after
 *      launch the application. ]
 */
EXTERNC void set_original_path_prefix_list(const char* oldPathPrefix)
        __attribute__((weak));

EXTERNC const char* get_original_path_prefix_list() __attribute__((weak));

EXTERNC const char* get_new_path_prefix_list() __attribute__((weak));

#endif
