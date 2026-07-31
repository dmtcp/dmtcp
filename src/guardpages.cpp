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

#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include "guardpages.h"
#include "dmtcpalloc.h"
#include "util.h"
#include "dmtcp_assert.h"

using namespace dmtcp;

// Guard ranges in this process, sorted by start address and non-overlapping.
// Allocated on first use and reused, so refilling it does not allocate.
static vector<MemRegion> *guardRanges = NULL;

// Appends [start, end), merging it with the previous entry if the two abut,
// which happens when one run spans two adjacent areas.
static void
addRange(uintptr_t start, uintptr_t end)
{
  ASSERT_LT(start, end, "empty guard range: start={} end={}",
            (void *)start, (void *)end);

  if (!guardRanges->empty()) {
    MemRegion &last = guardRanges->back();
    ASSERT_GE(start, last.endAddr,
              "guard range overlaps or precedes the previous one: "
              "start={} end={} previous=[{},{})",
              (void *)start, (void *)end,
              (void *)last.startAddr, (void *)last.endAddr);
    if (start == last.endAddr) {
      last.endAddr = (uint64_t)end;
      return;
    }
  }

  MemRegion range = { (uint64_t)start, (uint64_t)end };
  guardRanges->push_back(range);
}

bool
GuardPages::beginRecording()
{
  // Discard first, before anything can return early: the writers consult
  // nextRangeAfter() unconditionally, so a range left from an earlier cycle
  // would make them skip an address that now holds ordinary data.
  if (guardRanges != NULL) {
    guardRanges->clear();
  }

  if (!Util::guardPagesSupported()) {
    return false;
  }

  if (guardRanges == NULL) {
    guardRanges = new vector<MemRegion>();
  }
  return true;
}

// Scans every area regardless of type: the kernel accepts MADV_GUARD_INSTALL on
// shared, file-backed and read-only mappings, not just the writable anonymous
// private ones madvise(2) documents.
void
GuardPages::record(const Area &area)
{
  uintptr_t addr = (uintptr_t)area.addr;
  const uintptr_t areaEnd = (uintptr_t)area.endAddr;

  while (addr < areaEnd) {
    uintptr_t guardStart = 0;
    uintptr_t guardEnd = 0;
    if (!Util::scanNextGuardRange(addr, areaEnd, &guardStart, &guardEnd)) {
      break;
    }
    TRACE("recording guard page range: addr={} size={} area={}",
          (void *)guardStart, guardEnd - guardStart,
          area.name[0] ? area.name : "(anonymous)");
    addRange(guardStart, guardEnd);
    addr = guardEnd;
  }
}

bool
GuardPages::nextRangeAfter(uintptr_t addr, uintptr_t *start, uintptr_t *end)
{
  if (guardRanges == NULL) {
    return false;
  }
  for (const MemRegion &r : *guardRanges) {
    if (addr < r.endAddr) {
      *start = (uintptr_t)r.startAddr;
      *end = (uintptr_t)r.endAddr;
      return true;
    }
  }
  return false;
}

void
GuardPages::reinstall()
{
  if (guardRanges == NULL || guardRanges->empty()) {
    return;
  }

  size_t failed = 0;
  for (const MemRegion &r : *guardRanges) {
    // No mprotect needed: the install accepts read-only and PROT_NONE
    // mappings, and a guard page survives a later protection change.
    if (madvise((void *)r.startAddr, r.endAddr - r.startAddr,
                MADV_GUARD_INSTALL) != 0) {
      const int savedErrno = errno;
      failed++;
      TRACE("failed to reinstall guard page: addr={} size={} errno={} ({})",
            (void *)r.startAddr, r.endAddr - r.startAddr,
            savedErrno, strerror(savedErrno));
    }
  }

  WARN_EQ(0, failed,
          "could not reinstall {} of {} guard page range(s); the application's "
          "guard pages (e.g. thread stack-overflow detection) are not in "
          "effect. Does this kernel support madvise(MADV_GUARD_INSTALL)?",
          failed, guardRanges->size());
  TRACE("reinstalled guard page ranges after restart: total={} failed={}",
        guardRanges->size(), failed);
}
