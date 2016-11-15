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
#include "discover_dmtcpinput.h"
#include "discover_resources.h"
#include "discover_slurm.h"
#include "discover_torque.h"


resources *
discover_rm()
{
  if (resources_tm::probe() == true) {
    return new resources_tm();
  } else if (resources_slurm::probe() == true) {
    return new resources_slurm();
  }
  return NULL;
}

void
print_help(char *pname)
{
  std::string name = pname;
  std::cout << "Usage: " + name << std::endl;
  std::cout << "--help or no arguments - print this page" << std::endl;
  std::cout <<
  "-t, --test-rm          - check for rm and write out allocated nodes" <<
  std::endl;
  std::cout <<
  "-n, --new-output       - Output for RM remote launch utilitys" << std::endl;
  std::cout << "no options mean read worker_ckpts content from input and do" <<
  std::endl;
  std::cout << "mapping of exiting RM resources to old ones" << std::endl;
}

int
main(int argc, char **argv)
{
  resources *rm = discover_rm();
  bool new_out = false;
  char *input_arg = NULL;

  enum mode_t {
    help, rmtest, full
  } mode = help;

  while (1) {
    char c;
    int option_index;
    static struct option long_options[] = {
      // modes
      { "help", 0, 0, 'h' },
      { "test-rm", 1, 0, 't' },
      { "new-output", 1, 0, 'n' },
      { 0, 0, 0, 0 }
    };

    c = getopt_long(argc, argv, "htn", long_options, &option_index);
    if (c == -1) {
      break;
    }
    switch (c) {
    case 'h':
      break;
    case 't':
      mode = rmtest;
      break;
    case 'n':
      new_out = true;
      break;
    }
  }

  if (optind < argc && mode == help) {
    mode = full;
    input_arg = argv[optind];
  }


  switch (mode) {
  case help:
    print_help(argv[0]);
    break;
  case rmtest:
    if (rm == NULL || rm->discover()) {
      printf("RES_MANAGER=NONE\n");
    } else {
      printf("RES_MANAGER=%s\n", rm->type_str());
      rm->output("manager_resources");
      fflush(stdout);
    }
    break;
  case full:
    if (rm == NULL || rm->discover()) {
      printf("RES_MANAGER=NONE\n");
    } else {
      printf("RES_MANAGER=%s\n", rm->type_str());
      rm->output("manager_resources");
      fflush(stdout);
    }
    resources_input inp(input_arg);
    inp.output("input_config");
    if (!new_out) {
      inp.writeout_old("new_worker_ckpts", *rm);
    } else {
      inp.writeout_new("DMTCP_REMLAUNCH", *rm);
    }
    fflush(stdout);
    break;
  }
  return 0;
}
