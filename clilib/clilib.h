#ifndef _CLILIB_H_
#define _CLILIB_H_

#include <stdint.h>

struct dst {
  int family;
  union {
    uint32_t ip;
    unsigned char ipv6[16];
  } u;
  char path[8192];
};

int get_dst(struct dst *dst, int try_ipv6, char *name);

int connect_ex_dst(int sockfd, struct dst *dst, uint16_t port);

int socket_ex_ipv4(char *name, uint16_t port);

int socket_ex(char *name, uint16_t port);

int connect_ex(int sockfd, char *name, uint16_t port);

#endif
