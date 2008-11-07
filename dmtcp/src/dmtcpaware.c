/*************************************************************************
 * This file, dmtcpaware.c, is placed in the public domain.              *
 * The motivation for this is to allow anybody to freely use this file   *
 * without restriction to statically link this file with any software.   *
 * This allows that software to communicate with the DMTCP libraries.    *
 * -  Jason Ansel, Kapil Arya, and Gene Cooperman                        *
 *      jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu        *
 *************************************************************************/

#include "dmtcpaware.h"

#include <stdio.h>
#include <string.h>

//
// This file contains stub functions an redirect to the real implementations
// in dmtcpapi.cpp when dmtcp is enabled.
//

#ifdef DEBUG
# define WARN_NO_DMTCP_MSG "%s: ERROR, program is not running under dmtcp_checkpoint.\n"
# define WARN_NO_DMTCP fprintf(stderr, WARN_NO_DMTCP_MSG,  __FUNCTION__)
#else
# define WARN_NO_DMTCP_MSG ""
# define WARN_NO_DMTCP
#endif

#define WEAK __attribute__ ((weak))

// we define the weak symbols to support static linking libdmtcpaware.a, see:
// http://www.kolpackov.net/pipermail/notes/2004-March/000006.html
// for a description of the trick

// There are 4 possible cases:
// static linked,  no dmtcp  -- this stub called, returns default
// dynamic linked, no dmtcp  -- this stub called, returns default
// static linked,  dmtcp     -- this stub called, dispatches DMTCP (through __dyn_XXX)
// dynamic linked, dmtcp     -- DMTCP called directly

extern int   __dyn_dmtcpIsEnabled() WEAK;
extern int   __dyn_dmtcpCheckpoint() WEAK;
extern int   __dyn_dmtcpRunCommand(char command) WEAK;
extern int   __dyn_dmtcpDelayCheckpointsLock() WEAK;
extern int   __dyn_dmtcpDelayCheckpointsUnlock() WEAK;
extern int   __dyn_dmtcpInstallHooks( DmtcpFunctionPointer preCheckpoint
                                    ,  DmtcpFunctionPointer postCheckpoint
                                    ,  DmtcpFunctionPointer postRestart) WEAK;
extern const DmtcpCoordinatorStatus* __dyn_dmtcpGetCoordinatorStatus() WEAK;
extern const DmtcpLocalStatus* __dyn_dmtcpGetLocalStatus() WEAK;

//all functions call __dyn##fn if it exists, otherwise return ret
#define DMTCPAWARE_STUB( fn, args,  ret)\
  if(__dyn_ ## fn ) return __dyn_ ## fn args; \
  WARN_NO_DMTCP; \
  return ret;

int dmtcpIsEnabled() {
  DMTCPAWARE_STUB( dmtcpIsEnabled, (), 0 );
}

int dmtcpCheckpoint(){
  DMTCPAWARE_STUB( dmtcpCheckpoint, (), -128 );
}

int dmtcpDelayCheckpointsLock(){
  DMTCPAWARE_STUB( dmtcpDelayCheckpointsLock, (), -128 );
}

int dmtcpDelayCheckpointsUnlock(){
  DMTCPAWARE_STUB( dmtcpDelayCheckpointsUnlock, (), -128 );
}

int dmtcpRunCommand(char command){
  DMTCPAWARE_STUB( dmtcpRunCommand, (command), -128 );
}

const DmtcpCoordinatorStatus* dmtcpGetCoordinatorStatus(){
  DMTCPAWARE_STUB( dmtcpGetCoordinatorStatus, (), NULL );
}

const DmtcpLocalStatus* dmtcpGetLocalStatus(){
  DMTCPAWARE_STUB( dmtcpGetLocalStatus, (), NULL );
}

int dmtcpInstallHooks( DmtcpFunctionPointer preCp
                     , DmtcpFunctionPointer postCp
                     , DmtcpFunctionPointer postRs){
  DMTCPAWARE_STUB( dmtcpInstallHooks, (preCp,postCp,postRs), -128 );
}

