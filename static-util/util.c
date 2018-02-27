#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int str_starts_with(const char *symbol, const char *prefix) {
  for (;; symbol++, prefix++) {
    if (*prefix == '\0') return 1;
    if (*symbol != *prefix) return 0;
  }
}

int str_ends_with(const char *symbol, const char *suffix) {
  const char *symbol_char = symbol + strlen(symbol) - 1;
  const char *suffix_char = suffix + strlen(suffix) - 1;
  for ( ; symbol_char >= symbol || suffix_char >= suffix;
       symbol_char--, suffix_char--) {
    if (suffix_char == suffix) return 1;
    if (*symbol_char != *suffix_char) return 0;
  }
  return 0;
}

int str_index(const char *string, char c) {
  int idx;
  for ( idx = 0; *string != '\0'; string++, idx++ ) {
    if ( *string == c ) return idx;
  }
  return -1;
}



int readall(int fd, char *addr, size_t size) {
  int rc;
  while (1) {
    rc = read(fd, addr, size);
    if (rc == size) return rc;
    if (rc == -1) { perror("read"); exit(1); }
    if (rc == 0) { printf("readall: end of file\n"); exit(1); }
    assert( rc > 0 && rc < size );
    size -= rc;
    addr += rc;
  }
}
int writeall(int fd, char *addr, size_t size) {
  int rc;
  while (1) {
    rc = write(fd, addr, size);
    if (rc == size) return rc;
    if (rc == -1) { perror("read"); exit(1); }
    if (rc == 0) { printf("readall: end of file\n"); exit(1); }
    assert( rc > 0 && rc < size );
    size -= rc;
    addr += rc;
  }
}

void read_metadata(char *file, char *addr, size_t size, off_t offset) {
  int fd = open(file, O_RDONLY);
  off_t rc = lseek(fd, offset, SEEK_SET);
  if (rc == (off_t)(-1)) {
    perror("lseek");
    exit(1);
  }
  readall(fd, addr, size);
  close(fd);
}
void write_metadata(char *file, char *addr, size_t size, off_t offset) {
  int fd = open(file, O_WRONLY);
  if (fd == -1) {perror("open"); exit(1);};
  off_t rc = lseek(fd, offset, SEEK_SET);
  if (rc == (off_t)(-1)) {
    perror("lseek");
    exit(1);
  }
  writeall(fd, addr, size);
  close(fd);
}
