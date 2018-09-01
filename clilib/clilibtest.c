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
#include "clilib.h"

int main(int argc, char **argv)
{
  int sockfd;
  int port;
  if (argc != 3)
  {
    printf("Usage: %s foo2.lan 80\n", argv[0]);
    return 1;
  }
  port = atoi(argv[2]);
  sockfd = socket_ex(argv[1], port);
  if (sockfd < 0)
  {
    perror("Err");
    return 1;
  }
  printf("Connection successful\n");
  return 0;
}
