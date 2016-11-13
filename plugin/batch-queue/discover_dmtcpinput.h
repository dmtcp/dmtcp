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

#ifndef DISCOVER_DMTCPINPUT_H
#define DISCOVER_DMTCPINPUT_H

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
#include <vector>

#include "discover_resources.h"

#define MAX_LINE_LEN 1024

class resources_input : public resources
{
  private:
    typedef std::vector<std::string>slots_v;
    typedef enum { pm_unknown, pm_hydra, pm_orte } pmtype_t;
    std::string warning;
    inline std::string pmtype_to_string(pmtype_t pt)
    {
      switch (pt) {
      case pm_unknown:
        return "UNKNOWN";

      case pm_orte:
        return "ORTE";

      case pm_hydra:
        return "HYDRA";
      }
      return "ERROR";
    }

    bool _valid;
    std::map<std::string, slots_v>node_ckpt_map;
    std::string launch_ckpts;
    pmtype_t pmtype;

    void trim(std::string &str, std::string delim);
    bool get_checkpoint_filename(std::string &str, std::string &ckptname);
    bool is_serv_slot(std::string &str, pmtype_t &pt);
    bool is_launch_process(std::string &str, pmtype_t &pt);
    bool is_helper_process(std::string &str);
    void set_pm_type(std::string &str);
    bool add_host(std::string &str, uint &node_id);
    void split2slots(std::string &str,
                     std::vector<std::string> &app_slots,
                     std::vector<std::string> &srv_slots,
                     std::vector<std::string> &launch_slots,
                     pmtype_t &pt);

  public:
    resources_input(std::string str);
    int discover() { return 0; }

    bool valid() { return _valid; }

    void writeout_old(std::string env_var, resources &r);
    void writeout_new(std::string env_var, resources &r);
};
#endif // ifndef DISCOVER_DMTCPINPUT_H
