/****************************************************************************
 *   Copyright (C) 2006-2010 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *   This file is part of the dmtcp/src module of DMTCP (DMTCP:dmtcp/src).  *
 *                                                                          *
 *  DMTCP:dmtcp/src is free software: you can redistribute it and/or        *
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

#ifndef LOOKUP_SERVICE_H
#define LOOKUP_SERVICE_H

#include <map>
#include <string.h>
#include "dmtcpmessagetypes.h"
#include "uniquepid.h"
#include "../jalib/jsocket.h"

namespace dmtcp
{
  class KeyValue {
    public:
      KeyValue(const void *data, const size_t len) {
        _data = malloc(len);
        _len = len;
        memcpy(_data, data, len);
      }
      ~KeyValue() {}

      void *data() {return _data;}
      size_t len() {return _len;}

      bool operator< ( const KeyValue& that ) const {
        if (_len == that._len) {
          return memcmp(_data, that._data, _len) < 0;
        }
        return _len < that._len;
      }
      bool operator== ( const KeyValue& that ) const {
        return _len == that._len && memcmp(_data, that._data, _len) == 0;
      }
      bool operator!= ( const KeyValue& that ) const {
        return ! operator== ( that );
      }

    private:
      void *_data;
      size_t _len;
  };

  class LookupService {
    public:
      LookupService(){}
      ~LookupService() { reset(); }
      void reset();

      typedef map<KeyValue, KeyValue*>::iterator keyValueMapIterator;

      void registerData(const UniquePid& upid, const DmtcpMessage& msg,
                        const char *data);
      void respondToQuery(const UniquePid& upid, jalib::JSocket& remote,
                          const DmtcpMessage& msg, const char *data);

      void addKeyValue(const void *key, size_t keyLen,
                       const void *val, size_t valLen);
      const void *query(const void *key, size_t keyLen,
                        void **val, size_t *valLen);

    private:
      map<KeyValue, KeyValue*> _keyValueMap;
  };
}
#endif
