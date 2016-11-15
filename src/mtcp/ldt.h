/*****************************************************************************
 * Copyright (C) 2014 Kapil Arya <kapil@ccs.neu.edu>                         *
 * Copyright (C) 2014 Gene Cooperman <gene@ccs.neu.edu>                      *
 *                                                                           *
 * DMTCP is free software: you can redistribute it and/or                    *
 * modify it under the terms of the GNU Lesser General Public License as     *
 * published by the Free Software Foundation, either version 3 of the        *
 * License, or (at your option) any later version.                           *
 *                                                                           *
 * DMTCP is distributed in the hope that it will be useful,                  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of            *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             *
 * GNU Lesser General Public License for more details.                       *
 *                                                                           *
 * You should have received a copy of the GNU Lesser General Public          *
 * License along with DMTCP.  If not, see <http://www.gnu.org/licenses/>.    *
 *****************************************************************************/

#ifndef LDT_H
#define LDT_H
#include <linux/version.h>

// ARM is missing asm/ldt.h in Ubuntu 11.10 (Linux 3.0, glibc-2.13)
#if defined(__arm__) || defined(__aarch64__)

/* Structure passed to `modify_ldt', 'set_thread_area', and 'clone' calls.
   This seems to have been stable since the beginning of Linux 2.6  */
struct user_desc {
  unsigned int entry_number;
  unsigned long int base_addr;
  unsigned int limit;
  unsigned int seg_32bit : 1;
  unsigned int contents : 2;
  unsigned int read_exec_only : 1;
  unsigned int limit_in_pages : 1;
  unsigned int seg_not_present : 1;
  unsigned int useable : 1;
  unsigned int empty : 25;  /* Some variations leave this out. */
};
#else // if defined(__arm__) || defined(__aarch64__)

// Defines struct user_desc
# include <asm/ldt.h>

// WARNING: /usr/include/linux/version.h often has out-of-date version.

/* struct user_desc * uinfo; */

/* In Linux 2.6.9 for i386, uinfo->base_addr is
 *   correctly typed as unsigned long int.
 * In Linux 2.6.9, uinfo->base_addr is  incorrectly typed as
 *   unsigned int.  So, we'll just lie about the type.
 */

/* SuSE Linux Enterprise Server 9 uses Linux 2.6.5 and requires original
 * struct user_desc from /usr/include/.../ldt.h
 * Perhaps kernel was patched by backport.  Let's not re-define user_desc.
 */

/* RHEL 4 (Update 3) / Rocks 4.1.1-2.0 has <linux/version.h> saying
 *  LINUX_VERSION_CODE is 2.4.20 (and UTS_RELEASE=2.4.20)
 *  while uname -r says 2.6.9-34.ELsmp.  Here, it acts like a version earlier
 *  than the above 2.6.9.  So, we conditionalize on its 2.4.20 version.
 */
# if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0)

/* struct modify_ldt_ldt_s   was defined instead of   struct user_desc   */
#  define user_desc modify_ldt_ldt_s
# endif // if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0)
#endif // if defined(__arm__) || defined(__aarch64__)
#endif // ifndef LDT_H
