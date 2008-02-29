#include "mtcp_internal.h"

/**
 * This function behaves exactly like mtcp_sys_open(), except that you are
 * guaranteed to receive a file descriptor that is not one of the standard 3.
 * This is useful if, for instance, you have no fd 0, receive one through
 * open(), dup2 it to stdin, and close the original (therefore closing stdin).
 *
 * @param filename the file to open
 * @param flags the flags that indicate how to open it
 * @param mode if O_CREAT is in flags and the file gets created, mode indicates
 * the permissions
 *
 * @return a file descriptor to filename, opened according to flags and mode,
 * that is guaranteed to not be one of the standard 3
 */
int mtcp_safe_open(char const *filename, int flags, mode_t mode)
{
    int fds[3];
    int i, j, fd;

    for(i = 0; i < 4; i++)
    {
        fd = mtcp_sys_open(filename, flags, mode);

        if((fd != STDIN_FILENO) &&
           (fd != STDOUT_FILENO) &&
           (fd != STDERR_FILENO))
            break;
        
        fds[i] = fd;
    }

    for(j = 0; j < i; j++)
        mtcp_sys_close(fds[j]);

    return fd;
}
