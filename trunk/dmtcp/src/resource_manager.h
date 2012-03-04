/****************************************************************************
 *   Copyright (C) 2006-2010 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *   This file is part of the dmtcp/src module of DMTCP (DMTCP:dmtcp/src).  *
 *                                                                          *
 *  DMTCP:dmtcp/src is free software: you can redistribute it and/or        *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP:dmtcp/src is distributed in the hope that it will be useful,      *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

// This file was originally contributed
// by Artem Y. Polyakov <artpol84@gmail.com>.

#ifndef RESOURCE_MANAGER_H
#define RESOURCE_MANAGER_H

#include "dmtcpalloc.h"

// General
bool runUnderRMgr();

// Torque API

bool isResMgrFile(dmtcp::string &path);
int findLibTorque(dmtcp::string &libpath);
bool isTorqueFile(dmtcp::string relpath, dmtcp::string &path);
bool isTorqueIOFile(dmtcp::string &path);
bool isTorqueNodeFile(dmtcp::string &path);
bool isTorqueStdout(dmtcp::string &path);
bool isTorqueStderr(dmtcp::string &path);

#endif
