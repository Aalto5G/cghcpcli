#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/poll.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
#include "clilib.h"

/*
 * Usage: specify in .ssh/config:
 *
 * Host ssh.example.com
 *   HostName ssh.example.com
 *   ProxyCommand /directory/to/cghcpproxycmd 10.150.2.100 8080 %h %p
 *
 * Or if DNS is properly configured, specify:
 *
 * Host ssh.example.com
 *   HostName ssh.example.com
 *   ProxyCommand /directory/to/cghcpproxycmd %h %p
 */

// RFE move these into a common library
static void set_nonblock(int fd)
{
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void usage(const char *argv0)
{
  fprintf(stderr, "Usage: %s foo1.lan 8080 foo2.lan 80\n", argv0);
  fprintf(stderr, "Or:    %s foo2.lan 80\n", argv0);
  exit(1);
}

int main(int argc, char **argv)
{
  int sockfd;
  int port;
  struct hostent *he;
  struct sockaddr_in sin;
  char outbuf[8192];
  char inbuf[8192];
  size_t outbufcnt = 0;
  size_t inbufcnt = 0;

  if (argc != 3 && argc != 5)
  {
    usage(argv[0]);
    return 1;
  }
  if (argc == 3)
  {
    port = atoi(argv[2]);
    if (((int)(uint16_t)port) != port || port == 0)
    {
      usage(argv[0]);
    }
    sockfd = socket_ex(argv[1], port);
    if (sockfd < 0)
    {
      perror("Err");
      return 1;
    }
  }
  else
  {
    port = atoi(argv[2]);
    if (((int)(uint16_t)port) != port || port == 0)
    {
      usage(argv[0]);
    }
    he = gethostbyname(argv[1]);
    if (he == NULL)
    {
      errno = ENXIO;
      perror("Host not found");
      return 1;
    }
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    if (he->h_length < 0)
    {
      abort();
    }
    memcpy(&sin.sin_addr, he->h_addr_list[0], (size_t)he->h_length);

    port = atoi(argv[4]);
    if (((int)(uint16_t)port) != port)
    {
      usage(argv[0]);
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
      perror("Err");
      return 1;
    }
    if (connect(sockfd, (struct sockaddr*)&sin, sizeof(sin)) < 0)
    {
      perror("Err");
      return 1;
    }
    if (write_http_connect_port(sockfd, argv[3], port) != 0)
    {
      perror("Proxy connection failed");
      return 1;
    }
    if (read_http_ok(sockfd) != 0)
    {
      perror("Proxy connection failed");
      return 1;
    }
  }

  set_nonblock(0);
  set_nonblock(1);
  set_nonblock(sockfd);

  for (;;)
  {
    fd_set readfds;
    fd_set writefds;
    ssize_t ret;

    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    if (outbufcnt < sizeof(outbuf))
    {
      FD_SET(0, &readfds);
    }
    if (inbufcnt < sizeof(inbuf))
    {
      FD_SET(sockfd, &readfds);
    }
    if (outbufcnt > 0)
    {
      FD_SET(sockfd, &writefds);
    }
    if (inbufcnt > 0)
    {
      FD_SET(1, &writefds);
    }
    select(sockfd + 1, &readfds, &writefds, NULL, NULL);
    if (FD_ISSET(0, &readfds))
    {
      ret = read(0, outbuf+outbufcnt, sizeof(outbuf)-outbufcnt);
      if (ret < 0 && (errno != EWOULDBLOCK && errno != EAGAIN))
      {
        perror("Err");
        exit(1);
      }
      if (ret == 0)
      {
        exit(0);
      }
      if (ret > 0)
      {
        outbufcnt += (size_t)ret;
      }
    }
    if (FD_ISSET(sockfd, &readfds))
    {
      ret = read(sockfd, inbuf+inbufcnt, sizeof(inbuf)-inbufcnt);
      if (ret < 0 && (errno != EWOULDBLOCK && errno != EAGAIN))
      {
        perror("Err");
        exit(1);
      }
      if (ret == 0)
      {
        exit(0);
      }
      if (ret > 0)
      {
        inbufcnt += (size_t)ret;
      }
    }
    if (FD_ISSET(1, &writefds))
    {
      ret = write(1, inbuf, inbufcnt);
      if (ret < 0 && (errno != EWOULDBLOCK && errno != EAGAIN))
      {
        perror("Err");
        exit(1);
      }
      if (ret > 0)
      {
        memmove(inbuf, inbuf+ret, inbufcnt-(size_t)ret);
        inbufcnt -= (size_t)ret;
      }
    }
    if (FD_ISSET(sockfd, &writefds))
    {
      ret = write(sockfd, outbuf, outbufcnt);
      if (ret < 0 && (errno != EWOULDBLOCK && errno != EAGAIN))
      {
        perror("Err");
        exit(1);
      }
      if (ret > 0)
      {
        memmove(outbuf, outbuf+ret, outbufcnt-(size_t)ret);
        outbufcnt -= (size_t)ret;
      }
    }
  }

  return 0;
}
