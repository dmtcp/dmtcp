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


void jalib::JBinarySerializeWriterRaw::readOrWrite ( void* buffer, size_t len )
{
  JASSERT ( write (_fd, buffer, len) == len ) ( filename() ) ( len ).Text ( "write() failed" );
  _bytes+=len;
}


void jalib::JBinarySerializeReaderRaw::readOrWrite ( void* buffer, size_t len )
{
  JASSERT ( read (_fd, buffer, len) == len ) ( filename() ) ( len ).Text ( "read() failed" );
  _bytes+=len;
}

