/* Short utility to read sections of .mtcp file.
 * It is primarily useful to developers.
 * It is not used by the rest of MTCP.
 * To compile:  gcc -o readmtcp readmtcp.c
 */

#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include "mtcp_internal.h"

#define BINARY_NAME "readmtcp"

#ifdef __x86_64__
# define HEX_FIELD 8x
#else
# define HEX_FIELD 12x
#endif

int mtcp_restore_cpfd = -1; // '= -1' puts it in regular data instead of common

static void readcs (int fd, char cs);
static void skipfile (int fd, size_t size);
static ssize_t readall(int fd, void *buf, size_t count);

static const char* theUsage =
  "USAGE:\n"
  "readmtcp <ckpt_image_filename>\n"
  "   or: gzip -dc <ckpt_image_filename> | " BINARY_NAME " -\n\n"
  "  --help:      Print this message and exit.\n"
  "  --version:   Print version information and exit.\n"
  "\n"
;

int main(int argc, char **argv) {
  int fd = -1;
  char magicbuf[MAGIC_LEN], *restorename;

//shift args
#define shift argc--,argv++
  shift;
  while (1) {
    if (argc == 0 || (strcmp(argv[0], "--help") == 0 && argc == 1)) {
      printf("%s", theUsage);
      return (-1);
    } else if (strcmp (argv[0], "--version") == 0 && argc == 1) {
      printf("%s", VERSION_AND_COPYRIGHT_INFO);
      return (-1);
    } else if (strcmp (argv[0], "--") == 0 && argc == 2) {
      restorename = argv[1];
      break;
    } else if (strcmp (argv[0], "-") == 0 && argc == 1) {
      fd = 0; /* read from stdin */
    } else if (argc == 1) {
      if (-1 == (fd = open(argv[0], O_RDONLY))) {
        perror("open");
        exit(1);
      }
      break;
    } else {
      printf("%s", theUsage);
      return (-1);
    }
  }

  memset(magicbuf, 0, sizeof magicbuf);
  readall(fd, magicbuf, MAGIC_LEN);
  if (memcmp (magicbuf, "DMTCP_CHECKPOINT", MAGIC_LEN) == 0) {
    while (memcmp(magicbuf, MAGIC, MAGIC_LEN) != 0) {
      int i;
      for (i = 0; i < MAGIC_LEN-1; i++)
        magicbuf[i] = magicbuf[i+1];
      magicbuf[MAGIC_LEN-1] = '\0'; /* MAGIC should be a string w/o '\0' */
      if (0 == readall(fd, magicbuf+(MAGIC_LEN-1), 1)) /* if EOF */
        break;
    }
  }
  if (memcmp (magicbuf, MAGIC, MAGIC_LEN) != 0) {
    char command[512];
    fprintf (stderr, "readmtcp: Not an mtcp image; trying it as dmtcp image\n");
    sprintf (command, "gzip -dc %s | %s -", restorename, argv[0]);
    exit( system(command) );
  }


  /* Find where the restore image goes */
  VA restore_begin;
  size_t restore_size;
  void *restore_start; /* will be bound to fnc, mtcp_restore_start */
  void (*finishrestore) (void);

  /* Set the resource limits for stack from saved values */
  struct rlimit stack_rlimit;

#ifdef FAST_CKPT_RST_VIA_MMAP
  Area area;
  fastckpt_read_header(fd, &stack_rlimit, &area, (VA*) &restore_start);
  restore_begin = area.addr;
  restore_size = area.size;
  finishrestore = (void*) fastckpt_get_finishrestore();

  printf("mtcp_restart: saved stack resource limit:" \
	 " soft_lim: %lu, hard_lim: %lu\n",
	 stack_rlimit.rlim_cur, stack_rlimit.rlim_max);
  printf("*** restored libmtcp.so\n");

  printf("%p-%p rwxp %p 00:00 0          [libmtcp.so]\n",
	restore_begin, restore_begin + restore_size, restore_begin);
  printf("restore_start routine: %p\n", restore_start);

  printf("*** finishrestore\n");
  printf("finishrestore routine: %p\n", finishrestore);
#else
  readcs (fd, CS_STACKRLIMIT); /* resource limit for stack */
  readall(fd, &stack_rlimit, sizeof stack_rlimit);
  printf("mtcp_restart: saved stack resource limit:" \
	 " soft_lim: %lu, hard_lim: %lu\n",
	 stack_rlimit.rlim_cur, stack_rlimit.rlim_max);

  printf("*** restored libmtcp.so\n");
  readcs (fd, CS_RESTOREBEGIN); /* beginning of checkpointed libmtcp.so image */
  readall(fd, &restore_begin, sizeof restore_begin);
  readcs (fd, CS_RESTORESIZE); /* size of checkpointed libmtcp.so image */
  readall(fd, &restore_size, sizeof restore_size);
  readcs (fd, CS_RESTORESTART);
  readall(fd, &restore_start, sizeof restore_start);
  readcs (fd, CS_RESTOREIMAGE);
  skipfile (fd, restore_size);

  printf("%p-%p rwxp %p 00:00 0          [libmtcp.so]\n",
	restore_begin, restore_begin + restore_size, restore_begin);
  printf("restore_start routine: %p\n", restore_start);


  printf("*** finishrestore\n");
  readcs (fd, CS_FINISHRESTORE);
  readall(fd, &finishrestore, sizeof finishrestore);
  printf("finishrestore routine: %p\n", finishrestore);


  char linkbuf[FILENAMESIZE];
  int fdnum, linklen ;
  struct stat statbuf;
  off_t offset;

  printf("*** file descriptors\n");
  readcs (fd, CS_FILEDESCRS);
  while (1) {

    /* Read parameters of next file to restore */

    readall(fd, &fdnum, sizeof fdnum);
    if (fdnum < 0) break;
    readall(fd, &statbuf, sizeof statbuf);
    readall(fd, &offset, sizeof offset);
    readall(fd, &linklen, sizeof linklen);
    if (linklen >= sizeof linkbuf) {
      printf ("filename too long %d\n", linklen);
      exit(1);
    }
    readall(fd, linkbuf, linklen);
    linkbuf[linklen] = '\0';
  }
#endif

  printf("*** memory sections\n");
  while(1) {
    Area area;
    char cstype;
#ifdef FAST_CKPT_RST_VIA_MMAP
    if (fastckpt_get_next_area_dscr(&area) == 0) break;
#else
    readall(fd, &cstype, sizeof cstype);
    if (cstype == CS_THEEND) break;
    if (cstype != CS_AREADESCRIP) {
      printf ("readmtcp: expected CS_AREADESCRIP but had %d\n", cstype);
      exit(1);
    }

    readall(fd, &area, sizeof area);
    readcs (fd, CS_AREACONTENTS);
    if ((area.prot & MTCP_PROT_ZERO_PAGE) == 0) {
      skipfile (fd, area.size);
    }
#endif
    printf("%p-%p %c%c%c%c %8x 00:00 0          %s\n",
	   area.addr, area.addr + area.size,
	    ( area.prot & PROT_READ  ? 'r' : '-' ),
	    ( area.prot & PROT_WRITE ? 'w' : '-' ),
	    ( area.prot & PROT_EXEC  ? 'x' : '-' ),
	    ( area.flags & MAP_SHARED ? 's'
              : ( area.flags & MAP_ANONYMOUS ? 'p' : '-' ) ),
	    0, area.name);
  }

  printf("*** done\n");
   close (fd);
   mtcp_restore_cpfd = -1;
   return 0;
}

static void readcs (int fd, char cs)
{
  char xcs;

  readall(fd, &xcs, sizeof xcs);
  if (xcs != cs) {
    fprintf (stderr,
             "readmtcp readcs: checkpoint section %d next, expected %d\n",
             xcs, cs);
    abort ();
  }
}

static void skipfile(int fd, size_t size)
{
  ssize_t rc;
  size_t ar;
  char array[512];
  ar = 0;

  while(ar != size)
    {
      rc = readall(fd, array, (size-ar < 512 ? size - ar : 512));
      if(rc < 0)
        {
	  printf("readmtcp skipfile: error %d skipping checkpoint\n", errno);
	  exit(1);
        }
      else if(rc == 0)
        {
	  printf("readmtcp skipfile: only skipped %zu bytes instead of %zu from"
                 " checkpoint file\n",
                 ar, size);
	  exit(1);
        }

      ar += rc;
    }
}

static ssize_t readall(int fd, void *buf, size_t count)
{
  int rc;
  char *ptr = (char *) buf;
  size_t num_read = 0;
  for (num_read = 0; num_read < count;) {
    rc = read(fd, ptr + num_read, count - num_read);
    if (rc == -1) {
      if (errno == EINTR || errno == EAGAIN)
        continue;
      else
        return -1;
    }
    else if (rc == 0)
      break;
    else // else rc > 0
      num_read += rc;
  }
  return num_read;
}
