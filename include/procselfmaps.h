#ifndef __DMTCP_PROCSELFMAPS_H__
#define __DMTCP_PROCSELFMAPS_H__

#include "procmapsarea.h"
#include "jalloc.h"

namespace dmtcp {

class ProcSelfMaps
{
  public:
#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif

    ProcSelfMaps();
    ~ProcSelfMaps();

    size_t getNumAreas() const { return numAreas; }

    int getNextArea(ProcMapsArea* area);

  private:
    unsigned long int readDec();
    unsigned long int readHex();
    bool isValidData();

    char *data;
    size_t dataIdx;
    size_t numAreas;
    size_t numBytes;
    int fd;
};

}

#endif // #ifndef __DMTCP_PROCSELFMAPS_H__
