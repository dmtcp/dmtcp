/*****************************************************************************
 *   Copyright (C) 2006-2008 by Michael Rieker, Jason Ansel, Kapil Arya, and *
 *                                                            Gene Cooperman *
 *   mrieker@nii.net, jansel@csail.mit.edu, kapil@ccs.neu.edu, and           *
 *                                                          gene@ccs.neu.edu *
 *                                                                           *
 *   This file is part of the MTCP module of DMTCP (DMTCP:mtcp).             *
 *                                                                           *
 *  DMTCP:mtcp is free software: you can redistribute it and/or              *
 *  modify it under the terms of the GNU Lesser General Public License as    *
 *  published by the Free Software Foundation, either version 3 of the       *
 *  License, or (at your option) any later version.                          *
 *                                                                           *
 *  DMTCP:dmtcp/src is distributed in the hope that it will be useful,       *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *  GNU Lesser General Public License for more details.                      *
 *                                                                           *
 *  You should have received a copy of the GNU Lesser General Public         *
 *  License along with DMTCP:dmtcp/src.  If not, see                         *
 *  <http://www.gnu.org/licenses/>.                                          *
 *****************************************************************************/

// This file was originally contributed
// by Alex Brick <bricka@dccs.neu.edu>.

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

/**
 * This function takes in the name of an executable, and attempts to find an
 * executable in $PATH that matches the name and is executable by this user.
 *
 * @param program the name of the executable to search for
 * @return the full path to the executable, or NULL if no suitable executable
 * is found
 */
char *mtcp_executable_path(char *name)
{
    char *path;
    char *current_path;
    char *current_path_traverse;
    struct stat stat_buf;

    path = getenv("PATH");
    if(path == NULL)
        return NULL;
    if (name == NULL)
        return NULL;

    current_path = calloc(PATH_MAX, sizeof(char));

    while(*path != '\0')
    {
        current_path_traverse = current_path;

        while((*path != ':') && (*path != '\0'))
            *current_path_traverse++ = *path++;

        if(*path == ':')
            path++;

        *current_path_traverse = '\0';

        /* only add the '/' if it doesn't already exist.  This way, we won't
           violate PATH_MAX */
        if(*(current_path_traverse - 1) != '/')
            strcat(current_path, "/");
        strcat(current_path, name);

        /* does the file exist, and if so, is it executable? */
        if((access(current_path, F_OK) == 0) &&
           (access(current_path, X_OK) == 0))
        {
            /* it can't be a directory */
            if((stat(current_path, &stat_buf) == 0) &&
               (!S_ISDIR(stat_buf.st_mode)))
                return current_path;
        }
    }

    return NULL;
}
