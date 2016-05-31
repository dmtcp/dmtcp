/****************************************************************************
 *  Copyright (C) 2012-2014 by Artem Y. Polyakov <artpol84@gmail.com>       *
 *                                                                          *
 *  This file is part of the RM plugin for DMTCP                            *
 *                                                                          *
 *  RM plugin is free software: you can redistribute it and/or              *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  RM plugin is distributed in the hope that it will be useful,            *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include "discover_torque.h"

using namespace std;

int
resources_tm::discover()
{
  char buf[MAX_LINE_LEN];
  streamsize max_len = MAX_LINE_LEN;
  ulong node_id = 0;
  bool is_launch = true;

  /* Try to detect the default directory. */
  const char *nodefile = getenv("PBS_NODEFILE");

  if (nodefile == NULL) {
    return -1;
  }

  ifstream s(nodefile);

  if (!s.is_open()) {
    return -1;
  }

  s.getline(buf, max_len);
  while (!s.eof()) {
    if (s.fail() && !s.bad()) {
      // fail bit is set: string is too big.  Drop the rest.
      fprintf(stderr, "Error: reading from PE HOSTFILE: too long string\n");
      while (s.fail() && !s.bad()) {
        s.getline(buf, max_len);
      }
    } else if (s.bad()) {
      return -1;
    } else {
      if (node_map.find(buf) != node_map.end()) {
        node_map[buf].app_slots++;
      } else {
        node_map[buf].id = node_id;
        node_id++;
        node_map[buf].app_slots = 1;
        node_map[buf].name = buf;

        // The first node in the list is considered as the node
        // that launches all applications.
        node_map[buf].is_launch = is_launch;
        is_launch = false;
      }
      s.getline(buf, max_len);
    }
  }
  return 0;
}
