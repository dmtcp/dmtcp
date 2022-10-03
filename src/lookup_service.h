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
#include <string_view>
#include <map>
#include "../jalib/jsocket.h"
#include "dmtcpmessagetypes.h"

namespace dmtcp
{
using std::string_view;

class LookupService
{
  public:
    typedef map<string, string>KeyValueMap;
    typedef map<string, int64_t>KeyValueMap64;

    LookupService() {}

    ~LookupService() { reset(); }

    void reset();

    void get64(jalib::JSocket &remote, const DmtcpMessage &msg);
    void set64(jalib::JSocket &remote, const DmtcpMessage &msg);

    void addKeyValue(string id, string key, string val);
    void addKeyValue(string id, string key, int64_t val);

    void registerData(const DmtcpMessage &msg, const void *data);
    void respondToQuery(jalib::JSocket &remote,
                        const DmtcpMessage &msg,
                        const void *data);

    void serialize(ofstream &o, string_view str);
    void serialize(ofstream &o, KeyValueMap const &kvmap);
    void serialize(ofstream& o, KeyValueMap64 const& kvmap);
    void serialize(string_view file);

  private:
    map<string, KeyValueMap>_maps;
    map<string, KeyValueMap64>_maps64;
};
}
#endif // ifndef LOOKUP_SERVICE_H
