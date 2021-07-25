struct hostent_result {
  char *here;  // The address of this field within the child process
  struct hostent hostent;
  char padding[10000];
  struct hostent **result;
  int h_errno_value;
  char *end;
};

struct addrinfo_result {
  char *here;  // The address of this field within the child process
  char padding[10000];
  int h_errno_value;
  char *end;
};
