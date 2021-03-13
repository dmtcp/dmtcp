#ifndef __DMTCP_PROCSELFMAPS_H__
#define __DMTCP_PROCSELFMAPS_H__

#include "jalloc.h"
#include "procmapsarea.h"

namespace dmtcp
{
class ProcSelfMaps
{
  public:
#ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
#endif // ifdef JALIB_ALLOCATOR

    ProcSelfMaps();
    ~ProcSelfMaps();

    size_t getNumAreas() const { return numAreas; }

    void getStackInfo(ProcMapsArea *area);

    int getNextArea(ProcMapsArea *area);

  private:
    unsigned long int readDec();
    unsigned long int readHex();
    bool isValidData();

    char *data;
    size_t dataIdx;
    size_t numAreas;
    size_t numBytes;
    int fd;
    int numAllocExpands;
};
}
#endif // #ifndef __DMTCP_PROCSELFMAPS_H__
