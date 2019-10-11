/* For standalone testing, try:
 *   gcc -DSTANDALONE modify-env.c
 *   ./a.out
 * (Reads dmtcp_env.txt from local directory.)
 */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#ifndef STANDALONE
#include "config.h"
#include "dmtcp.h"
#endif /* ifndef STANDALONE */

#define DMTCP_ENV_VAR          "DMTCP_ENV_FILE"
#define DMTCP_DEFAULT_ENV_FILE "dmtcp_env.txt"

/* Example of dmtcp_env.txt:  spaces not allowed in VAR=VAL unless in quotes
 * # comment
 * HOME=me # new value of HOME
 * HOST=newhost
 * EDITOR  # if no '=', then remove EDITOR from environment.
 * FOO="a b c"  # value of var (in quotes) will include spaces
 */

char *read_dmtcp_env_file(char *file, int size);
int readAndSetEnv(char *buf, int size);
int readall(int fd, char *buf, int maxCount);
extern void warning(const char *warning_part1, const char *warning_part2);

EXTERNC int
dmtcp_modify_env_enabled() { return 1; }

#ifdef STANDALONE
int
dmtcp_get_restart_env(char *envName, char *dest, size_t size)
{
  if (getenv(envName)) {
    strncpy(dest, getenv(envName), size);
  }
  return getenv(envName) ? 0 : -1;
}
#endif /* ifdef STANDALONE */

#ifndef STANDALONE

static void
restart()
{
  char env_file[PATH_MAX];
  DmtcpGetRestartEnvErr_t retval = dmtcp_get_restart_env(DMTCP_ENV_VAR, env_file, PATH_MAX);

  if (retval != RESTART_ENV_SUCCESS) {
    if (sizeof DMTCP_DEFAULT_ENV_FILE > sizeof env_file) {
      printf("***** WARNING: pathname of DMTCP_DEFAULT_ENV_FILE"
             " exceeds PATH_MAX *****\n");
    }
    strncpy(env_file, DMTCP_DEFAULT_ENV_FILE, sizeof DMTCP_DEFAULT_ENV_FILE);
  }

  int size = 12288;
  char *buf = read_dmtcp_env_file(env_file, size);
  if (buf != NULL) { // If env_file exists
    readAndSetEnv(buf, size);
  } else { // else env_file doesn't exist (buf == NULL)
# if 0

    // FIXME:  This "if" condition to check environ var. always triggers,
    // even if environ var. was never present.  Uncomment this when fixed.
    if (!getenv("DMTCP_QUIET") &&
        strcmp(getenv("DMTCP_QUIET")[0], "0") != 0) {
# endif /* if 0 */
    warning("modify-env plugin: Couldn't open ",
            "\"dmtcp_env.txt\"\n");
# if 0
  }
# endif /* if 0 */
  }
}

static void
modifyenv_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
  case DMTCP_EVENT_RESTART:
    restart();
    break;

  default:  // other events are not registered
    break;
  }
}

DmtcpPluginDescriptor_t modify_env_plugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "modify-env",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Modify-Environment plugin",
  modifyenv_EventHook
};

DMTCP_DECL_PLUGIN(modify_env_plugin);
#endif /* ifndef STANDALONE */

#define readEOF ((char)-1)

char *
read_dmtcp_env_file(char *file, int size)
{
  // FIXME: WHAT DOES THIS DO?
  // We avoid using malloc.
  char *buf = mmap(NULL, size, PROT_READ | PROT_WRITE,
                   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

  if (buf == MAP_FAILED) {
    warning("mmap: ", strerror(errno));
    exit(1);
  }
#ifdef STANDALONE
  int fd = open(file, O_RDONLY);
  if (fd < 0) {
    warning("open: ", strerror(errno));
    exit(1);
  }
#else /* ifdef STANDALONE */
  char pathname[512];
  if (strlen(dmtcp_get_ckpt_dir()) > sizeof(pathname) - 1 - sizeof(file)) {
    warning(__FILE__ ": Pathname of ckpt dir is too long: ",
            dmtcp_get_ckpt_dir() /* , "\n" */);
    exit(1);
  }
  strcpy(pathname, dmtcp_get_ckpt_dir());
  strcpy(pathname + strlen(dmtcp_get_ckpt_dir()), "/");
  strcpy(pathname + strlen(dmtcp_get_ckpt_dir()) + strlen("/"), file);
  int fd = open(pathname, O_RDONLY);
  if (fd < 0) {
    return NULL;
  }
#endif /* ifdef STANDALONE */
  int count = readall(fd, buf, size);
  if (count < 0) {
    warning("read: ", strerror(errno));
    exit(1);
  }
  *(buf + count) = readEOF;
  close(fd);
  return buf;
}

int
readAndSetEnv(char *buf, int size)
{
  // We call read() on env.txt in dir of getCkptDir() until readEOF==(char)-1
  char *c = buf;
  char nameBuf[1000] = { '\0' };
  char valueBuf[1000] = { 0 };
  char nameChanged[10000] = { 0 };
  char *dest = nameBuf;
  int isStringMode = 0; // isStringMode is true if in middle of string: "..."
  char *nameChanged_end = nameChanged;

  nameChanged[0] = nameChanged[1] = '\0';
  while (1) {
    switch (*c) {
    case readEOF:
      return 0;

    case '\n':
      if (isStringMode) {
        *dest++ = *c++;
        break;
      }
      if (nameBuf[0] == '\0') {   // if comment line or blank line
        c++;
        break;
      }
      *dest++ = '\0';
      c++;

      // Put nameBuf and value into environment
      if (dest > nameBuf && dest < nameBuf + sizeof(nameBuf)) {
        unsetenv(nameBuf);    // No valueBuf means to unset that name
      } else {
        setenv(nameBuf, valueBuf, 1);   // 1 = overwrite
      }

      // Record that this name changed, in case user does $expansion on it
      if (nameChanged + sizeof(nameChanged) - nameChanged_end) {
        strcpy(nameChanged_end, nameBuf);
        nameChanged_end += strlen(nameBuf) + 1;
      } else {
        warning("", "modify-env.c: Too many '$' name expansions\n");
      }

      // Get ready for next name-value pair
      isStringMode = 0;
      dest = nameBuf;
      break;
    case ' ':
    case '\t':
    case '#':
      if (isStringMode) {
        *dest++ = *c++;
        break;
      }

      // Else set c to end of this line
      while (*c != '\n' && *c != readEOF) {
        c++;
      }
      break;
    case '=':
      *dest = '\0';
      dest = valueBuf;
      c++;
      break;
    case '\\':
      c++;
      *dest++ = *c++;   // consume char that was escaped
      break;
    case '"':   // Change parity of string mode
      isStringMode = 1 - isStringMode;
      c++;
      break;
    case '$':   // Expand variable in current environment
      // Env name after '$' may consist only of alphanumeric char's and '_'
    { char envName[1000];
      char *d = envName;
      c++;
      while (isalnum(*c) || *c == '_') {
        *d++ = *c++;
      }
      *d = '\0';

      // If we modified envName, this takes precedence over current value
      int isNameChanged = 0;
      char *n;
      for (n = nameChanged; n < nameChanged_end; n += strlen(n) + 1) {
        if (strcmp(envName, n) == 0) {
          isNameChanged = 1;
        }
      }

      // Copy expansion of envName into dest
      DmtcpGetRestartEnvErr_t rc = RESTART_ENV_SUCCESS;
      if (isNameChanged && getenv(envName)) {
        strncpy(dest, getenv(envName), sizeof nameBuf);
      } else {
        rc = dmtcp_get_restart_env(envName, dest,
                                   sizeof(valueBuf) - (dest - valueBuf));
      }
      if (rc == RESTART_ENV_SUCCESS) {
        dest += strlen(dest);      // Move dest ptr to end of expanded string
      }
      break; }
    default:
      *dest++ = *c++;
      break;
    }
  }
}

int
readall(int fd, char *buf, int maxCount)
{
  int count = 0;

  while (1) {
    if (count + 100 > maxCount) {
      warning("", "Environment file is too large.\n");
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

#ifdef STANDALONE
int
main()
{
  int size = 4096;

  printf("HOME: %s, DISPLAY: %s, FOO: %s, HOST: %s, EDITOR: %s, USER: %s\n",
         getenv("HOME"), getenv("DISPLAY"), getenv("FOO"), getenv("HOST"),
         getenv("EDITOR"), getenv("USER"));
  char *buf = read_dmtcp_env_file("dmtcp_env.txt", size);
  readAndSetEnv(buf, size);
  printf("HOME: %s, DISPLAY: %s, FOO: %s, HOST: %s, EDITOR: %s, USER: %s\n",
         getenv("HOME"), getenv("DISPLAY"), getenv("FOO"), getenv("HOST"),
         getenv("EDITOR"), getenv("USER"));
  return 0;
}
#endif /* ifdef STANDALONE */
