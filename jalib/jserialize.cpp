/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel                                 *
 *   jansel@csail.mit.edu                                                   *
 *                                                                          *
 *   This file is part of the JALIB module of DMTCP (DMTCP:dmtcp/jalib).    *
 *                                                                          *
 *  DMTCP:dmtcp/jalib is free software: you can redistribute it and/or      *
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

#include "jalib.h"
#define DMTCP_LOG_COMPONENT "jalib"
#include "jserialize.h"
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

jalib::JBinarySerializeWriterRaw::JBinarySerializeWriterRaw(
  const dmtcp::string &path, int fd)
  : JBinarySerializer(path)
  , _fd(fd)
{
  ASSERT_ERRNO(_fd >= 0, "open(path) failed: path={}", path);
}

jalib::JBinarySerializeWriter::JBinarySerializeWriter(const dmtcp::string &path)
  : JBinarySerializeWriterRaw(path,
                              jalib::open(path.c_str(),
                                          O_CREAT | O_WRONLY | O_TRUNC, 0600))
{}

jalib::JBinarySerializeReaderRaw::JBinarySerializeReaderRaw(
  const dmtcp::string &path, int fd)
  : JBinarySerializer(path)
  , _fd(fd)
{
  ASSERT_ERRNO(_fd >= 0, "open(path) failed: path={}", path);
}

jalib::JBinarySerializeReader::JBinarySerializeReader(const dmtcp::string &path)
  : JBinarySerializeReaderRaw(path, jalib::open(path.c_str(), O_RDONLY, 0))
{}

jalib::JBinarySerializeWriter::~JBinarySerializeWriter()
{
  close(_fd);
}

jalib::JBinarySerializeReader::~JBinarySerializeReader()
{
  close(_fd);
}

bool
jalib::JBinarySerializeWriterRaw::isReader() { return false; }

bool
jalib::JBinarySerializeReaderRaw::isReader() { return true; }

// Rewind file descriptor to start value
void
jalib::JBinarySerializeWriterRaw::rewind()
{
  ASSERT_ERRNO(lseek(_fd, 0, SEEK_SET) == 0, "cannot rewind serializer");
}

void
jalib::JBinarySerializeReaderRaw::rewind()
{
  ASSERT_ERRNO(lseek(_fd, 0, SEEK_SET) == 0, "cannot rewind serializer");
}

bool
jalib::JBinarySerializeWriterRaw::isempty()
{
  struct stat buf;

  ASSERT_ERRNO(fstat(_fd, &buf) == 0, "failed to stat serializer fd");
  return buf.st_size == 0;
}

bool
jalib::JBinarySerializeReaderRaw::isempty()
{
  struct stat buf;

  ASSERT_ERRNO(fstat(_fd, &buf) == 0, "failed to stat serializer fd");
  return buf.st_size == 0;
}

bool
jalib::JBinarySerializeReaderRaw::isEOF()
{
  struct stat buf;

  ASSERT_ERRNO(fstat(_fd, &buf) == 0, "failed to stat serializer fd");

  off_t cur = lseek(_fd, 0, SEEK_CUR);
  ASSERT_ERRNO(cur != -1, "failed to query serializer fd offset");

  return cur == buf.st_size;
}

void
jalib::JBinarySerializeWriterRaw::readOrWrite(void *buffer, size_t len)
{
  size_t ret = jalib::writeAll(_fd, buffer, len);

  ASSERT_ERRNO(ret == len,
               "write() failed: file={} expected_bytes={} actual_bytes={}",
               filename(), len, ret);
  _bytes += len;
}

void
jalib::JBinarySerializeReaderRaw::readOrWrite(void *buffer, size_t len)
{
  size_t ret = jalib::readAll(_fd, buffer, len);

  ASSERT_ERRNO(ret == len,
               "read() failed: file={} expected_bytes={} actual_bytes={}",
               filename(), len, ret);
  _bytes += len;
}
