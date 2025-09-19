#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <calcLib.h>
#include "protocol.h"


static ssize_t send_with_retry(int sock,
                               const void *buf, size_t len,
                               void *rbuf, size_t rlen,
                               struct sockaddr *srv, socklen_t slen,
                               struct sockaddr *from, socklen_t *flen)
{
  const int max_attempts = 3;
  const int timeout  = 2;
  
  for (int attempt = 1; attempt <= max_attempts; ++attempt) {
      if (sendto(sock, buf, len, 0, srv, slen) != (ssize_t)len) {
          printf("ERROR:SENDING");
          return -1;
      }

      fd_set fds;
      FD_ZERO(&fds);
      FD_SET(sock, &fds);
      struct timeval tv;
      tv.tv_sec  = timeout;
      tv.tv_usec = 0;

      int rv = select(sock + 1, &fds, NULL, NULL, &tv);
      if (rv > 0) {
          ssize_t got = recvfrom(sock, rbuf, rlen, 0, from, flen);
          if (got < 0) {
              printf("ERROR:RECEIVING");
              return -1;
          }
          return got;
      }
      if (rv < 0) {
          printf("ERROR:FILE DESCRIPTOR");
          return -1;
      }
  }
  return -2;
}

static void calculate(struct calcProtocol *p) {

  if (p->arith == 1)
    p->inResult = p->inValue1 + p->inValue2;
  else if (p->arith == 2)
    p->inResult = p->inValue1 - p->inValue2;
  else if (p->arith == 3)
    p->inResult = p->inValue1 * p->inValue2;
  else if (p->arith == 4)
    if (p->inValue2 != 0)
      p->inResult = p->inValue1/p->inValue2;
    else
      p->inResult = 0;  
  else if (p->arith == 5)
    p->flResult = p->flValue1 + p->flValue2;
  else if (p->arith == 6)
    p->flResult = p->flValue1 - p->flValue2;
  else if (p->arith == 7)
    p->flResult = p->flValue1 * p->flValue2;    
  else if (p->arith == 8)
    if (p->flValue2 != 0)
      p->flResult = p->flValue1 / p->flValue2;
    else
      p->flResult = 0;    
  else
    printf("ERROR:CALCULATING RESULT");
}


int main(int argc, char *argv[]){
  
  if (argc < 3) {
    printf("usage: %s <host> <port>\n", argv[0]);
  }

  const char *desthost = argv[1];
  int destport = atoi(argv[2]);
  printf("Connecting to %s:%d\n", desthost, destport);

  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
      perror("socket");
      return 1;
  }

}
