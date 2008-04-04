#include "mtcp_internal.h"

/**
 * This function behaves exactly like pipe(), except that you are
 * guaranteed to receive  file descriptors that are not among the standard 3.
 * This is useful if, for instance, you have no fd 0, receive one through
 * pipe(), dup2 it to stdin, and close the original (therefore closing stdin).
 *
 * @param arr the array to store the actual results
 *
 * @return On success, 0 is returned.  Otherwise, -1 is returned and errno
 * is set appropriately
 */
int mtcp_safe_pipe(int arr[2])
{
    int fds[3][2];
    int i, j, retval, in, out;

    for(i = 0; i < 4; i++)
    {
        retval = pipe(fds[i]);
        if(retval != 0)
            return retval;

        in = fds[i][0];
        out = fds[i][1];

        if((in != STDIN_FILENO) &&
           (in != STDOUT_FILENO) &&
           (in != STDERR_FILENO) &&
           (out != STDIN_FILENO) &&
           (out != STDOUT_FILENO) &&
           (out != STDERR_FILENO))
            break;
    }

    arr[0] = fds[i][0];
    arr[1] = fds[i][1];

    for(j = 0; j < i; j++)
    {
        mtcp_sys_close(fds[j][0]);
        mtcp_sys_close(fds[j][1]);
    }

    return retval;
}
