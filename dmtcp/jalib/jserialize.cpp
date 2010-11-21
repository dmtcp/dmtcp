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

#include "jserialize.h"
#include "jassert.h"
#include "stdio.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

jalib::JBinarySerializeWriterRaw::JBinarySerializeWriterRaw ( const jalib::string& path, int fd )
    : JBinarySerializer ( path )
    , _fd ( fd )
{
  JASSERT (_fd >= 0)(path)(JASSERT_ERRNO).Text("open(path) failed");
}

jalib::JBinarySerializeWriter::JBinarySerializeWriter ( const jalib::string& path )
  : JBinarySerializeWriterRaw ( path , open ( path.c_str(), O_CREAT|O_WRONLY|O_TRUNC, 0600) )
{}

jalib::JBinarySerializeReaderRaw::JBinarySerializeReaderRaw ( const jalib::string& path, int fd )
  : JBinarySerializer ( path )
  , _fd ( fd )
{
  JASSERT (_fd >= 0)(path)(JASSERT_ERRNO).Text("open(path) failed");
}

jalib::JBinarySerializeReader::JBinarySerializeReader ( const jalib::string& path )
  : JBinarySerializeReaderRaw ( path , open ( path.c_str(), O_RDONLY ) )
{}

jalib::JBinarySerializeWriter::~JBinarySerializeWriter()
{
  close ( _fd );
}

jalib::JBinarySerializeReader::~JBinarySerializeReader()
{
  close ( _fd );
}


bool jalib::JBinarySerializeWriterRaw::isReader() {return false;}

bool jalib::JBinarySerializeReaderRaw::isReader() {return true;}

// Rewind file descriptor to start value
void jalib::JBinarySerializeWriterRaw::rewind()
{
  JASSERT(lseek(_fd,0,SEEK_SET) == 0)(strerror(errno)).Text("Cannot rewind");
}

void jalib::JBinarySerializeReaderRaw::rewind()
{
  JASSERT(lseek(_fd,0,SEEK_SET) == 0)(strerror(errno)).Text("Cannot rewind");
}

bool jalib::JBinarySerializeWriterRaw::isempty()
{
  bool ret = false;
  off_t cur = lseek(_fd,0,SEEK_CUR);
  off_t end = lseek(_fd,0,SEEK_END);
  if( end == 0 )
    ret = true;
  JASSERT( lseek(_fd,cur,SEEK_SET) == cur )(strerror(errno)).Text("Cannot set current position");
  JTRACE("\n\n\nIsEmpty:")(cur)(end)(ret)("\n\n\n");
  return ret;
}

bool jalib::JBinarySerializeReaderRaw::isempty()
{
  bool ret = false;
    off_t cur = lseek(_fd,0,SEEK_CUR);
    off_t end = lseek(_fd,0,SEEK_END);
    if( end == 0 )
      ret = true;
    JASSERT( lseek(_fd,cur,SEEK_SET) == cur )(strerror(errno)).Text("Cannot set current position");
    JTRACE("\n\n\nIsEmpty:")(cur)(end)(ret)("\n\n\n");
    return ret;
}


void jalib::JBinarySerializeWriterRaw::readOrWrite ( void* buffer, size_t len )
{
  size_t ret;
  JASSERT ( (ret = write (_fd, buffer, len)) == len ) ( filename() ) (ret) ( len ).Text ( "write() failed" );
  _bytes+=len;
}


void jalib::JBinarySerializeReaderRaw::readOrWrite ( void* buffer, size_t len )
{
  size_t ret;
  JASSERT ( (ret = read (_fd, buffer, len)) == len ) ( filename() )(ret)( len ).Text ( "read() failed" );
  _bytes+=len;
}

