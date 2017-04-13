/*
  Debugging macroces file.
*/
#ifndef IBV_LIB_DEBUG_H
#define IBV_LIB_DEBUG_H

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

//#define IBV_ENABLE_DEBUG

#ifdef IBV_ENABLE_DEBUG
#define IBV_DEBUG(args...) \
  do { \
    fprintf(stderr, "[%d] %s:%d %s:\n", getpid(), \
            __FILE__, __LINE__, __FUNCTION__); \
    fprintf(stderr, args); \
  } while (0)
#else
#define IBV_DEBUG(args...) \
    do { } while (0)
#endif

#define IBV_WARNING(args...) \
  do { \
    fprintf(stderr, "[%d] %s:%d %s:\n", getpid(), \
            __FILE__, __LINE__, __FUNCTION__); \
    fprintf(stderr, args); \
  } while (0)

#define IBV_ERROR(args...) \
  do { \
    IBV_WARNING(args); \
    exit(1); \
  } while (0)

#endif
