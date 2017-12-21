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

#ifndef JALIBJSERIALIZE_H
#define JALIBJSERIALIZE_H

#include "dmtcpalloc.h"
#include "jalloc.h"
#include "jassert.h"

#include <stdint.h>
#include <string>
#include <vector>

#define JSERIALIZE_ASSERT_POINT(str)                                \
  { char versionCheck[] = str;                                      \
    dmtcp::string correctValue = versionCheck;                      \
    o &versionCheck;                                                \
    JASSERT(versionCheck ==                                         \
            correctValue)(versionCheck)(correctValue)(o.filename()) \
    .Text("invalid file format"); }

namespace jalib
{
class JBinarySerializer
{
  public:
#ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
#endif // ifdef JALIB_ALLOCATOR
    JBinarySerializer(const dmtcp::string &filename) : _filename(filename),
      _bytes(0) {}

    virtual ~JBinarySerializer() {}

    virtual void readOrWrite(void *buffer, size_t len) = 0;
    virtual bool isReader() = 0;
    bool isWriter() { return !isReader(); }

    virtual void rewind() = 0;
    virtual bool isempty() = 0;

    template<typename T>
    void serialize(T &t) { readOrWrite(&t, sizeof(T)); }

    template<typename T>
    JBinarySerializer&operator&(T &t)
    {
      serialize(t);
      return *this;
    }

    template<typename T>
    void serialize(dmtcp::vector<T> &t)
    {
      JBinarySerializer &o = *this;

      JSERIALIZE_ASSERT_POINT("dmtcp::vector:");

      // establish the size
      uint32_t len = t.size();
      serialize(len);

      // make sure we have correct size
      t.resize(len);

      // now serialize all the elements
      for (size_t i = 0; i < len; ++i) {
        JSERIALIZE_ASSERT_POINT("[");
        serialize(t[i]);
        JSERIALIZE_ASSERT_POINT("]");
      }

      JSERIALIZE_ASSERT_POINT("endvector");
    }

    template<typename K, typename V>
    void serializePair(K &key, V &val)
    {
      JBinarySerializer &o = *this;

      JSERIALIZE_ASSERT_POINT("[");
      serialize(key);
      JSERIALIZE_ASSERT_POINT(",");
      serialize(val);
      JSERIALIZE_ASSERT_POINT("]");
    }

    template<typename K, typename V>
    void serialize(dmtcp::map<K, V> &t)
    {
      JBinarySerializer &o = *this;

      JSERIALIZE_ASSERT_POINT("dmtcp::map:");

      // establish the size
      uint32_t len = t.size();
      serialize(len);

      // now serialize all the elements
      if (isReader()) {
        K key; V val;
        for (size_t i = 0; i < len; i++) {
          serializePair(key, val);
          t[key] = val;
        }
      } else {
        for (typename dmtcp::map<K, V>::iterator i = t.begin();
             i != t.end();
             ++i) {
          K key = i->first;
          V val = i->second;
          serializePair(key, val);
        }
      }
      JSERIALIZE_ASSERT_POINT("endmap");
    }

    const dmtcp::string &filename() const { return _filename; }

    size_t bytes() const { return _bytes; }

  private:
    dmtcp::string _filename;

  protected:
    size_t _bytes;
};

template<>
inline void
JBinarySerializer::serialize<dmtcp::string>(dmtcp::string &t)
{
  uint32_t len = t.length();

  serialize(len);
  t.resize(len, '?');
  readOrWrite(&t[0], len);
}

class JBinarySerializeWriterRaw : public JBinarySerializer
{
  public:
    JBinarySerializeWriterRaw(const dmtcp::string &file, int fd);
    void readOrWrite(void *buffer, size_t len);
    bool isReader();
    void rewind();
    bool isempty();
    int fd() { return _fd; }

  protected:
    int _fd;
};

class JBinarySerializeWriter : public JBinarySerializeWriterRaw
{
  public:
    JBinarySerializeWriter(const dmtcp::string &path);
    ~JBinarySerializeWriter();
};

class JBinarySerializeReaderRaw : public JBinarySerializer
{
  public:
    JBinarySerializeReaderRaw(const dmtcp::string &file, int fd);
    void readOrWrite(void *buffer, size_t len);
    bool isReader();
    void rewind();
    bool isempty();
    bool isEOF();
    int fd() { return _fd; }

  protected:
    int _fd;
};

class JBinarySerializeReader : public JBinarySerializeReaderRaw
{
  public:
    JBinarySerializeReader(const dmtcp::string &path);
    ~JBinarySerializeReader();
};
}
#endif // ifndef JALIBJSERIALIZE_H
