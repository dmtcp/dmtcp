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

#include "jfilesystem.h"
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include "jconvert.h"
#include <dirent.h>
#include <algorithm>
#include "errno.h"
#include <sys/utsname.h>
#include "syscallwrappers.h"

namespace
{
  jalib::string _GetProgramExe()
  {
    jalib::string exe = "/proc/self/exe";
    jalib::string exeRes = jalib::Filesystem::ResolveSymlink ( exe );
    JASSERT ( exe != exeRes ) ( exe ).Text ( "problem with /proc/self/exe" );
    return exeRes;
  }

  jalib::string _FileBaseName ( const jalib::string& str )
  {
    int lastSlash = 0;
    for ( size_t i = 0; i<str.length(); ++i )
      if ( str[i] == '/' )
        lastSlash = i;
    return str.substr ( lastSlash+1 );
  }

  jalib::string _DirBaseName ( const jalib::string& str )
  {
    int lastSlash = 0;
    for ( size_t i = 0; i<str.length(); ++i )
      if ( str[i] == '/' )
        lastSlash = i;
    return str.substr ( 0,lastSlash );
  }
}

jalib::string jalib::Filesystem::GetProgramDir()
{
  static jalib::string value = _DirBaseName ( GetProgramPath() );
  return value;
}

jalib::string jalib::Filesystem::GetProgramName()
{
  static jalib::string value = _FileBaseName ( GetProgramPath() );
  return value;
}

jalib::string jalib::Filesystem::GetProgramPath()
{
  static jalib::string value = _GetProgramExe();
  return value;
}


jalib::string jalib::Filesystem::ResolveSymlink ( const jalib::string& path )
{
  char buf [1024];
  memset ( buf,0,sizeof ( buf ) );
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
  /* Old variant. If file is write-only we fail but this is wrong 
  FILE* fp = fopen ( str.c_str(),"r" );
  if ( fp != NULL ) fclose ( fp );
  return fp != NULL;
   */
}

#define FHU_TRY_DIR(expr) {\
    jalib::string pth = expr; \
    if(FileExists(pth)) \
        return pth;}


jalib::string jalib::Filesystem::FindHelperUtility ( const jalib::string& file, bool dieOnError /*= true*/ )
{
  const char* d = NULL;
  if ( ( d=getenv ( "JALIB_UTILITY_DIR" ) ) != NULL )
  {
    jalib::string udir = d;
    FHU_TRY_DIR ( udir + "/" + file );
    FHU_TRY_DIR ( udir + "/mtcp/" + file );
    FHU_TRY_DIR ( udir + "/../mtcp/" + file );
    FHU_TRY_DIR ( udir + "/../../mtcp/" + file );
    FHU_TRY_DIR ( udir + "/../../../mtcp/" + file );
    FHU_TRY_DIR ( udir + "/../" + file );
    FHU_TRY_DIR ( udir + "/../../" + file );
    FHU_TRY_DIR ( udir + "/../../../" + file );
    FHU_TRY_DIR ( udir + "/../lib/dmtcp/" + file );
  }
  FHU_TRY_DIR ( GetProgramDir() + "/" + file );
  FHU_TRY_DIR ( GetProgramDir() + "/mtcp/" + file );
  FHU_TRY_DIR ( GetProgramDir() + "/../mtcp/" + file );
  FHU_TRY_DIR ( GetProgramDir() + "/../../mtcp/" + file );
  FHU_TRY_DIR ( GetProgramDir() + "/../../../mtcp/" + file );
  FHU_TRY_DIR ( GetProgramDir() + "/../" + file );
  FHU_TRY_DIR ( GetProgramDir() + "/../../" + file );
  FHU_TRY_DIR ( GetProgramDir() + "/../../../" + file );
  FHU_TRY_DIR ( GetProgramDir() + "/../lib/dmtcp/" + file );
  FHU_TRY_DIR ( "./" + file );
  FHU_TRY_DIR ( "../" + file );
  FHU_TRY_DIR ( "../../" + file );
  FHU_TRY_DIR ( "../../../" + file );
  FHU_TRY_DIR ( "/bin/" + file );
  FHU_TRY_DIR ( "/usr/bin/" + file );
  FHU_TRY_DIR ( "/lib/" + file );
  FHU_TRY_DIR ( "/lib64/" + file );
  FHU_TRY_DIR ( "/usr/lib/" + file );
  FHU_TRY_DIR ( "/usr/lib64/" + file );
  JASSERT ( !dieOnError ) ( file ) ( GetProgramDir() ) ( d )
    .Text ( "failed to find needed file" );
  return file;
}


jalib::StringVector jalib::Filesystem::GetProgramArgs()
{
  StringVector rv;

  jalib::string path = "/proc/self/cmdline";
  FILE* args = fopen ( path.c_str(),"r" );

  JASSERT ( args != NULL ) ( path ).Text ( "failed to open command line" );

  char * lineptr = ( char* ) malloc ( 512 ); //getdelim will auto-grow this buffer
  size_t len = 511;

  while ( getdelim ( &lineptr, &len, '\0', args ) >= 0 )
  {
    rv.push_back ( lineptr );
  }

  free ( lineptr );

  return rv;
}

#ifdef MALLOC_SAFE_LISTOPENFDS
/* Directory stream type.

   The miscellaneous Unix `readdir' implementations read directory data
   into a buffer and return `struct dirent *' pointers into it.  */

typedef struct _libc_dirstream
  {
    int fd;			/* File descriptor.  */

    pthread_mutex_t lock; /* Mutex lock for this structure.  */

    size_t allocation;		/* Space allocated for the block.  */
    size_t size;		/* Total valid data in the block.  */
    size_t offset;		/* Current offset into the block.  */

    off_t filepos;		/* Position of next entry to read.  */

    /* Directory block.  */
    char data[0] __attribute__ ((aligned (__alignof__ (void*))));
  } _DIR;

jalib::IntVector jalib::Filesystem::ListOpenFds()
{
  jalib::string dir = "/proc/self/fd";
  int fd = _real_open (dir.c_str(), O_RDONLY | O_NDELAY | O_LARGEFILE | 
                                    O_DIRECTORY | O_CLOEXEC, 0);
  JASSERT(fd>=0);

  const size_t allocation = (4 * BUFSIZ < sizeof (struct dirent64)
                             ? sizeof (struct dirent64) : 4 * BUFSIZ);

  _DIR *dp = (_DIR *) jalib::JAllocDispatcher::malloc (sizeof (_DIR) + allocation);
  JASSERT(dp != NULL);

  dp->fd = fd;
  pthread_mutex_init(&dp->lock, NULL);
  dp->allocation = allocation;
  dp->size = 0;
  dp->offset = 0;
  dp->filepos = 0;

  struct dirent *p;
  struct dirent d;
  IntVector fdVec;

  while (readdir_r ((DIR*)dp, &d, &p) == 0 && p != NULL) {
    char *ch;
    int fdnum = strtol ( d.d_name, &ch, 10 );
    if ( *ch == 0 && fdnum >= 0 )
    {
      fdVec.push_back ( fdnum );
    }
  }

  jalib::JAllocDispatcher::free (dp);
  close(fd);

  std::sort(fdVec.begin(), fdVec.end());
  return fdVec;
}
#else
jalib::IntVector jalib::Filesystem::ListOpenFds()
{
  jalib::string dir = "/proc/self/fd";
  IntVector rv;
  struct dirent **namelist;
  char* p;
  int nents = scandir ( dir.c_str(), &namelist, NULL, versionsort );
  JASSERT ( nents >= 0 ) ( dir ) ( JASSERT_ERRNO ).Text ( "failed to open directory" );

  for ( int i = 0; i < nents; i ++ )
  {
    struct dirent * de = namelist[i];
    int fdnum = strtol ( de -> d_name, &p, 10 );
    if ( *p == 0 && fdnum >= 0 )
    {
      rv.push_back ( fdnum );
    }
    free ( de );
  }
  free ( namelist );

  return rv;
}
#endif

jalib::string jalib::Filesystem::GetCurrentHostname()
{
  struct utsname tmp;
  memset ( &tmp,0,sizeof ( tmp ) );
  uname ( &tmp );
  jalib::string name = "unknown";
  if ( tmp.nodename != 0 )
    name = tmp.nodename;
//   #ifdef _GNU_SOURCE
//   if(tmp.domainname != 0)
//     name += jalib::string(".") + tmp.domainname;
//   #endif
  return name;
}

jalib::string jalib::Filesystem::GetCurrentTty()
{
  char sbuf[1024];
  jalib::ostringstream ttyName;
  char *tmp;
  char *S;
  char state;
  int ppid, pgrp, session, tty, tpgid;

  int fd, num_read;

  fd = open("/proc/self/stat", O_RDONLY, 0);
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
    ttyName << "/dev/pts/" << min+(maj-136)*256;
  else
    ttyName << "";

  return ttyName.str();
}

