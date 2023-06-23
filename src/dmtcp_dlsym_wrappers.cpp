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
#include "threadsync.h"


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
  if (!default_symbol_index) {
    JTRACE("ERROR:  No default symbol version found"
           "        Extend code to look for hidden symbols?")(symbol);
  }
#endif /* ifdef VERBOSE */
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
 * somewhat arbitrary version that is often the oldest version.
 */

EXTERNC void *
dmtcp_dlsym(void *handle, const char *symbol)
{
  // Acquire checkpoint lock. Released on return from this function as part of
  // object destructor.
  dmtcp::WrapperLock wrapperExecutionLock;

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
  // Acquire checkpoint lock. Released on return from this function as part of
  // object destructor.
  dmtcp::WrapperLock wrapperExecutionLock;

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
  // Acquire checkpoint lock. Released on return from this function as part of
  // object destructor.
  dmtcp::WrapperLock wrapperExecutionLock;

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
