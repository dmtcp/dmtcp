int str_starts_with(const char *symbol, const char *prefix);
int str_ends_with(const char *symbol, const char *suffix);
int str_index(const char *string, char c);
int readall(int fd, char *addr, size_t size);
int writeall(int fd, char *addr, size_t size);
void read_metadata(char *file, char *addr, size_t size, off_t offset);
void write_metadata(char *file, char *addr, size_t size, off_t offset);
