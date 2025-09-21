#include "protocol.h"
#include <arpa/inet.h>
#include <calcLib.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static ssize_t send_with_retry(int sock, const void *buf, size_t len,
                               void *rbuf, size_t rlen, struct sockaddr *server,
                               socklen_t slen, struct sockaddr *from,
                               socklen_t *flen) {
  const int max_attempts = 3;
  const int timeout = 2;

  for (int attempt = 1; attempt <= max_attempts; ++attempt) {
    if (sendto(sock, buf, len, 0, server, slen) != (ssize_t)len)
      return -1;

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sock, &fds);
    struct timeval tv;
    tv.tv_sec = timeout;
    tv.tv_usec = 0;

    int rv = select(sock + 1, &fds, NULL, NULL, &tv);
    if (rv > 0) {
      *flen = sizeof(struct sockaddr_storage);
      ssize_t got = recvfrom(sock, rbuf, rlen, 0, from, flen);
      if (got < 0)
        return -1;
      return got;
    } else if (rv < 0)
      return -1;
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
      p->inResult = p->inValue1 / p->inValue2;
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

static int resolve_addr(const char *host, int port,
                        struct sockaddr_storage *out, socklen_t *out_len,
                        int *out_family) {
  struct addrinfo hints, *res, *rp;
  char portstr[16];
  snprintf(portstr, sizeof(portstr), "%d", port);

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;

  int err = getaddrinfo(host, portstr, &hints, &res);
  if (err != 0) {
    printf("ERROR:RESOLVING HOST\n");
    return -1;
  }

  for (rp = res; rp != NULL; rp = rp->ai_next) {
    if (rp->ai_family == AF_INET6 || rp->ai_family == AF_INET) {
      memcpy(out, res->ai_addr, res->ai_addrlen);
      *out_len = res->ai_addrlen;
      *out_family = res->ai_family;
      freeaddrinfo(res);
      return 0;
    }
  }
  freeaddrinfo(res);
  return -1;
}

int main(int argc, char *argv[]) {
  char *desthost = NULL;
  int destport = 0;

  if (argc < 2) {
    printf("usage: %s <host> <port>\n", argv[0]);
    return -1;
  }

  if (argc >= 3) {
    desthost = argv[1];
    destport = atoi(argv[2]);
    if (destport <= 0)
      return -1;
  }

  if (argc == 2) {
    char *s = argv[1];
    char *colon = strchr(s, ':');
    if (!colon) {
      printf("ERROR:MISSING COLON. PROPER USAGE host:port\n");
      return -1;
    }
    size_t hostlen = (size_t)(colon - s);
    desthost = strndup(s, hostlen);
    if (!desthost) {
      free(desthost);
      return -1;
    }
    desthost[hostlen] = '\0';

    char *endptr;
    long port = strtol(colon + 1, &endptr, 10);
    if (*endptr != '\0') {
      printf("ERROR: INVALID PORT NUMBER\n");
      free(desthost);
      return -1;
    }
    destport = (int)port;
    if (destport <= 0) {
      free(desthost);
      return -1;
    }
  }

  printf("Connecting to %s:%d\n", desthost, destport);

  struct sockaddr_storage server_addr;
  socklen_t server_len;
  int family;
  if (resolve_addr(desthost, destport, &server_addr, &server_len, &family) < 0)
    return -1;

  int sock = socket(family, SOCK_DGRAM, 0);
  if (sock < 0) {
    printf("ERROR:SOCKET");
    return 1;
  }

  struct calcMessage init_msg;
  memset(&init_msg, 0, sizeof(init_msg));
  init_msg.type = htons(22);
  init_msg.message = htonl(0);
  init_msg.protocol = htons(17);
  init_msg.major_version = htons(1);
  init_msg.minor_version = htons(0);

  char buf[1024];
  struct sockaddr_storage client;
  socklen_t client_len = sizeof(client);

  ssize_t n =
      send_with_retry(sock, &init_msg, sizeof(init_msg), buf, sizeof(buf),
                      (struct sockaddr *)&server_addr, server_len,
                      (struct sockaddr *)&client, &client_len);

  if (n == -2) {
    printf("ERROR TIMEOUT\n");
    close(sock);
    return 1;
  } else if (n < 0) {
    printf("ERROR WRONG SIZE OR INCORRECT PROTOCOL\n");
    close(sock);
    return 1;
  }

  if ((size_t)n == sizeof(struct calcMessage)) {
    struct calcMessage msg;
    memcpy(&msg, buf, sizeof(msg));
    uint16_t t = ntohs(msg.type);
    uint32_t m = ntohl(msg.message);
    if (t == 2 && m == 2)
      printf("Server replied: NOT OK\n");
    else if (t == 2 && m == 1) {
      printf("Server replied: OK (unexpected early OK)\n");
      close(sock);
      return 0;
    } else {
      printf("ERROR WRONG SIZE OR INCORRECT PROTOCOL\n");
      close(sock);
      return 1;
    }
  } else if ((size_t)n != sizeof(struct calcProtocol)) {
    printf("ERROR WRONG SIZE OR INCORRECT PROTOCOL\n");
    close(sock);
    return 1;
  }

  struct calcProtocol task;
  memcpy(&task, buf, sizeof(task));
  task.type = ntohs(task.type);
  task.major_version = ntohs(task.major_version);
  task.minor_version = ntohs(task.minor_version);
  task.id = ntohl(task.id);
  task.arith = ntohl(task.arith);
  task.inValue1 = ntohl(task.inValue1);
  task.inValue2 = ntohl(task.inValue2);
  task.inResult = ntohl(task.inResult);

  printf("Assignment id=%u arith=%u\n", task.id, task.arith);

  calculate(&task);

  struct calcProtocol reply = task;
  reply.type = htons(2);
  reply.major_version = htons(task.major_version);
  reply.minor_version = htons(task.minor_version);
  reply.id = htonl(task.id);
  reply.arith = htonl(task.arith);
  reply.inValue1 = htonl(task.inValue1);
  reply.inValue2 = htonl(task.inValue2);
  reply.inResult = htonl(task.inResult);

  client_len = sizeof(client);

  n = send_with_retry(sock, &reply, sizeof(reply), buf, sizeof(buf),
                      (struct sockaddr *)&server_addr, server_len,
                      (struct sockaddr *)&client, &client_len);

  if (n == -2 || n < 0) {
    printf("ERROR WRONG SIZE OR INCORRECT PROTOCOL\n");
    close(sock);
    return 1;
  }

  if ((size_t)n != sizeof(struct calcMessage)) {
    printf("ERROR WRONG SIZE OR INCORRECT PROTOCOL\n");
    close(sock);
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
    printf("ERROR WRONG SIZE OR INCORRECT PROTOCOL\n");

  close(sock);
  return 0;
}
