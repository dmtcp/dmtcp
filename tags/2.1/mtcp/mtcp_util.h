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

#include "mtcp_internal.h"

#define MIN(x,y) ((x) < (y) ? (x) : (y))
#define MAX(x,y) ((x) > (y) ? (x) : (y))

void mtcp_readcs (int fd, char cs);
void mtcp_readfile(int fd, void *buf, size_t size);
void mtcp_skipfile(int fd, size_t size);
void mtcp_writecs (int fd, char cs);
size_t mtcp_writefile (int fd, void const *buff, size_t size);
void mtcp_check_vdso_enabled(void);
int  mtcp_is_executable(const char *exec_path);
char *mtcp_find_executable(char *filename, const char* path_env,
                           char exec_path[PATH_MAX]);
char mtcp_readchar (int fd);
char mtcp_readdec (int fd, VA *value);
char mtcp_readhex (int fd, VA *value);
ssize_t mtcp_read_all(int fd, void *buf, size_t count);
ssize_t mtcp_write_all(int fd, const void *buf, size_t count);
size_t mtcp_strlen (const char *s1);
const void *mtcp_strstr(const char *string, const char *substring);
void mtcp_strncpy(char *targ, const char* source, size_t len);
void mtcp_strcpy(char *dest, const char *src);
void mtcp_strncat(char *dest, const char *src, size_t n);
int mtcp_memcmp(const char *s1, const char* s2, size_t len);
void mtcp_memset(char *targ, int c, size_t n);
int mtcp_strncmp (const char *s1, const char *s2, size_t n);
int mtcp_strcmp (const char *s1, const char *s2);
int mtcp_strstartswith (const char *s1, const char *s2);
int mtcp_strendswith (const char *s1, const char *s2);
int mtcp_atoi(const char *nptr);
int mtcp_get_controlling_term(char* ttyName, size_t len);
const char* mtcp_getenv(const char* name);
void mtcp_rename_ckptfile(const char *tempckpt, const char *permckpt);
int mtcp_readmapsline (int mapsfd, Area *area, DeviceInfo *dev_info);
void mtcp_get_memory_region_of_this_library(VA *startaddr, VA *endaddr);
#endif
