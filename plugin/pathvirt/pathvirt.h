#ifndef PATHVIRT_H
#define PATHVIRT_H

#include "dmtcp.h"

extern void set_old_path_prefix_list(const char* oldPathPrefix) __attribute__((weak));

extern void set_new_path_prefix_list(const char* newPathPrefix) __attribute__((weak));

extern const char* get_old_path_prefix_list() __attribute__((weak));

extern const char* get_new_path_prefix_list() __attribute__((weak));

#endif
