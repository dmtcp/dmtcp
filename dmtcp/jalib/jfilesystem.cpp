/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel                                 *
 *   jansel@csail.mit.edu                                                   *
 *                                                                          *
 *   This file is part of the JALIB module of DMTCP (DMTCP:dmtcp/jalib).    *
 *                                                                          *
 *  DMTCP:dmtcp/jalib is free software: you can redistribute it and/or      *
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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <algorithm>
#include <errno.h>
#include <sys/utsname.h>
#include <sys/syscall.h>
#include "jfilesystem.h"
#include "jconvert.h"
#include "syscallwrappers.h"
#include "util.h"

namespace
{
  // In Red Hat Enterprise Linux Server 5.4 (Linux kernel 2.6.18)
  // For tests like dmtcp5, forkexec (tests with child processes),
  //   the child process may have a name "NAME (deleted)".
  // This is arguably a bug in the kernel.
  jalib::string _GetProgramExe()
  {
    jalib::string exe = "/proc/self/exe";
    // NOTE: ResolveSymlink is returning string on stack.  Hopefully
    //   C++ jalib::string is smart enough to copy it.
    jalib::string exeRes = jalib::Filesystem::ResolveSymlink ( exe );
    JASSERT ( exe != exeRes ) ( exe ).Text ( "problem with /proc/self/exe" );
    // Bug fix for Linux 2.6.19
    const char *deleted = strstr(exeRes.c_str(), " (deleted)");
    if ((deleted != NULL) && (strlen(deleted) == strlen(" (deleted)"))) {
      char substring[256];
      strncpy(substring, exeRes.c_str(), sizeof(substring));
      JASSERT(strlen(substring) < sizeof(substring)) (substring);;
      char *deleted2 = strstr(substring, " (deleted)");
      *deleted2 = '\0';
      exeRes = substring;
    }
    return exeRes;
  }

  // Set buf, and return length read (including all null characters)
  int _GetProgramCmdline(char *buf, int size)
  {
    int fd = _real_open("/proc/self/cmdline", O_RDONLY, 0);
    int rc;
    JASSERT(fd >= 0);
    // rc == 0 means EOF, or else it means buf is full (size chars read)
    rc = dmtcp::Util::readAll(fd, buf, size);
    _real_close(fd);
    return rc;
  }

}

jalib::string jalib::Filesystem::GetCWD()
{
  jalib::string cwd;
  char buf[PATH_MAX];
  JASSERT(getcwd(buf, PATH_MAX) == buf)
    .Text("Pathname too long");
  cwd = buf;
  return cwd;
}

jalib::string jalib::Filesystem::BaseName ( const jalib::string& str )
{
  size_t len = str.length();

  if (str == "/" || str == "." || str == ".." || str.empty())
    return str;

  // Remove trailing slashes
  while (len > 0 && str[len - 1] == '/') {
    len--;
  }

  size_t lastSlash = str.find_last_of('/', len);

  if (lastSlash == string::npos)
    return str.substr(0, len);

  return str.substr(lastSlash + 1, len - lastSlash);
}

jalib::string jalib::Filesystem::DirName ( const jalib::string& str )
{
  size_t len = str.length();

  if (str == "/" || str == "." || str.empty())
    return str;

  if (str == "..")
    return ".";

  // Remove trailing slashes
  while (len > 0 && str[len - 1] == '/') {
    len--;
  }

  size_t lastSlash = str.find_last_of('/', len);

  if (lastSlash == string::npos)
    return ".";

  return str.substr(0, lastSlash);
}

jalib::string jalib::Filesystem::GetProgramDir()
{
  static jalib::string value = DirName ( GetProgramPath() );
  return value;
}

jalib::string jalib::Filesystem::GetProgramName()
{
  static jalib::string value = "";
  if (value == "") {
    size_t len;
    char cmdline[1024];
    value = BaseName ( GetProgramPath() ); // uses /proc/self/exe
    // We may rewrite "a.out" to "/lib/ld-linux.so.2 a.out".  If so, find cmd.
    if (!value.empty()
        && ( value == ResolveSymlink("/lib/ld-linux.so.2")
            || value == ResolveSymlink("/lib64/ld-linux-x86-64.so.2") )
	&& (len = _GetProgramCmdline(cmdline, sizeof(cmdline))) > 0
	&& len > strlen(cmdline) + 1 // more than one word in cmdline
	&& *(cmdline + strlen(cmdline) + 1) != '-') // second word not a flag
      value = BaseName(cmdline + strlen(cmdline) + 1); // find second word
  }
  return value;
}

jalib::string jalib::Filesystem::GetProgramPath()
{
  static jalib::string value = _GetProgramExe();
  return value;
}

// NOTE: ResolveSymlink returns a string, buf, allocated on the stack.
//   While this is dangerous, it avoids the use of malloc or 'new'.
//   Finish using the result before you call a different function.
jalib::string jalib::Filesystem::ResolveSymlink ( const jalib::string& path )
{
  struct stat statBuf;
  // If path is not a symbolic link, just return it.
  if (lstat(path.c_str(), &statBuf) == 0
      && ! S_ISLNK(statBuf.st_mode))
    return path;
  char buf [PATH_MAX]; // This could be passed on via call to readlink()
  memset ( buf, 0, sizeof buf );
  int len = readlink ( path.c_str(), buf, sizeof ( buf )-1 );
  if ( len <= 0 )
    return "";
  return buf;
}

bool jalib::Filesystem::FileExists ( const jalib::string& str )
{
  struct stat st;

  if( !stat(str.c_str(),&st) ){
    return true;
  }else {
    return false;
  }
}

jalib::string jalib::Filesystem::FindHelperUtility ( const jalib::string& file, bool dieOnError /*= true*/ )
{
  const char* d = NULL;
  const char *p1[] = {
    "/",
    "/mtcp/",
    "/../mtcp/",
    "/../../mtcp/",
    "/../../../mtcp/",
    "/../",
    "/../../",
    "/../../../",
    "/../lib/dmtcp/",
    "/../lib64/dmtcp/"
  };

  const char *p2[] = {
    "./",
    "../",
    "../../",
    "../../../",
    "/bin/",
    "/usr/bin/",
    "/lib/",
    "/lib64/",
    "/usr/lib/",
    "/usr/lib64/"
  };

  jalib::string pth;
  jalib::string udir;
  size_t i = 0;
  if ( ( d=getenv ( "JALIB_UTILITY_DIR" ) ) != NULL )
  {
    udir = d;
    for (i = 0; i < sizeof(p1) / sizeof(char*); i++) {
      pth = udir + p1[i] + file;
      if (FileExists(pth)) {
        return pth;
      }
    }
  }

  udir = GetProgramDir();
  for (i = 0; i < sizeof(p1) / sizeof(char*); i++) {
    pth = udir + p1[i] + file;
    if (FileExists(pth)) {
      return pth;
    }
  }

  for (i = 0; i < sizeof(p1) / sizeof(char*); i++) {
    pth = p2[i] + file;
    if (FileExists(pth)) {
      return pth;
    }
  }
  JASSERT ( !dieOnError ) ( file ) ( GetProgramDir() ) ( d )
    .Text ( "failed to find needed file" );
  return file;
}

jalib::StringVector jalib::Filesystem::GetProgramArgs()
{
  static StringVector rv;

  if (rv.empty()) {
    jalib::string path = "/proc/self/cmdline";
    FILE* args = _real_fopen ( path.c_str(),"r" );

    JASSERT ( args != NULL ) ( path ).Text ( "failed to open command line" );

    char * lineptr = ( char* ) malloc ( 512 ); //getdelim will auto-grow this buffer
    size_t len = 511;

    while ( getdelim ( &lineptr, &len, '\0', args ) >= 0 ) {
      rv.push_back ( lineptr );
    }

    free ( lineptr );
    fclose(args);
  }

  return rv;
}

jalib::IntVector jalib::Filesystem::ListOpenFds()
{
  int fd = _real_open ("/proc/self/fd", O_RDONLY | O_NDELAY |
                                        O_LARGEFILE | O_DIRECTORY, 0);
  JASSERT(fd>=0);

  const size_t allocation = (4 * BUFSIZ < sizeof (struct dirent64)
                             ? sizeof (struct dirent64) : 4 * BUFSIZ);
  char *buf = (char*) JALLOC_HELPER_MALLOC(allocation);

  IntVector fdVec;

  while (true) {
    int nread = _real_syscall(SYS_getdents, fd, buf, allocation);
    if (nread == 0) {
      break;
    }
    JASSERT(nread > 0);
    for (int pos = 0; pos < nread;) {
      struct linux_dirent *d = (struct linux_dirent *) (&buf[pos]);
      if (d->d_ino > 0) {
        char *ch;
        int fdnum = strtol ( d->d_name, &ch, 10 );
        if ( *ch == 0 && fdnum >= 0 && fdnum != fd ) {
          fdVec.push_back ( fdnum );
        }
      }
      pos += d->d_reclen;
    }
  }

  _real_close(fd);

  std::sort(fdVec.begin(), fdVec.end());
  JALLOC_HELPER_FREE(buf);
  return fdVec;
}

jalib::string jalib::Filesystem::GetCurrentHostname()
{
  struct utsname tmp;
  memset ( &tmp,0,sizeof ( tmp ) );
  JASSERT(uname ( &tmp ) != -1) (JASSERT_ERRNO);
  jalib::string name = "unknown";
  if ( strlen(tmp.nodename) != 0 )
    name = tmp.nodename;
//   #ifdef _GNU_SOURCE
//   if(tmp.domainname != 0)
//     name += jalib::string(".") + tmp.domainname;
//   #endif
  return name;
}

jalib::string jalib::Filesystem::GetControllingTerm()
{
  char sbuf[1024];
  char ttyName[64];
  char *tmp;
  char *S;
  char state;
  int ppid, pgrp, session, tty, tpgid;

  int fd, num_read;

  fd = _real_open("/proc/self/stat", O_RDONLY, 0);
  JASSERT( fd >= 0 ) (strerror(errno))
    .Text ("Unable to open /proc/self/stat\n");

  num_read = read(fd, sbuf, sizeof sbuf - 1);
  close(fd);
  if(num_read<=0) return NULL;
  sbuf[num_read] = '\0';

  S = strchr(sbuf, '(') + 1;
  tmp = strrchr(S, ')');
  S = tmp + 2;                 // skip ") "

  sscanf(S,
      "%c "
      "%d %d %d %d %d ",
      &state,
      &ppid, &pgrp, &session, &tty, &tpgid
      );

  int maj =  ((unsigned)(tty)>>8u) & 0xfffu;
  int min =  ((unsigned)(tty)&0xffu) | (((unsigned)(tty)&0xfff00000u)>>12u);

  /* /dev/pts/ * has major numbers in the range 136 - 143 */
  if ( maj >= 136 && maj <= 143)
    sprintf(ttyName, "/dev/pts/%d", min+(maj-136)*256);
  else
    ttyName[0] = '\0';

  return ttyName;
}
