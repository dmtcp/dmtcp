/*****************************************************************************
 *   Copyright (C) 2006-2009 by Michael Rieker, Jason Ansel, Kapil Arya, and *
 *                                                            Gene Cooperman *
 *   mrieker@nii.net, jansel@csail.mit.edu, kapil@ccs.neu.edu, and           *
 *                                                          gene@ccs.neu.edu *
 *                                                                           *
 *   This file is part of the MTCP module of DMTCP (DMTCP:mtcp).             *
 *                                                                           *
 *  DMTCP:mtcp is free software: you can redistribute it and/or              *
 *  modify it under the terms of the GNU Lesser General Public License as    *
 *  published by the Free Software Foundation, either version 3 of the       *
 *  License, or (at your option) any later version.                          *
 *                                                                           *
 *  DMTCP:dmtcp/src is distributed in the hope that it will be useful,       *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *  GNU Lesser General Public License for more details.                      *
 *                                                                           *
 *  You should have received a copy of the GNU Lesser General Public         *
 *  License along with DMTCP:dmtcp/src.  If not, see                         *
 *  <http://www.gnu.org/licenses/>.                                          *
 *****************************************************************************/

#ifndef _MTCP_UTIL_H
#define _MTCP_UTIL_H

typedef char * VA; /* VA = virtual address */

#define FILENAMESIZE 1024
typedef union Area {
  struct {
  int type; // Content type (CS_XXX
  char *addr;   // args required for mmap to restore memory area
  size_t size;
  off_t filesize;
  int prot;
  int flags;
  off_t offset;
  char name[FILENAMESIZE];
  };
  char _padding[4096];
} Area;

typedef struct DeviceInfo {
  unsigned int long devmajor;
  unsigned int long devminor;
  unsigned int long inodenum;
} DeviceInfo;

#define MTCP_PRINTF(args...) \
  do { \
    int mtcp_sys_errno; \
    mtcp_printf("[%d] %s:%d %s:\n  ", \
                mtcp_sys_getpid(), __FILE__, __LINE__, __FUNCTION__); \
    mtcp_printf(args); \
  } while (0)

#define MTCP_ASSERT(condition) \
  if (! (condition)) { \
    MTCP_PRINTF("Assertion failed: %s\n", #condition); \
    mtcp_abort(); \
  }

#ifdef DEBUG
# define DPRINTF MTCP_PRINTF
#else
# define DPRINTF(args...) // debug printing
#endif

void mtcp_printf (char const *format, ...);
void mtcp_readfile(int fd, void *buf, size_t size);
void mtcp_skipfile(int fd, size_t size);
unsigned long mtcp_strtol (char *str);
char mtcp_readchar (int fd);
char mtcp_readdec (int fd, VA *value);
char mtcp_readhex (int fd, VA *value);
ssize_t mtcp_write_all(int fd, const void *buf, size_t count);
size_t mtcp_strlen (const char *s1);
const void *mtcp_strstr(const char *string, const char *substring);
void mtcp_strncpy(char *targ, const char* source, size_t len);
void mtcp_strcpy(char *dest, const char *src);
void mtcp_strncat(char *dest, const char *src, size_t n);
int mtcp_strncmp (const char *s1, const char *s2, size_t n);
int mtcp_strcmp (const char *s1, const char *s2);
char *mtcp_strchr(const char *s, int c);
int mtcp_strstartswith (const char *s1, const char *s2);
int mtcp_strendswith (const char *s1, const char *s2);
int mtcp_readmapsline (int mapsfd, Area *area, DeviceInfo *dev_info);
void mtcp_sys_memcpy (void *dstpp, const void *srcpp, size_t len);
#endif
