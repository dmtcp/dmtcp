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

#ifndef JALIBJBUFFER_H
#define JALIBJBUFFER_H

#include "jalloc.h"
namespace jalib
{
/**
  @author Jason Ansel <jansel@ccs.neu.edu>
*/
class JBuffer
{
  public:
#ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
#endif // ifdef JALIB_ALLOCATOR
    JBuffer(int size = 0);
    JBuffer(const char *source, int size);
    JBuffer(const void *source, int size);
    JBuffer(const JBuffer &that);
    ~JBuffer();
    jalib::JBuffer&operator=(const JBuffer &that);


    const char *buffer() const;
    char *buffer();
    int size() const;
    operator char *() { return buffer(); }
    operator const char *() { return buffer(); }

  private:
    char *_buffer;
    int _size;
};
}
#endif // ifndef JALIBJBUFFER_H
