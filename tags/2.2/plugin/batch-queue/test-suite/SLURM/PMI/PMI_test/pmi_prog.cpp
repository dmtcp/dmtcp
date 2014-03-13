#include <stdio.h>
#include <iostream>
#include "pmi.h"
#include <stdlib.h>

#define RUNWCK(x) \
{ \
  if( x != PMI_SUCCESS ) { \
    std::cout << "Error running " << #x << std::endl; \
    exit(0); \
  } \
}


int main()
{
    int rank, size, appnum;
    int ret;
    int spawned;
    char kvsname[256];
    
    sleep(1);
    
    RUNWCK( PMI_Init(&spawned) );
    RUNWCK( PMI_Get_size( &size ) );
    RUNWCK( PMI_Get_rank( &rank ) );
    RUNWCK( PMI_Get_appnum( &appnum ) );

    std::cout << "Job #" << appnum << " size=" << size << " rank =" << rank << std::endl;

    sleep(1000);
/*
    int i = 1;
    while( i ){
      sleep(1);
    }
*/
    RUNWCK ( PMI_Barrier() );

    RUNWCK( PMI_Get_size( &size ) );
    RUNWCK( PMI_Get_rank( &rank ) );
    RUNWCK( PMI_Get_appnum( &appnum ) );

    std::cout << "Job #" << appnum << " size=" << size << " rank =" << rank << std::endl;

    
    RUNWCK ( PMI_Finalize() );
    return 0;
}
