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

void mtcp_check_vdso_enabled(void) {
  char buf[1];
  int fd = open("/proc/sys/vm/vdso_enabled", O_RDONLY);

  if (fd == -1)
    return;  /* Good news.  If it doesn't exist, it can't be enabled.  :-) */
  else if (-1 == close(fd))
    { perror("close"); exit(1); }

  FILE * stream = popen("cat /proc/sys/vm/vdso_enabled", "r");
  if (stream == NULL) {
    perror("popen");
    exit(1);
  }
  clearerr(stream);
  if (fread(buf, sizeof(buf[0]), 1, stream) < 1) {
    if (ferror(stream)) {
      perror("fread");
      exit(1);
    }
  }
  if (-1 == pclose(stream)) {
    perror("pclose");
    exit(1);
  }
  if (buf[0] == '1') {
    printf("\n\n\nPROBLEM:  cat /proc/sys/vm/vdso_enabled returns 1\n"
    "  On 32-bit architectures, checkpointing doesn't work with vdso_enabled.\n"
    "  Please run this program again after doing as root:\n"
    "                                    echo 0 > /proc/sys/vm/vdso_enabled\n");
    exit(1);
  }
}

#ifdef STANDALONE
int main() {
  mtcp_check_vdso_enabled();
  return 0;
}
#endif
