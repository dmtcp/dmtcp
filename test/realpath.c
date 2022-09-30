#define _XOPEN_SOURCE 500
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void resolve_symlink(char *path, char *buf)
{
  char orig_proc_pid_exe_sym[64];
  char orig_proc_pid_exe_filepath[256] = {0};
  sprintf(orig_proc_pid_exe_sym, "/proc/%d/exe", getpid());
  int orig_proc_pid_exe_filepath_len = readlink(orig_proc_pid_exe_sym, orig_proc_pid_exe_filepath, 255);
  if (orig_proc_pid_exe_filepath_len == -1) {
    abort();
  }
}

// POSIX.1-2008 says that NULL will cause memory to be allocated.
// In the earlier POSIX.1-2001, GNU implementations would return NULL.
// GNU libc contains two symbols:  one for each flavor.
// Hence, this test can succeed only in newer GNU libc's supporting POSIX.1-2008
// A value of _XOPEN_SOURCE of 500 should support this.
// Configure autotest first to test if this program can succeed without DMTCP.
int
main()
{
  char orig_proc_pid_exe_filepath[256] = {0};
  char orig_proc_self_exe_filepath[256] = {0};

  char proc_pid_exe[64];
  sprintf(proc_pid_exe, "/proc/%d/exe", getpid());

  resolve_symlink(proc_pid_exe, orig_proc_pid_exe_filepath);
  resolve_symlink("/proc/self/exe", orig_proc_self_exe_filepath);

  if (strcmp(orig_proc_pid_exe_filepath, orig_proc_self_exe_filepath) == 0) {
    printf("/proc/self/exe (%s) and /proc/pid/exe (%s) returned same values.\n", orig_proc_pid_exe_filepath, orig_proc_self_exe_filepath);
  } else {
    printf("WARNING: /proc/self/exe (%s) and /proc/pid/exe (%s) returned different values.\n", orig_proc_pid_exe_filepath, orig_proc_self_exe_filepath);
    abort();
  }

  for (int i = 0; i < (int)2e9; i++) {
    char proc_pid_exe_filepath[256] = {0};
    char proc_self_exe_filepath[256] = {0};

    resolve_symlink(proc_pid_exe, proc_pid_exe_filepath);
    resolve_symlink("/proc/self/exe", proc_self_exe_filepath);

    if (strcmp(proc_pid_exe_filepath, proc_self_exe_filepath) == 0) {
      //printf("/proc/self/exe (%s) and /proc/pid/exe (%s) returned same values.\n", orig_proc_pid_exe_filepath, orig_proc_self_exe_filepath);
    } else {
      printf("WARNING: /proc/self/exe (%s) and /proc/pid/exe (%s) returned different values.\n", orig_proc_pid_exe_filepath, orig_proc_self_exe_filepath);
      abort();
    }

    if (strcmp(proc_self_exe_filepath, orig_proc_self_exe_filepath) == 0) {
      //printf("original /proc/self/exe (%s) and current /proc/self/exe (%s) returned same values.\n", orig_proc_self_exe_filepath, proc_self_exe_filepath);
    } else {
      printf("WARNING: original /proc/self/exe (%s) and current /proc/self/exe (%s) returned different values.\n", orig_proc_self_exe_filepath, proc_self_exe_filepath);
      abort();
    }

    char *path = realpath("/etc/passwd", NULL);
    if (path == NULL) {
      abort();
    } else {
      free(path);
    }
    if (i % (int)1e6 == 0) {
      printf(".");
      fflush(stdout);
    }
  }
  return 0;
}
