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

#ifndef DISCOVER_RESOURCES_H
#define DISCOVER_RESOURCES_H

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <string>
#include <vector>

# define MAX_LINE_LEN 1024

class resources
{
  public:
    typedef enum {
      input, torque, slurm, sge
    } res_manager_t;
    typedef unsigned long ulong;
    typedef unsigned int uint;
    typedef unsigned short ushort;

    class node_t
    {
      public:
        std::string name;
        uint app_slots;
        uint srv_slots;
        uint launch_slots;
        uint id;
        std::string mode;
        bool is_launch;

        node_t()
        {
          name = "";
          app_slots = srv_slots = 0;
          id = 0;
          mode = "";
          is_launch = false;
        }
    };

    typedef std::vector<std::vector<uint> >mapping_t;

  protected:
    res_manager_t _type;
    typedef std::map<std::string, node_t>node_map_t;
    node_map_t node_map;
    uint slots_cnt;
    std::vector<node_t *>sorted_v;

    static bool compare(node_t *l, node_t *r)
    {
      if (l->is_launch) {
        return true;
      }
      if (r->is_launch) {
        return false;
      }
      if (l->app_slots > r->app_slots) {
        return true;
      }
      return false;
    }

    void update_sorted();

  public:
    resources(res_manager_t type)
    {
      _type = type;
      node_map.clear();
      sorted_v.clear();
      slots_cnt = 0;
    }

    ~resources()
    {
      node_map.clear();
    }

    res_manager_t type()
    {
      return _type;
    }

    const char *type_str()
    {
      switch (_type) {
      case torque:
        return "TORQUE";

      case sge:
        return "SGE";

      case slurm:
        return "SLURM";

      default:
        return "NONE";
      }
    }

    uint get_node_count()
    {
      return node_map.size();
    }

    bool get_node(ulong node_num, node_t &node)
    {
      node_map_t::iterator it = node_map.begin();
      ulong cnt;

      for (cnt = 0; it != node_map.end() && cnt < node_num; it++, cnt++) {}

      if (cnt < node_num) {
        return false;
      }

      node = it->second;
      return true;
    }

    node_map_t *get_node_map_copy()
    {
      return new node_map_t(node_map);
    }

    void output(std::string env_name);
    void output_sorted(std::string env_name);

    node_t operator[](size_t index)
    {
      if (index < sorted_v.size()) {
        return *sorted_v[index];
      } else {
        node_t ret;
        return ret;
      }
    }

    size_t ssize()
    {
      return sorted_v.size();
    }

    bool map_to(resources &newres, mapping_t &map, std::string &warning);
    virtual int discover() = 0;
};
#endif // ifndef DISCOVER_RESOURCES_H
