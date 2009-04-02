/* Short utility to read sections of .mtcp file.
 * It is primarily useful to developers. 
 * It is not used by the rest of MTCP.
 * To compile:  gcc -o readmtcp readmtcp.c
 */

#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "mtcp_internal.h"

#ifdef __x86_64__
# define HEX_FIELD 8x
#else
# define HEX_FIELD 12x
#endif

int mtcp_restore_cpfd = -1; // '= -1' puts it in regular data instead of common

static char first_char(char *filename);
static void readcs (int fd, char cs);
static void readfile (int fd, void *buf, int size);
static void skipfile (int fd, int size);

int main(int argc, char **argv) {
  int fd = -1;
  char magicbuf[MAGIC_LEN], *restorename;

  restorename = argv[1];
  if (restorename == NULL) {
    printf("usage: readmtcp <ckpt_image_filename>\n"
	   "   or: gzip -dc <ckpt_image_filename> | %s -\n",
	   argv[0]);
    exit(1);
  } else if (restorename[0] == '-' && restorename[1] == '\0') {
    fd = 0; /* read from stdin */
  } else {    /* argv[1] should be a real filename */
    if (-1 == (fd = open(restorename, O_RDONLY))) {
      perror("open"); exit(1); }
  }

  memset(magicbuf, 0, sizeof magicbuf);
  readfile (fd, magicbuf, MAGIC_LEN);
  if (memcmp (magicbuf, "DMTCP_CHECKPOINT", MAGIC_LEN) == 0) {
    while (memcmp(magicbuf, MAGIC, MAGIC_LEN) != 0) {
      int i;
      for (i = 0; i < MAGIC_LEN-1; i++)
        magicbuf[i] = magicbuf[i+1];
      magicbuf[MAGIC_LEN-1] = '\0'; /* MAGIC should be a string w/o '\0' */
      if (0 == read(fd, magicbuf+(MAGIC_LEN-1), 1)) /* if EOF */
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
  void *restore_begin, *restore_mmap;
  int restore_size;
  void *restore_start; /* will be bound to fnc, mtcp_restore_start */

  printf("*** restored mtcp.so\n");
  readcs (fd, CS_RESTOREBEGIN); /* beginning of checkpointed mtcp.so image */
  readfile (fd, &restore_begin, sizeof restore_begin);
  readcs (fd, CS_RESTORESIZE); /* size of checkpointed mtcp.so image */
  readfile (fd, &restore_size, sizeof restore_size);
  readcs (fd, CS_RESTORESTART);
  readfile (fd, &restore_start, sizeof restore_start);
  readcs (fd, CS_RESTOREIMAGE);
  skipfile (fd, restore_size);

  printf("%p-%p rwxp %p 00:00 0          [mtcp.so]\n",
	restore_begin, restore_begin + restore_size, restore_begin);
  printf("restore_start routine: 0x%p\n", restore_start);


  printf("*** finishrestore\n");
   void (*finishrestore) (void);
   readcs (fd, CS_FINISHRESTORE);
   readfile (fd, &finishrestore, sizeof finishrestore);
  printf("finishrestore routine: 0x%p\n", finishrestore);


  char linkbuf[FILENAMESIZE];
  int fdnum, flags, linklen, tempfd;
  struct Stat statbuf;

  off_t offset;
  Area area;


  printf("*** file descriptors\n");
  readcs (fd, CS_FILEDESCRS);
  while (1) {

    /* Read parameters of next file to restore */

    readfile (fd, &fdnum, sizeof fdnum);
    if (fdnum < 0) break;
    readfile (fd, &statbuf, sizeof statbuf);
    readfile (fd, &offset, sizeof offset);
    readfile (fd, &linklen, sizeof linklen);
    if (linklen >= sizeof linkbuf) {
      printf ("filename too long %d\n", linklen);
      exit(1);
    }
    readfile (fd, linkbuf, linklen);
    linkbuf[linklen] = 0;
  }



  char cstype;

  printf("*** memory sections\n");
  while(1) {
    readfile (fd, &cstype, sizeof cstype);
    if (cstype == CS_THEEND) break;
    if (cstype != CS_AREADESCRIP) {
      printf ("readmtcp: expected CS_AREADESCRIP but had %d\n", cstype);
      exit(1);
    }

    readfile (fd, &area, sizeof area);
    readcs (fd, CS_AREACONTENTS);
    skipfile (fd, area.size);
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
}

static void readcs (int fd, char cs)

{
  char xcs;

  readfile (fd, &xcs, sizeof xcs);
  if (xcs != cs) {
    fprintf (stderr, "readmtcp readcs: checkpoint section %d next, expected %d\n", xcs, cs);
    abort ();
  }
}

static void readfile(int fd, void *buf, int size)
{
  int rc, ar;

  ar = 0;

  while(ar != size)
    {
      rc = read(fd, buf + ar, size - ar);
      if(rc < 0)
        {
	  fprintf(stderr, "readmtcp readfile: error reading checkpoint file: %s\n", strerror(errno));
	  abort();
        }
      else if(rc == 0)
	{
          fprintf(stderr, "readmtcp readfile: only read %d bytes instead of %d from checkpoint file\n", ar, size);
          abort();
	}

      ar += rc;
    }
}

static void skipfile(int fd, int size)
{
  int rc, ar;
  ar = 0;
  char array[512];

  while(ar != size)
    {
      rc = read(fd, array, (size-ar < 512 ? size - ar : 512));
      if(rc < 0)
        {
	  printf("readmtcp skipfile: error %d skipping checkpoint\n", errno);
	  exit(1);
        }
      else if(rc == 0)
        {
	  printf("readmtcp skipfile: only skipped %d bytes instead of %d from checkpoint file\n", ar, size);
	  exit(1);
        }

      ar += rc;
    }
}
