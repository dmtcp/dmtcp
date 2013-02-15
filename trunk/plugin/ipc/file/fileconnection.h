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

// THESE INCLUDES ARE IN RANDOM ORDER.  LET'S CLEAN IT UP AFTER RELEASE. - Gene
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <mqueue.h>
#include <stdint.h>
#include <signal.h>
#include "jfilesystem.h"
#include "jbuffer.h"
#include "jconvert.h"
#include "connection.h"

namespace dmtcp
{
  class PtyConnection : public Connection
  {
    public:
      enum PtyType
      {
        PTY_INVALID = Connection::PTY,
        PTY_DEV_TTY,
        PTY_CTTY,
        PTY_MASTER,
        PTY_SLAVE,
        PTY_BSD_MASTER,
        PTY_BSD_SLAVE

          //        TYPEMASK = PTY_CTTY | PTY_Master | PTY_Slave
      };

      PtyConnection() {}
      PtyConnection(int fd, const char *path, int flags, mode_t mode, int type);

      int  ptyType() { return _type;}// & TYPEMASK);
      dmtcp::string ptsName() { return _ptsName;; }
      dmtcp::string virtPtsName() { return _virtPtsName;; }

      virtual void drain();
      virtual void refill(bool isRestart);
      virtual void postRestart();
      virtual void serializeSubClass(jalib::JBinarySerializer& o);
      virtual string str() { return _masterName + ":" + _ptsName; }
    private:
      //PtyType   _type;
      dmtcp::string _masterName;
      dmtcp::string _ptsName;
      dmtcp::string _virtPtsName;
      int           _flags;
      mode_t        _mode;
      bool          _ptmxIsPacketMode;

  };

  class StdioConnection : public Connection
  {
    public:
      enum StdioType
      {
        STDIO_IN = STDIO,
        STDIO_OUT,
        STDIO_ERR,
        STDIO_INVALID
      };

      StdioConnection(int fd): Connection(STDIO + fd) {
        JTRACE("creating stdio connection") (fd) (id());
        JASSERT(jalib::Between(0, fd, 2)) (fd)
          .Text("invalid fd for StdioConnection");
      }

      StdioConnection() {}

      virtual void drain() {}
      virtual void refill(bool isRestart) {}
      virtual void postRestart();
      virtual void serializeSubClass(jalib::JBinarySerializer& o) {}

      virtual string str() { return "<STDIO>"; };
  };

  class FileConnection : public Connection
  {
    public:
      enum FileType
      {
        FILE_INVALID = FILE,
        FILE_REGULAR,
        FILE_PROCFS,
        FILE_DELETED,
        FILE_BATCH_QUEUE
      };

      FileConnection() {}
      FileConnection(const dmtcp::string& path, int flags, mode_t mode,
                     int type = FILE_REGULAR)
        : Connection(FILE)
        , _path(path)
        , _flags(flags)
        , _mode(mode)
        , _fileAlreadyExists(false)
      {
         _type = type;
      }


      virtual void doLocking();
      virtual void drain();
      virtual void refill(bool isRestart);
      virtual void postRestart();
      virtual void resume(bool isRestart);

      virtual void serializeSubClass(jalib::JBinarySerializer& o);

      virtual string str() { return _path; }
      dmtcp::string filePath() { return _path; }
      void updatePath();
      bool checkpointed() { return _checkpointed; }
      void doNotRestoreCkptCopy() { _checkpointed = false; }

      int fileType() { return _type; }

      bool checkDup(int fd);
    private:
      void saveFile();
      int  openFile();
      void refreshPath();
      void handleUnlinkedFile();
      void calculateRelativePath();
      dmtcp::string getSavedFilePath(const dmtcp::string& path);

      dmtcp::string _path;
      dmtcp::string _rel_path;
      dmtcp::string _ckptFilesDir;
      bool          _checkpointed;
      bool          _fileAlreadyExists;
      int           _flags;
      mode_t        _mode;
      off_t         _offset;
      struct stat   _stat;
      int           _rmtype;
  };

  class FifoConnection : public Connection
  {
    public:

      FifoConnection() {}
      FifoConnection(const dmtcp::string& path, int flags, mode_t mode)
        : Connection(FIFO)
          , _path(path)
    {
      dmtcp::string curDir = jalib::Filesystem::GetCWD();
      int offs = _path.find(curDir);
      if (offs < 0) {
        _rel_path = "*";
      } else {
        offs += curDir.size();
        offs = _path.find('/',offs);
        offs++;
        _rel_path = _path.substr(offs);
      }
      JTRACE("New Fifo connection created") (_path) (_rel_path);
      _in_data.clear();
    }

      virtual void drain();
      virtual void refill(bool isRestart);
      virtual void postRestart();

      virtual string str() { return _path; };
      virtual void serializeSubClass(jalib::JBinarySerializer& o);

    private:
      int  openFile();
      void refreshPath();
      dmtcp::string getSavedFilePath(const dmtcp::string& path);
      dmtcp::string _path;
      dmtcp::string _rel_path;
      dmtcp::string _savedRelativePath;
      int           _flags;
      mode_t        _mode;
      struct stat _stat;
      vector<char> _in_data;
      int ckptfd;
  };

  class PosixMQConnection: public Connection
  {
    public:
      inline PosixMQConnection(const char *name, int oflag, mode_t mode,
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

      virtual void drain();
      virtual void refill(bool isRestart);
      virtual void postRestart();

      virtual void serializeSubClass(jalib::JBinarySerializer& o);

      virtual string str() { return _name; }

      void on_mq_close();
      void on_mq_notify(const struct sigevent *sevp);

    private:
      dmtcp::string  _name;
      int            _oflag;
      mode_t         _mode;
      struct mq_attr _attr;
      long           _qnum;
      bool           _notifyReg;
      struct sigevent _sevp;
      dmtcp::vector<jalib::JBuffer> _msgInQueue;
      dmtcp::vector<unsigned> _msgInQueuePrio;
  };

}

#endif
