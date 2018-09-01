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
#include <netdb.h>

int main(int argc, char **argv)
{
  int sockfd;
  int port;
  struct hostent *he;
  struct sockaddr_in sin;
  if (argc != 3)
  {
    printf("Usage: %s foo2.lan 80\n", argv[0]);
    return 1;
  }
  port = atoi(argv[2]);
  he = gethostbyname(argv[1]);
  if (he == NULL)
  {
    perror("Host not found");
    return 1;
  }
  sin.sin_family = AF_INET;
  sin.sin_port = htons(port);
  memcpy(&sin.sin_addr, he->h_addr_list[0], he->h_length);
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
  return 0;
}
