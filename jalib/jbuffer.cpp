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

#include "jalloc.h"
#include "jassert.h"
#include "jbuffer.h"

jalib::JBuffer::JBuffer(int size)
  : _size(size)
{
  _buffer = (char *)JALLOC_HELPER_MALLOC(size);
  JASSERT(size >= 0) (size);
}

jalib::JBuffer::JBuffer(const char *src, int size)
  : _size(size)
{
  _buffer = (char *)JALLOC_HELPER_MALLOC(size);
  JASSERT(size >= 0) (size);
  memcpy(_buffer, src, _size);
}

jalib::JBuffer::JBuffer(const void *src, int size)
  : _size(size)
{
  _buffer = (char *)JALLOC_HELPER_MALLOC(size);
  JASSERT(size >= 0) (size);
  memcpy(_buffer, src, _size);
}

jalib::JBuffer::~JBuffer()
{
  JALLOC_HELPER_FREE(_buffer);
  _buffer = 0;
  _size = 0;
}

jalib::JBuffer::JBuffer(const JBuffer &that)
  : _size(that._size)
{
  _buffer = (char *)JALLOC_HELPER_MALLOC(that._size);
  memcpy(_buffer, that._buffer, _size);
}

jalib::JBuffer&
jalib::JBuffer::operator=(const JBuffer &that)
{
  JALLOC_HELPER_FREE(_buffer);
  _buffer = 0;
  _size = 0;
  new (this)JBuffer(that);
  return *this;
}

const char *
jalib::JBuffer::buffer() const
{
  return _buffer;
}

char *
jalib::JBuffer::buffer()
{
  return _buffer;
}

int
jalib::JBuffer::size() const
{
  return _size;
}
