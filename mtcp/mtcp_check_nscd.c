//    Copyright (C) 2007  Gene Cooperman
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
//---2007-3-3


/* To test:  env STANDALONE=1 gcc THIS_FILE; ./a.out */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void mtcp_check_nscd(void) {
  int fd = open("/var/run/nscd", O_RDONLY);

  if (fd == -1)
    return;  /* Good news.  If it doesn't exist, it can't be enabled.  :-) */
  else if (-1 == close(fd))
    { perror("close"); exit(1); }

  printf("\n\n\nWARNING:  /var/run/nscd exists\n"
  "  MTCP currently might not correctly restart when nscd is running.\n"
  "  Please test checkpoint/restart on your machine to see if it works.\n"
  "  If necessary, turn off nscd.  For example:  /etc/init.d/nscd --stop\n"
  "  We will fix compatibility with nscd in a future version.\n\n\n\n");
}

#ifdef STANDALONE
int main() {
  mtcp_check_nscd();
  return 0;
}
#endif
