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

#ifndef MEM_BARRIER_H
#define MEM_BARRIER_H

#if defined(__i386__) || defined(__x86_64__)
# if defined(__i386__) && defined(__PIC__)
// FIXME:  After DMTCP-1.2.5, this can be made only case for i386/x86_64
#  define RMB asm volatile ("lfence" \
                           : : : "eax", "ecx", "edx", "memory")
#  define WMB asm volatile ("sfence" \
                           : : : "eax", "ecx", "edx", "memory")
#  define IMB
# else
#  define RMB asm volatile ("xorl %%eax,%%eax ; cpuid" \
                           : : : "eax", "ebx", "ecx", "edx", "memory")
#  define WMB asm volatile ("xorl %%eax,%%eax ; cpuid" \
                           : : : "eax", "ebx", "ecx", "edx", "memory")
#  define IMB
# endif
#elif defined(__arm__)
# define RMB asm volatile (".arch armv7-a \n\t dsb ; dmb" : : : "memory")
# define WMB asm volatile (".arch armv7-a \n\t dsb ; dmb" : : : "memory")
# define IMB asm volatile (".arch armv7-a \n\t isb" : : : "memory")
#else
# error "instruction architecture not implemented"
#endif

#endif
