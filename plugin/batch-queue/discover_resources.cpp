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

#include "discover_resources.h"

using namespace std;

void
resources::update_sorted()
{
  sorted_v.clear();
  node_map_t::iterator it = node_map.begin();
  slots_cnt = 0;

  for (; it != node_map.end(); it++) {
    sorted_v.push_back(&(it->second));
    slots_cnt += it->second.app_slots;
  }

  sort(sorted_v.begin(), sorted_v.end(), compare);

  // output_sorted("sorted");
}

void
resources::output(string env_name)
{
  node_map_t::iterator it = node_map.begin();

  printf("%s=\"", env_name.c_str());
  for (; it != node_map.end(); it++) {
    if (it->second.is_launch) {
      printf("*");
    }
    printf("%s:%u ", it->second.name.c_str(), it->second.app_slots);
  }
  printf("\"\n");
}

void
resources::output_sorted(string env_name)
{
  vector<node_t *>::iterator it = sorted_v.begin();
  printf("%s=\"", env_name.c_str());
  for (; it != sorted_v.end(); it++) {
    if ((*it)->is_launch) {
      printf("*");
    }
    printf("%s:%u ", (*it)->name.c_str(), (*it)->app_slots);
  }
  printf("\"\n");
}

bool
resources::map_to(resources &newres, mapping_t &map, string &warning)
{
  newres.update_sorted();
  update_sorted();

  if (slots_cnt > newres.slots_cnt) {
    return false;
  }

  size_t size = newres.node_map.size();
  uint map_used[size];
  map.resize(size);
  for (size_t i = 0; i < size; i++) {
    map[i].clear();
    map_used[i] = 0;
  }

  // map old launch node to new launch node
  uint old_launch = 0, new_launch = 0;
  for (size_t i = 0; i < sorted_v.size(); i++) {
    if (sorted_v[i]->is_launch) {
      old_launch = i;
      break;
    }
  }
  for (size_t i = 0; i < newres.ssize(); i++) {
    if (newres[i].is_launch) {
      new_launch = i;
      break;
    }
  }

  if (newres[new_launch].app_slots < sorted_v[old_launch]->app_slots) {
    warning +=
      "WARNING: amount of MPI-worker slots on new node is less than "
      "amount on old one\n";

    // Put only the launch process on the launch node?
  }

  map[new_launch].push_back(old_launch);
  map_used[new_launch] += sorted_v[old_launch]->app_slots;

  // map other nodes
  for (size_t i = 0; i < sorted_v.size(); i++) {
    // skip launch node
    if (i == old_launch) {
      continue;
    }

    // continue with any other node
    uint search_res = operator[](i).app_slots;
    bool found = false;
    for (size_t j = 0; j < size && !found; j++) {
      uint free = newres[j].app_slots - map_used[j];
      if (free >= search_res) {
        map_used[j] += search_res;
        map[j].push_back(i);
        found = true;
      }
    }
    if (!found) {
      return false;
    }
  }
  return true;
}
