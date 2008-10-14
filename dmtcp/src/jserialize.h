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
      JBinarySerializer ( const std::string& filename ) : _filename ( filename ) {}
    private:
      std::string _filename;
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
