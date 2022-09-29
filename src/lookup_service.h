/****************************************************************************
 *   Copyright (C) 2006-2010 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *  This file is part of DMTCP.                                             *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or                  *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,                *
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

#include <string.h>
#include <map>
#include "../jalib/jsocket.h"
#include "dmtcpmessagetypes.h"

namespace dmtcp
{
class KeyValue
{
  public:
    KeyValue(const void *data, const size_t len)
    {
      _data = JALLOC_HELPER_MALLOC(len);
      _len = len;
      memcpy(_data, data, len);
    }

    ~KeyValue()
    {
      if (_data != NULL) {
        JALLOC_HELPER_FREE(_data);
        _data = nullptr;
      }
    }

    void *data() const { return _data; }

    size_t len() const { return _len; }

    bool operator<(const KeyValue &that) const
    {
      if (_len == that._len) {
        return memcmp(_data, that._data, _len) < 0;
      }
      return _len < that._len;
    }

    bool operator==(const KeyValue &that) const
    {
      return _len == that._len && memcmp(_data, that._data, _len) == 0;
    }

    bool operator!=(const KeyValue &that) const
    {
      return !operator==(that);
    }

  private:
    void *_data = nullptr;
    size_t _len = 0;
};

class LookupService
{
  public:
    LookupService() {}

    ~LookupService() { reset(); }

    void reset();

    void get64(jalib::JSocket &remote, const DmtcpMessage &msg);
    void set64(jalib::JSocket &remote, const DmtcpMessage &msg);

    void registerData(const DmtcpMessage &msg, const void *data);
    void respondToQuery(jalib::JSocket &remote,
                        const DmtcpMessage &msg,
                        const void *data);

  private:
    void addKeyValue(string id, string key, string val);

    typedef map<string, string>KeyValueMap;
    typedef map<int64_t, int64_t>KeyValueMap64;

    map<string, KeyValueMap>_maps;
    map<string, KeyValueMap64>_maps64;
};
}
#endif // ifndef LOOKUP_SERVICE_H
