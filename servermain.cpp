#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>
#include <math.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <calcLib.h>
#include "protocol.h"


#define MAX_JOBS 256
#define JOB_TIMEOUT 10

volatile sig_atomic_t terminate_flag = 0;
volatile sig_atomic_t housekeeping_flag = 0;

struct Job {
  int active;
  struct sockaddr_storage addr;
  socklen_t addr_len;
  uint32_t id;
  struct calcProtocol task;
  time_t assigned_at;
};

static struct Job jobs[MAX_JOBS];

static void sigint_handler(int signum) { (void)signum; terminate_flag = 1; }
static void sigalrm_handler(int signum){ (void)signum; housekeeping_flag = 1; }

static int addr_equal(const struct sockaddr_storage *a, socklen_t alen,
                      const struct sockaddr_storage *b, socklen_t blen) {
  if (a->ss_family != b->ss_family) return 0;
  if (a->ss_family == AF_INET) {
    const struct sockaddr_in *ai=(const struct sockaddr_in*)a;
    const struct sockaddr_in *bi=(const struct sockaddr_in*)b;
    return ai->sin_port==bi->sin_port &&
           ai->sin_addr.s_addr==bi->sin_addr.s_addr;
  } else if (a->ss_family == AF_INET6) {
    const struct sockaddr_in6 *ai6=(const struct sockaddr_in6*)a;
    const struct sockaddr_in6 *bi6=(const struct sockaddr_in6*)b;
    return ai6->sin6_port==bi6->sin6_port &&
           memcmp(&ai6->sin6_addr,&bi6->sin6_addr,sizeof(ai6->sin6_addr))==0;
  }
  return 0;
}

static int find_job_addr(const struct sockaddr_storage *addr,socklen_t len){
  for(int i=0;i<MAX_JOBS;i++)
    if(jobs[i].active && addr_equal(&jobs[i].addr,jobs[i].addr_len,addr,len))
      return i;
  return -1;
}

static int alloc_job(void){
  for(int i=0;i<MAX_JOBS;i++)
    if(!jobs[i].active) return i;
  return -1;
}

static void expire_jobs(void){
  time_t now=time(NULL);
  for(int i=0;i<MAX_JOBS;i++)
    if(jobs[i].active && difftime(now,jobs[i].assigned_at)>=JOB_TIMEOUT)
      jobs[i].active=0;
}

static void send_calc_msg(int sock,const struct sockaddr_storage *addr,socklen_t len,
                          uint16_t type,uint32_t message){
  struct calcMessage m;
  memset(&m,0,sizeof(m));
  m.type=htons(type);
  m.message=htonl(message);
  m.protocol=htons(17);
  m.major_version=htons(1);
  m.minor_version=htons(0);
  sendto(sock,&m,sizeof(m),0,(const struct sockaddr*)addr,len);
}

static void assign_task(int sock,const struct sockaddr_storage *addr,socklen_t len){
  int slot=alloc_job();
  if(slot<0){ send_calc_msg(sock,addr,len,2,2); return; }

  struct calcProtocol p;
  memset(&p,0,sizeof(p));
  p.type=htons(1);
  p.major_version=htons(1);
  p.minor_version=htons(0);

  static uint32_t next_id=1;
  uint32_t id=next_id++;
  if(next_id==0) next_id=1;
  p.id=htonl(id);

  int arith=(rand()%8)+1;
  p.arith=htonl(arith);

  if(arith<=4){
    int32_t v1=randomInt();
    int32_t v2=randomInt();
    p.inValue1=htonl(v1);
    p.inValue2=htonl(v2);
    p.inResult=htonl(0);
  }else{
    double f1=randomFloat();
    double f2=randomFloat();
    p.flValue1=f1;
    p.flValue2=f2;
    p.flResult=0.0;
  }

  jobs[slot].active=1;
  memcpy(&jobs[slot].addr,addr,len);
  jobs[slot].addr_len=len;
  jobs[slot].id=id;
  memcpy(&jobs[slot].task,&p,sizeof(p));
  jobs[slot].task.type=1;
  jobs[slot].task.major_version=1;
  jobs[slot].task.minor_version=0;
  jobs[slot].task.id=id;
  jobs[slot].task.arith=arith;
  jobs[slot].task.inValue1=ntohl(p.inValue1);
  jobs[slot].task.inValue2=ntohl(p.inValue2);
  jobs[slot].task.inResult=0;
  jobs[slot].assigned_at=time(NULL);

  sendto(sock,&p,sizeof(p),0,(const struct sockaddr*)addr,len);
}

using namespace std;
/* Needs to be global, to be rechable by callback and main */
int loopCount=0;
int terminate=0;


/* Call back function, will be called when the SIGALRM is raised when the timer expires. */
void checkJobbList(int signum){
  // As anybody can call the handler, its good coding to check the signal number that called it.

  printf("Let me be, I want to sleep, loopCount = %d.\n", loopCount);

  if(loopCount>20){
    printf("I had enough.\n");
    terminate=1;
  }
  
  return;
}



int main(int argc, char *argv[]){
  
  if(argc!=2){
    printf("Usage: %s <IP-or-DNS:PORT>\n",argv[0]);
    return 1;
  }

  char delim[] = ":";
  char *Desthost = strtok(argv[1], delim);
  char *Destport = strtok(NULL, delim);
  int port = atoi(Destport);
  if (!Desthost || !Destport) {
    printf("Wrong input arguments\n");
    return 1;
  }

  /* 
     Prepare to setup a reoccurring event every 10s. If it_interval, or it_value is omitted, it will be a single alarm 10s after it has been set. 
  */
  struct itimerval alarmTime;
  alarmTime.it_interval.tv_sec=10;
  alarmTime.it_interval.tv_usec=10;
  alarmTime.it_value.tv_sec=10;
  alarmTime.it_value.tv_usec=10;

  /* Regiter a callback function, associated with the SIGALRM signal, which will be raised when the alarm goes of */
  signal(SIGALRM, checkJobbList);
  setitimer(ITIMER_REAL,&alarmTime,NULL); // Start/register the alarm. 

#ifdef DEBUG
  printf("DEBUGGER LINE ");
#endif
  
  
  while(terminate==0){
    printf("This is the main loop, %d time.\n",loopCount);
    sleep(1);
    loopCount++;
  }

  printf("done.\n");
  return(0);


  
}
