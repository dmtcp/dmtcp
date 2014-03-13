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

/* Torque PBS resource manager supporting code

   Torque PBS contains libtorque library that provides API for communications
   with MOM Node management servers to obtain information about allocated
   resources and use them. In particular spawn programs on remote nodes using
   tm_spawn.

   To keep track and control under all processes spawned using any method (like
   exec, ssh) we need also to wrap tm_spawn function
*/

#ifndef TORQUE_PLUGIN_H
#define TORQUE_PLUGIN_H

void probeTorque();
bool isTorqueFile(dmtcp::string relpath, dmtcp::string &path);
bool isTorqueHomeFile(dmtcp::string &path);
bool isTorqueIOFile(dmtcp::string &path);
bool isTorqueStdout(dmtcp::string &path);
bool isTorqueStderr(dmtcp::string &path);
bool isTorqueNodeFile(dmtcp::string &path);
int torqueShouldCkptFile(const char *path, int *type);
int torqueRestoreFile(const char *path, const char *savedFilePath,
                                     int fcntlFlags, int type);

#endif
