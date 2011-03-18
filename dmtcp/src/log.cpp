
/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
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

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "constants.h"
#include "log.h"
#include "synchronizationlogging.h"
#include "syscallwrappers.h"
#include "util.h"
#include "../jalib/jassert.h"

#ifdef RECORD_REPLAY
void dmtcp::SynchronizationLog::init(size_t size, bool readOnly, bool globalLog)
{
  //if (!SYNC_IS_RECORD || !SYNC_IS_REPLAY) {
    //return;
  //}

  if (globalLog) {
    init3(RECORD_LOG_PATH, size, readOnly);
  } else {
    init2(my_clone_id, size, readOnly);
    if (SYNC_IS_RECORD) {
      register_in_global_log_list(my_clone_id);
    }
  }

  JTRACE ("Initialized synchronization log path to" ) (_path) (_size);
}

void dmtcp::SynchronizationLog::init2(long long int clone_id, size_t size, bool readOnly)
{
  char buf[RECORD_LOG_PATH_MAX];
  snprintf(buf, RECORD_LOG_PATH_MAX, "%s_%Ld", RECORD_LOG_PATH, clone_id);

  init3(buf, size, readOnly);
  _cloneId = clone_id;
}

void dmtcp::SynchronizationLog::init3(const char *path, size_t size, bool readOnly)
{
  JASSERT(_startAddr == NULL);
  JASSERT(_log == NULL);
  JASSERT(_index == 0);
  JASSERT(_dataSize == 0);
  JASSERT(_entryIndex == 0);
  JASSERT(readOnly || _numEntries == NULL);
  JASSERT(_isLoaded == false);
  JASSERT(_isPatched == NULL);
  JASSERT(_isMerged == NULL);

  JASSERT(path != NULL);

  int fd = _real_open(path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
  JASSERT(fd != -1);

  JASSERT(_real_lseek(fd, size, SEEK_SET) == (off_t)size);
  Util::writeAll(fd, "", 1);

  SET_IN_MMAP_WRAPPER();
  _startAddr = (char*) _real_mmap(0, MAX_LOG_LENGTH, PROT_READ | PROT_WRITE,
                            MAP_SHARED, fd, 0);
  JASSERT(_startAddr != MAP_FAILED);
  UNSET_IN_MMAP_WRAPPER();

  _real_close(fd);

  _path = path;
  init_common(size, readOnly);
}

void dmtcp::SynchronizationLog::init4(size_t size)
{
  JASSERT(_startAddr == NULL);
  JASSERT(_log == NULL);
  JASSERT(_index == 0);
  JASSERT(_dataSize == 0);
  JASSERT(_entryIndex == 0);
  JASSERT(_numEntries == NULL);
  JASSERT(_isLoaded == false);
  JASSERT(_isPatched == NULL);
  JASSERT(_isMerged == NULL);

  SET_IN_MMAP_WRAPPER();
  _startAddr = (char*) _real_mmap(0, MAX_LOG_LENGTH, PROT_READ | PROT_WRITE,
                            MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  JASSERT(_startAddr != MAP_FAILED);
  UNSET_IN_MMAP_WRAPPER();

  _path = "";
  init_common(size);
}

void dmtcp::SynchronizationLog::init_common(size_t size, bool readOnly)
{
  if (!readOnly) {
    memset(_startAddr, 0, 4096);
//    LOG_IS_PATCHED_TYPE c = 0;
//    memcpy(_startAddr, (void *)&c, LOG_IS_PATCHED_SIZE);
//    LOG_IS_MERGED_TYPE m = 0;
//    memcpy(_startAddr + LOG_IS_PATCHED_SIZE, (void *)&m, LOG_IS_MERGED_SIZE);
  }

  _isPatched = (bool*) (_startAddr);
  _isMerged  = (bool*) (_startAddr + LOG_IS_PATCHED_SIZE);

  _numEntries = (size_t*) (_startAddr + LOG_IS_PATCHED_SIZE + LOG_IS_MERGED_SIZE);

  _size = size;
  _log = _startAddr + LOG_OFFSET_FROM_START;
  _isLoaded = true;
}


void dmtcp::SynchronizationLog::destroy()
{
  if (_startAddr != NULL) {
    JASSERT(_real_munmap(_startAddr, _size) == 0) (JASSERT_ERRNO);
  }
  _startAddr = _log = NULL;
  _path = "";
  _size = 0;
  _index = 0;
  _dataSize = 0;
  _entryIndex = 0;
  _numEntries = NULL;
  _isLoaded = false;
  _isPatched = NULL;
  _isMerged = NULL;
}

void dmtcp::SynchronizationLog::clearLog()
{
  JASSERT(_startAddr != NULL);
  bzero(_startAddr, _index);
  resetIndex();
  _dataSize = 0;
  _entryIndex = 0;
  _numEntries = NULL;
  _isPatched = NULL;
  _isMerged = NULL;
}

void dmtcp::SynchronizationLog::markAsPatched()
{
  JASSERT(_log != NULL);
  *_isPatched = LOG_IS_PATCHED_VALUE;
  _index = 0;
}

void dmtcp::SynchronizationLog::markAsMerged()
{
  JASSERT(_log != NULL);
  *_isMerged = LOG_IS_MERGED_VALUE;
  _index = 0;
}

bool dmtcp::SynchronizationLog::isEndOfLog()
{
  JASSERT(_log != NULL);
  return _log[_index] == '\0';
}

void dmtcp::SynchronizationLog::copyDataFrom(SynchronizationLog& other)
{
  if (other.numEntries() == 0) return;

  JASSERT(_log != NULL && other.logAddr() != NULL) (_log);
  JASSERT(empty() == false && other.empty() == false) (empty());
  JASSERT(numEntries() == other.numEntries()) (numEntries()) (other.numEntries());
  JASSERT(_dataSize == other.dataSize()) (_dataSize) (other.dataSize());

  memcpy(_log, other.logAddr(), other.dataSize());
  resetIndex();
}

void dmtcp::SynchronizationLog::appendDataFrom(SynchronizationLog& other)
{
  if (other.numEntries() == 0) return;
  JASSERT(_log != NULL && other.logAddr() != NULL) (_log);
  JASSERT(other.empty() == false);

  JASSERT(_index + other.dataSize() < _size)
    (_index) (other.dataSize()) (_size);

  memcpy(&_log[_index], other.logAddr(), other.dataSize());
  _index += other.dataSize();
  _dataSize += other.dataSize();
  *_numEntries += other.numEntries();
}

/* Given a clone_id, this returns the entry in patch_list with the lowest index
   and same clone_id.  If clone_id is 0, return the oldest entry, regardless of
   clone_id. If clone_id != 0 and no entry found, returns EMPTY_LOG_ENTRY. */
log_entry_t dmtcp::SynchronizationLog::getFirstEntryWithCloneID(int clone_id)
{
  size_t savedIndex = _index;
  size_t savedEntryIndex = _entryIndex ;
  log_entry_t entry = EMPTY_LOG_ENTRY;

  resetIndex();
  size_t oldIndex = _index;
  size_t oldEntryIndex = _entryIndex;
  while (getNextEntry(entry) != 0) {
    if (GET_COMMON(entry, clone_id) == clone_id) {
      break;
    }
    oldIndex = _index;
  }

  if (GET_COMMON(entry, clone_id) == 0) {
    _index = savedIndex;
    _entryIndex = savedEntryIndex;
    return entry;
  }

  size_t event_size = 0;
  GET_EVENT_SIZE(GET_COMMON(entry, event), event_size);

  memmove(&_log[oldIndex], &_log[_index], savedIndex - _index);

  _index = savedIndex - (event_size + log_event_common_size);

  _dataSize -= (event_size + log_event_common_size);

  _entryIndex = oldEntryIndex;
  *_numEntries -= 1;

  return entry;
}

int dmtcp::SynchronizationLog::getNextEntry(log_entry_t& entry)
{
  size_t event_size;

  JASSERT ((_index + sizeof (log_entry_t)) < _size) 
    (_index) (sizeof(log_entry_t)) (_size);

  if (_entryIndex == *_numEntries) {
    entry = EMPTY_LOG_ENTRY;
    return 0;
  }

  getNextEntryHeader(entry);

  if (GET_COMMON(entry, clone_id) == 0) {
    return 0;
  }

  GET_EVENT_SIZE(GET_COMMON(entry, event), event_size);

  JASSERT ((_index + log_event_common_size + event_size) < MAX_LOG_LENGTH)
    (_index) (log_event_common_size) (event_size) (MAX_LOG_LENGTH);

  _entryIndex++;
  READ_ENTRY_FROM_LOG(_log, _index, entry);

  JASSERT(false) .Text("Unreachable");
  return -1;
}

int dmtcp::SynchronizationLog::replaceEntry(const log_entry_t& entry)
{
  int event_size;
  GET_EVENT_SIZE(GET_COMMON(entry, event), event_size);

  JASSERT ((_index + log_event_common_size + event_size) < MAX_LOG_LENGTH)
    .Text ("Log size too large");

  writeEntryHeader(entry);

  int ret_log_event;
  WRITE_ENTRY_TO_LOG(_log, _index, entry, ret_log_event);
  _index += ret_log_event;
  _entryIndex++;

  return ret_log_event + log_event_common_size;
}

int dmtcp::SynchronizationLog::writeEntry(const log_entry_t& entry)
{
  if (__builtin_expect(_startAddr == 0, 0)) {
    JASSERT(false);
  }

  int event_size;
  GET_EVENT_SIZE(GET_COMMON(entry, event), event_size);

  JASSERT ((_index + log_event_common_size + event_size) < MAX_LOG_LENGTH)
    .Text ("Log size too large");

  writeEntryHeader(entry);

  int ret_log_event;
  WRITE_ENTRY_TO_LOG(_log, _index, entry, ret_log_event);
  _index += ret_log_event;

  _dataSize += ret_log_event + log_event_common_size;

  *_numEntries += 1;
  _entryIndex++;

  return ret_log_event + log_event_common_size;
}

void dmtcp::SynchronizationLog::getNextEntryHeader(log_entry_t& entry)
{
#ifdef NO_LOG_ENTRY_TO_BUFFER
  memcpy(&entry.header, &_log[_index], log_event_common_size);
#else
  char* buffer = &_log[_index];

  memcpy(&GET_COMMON(entry, event), buffer, sizeof(GET_COMMON(entry, event)));
  buffer += sizeof(GET_COMMON(entry, event));
  memcpy(&GET_COMMON(entry, log_id), buffer, sizeof(GET_COMMON(entry, log_id)));
  buffer += sizeof(GET_COMMON(entry, log_id));
  memcpy(&GET_COMMON(entry, tid), buffer, sizeof(GET_COMMON(entry, tid)));
  buffer += sizeof(GET_COMMON(entry, tid));
  memcpy(&GET_COMMON(entry, clone_id), buffer, sizeof(GET_COMMON(entry, clone_id)));
  buffer += sizeof(GET_COMMON(entry, clone_id));
  memcpy(&GET_COMMON(entry, my_errno), buffer, sizeof(GET_COMMON(entry, my_errno)));
  buffer += sizeof(GET_COMMON(entry, my_errno));
  memcpy(&GET_COMMON(entry, retval), buffer, sizeof(GET_COMMON(entry, retval)));
  buffer += sizeof(GET_COMMON(entry, retval));

  JASSERT((buffer - &_log[_index]) == log_event_common_size)
    (_index) (log_event_common_size) (buffer);
#endif

  _index += log_event_common_size;
}

void dmtcp::SynchronizationLog::writeEntryHeader(const log_entry_t& entry)
{
#ifdef NO_LOG_ENTRY_TO_BUFFER
  memcpy(&_log[_index], &entry.header, log_event_common_size);
#else
  char* buffer = &_log[_index];

  memcpy(buffer, &GET_COMMON(entry, event), sizeof(GET_COMMON(entry, event)));
  buffer += sizeof(GET_COMMON(entry, event));
  memcpy(buffer, &GET_COMMON(entry, log_id), sizeof(GET_COMMON(entry, log_id)));
  buffer += sizeof(GET_COMMON(entry, log_id));
  memcpy(buffer, &GET_COMMON(entry, tid), sizeof(GET_COMMON(entry, tid)));
  buffer += sizeof(GET_COMMON(entry, tid));
  memcpy(buffer, &GET_COMMON(entry, clone_id), sizeof(GET_COMMON(entry, clone_id)));
  buffer += sizeof(GET_COMMON(entry, clone_id));
  memcpy(buffer, &GET_COMMON(entry, my_errno), sizeof(GET_COMMON(entry, my_errno)));
  buffer += sizeof(GET_COMMON(entry, my_errno));
  memcpy(buffer, &GET_COMMON(entry, retval), sizeof(GET_COMMON(entry, retval)));
  buffer += sizeof(GET_COMMON(entry, retval));

  JASSERT((buffer - &_log[_index]) == log_event_common_size)
    (_index) (log_event_common_size) (buffer);
#endif

  _index += log_event_common_size;
}

#endif
