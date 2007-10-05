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
#include "mtcpinterface.h"
#include "jassert.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "constants.h"
#include "sockettable.h"
#include <unistd.h>
#include "uniquepid.h"
#include "dmtcpworker.h"
#include "jfilesystem.h"
#include "jconvert.h"
namespace
{
static const char* REOPEN_MTCP = (char*) 0x1;
    
static void* find_and_open_mtcp_so()
{
    std::string mtcpso = jalib::Filesystem::FindHelperUtility( "mtcp.so" );
    void* handle = dlopen(mtcpso.c_str(), RTLD_NOW);
    JASSERT(handle != NULL)(mtcpso).Text("failed to load mtcp.so");
    return handle;
}

}

extern "C" void* _get_mtcp_symbol(const char* name)
{
    static void* theMtcpHandle = find_and_open_mtcp_so();
    
    if(name == REOPEN_MTCP)
    {
       JTRACE("reopening mtcp.so")(theMtcpHandle);
       while(dlclose(theMtcpHandle) == 0){ JTRACE("closing..."); }
       theMtcpHandle = find_and_open_mtcp_so();
       JTRACE("reopening mtcp.so DONE")(theMtcpHandle);
       return 0; 
    }
    
    void* tmp = dlsym(theMtcpHandle, name);
    JASSERT(tmp != NULL)(name).Text("failed to find mtcp.so symbol");
    
    //JTRACE("looking up mtcp.so symbol")(name);
    
    return tmp;
}

extern "C" {
    typedef int (*t_mtcp_init) (char const *checkpointfilename, int interval, int clonenabledefault);
    typedef void (*t_mtcp_set_callbacks)(void (*sleep_between_ckpt)(int sec),
                        void (*pre_ckpt)(),
                        void (*post_ckpt)(int is_restarting),
                        int  (*ckpt_fd)(int fd));
    typedef int (*t_mtcp_ok) (void);
}

static void callbackSleepBetweenCheckpoint(int sec)
{
    dmtcp::DmtcpWorker::instance().waitForStage1Suspend();
}

static void callbackPreCheckpoint()
{
    dmtcp::DmtcpWorker::instance().waitForStage2Checkpoint();;
}


static void callbackPostCheckpoint(int isRestart)
{
    if(isRestart)
    {
        JASSERT_SET_LOGFILE("/tmp/jassertlog." + jalib::XToString(getpid()));
        dmtcp::DmtcpWorker::instance().postRestart();
    }
    else
    {
        JNOTE("checkpointed")(dmtcp::UniquePid::checkpointFilename()); 
    }
    dmtcp::DmtcpWorker::instance().waitForStage3Resume();
}

static int callbackShouldCkptFD(int /*fd*/)
{
    //mtcp should never checkpoint files... dmtcp will handle it
    return 0;
}


void dmtcp::initializeMtcpEngine()
{
    
    t_mtcp_set_callbacks setCallbks = (t_mtcp_set_callbacks) _get_mtcp_symbol("mtcp_set_callbacks");
    
    
    t_mtcp_init init = (t_mtcp_init) _get_mtcp_symbol("mtcp_init");
    t_mtcp_ok okFn = (t_mtcp_ok) _get_mtcp_symbol("mtcp_ok");
    
    
    (*setCallbks)(callbackSleepBetweenCheckpoint,callbackPreCheckpoint,callbackPostCheckpoint,callbackShouldCkptFD);
    (*init)(UniquePid::checkpointFilename(),0xBadF00d,1);
    (*okFn)();
    
    
    JTRACE("mtcp_init complete")(UniquePid::checkpointFilename());
}




//need to forward user clone
extern "C" int __clone (int (*fn) (void *arg), void *child_stack, int flags, void *arg, int *parent_tidptr, struct user_desc *newtls, int *child_tidptr)
{
    typedef int (*cloneptr)(int (*)(void*), void*, int, void*, int*, user_desc*, int*);
    static cloneptr realclone = (cloneptr) _get_mtcp_symbol("__clone");
    
    JTRACE("forwarding user's clone call to mtcp");
    
    return (*realclone)(fn,child_stack,flags,arg,parent_tidptr,newtls,child_tidptr);
}

void dmtcp::shutdownMtcpEngineOnFork()
{
    _get_mtcp_symbol(REOPEN_MTCP);
}

