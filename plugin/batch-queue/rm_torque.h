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

/* Update by Kapil Arya to create the Torque DMTCP plugin. */

/* The Torque PBS resource manager supporting code.

   Torque PBS contains the libtorque library, which provides the API for
   communications with the MOM Node management servers.  The library obtains
   information about the allocated resources and uses it.  In particular the
   spawn programs on the remote nodes use tm_spawn.

   To keep track of and control all processes spawned using any method (such as
   exec, ssh), we also need to wrap the tm_spawn function.
*/

#ifndef TORQUE_PLUGIN_H
#define TORQUE_PLUGIN_H

#include "dmtcpalloc.h"

namespace dmtcp
{
void probeTorque();
bool isTorqueFile(string relpath, string &path);
bool isTorqueHomeFile(string &path);
bool isTorqueIOFile(string &path);
bool isTorqueStdout(string &path);
bool isTorqueStderr(string &path);
bool isTorqueNodeFile(string &path);
int torqueShouldCkptFile(const char *path, int *type);
int torqueRestoreFile(const char *path,
                      const char *savedFilePath,
                      int fcntlFlags,
                      int type);
}
#endif // ifndef TORQUE_PLUGIN_H
