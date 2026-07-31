/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, and gene@ccs.neu.edu          *
 *                                                                          *
 *   This file is part of the DMTCP.                                        *
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

#ifndef GUARDPAGES_H
#define GUARDPAGES_H

#include <stdint.h>
#include <stddef.h>
#include "procmapsarea.h"

// Tracks the lightweight guard pages (madvise(MADV_GUARD_INSTALL), Linux 6.13+)
// present in this process.  A guard page raises SIGSEGV on any access, so the
// checkpoint must not read one, and restart must put them back.
//
// The checkpoint never uninstalls one: writeckpt.cpp skips them instead, so
// reinstall() below is the only madvise() here.
//
// The list lives in DMTCP's heap, which is itself checkpointed, so it survives
// restart and the image records nothing.  It must therefore be complete before
// the first area is written, since the heap is one of those areas.
//
// Not covered: the file plugin replaces every writable MAP_SHARED file mapping
// with an anonymous PROT_NONE one during pre-checkpoint
// (FileConnList::prepareShmList), destroying any guard there before record()
// runs.

namespace dmtcp
{
namespace GuardPages
{
// Starts a recording pass, discarding the previous checkpoint's ranges.  False
// if this kernel cannot report guard pages; record() need not be called then.
bool beginRecording();

// Records the guard pages in 'area'.  Call for every area, before writing any.
void record(const Area &area);

// Finds the first recorded range ending after 'addr'.
bool nextRangeAfter(uintptr_t addr, uintptr_t *start, uintptr_t *end);

// Reinstalls the guard pages recorded at checkpoint time; failure warns, not
// fatal.  Must run after the DMTCP_EVENT_RESTART plugin hooks, which copy out
// of restored memory (faulting on a guard page) and re-mmap it (discarding a
// guard installed earlier), and before any user thread is released.
void reinstall();
} // namespace GuardPages
} // namespace dmtcp

#endif // ifndef GUARDPAGES_H
