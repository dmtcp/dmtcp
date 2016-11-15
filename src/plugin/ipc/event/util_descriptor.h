/****************************************************************************
 *   Copyright (C) 2012 by Onyeka Igabari                                   *
 *                                                                          *
 *   This file is part of the dmtcp/src module of DMTCP (DMTCP:dmtcp/src).  *
 *                                                                          *
 *  DMTCP:dmtcp/src is free software: you can redistribute it and/or        *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP:dmtcp/src is distributed in the hope that it will be useful,      *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

/******************************************************************
 * File:   util_descriptor.h
 *
 * Author: onyeka Igabari
 *
 * Description: Declares the Descriptor class for handling memory
 *              allocation for system calls with different descriptors
 *
 * Created on July 11, 2012, 6:02 PM
******************************************************************/

#pragma once
#ifndef UTIL_DESCRIPTOR_H
#define UTIL_DESCRIPTOR_H

#include <signal.h>

# define SUCCESS         0
# define FAILURE         -1
# define MAX_DESCRIPTORS 24
# define MAX_PATH_LEN    48

typedef enum {
  UNUSED_DESCRIPTOR,
  TIMER_CREATE_DECRIPTOR,
  INOTIFY_ADD_WATCH_DESCRIPTOR,
  MAX_NUM_OF_DESCRIPTORS
} descriptor_type_e;

typedef struct {
  descriptor_type_e type;
  int watch_descriptor;
  int file_descriptor;
  unsigned int mask;
  char pathname[MAX_PATH_LEN];
} inotify_add_watch_t;

typedef struct {
  descriptor_type_e type;
  clockid_t clockid;
  sigevent signal_event;
  timer_t timerid;
} timer_create_t;

typedef union {
  timer_create_t create_timer;
  inotify_add_watch_t add_watch;
} descriptor_types_u;

namespace dmtcp
{
namespace Util
{
class Descriptor
{
  static descriptor_types_u *descrip_types_p[MAX_DESCRIPTORS];
  static unsigned int descriptor_counter;
  static bool is_initialized;
  int remove_timer_descriptor(timer_t timer_id);
  int remove_inotify_watch_descriptor(int watch_descriptor);

  public:
    Descriptor();
    ~Descriptor();
    unsigned int count_descriptors();
    void add_descriptor(descriptor_types_u *descriptor);
    int remove_descriptor(descriptor_type_e type, void *descriptor);
    bool get_descriptor(unsigned int index,
                        descriptor_type_e type,
                        descriptor_types_u *descriptor);
};
}
}
#endif  /* UTIL_DESCRIPTOR_H */
