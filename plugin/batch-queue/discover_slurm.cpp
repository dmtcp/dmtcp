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

#include <iomanip>
#include <iostream>
#include <sstream>

#include "discover_slurm.h"

using namespace std;

class slurm_nodes
{
  private:
    string str, prefix, num;
    size_t pos;
    int range_cur, range_end, range_num_len;
    bool is_end, last_range;
    bool with_prefix;

  public:
    slurm_nodes(string s)
    {
      str = s;
      pos = 0;
      is_end = false;
      with_prefix = false;
      prefix = "";
      range_cur = range_end = range_num_len = -1;
    }

    string next()
    {
      if (is_end) {
        return "";
      }

      if (range_cur >= 0) {
        if (range_cur <= range_end) {
          stringstream ss;
          ss << setfill('0');
          ss << setw(range_num_len) << range_cur;
          range_cur++;
          return prefix + ss.str();
        } else {
          range_cur = range_end = range_num_len = -1;
          if (last_range) {
            with_prefix = false;
            last_range = false;
          }
        }
      }

      while (1) {
        size_t next = str.find_first_of(",[]", pos);
        if (next == string::npos) {
          prefix = str.substr(pos);
          is_end = true;
          pos = next;
          return prefix;
        }

        if (str[next] == ',') {
          if (!with_prefix) {
            prefix = str.substr(pos, next - pos);
            with_prefix = false;
            pos = next + 1;
            if (prefix != "") {
              return prefix;
            }
          } else {
            num = str.substr(pos, next - pos);
            size_t dash = num.find_first_of("-", 0);
            if (dash == string::npos) {
              num = str.substr(pos, next - pos);
              pos = next + 1;
              return prefix + num;
            } else {
              string start = num.substr(0, dash);
              string end = num.substr(dash + 1);
              range_num_len = end.size();
              range_cur = atoi(start.c_str());
              range_end = atoi(end.c_str());
              stringstream ss;
              ss << setfill('0');
              ss << setw(range_num_len) << range_cur;
              range_cur++;
              pos = next + 1;
              return prefix + ss.str();
            }
          }
        }
        if (str[next] == '[') {
          prefix = str.substr(pos, next - pos);
          with_prefix = true;
          last_range = false;
          pos = next + 1;
        }

        if (str[next] == ']') {
          num = str.substr(pos, next - pos);
          size_t dash = num.find_first_of("-", 0);
          if (dash == string::npos) {
            with_prefix = false;
            pos = next + 1;
            return prefix + num;
          } else {
            last_range = true;
            string start = num.substr(0, dash);
            string end = num.substr(dash + 1);
            range_num_len = end.size();
            range_cur = atoi(start.c_str());
            range_end = atoi(end.c_str());
            stringstream ss;
            ss << setfill('0');
            ss << setw(range_num_len) << range_cur;
            range_cur++;
            pos = next + 1;
            return prefix + ss.str();
          }
        }
      }
    }
};

class slurm_slots
{
  private:
    string str;
    size_t pos;
    int slots, slots_remain;
    bool is_end;

    void set_element(string s)
    {
      size_t pos1 = s.find("(x");
      size_t pos2 = s.find(")");
      string sls, snum;

      if (pos1 != pos2) {
        sls = s.substr(0, pos1);
        snum = s.substr(pos1 + 2, pos2 - (pos1 + 2));
      } else {
        sls = s;
        snum = "1";
      }

      stringstream ss;
      ss << sls << " " << snum;
      ss >> slots >> slots_remain;
    }

  public:
    slurm_slots(string s)
    {
      slots = 0;
      slots_remain = 0;
      pos = 0;
      str = s;
      is_end = false;
      if (str.size() == 0) {
        is_end = true;
      }
    }

    int next()
    {
      if (is_end) {
        return -1;
      }
      if (slots_remain) {
        slots_remain--;
        return slots;
      }

      if (pos >= str.size()) {
        is_end = true;
        return -1;
      }

      size_t next = str.find_first_of(",", pos);
      if (next == string::npos) {
        next = str.size();
      }
      string tmp = str.substr(pos, next - pos);
      pos = next + 1;
      set_element(tmp);
      if (!slots_remain) {
        is_end = true;
        return -1;
      } else {
        slots_remain--;
        return slots;
      }
    }
};


int
resources_slurm::discover()
{
  ulong node_id = 0;
  bool is_launch = true;

  /* Detect resources */
  const char *nodelist = getenv("SLURM_JOB_NODELIST");

  if (nodelist == NULL) {
    nodelist = getenv("SLURM_NODELIST");
  }

  if (nodelist == NULL) {
    fprintf(stderr,
            "Error: environment variables SLURM_JOB_NODELIST or "
            "SLURM_NODELIST are not set!\n");
    return -1;
  }
  slurm_nodes nodes(nodelist);

  const char *slotlist = getenv("SLURM_TASKS_PER_NODE");

  slurm_slots *slots = NULL;
  if (slotlist == NULL) {
    // fail bit is set:  string is too big.  Drop the rest.
    fprintf(stderr,
            "WARNING: environment variables SLURM_JOB_CPUS_PER_NODE or "
            "SLURM_TASKS_PER_NODE are not set!\n");
  } else {
    slots = new slurm_slots(slotlist);
  }

  string node;
  while ((node = nodes.next()) != "") {
    int slotnum = 1;
    if (slots) {
      slotnum = slots->next();
      if (slotnum < 0) {
        fprintf(stderr,
                "Error: environment variables SLURM_JOB_NODELIST or "
                "SLURM_NODELIST are not set!\n");
        delete slots;
        return -1;
      }
    }
    node_map[node].id = node_id++;
    node_map[node].app_slots = slotnum;
    node_map[node].name = node;

    // The first node in the list launches all applications.
    node_map[node].is_launch = is_launch;
    is_launch = false;
  }
  delete slots;
  return 0;
}
