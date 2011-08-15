/****************************************************************************
 *   Copyright (C) 2006-2010 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *   This file is part of the dmtcp/src module of DMTCP (DMTCP:dmtcp/src).  *
 *                                                                          *
 *  DMTCP:dmtcp/src is free software: you can redistribute it and/or        *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP:dmtcp/src is distributed in the hope that it will be useful,      *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

// TODO: Better way to do this. I think it was only a problem on dekaksi.
// Remove this, and see the compile error.
#define read _libc_read
#include <stdarg.h>
#include <stdlib.h>
#include <vector>
#include <list>
#include <string>
#include <fcntl.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/version.h>
#include <limits.h>
#include "uniquepid.h"
#include "dmtcpworker.h"
#include "dmtcpmessagetypes.h"
#include "protectedfds.h"
#include "constants.h"
#include "connectionmanager.h"
#include "syscallwrappers.h"
#include "sysvipc.h"
#include "util.h"
#include  "../jalib/jassert.h"
#include  "../jalib/jconvert.h"

#ifdef RECORD_REPLAY
#include "fred_wrappers.h"
#include "synchronizationlogging.h"
#include <sys/mman.h>
#include <sys/syscall.h>
// TODO: hack to be able to compile this (fcntl wrapper).
#define open _libc_open
#define open64 _libc_open64
#define openat _libc_openat
#include <fcntl.h>
#undef open
#undef open64
#undef openat
#undef read

static __thread bool ok_to_log_readdir = false;

extern "C" int close ( int fd )
{
  BASIC_SYNC_WRAPPER(int, close, _almost_real_close, fd);
}

extern "C" int fclose(FILE *fp)
{
  WRAPPER_HEADER(int, fclose, _almost_real_fclose, fp);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_TYPED(int, fclose);
  } else if (SYNC_IS_RECORD) {
    isOptionalEvent = true;
    retval = _almost_real_fclose(fp);
    isOptionalEvent = false;
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

extern "C" int open (const char *path, int flags, ... )
{
  mode_t mode = 0;
  // Handling the variable number of arguments
  if (flags & O_CREAT) {
    va_list arg;
    va_start (arg, flags);
    mode = va_arg (arg, int);
    va_end (arg);
  }
  BASIC_SYNC_WRAPPER(int, open, _almost_real_open, path, flags, mode);
}

// FIXME: The 'fn64' version of functions is defined only when within
// __USE_LARGEFILE64 is #defined. The wrappers in this file need to consider
// this fact. The problem can occur, for example, when DMTCP is not compiled
// with __USE_LARGEFILE64 whereas the user-binary is. In that case the open64()
// call from user will come to DMTCP and DMTCP might fail to execute it
// properly.

// FIXME: Add the 'fn64' wrapper test cases to dmtcp test suite.
extern "C" int open64 (const char *path, int flags, ... )
{
  mode_t mode;
  // Handling the variable number of arguments
  if (flags & O_CREAT) {
    va_list arg;
    va_start (arg, flags);
    mode = va_arg (arg, int);
    va_end (arg);
  }
  BASIC_SYNC_WRAPPER(int, open64, _almost_real_open64, path, flags, mode);
}

extern "C" FILE *fdopen(int fd, const char *mode)
{
  WRAPPER_HEADER(FILE*, fdopen, _real_fdopen, fd, mode);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START_TYPED(FILE*, fdopen);
    if (retval != NULL) {
      *retval = GET_FIELD(currentLogEntry, fdopen, fdopen_retval);
    }
    WRAPPER_REPLAY_END(fdopen);
  } else if (SYNC_IS_RECORD) {
    isOptionalEvent = true;
    retval = _real_fdopen(fd, mode);
    isOptionalEvent = false;
    if (retval != NULL) {
      SET_FIELD2(my_entry, fdopen, fdopen_retval, *retval);
    }
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

#if 0
/* Until we fix the readdir() bug for tar, this is commented out.  If
   we don't comment this out (and fdopendir also), readdir() does not
   function properly in tar.  This is a "special case hack" for tar 1.26. */
// TODO: handle the variable argument here.
extern "C" int openat(int dirfd, const char *pathname, int flags, ...)
{
  BASIC_SYNC_WRAPPER(int, openat, _real_openat, dirfd, pathname, flags);
}
#endif

extern "C" DIR *opendir(const char *name)
{
  WRAPPER_HEADER(DIR*, opendir, _real_opendir, name);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_TYPED(DIR*, opendir);
    //TODO: May be we should restore data in *retval;
  } else if (SYNC_IS_RECORD) {
    isOptionalEvent = true;
    retval = _real_opendir(name);
    isOptionalEvent = false;
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

#if 0
/* Until we fix the readdir() bug for tar, this is commented out.  If
   we don't comment this out (and openat also), readdir() does not
   function properly in tar.  This is a "special case hack" for tar 1.26. */
extern "C" DIR *fdopendir(int fd)
{
  WRAPPER_HEADER(DIR*, fdopendir, _real_fdopendir, fd);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_TYPED(DIR*, fdopendir);
    //TODO: May be we should restore data in *retval;
  } else if (SYNC_IS_RECORD) {
    isOptionalEvent = true;
    retval = _real_fdopendir(fd);
    isOptionalEvent = false;
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}
#endif

extern "C" int closedir(DIR *dirp)
{
  WRAPPER_HEADER(int, closedir, _real_closedir, dirp);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY(closedir);
  } else if (SYNC_IS_RECORD) {
    isOptionalEvent = true;
    retval = _real_closedir(dirp);
    isOptionalEvent = false;
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

// WARNING:  Early versions of glibc (e.g. glibc 2.3) define this
//  function in stdio.h as inline.  This wrapper won't work in that case.
# if __GLIBC_PREREQ (2,4)
extern "C" ssize_t getline(char **lineptr, size_t *n, FILE *stream)
{
  WRAPPER_HEADER(ssize_t, getline, _real_getline, lineptr, n, stream);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START_TYPED(ssize_t, getline);
    if (retval != -1) {
      *lineptr = GET_FIELD(currentLogEntry, getline, new_lineptr);
      *n       = GET_FIELD(currentLogEntry, getline, new_n);
      WRAPPER_REPLAY_READ_FROM_READ_LOG(getline, *lineptr, *n);
    }
    WRAPPER_REPLAY_END(getline);
  } else if (SYNC_IS_RECORD) {
    isOptionalEvent = true;
    retval = _real_getline(lineptr, n, stream);
    isOptionalEvent = false;
    if (retval != -1) {
      SET_FIELD2(my_entry, getline, new_lineptr, *lineptr);
      SET_FIELD2(my_entry, getline, new_n, *n);
      WRAPPER_LOG_WRITE_INTO_READ_LOG(getline, *lineptr, *n);
    }
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}
# else
#  error getline() is already defined as inline in <stdio.h>.  Wrapper fails.
# endif

/* The list of strings: each string is a format, like for example %d or %lf.
 * This function deals with the following possible formats:
 * with or without whitespace delimited, eg: "%d%d" or "%d   %d".  */
static void parse_format (const char *format, dmtcp::list<dmtcp::string> *formats)
{
  int start = 0;
  size_t i;
  /* An argument format is delimited by expecting_start and expecting_end.
   * When expecting_start is true, that means we are about to begin a new
   * argument format. When expecting_end is true, we are expecting the
   * end of the argument format. expecting_start and expecting_end have
   * always opposite values. */
  bool expecting_start = true;
  bool expecting_end = false;
  char tmp[128];

  for ( i = 0; i < strlen(format); i++) {
    if (format[i] == '%') {
      if (expecting_end) {
        memset(tmp, 0, 128);
        memcpy(tmp, &format[start], i - start);
        formats->push_back(dmtcp::string(tmp));
        start = i;
      } else {
        start = i;
        expecting_end = true;
        expecting_start = false;
      }
      continue;
    }
    /* For formats like "%.2lf". */
    if (isdigit(format[i]) || format[i] == '.') continue;
    if (format[i] == ' ' || format[i] == '\t') {
      if (expecting_end) {
        expecting_end = false;
        expecting_start = true;
        memset(tmp, 0, 128);
        memcpy(tmp, &format[start], i - start);
        formats->push_back(dmtcp::string(tmp));
      }
      continue;
    }
  }
  /* This is for the last argument format in the list */
  if (!expecting_start && expecting_end) {
    memset(tmp, 0, 128);
    memcpy(tmp, &format[start], i - start);
    formats->push_back(dmtcp::string(tmp));
  }
}

/* For fscanf, for %5c like formats.
 * This function returns the number of characters read. */
static int get_how_many_characters (const char *str)
{
  /* The format has no integer conversion specifier, if the size of str is 2. */
  if (strlen(str) == 2) return 1;
  char tmp[512] = {'\0'};
  for (size_t i = 1; i < strlen(str) - 1; i++)
    tmp[i-1] = str[i];
  return atoi(tmp);
}

/* TODO: not all formats are mapped.
 * This function parses the given argument list and logs the values of the
 * arguments in the list to read_data_fd. Returns the number of bytes written. */
static int parse_va_list_and_log (va_list arg, const char *format)
{
  dmtcp::list<dmtcp::string> formats;
  parse_format (format, &formats);

  dmtcp::list<dmtcp::string>::iterator it;
  int bytes = 0;

  /* The list arg is made up of pointers to variables because the list arg
   * resulted as a call to fscanf. Thus we need to extract the address for
   * each argument and cast it to the corresponding type. */
  for (it = formats.begin(); it != formats.end(); it++) {
    /* Get next argument in the list. */
    long int *val = va_arg(arg, long int *);
    if (it->find("lf") != dmtcp::string::npos) {
      logReadData ((double *)val, sizeof(double));
      bytes += sizeof(double);
    }
    else if (it->find("d") != dmtcp::string::npos) {
      logReadData ((int *)val, sizeof(int));
      bytes += sizeof(int);
    }
    else if (it->find("c") != dmtcp::string::npos) {
      int nr_chars = get_how_many_characters(it->c_str());
      logReadData ((char *)val, nr_chars * sizeof(char));
      bytes += nr_chars * sizeof(char);
    }
    else if (it->find("s") != dmtcp::string::npos) {
      logReadData ((char *)val, strlen((char *)val)+ 1);
      bytes += strlen((char *)val) + 1;
    }
    else {
      JTRACE ("Format to add: ") (it->c_str());
      JASSERT (false).Text("format not added.");
    }
  }
  return bytes;
}

/* Parses the format string and reads into the given va_list of arguments.
  */
static void read_data_from_log_into_va_list (va_list arg, const char *format)
{
  dmtcp::list<dmtcp::string>::iterator it;
  dmtcp::list<dmtcp::string> formats;

  parse_format (format, &formats);
  /* The list arg is made up of pointers to variables because the list arg
   * resulted as a call to fscanf. Thus we need to extract the address for
   * each argument and cast it to the corresponding type. */
  for (it = formats.begin(); it != formats.end(); it++) {
    /* Get next argument in the list. */
    long int *val = va_arg(arg, long int *);
    if (it->find("lf") != dmtcp::string::npos) {
      _real_read(read_data_fd, (void *)val, sizeof(double));
    }
    else if (it->find("d") != dmtcp::string::npos) {
      _real_read(read_data_fd, (void *)val, sizeof(int));
    }
    else if (it->find("c") != dmtcp::string::npos) {
      int nr_chars = get_how_many_characters(it->c_str());
      _real_read(read_data_fd, (void *)val, nr_chars * sizeof(char));
    }
    else if (it->find("s") != dmtcp::string::npos) {
      bool terminate = false;
      int offset = 0;
      int i;
      char tmp[1024] = {'\0'};
      while (!terminate) {
        _real_read(read_data_fd, &tmp[offset], 128);
        for (i = 0; i < 128; i++) {
          if (tmp[i] == '\0') {
            terminate = true;
            break;
          }
        }
        if (!terminate) {
          offset += 128;
        }
      }
      /* We want to copy \0 at the end. */
      memcpy((void *)val, tmp, offset + i + 1);
      /* We want to be located one position to the right of \0. */
      _real_lseek(read_data_fd, i - 128 - 1, SEEK_CUR);
    }
    else {
      JASSERT (false).Text("format not added.");
    }
  }
}

/* fscanf seems to be #define'ed into this. */
extern "C" int __isoc99_fscanf (FILE *stream, const char *format, ...)
{
  va_list arg;
  va_start (arg, format);

  WRAPPER_HEADER(int, fscanf, vfscanf, stream, format, arg);

  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START(fscanf);
    if (retval != EOF) {
      if (__builtin_expect(read_data_fd == -1, 0)) {
        read_data_fd = _real_open(RECORD_READ_DATA_LOG_PATH, O_RDONLY, 0);
      }
      JASSERT ( read_data_fd != -1 );
      lseek(read_data_fd, GET_FIELD(currentLogEntry,fscanf,data_offset), SEEK_SET);
      read_data_from_log_into_va_list (arg, format);
      va_end(arg);
    }
    WRAPPER_REPLAY_END(fscanf);
  } else if (SYNC_IS_RECORD) {
    errno = 0;
    retval = vfscanf(stream, format, arg);
    int saved_errno = errno;
    va_end (arg);
    if (retval != EOF) {
      _real_pthread_mutex_lock(&read_data_mutex);
      SET_FIELD2(my_entry, fscanf, data_offset, read_log_pos);
      va_start (arg, format);
      int bytes = parse_va_list_and_log(arg, format);
      va_end (arg);
      SET_FIELD(my_entry, fscanf, bytes);
      _real_pthread_mutex_unlock(&read_data_mutex);
    }
    errno = saved_errno;
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

/* Here we borrow the data file used to store data returned from read() calls
   to store/replay the data for fgets() calls. */
extern "C" char *fgets(char *s, int size, FILE *stream)
{
  WRAPPER_HEADER(char *, fgets, _real_fgets, s, size, stream);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START_TYPED(char*, fgets);
    if (retval != NULL) {
      WRAPPER_REPLAY_READ_FROM_READ_LOG(fgets, s, size);
    }
    WRAPPER_REPLAY_END(fgets);
  } else if (SYNC_IS_RECORD) {
    isOptionalEvent = true;
    retval = _real_fgets(s, size, stream);
    isOptionalEvent = false;
    if (retval != NULL) {
      WRAPPER_LOG_WRITE_INTO_READ_LOG(fgets, s, size);
    }
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

static int _almost_real_fprintf(FILE *stream, const char *format, va_list arg)
{
  return vfprintf(stream, format, arg);
}

/* TODO: I think fprintf() is an inline function, so we can't wrap it directly.
   fprintf() internally calls this function, which happens to be exported.
   So we wrap this and have it call vfprintf(). We should think carefully about
   this and determine whether it is an acceptable solution or not. */
extern "C" int __fprintf_chk (FILE *stream, int flag, const char *format, ...)
{
  va_list arg;
  va_start (arg, format);
  WRAPPER_HEADER(int, fprintf, _almost_real_fprintf, stream, format, arg);

  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY(fprintf);
    /* If we're writing to stdout, we want to see the data to screen.
     * Thus execute the real system call. */
    // XXX We can't do this so easily. If we make the _real_printf call here,
    // it can call mmap() on replay at a different time as on record, since
    // the other FILE related syscalls are NOT made on replay.
    /*if (stream == stdout || stream == stderr) {
      retval = _almost_real_fprintf(stream, format, arg);
      }*/
    retval = (int)(unsigned long)GET_COMMON(currentLogEntry,
					    retval);
  } else if (SYNC_IS_RECORD) {
    isOptionalEvent = true;
    retval = _almost_real_fprintf(stream, format, arg);
    isOptionalEvent = false;
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

extern "C" int fprintf (FILE *stream, const char *format, ...)
{
  va_list arg;
  va_start (arg, format);
  WRAPPER_HEADER(int, fprintf, _almost_real_fprintf, stream, format, arg);

  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY(fprintf);
    /* If we're writing to stdout, we want to see the data to screen.
     * Thus execute the real system call. */
    // XXX We can't do this so easily. If we make the _real_printf call here,
    // it can call mmap() on replay at a different time as on record, since
    // the other FILE related syscalls are NOT made on replay.
    /*if (stream == stdout || stream == stderr) {
      retval = _almost_real_fprintf(stream, format, arg);
      }*/
    retval = (int)(unsigned long)GET_COMMON(currentLogEntry,
					    retval);
  } else if (SYNC_IS_RECORD) {
    isOptionalEvent = true;
    retval = _almost_real_fprintf(stream, format, arg);
    isOptionalEvent = false;
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

extern "C" int _IO_getc(FILE *stream)
{
  BASIC_SYNC_WRAPPER(int, getc, _real_getc, stream);
}

extern "C" int fgetc(FILE *stream)
{
  BASIC_SYNC_WRAPPER(int, fgetc, _real_fgetc, stream);
}

extern "C" int ungetc(int c, FILE *stream)
{
  BASIC_SYNC_WRAPPER(int, ungetc, _real_ungetc, c, stream);
}

extern "C" int fputs(const char *s, FILE *stream)
{
  WRAPPER_HEADER(int, fputs, _real_fputs, s, stream);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_TYPED(int, fputs);
  } else if (SYNC_IS_RECORD) {
    isOptionalEvent = true;
    retval = _real_fputs(s, stream);
    isOptionalEvent = false;
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

extern "C" int fputc(int c, FILE *stream)
{
  WRAPPER_HEADER(int, fputc, _real_fputc, c, stream);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_TYPED(int, fputc);
  } else if (SYNC_IS_RECORD) {
    isOptionalEvent = true;
    retval = _real_fputc(c, stream);
    isOptionalEvent = false;
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

extern "C" int _IO_putc(int c, FILE *stream)
{
  BASIC_SYNC_WRAPPER(int, putc, _real_putc, c, stream);
}

// WARNING:  Early versions of glibc (e.g. glibc 2.3) define this
//  function in stdio.h as inline.  This wrapper won't work in that case.
# if __GLIBC_PREREQ (2,4)
extern "C" int putchar(int c)
{
  return _IO_putc(c, stdout);
}
# else
#  error getline() is already defined as inline in <stdio.h>.  Wrapper fails.
# endif

extern "C" size_t fwrite(const void *ptr, size_t size, size_t nmemb,
    FILE *stream)
{
  BASIC_SYNC_WRAPPER(size_t, fwrite, _real_fwrite, ptr, size, nmemb, stream);
}

extern "C" void rewind(FILE *stream)
{
  BASIC_SYNC_WRAPPER_VOID(rewind, _real_rewind, stream);
}

extern "C" long ftell(FILE *stream)
{
  BASIC_SYNC_WRAPPER(long, ftell, _real_ftell, stream);
}

extern "C" FILE *fopen (const char* path, const char* mode)
{
  WRAPPER_HEADER(FILE *, fopen, _almost_real_fopen, path, mode);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_TYPED(FILE*, fopen);
    if (retval != NULL) {
      *retval = GET_FIELD(currentLogEntry, fopen, fopen_retval);
    }
  } else if (SYNC_IS_RECORD) {
    isOptionalEvent = true;
    retval = _almost_real_fopen(path, mode);
    isOptionalEvent = false;
    if (retval != NULL) {
      SET_FIELD2(my_entry, fopen, fopen_retval, *retval);
    }
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

extern "C" FILE *fopen64 (const char* path, const char* mode)
{
  WRAPPER_HEADER(FILE *, fopen64, _almost_real_fopen64, path, mode);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_TYPED(FILE*, fopen64);
    if (retval != NULL) {
      *retval = GET_FIELD(currentLogEntry, fopen64, fopen64_retval);
    }
  } else if (SYNC_IS_RECORD) {
    isOptionalEvent = true;
    retval = _almost_real_fopen64(path, mode);
    isOptionalEvent = false;
    if (retval != NULL) {
      SET_FIELD2(my_entry, fopen64, fopen64_retval, *retval);
    }
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

static int _almost_real_fcntl(int fd, int cmd, long arg_3_l, struct flock *arg_3_f)
{
  if (arg_3_l == -1 && arg_3_f == NULL) {
    return _real_fcntl(fd, cmd);
  } else if (arg_3_l == -1) {
    return _real_fcntl(fd, cmd, arg_3_f);
  } else {
    return _real_fcntl(fd, cmd, arg_3_l);
  }
}

extern "C" int fcntl(int fd, int cmd, ...)
{
  va_list ap;
  // Handling the variable number of arguments
  long arg_3_l = -1;
  struct flock *arg_3_f = NULL;
  va_start( ap, cmd );
  switch (cmd) {
  case F_DUPFD:
  //case F_DUP_FD_CLOEXEC:
  case F_SETFD:
  case F_SETFL:
  case F_SETOWN:
  case F_SETSIG:
  case F_SETLEASE:
  case F_NOTIFY:
    arg_3_l = va_arg ( ap, long );
    va_end ( ap );
    break;
  case F_GETFD:
  case F_GETFL:
  case F_GETOWN:
  case F_GETSIG:
  case F_GETLEASE:
    va_end ( ap );
    break;
  case F_SETLK:
  case F_SETLKW:
  case F_GETLK:
    arg_3_f = va_arg ( ap, struct flock *);
    va_end ( ap );
    break;
  default:
    break;
  }

  BASIC_SYNC_WRAPPER(int, fcntl, _almost_real_fcntl, fd, cmd, arg_3_l, arg_3_f);
}


#define _XSTAT_COMMON_SYNC_WRAPPER(name, ...)                               \
  do {                                                                      \
    if (SYNC_IS_REPLAY) {                                                   \
      WRAPPER_REPLAY_START(name);                                           \
      int saved_errno = GET_COMMON(currentLogEntry, my_errno);              \
      if (retval == 0 && buf != NULL) {                                     \
        *buf = GET_FIELD(currentLogEntry, name, buf);                       \
      }                                                                     \
      getNextLogEntry();                                                    \
      if (saved_errno != 0) {                                               \
        errno = saved_errno;                                                \
      }                                                                     \
    } else if (SYNC_IS_RECORD) {                                            \
      retval = _real_ ## name(__VA_ARGS__);                                 \
      if (retval != -1 && buf != NULL) {                                    \
        SET_FIELD2(my_entry, name, buf, *buf);                              \
      }                                                                     \
      WRAPPER_LOG_WRITE_ENTRY(my_entry);                                    \
    }                                                                       \
  }  while(0)

extern "C"
int __xstat(int vers, const char *path, struct stat *buf)
{
  WRAPPER_HEADER(int, xstat, _almost_real_xstat, vers, path, buf);
  _XSTAT_COMMON_SYNC_WRAPPER(xstat, vers, path, buf);
  return retval;
}

extern "C"
int __xstat64(int vers, const char *path, struct stat64 *buf)
{
  WRAPPER_HEADER(int, xstat64, _almost_real_xstat64, vers, path, buf);
  _XSTAT_COMMON_SYNC_WRAPPER(xstat64, vers, path, buf);
  return retval;
}

extern "C"
int __fxstat(int vers, int fd, struct stat *buf)
{
  WRAPPER_HEADER(int, fxstat, _real_fxstat, vers, fd, buf);
  _XSTAT_COMMON_SYNC_WRAPPER(fxstat, vers, fd, buf);
  return retval;
}

extern "C"
int __fxstat64(int vers, int fd, struct stat64 *buf)
{
  WRAPPER_HEADER(int, fxstat64, _real_fxstat64, vers, fd, buf);
  _XSTAT_COMMON_SYNC_WRAPPER(fxstat64, vers, fd, buf);
  return retval;
}

extern "C"
int __lxstat(int vers, const char *path, struct stat *buf)
{
  WRAPPER_HEADER(int, lxstat, _almost_real_lxstat, vers, path, buf);
  _XSTAT_COMMON_SYNC_WRAPPER(lxstat, vers, path, buf);
  return retval;
}

extern "C"
int __lxstat64(int vers, const char *path, struct stat64 *buf)
{
  WRAPPER_HEADER(int, lxstat64, _almost_real_lxstat64, vers, path, buf);
  _XSTAT_COMMON_SYNC_WRAPPER(lxstat64, vers, path, buf);
  return retval;
}

#if __GLIBC_PREREQ(2,5)
# define READLINK_RET_TYPE ssize_t
#else
# define READLINK_RET_TYPE int
#endif

extern "C" READLINK_RET_TYPE readlink(const char *path, char *buf,
                                      size_t bufsiz)
{
  WRAPPER_HEADER(READLINK_RET_TYPE, readlink, _almost_real_readlink,
                 path, buf, bufsiz);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START_TYPED(READLINK_RET_TYPE, readlink);
    if (retval == 0 && buf != NULL) {
      WRAPPER_REPLAY_READ_FROM_READ_LOG(readlink, buf, retval);
    }
    WRAPPER_REPLAY_END(readlink);
  } else if (SYNC_IS_RECORD) {
    retval = _almost_real_readlink(path, buf, bufsiz);
    if (retval != -1 && buf != NULL) {
      WRAPPER_LOG_WRITE_INTO_READ_LOG(getline, buf, retval);
    }
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

extern "C" int select(int nfds, fd_set *readfds, fd_set *writefds,
                      fd_set *exceptfds, struct timeval *timeout)
{
  WRAPPER_HEADER(int, select, _real_select, nfds, readfds, writefds, exceptfds, timeout);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START(select);
    if (retval != -1) {
      copyFdSet(&GET_FIELD(currentLogEntry, select, readfds), readfds);
      copyFdSet(&GET_FIELD(currentLogEntry, select, writefds), writefds);
    }
    WRAPPER_REPLAY_END(select);
  } else if (SYNC_IS_RECORD) {
    retval = _real_select(nfds, readfds, writefds, exceptfds, timeout);
    int saved_errno = errno;
    if (retval != -1) {
      // Note that we're logging the *changed* fd set, so on replay we can
      // just read that from the log, load it into user's location and return.
      copyFdSet(readfds, &GET_FIELD(my_entry, select, readfds));
      copyFdSet(writefds, &GET_FIELD(my_entry, select, writefds));
    }
    errno = saved_errno;
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

extern "C" ssize_t read(int fd, void *buf, size_t count)
{
  if (dmtcp::ProtectedFDs::isProtected(fd)) {
    return _real_read(fd, buf, count);
  }

  WRAPPER_HEADER(ssize_t, read, _real_read, fd, buf, count);

  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START_TYPED(ssize_t, read);
    // NOTE: We never actually call the user's _real_read. We don't
    // need to. We wait for the next event in the log that is the
    // READ_data_event, read from the read data log, and return the
    // corresponding value.
    if (retval > 0) {
      WRAPPER_REPLAY_READ_FROM_READ_LOG(read, buf, retval);
    }
    WRAPPER_REPLAY_END(read);
  } else if (SYNC_IS_RECORD) {
    // Note we don't call readAll here. It should be the responsibility of
    // the user code to handle EINTR if needed.
    retval = _real_read(fd, buf, count);
    if (retval > 0) {
      WRAPPER_LOG_WRITE_INTO_READ_LOG(read, buf, retval);
    }
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

extern "C" ssize_t write(int fd, const void *buf, size_t count)
{
  if (dmtcp::ProtectedFDs::isProtected(fd)) {
    return _real_write(fd, buf, count);
  }
  BASIC_SYNC_WRAPPER(ssize_t, write, _real_write, fd, buf, count);
}

extern "C" ssize_t pread(int fd, void *buf, size_t count, off_t offset)
{
  if (dmtcp::ProtectedFDs::isProtected(fd)) {
    return _real_pread(fd, buf, count, offset);
  }
  WRAPPER_HEADER(ssize_t, pread, _real_pread, fd, buf, count, offset);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START_TYPED(ssize_t, pread);
    if (retval > 0) {
      WRAPPER_REPLAY_READ_FROM_READ_LOG(pread, buf, retval);
    }
    WRAPPER_REPLAY_END(pread);
  } else if (SYNC_IS_RECORD) {
    retval = _real_pread(fd, buf, count, offset);
    if (retval > 0) {
      WRAPPER_LOG_WRITE_INTO_READ_LOG(pread, buf, retval);
    }
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

extern "C" ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset)
{
  if (dmtcp::ProtectedFDs::isProtected(fd)) {
    return _real_pwrite(fd, buf, count, offset);
  }
  BASIC_SYNC_WRAPPER(ssize_t, pwrite, _real_pwrite, fd, buf, count, offset);
}

extern "C" int access(const char *pathname, int mode)
{
  BASIC_SYNC_WRAPPER(int, access, _real_access, pathname, mode);
}

extern "C" int dup(int oldfd)
{
  BASIC_SYNC_WRAPPER(int, dup, _real_dup, oldfd);
}

extern "C" int dup2(int oldfd, int newfd)
{
  BASIC_SYNC_WRAPPER(int, dup2, _real_dup2, oldfd, newfd);
}

extern "C" int dup3(int oldfd, int newfd, int flags)
{
  BASIC_SYNC_WRAPPER(int, dup3, _real_dup3, oldfd, newfd, flags);
}

extern "C" off_t lseek(int fd, off_t offset, int whence)
{
  BASIC_SYNC_WRAPPER(off_t, lseek, _real_lseek, fd, offset, whence);
}

extern "C" int unlink(const char *pathname)
{
  BASIC_SYNC_WRAPPER(int, unlink, _real_unlink, pathname);
}

extern "C" int fdatasync(int fd)
{
  BASIC_SYNC_WRAPPER(int, fdatasync, _real_fdatasync, fd);
}

extern "C" int fsync(int fd)
{
  BASIC_SYNC_WRAPPER(int, fsync, _real_fsync, fd);
}

extern "C" int link(const char *oldpath, const char *newpath)
{
  BASIC_SYNC_WRAPPER(int, link, _real_link, oldpath, newpath);
}

extern "C" int rename(const char *oldpath, const char *newpath)
{
  BASIC_SYNC_WRAPPER(int, rename, _real_rename, oldpath, newpath);
}

extern "C" int rmdir(const char *pathname)
{
  BASIC_SYNC_WRAPPER(int, rmdir, _real_rmdir, pathname);
}

extern "C" int mkdir(const char *pathname, mode_t mode)
{
  BASIC_SYNC_WRAPPER(int, mkdir, _real_mkdir, pathname, mode);
}

extern "C" struct dirent * /*__attribute__ ((optimize(0)))*/ readdir(DIR *dirp)
{
  static __thread struct dirent buf;
  struct dirent *ptr;
  int res = 0;

  //ok_to_log_readdir = true;
  res = readdir_r(dirp, &buf, &ptr);
  //ok_to_log_readdir = false;

  if (res == 0) {
    return ptr;
  }

  return NULL;
}
#if 0



  //WRAPPER_HEADER(struct dirent*, readdir, _real_readdir, dirp);
  void *return_addr = GET_RETURN_ADDRESS();
  do {
    if (!shouldSynchronize(return_addr) ||
        jalib::Filesystem::GetProgramName() == "gdb") {
      return _real_readdir(dirp);
    }
  } while(0);
  struct dirent* retval = NULL;
  log_entry_t my_entry = create_readdir_entry(my_clone_id,
                                              readdir_event, dirp);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_TYPED(struct dirent*, readdir);
    if (retval != NULL) {
      buf = GET_FIELD(currentLogEntry, readdir, retval);
    }
  } else if (SYNC_IS_RECORD) {
    retval = _real_readdir(dirp);
    if (retval != NULL) {
      buf = *retval;
      SET_FIELD2(my_entry, readdir, retval, buf);
    }
    //WRAPPER_LOG_WRITE_ENTRY(my_entry);
    do {
      SET_COMMON2(my_entry, retval, (void*)retval);
      SET_COMMON2(my_entry, my_errno, errno);
      SET_COMMON2(my_entry, isOptional, isOptionalEvent);
      addNextLogEntry(my_entry);
      errno = GET_COMMON(my_entry, my_errno);
    } while (0);
  }
  return retval == NULL ? retval : &buf;
}
#endif // if 0

extern "C" int readdir_r(DIR *dirp, struct dirent *entry,
                         struct dirent **result)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if ((!shouldSynchronize(return_addr) ||
       jalib::Filesystem::GetProgramName() == "gdb") &&
      !ok_to_log_readdir) {
    return _real_readdir_r(dirp, entry, result);
  }
  int retval;
  log_entry_t my_entry = create_readdir_r_entry(my_clone_id,
      readdir_r_event, dirp, entry, result);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START(readdir_r);
    if (retval == 0 && entry != NULL) {
      *entry = GET_FIELD(currentLogEntry, readdir_r, ret_entry);
    }
    if (retval == 0 && result != NULL) {
      *result = GET_FIELD(currentLogEntry, readdir_r, ret_result);
    }
    if (retval != 0) {
      *result = NULL;
    }
    WRAPPER_REPLAY_END(readdir_r);
  } else if (SYNC_IS_RECORD) {
    retval = _real_readdir_r(dirp, entry, result);
    if (retval == 0 && entry != NULL) {
      SET_FIELD2(my_entry, readdir_r, ret_entry, *entry);
    }
    if (retval == 0 && result != NULL) {
      SET_FIELD2(my_entry, readdir_r, ret_result, *result);
    }
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

extern "C" int mkstemp(char *temp)
{
  BASIC_SYNC_WRAPPER(int, mkstemp, _real_mkstemp, temp);
}

extern "C" int fflush(FILE *stream)
{
  WRAPPER_HEADER(int, fflush, _real_fflush, stream);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY(fflush);
    /* If the stream is stdout, we want to see the data to screen.
     * Thus execute the real system call. */
    // XXX We can't do this so easily. If we make the _real_fflush call here,
    // it can call mmap() on replay at a different time as on record, since
    // the other FILE related syscalls are NOT made on replay.
    /*if (stream == stdout || stream == stderr) {
      retval = _real_fflush(stream);
      }*/
  } else if (SYNC_IS_RECORD) {
    retval = _real_fflush(stream);
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

// FIXME: Ask Ana to write this wrapper.
#ifdef PID_VIRTUALIZATION
extern int send_sigwinch;

void ioctl_helper(log_entry_t &my_entry, int &retval, int d, int request,
                  void *arg) {
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY(ioctl);
    switch (request) {
      case SIOCGIFCONF: {
        *((struct ifconf *)arg) = GET_FIELD(currentLogEntry,ioctl,ifconf_val);
        struct ifconf *i = (struct ifconf *)arg;
        WRAPPER_REPLAY_READ_FROM_READ_LOG(ioctl, i->ifc_buf, i->ifc_len);
        break;
      }
      case TIOCGWINSZ: {
        *((struct winsize *)arg) = GET_FIELD(currentLogEntry,ioctl,win_val);
        break;
      }
      default:
        break;
    }
  } else if (SYNC_IS_RECORD) {
    retval = _real_ioctl(d, request, arg);
    switch (request) {
      case SIOCGIFCONF: {
        SET_FIELD2(my_entry, ioctl, ifconf_val, *((struct ifconf *)arg));
        struct ifconf *i = (struct ifconf *)arg;
        WRAPPER_LOG_WRITE_INTO_READ_LOG(ioctl, i->ifc_buf, i->ifc_len);
        break;
      }
      case TIOCGWINSZ: {
        SET_FIELD2(my_entry, ioctl, win_val, *((struct winsize *)arg));
        break;
      }
      default: {
        break;
      }
    }
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
}

extern "C" int ioctl(int d,  unsigned long int request, ...)
{
  va_list ap;
  int retval;

  if (send_sigwinch && request == TIOCGWINSZ) {
    send_sigwinch = 0;
    va_list local_ap;
    va_copy(local_ap, ap);
    va_start(local_ap, request);
    struct winsize * win = va_arg(local_ap, struct winsize *);
    va_end(local_ap);
    WRAPPER_HEADER(int, ioctl, _real_ioctl, d, request, win);
    ioctl_helper(my_entry, retval, d, request, win);
    win->ws_col--; // Lie to application, and force it to resize window,
		   //  reset any scroll regions, etc.
    kill(getpid(), SIGWINCH); // Tell application to look up true winsize
			      // and resize again.
  } else {
    void * arg;
    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);
    WRAPPER_HEADER(int, ioctl, _real_ioctl, d, request, arg);
    ioctl_helper(my_entry, retval, d, request, arg);
  }
  return retval;
}
#endif
#endif
