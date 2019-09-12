/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, Gene Cooperman,    *
 *                                                           and Rohan Garg *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu, and         *
 *                                                      rohgarg@ccs.neu.edu *
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

#pragma once
#ifndef FILECONNECTION_H
#define FILECONNECTION_H

# include <mqueue.h>
# include <signal.h>
# include <stdint.h>
# include <sys/socket.h>
# include <sys/stat.h>
# include <sys/types.h>
# include <sys/types.h>
# include <unistd.h>

# include "jbuffer.h"
# include "jconvert.h"
# include "jfilesystem.h"

# include "connection.h"

namespace dmtcp
{
class StdioConnection : public Connection
{
  public:
    enum StdioType {
      STDIO_IN = STDIO,
      STDIO_OUT,
      STDIO_ERR,
      STDIO_INVALID
    };

    StdioConnection(int fd) : Connection(STDIO_IN + fd)
    {
      JTRACE("creating stdio connection") (fd) (id());
      JASSERT(jalib::Between(0, fd, 2)) (fd)
      .Text("invalid fd for StdioConnection");
    }

    StdioConnection() {}

    virtual void drain() {}

    virtual void refill(bool isRestart) {}

    virtual void postRestart();
    virtual void serializeSubClass(jalib::JBinarySerializer &o) {}

    virtual string str() { return "<STDIO>"; }
};

class FileConnection : public Connection
{
  public:
    enum FileType {
      FILE_INVALID = FILE,
      FILE_REGULAR,
      FILE_SHM,
      FILE_PROCFS,
      FILE_DELETED,
      FILE_BATCH_QUEUE
    };

    FileConnection() {}

    FileConnection(const string &path,
                   int flags,
                   mode_t mode,
                   int type = FILE_REGULAR)
      : Connection(type)
      , _path(path)
      , _fileAlreadyExists(false)
    { }

    virtual void doLocking();
    virtual void drain();
    virtual void preCkpt();
    virtual void refill(bool isRestart);
    virtual void postRestart();
    virtual void resume(bool isRestart);

    virtual void serializeSubClass(jalib::JBinarySerializer &o);

    virtual string str() { return _path; }

    string filePath() { return _path; }

    string savedFilePath() { return _savedFilePath; }

    bool checkpointed() { return _ckpted_file; }

    void doNotRestoreCkptCopy() { _ckpted_file = false; }

    dev_t devnum() const { return _st_dev; }

    ino_t inode() const { return _st_ino; }

    bool checkDup(int fd, const char *npath);

  private:
    int openFile();
    void refreshPath();
    void calculateRelativePath();
    string getSavedFilePath(const string &path);
    void overwriteFileWithBackup(int savedFd);

    string _path;
    string _savedFilePath;
    string _rel_path;
    string _ckptFilesDir;
    int32_t _ckpted_file;
    int32_t _allow_overwrite;
    int32_t _fileAlreadyExists;
    int32_t _rmtype;

    // int64_t       _flags;

    /* No method uses _mode yet.  Stop compiler from issuing warning. */
    /* int64_t       _mode; */
    int64_t _offset;
    uint64_t _st_dev;
    uint64_t _st_ino;
    int64_t _st_size;
};

class FifoConnection : public Connection
{
  public:
    FifoConnection() {}

    FifoConnection(const string &path, int flags, mode_t mode)
      : Connection(FIFO)
      , _path(path)
    {
      string curDir = jalib::Filesystem::GetCWD();
      int offs = _path.find(curDir);

      if (offs < 0) {
        _rel_path = "*";
      } else {
        offs += curDir.size();
        offs = _path.find('/', offs);
        offs++;
        _rel_path = _path.substr(offs);
      }
      JTRACE("New Fifo connection created") (_path) (_rel_path);
      _in_data.clear();
    }

    virtual void drain();
    virtual void refill(bool isRestart);
    virtual void postRestart();

    virtual string str() { return _path; }

    virtual void serializeSubClass(jalib::JBinarySerializer &o);

  private:
    int openFile();
    void refreshPath();
    string getSavedFilePath(const string &path);
    string _path;
    string _rel_path;
    string _savedRelativePath;
    int64_t _flags;
    int64_t _mode;
    vector<char>_in_data;
    int32_t ckptfd;
};

class PosixMQConnection : public Connection
{
  public:
    inline PosixMQConnection(const char *name,
                             int oflag,
                             mode_t mode,
                             struct mq_attr *attr)
      : Connection(POSIXMQ)
      , _name(name)
      , _oflag(oflag)
      , _mode(mode)
      , _qnum(0)
      , _notifyReg(false)
    {
      if (attr != NULL) {
        _attr = *attr;
      }
    }

    virtual void doLocking();
    virtual void drain();
    virtual void refill(bool isRestart);
    virtual void postRestart();

    virtual void serializeSubClass(jalib::JBinarySerializer &o);

    virtual string str() { return _name; }

    void on_mq_close();
    void on_mq_notify(const struct sigevent *sevp);

  private:
    string _name;
    int64_t _oflag;
    int64_t _mode;
    struct mq_attr _attr;
    int64_t _qnum;
    char _notifyReg;
    struct sigevent _sevp;
    vector<jalib::JBuffer>_msgInQueue;
    vector<uint32_t>_msgInQueuePrio;
};
}
#endif // ifndef FILECONNECTION_H
