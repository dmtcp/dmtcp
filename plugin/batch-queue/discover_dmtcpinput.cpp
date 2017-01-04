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

#include "discover_dmtcpinput.h"

using namespace std;

void
resources_input::trim(string &str, string delim)
{
  size_t first = 0;

  first = str.find_first_of(delim, first);
  while (first != string::npos) {
    size_t last = str.find_first_not_of(delim, first);
    if (last != string::npos) {
      str.erase(first, last - first);
      first = str.find_first_of(delim, first);
    } else {
      str.erase(first, str.length() - first);
      first = string::npos;
    }
  }
}

bool
resources_input::get_checkpoint_filename(string &str, string &ckptname)
{
  size_t pos = str.find_last_of("/");

  if (pos != string::npos) {
    ckptname.clear();
    ckptname.insert(0, str, pos + 1, str.length() - pos + 1);
    return true;
  }
  return false;
}

bool
resources_input::is_serv_slot(string &str, pmtype_t &pt)
{
  string serv_names[] = {
    // Open MPI
    "orted",

    // MPICH/Hydra
    "hydra_pmi_proxy"

    // To be continued ...
  };
  pmtype_t ptmap[] = {
    // Open MPI
    pm_orte,

    // Hydra
    pm_hydra
  };
  uint size = sizeof(serv_names) / sizeof(serv_names[0]);
  uint i;

  for (i = 0; i < size; i++) {
    if (str.find("ckpt_" + serv_names[i]) != string::npos) {
      pt = ptmap[i];
      return true;
    }
  }
  return false;
}

bool
resources_input::is_launch_process(string &str, pmtype_t &pt)
{
  string serv_names[] = {
    // MPI
    "mpiexec", "mpirun",

    // Open MPI
    "orterun",

    // MPICH/Hydra
    "mpiexec.hydra"

    // To be continued ...
  };
  pmtype_t ptmap[] = {
    // MPI
    pm_unknown, pm_unknown,

    // Open MPI
    pm_orte,

    // Hydra
    pm_hydra
  };
  uint size = sizeof(serv_names) / sizeof(serv_names[0]);
  uint i;

  for (i = 0; i < size; i++) {
    if (str.find("ckpt_" + serv_names[i]) != string::npos) {
      pt = ptmap[i];
      return true;
    }
  }
  return false;
}

bool
resources_input::is_helper_process(string &str)
{
  string serv_names[] = {
    // DMTCP
    "dmtcp_srun_helper"
  };
  uint size = sizeof(serv_names) / sizeof(serv_names[0]);
  uint i;

  for (i = 0; i < size; i++) {
    if (str.find("ckpt_" + serv_names[i]) != string::npos) {
      return true;
    }
  }
  return false;
}

void
resources_input::split2slots(std::string &str,
                             std::vector<std::string> &app_slots,
                             std::vector<std::string> &srv_slots,
                             std::vector<std::string> &launch_slots,
                             pmtype_t &pt)
{
  string delim = " ";
  size_t start_pos = 0, match_pos;

  str += ' ';
  if ((start_pos = str.find_first_not_of(delim, start_pos)) == string::npos) {
    return;
  }
  while (start_pos != string::npos &&
         (match_pos = str.find_first_of(delim, start_pos)) != string::npos) {
    size_t sublen = match_pos - start_pos;
    if (sublen > 0) {
      string sub(str.substr(start_pos, sublen));
      string ckptname;
      pmtype_t _pt = pm_unknown;
      if (get_checkpoint_filename(sub, ckptname)) {
        if (is_launch_process(ckptname, _pt)) {
          launch_slots.push_back(sub);
        } else if (is_helper_process(ckptname)) {
          launch_slots.push_back(sub);
        } else if (is_serv_slot(ckptname, _pt)) {
          srv_slots.push_back(sub);
        } else {
          app_slots.push_back(sub);
        }
        if (pt == pm_unknown) {
          pt = _pt;
        } else {
          if (_pt != pm_unknown && pt != _pt) {
            warning +=
              "WARNING: Conflicting types of process manager detected: " +
              pmtype_to_string(pt) + " & " + pmtype_to_string(_pt) +
              ". Use first one\n";
          }
        }
      }
    }
    start_pos = match_pos;
    start_pos = str.find_first_not_of(delim, start_pos);
  }
}

bool
resources_input::add_host(string &str, uint &node_id)
{
  string delim = ":";
  size_t start_pos = 0;
  size_t match_pos;
  string hostname = "";
  string mode = "";

  // get host name
  if ((match_pos = str.find(delim)) == string::npos) {
    return false;
  }
  if (match_pos - start_pos > 0) {
    size_t sublen = match_pos - start_pos;
    hostname = str.substr(start_pos, sublen);
    trim(hostname, " \n\t");   // delete spaces, newlines and tabs
  } else {
    return false;
  }
  start_pos = match_pos + delim.length();

  // skip mode
  if ((match_pos = str.find(delim, start_pos)) == string::npos) {
    return false;
  }
  if (!(match_pos - start_pos > 0)) {
    return false;
  } else {
    size_t sublen = match_pos - start_pos;
    mode = str.substr(start_pos, sublen);
  }
  start_pos = match_pos + delim.length();

  // process checkpoints
  size_t sublen = str.length() - start_pos;
  string ckpts(str.substr(start_pos, sublen));
  trim(ckpts, "\n");
  slots_v app_slots, srv_slots, launch_slots;
  split2slots(ckpts, app_slots, srv_slots, launch_slots, pmtype);

  if (node_map.find(hostname) != node_map.end()) {
    node_map[hostname].app_slots += app_slots.size();
    node_map[hostname].srv_slots += srv_slots.size();
    node_map[hostname].launch_slots += launch_slots.size();
    node_map[hostname].is_launch = node_map[hostname].is_launch ||
      (launch_slots.size() > 0);
    slots_v &v = node_ckpt_map[hostname];
    v.insert(v.end(), app_slots.begin(), app_slots.end());
    slots_v::iterator it = srv_slots.begin();
    for (; it != srv_slots.end(); it++) {
      v[0] += " " + (*it);
    }
    it = launch_slots.begin();
    for (; it != launch_slots.end(); it++) {
      launch_ckpts += " " + (*it);
    }
  } else {
    node_map[hostname].id = node_id;
    node_id++;
    node_map[hostname].app_slots = app_slots.size();
    node_map[hostname].srv_slots = srv_slots.size();
    node_map[hostname].launch_slots = launch_slots.size();
    node_map[hostname].name = hostname;
    node_map[hostname].mode = mode;
    node_map[hostname].is_launch = (launch_slots.size() > 0);
    slots_v &v = node_ckpt_map[hostname];
    v.insert(v.end(), app_slots.begin(), app_slots.end());
    slots_v::iterator it = srv_slots.begin();
    for (; it != srv_slots.end(); it++) {
      v[0] += " " + (*it);
    }
    it = launch_slots.begin();
    for (; it != launch_slots.end(); it++) {
      launch_ckpts += " " + (*it);
    }
  }
  return true;
}

resources_input::resources_input(string str) : resources(input)
{
  string delim = "::";
  uint hostid = 0;

  warning = "";
  pmtype = pm_unknown;

  _valid = false;
  size_t start_pos = 0, match_pos;
  launch_ckpts = "";

  if ((match_pos = str.find(delim)) == string::npos) {
    return;
  }
  start_pos = match_pos + delim.length();
  while ((match_pos = str.find(delim, start_pos)) != string::npos) {
    size_t sublen = match_pos - start_pos;
    if (sublen > 0) {
      string sub(str.substr(start_pos, sublen));
      if (add_host(sub, hostid)) {
        _valid = true;
      }
    }
    start_pos = match_pos + delim.length();
  }

  if (start_pos != str.length()) {
    size_t sublen = str.length() - start_pos;
    if (sublen > 0) {
      string sub(str.substr(start_pos, sublen));
      if (add_host(sub, hostid)) {
        _valid = true;
      }
    }
    start_pos = match_pos + delim.length();
  }
}

void
resources_input::writeout_old(string env_var, resources &r)
{
  mapping_t map;

  if (!map_to(r, map, warning)) {
    cout <<
      "DMTCP_DISCOVER_RM_ERROR=\'Cannot map initial resources into the restart "
      "allocation\'" << endl;
    return;
  }
  if (warning != "") {
    cout << "DMTCP_DISCOVER_RM_WARNING=\'" << warning << "\'" << endl;
  }

  cout << env_var + "=\'" << endl;
  for (size_t i = 0; i < r.ssize(); i++) {
    if (map[i].size()) {
      cout << ":: " + r[i].name + " :" + sorted_v[0]->mode + ": ";
      for (size_t j = 0; j < map[i].size(); j++) {
        int k = map[i][j];
        string name = sorted_v[k]->name;
        slots_v &v = node_ckpt_map[name];
        slots_v::iterator it = v.begin();
        for (; it != v.end(); it++) {
          cout << (*it) + " ";
        }
      }
      cout << endl;
    }
  }
  cout << "\'" << endl;
}

void
resources_input::writeout_new(string env_var, resources &r)
{
  mapping_t map;

  if (!map_to(r, map, warning)) {
    cout <<
      "DMTCP_DISCOVER_RM_ERROR=\'Cannot map initial resources into the restart "
      "allocation\'" << endl;
    return;
  }
  if (warning != "") {
    cout << "DMTCP_DISCOVER_RM_WARNING=\'" << warning << "\'" << endl;
  }

  cout << "DMTCP_DISCOVER_PM_TYPE=\'" << pmtype_to_string(pmtype) << "\'" <<
    endl;

  cout << "DMTCP_LAUNCH_CKPTS=\'" << launch_ckpts << "\'" << endl;

  cout << env_var + "_NODES=" << r.ssize() << endl;

  bool has_srv_slots = false;
  for (size_t i = 0; i < r.ssize(); i++) {
    if (map[i].size()) {
      int slots_cnt = 0, slot_num;
      for (size_t j = 0; j < map[i].size(); j++) {
        int k = map[i][j];
        string name = sorted_v[k]->name;
        slots_v &v = node_ckpt_map[name];
        slots_cnt += v.size();
        has_srv_slots = has_srv_slots || node_map[name].srv_slots > 0;
      }

      if (!has_srv_slots) {
        std::cout << env_var + "_" << r[i].id << "_SLOTS=" << slots_cnt <<
          std::endl;

        slot_num = 0;
        for (size_t j = 0; j < map[i].size(); j++) {
          int k = map[i][j];
          string name = sorted_v[k]->name;
          slots_v &v = node_ckpt_map[name];
          slots_v::iterator it = v.begin();
          for (; it != v.end(); it++) {
            std::cout << env_var + "_" << r[i].id << "_" << slot_num;
            std::cout << "=\'" << (*it) << "\'" << endl;
            slot_num++;
          }
        }
      } else {
        slots_cnt = 1;
        std::cout << env_var + "_" << r[i].id << "_SLOTS=" << slots_cnt <<
          std::endl;
        slot_num = 0;
        std::cout << env_var + "_" << r[i].id << "_" << slot_num << "=\'";
        for (size_t j = 0; j < map[i].size(); j++) {
          int k = map[i][j];
          string name = sorted_v[k]->name;
          slots_v &v = node_ckpt_map[name];
          slots_v::iterator it = v.begin();
          for (; it != v.end(); it++) {
            std::cout << (*it) << " " << endl;
          }
        }
        std::cout << "\'" << std::endl;
      }
    } else {
      cout << env_var + "_" << r[i].id << "_SLOTS=0";
    }
  }
}
