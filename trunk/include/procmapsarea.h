#ifndef PROCMAPSAREA_H
#define PROCMAPSAREA_H
#include <stdint.h>
#include <sys/types.h>

// MTCP_PAGE_SIZE must be page-aligned:  multiple of sysconf(_SC_PAGESIZE).
#define MTCP_PAGE_SIZE 4096
#define MTCP_PAGE_MASK (~(MTCP_PAGE_SIZE-1))
#define MTCP_PAGE_OFFSET_MASK (MTCP_PAGE_SIZE-1)
#define FILENAMESIZE 1024

#ifndef HIGHEST_VA
// If 32-bit process in 64-bit Linux, then Makefile overrides this address,
// with correct address for that case.
# ifdef __x86_64__
 /* There's a segment, 7fbfffb000-7fc0000000 rw-p 7fbfffb000 00:00 0;
  * What is it?  It's busy (EBUSY) when we try to unmap it.
  */
// #  define HIGHEST_VA ((VA)0xFFFFFF8000000000)
// #  define HIGHEST_VA ((VA)0x8000000000)
#  define HIGHEST_VA ((VA)0x7f00000000)
# else
#  define HIGHEST_VA ((VA)0xC0000000)
# endif
#endif

#define NSCD_MMAP_STR1 "/var/run/nscd/"   /* OpenSUSE*/
#define NSCD_MMAP_STR2 "/var/cache/nscd"  /* Debian / Ubuntu*/
#define NSCD_MMAP_STR3 "/var/db/nscd"     /* RedHat / Fedora*/
#define DEV_ZERO_DELETED_STR "/dev/zero (deleted)"
#define DEV_NULL_DELETED_STR "/dev/null (deleted)"
#define SYS_V_SHMEM_FILE "/SYSV"
#define INFINIBAND_SHMEM_FILE "/dev/infiniband/uverbs"

/* Shared memory regions for Direct Rendering Infrastructure */
#define DEV_DRI_SHMEM "/dev/dri/card"

#define DELETED_FILE_SUFFIX " (deleted)"

/* Let MTCP_PROT_ZERO_PAGE be a unique bit mask
 * This assumes: PROT_READ == 0x1, PROT_WRITE == 0x2, and PROT_EXEC == 0x4
 */
#define MTCP_PROT_ZERO_PAGE (PROT_EXEC << 1)

#define FILENAMESIZE 1024

typedef char * VA; /* VA = virtual address */

typedef union ProcMapsArea {
  struct {
    union {
      VA addr;   // args required for mmap to restore memory area
      uint64_t __addr;
    };
    union {
      VA endAddr;   // args required for mmap to restore memory area
      uint64_t __endAddr;
    };
    union {
      size_t size;
      uint64_t __size;
    };
    union {
      off_t filesize;
      uint64_t __filesize;
    };
    union {
      off_t offset;
      uint64_t __offset;
    };
    union {
      int prot;
      uint64_t __prot;
    };
    union {
      int flags;
      uint64_t __flags;
    };
    union {
      unsigned int long devmajor;
      uint64_t __devmajor;
    };
    union {
      unsigned int long devminor;
      uint64_t __devminor;
    };
    union {
      ino_t inodenum;
      uint64_t __inodenum;
    };
    char name[FILENAMESIZE];
  };
  char _padding[4096];
} ProcMapsArea;

typedef ProcMapsArea Area;

#endif
