// For standalone testing, define STANDALONE, which reads dmtcp_env.txt
//  from the local directory.
// #define STANDALONE

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#ifndef STANDALONE
# include "dmtcpplugin.h"
#endif

/* Example of dmtcp_env.txt:  spaces not allowed in VAR=VAL unless in quotes
 * # comment
 * HOME=me # new value of HOME
 * HOST=newhost
 * EDITOR  # if no '=', then remove EDITOR from environment.
 * FOO="a b c"  # value of var (in quotes) will include spaces
 */

char * read_dmtcp_env_file(char *file, int size);
int readAndSetEnv(char *buf, int size);
int readall(int fd, char *buf, int maxCount);

#ifndef STANDALONE
void dmtcp_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  /* NOTE:  See warning in plugin/README about calls to printf here. */
  switch (event) {
  case DMTCP_EVENT_RESTART:
  { int size = 4096;
    char *buf = read_dmtcp_env_file("dmtcp_env.txt", size);
    readAndSetEnv(buf, size);
    printf("HOME: %s\n", getenv("HOME"));
    break;
  }
  default:
    break;
  }
  DMTCP_NEXT_EVENT_HOOK(event, data);
}
#endif

const char readEOF = -1;

char * read_dmtcp_env_file(char *file, int size) {
  // We avoid using malloc.
  char *buf = mmap(NULL, size, PROT_READ | PROT_WRITE,
                   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (buf == MAP_FAILED) {
    perror("mmap");
    exit(1);
  }
#ifdef STANDALONE
  int fd = open(file, O_RDONLY);
#else
  char pathname[512];
  if (strlen(dmtcp_get_ckpt_dir()) > sizeof(pathname)-1-sizeof(file)) {
    fprintf(stderr, "%s:%d : Pathname of ckpt dir is too long: %s\n",
            __FILE__, __LINE__, dmtcp_get_ckpt_dir());
    exit(1);
  }
  strcpy(pathname, dmtcp_get_ckpt_dir());
  strcpy(pathname + strlen(dmtcp_get_ckpt_dir()), "/");
  strcpy(pathname + strlen(dmtcp_get_ckpt_dir()) + strlen("/"), file);
  int fd = open(pathname, O_RDONLY);
#endif
  if (fd < 0) {
    perror("open");
    exit(1);
  }
  int count = readall(fd, buf, size);
  if (count < 0) {
    perror("read");
    exit(1);
  }
  *(buf+count) = readEOF;
  return buf;
}


int readAndSetEnv(char *buf, int size) {
  // We call read() on env.txt in dir of getCkptDir() unti readEOF==(char)-1
  char *c = buf;
  int interval = 0;
  while (*c != readEOF) {
    char *newString = buf - interval;
    int isStringMode = 0; // isStringMode is true if in middle of string: "..."
    // Advance c to end of env variable-value pair
    while (*c != '\n' && *c != readEOF &&
           (isStringMode || (*c != '#' && *c != ' ' && *c != '\t'))) {
      if (c > buf && *(c-1) == '\\') {  // if we had escaped the current *c
        interval++; c++; // then let c back up and replace escape char by char being escaped
      } else if (*c == '"') {
        isStringMode = 1 - isStringMode;
        interval++; c++;
        continue;  // Change parity of string mode
      }

      if (c-buf >= size) {
        fprintf(stderr, "Environment is longer than buffer size (%d)\n", size);
        return -1; // return error
      } else {
        *(c-interval) = *c; // get next char of envVar=envVal
        c++;
      }
  }
  if (isStringMode) {
    fprintf(stderr, "Unterminated string in environment\n");
    return -1;
  }
  assert(isspace(*c) || *c == '#' || *c == readEOF);
  int endOfLine = (*c == '\n' || *c == readEOF);
  if (c>buf) {
    // Add string to environment.
    if (*c == readEOF) *(c+1) = readEOF; // don't lose the end of file.
    *(c-interval) = '\0';
    c++;
    // malloc is not reentrant.
    // putenv mostly avoids using malloc.  (If a different thread is stopped
    //  inside malloc, it would be bad for this thread to use malloc.)
    // But 'man putenv' says:  If putenv() has  to  allocate  a new  array
    //  environ,  and  the  previous  array  was  also allocated by
    //  putenv(), then it will be freed.  In no case will the old storage
    //  associated to the environment variable itself be freed.
    // Note that putenv need not be reentrant, but was so starting w/ glibc-2.1
    putenv(newString);
  }
  size = size - (c-buf);
  buf = c;
  if (! endOfLine) {
    while (*c != '\0' && *c != '\n' && *c != readEOF) c++; // eat rest of line
    if (*c == '\n') c++; // eat newline at end of line
  }
  interval += c-buf;
  buf = c;
  }
}

int readall(int fd, char *buf, int maxCount) {
  int count = 0;
  while (1) {
    if (count + 100 > maxCount) {
      fprintf(stderr, "Environment file is too large.\n");
      return -1;
    }
    int numRead = read(fd, buf+count, 100); // read up to 100 char's at once
    if (numRead == 0) return count; // Reading 0 means EOF
    if (numRead > 0) count += numRead;
    if (numRead < 0 && errno != EAGAIN && errno != EINVAL) return -1; // error
  }
}

#ifdef STANDALONE
int main() {
  int size = 4096;
/*
  // We avoid using malloc.
  char *buf = mmap(NULL, size, PROT_READ | PROT_WRITE,
                   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (buf == MAP_FAILED) {
    perror("mmap");
    return 1;
  }
  int fd = open("dmtcp_env.txt", O_RDONLY);
  if (fd < 0) {
    perror("open: \"dmtcp_env.txt\"");
    return 1;
  }
  int count = readall(fd, buf, size);
  if (count < 0) {
    perror("read: \"dmtcp_env.txt\"");
    return 1; // Error in reading
  }
  *(buf+count) = readEOF;
*/
  char *buf = read_dmtcp_env_file("dmtcp_env.txt", size);
  readAndSetEnv(buf, size);
  return 0;
}
#endif
