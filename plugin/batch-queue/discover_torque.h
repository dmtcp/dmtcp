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
#include <string>
#include <map>
#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include <algorithm>
#include <getopt.h>
#include <string.h>

#ifndef DISCOVER_TORQUE_H
#define DISCOVER_TORQUE_H

#include "discover_resources.h"

class resources_tm : public resources {
public:

  resources_tm() : resources(torque) {   }

  int discover();
  static bool probe()
  {
    return (getenv("PBS_ENVIRONMENT") != NULL) &&
            (NULL != getenv("PBS_JOBID"));
  }
};

#endif
