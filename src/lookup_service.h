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
#include "kvdb.h"

namespace dmtcp
{
class LookupService
{
  public:
    typedef map<string, string>KeyValueMap;

    LookupService() {}

    ~LookupService() { reset(); }

    void reset();

    void set(string const& id, string const& key, string const& val);
    kvdb::KVDBResponse get(string const &id, string const &key, string *val);

    void processRequest(jalib::JSocket &remote,
                        const DmtcpMessage &msg,
                        const void *extraData);

    void serialize(ofstream &o, string const& str);
    void serialize(ofstream &o, KeyValueMap const &kvmap);
    void serialize(string const& file);

  private:
    void sendResponse(jalib::JSocket &remote, kvdb::KVDBResponse response);
    void sendResponse(jalib::JSocket &remote, string const &val);

    void processGet(jalib::JSocket &remote,
                    const DmtcpMessage &msg,
                    const void *extraData);
    void processSet(jalib::JSocket &remote,
                    const DmtcpMessage &msg,
                    const void *extraData);

    map<string, KeyValueMap>_maps;
};
}
#endif // ifndef LOOKUP_SERVICE_H
