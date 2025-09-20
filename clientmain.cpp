#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
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
          printf("ERROR:SENDING\n");
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
              printf("ERROR:RECEIVING\n");
              return -1;
          }
          return got;
      }
      if (rv < 0) {
          printf("ERROR:FILE DESCRIPTOR\n");
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
    printf("ERROR:CALCULATING RESULT\n");
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

  struct sockaddr_in server;
  memset(&server, 0, sizeof(server));
  server.sin_family = AF_INET;
  server.sin_port   = htons(destport);
  if (inet_pton(AF_INET, desthost, &server.sin_addr) != 1) {
      printf("ERROR: CONVERSION TO BINARY\n");
      return 1;
  }

  struct calcMessage init_msg;
  memset(&init_msg, 0, sizeof(init_msg));
  init_msg.type          = htons(22);
  init_msg.message       = htonl(0);
  init_msg.protocol      = htons(17);
  init_msg.major_version = htons(1);
  init_msg.minor_version = htons(0);

  char buf[1024];
  struct sockaddr_storage client;
  socklen_t client_len = sizeof(client);

  ssize_t n = send_with_retry(sock,
                              &init_msg, sizeof(init_msg),
                              buf, sizeof(buf),
                              (struct sockaddr*)&server, sizeof(server),
                              (struct sockaddr*)&client, &client_len);
  if (n == -2) {
    printf("No reply after 3 attempts. Exit.\n");
    return 1;
  }
  if (n < 0)
    return 1;

  if ((size_t)n == sizeof(struct calcMessage)) {
    struct calcMessage msg;
    memcpy(&msg, buf, sizeof(msg));
    uint16_t t = ntohs(msg.type);
    uint32_t m = ntohl(msg.message);
    if (t == 2 && m == 2)
      printf("Server replied: NOT OK\n");
    else
      printf("ERROR:UNKOWN REPLY.\n");
    return 0;
  }

  if ((size_t)n != sizeof(struct calcProtocol)) {
    fprintf(stderr, "ERROR WRONG SIZE OR INCORRECT PROTOCOL\n");
    return 1;
  }

  struct calcProtocol task;
  memcpy(&task, buf, sizeof(task));
  task.type          = ntohs(task.type);
  task.major_version = ntohs(task.major_version);
  task.minor_version = ntohs(task.minor_version);
  task.id            = ntohl(task.id);
  task.arith         = ntohl(task.arith);
  task.inValue1      = ntohl(task.inValue1);
  task.inValue2      = ntohl(task.inValue2);
  task.inResult      = ntohl(task.inResult);

  printf("Assignment id=%u arith=%u\n", task.id, task.arith);

  calculate(&task);

  struct calcProtocol reply = task;
  reply.type          = htons(2);
  reply.major_version = htons(task.major_version);
  reply.minor_version = htons(task.minor_version);
  reply.id            = htonl(task.id);
  reply.arith         = htonl(task.arith);
  reply.inValue1      = htonl(task.inValue1);
  reply.inValue2      = htonl(task.inValue2);
  reply.inResult      = htonl(task.inResult);

  client_len = sizeof(client);

  n = send_with_retry(sock,
                      &reply, sizeof(reply),
                      buf, sizeof(buf),
                      (struct sockaddr*)&server, sizeof(server),
                      (struct sockaddr*)&client, &client_len);

  if (n == -2) {
    printf("No reply after sending result.\n");
    return 1;
  } else if (n < 0)
    return 1;

  if ((size_t)n != sizeof(struct calcMessage)) {
    printf("ERROR WRONG SIZE OR INCORRECT PROTOCOL\n");
    return 1;
  }

  struct calcMessage finalmsg;
  memcpy(&finalmsg, buf, sizeof(finalmsg));
  uint32_t m = ntohl(finalmsg.message);
  if (m == 1)
    printf("Server replied: OK\n");
  else if (m == 2) 
    printf("Server replied: NOT OK\n");
  else 
    printf("Server replied with unknown message=%u\n", m);

  close(sock);
  return 0;

}
