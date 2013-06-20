/* Short utility to read sections of .mtcp file.
 * It is primarily useful to developers.
 * It is not used by the rest of MTCP.
 * To compile:  gcc -o readmtcp readmtcp.c
 */

#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/resource.h>
#include "mtcp_internal.h"
#include "mtcp_util.h"

int mtcp_restore_cpfd = -1; // '= -1' puts it in regular data instead of common

static const char* theUsage =
  "USAGE:\n"
  "readmtcp <ckpt_image_filename>\n"
  "OR: gzip -dc <ckpt_image_filename> | ./readmtcp -\n"
  "OR: readmtcp OPTION\n"
  "for OPTION=\n"
  "  --help:      Print this message and exit.\n"
  "  --version:   Print version information and exit.\n"
  "\n"
;

int main(int argc, char **argv)
{
  int fd = -1;
  char magicbuf[MAGIC_LEN], *restorename;
  char *version = PACKAGE_VERSION;
  Area area;

  restorename = argv[1];
  if (argc == 1 || (strcmp(argv[1], "--help") == 0 && argc == 2)) {
    printf("%s", theUsage);
    return 1;
  } else if (strcmp (argv[1], "--version") == 0 && argc == 2) {
    printf("%s\n", (version[0] == '\0' ? "Standalone MTCP" : version));
    return 1;
  } else if (restorename[0] == '-' && restorename[1] == '\0') {
    fd = 0; /* read from stdin */
  } else {    /* argv[1] should be a real filename */
    if (-1 == (fd = open(restorename, O_RDONLY))) {
      perror("open");
      return 1; }
  }

  memset(magicbuf, 0, sizeof magicbuf);
  mtcp_readfile(fd, magicbuf, MAGIC_LEN);
  if (memcmp (magicbuf, "DMTCP_CHECKPOINT", MAGIC_LEN) == 0) {
    while (memcmp(magicbuf, MAGIC, MAGIC_LEN) != 0) {
      int i;
      for (i = 0; i < MAGIC_LEN-1; i++)
        magicbuf[i] = magicbuf[i+1];
      magicbuf[MAGIC_LEN-1] = '\0'; /* MAGIC should be a string w/o '\0' */
      mtcp_readfile(fd, magicbuf+(MAGIC_LEN-1), 1);
    }
  }
  if (memcmp (magicbuf, MAGIC, MAGIC_LEN) != 0) {
    char command[PATH_MAX];
    assert(strlen(argv[0]) < PATH_MAX);
    fprintf (stderr, "readmtcp: Not an uncompressed image;"
                     " trying it as gzip compressed image.\n");
    sprintf (command, "gzip -dc %s | %s -", restorename, argv[0]);
    exit( system(command) );
  }

  char tmpBuf[MTCP_PAGE_SIZE];
  mtcp_readfile(fd, &tmpBuf, MTCP_PAGE_SIZE - MAGIC_LEN);
  mtcp_ckpt_image_hdr_t *ckpt_hdr = (mtcp_ckpt_image_hdr_t*) tmpBuf;

  printf("mtcp_restart: saved stack resource limit:" \
	 " soft_lim: %lu, hard_lim: %lu\n",
	 ckpt_hdr->stack_rlimit.rlim_cur, ckpt_hdr->stack_rlimit.rlim_max);

  printf("*** restored libmtcp.so\n");
  mtcp_skipfile (fd, ckpt_hdr->libmtcp_size);

  printf("%p-%p rwxp %p 00:00 0          [libmtcp.so]\n",
	ckpt_hdr->libmtcp_begin,
        ckpt_hdr->libmtcp_begin + ckpt_hdr->libmtcp_size,
        ckpt_hdr->libmtcp_begin);
  printf("restore_start routine: %p\n", ckpt_hdr->restore_start_fptr);
  printf("restore_finish routine: %p\n", ckpt_hdr->restore_finish_fptr);

  // Skip over file descriptors.
  do {
    mtcp_readfile(fd, &area, sizeof area);
  } while (area.fdinfo.fdnum >= 0);

  printf("*** memory sections\n");
  while(1) {
    mtcp_readfile(fd, &area, sizeof area);
    if (area.size == -1) break;
    if ((area.prot & MTCP_PROT_ZERO_PAGE) == 0) {
      mtcp_skipfile (fd, area.size);
    }
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
