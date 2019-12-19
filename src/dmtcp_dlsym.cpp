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
 * #include "dmtcp_dlsym.h"
 * ... dmtcp_dlsym(RTLD_NEXT, ...) ...
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
 * static or dynamic linker.  Hence, dmtcp_dlsym tries to replicate
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

#include "dmtcp.h"
#include "jassert.h"
#include "config.h"

// ***** NOTE:  link.h invokes elf.h, which:
// *****        expands ElfW(Word)  to  Elf64_Word; and then defines:
// *****        typedef uint32_t Elf63_Word;

// older sysv standard
static unsigned long
elf_hash(const char *name)
{
  unsigned long h = 0, g;

  while (*name) {
    h = (h << 4) + *name++;
    if ((g = h & 0xf0000000)) {
      h ^= g >> 24;
    }
    h &= ~g;
  }
  return h;
}

// For GNU standard, below, see:
// https://blogs.oracle.com/ali/entry/gnu_hash_elf_sections
// http://deroko.phearless.org/dt_gnu_hash.txt
// glibc:elf/dl-lookup.c:do_lookup_x()
// See:  dl_setup_hash()  and  Elf32_Word bucket = map->l_gnu_buckets  ...

// GNU standard
#if 0
static uint32_t
elf_gnu_hash(const char *s)
{
  uint32_t h = 5381;
  unsigned char c;

  for (c = *s; c != '\0'; c = *++s) {
    h = h * 33 + c;
  }
  return h;
}

#elif 0

// From binutils:bfd/elf.c:bfd_elf_gnu_hash()
unsigned long
elf_gnu_hash(const char *namearg)
{
  const unsigned char *name = (const unsigned char *)namearg;
  unsigned long h = 5381;
  unsigned char ch;

  while ((ch = *name++) != '\0') {
    h = (h << 5) + h + ch;
  }
  return h & 0xffffffff;
}

#else /* if 0 */

// From glibc-2.19
static uint_fast32_t
elf_gnu_hash(const char *s)
{
  uint_fast32_t h = 5381;
  unsigned char c;

  for (c = *s; c != '\0'; c = *++s) {
    h = h * 33 + c;
  }
  return h & 0xffffffff;
}
#endif /* if 0 */

static Elf32_Word
hash_first(const char *name, Elf32_Word *hash_table, int use_gnu_hash)
{
  if (use_gnu_hash) {
    uint32_t nbuckets = ((uint32_t *)hash_table)[0];

    // uint32_t symndx = ((uint32_t*)hash_table)[1];
    uint32_t maskwords = ((uint32_t *)hash_table)[2];
    uint32_t *buckets = (uint32_t *)
      ((char *)hash_table + 4 * sizeof(uint32_t) + maskwords *
       sizeof(long unsigned int));

    // uint32_t *hashval = & buckets[nbuckets];
    if (buckets[elf_gnu_hash(name) % nbuckets]) {
      return buckets[elf_gnu_hash(name) % nbuckets];
    } else {
      return STN_UNDEF;
    }
  } else {
    // http://www.sco.com/developers/gabi/latest/ch5.dynamic.html#hash
    Elf32_Word nbucket = *hash_table++;
    hash_table++; // Elf32_Word nchain = *hash_table++; // Note: nchain same as
                  // n_symtab
    Elf32_Word *bucket = hash_table;

    // Elf32_Word *chain = hash_table + nbucket;
    return bucket[elf_hash(name) % nbucket]; // return index into symbol table
  }
}

static Elf32_Word
hash_next(Elf32_Word index, Elf32_Word *hash_table, int use_gnu_hash)
{
  if (use_gnu_hash) {
    JASSERT(index > STN_UNDEF);
    uint32_t nbuckets = ((uint32_t *)hash_table)[0];
    uint32_t symndx = ((uint32_t *)hash_table)[1];
    uint32_t maskwords = ((uint32_t *)hash_table)[2];
    uint32_t *hashval = (uint32_t *)
      ((char *)hash_table + 4 * sizeof(uint32_t) /* sizeof header */
       + maskwords * sizeof(long unsigned int) /* sizeof Bloom filter */
       + nbuckets * sizeof(Elf32_Word) /* sizeof hash buckets */
      );
    if (hashval[index - symndx] & 1) {
      return STN_UNDEF;  // end-of-chain indicator
    } else {
      return index + 1;
    }
  } else {
    Elf32_Word nbucket = *hash_table++;
    hash_table++; // Elf32_Word nchain = *hash_table++;
    // Elf32_Word *bucket = hash_table;
    Elf32_Word *chain = hash_table + nbucket;
    return chain[index]; // If this returns STN_UNDEF, then it's end of chain
  }
}

typedef struct dt_tag {
  char *base_addr;   /* Base address shared object is loaded at. */

  // ElfW(Sym) *dynsym; // On disk, dynsym would be dynamic symbols only
  ElfW(Sym) * symtab;  // Same as dynsym, for in-memory symbol table.
  // ElfW(Word) n_symtab;
  ElfW(Half) * versym;

  /* elf.h lies.  DT_VERDEF is offset from base_addr, not addr. */
  ElfW(Verdef) * verdef;
  ElfW(Word) verdefnum;

  // ElfW(Word) first_ext_def;
  char *strtab;
  Elf32_Word *hash;
  Elf32_Word *gnu_hash;
} dt_tag;

static char *
symbol_name(int i, dt_tag *tags)
{
  return tags->strtab + tags->symtab[i].st_name;
}

static char *
version_name(ElfW(Word)version_ndx, dt_tag *tags)
{
  ElfW(Verdef) * cur, *prev;

  // Remove hidden bit, if it's set.
  if (version_ndx & (1 << 15)) {
    version_ndx -= (1 << 15);
  }

  // Walk the list of all versions.
  for (prev = NULL, cur = tags->verdef;
       // Could alternatively use verdefnum (DT_VERDEFNUM) here.
       cur != prev;
       prev = cur, cur = (ElfW(Verdef) *)(((char *)cur) + cur->vd_next)) {
    JASSERT(cur->vd_version == 1);
    if (cur->vd_ndx == version_ndx) {
      ElfW(Verdaux) * first = (ElfW(Verdaux) *)(((char *)cur) + cur->vd_aux);
      return tags->strtab + first->vda_name;
    }
  }
  return NULL;    // failed to find version name
}

// Note that the dynamic section is usually also a segment by itself.
// [ 'readelf -l libXXX.so' to verify. ]
// So, we don't need the object handle.  Its base address is enough,
// and we can then read the program header to get the right segment.
// Also, the _DYNAMIC symbol in a section should also be a pointer to
// the address of the dynamic section.  (See comment in /usr/include/link.h)
static void
get_dt_tags(void *handle, dt_tag *tags)
{
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

  ElfW(Dyn) * cur_dyn;

/*
 * This code extends dmtcp_dlsym to work on VDSO, while in libc, they have a
 * separate internal vdso_dlsym function for this purpose.
 *
 * For libraries loaded by RTLD, the DT_* entries are patched at load time.
 * Here's an example call trace:
 *   _start()
 *     ...
 *     dl_main()
 *       ...
 *       _dl_map_object()
 *         _dl_map_object_from_fd()
 *           _dl_map_segments()
 *           elf_get_dynamic_info()
 *
 * The elf_get_dynamic_info() function changes the DT_* entries from
 * relative offsets to absolute addresses.
 *
 * For cases where the DSO is *not* loaded by RTLD, like vDSO (with read-only
 * data), we need to manually adjust at access time.
 */

// Note that the dynamic tags for the sections of the virtual
//   library, linux-vdso.so, have to be handled specially, since their d_ptr
//   is an offset, and not an absolute address.
// QUESTION:
//   Since we handle linux-vdso.so directly, what is the "else" condition for?
#define ADJUST_DYN_INFO_RO(dst, map, dyn)                   \
  if ( strstr(map->l_name, "linux-vdso.so") ) {             \
    dst = (__typeof(dst))(map->l_addr + dyn->d_un.d_ptr);   \
  } else if (dyn->d_un.d_ptr > map->l_addr) {               \
    dst = (__typeof(dst))dyn->d_un.d_ptr;                   \
  } else {                                                  \
    dst = (__typeof(dst))(map->l_addr + dyn->d_un.d_ptr);   \
  }

  // The _DYNAMIC symbol should be pointer to address of the dynamic section.
  // printf("dyn: %p; _DYNAMIC: %p\n", dyn, _DYNAMIC);
  for (cur_dyn = dyn; cur_dyn->d_tag != DT_NULL; cur_dyn++) {
    if (cur_dyn->d_tag == DT_VERSYM) {
      ADJUST_DYN_INFO_RO(tags->versym, lmap, cur_dyn);
    }
    if (cur_dyn->d_tag == DT_VERDEF) {
      ADJUST_DYN_INFO_RO(tags->verdef, lmap, cur_dyn);
    }
    if (cur_dyn->d_tag == DT_VERDEFNUM) {
      tags->verdefnum = (ElfW(Word))cur_dyn->d_un.d_val;
    }
    if (cur_dyn->d_tag == DT_STRTAB && tags->strtab == 0) {
      ADJUST_DYN_INFO_RO(tags->strtab, lmap, cur_dyn);
    }

    // Not DT_DYNSYM, since only dynsym section loaded into RAM; not symtab.??
    // So, DT_SYMTAB refers to dynsym section ??
    if (cur_dyn->d_tag == DT_SYMTAB) {
      ADJUST_DYN_INFO_RO(tags->symtab, lmap, cur_dyn);
    }
    if (cur_dyn->d_tag == DT_HASH) {
      ADJUST_DYN_INFO_RO(tags->hash, lmap, cur_dyn);
    }
#ifdef HAS_GNU_HASH
    if (cur_dyn->d_tag == DT_GNU_HASH) {
      ADJUST_DYN_INFO_RO(tags->gnu_hash, lmap, cur_dyn);
    }
#endif /* ifdef HAS_GNU_HASH */

    // if (cur_dyn->d_tag == DT_MIPS_SYMTABNO) // Number of DYNSYM entries
    // n_symtab = (ElfW(Word))cur_dyn->d_un.d_val;
    // if (cur_dyn->d_tag == DT_MIPS_UNREFEXTNO)  // First external DYNSYM
    // first_ext_def = (ElfW(Word))cur_dyn->d_un.d_val;  // first dynsym entry??
  }
}

// Given a handle for a library (not RTLD_DEFAULT or RTLD_NEXT), retrieves the
// default symbol for the given symbol if it exists in that library.
// Also sets the tags and default_symbol_index for usage later
void *
dlsym_default_internal_library_handler(void *handle,
                                       const char *symbol,
                                       const char *version,
                                       dt_tag *tags_p,
                                       Elf32_Word *default_symbol_index_p)
{
  dt_tag tags;
  Elf32_Word default_symbol_index = 0;
  Elf32_Word i;
  uint32_t numNonHiddenSymbols = 0;

  get_dt_tags(handle, &tags);
  JASSERT(tags.hash != NULL || tags.gnu_hash != NULL);
  int use_gnu_hash = (tags.hash == NULL);
  Elf32_Word *hash = (use_gnu_hash ? tags.gnu_hash : tags.hash);
  for (i = hash_first(symbol, hash, use_gnu_hash); i != STN_UNDEF;
       i = hash_next(i, hash, use_gnu_hash)) {
    if (tags.symtab[i].st_name == 0 || tags.symtab[i].st_value == 0) {
      continue;
    }
    if (strcmp(symbol_name(i, &tags), symbol) != 0) {
      // If different symbol name
      continue;
    }
    char *symversion = version_name(tags.versym[i], &tags);
    if (version && symversion && strcmp(symversion, version) == 0) {
      default_symbol_index = i;
      break;
    }
    // We have a symbol of the same name.  Let's look at the version number.
    if (version == NULL) {
      if (!(tags.versym[i] & (1<<15))) { // If hidden bit is not set.
        numNonHiddenSymbols++;
      }
      // If default symbol not set or if new version later than old one.
      // Notice that default_symbol_index will be set first to the
      // base definition (1 for unversioned symbols; 2 for versioned symbols)
      if (default_symbol_index && numNonHiddenSymbols > 1) {
        JWARNING(false)(symbol).Text("More than one default symbol version.");
      }
      char *defaultSymVersion = version_name(tags.versym[default_symbol_index],
                                             &tags);
      if (default_symbol_index == 0 ||
          // Could look at version dependencies, but using strcmp instead.
          (symversion && defaultSymVersion &&
           strcmp(symversion, defaultSymVersion) > 0)) {
        default_symbol_index = i;
      }
    }
  }
  *tags_p = tags;
  *default_symbol_index_p = default_symbol_index;

  if (default_symbol_index) {
#if __GLIBC_PREREQ(2, 11)
    // See https://gcc.gnu.org/onlinedocs/gcc-4.9.2/gcc/Function-Attributes.html
    if (ELF64_ST_TYPE(tags.symtab[default_symbol_index].st_info) ==
        STT_GNU_IFUNC) {
      typedef void* (*fnc)();
      fnc f =  (fnc)(tags.base_addr +
                     tags.symtab[default_symbol_index].st_value);
      return f();
    }
#endif
    return tags.base_addr + tags.symtab[default_symbol_index].st_value;
  } else {
    return NULL;
  }
}

// Given a pseudo-handle, symbol name, and addr, returns the address of the
// symbol
// with the given name of a default version found by the search order of the
// given
// handle which is either RTLD_DEFAULT or RTLD_NEXT.
void *
dlsym_default_internal_flag_handler(void *handle,
                                    const char *libname,
                                    const char *symbol,
                                    const char *version,
                                    void *addr,
                                    dt_tag *tags_p,
                                    Elf32_Word *default_symbol_index_p)
{
  Dl_info info;
  struct link_map *map;
  void *result = NULL;

  // Retrieve the link_map for the library given by addr
  int ret = dladdr1(addr, &info, (void **)&map, RTLD_DL_LINKMAP);

  if (!ret) {
    JWARNING(false)(symbol)
            .Text("dladdr1 could not find shared object for address");
    return NULL;
  }


  // Handle RTLD_DEFAULT starts search at first loaded object
  if (handle == RTLD_DEFAULT || libname != NULL) {
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
  while (map) {
    // printf("l_name: %s\n", map->l_name);
    /* If the caller specified a specific library name, only search through
     * that.
     */
    if (libname == NULL ||
        (strlen(map->l_name) > 0 && strstr(map->l_name, libname)))  {
      // Search current library
      result = dlsym_default_internal_library_handler((void*) map,
                                                      symbol,
                                                      version,
                                                      tags_p,
                                                      default_symbol_index_p);
    }
    if (result) {
      return result;
    }

    // Change link map to next library
    map = map->l_next;
  }
  return NULL;
}

// Produces an error message and hard fails if no default_symbol was found.
static void
print_debug_messages(dt_tag tags,
                     Elf32_Word default_symbol_index,
                     const char *symbol)
{
#ifdef VERBOSE
  if (default_symbol_index) {
    JTRACE("** st_value: ")
          (tags.base_addr + tags.symtab[default_symbol_index].st_value);
    JTRACE("** symbol version: ")
          (version_name(tags.versym[default_symbol_index], &tags));
  }
#endif /* ifdef VERBOSE */
  if (!default_symbol_index) {
    JTRACE("ERROR:  No default symbol version found"
           "        Extend code to look for hidden symbols?")(symbol);
  }
}

// Like dlsym but finds the 'default' symbol of a library (the symbol that the
// dynamic executable automatically links to) rather than the oldest version
// which is what dlsym finds

/*
 * This implementation tries to mimic the behavior of the linking-loader,
 * as opposed to dlsym().  In particular, if no versioned symbol exists,
 * then the standard symbol is returned.  If more than one versioned symbol
 * exists, and all but one have the hidden bit set, then the version without
 * the hidden bit is returned.  If only one versioned symbol exists, then
 * it is returned whether the hidden bit is set or not.  From examples
 * in various libraries, it seems that when only one versioned symbol
 * exists, it has the hidden bit set.  If two or more versions of the
 * symbol exist, and the hidden bit is set in all cases, then the newest
 * version is returned.  If two or more versions of the symbol exist in
 * which the hidden bit is not set, then the behavior is undefined. [
 * OR DO WE HAVE A FIXED BEHAVIOR HERE? I THINK THIS CASE DOESN'T OCCUR
 * IN THE ACTUAL LIBRARIES. ] If the unversioned symbol and the versioned
 * symbol both exist, then the versioned symbol is preferred. [LET'S CHECK
 * THE CORRECTNESS OF THIS LAST RULE.]  Note that dlsym() in libdl.so seems
 * to follow the unusual rule of ignoring the hidden bit, and choosing a
 * somewhat arbtrary version that is often the oldest version.
 */

EXTERNC void *
dmtcp_dlsym(void *handle, const char *symbol)
{
  dt_tag tags;
  Elf32_Word default_symbol_index = 0;

#ifdef __USE_GNU
  if (handle == RTLD_NEXT || handle == RTLD_DEFAULT) {
    // Determine where this function will return
    void *return_address = __builtin_return_address(0);

    // Search for symbol using given pseudo-handle order
    void *result = dlsym_default_internal_flag_handler(handle, NULL, symbol,
                                                       NULL,
                                                       return_address, &tags,
                                                       &default_symbol_index);
    print_debug_messages(tags, default_symbol_index, symbol);
    return result;
  }
#endif /* ifdef __USE_GNU */

  void *result = dlsym_default_internal_library_handler(handle, symbol,
                                                        NULL, &tags,
                                                        &default_symbol_index);
  print_debug_messages(tags, default_symbol_index, symbol);
  return result;
}

EXTERNC void *
dmtcp_dlvsym(void *handle, char *symbol, const char *version)
{
  dt_tag tags;
  Elf32_Word default_symbol_index = 0;

#ifdef __USE_GNU
  if (handle == RTLD_NEXT || handle == RTLD_DEFAULT) {
    // Determine where this function will return
    void* return_address = __builtin_return_address(0);
    // Search for symbol using given pseudo-handle order
    void *result = dlsym_default_internal_flag_handler(handle, NULL, symbol,
                                                       version,
                                                       return_address, &tags,
                                                       &default_symbol_index);
    return result;
  }
#endif

  void *result = dlsym_default_internal_library_handler(handle, symbol, version,
                                                        &tags,
                                                        &default_symbol_index);
  return result;
}

EXTERNC void *
dmtcp_dlsym_lib(const char *libname, const char *symbol)
{
  dt_tag tags;
  Elf32_Word default_symbol_index = 0;

  // Determine where this function will return
  void* return_address = __builtin_return_address(0);
  void *result = dlsym_default_internal_flag_handler(NULL, libname, symbol,
                                                     NULL,
                                                     return_address, &tags,
                                                     &default_symbol_index);
  return result;
}

/*
 * Returns the offset of the given function within the given shared library
 * or LIB_FNC_OFFSET_FAILED if the function does not exist in the library
 */
EXTERNC uint64_t
dmtcp_dlsym_lib_fnc_offset(const char *libname, const char *symbol)
{
  dt_tag tags;
  uint64_t ret = LIB_FNC_OFFSET_FAILED;
  Elf32_Word default_symbol_index = 0;

  // Determine where this function will return
  void* return_address = __builtin_return_address(0);
  void *result = dlsym_default_internal_flag_handler(NULL, libname, symbol,
                                                     NULL,
                                                     return_address, &tags,
                                                     &default_symbol_index);
  if (result) {
    ret = (char*)result - tags.base_addr;
  }
  return ret;
}
