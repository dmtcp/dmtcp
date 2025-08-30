#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#include <err.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

int main(int argc, const char *argv[])
{
    int count = 0;
    char *append = "appending";
    char *fname = "/pv-test.txt";
    char cwd[128];
    if (!getcwd(cwd, sizeof cwd))
        err(1, "could not get cwd");

    char last_digit = *(cwd + strlen(cwd) - 1);

    size_t fullpathsize = strlen(cwd) + strlen(fname) + 1;
    char fullpath[fullpathsize];
    snprintf(fullpath, sizeof(fullpath), "%s%s", cwd, fname);

    while (1) {
        printf("[%d] Appending...\n", count);

        int fd = open(fullpath, O_RDWR | O_APPEND, S_IRWXU);
        if (fd < 0) {
            err(1, "could not open file");
        } else {
            dprintf(fd, "%d %s\n", count++, append);
        }
        close(fd);

        sleep(1);
    }


    return 0;
}
