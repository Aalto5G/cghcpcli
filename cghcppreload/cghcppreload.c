#define _GNU_SOURCE 1
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <netdb.h>
#include <dlfcn.h>
#include "clilib.h"
#include "hdr.h"

#define MAX_RESOLVS (8*1024)

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

void freeaddrinfo(struct addrinfo *res)
{
  if (res->ai_next != NULL)
  {
    abort();
  }
  free(res->ai_addr);
  free(res);
}

int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res)
{
  int (*orig_getaddrinfo)(const char*, const char*, const struct addrinfo*, struct addrinfo**) = dlsym(RTLD_NEXT, "getaddrinfo");
  int (*orig_freeaddrinfo)(struct addrinfo*) = dlsym(RTLD_NEXT, "freeaddrinfo");
  struct dst dst;
  struct addrinfo orighints = {.ai_family = AF_INET};
  struct addrinfo *origres = NULL;
  uint16_t port;
  orig_getaddrinfo("localhost", service, &orighints, &origres);
  port = ntohs(((struct sockaddr_in*)origres->ai_addr)->sin_port);
  orig_freeaddrinfo(origres);
  if (hints != NULL && hints->ai_family == AF_INET6)
  {
    struct sockaddr_in6 *sin6;
    if (get_dst(&dst, 1, (char*)node) != 0)
    {
      return EAI_ADDRFAMILY;
    }
    if (dst.family != AF_INET6)
    {
      return EAI_ADDRFAMILY;
    }
    *res = malloc(sizeof(**res));
    memset(*res, 0, sizeof(**res));
    (*res)->ai_socktype = hints->ai_socktype;
    (*res)->ai_protocol = hints->ai_protocol;
    (*res)->ai_family = AF_INET6;
    (*res)->ai_addrlen = sizeof(struct sockaddr_in6);
    sin6 = malloc(sizeof(struct sockaddr_in6));
    (*res)->ai_addr = (struct sockaddr*)sin6;
    memset(sin6, 0, sizeof(struct sockaddr_in6));
    sin6->sin6_family = AF_INET6;
    sin6->sin6_port = htons(port);
    memcpy(sin6->sin6_addr.s6_addr, dst.u.ipv6, 16);
    return 0;
  }
  if (hints != NULL && hints->ai_family == AF_INET)
  {
    struct sockaddr_in *sin;
    if (get_dst(&dst, 0, (char*)node) != 0)
    {
      return EAI_ADDRFAMILY;
    }
    if (dst.family != AF_INET)
    {
      return EAI_ADDRFAMILY;
    }
    *res = malloc(sizeof(**res));
    memset(*res, 0, sizeof(**res));
    (*res)->ai_socktype = hints->ai_socktype;
    (*res)->ai_protocol = hints->ai_protocol;
    (*res)->ai_family = AF_INET;
    (*res)->ai_addrlen = sizeof(struct sockaddr_in);
    sin = malloc(sizeof(struct sockaddr_in));
    (*res)->ai_addr = (struct sockaddr*)sin;
    memset(sin, 0, sizeof(struct sockaddr_in));
    sin->sin_family = AF_INET;
    sin->sin_port = htons(port);
    if (dst.path[0] != '\0')
    {
      resolvs[cur_resolv] = dst;
      sin->sin_addr.s_addr = htonl(cur_resolv);
      cur_resolv++;
      cur_resolv %= MAX_RESOLVS;
    }
    else
    {
      sin->sin_addr.s_addr = htonl(dst.u.ip);
    }
    return 0;
  }
  if (get_dst(&dst, 0, (char*)node) != 0)
  {
    return EAI_ADDRFAMILY;
  }
  if (dst.family == AF_INET)
  {
    struct sockaddr_in *sin;
    *res = malloc(sizeof(**res));
    memset(*res, 0, sizeof(**res));
    if (hints)
    {
      (*res)->ai_socktype = hints->ai_socktype;
      (*res)->ai_protocol = hints->ai_protocol;
    }
    (*res)->ai_family = AF_INET;
    (*res)->ai_addrlen = sizeof(struct sockaddr_in);
    sin = malloc(sizeof(struct sockaddr_in));
    (*res)->ai_addr = (struct sockaddr*)sin;
    memset(sin, 0, sizeof(struct sockaddr_in));
    sin->sin_family = AF_INET;
    sin->sin_port = htons(port);
    if (dst.path[0] != '\0')
    {
      resolvs[cur_resolv] = dst;
      sin->sin_addr.s_addr = htonl(cur_resolv);
      cur_resolv++;
      cur_resolv %= MAX_RESOLVS;
    }
    else
    {
      sin->sin_addr.s_addr = htonl(dst.u.ip);
    }
    return 0;
  }
  else if (dst.family == AF_INET6)
  {
    struct sockaddr_in6 *sin6;
    *res = malloc(sizeof(**res));
    if (hints)
    {
      (*res)->ai_socktype = hints->ai_socktype;
      (*res)->ai_protocol = hints->ai_protocol;
    }
    memset(*res, 0, sizeof(**res));
    (*res)->ai_family = AF_INET6;
    (*res)->ai_addrlen = sizeof(struct sockaddr_in6);
    sin6 = malloc(sizeof(struct sockaddr_in6));
    (*res)->ai_addr = (struct sockaddr*)sin6;
    memset(sin6, 0, sizeof(struct sockaddr_in6));
    sin6->sin6_family = AF_INET6;
    sin6->sin6_port = htons(port);
    memcpy(sin6->sin6_addr.s6_addr, dst.u.ipv6, 16);
    return 0;
  }
  else
  {
    abort();
  }
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
