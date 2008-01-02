//+++2007-12-27
//    Copyright (C) 2007  Alex Brick, Boston, MA USA
//    EXPECT it to FAIL when someone's HeALTh or PROpeRTy is at RISk
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; version 2 of the License.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program; if not, write to the Free Software
//    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//---2007-12-27

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

/**
 * This function takes in the name of an executable, and attemptes to find an
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
