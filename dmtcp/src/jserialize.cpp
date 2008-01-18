/***************************************************************************
 *   Copyright (C) 2006 by Jason Ansel                                     *
 *   jansel@ccs.neu.edu                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#include "jserialize.h"
#include "jassert.h"
#include "stdio.h"

jalib::JBinarySerializeWriter::JBinarySerializeWriter ( const std::string& path )
    : JBinarySerializer ( path )
    , _fd ( fopen ( path.c_str(), "w" ) )
{
  JASSERT ( _fd != NULL ) ( path ).Text ( "fopen(path) failed" );
}

jalib::JBinarySerializeReader::JBinarySerializeReader ( const std::string& path )
    : JBinarySerializer ( path )
    , _fd ( fopen ( path.c_str(), "r" ) )
{
  JASSERT ( _fd != NULL ) ( path ).Text ( "fopen(path) failed" );
}

jalib::JBinarySerializeWriter::~JBinarySerializeWriter()
{
  fclose ( _fd );
}

jalib::JBinarySerializeReader::~JBinarySerializeReader()
{
  fclose ( _fd );
}


bool jalib::JBinarySerializeWriter::isReader() {return false;}

bool jalib::JBinarySerializeReader::isReader() {return true;}


void jalib::JBinarySerializeWriter::readOrWrite ( void* buffer, size_t len )
{
  JASSERT ( fwrite ( buffer, len, 1, _fd ) ) ( filename() ) ( len ).Text ( "fwrite() failed" );
}


void jalib::JBinarySerializeReader::readOrWrite ( void* buffer, size_t len )
{
  JASSERT ( fread ( buffer, len, 1, _fd ) ) ( filename() ) ( len ).Text ( "fread() failed" );
}

