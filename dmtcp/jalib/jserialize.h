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


#include "jassert.h"

#include <string>
#include <vector>

#define JSERIALIZE_ASSERT_POINT(str) \
    { char versionCheck[] = str;                                        \
    std::string correctValue = versionCheck;               \
    o & versionCheck;                                                      \
    JASSERT(versionCheck == correctValue)(versionCheck)(correctValue)(o.filename()) \
            .Text("invalid file format"); }

namespace jalib
{

  class JBinarySerializer
  {
    public:
      JBinarySerializer ( const std::string& filename ) : _filename ( filename ), _bytes(0) {}
      virtual ~JBinarySerializer() {}

      virtual void readOrWrite ( void* buffer, size_t len ) = 0;
      virtual bool isReader() = 0;
      bool isWriter() { return ! isReader(); }

      template < typename T >
      void serialize ( T& t ) {readOrWrite ( &t, sizeof ( T ) );}

      template < typename T >
      JBinarySerializer& operator& ( T& t )
      {
        serialize ( t );
        return *this;
      }

      template < typename T >
      void serializeVector ( std::vector<T>& t )
      {
        JBinarySerializer& o = *this;

        JSERIALIZE_ASSERT_POINT ( "std::vector:" );

        //establish the size
        size_t len = t.size();
        serialize ( len );

        //make sure we have correct size
        t.resize ( len );

        //now serialize all the elements
        for ( size_t i=0; i<len; ++i )
        {
          JSERIALIZE_ASSERT_POINT ( "[" );
          serialize ( t[i] );
          JSERIALIZE_ASSERT_POINT ( "]" );
        }

        JSERIALIZE_ASSERT_POINT ( "endvector" );
      }

      const std::string& filename() const {return _filename;}
      size_t bytes() const { return _bytes; }
    private:
      std::string _filename;
    protected:
      size_t _bytes;
  };

  template <>
  inline void JBinarySerializer::serialize<std::string> ( std::string& t )
  {
    size_t len = t.length();
    serialize ( len );
    t.resize ( len,'?' );
    readOrWrite ( &t[0], len );
  }

  template <>
  inline void JBinarySerializer::serialize<std::vector<int> > ( std::vector<int>& t )
  {
    serializeVector<int> ( t );
  }

  class JBinarySerializeWriterRaw : public JBinarySerializer
  {
    public:
      JBinarySerializeWriterRaw ( const std::string& file, int fd );
      void readOrWrite ( void* buffer, size_t len );
      bool isReader();
    protected:
      int _fd;
  };

  class JBinarySerializeWriter : public JBinarySerializeWriterRaw
  {
    public:
      JBinarySerializeWriter ( const std::string& path );
      ~JBinarySerializeWriter();
  };

  class JBinarySerializeReaderRaw : public JBinarySerializer
  {
    public:
      JBinarySerializeReaderRaw ( const std::string& file, int fd );
      void readOrWrite ( void* buffer, size_t len );
      bool isReader();
    protected:
      int _fd;
  };

  class JBinarySerializeReader : public JBinarySerializeReaderRaw
  {
    public:
      JBinarySerializeReader ( const std::string& path );
      ~JBinarySerializeReader();
  };


}



#endif
