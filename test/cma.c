#define _GNU_SOURCE
#include <linux/version.h>
#include <stdio.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)) && __GLIBC_PREREQ(2, 15)
  # include <sys/uio.h>
#endif /* if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)) &&
          __GLIBC_PREREQ(2, 15) */
#include <assert.h>
#include <unistd.h>

int
main()
{
  int i = 0;
  pid_t pid;
  void *addr = &i;

  pid = fork();
  if (pid < 0) {
    perror("fork");
  } else if (pid == 0) {
    while (1) {
      sleep(1);
    }
  } else {
    while (1) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)) && __GLIBC_PREREQ(2, 15)
      struct iovec local, remote;
      ssize_t ret;

      local.iov_base = addr;
      local.iov_len = sizeof(int);
      remote.iov_base = addr;
      remote.iov_len = sizeof(int);
      ret = process_vm_writev(pid, &local, 1, &remote, 1, 0);
      assert(ret == sizeof(int));
      ret = process_vm_readv(pid, &local, 1, &remote, 1, 0);
      assert(ret == sizeof(int));
#endif /* if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)) &&
          __GLIBC_PREREQ(2, 15) */
      printf("%d ", i);
      fflush(stdout);
      sleep(1);
      i++;
    }
  }

  return 0;
}
