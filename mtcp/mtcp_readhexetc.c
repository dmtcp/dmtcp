//+++2006-01-17
//    Copyright (C) 2006  Mike Rieker, Beverly, MA USA
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
//---2006-01-17

/********************************************************************************************************************************/
/*																*/
/*  Read from file without using any external memory routines (like malloc, fget, etc)						*/
/*																*/
/********************************************************************************************************************************/

#include <unistd.h>
#include <errno.h>

#include "mtcp_internal.h"

/* Read decimal number, return value and terminating character */

char mtcp_readdec (int fd, VA *value)

{
  char c;
  VA v;

  v = 0;
  while (1) {
    c = mtcp_readchar (fd);
    if ((c >= '0') && (c <= '9')) c -= '0';
    else break;
    v = v * 10 + c;
  }
  *value = v;
  return (c);
}

/* Read decimal number, return value and terminating character */

char mtcp_readhex (int fd, VA *value)

{
  char c;
  VA v;

  v = 0;
  while (1) {
    c = mtcp_readchar (fd);
         if ((c >= '0') && (c <= '9')) c -= '0';
    else if ((c >= 'a') && (c <= 'f')) c -= 'a' - 10;
    else if ((c >= 'A') && (c <= 'F')) c -= 'A' - 10;
    else break;
    v = v * 16 + c;
  }
  *value = v;
  return (c);
}

/* Read non-null character, return null if EOF */

char mtcp_readchar (int fd)

{
  char c;
  int rc;

  do {
    rc = mtcp_sys_read (fd, &c, 1);
  } while ( rc == -1 && mtcp_sys_errno == EINTR );
  if (rc <= 0) return (0);
  return (c);
}
