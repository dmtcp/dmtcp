/***************************************************************************
 *   Copyright (C) 2006 by Jason Ansel                                     *
 *   jansel@ccs.neu.edu                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#include "signalmanager.h"
#include "constants.h"
#include <signal.h>
#include  "../jalib/jassert.h"

namespace
{
  //list of signals to be checkpointed
  int allSignals[] =
  {
    SIGHUP,// 1 Hangup (POSIX)
    SIGINT,// 2 Terminal interrupt (ANSI)
    SIGQUIT,//  3 Terminal quit (POSIX)
    SIGILL,// 4 Illegal instruction (ANSI)
    SIGTRAP,//  5 Trace trap (POSIX)
    SIGIOT,// 6 IOT Trap (4.2 BSD)
    SIGBUS,// 7 BUS error (4.2 BSD)
    SIGFPE,// 8 Floating point exception (ANSI)
    //SIGKILL,//  9 Kill(can't be caught or ignored) (POSIX)
    SIGUSR1,//  10  User defined signal 1 (POSIX)
    SIGSEGV,//  11  Invalid memory segment access (ANSI)
    SIGUSR2,//  12  User defined signal 2 (POSIX)
    SIGPIPE ,//13 Write on a pipe with no reader, Broken pipe (POSIX)
    SIGALRM,//  14  Alarm clock (POSIX)
    SIGTERM ,//15 Termination (ANSI)
    SIGSTKFLT,//  16  Stack fault
    SIGCHLD,//  17  Child process has stopped or exited, changed (POSIX)
    SIGCONT,//  18  Continue executing, if stopped (POSIX)
    //SIGSTOP,//  19  Stop executing(can't be caught or ignored) (POSIX)
    SIGTSTP,//  20  Terminal stop signal (POSIX)
    SIGTTIN,//  21  Background process trying to read, from TTY (POSIX)
    SIGTTOU,//  22  Background process trying to write, to TTY (POSIX)
    SIGURG,// 23  Urgent condition on socket (4.2 BSD)
    SIGXCPU,//  24  CPU limit exceeded (4.2 BSD)
    SIGXFSZ,//  25  File size limit exceeded (4.2 BSD)
    SIGVTALRM,//  26  Virtual alarm clock (4.2 BSD)
    SIGPROF,//  27  Profiling alarm clock (4.2 BSD)
    SIGWINCH,// 28  Window size change (4.3 BSD, Sun)
    SIGIO,//  29  I/O now possible (4.2 BSD)
    SIGPWR//  30  Power failure restart (System V)
  };


#define signalCount (sizeof(allSignals)/sizeof(int))

  //space to save all the signals to
  struct sigaction theActionTable[signalCount];


}

void dmtcp::SignalManager::saveSignals()
{
  JTRACE ( "saving signals..." ) ( signalCount );
  memset ( &theActionTable, 0, sizeof theActionTable );
  for ( size_t i = 0; i < signalCount; ++i )
  {
    int signum = allSignals[i];
    int rv = sigaction ( signum, NULL, theActionTable + i );
    JASSERT ( rv == 0 ) ( signum ) ( i ) ( JASSERT_ERRNO ).Text ( "sigaction() failed" );
  }
}

void dmtcp::SignalManager::restoreSignals()
{
  JTRACE ( "restoring signals..." );
  for ( size_t i = 0; i < signalCount; ++i )
  {
    int signum = allSignals[i];
    int rv = sigaction ( signum, theActionTable + i, NULL );
    JASSERT ( rv == 0 ) ( signum ) ( i ) ( JASSERT_ERRNO ).Text ( "sigaction() failed" );
  }
}
