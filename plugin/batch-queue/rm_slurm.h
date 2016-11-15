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

#ifndef SLURM_PLUGIN_H
#define SLURM_PLUGIN_H

#include "dmtcpalloc.h"

namespace dmtcp
{
void probeSlurm();
void slurm_restore_env();
bool isSlurmTmpDir(string &str);
int slurmShouldCkptFile(const char *path, int *type);
int slurmRestoreFile(const char *path,
                     const char *savedFilePath,
                     int fcntlFlags,
                     int type);
void slurmRestoreHelper(bool isRestart);
}
#endif // ifndef SLURM_PLUGIN_H
