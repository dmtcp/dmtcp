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
 * ... dlsym_default(RTLD_NEXT, ...) ...
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

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif
#include <dlfcn.h>

#include "dlsym_default.h"
#include "jassert.h"
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
    JASSERT( index > STN_UNDEF );
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
      JASSERT (cur->vd_version == 1);
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
    struct link_map *lmap;  // from /usr/include/link.h

    /* The handle we get here is either from an earlier call to
     * dlopen(), or from a call to dladdr(). In both the cases,
     * the handle corresponds to a link_map node.
     */
    lmap = (link_map *) handle;
    ElfW(Dyn) * dyn = lmap->l_ld;     // from /usr/include/link.h
    // http://www.sco.com/developers/gabi/latest/ch5.dynamic.html#dynamic_section
    /* Base address shared object is loaded at. (from /usr/include/lnik.h) */
    tags->base_addr = (char *)(lmap->l_addr);

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
        tags->versym = (ElfW(Half) *)cur_dyn->d_un.d_ptr;
      if (cur_dyn->d_tag == DT_VERDEF)
        tags->verdef = (ElfW(Verdef) *)cur_dyn->d_un.d_ptr;
      if (cur_dyn->d_tag == DT_VERDEFNUM)
        tags->verdefnum = (ElfW(Word))cur_dyn->d_un.d_val;
      if (cur_dyn->d_tag == DT_STRTAB && tags->strtab == 0)
        tags->strtab = (char *)cur_dyn->d_un.d_ptr;
      // Not DT_DYNSYM, since only dynsym section loaded into RAM; not symtab.??
      //   So, DT_SYMTAB refers to dynsym section ??
      if (cur_dyn->d_tag == DT_SYMTAB)
        tags->symtab = (ElfW(Sym) *)cur_dyn->d_un.d_ptr;
      if (cur_dyn->d_tag == DT_HASH)
        tags->hash = (Elf32_Word *)cur_dyn->d_un.d_ptr;
#ifdef HAS_GNU_HASH
      if (cur_dyn->d_tag == DT_GNU_HASH)
        tags->gnu_hash = (Elf32_Word *)cur_dyn->d_un.d_ptr;
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
  JASSERT(tags.hash != NULL || tags.gnu_hash != NULL);
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
        JWARNING(false)(symbol).Text("More than one default symbol version.");
      }
      if (!default_symbol_index ||
          // Could look at version dependencies, but using strcmp instead.
          strcmp(version_name(tags.versym[i], &tags),
                 version_name(tags.versym[default_symbol_index], &tags)) > 0) {
        default_symbol_index = i;
      }
    }
  }
  *tags_p = tags;
  *default_symbol_index_p = default_symbol_index;

  if (default_symbol_index)
    return tags.base_addr + tags.symtab[default_symbol_index].st_value;
  else
    return NULL;
}

// Given a pseudo-handle, symbol name, and addr, returns the address of the symbol
// with the given name of a default version found by the search order of the given
// handle which is either RTLD_DEFAULT or RTLD_NEXT.
void *dlsym_default_internal_flag_handler(void* handle, const char* symbol,
                                          void* addr, dt_tag *tags_p,
                                          Elf32_Word *default_symbol_index_p)
{
  Dl_info info;
  struct link_map* map;
  void* result;

  // Retrieve the link_map for the library given by addr
  int ret = dladdr1(addr, &info, (void**)&map, RTLD_DL_LINKMAP);
  if (!ret) {
    JWARNING(false)(symbol)
            .Text("dladdr1 could not find shared object for address");
    return NULL;
  }


  // Handle RTLD_DEFAULT starts search at first loaded object
  if (handle == RTLD_DEFAULT) {
    while (map->l_prev) {
      // Rewinding to search by load order
      map = map->l_prev;
    }
  }

  // Handle RTLD_NEXT starts search after current library
  if (handle == RTLD_NEXT) {
    // Skip current library
    if (!map->l_next) {
      JTRACE("There are no libraries after the current library.");
      return NULL;
    }
    map = map->l_next;
  }

  // Search through libraries until end of list is reached or symbol is found.
  while (1) {
    // printf("l_name: %s\n", map->l_name);
    // Running dlsym_default_internal on the linux-vdso library will
    // cause a segfault. Because it may have different versions on
    // different systems, we check this way rather than a strcmp.
    // If the library is vdso, we simply move to the next loaded,
    // library if it exists.
    if (map->l_name[0] == 'l' &&
        map->l_name[1] == 'i' &&
        map->l_name[2] == 'n' &&
        map->l_name[3] == 'u' &&
        map->l_name[4] == 'x' &&
        map->l_name[5] == '-' &&
        map->l_name[6] == 'v' &&
        map->l_name[7] == 'd' &&
        map->l_name[8] == 's' &&
        map->l_name[9] == 'o') {
      if (!map->l_next) {
        JTRACE("No more libraries to search.");
        return NULL;
      }
      // Change link map to next library
      map = map->l_next;
      continue;
    }

    // Search current library
    result = dlsym_default_internal_library_handler((void*) map, symbol, tags_p,
                                                    default_symbol_index_p);
    if (result) {
      return result;
    }

    // Check if next library exists
    if (!map->l_next) {
      //printf("No more libraries to search.\n");
      return NULL;
    }
    // Change link map to next library
    map = map->l_next;
  }
}

// Produces an error message and hard fails if no default_symbol was found.
void print_debug_messages(dt_tag tags, Elf32_Word default_symbol_index,
                          const char *symbol)
{
#ifdef VERBOSE
  if (default_symbol_index) {
    JTRACE("** st_value: ")
          (tags.base_addr + tags.symtab[default_symbol_index].st_value);
    JTRACE("** symbol version: ")
          (version_name(tags.versym[default_symbol_index], &tags));
  }
#endif
  if (!default_symbol_index) {
    JTRACE("ERROR:  No default symbol version found"
           "        Extend code to look for hidden symbols?")(symbol);
  }
}

// Like dlsym but finds the 'default' symbol of a library (the symbol that the
// dynamic executable automatically links to) rather than the oldest version
// which is what dlsym finds
EXTERNC void *dlsym_default(void *handle, const char*symbol) {
  dt_tag tags;
  Elf32_Word default_symbol_index = 0;

#ifdef __USE_GNU
  if (handle == RTLD_NEXT || handle == RTLD_DEFAULT) {
    // Determine where this function will return
    void* return_address = __builtin_return_address(0);
    // Search for symbol using given pseudo-handle order
    void *result = dlsym_default_internal_flag_handler(handle, symbol, return_address,
                                                       &tags, &default_symbol_index);
    print_debug_messages(tags, default_symbol_index, symbol);
    return result;
  }
#endif

  void *result = dlsym_default_internal_library_handler(handle, symbol, &tags,
                                                        &default_symbol_index);
  print_debug_messages(tags, default_symbol_index, symbol);
  return result;
}
