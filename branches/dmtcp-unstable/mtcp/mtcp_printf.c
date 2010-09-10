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

/********************************************************************************************************************************/
/*																*/
/*  Print on stderr without using any malloc stuff										*/
/*  We can't use vsnprintf or anything like that as it calls malloc								*/
/*  This routine supports only simple %c, %d, %o, %p, %s, %u, %x (or %X)							*/
/*																*/
/********************************************************************************************************************************/

#include <stdarg.h>
#include <string.h>
#include <unistd.h>

// Force mtcp_sys.h to define this.
#define MTCP_SYS_STRLEN
#define MTCP_SYS_STRCHR


#include "mtcp_internal.h"

int dmtcp_info_stderr_fd = 2;
/* For the default value of -1, mtcp_printf() should not go to jassertlogs */
int dmtcp_info_jassertlog_fd = -1;

static char const hexdigits[] = "0123456789ABCDEF";
static MtcpState printflocked = MTCP_STATE_INITIALIZER;

static void rwrite (char const *buff, int size);

__attribute__ ((visibility ("hidden")))
void mtcp_printf (char const *format, ...)

{
  char const *p, *q;
  va_list ap;

  while (!mtcp_state_set (&printflocked, 1, 0)) {
    mtcp_state_futex (&printflocked, FUTEX_WAIT, 1, NULL);
  }

  va_start (ap, format);

  /* Scan along until we find a % */

  for (p = format; (q = mtcp_sys_strchr (p, '%')) != NULL; p = ++ q) {

    /* Print all before the % as is */

    if (q > p) rwrite (p, q - p);

    /* Process based on character following the % */

gofish:
    switch (*(++ q)) {

      /* Ignore digits (field width) */

      case '0' ... '9': {
        goto gofish;
      }

      /* Single character */

      case 'c': {
        char buff[4];

        buff[0] = va_arg (ap, int); // va_arg (ap, char);
        rwrite (buff, 1);
        break;
      }

      /* Signed decimal integer */

      case 'd': {
        char buff[16];
        int i, n, neg;

        i = sizeof buff;
        n = va_arg (ap, int);
        neg = (n < 0);
        if (neg) n = - n;
        do {
          buff[--i] = (n % 10) + '0';
          n /= 10;
        } while (n > 0);
        if (neg) buff[--i] = '-';
        rwrite (buff + i, sizeof buff - i);
        break;
      }

      /* Unsigned octal number */

      case 'o': {
        char buff[16];
        int i;
        unsigned int n;

        i = sizeof buff;
        n = va_arg (ap, unsigned int);
        do {
          buff[--i] = (n & 7) + '0';
          n /= 8;
        } while (n > 0);
        rwrite (buff + i, sizeof buff - i);
        break;
      }

      /* Address in hexadecimal */

      case 'p': {
        char buff[16];
        int i;
        VA n;

        i = sizeof buff;
        n = (VA) va_arg (ap, void *);
        do {
          buff[--i] = hexdigits[n%16];
          n /= 16;
        } while (n > 0);
        buff[--i] = 'x';
        buff[--i] = '0';
        rwrite (buff + i, sizeof buff - i);
        break;
      }

      /* Null terminated string */

      case 's': {
        p = va_arg (ap, char *);
        rwrite (p, mtcp_sys_strlen (p));
        break;
      }

      /* Unsigned decimal integer */

      case 'u': {
        char buff[16];
        int i;
        unsigned int n;

        i = sizeof buff;
        n = va_arg (ap, unsigned int);
        do {
          buff[--i] = (n % 10) + '0';
          n /= 10;
        } while (n > 0);
        rwrite (buff + i, sizeof buff - i);
        break;
      }

      /* Unsigned hexadecimal number */

      case 'X':
      case 'x': {
        char buff[16];
        int i;
        unsigned int n;

        i = sizeof buff;
        n = va_arg (ap, unsigned int);
        do {
          buff[--i] = hexdigits[n%16];
          n /= 16;
        } while (n > 0);
        rwrite (buff + i, sizeof buff - i);
        break;
      }

      /* Anything else, print the character as is */

      default: {
        rwrite (q, 1);
        break;
      }
    }
  }

  va_end (ap);

  /* Print whatever comes after the last format spec */

  rwrite (p, mtcp_sys_strlen (p));

  if (!mtcp_state_set (&printflocked, 0, 1)) mtcp_abort ();
  mtcp_state_futex (&printflocked, FUTEX_WAKE, 1, NULL);
}

static void rwrite (char const *buff, int size)

{
  int offs, rc;

  if (dmtcp_info_stderr_fd != -1) {
    for (offs = 0; offs < size; offs += rc) {
#ifdef DMTCP
      /* DEBUG macro says to print debugging info, and DMTCP macro says
       * to do it even when DMTCP doesn't want to allow MTCP debugging.
       * Many mtcp_printf() calls occur inside DEBUG conditional.
       * NOTE:   ./configure --enable-debug doesn't automatically set DEBUG.
       * This is useful, so that when DMTCP runs, MTCP debugging is off by
       * default.  Setting DMTCP in mtcp/Makefile and re-compiling changes this.
       */ 
      rc = mtcp_sys_write (STDERR_FILENO, buff + offs, size - offs);
#else
      /* File descriptor where all the debugging outputs should go. */
      rc = mtcp_sys_write (dmtcp_info_stderr_fd, buff + offs, size - offs);
#endif
      if (rc <= 0) break;
    }
  }

  if (dmtcp_info_jassertlog_fd != -1) {
    for (offs = 0; offs < size; offs += rc) {
      rc = mtcp_sys_write (dmtcp_info_jassertlog_fd, buff + offs, size - offs);
      if (rc <= 0) break;
    }
  }
}
