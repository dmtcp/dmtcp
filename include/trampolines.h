/****************************************************************************
 *   Copyright (C) 2006-2010 by Tyler Denniston and Kapil Arya              *
 *   tyler@ccs.neu.edu and kapil@ccs.neu.edu                                *
 *                                                                          *
 *   This file is part of the dmtcp/src module of DMTCP (DMTCP:dmtcp/src).  *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or        *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,      *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#ifndef TRAMPOLINES_H
#define TRAMPOLINES_H

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "constants.h"

#ifdef __x86_64__
static unsigned char asm_jump[] = {
  // mov    $0x1234567812345678,%rax
  0x48, 0xb8, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12,

  // jmpq   *%rax
  0xff, 0xe0
};

// Beginning of address in asm_jump:
# define ADDR_OFFSET 2
#else // ifdef __x86_64__
static unsigned char asm_jump[] = {
  0xb8, 0x78, 0x56, 0x34, 0x12,   // mov    $0x12345678,%eax
  0xff, 0xe0                      // jmp    *%eax
};

// Beginning of address in asm_jump:
# define ADDR_OFFSET 1
#endif // ifdef __x86_64__

#define ASM_JUMP_LEN sizeof(asm_jump)

typedef struct trampoline_info {
  void *addr;
  char jump[ASM_JUMP_LEN];
  char displaced_instructions[ASM_JUMP_LEN];
} trampoline_info_t;


#define INSTALL_TRAMPOLINE(info) \
  memcpy((info).addr, (info).jump, ASM_JUMP_LEN)

#define UNINSTALL_TRAMPOLINE(info) \
  memcpy((info).addr, (info).displaced_instructions, ASM_JUMP_LEN)

static void dmtcp_setup_trampoline(const char *func_name,
                                   void *trampoline_fn,
                                   trampoline_info_t *info);

static void
dmtcp_setup_trampoline_by_addr(void *addr,
                               void *trampoline_fn,
                               trampoline_info_t *info)
{
  unsigned long pagesize = sysconf(_SC_PAGESIZE);
  unsigned long pagemask = ~(pagesize - 1);
  void *page_base;

  info->addr = addr;

  /* Base address of page where func resides. */
  page_base = (void *)((unsigned long)info->addr & pagemask);
  int pagecount = 1;

  /* Increase the pagecount if number of bytes that needs to be written
   * for the trampoline happens to fall in the next page.
   */
  if (((unsigned long)page_base + pagesize) - (unsigned long)info->addr
        < ASM_JUMP_LEN) {
    pagecount += 1;
  }
  /* Give that whole page RWX permissions. */
  int retval = mprotect(page_base, pagecount * pagesize,
                        PROT_READ | PROT_WRITE | PROT_EXEC);
  if (retval == -1) {
    fprintf(stderr, "*** %s:%d DMTCP Internal Error: mprotect() failed.\n",
            __FILE__, __LINE__);
    abort();
  }

  /************ Set up trampoline injection code. ***********/

  /* Trick to get "free" conversion of a long value to the
     character-array representation of that value. Different sizes of
     long and endian-ness are handled automatically. */
  union u {
    void *val;
    char bytes[sizeof(void *)];
  } data;

  data.val = trampoline_fn;
  memcpy(info->jump, asm_jump, ASM_JUMP_LEN);

  /* Insert real trampoline address into injection code. */
  memcpy(info->jump + ADDR_OFFSET, data.bytes, sizeof(data.bytes));

  /* Save displaced instructions for later restoration. */
  memcpy(info->displaced_instructions, info->addr, ASM_JUMP_LEN);

  /* Inject trampoline. */
  INSTALL_TRAMPOLINE(*info);

  (void)dmtcp_setup_trampoline; // Suppress unused function warning.
}

static void
dmtcp_setup_trampoline(const char *func_name,
                       void *trampoline_fn,
                       trampoline_info_t *info)
{
  /* Find libc func
     We assume that no one is wrapping func yet. */
  void *handle = dlopen(LIBC_FILENAME, RTLD_NOW);

  if (handle == NULL) {
    fprintf(stderr, "*** %s:%d DMTCP Internal Error: dlopen() failed.\n",
            __FILE__, __LINE__);
    abort();
  }
  void *addr = dlsym(handle, func_name);
  if (addr == NULL) {
    fprintf(stderr, "*** %s:%d DMTCP Internal Error: dlsym() failed.\n",
            __FILE__, __LINE__);
    abort();
  }
  dlclose(handle);
  dmtcp_setup_trampoline_by_addr(addr, trampoline_fn, info);
}
#endif // ifndef TRAMPOLINES_H
