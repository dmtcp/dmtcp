/*
  Debugging macroces file. Can be removed lately

*/
#ifndef IBV_LIB_DEBUG_H
#define IBV_LIB_DEBUG_H

#include <stdio.h>

//
//#define PDEBUG(fmt,args...) fprintf(stderr,__FILE__ "(%d): " fmt " \n", __LINE__, ## args  )
//#define PDEBUG(fmt, args...) write(825, fmt, strlen(fmt))
#define PDEBUG(fmt, args...) printf(fmt, ##args)
#endif
