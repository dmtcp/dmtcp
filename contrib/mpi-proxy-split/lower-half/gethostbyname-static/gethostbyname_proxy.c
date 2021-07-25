/* Copyright 2021 Gene Cooperman (gene@ccs.neu.edu)
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <assert.h>
#include "gethostbyname_static.h"
#include "gethostbyname_utils.ic"

extern int h_errno;

struct hostent_result hostent_result;
struct addrinfo_result addrinfo_result;

// Copy information of 'list' to contiguous buffer starting at '*dest_p'
// char **list is similar to 'char argv[]' in main()
// dest_p is INOUT param.
// IN: *dest_p is the destination starting address.
// OUT: Set *dest_p to the next address after the final address when done.
// h_length is length of each item in list, or if it's 0, then item is a string.
void copy_pointer_list(char **dest_p, char **list, int h_length) {
  char *dest = *dest_p;
  char **list_start = list;
  char **dest_ptr = (char **)dest;
  while (*list != NULL) {
    *(char **)dest = *list;
    dest += sizeof(*list);
    list++;
  }
  *(char **)dest = NULL;
  dest += sizeof(*list);
  list = list_start;
  if (h_length > 0) { // Each item is an h_addr of length h_length
    while (*list != NULL) {
      memcpy(dest, *list, h_length);
      *dest_ptr = dest;
      dest_ptr++;
      dest += h_length;
      list++;
    }
  } else if (h_length == 0) { // else it's a string
    while (*list != NULL) {
      int len = strlen(*list);
      // FIXME:  Where is 'end' for struct hostent_result?
      char *end = dest + 10000;  // FIXME
      if (dest + len < end) {
        strcpy(dest, *list);
        *dest_ptr = dest;
        dest_ptr++;
        dest += (len + 1);
        list++;
      } else {
        errno = ERANGE;
        exit(ERANGE);
      }
    }
  }
  *dest_p = dest; 
}

#define ROUND_UP(x) (char *)(((unsigned long)(x)+3)>>2<<2)
int do_gethostbyname(const char *name) {
  struct hostent *result = gethostbyname(name);
  if (result == NULL) {
    perror("gethostbyname");
    hostent_result.result = NULL;
    hostent_result.h_errno_value = h_errno;
  } else {
    hostent_result.here = (char*)&hostent_result.here; // record addr in child
    // h_addrtype, h_length: were filled in
    memcpy(&hostent_result.hostent, result, sizeof(struct hostent));
    // h_name
    char *tmp = hostent_result.padding;
    strncpy(tmp, hostent_result.hostent.h_name, sizeof(hostent_result.padding));
    hostent_result.hostent.h_name = tmp;
    tmp += strlen(hostent_result.hostent.h_name) + 1;
    tmp = ROUND_UP(tmp);
    // h_aliases
    hostent_result.hostent.h_aliases = (char**)tmp;
    copy_pointer_list(&tmp, result->h_aliases, 0);
    // h_addr_list
    hostent_result.hostent.h_addr_list = (char**)tmp;
    copy_pointer_list(&tmp, result->h_addr_list, result->h_length);
    hostent_result.result = (void *)1; // Anything but NULL.  NULL is error.

    writeall(1, &hostent_result, sizeof(hostent_result));
  }
  return 0;
}

int do_getaddrinfo(const char *node, const char *service,
                   const struct addrinfo *hints) {
  struct addrinfo *res;
  int rc = getaddrinfo(node, service, hints, &res);
  if (rc != 0) {
    return rc;
  }
  // Else success
  addrinfo_result.here = (char*)&addrinfo_result.here; // record addr in child
  char *tmp = addrinfo_result.padding;
  while (res != NULL) {
    memcpy(tmp, res, sizeof(*res));
    struct sockaddr *ai_addr = (void *)(tmp + sizeof(*res));
    char *ai_canonname = (char *)ai_addr + res->ai_addrlen;
    char *end = (char *)ai_canonname +
                (res->ai_canonname == NULL ? 0 : strlen(ai_canonname) + 1);
    assert(end - addrinfo_result.padding < sizeof(addrinfo_result.padding));
    memcpy(ai_addr, res->ai_addr, res->ai_addrlen);
    if (res->ai_canonname != NULL) {
      strcpy(ai_canonname, res->ai_canonname);
      ((struct addrinfo *)tmp)->ai_canonname = ai_canonname;
    }
    ((struct addrinfo *)tmp)->ai_addr = ai_addr;
    ((struct addrinfo *)tmp)->ai_next =
      (res->ai_next == NULL ? NULL : (struct addrinfo *)end);
    tmp = end;
    res = res->ai_next;
  }
  // int tmp2 = tmp - addrinfo_result.padding;
  // writeall(1, &tmp2, sizeof(tmp2));
  writeall(1, &addrinfo_result, sizeof(addrinfo_result));
  return rc;
}

int main(int argc, char **argv) {
  if (strcmp(argv[0], "gethostbyname_r") == 0 ||
      strcmp(argv[0], "gethostbyname") == 0 ) {
    return do_gethostbyname(argv[1]);
  } else if (strcmp(argv[0], "getaddrinfo") == 0) {
    struct addrinfo buf;
    struct addrinfo *hints = &buf;
    struct sockaddr ai_addr;
    char *ai_canonname;
    int rc = readall(0, hints, sizeof(*hints));
    assert(rc == sizeof(*hints));
    if (*(int *)hints != -1) {
      rc = readall(0, &ai_addr, sizeof(ai_addr));
      assert(rc == sizeof(ai_addr));
      hints->ai_addr = &ai_addr;
      hints->ai_canonname = NULL;
    } else {
      hints = NULL;
    }
    return do_getaddrinfo(argv[1], argv[2], hints); // res is output parameter
  }
}
