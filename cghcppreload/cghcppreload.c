#define _GNU_SOURCE 1
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <netdb.h>
#include <dlfcn.h>
#include "clilib.h"
#include "dnshdr.h"

#define MAX_RESOLVS (64*1024)

size_t cur_resolv = 0;
struct dst resolvs[MAX_RESOLVS];

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
  int (*orig_connect)(int, const struct sockaddr*, socklen_t) = dlsym(RTLD_NEXT, "connect");
  const struct sockaddr_in *sin;
  uint32_t idx;
  if (addr->sa_family != AF_INET)
  {
    return orig_connect(sockfd, addr, addrlen);
  }
  sin = (const struct sockaddr_in*)addr;
  idx = ntohl(sin->sin_addr.s_addr);
  if (idx >= MAX_RESOLVS)
  {
    return orig_connect(sockfd, addr, addrlen);
  }
  if (resolvs[idx].family != AF_INET ||
      resolvs[idx].path[0] == '\0')
  {
    return orig_connect(sockfd, addr, addrlen);
  }
  return connect_ex_dst(sockfd, &resolvs[idx], ntohs(sin->sin_port));
}

struct hostent *gethostbyname(const char *name)
{
  static struct hostent res;
  static char h_name[8192] = {0};
  static char *h_aliases[1] = {NULL};
  static char h_addr_list_zero[4] = {0};
  static char *h_addr_list[2] = {h_addr_list_zero, NULL};
  //struct hostent *(*orig_gethostbyname)(const char*) = dlsym(RTLD_NEXT, "gethostbyname");
  struct dst dst;
  if (get_dst(&dst, 0, (char*)name) != 0)
  {
    return NULL;
  }
  if (dst.path[0] == '\0')
  {
    snprintf(h_name, sizeof(h_name), "%s", name);
    res.h_name = h_name;
    res.h_addrtype = AF_INET;
    res.h_length = 4;
    res.h_aliases = h_aliases;
    res.h_addr_list = h_addr_list;
    hdr_set32n(h_addr_list_zero, dst.u.ip);
    return &res;
  }

  resolvs[cur_resolv] = dst;

  snprintf(h_name, sizeof(h_name), "%s", name);
  res.h_name = h_name;
  res.h_addrtype = AF_INET;
  res.h_length = 4;
  res.h_aliases = h_aliases;
  res.h_addr_list = h_addr_list;
  hdr_set32n(h_addr_list_zero, cur_resolv);

  cur_resolv++;
  cur_resolv %= MAX_RESOLVS;

  return &res;
}
