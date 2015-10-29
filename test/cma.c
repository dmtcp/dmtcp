#define _GNU_SOURCE
#include <stdio.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0))
#include <sys/uio.h>
#endif
#include <unistd.h>
#include <assert.h>

int main() {
  int i = 0;
  pid_t pid;
  void *addr = &i;

  pid = fork();
  if (pid < 0) {
    perror("fork");
  }
  else if (pid == 0) {
    while (1) {
      sleep(1);
    }
  }
  else {
    while (1) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0))
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
#endif
      printf("%d ", i);
      fflush(stdout);
      sleep(1);
      i++;
    }
  }

  return 0;
}
