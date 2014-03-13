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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <map>
#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include <algorithm>

#include "discover_resources.h"

#ifndef DISCOVER_DMTCPINPUT_H
#define DISCOVER_DMTCPINPUT_H

#define MAX_LINE_LEN 1024

class resources_input : public resources {
private:
  bool _valid;
  typedef std::vector<std::string> slots_v;
  std::map< std::string, slots_v> node_ckpt_map;

  void trim(std::string &str, std::string delim);
  bool get_checkpoint_filename(std::string &str, std::string &ckptname);
  bool is_serv_slot(std::string &str);
  bool is_launch_process(std::string &str);
  bool add_host(std::string &str, uint &node_id);
  void split2slots(std::string &str, std::vector<std::string> &app_slots,
                   std::vector<std::string> &srv_slots, bool &is_launch);

public:
  resources_input(std::string str);
  int discover() { return 0; }
  bool valid() { return _valid; }
  void writeout_old(std::string env_var, resources &r);
  void writeout_new(std::string env_var, resources &r);

};

#endif
