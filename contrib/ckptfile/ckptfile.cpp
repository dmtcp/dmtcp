#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef STANDALONE
#include "jassert.h"
#include "dmtcp.h"
#endif  // ifndef STANDALONE

#define DEFAULT_DATA_FILE  "./ckptfiles.dat"
#define MAX_DATA_FILE_SIZE 12288      // Max. data file size: 12kB
#define MAX_FILE_LEN       PATH_MAX
#define MAX_EXPRESSIONS    1024       // Max. entries in the database
#define readEOF            ((char)-1)

struct ckptfilesdata {
  char filep[MAX_FILE_LEN];
  int ckpt;
  char rstrt[MAX_FILE_LEN];
};

ckptfilesdata ckptfileslist[MAX_EXPRESSIONS];
char *buff = NULL;

static int
readall(int fd, char *buf, int maxCount)
{
  int count = 0;

  while (1) {
    if (count + 100 > maxCount) {
      fprintf(stderr, "Data file is too large.\n");
      return -1;
    }
    int numRead = read(fd, buf + count, 100); // read up to 100 char's at once
    if (numRead == 0) {
      return count;                 // Reading 0 means EOF
    }
    if (numRead > 0) {
      count += numRead;
    }
    if (numRead < 0 && errno != EAGAIN && errno != EINVAL) {
      return -1;                                                      // error
    }
  }
}

static int
read_data_file()
{
  int size = MAX_DATA_FILE_SIZE;

  // We avoid using malloc.
  buff = (char *)mmap(NULL, size, PROT_READ | PROT_WRITE,
                      MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (buff == MAP_FAILED) {
    perror("mmap");
    return -1;
  }
  const char *fname = getenv("DMTCP_CKPTFILES_DATA");
  if (!fname) {
    fname = DEFAULT_DATA_FILE;
  }
  int fd = open(fname, O_RDONLY);
  if (fd < 0) {
    perror("open");
    return -1;
  }
  int count = readall(fd, buff, size);
  if (count < 0) {
    perror("read");
    return -1;
  }
  *(buff + count) = readEOF;
  close(fd);
  return 0;
}

static int listlen = 0;

/*
 * Expects the buffer to contain data in the following format:
 *   FILE:SAVE_AND_RESTORE={1|0};RESTART_PATH=NEW_FILE
 *   FILE_PATTERN:SAVE_AND_RESTORE={1|0};RESTART_PATH=NEW_FILE_PATTERN
 */
static int
parse_data_file()
{
  char *c = buff;
  int j, i = 0;
  int ckptoffset = strlen(":SAVE_AND_RESTORE=");
  int rstrtoffset = strlen(" ;RESTART_PATH=");

  while (*c != readEOF) {
    j = 0;
    while (*c != ':') {
      ckptfileslist[i].filep[j] = *c++;
      j++;
    }
    c += ckptoffset;
    ckptfileslist[i].ckpt = (*c == '1') ? 1 : 0;
    c += rstrtoffset;
    j = 0;
    while (*c != '\n') {
      ckptfileslist[i].rstrt[j] = *c++;
      j++;
    }
    c++; // skip the newline
    i++;
  }
  listlen = i;
}

static int
is_in_ckptfileslist(const char *abspath)
{
  int ret = -1;

  for (int i = 0; i < listlen; i++) {
    ret = fnmatch(ckptfileslist[i].filep, abspath, 0);
    if (ret == 0) {
      return ckptfileslist[i].ckpt;
    }
  }
  return 0;
}

static void
get_restart_path(const char *abspath, const char *cwd, char *newpath)
{
  int ret = -1;

  for (int i = 0; i < listlen; i++) {
    ret = fnmatch(ckptfileslist[i].filep, abspath, 0);
    if (ret == 0 && !ckptfileslist[i].ckpt) {
      strncpy(newpath, ckptfileslist[i].rstrt, MAX_FILE_LEN);
      return;
    }
  }
}

/* Return 1 if the file should be checkpointed, 0 otherwise. */
extern "C" int
dmtcp_must_ckpt_file(const char *abspath)
{
  return is_in_ckptfileslist(abspath);
}

extern "C" void
dmtcp_get_new_file_path(const char *abspath, const char *cwd, char *newpath)
{
  get_restart_path(abspath, cwd, newpath);
}

static void
preCkpt()
{
  // Code to execute on ckpt phase.
  read_data_file();
  parse_data_file();
}

static void
restart()
{
  // Code to execute on restart phase.
  // You might want to update the criterion for dmtcp_get_new_file_path.
}

#ifndef STANDALONE
static void
ckpfile_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
  case DMTCP_EVENT_PRECHECKPOINT:
    preCkpt();
    break;

  case DMTCP_EVENT_RESTART:
    restart();
    break;
  }
}

#else // ifndef STANDALONE
int
main()
{
  read_data_file();
  parse_data_file();
  return 0;
}
#endif // ifndef STANDALONE

DmtcpPluginDescriptor_t ckpfile_plugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "ckptfile",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Ckptfile plugin",
  ckptfile_event_hook
};

DMTCP_DECL_PLUGIN(ckpfile_plugin);
