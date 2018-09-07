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

/*
 * Usage: specify in .ssh/config:
 *
 * Host ssh.example.com
 *   HostName ssh.example.com
 *   ProxyCommand /directory/to/cghcpproxycmd 10.150.2.100 8080 %h %p
 */

// RFE move these into a common library
static size_t bytes_iovs(struct iovec *iovs, size_t sz)
{
  size_t total = 0;
  size_t i;
  for (i = 0; i < sz; i++)
  {
    total += iovs[i].iov_len;
  }
  return total;
}

static void set_nonblock(int fd)
{
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static size_t reduce_iovs(struct iovec *iovs, size_t sz, size_t reduction)
{
  size_t i;
  struct iovec *iov;
  for (i = 0; i < sz; i++)
  {
    iov = &iovs[i];
    if (iov->iov_len > reduction)
    {
      iov->iov_len -= reduction;
      iov->iov_base = ((char*)iov->iov_base) + reduction;
      return i;
    }
    else if (iov->iov_len == reduction)
    {
      iov->iov_len -= reduction;
      iov->iov_base = ((char*)iov->iov_base) + reduction;
      return i+1;
    }
    else
    {
      reduction -= iov->iov_len;
      iov->iov_base = ((char*)iov->iov_base) + iov->iov_len;
      iov->iov_len = 0;
    }
  }
  return i;
}

static ssize_t writev_all(int sockfd, struct iovec *iovs, size_t sz)
{
  size_t bytes_written = 0;
  ssize_t ret;
  size_t reduceret = 0;
  struct pollfd pfd;
  if (sz == 0)
  {
    return 0;
  }
  for (;;)
  {
    ret = writev(sockfd, iovs + reduceret, sz - reduceret);
    if (ret > 0)
    {
      bytes_written += ret;
      reduceret = reduce_iovs(iovs, sz, ret);
      if (reduceret == sz)
      {
        return bytes_written;
      }
    }
    else if (ret <= 0)
    {
      if (ret == 0)
      {
        errno = EPIPE; // Let's give some errno
      }
      if (ret < 0 && errno == EINTR)
      {
        continue;
      }
      else if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
      {
        pfd.fd = sockfd;
        pfd.events = POLLOUT;
        poll(&pfd, 1, -1);
        continue;
      }
      else if (bytes_written > 0)
      {
        return bytes_written;
      }
      else
      {
        return -1;
      }
    }
  }
}

const char conbegin[] = "CONNECT ";
const char colon[] = ":";
const char interim[] = " HTTP/1.1\r\nHost: ";
const char crlfcrlf[] = "\r\n\r\n";
const char httpslash[] = "HTTP/";
const char twohundred[] = "200";

int main(int argc, char **argv)
{
  int sockfd;
  int port;
  struct hostent *he;
  struct sockaddr_in sin;
  char portint[16] = {0};
  ssize_t bytes_expected;
  ssize_t bytes_written;
  struct iovec iovs_src[] = {
    {.iov_base = (char*)conbegin, .iov_len = sizeof(conbegin)-1},
    {.iov_base = NULL, .iov_len = 0}, // [1]
    {.iov_base = (char*)colon, .iov_len = sizeof(colon)-1},
    {.iov_base = portint, .iov_len = 0}, // [3]
    {.iov_base = (char*)interim, .iov_len = sizeof(interim)-1},
    {.iov_base = NULL, .iov_len = 0}, // [5]
    {.iov_base = (char*)colon, .iov_len = sizeof(colon)-1},
    {.iov_base = portint, .iov_len = 0}, // [7]
    {.iov_base = (char*)crlfcrlf, .iov_len = sizeof(crlfcrlf)-1},
  };
  size_t httpslashcnt = 0;
  size_t majcnt = 0;
  size_t dot_seen = 0;
  size_t spseen = 0;
  size_t sp2seen = 0;
  size_t mincnt = 0;
  size_t crlfcrlfcnt = 0;
  size_t twohundredcnt = 0;
  ssize_t read_ret;
  char outbuf[8192];
  char inbuf[8192];
  size_t outbufcnt = 0;
  size_t inbufcnt = 0;

  if (argc != 5)
  {
    printf("Usage: %s foo1.lan 8080 foo2.lan 80\n", argv[0]);
    return 1;
  }
  port = atoi(argv[2]);
  if (((int)(uint16_t)port) != port)
  {
    printf("Usage: %s foo1.lan 8080 foo2.lan 80\n", argv[0]);
    return 1;
  }
  he = gethostbyname(argv[1]);
  if (he == NULL)
  {
    perror("Host not found");
    return 1;
  }
  sin.sin_family = AF_INET;
  sin.sin_port = htons(port);
  memcpy(&sin.sin_addr, he->h_addr_list[0], he->h_length);

  port = atoi(argv[4]);
  if (((int)(uint16_t)port) != port)
  {
    printf("Usage: %s foo1.lan 8080 foo2.lan 80\n", argv[0]);
    return 1;
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
  printf("Connection successful\n");
  snprintf(portint, sizeof(portint), "%d", port);
  iovs_src[1].iov_base = argv[3];
  iovs_src[1].iov_len = strlen(argv[3]);
  iovs_src[3].iov_len = strlen(portint);
  iovs_src[5].iov_base = argv[3];
  iovs_src[5].iov_len = strlen(argv[3]);
  iovs_src[7].iov_len = strlen(portint);
  bytes_expected = bytes_iovs(iovs_src, 9);
  bytes_written = writev_all(sockfd, iovs_src, 9);
  if (bytes_written != bytes_expected)
  {
    perror("Err");
    return 1;
  }

  // RFE move this into a common function
  for (;;)
  {
    char ch;
    struct pollfd pfd;
    read_ret = read(sockfd, &ch, 1);
    if (read_ret < 0 && (errno == EWOULDBLOCK || errno == EAGAIN))
    {
      pfd.fd = sockfd;
      pfd.events = POLLIN;
      poll(&pfd, 1, -1);
      continue;
    }
    if (read_ret < 0)
    {
      perror("Read returned -1");
      return 1;
    }
    if (read_ret == 0)
    {
      errno = EBADMSG;
      perror("Read returned 0");
      return 1;
    }
    if (read_ret > 1)
    {
      abort();
    }
    if (httpslashcnt < 5)
    {
      if (httpslash[httpslashcnt] == ch)
      {
        httpslashcnt++;
      }
      else
      {
        errno = EBADMSG;
        perror("Bad message");
        return 1;
      }
    }
    else if (!spseen)
    {
      if (isdigit(ch))
      {
        if (dot_seen)
        {
          mincnt++;
        }
        else
        {
          majcnt++;
        }
        continue;
      }
      if (ch == '.' && !dot_seen)
      {
        dot_seen = 1;
      }
      if (ch == ' ')
      {
        if (majcnt == 0 || mincnt == 0)
        {
          errno = EBADMSG;
          perror("Bad message");
          return 1;
        }
        spseen = 1;
      }
    }
    else if (!sp2seen)
    {
      if (twohundredcnt < 3 && twohundred[twohundredcnt] == ch)
      {
        twohundredcnt++;
      }
      else if (ch == ' ')
      {
        sp2seen = 1;
      }
      else
      {
        errno = EBADMSG;
        perror("Bad message");
        return 1;
      }
    }
    if (crlfcrlf[crlfcrlfcnt] == ch)
    {
      crlfcrlfcnt++;
    }
    if (crlfcrlfcnt == 4)
    {
      if (twohundredcnt != 3 || !sp2seen)
      {
        errno = EBADMSG;
        perror("Bad message");
        return 1;
      }
      break;
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
      outbufcnt += ret;
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
      inbufcnt += ret;
    }
    if (FD_ISSET(1, &writefds))
    {
      ret = write(1, inbuf, inbufcnt);
      if (ret < 0 && (errno != EWOULDBLOCK && errno != EAGAIN))
      {
        perror("Err");
        exit(1);
      }
      memmove(inbuf, inbuf+ret, inbufcnt-ret);
      inbufcnt -= ret;
    }
    if (FD_ISSET(sockfd, &writefds))
    {
      ret = write(sockfd, outbuf, outbufcnt);
      if (ret < 0 && (errno != EWOULDBLOCK && errno != EAGAIN))
      {
        perror("Err");
        exit(1);
      }
      memmove(outbuf, outbuf+ret, outbufcnt-ret);
      outbufcnt -= ret;
    }
  }

  return 0;
}
