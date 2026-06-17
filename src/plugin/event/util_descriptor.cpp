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
 * File:   util_descriptor.cpp
 *
 * Author: onyeka Igabari
 *
 * Description: Implements a class for handling memory allocation
 *              for system calls with different descriptors
 *
 * Created on July 05, 2012, 11:52 PM
******************************************************************/

#include "util.h"
#include <stdbool.h>
#include <stdio.h>
#include <sys/mman.h>
#include <iostream>
#include "../jalib/jalloc.h"
#include "util_assert.h"
#include "util_descriptor.h"

using namespace dmtcp;

/******************************************************************
 * Class Variables: is_initialized, descriptor_counter,
 *                  descript_types_p
 *
 * Description:    Initialize variables belonging to the Descriptor class
 *
 * Parameters:     NONE
 *
 * Return:         NONE
 ******************************************************************/
bool Util::Descriptor::is_initialized = false;
unsigned int Util::Descriptor::descriptor_counter = 0;
descriptor_types_u *Util::Descriptor::descrip_types_p[MAX_DESCRIPTORS] = { 0 };

/******************************************************************
 * Class Function: Descriptor
 *
 * Description:    Constructor of the Descriptor class
 *
 * Parameters:     NONE
 *
 * Return:         NONE
 ******************************************************************/
Util::Descriptor::Descriptor()
{
  if (false == is_initialized) {
    descriptor_counter = 0;
    is_initialized = true;

    TRACE("Initializing descriptor store");

    // allocate memory for MAX_DESCRIPTORS that would be stored
    for (int i = 0; i < MAX_DESCRIPTORS; i++) {
      // TODO: Is this a potential memory leak? Should this class be a proper singleton instead?
      void *mem_ptr = JALLOC_MALLOC(sizeof(descriptor_types_u));
      if (MAP_FAILED == mem_ptr) {
        TRACE("Failed to allocate descriptor storage");
        break;
      } else {
        descrip_types_p[i] = (descriptor_types_u *)mem_ptr;
      }
    }
  } else {
    TRACE("Descriptor store is already initialized");
  }
}

/******************************************************************
 * Class Function: ~Descriptor
 *
 * Description:    Destructor of the Descriptor class
 *
 * Parameters:     NONE
 *
 * Return:         NONE
 ******************************************************************/
Util::Descriptor::~Descriptor()
{
  if (true == is_initialized) {
    TRACE("Destroying descriptor store");
  }
}

/******************************************************************
 * Class Function: add_descriptor
 *
 * Description:    Adds descriptors
 *
 * Parameters:     descriptor
 *
 * Return:         NONE
 ******************************************************************/
void
Util::Descriptor::add_descriptor(descriptor_types_u *descriptor)
{
  ASSERT_NOT_NULL(descriptor);
  if (descriptor_counter < MAX_DESCRIPTORS) {
    TRACE("Adding saved descriptor: index={} storage={}",
          descriptor_counter, descrip_types_p[descriptor_counter]);
    memcpy(descrip_types_p[descriptor_counter],
           descriptor, sizeof(descriptor_types_u));
    descriptor_counter++;
  } else {
    TRACE("Descriptor store is full: max_descriptors={}", MAX_DESCRIPTORS);
  }
}

/******************************************************************
 * Class Function: remove_descriptor
 *
 * Description:    Removes descriptors
 *
 * Parameters:     type       - descriptor type
 *                 descriptor - descriptor to be removed
 *
 * Return:         NONE
 ******************************************************************/
int
Util::Descriptor::remove_descriptor(descriptor_type_e type, void *descriptor)
{
  int ret_val = FAILURE;

  ASSERT_NOT_NULL(descriptor);

  // determine which descriptor needs to be removed
  switch (type) {
  case TIMER_CREATE_DECRIPTOR:
  {
    timer_t timer_id;
    memcpy(&timer_id, descriptor, sizeof(timer_t));

    // calling timer function
    ret_val = remove_timer_descriptor(timer_id);
    break;
  }
  case INOTIFY_ADD_WATCH_DESCRIPTOR:
  {
    int watch_descriptor;
    memcpy(&watch_descriptor, descriptor, sizeof(int));

    // calling timer function
    ret_val = remove_inotify_watch_descriptor(watch_descriptor);
    break;
  }
  default:
  {
    TRACE("Unknown descriptor type: type={}", type);
    break;
  }
  }

  return ret_val;
}

/******************************************************************
 * Class Function: get_descriptor
 *
 * Description:    Returns the descriptor object if it is of the
 *                 type specified
 *
 * Parameters:     index       - index to descriptor objects
 *                 type        - descriptor type
 *                 descriptor  - descriptor structure returned
 *
 * Return:         true or false
 ******************************************************************/
bool
Util::Descriptor::get_descriptor(unsigned int index,
                                 descriptor_type_e type,
                                 descriptor_types_u *descriptor)
{
  bool ret_val = false;

  ASSERT_NOT_NULL(descriptor);
  TRACE("Looking up saved descriptor: index={} type={}", index, type);
  if ((descrip_types_p[index])->add_watch.type == type) {
    memcpy(descriptor, descrip_types_p[index], sizeof(descriptor_types_u));
    ret_val = true;
  } else {
    TRACE("Saved descriptor type mismatch: requested_type={} saved_type={}",
          type, (descrip_types_p[index])->add_watch.type);
  }

  return ret_val;
}

/******************************************************************
 * Class Function: count_descriptors
 *
 * Description:    Returns the number of descriptor
 *                 objects stored in dmtcp
 *
 * Parameters:     None
 *
 * Return:         the number stored
 ******************************************************************/
unsigned int
Util::Descriptor::count_descriptors()
{
  unsigned int count = descriptor_counter;

  return count;
}

/******************************************************************
 * Class Function: remove_timer_descriptor
 *
 * Description:    Removes timer descriptors
 *
 * Parameters:     timer_id       - timer descriptor
 *
 * Return:         NONE
 ******************************************************************/
int
Util::Descriptor::remove_timer_descriptor(timer_t timer_id)
{
  int i;
  int ret_val = FAILURE;

  for (i = 0; i < MAX_DESCRIPTORS; i++) {
    if ((descrip_types_p[i])->create_timer.type == TIMER_CREATE_DECRIPTOR) {
      TRACE("Checking saved timer descriptor: index={} timer_id={}",
            i, timer_id);
      if ((descrip_types_p[i])->create_timer.timerid == timer_id) {
        TRACE("Removing saved timer descriptor: index={} timer_id={}",
              i, timer_id);
        memset(descrip_types_p[i], 0, sizeof(descriptor_types_u));
        (descrip_types_p[i])->create_timer.type = UNUSED_DESCRIPTOR;

        // set the return value
        ret_val = SUCCESS;
        break;
      }
    }
  }

  return ret_val;
}

/******************************************************************
 * Class Function: remove_inotify_watch_descriptor
 *
 * Description:    Removes inotify watch descriptors
 *
 * Parameters:     watch_descriptor - watch descriptor
 *
 * Return:         NONE
 ******************************************************************/
int
Util::Descriptor::remove_inotify_watch_descriptor(int watch_descriptor)
{
  int i;
  int ret_val = FAILURE;

  for (i = 0; i < MAX_DESCRIPTORS; i++) {
    if ((descrip_types_p[i])->add_watch.type == INOTIFY_ADD_WATCH_DESCRIPTOR) {
      TRACE("Checking saved inotify watch descriptor: index={} wd={}",
            i, watch_descriptor);

      if ((descrip_types_p[i])->add_watch.watch_descriptor ==
          watch_descriptor) {
        TRACE("Removing saved inotify watch descriptor: index={} wd={}",
              i, watch_descriptor);
        memset((descrip_types_p[i]), 0, sizeof(descriptor_types_u));
        (descrip_types_p[i])->add_watch.type = UNUSED_DESCRIPTOR;

        // set the return value
        ret_val = SUCCESS;
        break;
      }
    }
  }

  return ret_val;
}
