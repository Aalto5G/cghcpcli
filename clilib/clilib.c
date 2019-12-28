#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/poll.h>
#include "dnshdr.h"
#include "clilib.h"

static int global_inited = 0;

#define NAMESERVERS_MAXCOUNT 3
static size_t nameservers_count = 0;
static uint32_t nameservers[NAMESERVERS_MAXCOUNT];

static pthread_mutex_t global_mtx = PTHREAD_MUTEX_INITIALIZER;

struct searchentry {
  char domain[256];
};

#define SEARCHENTRIES_MAXCOUNT 6
static size_t searchentries_count = 0;
static struct searchentry searchentries[SEARCHENTRIES_MAXCOUNT];

static void global_addr_set(void)
{
  char *line = NULL;
  size_t n = 0;
  FILE *f;
  struct in_addr in;
  if (global_inited)
  {
    return;
  }
  pthread_mutex_lock(&global_mtx);
  if (global_inited)
  {
    pthread_mutex_unlock(&global_mtx);
    return;
  }
  f = fopen("/etc/resolv.conf", "r");
  if (f == NULL)
  {
    if (nameservers_count == 0)
    {
      nameservers[nameservers_count] = (127<<24)|1;
      nameservers_count++;
    }
    global_inited = 1;
    pthread_mutex_unlock(&global_mtx);
    return;
  }
  for (;;)
  {
    if (getline(&line, &n, f) < 0)
    {
      break;
    }
    if (strncmp(line, "search ", 7) == 0 ||
        strncmp(line, "search\t", 7) == 0)
    {
      char *entry = line+7;
      char *end = entry + strlen(entry);
      while (entry != end)
      {
        char *sp = entry + strcspn(entry, " \t\r\n");
        if (*sp)
        {
          *sp = '\0';
        }
        else
        {
          sp--;
        }
        if (searchentries_count < SEARCHENTRIES_MAXCOUNT)
        {
          snprintf(searchentries[searchentries_count].domain,
                   sizeof(searchentries[searchentries_count].domain), "%s",
                   entry);
          searchentries_count++;
        }
        entry = sp+1 + strspn(sp+1, " \t\r\n");
      }
    }
    if (strncmp(line, "nameserver ", 11) == 0 ||
        strncmp(line, "nameserver\t", 11) == 0)
    {
      char *srv = line+11;
      char *end = srv + strlen(srv) - 1;
      while (*srv == ' ' || *srv == '\t')
      {
        srv++;
      }
      while (*end == ' ' || *end == '\t')
      {
        *end = '\0';
        end--;
      }
      if (inet_aton(srv, &in) == 1)
      {
        if (nameservers_count < NAMESERVERS_MAXCOUNT)
        {
          nameservers[nameservers_count] = ntohl(in.s_addr);
          nameservers_count++;
        }
      }
    }
  }
  if (nameservers_count == 0)
  {
    nameservers[nameservers_count] = (127<<24)|1;
    nameservers_count++;
  }
  fclose(f);
  free(line);
  global_inited = 1;
  pthread_mutex_unlock(&global_mtx);
}

static int resolv_patha(struct dst *dst)
{
  int sockfd;
  char pathfirst[8192];
  char querya[1536] = {0};
  char answer[1536] = {0};
  struct sockaddr_in ss = {};
  struct sockaddr_storage ss2 = {};
  socklen_t sslen = sizeof(ss), ss2len = sizeof(ss2);
  struct timeval tv;
  char *bang, *colon;
  uint16_t txid = rand()&0xFFFF;
  uint16_t txida;
  uint16_t qoffa;
  uint16_t remcnt;
  struct in_addr in;
  int answer_a = 0;
  int retrya = 0;
  size_t curserver = 0;

  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0)
  {
    return -errno;
  }

  tv.tv_sec = 1;
  tv.tv_usec = 0;
  if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0) {
    close(sockfd);
    return -errno;
  }

  global_addr_set();

  ss.sin_family = AF_INET;
  ss.sin_addr.s_addr = htonl(nameservers[curserver++]);
  curserver %= nameservers_count;
  ss.sin_port = htons(53);

  snprintf(pathfirst, sizeof(pathfirst), "%s", dst->path);
  bang = strchr(pathfirst, '!');
  if (bang && *bang)
  {
    *bang = '\0';
  }
  colon = strchr(pathfirst, ':');
  if (colon && *colon)
  {
    *colon = '\0';
  }

  if (inet_aton(pathfirst, &in))
  {
    dst->family = AF_INET;
    dst->u.ip = ntohl(in.s_addr);
    close(sockfd);
    return 0;
  }

  //printf("Resolving %s\n", pathfirst);

  txida = txid++;
  dns_set_id(querya, txida);
  dns_set_qr(querya, 0);
  dns_set_opcode(querya, 0);
  dns_set_tc(querya, 0);
  dns_set_rd(querya, 1);
  dns_set_z(querya);
  dns_set_rcode(querya, 0);
  dns_set_qdcount(querya, 0);
  dns_set_ancount(querya, 0);
  dns_set_nscount(querya, 0);
  dns_set_arcount(querya, 0);

  dns_next_init_qd(querya, &qoffa, &remcnt, sizeof(querya));
  dns_set_qdcount(querya, dns_qdcount(querya) + 1);
  dns_put_next_qr(querya, &qoffa, &remcnt, sizeof(querya), pathfirst, 1, 1);

  if (sendto(sockfd, querya, qoffa, 0, (struct sockaddr*)&ss, sslen) < 0)
  {
    //printf("sendto failed\n");
    close(sockfd);
    return -errno;
  }

  while ((!answer_a) && (retrya <= 4))
  {
    int recvd;
    recvd = recvfrom(sockfd, answer, sizeof(answer), 0, (struct sockaddr*)&ss2, &ss2len);
    if (recvd < 0)
    {
      if (errno == EAGAIN)
      {
        if (!answer_a)
        {
          //printf("resent A\n");
          ss.sin_addr.s_addr = htonl(nameservers[curserver++]);
          curserver %= nameservers_count;
          if (sendto(sockfd, querya, qoffa, 0, (struct sockaddr*)&ss, sslen) < 0)
          {
            //printf("sendto failed\n");
            close(sockfd);
            return -errno;
          }
          retrya++;
        }
      }
      continue;
    }
  
    if (dns_id(answer) == txida && !answer_a)
    {
      uint16_t qtype;
      char databuf[8192];
      size_t datalen;
      if (recursive_resolve(answer, (size_t)recvd, pathfirst, 1, &qtype,
                            databuf, sizeof(databuf), &datalen) == 0)
      {
        if (datalen == 4 && qtype == 1)
        {
          dst->family = AF_INET;
          dst->u.ip = hdr_get32n(databuf);
#if 0
          printf("%d.%d.%d.%d\n", (unsigned char)databuf[0],
            (unsigned char)databuf[1],
            (unsigned char)databuf[2],
            (unsigned char)databuf[3]);
#endif
          close(sockfd);
          return 0;
        }
      }
      answer_a = 1;
    }
  }

  //printf("Not found\n"); // FIXME rm
  
  close(sockfd);
  return -ENXIO;
}

int get_dst(struct dst *dst, int try_ipv6, char *name)
{
  struct timeval tv;
  int sockfd;
  char namcgtp[8192] = {0};
  char querya[1536] = {0};
  char queryaaaa[1536] = {0};
  char querytxt[1536] = {0};
  char answer[1536] = {0};
  struct sockaddr_in ss = {};
  struct sockaddr_storage ss2 = {};
  socklen_t sslen, ss2len;
  uint16_t remcnt;
  uint16_t qoffa, qofftxt, qoffaaaa;
  uint16_t txid = rand()&0xFFFF;
  int answer_a = 0, answer_txt = 0, answer_aaaa = 0, answer_a_ok = 0;
  int txida;
  int txidtxt;
  int txidaaaa;
  int retrya = 0, retrytxt = 0, retryaaaa = 0;
  uint16_t qtype;
  char databuf[8192];
  size_t datalen;
  size_t i;
  size_t curserver = 0;

  global_addr_set();

  if (strchr(name, '.') == NULL)
  {
    for (i = 0; i < searchentries_count; i++)
    {
      snprintf(namcgtp, sizeof(namcgtp), "%s.%s", name,
               searchentries[i].domain);
      if (get_dst(dst, try_ipv6, namcgtp) == 0)
      {
        return 0;
      }
    }
  }

  snprintf(namcgtp, sizeof(namcgtp), "_cgtp.%s", name);

  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0)
  {
    return -errno;
  }

  tv.tv_sec = 1;
  tv.tv_usec = 0;
  if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0) {
    close(sockfd);
    return -errno;
  }

  ss.sin_family = AF_INET;
  ss.sin_addr.s_addr = htonl(nameservers[curserver++]);
  curserver %= nameservers_count;
  ss.sin_port = htons(53);

  dst->family = 0;
  dst->u.ip = 0;
  dst->path[0] = '\0';

  sslen = sizeof(ss);
  ss2len = sizeof(ss);

  txidtxt = txid++;
  dns_set_id(querytxt, txidtxt);
  dns_set_qr(querytxt, 0);
  dns_set_opcode(querytxt, 0);
  dns_set_tc(querytxt, 0);
  dns_set_rd(querytxt, 1);
  dns_set_z(querytxt);
  dns_set_rcode(querytxt, 0);
  dns_set_qdcount(querytxt, 0);
  dns_set_ancount(querytxt, 0);
  dns_set_nscount(querytxt, 0);
  dns_set_arcount(querytxt, 0);

  dns_next_init_qd(querytxt, &qofftxt, &remcnt, sizeof(querytxt));
  dns_set_qdcount(querytxt, dns_qdcount(querytxt) + 1);
  dns_put_next_qr(querytxt, &qofftxt, &remcnt, sizeof(querytxt), namcgtp, 16, 1);

  txida = txid++;
  dns_set_id(querya, txida);
  dns_set_qr(querya, 0);
  dns_set_opcode(querya, 0);
  dns_set_tc(querya, 0);
  dns_set_rd(querya, 1);
  dns_set_z(querya);
  dns_set_rcode(querya, 0);
  dns_set_qdcount(querya, 0);
  dns_set_ancount(querya, 0);
  dns_set_nscount(querya, 0);
  dns_set_arcount(querya, 0);

  dns_next_init_qd(querya, &qoffa, &remcnt, sizeof(querya));
  dns_set_qdcount(querya, dns_qdcount(querya) + 1);
  dns_put_next_qr(querya, &qoffa, &remcnt, sizeof(querya), name, 1, 1);

  txidaaaa = txid++;
  dns_set_id(queryaaaa, txidaaaa);
  dns_set_qr(queryaaaa, 0);
  dns_set_opcode(queryaaaa, 0);
  dns_set_tc(queryaaaa, 0);
  dns_set_rd(queryaaaa, 1);
  dns_set_z(queryaaaa);
  dns_set_rcode(queryaaaa, 0);
  dns_set_qdcount(queryaaaa, 0);
  dns_set_ancount(queryaaaa, 0);
  dns_set_nscount(queryaaaa, 0);
  dns_set_arcount(queryaaaa, 0);

  dns_next_init_qd(queryaaaa, &qoffaaaa, &remcnt, sizeof(queryaaaa));
  dns_set_qdcount(queryaaaa, dns_qdcount(queryaaaa) + 1);
  dns_put_next_qr(queryaaaa, &qoffaaaa, &remcnt, sizeof(queryaaaa), name, 28, 1);

  if (sendto(sockfd, querytxt, qofftxt, 0, (struct sockaddr*)&ss, sslen) < 0)
  {
    //printf("sendto failed\n");
    close(sockfd);
    return -errno;
  }

  while ((!answer_txt) && (retrytxt <= 4))
  {
    int recvd;
    recvd = recvfrom(sockfd, answer, sizeof(answer), 0, (struct sockaddr*)&ss2, &ss2len);
    if (recvd < 0)
    {
      if (errno == EAGAIN)
      {
        if (!answer_txt)
        {
          ss.sin_addr.s_addr = htonl(nameservers[curserver++]);
          curserver %= nameservers_count;
          //printf("resent TXT\n");
          if (sendto(sockfd, querytxt, qofftxt, 0, (struct sockaddr*)&ss, sslen) < 0)
          {
            //printf("sendto failed\n");
            close(sockfd);
            return -errno;
          }
          retrytxt++;
        }
      }
      continue;
    }
  
    if (dns_id(answer) == txidtxt && !answer_txt)
    {
      if (recursive_resolve(answer, (size_t)recvd, namcgtp, 1, &qtype,
                            databuf, sizeof(databuf)-1, &datalen) == 0)
      {
        if (qtype == 16)
        {
          databuf[datalen] = '\0';
          snprintf(dst->path, sizeof(dst->path), "%s", databuf+1);
          //printf("%s\n", databuf+1);
          close(sockfd);
          return resolv_patha(dst);
        }
      }
      answer_txt = 1;
    }
  }

  curserver = 0;
  ss.sin_addr.s_addr = htonl(nameservers[curserver++]);
  curserver %= nameservers_count;

  if (sendto(sockfd, querya, qoffa, 0, (struct sockaddr*)&ss, sslen) < 0)
  {
    //printf("sendto failed\n");
    close(sockfd);
    return -errno;
  }
  if (try_ipv6 &&
      sendto(sockfd, queryaaaa, qoffaaaa, 0, (struct sockaddr*)&ss, sslen) < 0)
  {
    //printf("sendto failed\n");
    close(sockfd);
    return -errno;
  }
  if (!try_ipv6)
  {
    answer_aaaa = 1;
  }

  while ((!answer_a || !answer_aaaa) && (retrya <= 4 || retryaaaa <= 4))
  {
    int recvd;
    recvd = recvfrom(sockfd, answer, sizeof(answer), 0, (struct sockaddr*)&ss2, &ss2len);
    if (recvd < 0)
    {
      if (errno == EAGAIN)
      {
        ss.sin_addr.s_addr = htonl(nameservers[curserver++]);
        curserver %= nameservers_count;
        if (!answer_a)
        {
          //printf("resent A\n");
          if (sendto(sockfd, querya, qoffa, 0, (struct sockaddr*)&ss, sslen) < 0)
          {
            //printf("sendto failed\n");
            close(sockfd);
            return -errno;
          }
          retrya++;
        }
        if (!answer_aaaa)
        {
          //printf("resent AAAA\n");
          if (sendto(sockfd, queryaaaa, qoffaaaa, 0, (struct sockaddr*)&ss, sslen) < 0)
          {
            //printf("sendto failed\n");
            close(sockfd);
            return -errno;
          }
          retryaaaa++;
        }
      }
      continue;
    }
  
    if (dns_id(answer) == txida && !answer_a)
    {
      if (recursive_resolve(answer, (size_t)recvd, name, 1, &qtype,
                            databuf, sizeof(databuf), &datalen) == 0)
      {
        if (datalen == 4 && qtype == 1)
        {
          dst->family = AF_INET;
          dst->u.ip = hdr_get32n(databuf);
#if 0
          printf("%d.%d.%d.%d\n", (unsigned char)databuf[0],
            (unsigned char)databuf[1],
            (unsigned char)databuf[2],
            (unsigned char)databuf[3]);
#endif
          answer_a_ok = 1;
        }
      }
      answer_a = 1;
    }
    if (dns_id(answer) == txidaaaa && !answer_aaaa)
    {
      if (recursive_resolve(answer, (size_t)recvd, name, 1, &qtype,
                            databuf, sizeof(databuf), &datalen) == 0)
      {
        if (datalen == 16 && qtype == 28)
        {
          dst->family = AF_INET6;
          memcpy(dst->u.ipv6, databuf, 16);
          close(sockfd);
          return 0;
        }
      }
      answer_aaaa = 1;
    }
  }

  close(sockfd);
  if (answer_a_ok)
  {
    return 0;
  }
  return -ENXIO;
}

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
      bytes_written += (size_t)ret;
      reduceret = reduce_iovs(iovs, sz, (size_t)ret);
      if (reduceret == sz)
      {
        return (ssize_t)bytes_written;
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
        return (ssize_t)bytes_written;
      }
      else
      {
        return -1;
      }
    }
  }
}

static ssize_t read_all(int sockfd, char *buf, size_t sz)
{
  size_t bytes_read = 0;
  ssize_t ret;
  struct pollfd pfd;
  if (sz == 0)
  {
    return 0;
  }
  for (;;)
  {
    ret = read(sockfd, buf + bytes_read, sz - bytes_read);
    if (ret > 0)
    {
      bytes_read += (size_t)ret;
      if (bytes_read >= sz)
      {
        return (ssize_t)bytes_read;
      }
    }
    else if (ret <= 0)
    {
      if (ret == 0)
      {
        return (ssize_t)bytes_read;
      }
      if (ret < 0 && errno == EINTR)
      {
        continue;
      }
      else if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
      {
        pfd.fd = sockfd;
        pfd.events = POLLIN;
        poll(&pfd, 1, -1);
        continue;
      }
      else if (bytes_read > 0)
      {
        return (ssize_t)bytes_read;
      }
      else
      {
        return -1;
      }
    }
  }
}

static const char conbegin[] = "CONNECT ";
static const char colon[] = ":";
static const char interim[] = " HTTP/1.1\r\nHost: ";
static const char crlfcrlf[] = "\r\n\r\n";
static const char httpslash[] = "HTTP/";
static const char twohundred[] = "200";

int write_http_connect_port(int fd, const char *host, uint16_t port)
{
  ssize_t bytes_expected, bytes_written;
  char portint[16] = {0};
  struct iovec iovs[] = {
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
  snprintf(portint, sizeof(portint), "%d", (int)port);
  iovs[1].iov_base = (char*)host;
  iovs[1].iov_len = strlen(host);
  iovs[3].iov_len = strlen(portint);
  iovs[5].iov_base = (char*)host;
  iovs[5].iov_len = strlen(host);
  iovs[7].iov_len = strlen(portint);
  bytes_expected = (ssize_t)bytes_iovs(iovs, 9);
  bytes_written = writev_all(fd, iovs, 9);
  if (bytes_written != bytes_expected)
  {
    return -1;
  }
  return 0;
}

int write_http_connect(int fd, const char *host_and_port)
{
  ssize_t bytes_expected, bytes_written;
  struct iovec iovs[] = {
    {.iov_base = (char*)conbegin, .iov_len = sizeof(conbegin)-1},
    {.iov_base = NULL, .iov_len = 0}, // [1]
    {.iov_base = (char*)interim, .iov_len = sizeof(interim)-1},
    {.iov_base = NULL, .iov_len = 0}, // [3]
    {.iov_base = (char*)crlfcrlf, .iov_len = sizeof(crlfcrlf)-1},
  };
  iovs[1].iov_base = (char*)host_and_port;
  iovs[1].iov_len = strlen(host_and_port);
  iovs[3].iov_base = (char*)host_and_port;
  iovs[3].iov_len = strlen(host_and_port);
  bytes_expected = (ssize_t)bytes_iovs(iovs, 5);
  bytes_written = writev_all(fd, iovs, 5);
  if (bytes_written != bytes_expected)
  {
    return -1;
  }
  return 0;
}

int read_http_ok(int fd)
{
  size_t httpslashcnt = 0;
  size_t majcnt = 0;
  size_t dot_seen = 0;
  size_t spseen = 0;
  size_t sp2seen = 0;
  size_t mincnt = 0;
  size_t crlfcrlfcnt = 0;
  size_t twohundredcnt = 0;
  char buf[1024] = {};
  ssize_t cnt = 0;
  ssize_t read_ret = 0;
  for (;;)
  {
    char ch;
    struct pollfd pfd;
    if (read_ret <= 0 || cnt >= read_ret)
    {
      errno = EBADMSG;
      if (read_ret > 0)
      {
        if (read_all(fd, buf, (size_t)read_ret) != read_ret)
        {
          return -1;
        }
      }
      cnt = 0;
      read_ret = recv(fd, buf, sizeof(buf), MSG_PEEK);
    }
    if (read_ret < 0 && (errno == EWOULDBLOCK || errno == EAGAIN))
    {
      pfd.fd = fd;
      pfd.events = POLLIN;
      poll(&pfd, 1, -1);
      continue;
    }
    if (read_ret < 0)
    {
      return -1;
    }
    if (read_ret == 0)
    {
      errno = EBADMSG;
      return -1;
    }
    ch = buf[cnt++];
    if (httpslashcnt < 5)
    {
      if (httpslash[httpslashcnt] == ch)
      {
        httpslashcnt++;
      }
      else
      {
        errno = EBADMSG;
        return -1;
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
          return -1;
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
        return -1;
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
        return -1;
      }
      break;
    }
  }
  if (read_all(fd, buf, (size_t)cnt) != cnt)
  {
    return -1;
  }
  return 0;
}

int connect_ex_dst(int sockfd, struct dst *dst, uint16_t port)
{
  struct sockaddr_in sin = {};
  struct sockaddr_in6 sin6 = {};
  char *namptr;
  char *endptr;
  char *colonptr;
  const int gw_port = 8080;
  char *portptr;
  unsigned long portul;
  uint16_t used_port;
  struct pollfd pfd;
  int connret = 0;

  namptr = dst->path;
  endptr = strchr(namptr, '!');
  if (endptr == NULL)
  {
    if (dst->family == AF_INET6)
    {
      sin6.sin6_family = AF_INET6;
      sin6.sin6_port = htons(port);
      memcpy(sin6.sin6_addr.s6_addr, dst->u.ipv6, 16);
      if (connect(sockfd, (const struct sockaddr*)&sin6, sizeof(sin6)) < 0)
      {
        return -1;
      }
      return 0;
    }
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = htonl(dst->u.ip);
    if (connect(sockfd, (const struct sockaddr*)&sin, sizeof(sin)) < 0)
    {
      return -1;
    }
    return 0;
  }
  *endptr = '\0';
  portptr = strchr(namptr, ':');
  if (portptr)
  {
    char *portendptr = NULL;
    portul = strtoul(portptr+1, &portendptr, 10);
    if (portptr[1] == '\0' || *portendptr != '\0')
    {
      errno = ENXIO;
      return -1;
    }
    if (portul > 65535)
    {
      errno = ENXIO;
      return -1;
    }
    used_port = portul;
  }
  else
  {
    used_port = gw_port;
  }
  if (sin.sin_family == AF_INET6)
  {
    sin6.sin6_family = AF_INET6;
    sin6.sin6_port = htons(used_port);
    memcpy(sin6.sin6_addr.s6_addr, dst->u.ipv6, 16);
    connret = connect(sockfd, (const struct sockaddr*)&sin6, sizeof(sin6));
    if (connret < 0 && errno != EINPROGRESS)
    {
      return -1;
    }
  }
  else
  {
    sin.sin_family = AF_INET;
    sin.sin_port = htons(used_port);
    sin.sin_addr.s_addr = htonl(dst->u.ip);
    connret = connect(sockfd, (const struct sockaddr*)&sin, sizeof(sin));
    if (connret < 0 && errno != EINPROGRESS)
    {
      return -1;
    }
  }
  if (connret < 0 && errno == EINPROGRESS)
  {
    pfd.fd = sockfd;
    pfd.events = POLLOUT;
    poll(&pfd, 1, -1);
  }
  namptr = endptr + 1;
  while (namptr)
  {
    endptr = strchr(namptr, '!');
    if (endptr)
    {
      *endptr = '\0';
    }
    colonptr = strchr(namptr, ':');
    if (!endptr && colonptr)
    {
      errno = ENXIO;
      return -1; // Last hop may not have a port specified
    }
    if (colonptr)
    {
      if (write_http_connect(sockfd, namptr) != 0)
      {
        return -1;
      }
    }
    else
    {
      if (endptr)
      {
        used_port = gw_port;
      }
      else
      {
        used_port = port;
      }
      if (write_http_connect_port(sockfd, namptr, used_port) != 0)
      {
        return -1;
      }
    }
    if (read_http_ok(sockfd) != 0)
    {
      return -1;
    }
    if (endptr)
    {
      namptr = endptr + 1;
    }
    else
    {
      namptr = NULL;
    }
  }
  return 0;
}

int socket_ex_ipv4(char *name, uint16_t port)
{
  struct dst dst;
  int sockfd;
  if (get_dst(&dst, 0, name) != 0)
  {
    errno = ENXIO;
    return -1;
  }
  if (dst.family != AF_INET)
  {
    abort();
  }
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)
  {
    return -1;
  }
  if (connect_ex_dst(sockfd, &dst, port) != 0)
  {
    close(sockfd);
    return -1;
  }
  return sockfd;
}

int socket_ex(char *name, uint16_t port)
{
  struct dst dst;
  int sockfd;
  if (get_dst(&dst, 1, name) != 0)
  {
    errno = ENXIO;
    return -1;
  }
  if (dst.family == AF_INET6 && dst.path[0] == '\0')
  {
    sockfd = socket(AF_INET6, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
      return socket_ex_ipv4(name, port);
    }
    if (connect_ex_dst(sockfd, &dst, port) != 0)
    {
      close(sockfd);
      return socket_ex_ipv4(name, port);
    }
    return sockfd;
  }
  if (dst.family != AF_INET)
  {
    abort();
  }
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)
  {
    return -1;
  }
  if (connect_ex_dst(sockfd, &dst, port) != 0)
  {
    close(sockfd);
    return -1;
  }
  return sockfd;
}

int connect_ex(int sockfd, char *name, uint16_t port)
{
  struct dst dst;
  if (get_dst(&dst, 0, name) != 0)
  {
    errno = ENXIO;
    return -1;
  }
  return connect_ex_dst(sockfd, &dst, port);
}
