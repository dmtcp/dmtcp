/****************************************************************************
 *   Copyright (C) 2014 by Gene Cooperman                                   *
 *   gene@ccs.neu.edu                                                       *
 *                                                                          *
 *  This file is part of DMTCP.                                             *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or                  *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,                *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

/* USAGE:
 * #include "dlsym_default.h"
 * ... DLSYM_DEFAULT(RTLD_NEXT, ...) ...
 * WARNING:  DLSYM_DEFAULT works within a library, but not in base executable
 * WARNING:  RTLD_DEFAULT will not work with DLSYM_DEFAULT()
 */

/* THEORY:  A versioned symbol consists of multiple symbols, one for
 * each version.  Each symbol entry in the dynsym section (which becomes the
 * same as the symtab section when loaded in memory) should have a
 * corresponding entry in the symtab section.  So, the dynsym array values
 * can be viewed as an extra field of the symtab array of structs.
 * The dynsym entry (value) is a version index to the version string
 * for that symbol.  The version string is indicated by an entry in the
 * versym section with the same version index.  (versym is an array of strings)
 *     The dynsym entry can also have the 'hidden' bit (bit 15) set.  For a
 * given * symbol name, there should be exactly one symbol of that name for
 * which the hidden bit is not set.  This is the default version.  The normal
 * "static linker" (at load time) should only link to the base version (version
 * given by index 1 or 2 in the versym section).  The "dynamic linker"
 * (invoked by dlopen) tries first for a base version, and if not found,
 * then hopes for a unique versioned symbol.  (It seems that in all of
 * the above, the linker will always ignore a hidden symbol for these
 * purposes.  Unfortunately, dlsym doesn't follow the same policy as the
 * static or dynamic linker.  Hence, dlsym_default_internal tries to replicate
 * that policy of preferring non-hidden symbols always.)
 *     The symbol pthread_cond_broadcast is a good test case.  It seems to
 * have its base version referenced as a hidden symbol, and only a non-base
 * version exists as unhidden.  Unfortunately, dlsym still chooses the
 * hidden base definition.
 *     Is this a bug in dlsym?  Or maybe just a bug in the 'man dlsym'
 * description?  Since versioning is not POSIX, it's difficult to say.
 */

// Uncomment this to see what symbols and versions are chosen.
// #define VERBOSE

#define _GNU_SOURCE
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#define _GNU_SOURCE
#include <dlfcn.h>

#include "config.h"

// ***** NOTE:  link.h invokes elf.h, which:
// *****        expands ElfW(Word)  to  Elf64_Word; and then defines:
// *****        typedef uint32_t Elf63_Word;

// older sysv standard
static unsigned long elf_hash(const char *name) {
  unsigned long h = 0, g;
  while (*name) {
    h = (h << 4) + *name++;
    if ((g = h & 0xf0000000))
      h ^= g >> 24;
      h &= ~g;
  }
  return h;
}

// For GNU standard, below, see:
//   https://blogs.oracle.com/ali/entry/gnu_hash_elf_sections
//   http://deroko.phearless.org/dt_gnu_hash.txt
//   glibc:elf/dl-lookup.c:do_lookup_x()
//     See:  dl_setup_hash()  and  Elf32_Word bucket = map->l_gnu_buckets  ...

// GNU standard
#if 0
static uint32_t elf_gnu_hash(const char *s) {
  uint32_t h = 5381;
  unsigned char c;
  for (c = *s; c != '\0'; c = *++s)
    h = h * 33 + c;
  return h;
}
#elif 0
// From binutils:bfd/elf.c:bfd_elf_gnu_hash()
unsigned long elf_gnu_hash (const char *namearg)
{
  const unsigned char *name = (const unsigned char *) namearg;
  unsigned long h = 5381;
  unsigned char ch;

  while ((ch = *name++) != '\0')
    h = (h << 5) + h + ch;
  return h & 0xffffffff;
}
#else
// From glibc-2.19
static uint_fast32_t elf_gnu_hash (const char *s)
{
  uint_fast32_t h = 5381;
  unsigned char c;
  for (c = *s; c != '\0'; c = *++s)
    h = h * 33 + c;
  return h & 0xffffffff;
}
#endif

static Elf32_Word hash_first(const char *name, Elf32_Word *hash_table,
                             int use_gnu_hash) {
  if (use_gnu_hash) {
    uint32_t nbuckets = ((uint32_t*)hash_table)[0];
    // uint32_t symndx = ((uint32_t*)hash_table)[1];
    uint32_t maskwords = ((uint32_t*)hash_table)[2];
    uint32_t *buckets = (uint32_t *)
      ((char *)hash_table + 4*sizeof(uint32_t) + maskwords*sizeof(long unsigned int));
    // uint32_t *hashval = & buckets[nbuckets];
    if (buckets[elf_gnu_hash(name) % nbuckets])
      return buckets[elf_gnu_hash(name) % nbuckets];
    else
      return STN_UNDEF;
  } else {
    // http://www.sco.com/developers/gabi/latest/ch5.dynamic.html#hash
    Elf32_Word nbucket = *hash_table++;
    hash_table++; // Elf32_Word nchain = *hash_table++; // Note: nchain same as n_symtab
    Elf32_Word *bucket = hash_table;
    // Elf32_Word *chain = hash_table + nbucket;
    return bucket[elf_hash(name) % nbucket]; // return index into symbol table
  }
}

static Elf32_Word hash_next(Elf32_Word index, Elf32_Word *hash_table,
                            int use_gnu_hash) {
  if (use_gnu_hash) {
    assert( index > STN_UNDEF );
    uint32_t nbuckets = ((uint32_t*)hash_table)[0];
    uint32_t symndx = ((uint32_t*)hash_table)[1];
    uint32_t maskwords = ((uint32_t*)hash_table)[2];
    uint32_t *hashval = (uint32_t *)
      ((char *)hash_table + 4*sizeof(uint32_t) /* sizeof header */
       + maskwords*sizeof(long unsigned int) /* sizeof Bloom filter */
       + nbuckets*sizeof(Elf32_Word) /* sizeof hash buckets */
      );
    if (hashval[index - symndx] & 1)
      return STN_UNDEF;  // end-of-chain indicator
    else
      return index+1;
  } else {
    Elf32_Word nbucket = *hash_table++;
    hash_table++; // Elf32_Word nchain = *hash_table++;
    // Elf32_Word *bucket = hash_table;
    Elf32_Word *chain = hash_table + nbucket;
    return chain[index]; // If this returns STN_UNDEF, then it's end of chain
  }
}

typedef struct dt_tag{
    char *base_addr; /* Base address shared object is loaded at. */
    // ElfW(Sym) *dynsym; // On disk, dynsym would be dynamic symbols only
    ElfW(Sym) *symtab; // Same as dynsym, for in-memory symbol table.
    // ElfW(Word) n_symtab;
    ElfW(Half) *versym;
    /* elf.h lies.  DT_VERDEF is offset from base_addr, not addr. */
    ElfW(Verdef) *verdef;
    ElfW(Word) verdefnum;
    // ElfW(Word) first_ext_def;
    char *strtab;
    Elf32_Word *hash;
    Elf32_Word *gnu_hash;
} dt_tag;

static char *symbol_name(int i, dt_tag *tags) {
  return tags->strtab + tags->symtab[i].st_name;
}

static char *version_name(ElfW(Word) version_ndx, dt_tag *tags) {
    ElfW(Verdef) *cur, *prev;

    // Remove hidden bit, if it's set.
    if (version_ndx & (1<<15))
      version_ndx -= (1<<15);
    // Walk the list of all versions.
    for (prev = NULL, cur =
          (ElfW(Verdef)*)(tags->base_addr + (unsigned long int)(tags->verdef));
         // Could alternatively use verdefnum (DT_VERDEFNUM) here.
         cur != prev;
         prev = cur, cur = (ElfW(Verdef)*)(((char *)cur)+cur->vd_next))
    {
      assert (cur->vd_version == 1);
      if (cur->vd_ndx == version_ndx) {
        ElfW(Verdaux) *first = (ElfW(Verdaux) *)(((char *)cur)+cur->vd_aux);
        return tags->strtab + first->vda_name;
      }
    }
    return NULL;  // failed to find version name
}

// Note that the dynamic section is usually also a segment by itself.
// [ 'readelf -l libXXX.so' to verify. ]
// So, we don't need the object handle.  Its base address is enough,
//   and we can then read the program header to get the right segment.
// Also, the _DYNAMIC symbol in a section should also be a pointer to
//   the address of the dynamic section.  (See comment in /usr/include/link.h)
static void get_dt_tags(void *handle, dt_tag *tags) {
    struct link_map *link_map;  // from /usr/include/link.h
    if (dlinfo(handle, RTLD_DI_LINKMAP, &link_map) == -1)
      printf("ERROR: %s\n", dlerror());
    ElfW(Dyn) *dyn = link_map -> l_ld;  // from /usr/include/link.h
    // http://www.sco.com/developers/gabi/latest/ch5.dynamic.html#dynamic_section
    /* Base address shared object is loaded at. (from /usr/include/lnik.h) */
    tags->base_addr = (char *)(link_map -> l_addr);

    tags->symtab = NULL;
    tags->versym = NULL;
    tags->verdef = NULL;
    tags->strtab = NULL;
    tags->hash = NULL;
    tags->gnu_hash = NULL;
    tags->verdefnum = 0;

    ElfW(Dyn) *cur_dyn;
    // The _DYNAMIC symbol should be pointer to address of the dynamic section.
    // printf("dyn: %p; _DYNAMIC: %p\n", dyn, _DYNAMIC);
    for (cur_dyn = dyn; cur_dyn->d_tag != DT_NULL;  cur_dyn++) {
      if (cur_dyn->d_tag == DT_VERSYM)
        tags->versym = (void *)cur_dyn->d_un.d_ptr;
      if (cur_dyn->d_tag == DT_VERDEF)
        tags->verdef = (void *)cur_dyn->d_un.d_ptr;
      if (cur_dyn->d_tag == DT_VERDEFNUM)
        tags->verdefnum = (ElfW(Word))cur_dyn->d_un.d_val;
      if (cur_dyn->d_tag == DT_STRTAB && tags->strtab == 0)
        tags->strtab = (void *)cur_dyn->d_un.d_ptr;
      // Not DT_DYNSYM, since only dynsym section loaded into RAM; not symtab.??
      //   So, DT_SYMTAB refers to dynsym section ??
      if (cur_dyn->d_tag == DT_SYMTAB)
        tags->symtab = (void *)cur_dyn->d_un.d_ptr;
      if (cur_dyn->d_tag == DT_HASH)
        tags->hash = (void *)cur_dyn->d_un.d_ptr;
#ifdef HAS_GNU_HASH
      if (cur_dyn->d_tag == DT_GNU_HASH)
        tags->gnu_hash = (void *)cur_dyn->d_un.d_ptr;
#endif
      //if (cur_dyn->d_tag == DT_MIPS_SYMTABNO) // Number of DYNSYM entries
      //  n_symtab = (ElfW(Word))cur_dyn->d_un.d_val;
      //if (cur_dyn->d_tag == DT_MIPS_UNREFEXTNO)  // First external DYNSYM
      //  first_ext_def = (ElfW(Word))cur_dyn->d_un.d_val;  // first dynsym entry??
    }
}

//  Don't use dlsym_default_internal(); use dlsym_default.h:DLSYM_DEFAULT()
void *dlsym_default_internal(void *handle, const char*symbol) {
  dt_tag tags;
  Elf32_Word default_symbol_index = 0;
  Elf32_Word i;

#ifdef __USE_GNU
  if (handle == RTLD_NEXT || handle == RTLD_DEFAULT) {
    Dl_info info;
    void *tmp_fnc = dlsym(handle, symbol);  // Hack: get symbol w/ any version
    // printf("tmp_fnc: %p\n", tmp_fnc);
    dladdr(tmp_fnc, &info);
    // ... and find what library the symbol is in
   printf("info.dli_fname: %s\n", info.dli_fname);
#if 0
char *tmp = info.dli_fname;
char *basename = tmp;
for ( ; *tmp != '\0'; tmp++ ) {
  if (*tmp == '/')
    basename = tmp+1;
}
#endif
    // Found handle of RTLD_NEXT or RTLD_DEFAULT
    handle = dlopen(info.dli_fname, RTLD_NOLOAD | RTLD_LAZY);
    // symbol name is:  info.dli_sname;  Could add assert as double-check.
    if (handle == NULL)
      printf("ERROR:  RTLD_DEFAULT or RTLD_NEXT called; no library found.\n");
    // Could try:  dlopen(info.dli_fname, RTLD_LOCAL|RTLD_LAZY); to get handle
    // But if library wasn't loaded before, we shouldn't load it now.
  }
  // An alternative to the above code is to use dl_iterate_phdr() to walk the
  //   list of loaded libraries, and for each one, hash on the symbol name
  //   to see if it's contained in that one.  But dl_iterate_phdr gives you
  //   the base address of the shared object.
  // dlopen(NULL); provides a handle for main program.  dlinfo can then get
  //   dynamic section (see get_dt_tags()), and also the link_map.
  //   When we find a shared object with our symbol in it, the link_map
  //   will give us the name, and dlopen (w/ NOLOAD?) on it gives us a handle.
  // A better way might be to start with any library handle at all: dlopen
  //   Then call dlinfo(handle, RTLD_DI_LINKMAP, &link_map);
  //   for: 'struct link_map &link_map;'  and follow get_dt_tags() for find
  //   info from dynamic section.
#endif

  get_dt_tags(handle, &tags);
  assert(tags.hash != NULL || tags.gnu_hash != NULL);
  int use_gnu_hash = (tags.hash == NULL);
  Elf32_Word *hash = (use_gnu_hash ? tags.gnu_hash : tags.hash);
  for (i = hash_first(symbol, hash, use_gnu_hash); i != STN_UNDEF;
       i = hash_next(i, hash, use_gnu_hash)) {
    if (tags.symtab[i].st_name == 0 || tags.symtab[i].st_value == 0)
      continue;
    if (strcmp(symbol_name(i, &tags), symbol) != 0) // If different symbol name
      continue;
    // We have a symbol of the same name.  Let's look at the version number.
    if ( !(tags.versym[i] & (1<<15)) ) { // If hidden bit is not set.
      // If default symbol not set or if new version later than old one.
      // Notice that default_symbol_index will be set first to the
      //  base definition (1 for unversioned symbols; 2 for versioned symbols)
if (default_symbol_index) {
  printf("WARNING:  More than one default symbol version.\n");
}
      if (!default_symbol_index ||
          // Could look at version dependencies, but using strcmp instead.
          strcmp(version_name(tags.versym[i], &tags),
                 version_name(tags.versym[default_symbol_index], &tags)) > 0) {
        default_symbol_index = i;
      }
    }
  }
#ifdef VERBOSE
  if (default_symbol_index) {
    printf("** st_value: %p\n",
           tags.base_addr + tags.symtab[default_symbol_index].st_value);
    printf("** symbol version: %s\n",
           version_name(tags.versym[default_symbol_index], &tags));
  }
#endif
  if (!default_symbol_index) {
    printf("ERROR:  No default symbol version found for %s.\n"
           "        Extend code to look for hidden symbols?\n", symbol);
  }
  if (default_symbol_index)
    return tags.base_addr + tags.symtab[default_symbol_index].st_value;
  else
    assert(0);
    return NULL;
}
