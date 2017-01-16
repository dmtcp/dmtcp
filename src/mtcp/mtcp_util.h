/*****************************************************************************
 * Copyright (C) 2010-2014 Kapil Arya <kapil@ccs.neu.edu>                    *
 * Copyright (C) 2010-2014 Gene Cooperman <gene@ccs.neu.edu>                 *
 *                                                                           *
 * DMTCP is free software: you can redistribute it and/or                    *
 * modify it under the terms of the GNU Lesser General Public License as     *
 * published by the Free Software Foundation, either version 3 of the        *
 * License, or (at your option) any later version.                           *
 *                                                                           *
 * DMTCP is distributed in the hope that it will be useful,                  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of            *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             *
 * GNU Lesser General Public License for more details.                       *
 *                                                                           *
 * You should have received a copy of the GNU Lesser General Public          *
 * License along with DMTCP.  If not, see <http://www.gnu.org/licenses/>.    *
 *****************************************************************************/

#ifndef _MTCP_UTIL_H
#define _MTCP_UTIL_H

#include "procmapsarea.h"

#define MTCP_PRINTF(args ...)                                               \
  do {                                                                      \
    mtcp_printf("[%d] %s:%d %s:\n  ",                                       \
                mtcp_sys_getpid(), __FILE__, __LINE__, __FUNCTION__);       \
    (void)mtcp_sys_errno; /* prevent compiler warning if we don't use it */ \
    mtcp_printf(args);                                                      \
  } while (0)

#define MTCP_ASSERT(condition)                          \
  if (!(condition)) {                                   \
    MTCP_PRINTF("Assertion failed: %s\n", # condition); \
    mtcp_abort();                                       \
  }

#ifdef LOGGING
# define DPRINTF MTCP_PRINTF
#else // ifdef LOGGING
# define DPRINTF(args ...) // debug printing
#endif // ifdef LOGGING

#if 0

// From glibc-2.5/sysdeps/generic/memcopy.h:BYTE_COPY_FWD
// From glibc-2.5/sysdeps/generic/memcopy.h:BYTE_COPY_BWD
# define MTCP_BYTE_COPY_FWD(dst_bp, src_bp, nbytes) \
  do {                                              \
    size_t __nbytes = (nbytes);                     \
    while (__nbytes > 0) {                          \
      byte __x = ((byte *)src_bp)[0];               \
      src_bp += 1;                                  \
      __nbytes -= 1;                                \
      ((byte *)dst_bp)[0] = __x;                    \
      dst_bp += 1;                                  \
    }                                               \
  } while (0)
# define MTCP_BYTE_COPY_BWD(dst_ep, src_ep, nbytes) \
  do {                                              \
    size_t __nbytes = (nbytes);                     \
    while (__nbytes > 0) {                          \
      byte __x;                                     \
      src_ep -= 1;                                  \
      __x = ((byte *)src_ep)[0];                    \
      dst_ep -= 1;                                  \
      __nbytes -= 1;                                \
      ((byte *)dst_ep)[0] = __x;                    \
    }                                               \
  } while (0)

# ifdef MTCP_SYS_MEMMOVE
#  ifndef _MTCP_MEMMOVE_
#   define _MTCP_MEMMOVE_

// From glibc-2.5/string/memmove.c
static void *
mtcp_sys_memmove(void *a1, const void *a2, size_t len)
{
  unsigned long int dstp = (long int)a1 /* dest */;
  unsigned long int srcp = (long int)a2 /* src */;

  /* This test makes the forward copying code be used whenever possible.
     Reduces the working set.  */
  if (dstp - srcp >= len) {     /* *Unsigned* compare!  */
    /* Copy from the beginning to the end.  */

    /* There are just a few bytes to copy.  Use byte memory operations.  */
    MTCP_BYTE_COPY_FWD(dstp, srcp, len);
  } else {
    /* Copy from the end to the beginning.  */
    srcp += len;
    dstp += len;

    /* There are just a few bytes to copy.  Use byte memory operations.  */
    MTCP_BYTE_COPY_BWD(dstp, srcp, len);
  }

  return a1 /* dest */;
}
#  endif // ifndef _MTCP_MEMMOVE_
# endif // ifdef MTCP_SYS_MEMMOVE

# ifdef MTCP_SYS_MEMCPY
#  ifndef _MTCP_MEMCPY_
#   define _MTCP_MEMCPY_

// From glibc-2.5/string/memcpy.c; and

/* Copy exactly NBYTES bytes from SRC_BP to DST_BP,
   without any assumptions about alignment of the pointers.  */
static void *
mtcp_sys_memcpy(void *dstpp, const void *srcpp, size_t len)
{
  unsigned long int dstp = (long int)dstpp;
  unsigned long int srcp = (long int)srcpp;

  /* SHOULD DO INITIAL WORD COPY BEFORE THIS. */
  /* There are just a few bytes to copy.  Use byte memory operations.  */
  MTCP_BYTE_COPY_FWD(dstp, srcp, len);
  return dstpp;
}
#  endif // ifndef _MTCP_MEMCPY_
# endif // ifdef MTCP_SYS_MEMCPY
# if 0 /*  DEMONSTRATE_BUG */

// From glibc-2.5/string/memcmp.c:memcmp at end.
#  ifndef _MTCP_MEMCMP_
#   define _MTCP_MEMCMP_
static int
  mtcp_sys_memcmp(s1, s2, len)
const __ptr_t s1;
const __ptr_t s2;
size_t len;
{
  op_t a0;
  op_t b0;
  long int srcp1 = (long int)s1;
  long int srcp2 = (long int)s2;
  op_t res;

  /* There are just a few bytes to compare.  Use byte memory operations.  */
  while (len != 0) {
    a0 = ((byte *)srcp1)[0];
    b0 = ((byte *)srcp2)[0];
    srcp1 += 1;
    srcp2 += 1;
    res = a0 - b0;
    if (res != 0) {
      return res;
    }
    len -= 1;
  }

  return 0;
}
#  endif // ifndef _MTCP_MEMCMP_
# endif /* DEMONSTRATE_BUG */
#endif // if 0

void mtcp_printf(char const *format, ...);
ssize_t mtcp_read_all(int fd, void *buf, size_t count);
int mtcp_readfile(int fd, void *buf, size_t size);
void mtcp_skipfile(int fd, size_t size);
unsigned long mtcp_strtol(char *str);
char mtcp_readchar(int fd);
char mtcp_readdec(int fd, VA *value);
char mtcp_readhex(int fd, VA *value);
ssize_t mtcp_write_all(int fd, const void *buf, size_t count);
size_t mtcp_strlen(const char *s1);
const void *mtcp_strstr(const char *string, const char *substring);
void mtcp_strncpy(char *targ, const char *source, size_t len);
void mtcp_strcpy(char *dest, const char *src);
void mtcp_strncat(char *dest, const char *src, size_t n);
int mtcp_strncmp(const char *s1, const char *s2, size_t n);
int mtcp_strcmp(const char *s1, const char *s2);
char *mtcp_strchr(const char *s, int c);
int mtcp_strstartswith(const char *s1, const char *s2);
int mtcp_strendswith(const char *s1, const char *s2);
int mtcp_readmapsline(int mapsfd, Area *area);
void mtcp_sys_memcpy(void *dstpp, const void *srcpp, size_t len);
#endif // ifndef _MTCP_UTIL_H
