//+++2006-01-17
//    Copyright (C) 2006  Mike Rieker, Beverly, MA USA
//    EXPECT it to FAIL when someone's HeALTh or PROpeRTy is at RISk
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; version 2 of the License.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program; if not, write to the Free Software
//    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//---2006-01-17

/********************************************************************************************************************************/
/*                                                                                                                              */
/*  Simple single-threaded test program                                                                                         */
/*  Checkpoint is written to testmtcp.mtcp every 10 seconds                                                                     */
/*                                                                                                                              */
/*  Input lines of data                                                                                                         */
/*  As each line is entered, they are all echoed from first line thru latest line entered                                       */
/*  When checkpoint is restored, all the old lines should still echo and it accepts new ones                                    */
/*                                                                                                                              */
/********************************************************************************************************************************/

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "mtcp.h"

#define USE_BUFFERED_IO 1  // 0=use read/write calls for IO
                           // 1=use fgets/fputs calls for IO

#if USE_BUFFERED_IO
#define printline printf
#else
#define printline mtcp_printf
void mtcp_printf (char const *format, ...);
#endif

typedef struct Line Line;
struct Line { Line *next;
              char buff[1024];
            };

static int readline (char *buff, int size);

// #define USE_STATIC_MALLOC
#ifdef USE_STATIC_MALLOC
char mymemory[1000000];
int end = 0;
void * mymalloc(size_t x) {
  int *result = &(mymemory[end]);
  end += x;
  if (x > 1000000) { printf("malloc:  ERROR\n"); exit(1); }
  return result;
}
#endif

void dmtcpHookPreCheckpoint()
{
  printf("\ntestdmtcp.c: dmtcpHookPreCheckpoint: about to checkpoint\n");
}
void dmtcpHookPostCheckpoint()
{
  printf("\ntestdmtcp.c: dmtcpHookPostCheckpoint: done checkpointing\n");
}
void dmtcpHookRestart()
{
  printf("\ntestdmtcp.c: dmtcpHookRestart: restarting\n");
}  

int main ()

{
  int number;
  Line *line, **lline, *lines;

  mtcp_init ("testmtcp.mtcp", 10, 0);
  mtcp_ok ();

  lines = NULL;
  lline = &lines;
  number = 0;
  while (1) {
    printline ("%6d> ", number + 1);
    //printline ("testmtcp.c: ABOUT TO malloc\n");fflush(stdout);
#ifdef USE_STATIC_MALLOC
    line = mymalloc (sizeof *line);
#else
    line = malloc (sizeof *line);
#endif
    //printline ("testmtcp.c: DID malloc\n");fflush(stdout);
    if (!readline (line -> buff, sizeof line -> buff)) break;
    *lline = line;
    line -> next = NULL;
    lline = &(line -> next);
    printline ("\n");
    number = 0;
    for (line = lines; line != NULL; line = line -> next) {
      printline ("%6d: %s", ++ number, line -> buff);
    }
  }
  //mtcp_no ();
  printline ("All done!\n");
  exit (0);
  return (0);
}


static int readline (char *buff, int size)

{
#if USE_BUFFERED_IO
  return (fgets (buff, size, stdin) != NULL);
#else
  int of, rc;

  for (of = 0; of < size - 1;) {
    rc = read (0, buff + of, 1);
    if (rc < 0) {
      printline ("error %d reading stdin: %s\n", errno, strerror (errno));
    }
    if (rc <= 0) return (0);
    if (buff[of++] == '\n') break;
  }
  buff[of] = 0;
  return (1);
#endif
}
