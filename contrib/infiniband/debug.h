/*
  Debugging macroces file. Can be removed lately

*/
#ifndef IBV_LIB_DEBUG_H
#define IBV_LIB_DEBUG_H

#include <stdio.h>

//#define IBV_DEBUG

#ifdef IBV_DEBUG
#define PDEBUG(fmt, ...) \
    do { fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)
#else
#define PDEBUG(fmt, ...) \
    do { } while (0)
#endif

#endif
