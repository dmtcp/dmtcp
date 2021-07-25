#ifndef _PROCMAPSUTILS_H
#define _PROCMAPSUTILS_H

#include "procmapsarea.h"

#ifndef EXTERNC
# ifdef __cplusplus
#  define EXTERNC extern "C"
# else // ifdef __cplusplus
#  define EXTERNC
# endif // ifdef __cplusplus
#endif // ifndef EXTERNC

EXTERNC int readMapsLine(int , Area* );

#endif // #ifndef _PROCMAPSUTILS_H
