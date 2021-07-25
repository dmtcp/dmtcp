#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/sysmacros.h>
#include <limits.h>
#include <stdio.h>
#include <sys/mman.h>
#include <stdlib.h>

#include "procmapsutils.h"

static char readDec(int , VA* );
static char readChar(int );
static char readHex(int , VA* );

EXTERNC int
readMapsLine(int mapsfd, Area *area)
{
  char c, rflag, sflag, wflag, xflag;
  int i;
  off_t offset;
  unsigned int long devmajor, devminor, inodenum;
  VA startaddr, endaddr;

  c = readHex(mapsfd, &startaddr);
  if (c != '-') {
    if ((c == 0) && (startaddr == 0)) return (0);
    goto skipeol;
  }
  c = readHex(mapsfd, &endaddr);
  if (c != ' ') goto skipeol;
  if (endaddr < startaddr) goto skipeol;

  rflag = c = readChar(mapsfd);
  if ((c != 'r') && (c != '-')) goto skipeol;
  wflag = c = readChar(mapsfd);
  if ((c != 'w') && (c != '-')) goto skipeol;
  xflag = c = readChar(mapsfd);
  if ((c != 'x') && (c != '-')) goto skipeol;
  sflag = c = readChar(mapsfd);
  if ((c != 's') && (c != 'p')) goto skipeol;

  c = readChar(mapsfd);
  if (c != ' ') goto skipeol;

  c = readHex (mapsfd, (VA *)&offset);
  if (c != ' ') goto skipeol;
  area -> offset = offset;

  c = readHex (mapsfd, (VA *)&devmajor);
  if (c != ':') goto skipeol;
  c = readHex (mapsfd, (VA *)&devminor);
  if (c != ' ') goto skipeol;
  c = readDec (mapsfd, (VA *)&inodenum);
  area -> name[0] = '\0';
  while (c == ' ') c = readChar (mapsfd);
  if (c == '/' || c == '[') { /* absolute pathname, or [stack], [vdso], etc. */
    i = 0;
    do {
      area -> name[i++] = c;
      if (i == sizeof area -> name) goto skipeol;
      c = readChar (mapsfd);
    } while (c != '\n');
    area -> name[i] = '\0';
  }

  if (c != '\n') goto skipeol;

  area -> addr = startaddr;
  area -> endAddr = endaddr;
  area -> size = endaddr - startaddr;
  area -> prot = 0;
  if (rflag == 'r') area -> prot |= PROT_READ;
  if (wflag == 'w') area -> prot |= PROT_WRITE;
  if (xflag == 'x') area -> prot |= PROT_EXEC;
  area -> flags = MAP_FIXED;
  if (sflag == 's') area -> flags |= MAP_SHARED;
  if (sflag == 'p') area -> flags |= MAP_PRIVATE;
  if (area -> name[0] == '\0') area -> flags |= MAP_ANONYMOUS;

  area->devmajor = devmajor;
  area->devminor = devminor;
  area->inodenum = inodenum;
  return (1);

skipeol:
  fprintf(stderr, "ERROR: readMapsLine*: bad maps line <%c", c);
  while ((c != '\n') && (c != '\0')) {
    c = readChar (mapsfd);
    printf ("%c", c);
  }
  printf(">\n");
  abort();
  return 0;  /* NOTREACHED : stop compiler warning */
}

/* Read non-null character, return null if EOF */
static char
readChar(int fd)
{
  int errno;
  char c;
  int rc;

  do {
    rc = read(fd, &c, 1);
  } while (rc == -1 && errno == EINTR);
  if (rc <= 0) return 0;
  return c;
}

/* Read decimal number, return value and terminating character */
static char
readDec(int fd, VA *value)
{
  char c;
  unsigned long int v = 0;

  while (1) {
    c = readChar(fd);
    if ((c >= '0') && (c <= '9')) c -= '0';
    else break;
    v = v * 10 + c;
  }
  *value = (VA)v;
  return c;
}

/* Read decimal number, return value and terminating character */
static char
readHex(int fd, VA *virt_mem_addr)
{
  char c;
  unsigned long int v = 0;

  while (1) {
    c = readChar (fd);
    if ((c >= '0') && (c <= '9')) c -= '0';
    else if ((c >= 'a') && (c <= 'f')) c -= 'a' - 10;
    else if ((c >= 'A') && (c <= 'F')) c -= 'A' - 10;
    else break;
    v = v * 16 + c;
  }
  *virt_mem_addr = (VA)v;
  return c;
}
