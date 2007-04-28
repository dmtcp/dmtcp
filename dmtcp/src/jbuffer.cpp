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
#include "jbuffer.h"
#include "jassert.h"

jalib::JBuffer::JBuffer(int size)
    :_buffer(new char[size])
    ,_size(size)
{
    JASSERT(size >= 0)(size);
}

jalib::JBuffer::JBuffer(const char* src, int size)
    :_buffer(new char[size])
    ,_size(size)
{
    JASSERT(size >= 0)(size);
    memcpy(_buffer, src, _size);
}


jalib::JBuffer::~JBuffer()
{
    delete [] _buffer;
    _buffer = 0;
    _size = 0;
}

jalib::JBuffer::JBuffer(const JBuffer& that)
    : _buffer(new char[that._size])
    , _size(that._size)
{
    memcpy(_buffer, that._buffer, _size);
}

jalib::JBuffer& jalib::JBuffer::operator=(const JBuffer& that)
{
    delete [] _buffer;
    _buffer = 0;
    _size = 0;
    new (this) JBuffer( that );
    return *this;
}


const char* jalib::JBuffer::buffer() const
{
  return _buffer;
}
char* jalib::JBuffer::buffer()
{
  return _buffer;
}
int jalib::JBuffer::size() const
{
  return _size;
}




