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
/*  Print on stderr without using any malloc stuff										*/
/*  We can't use vsnprintf or anything like that as it calls malloc								*/
/*  This routine supports only simple %c, %d, %o, %p, %s, %u, %x (or %X)							*/
/*																*/
/********************************************************************************************************************************/

#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "mtcp_internal.h"

static char const hexdigits[] = "0123456789ABCDEF";

static void rwrite (char const *buff, int size);

void mtcp_printf (char const *format, ...)

{
  char const *p, *q;
  va_list ap;

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

      /* Single charaacter */

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
}

static void rwrite (char const *buff, int size)

{
  int offs, rc;

  for (offs = 0; offs < size; offs += rc) {
    rc = mtcp_sys_write (2, buff + offs, size - offs);
    if (rc <= 0) break;
  }
}
