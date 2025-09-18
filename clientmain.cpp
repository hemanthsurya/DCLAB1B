#include <stdio.h>
#include <stdlib.h>
/* You will to add includes here */


// Included to get the support library
#include <calcLib.h>


#include "protocol.h"

int main(int argc, char *argv[]){
  
  if (argc < 3) {
    printf("usage: %s <host> <port>\n", argv[0]);
  }

  const char *desthost = argv[1];
  int destport = atoi(argv[2]);
  printf("Connecting to %s:%d\n", desthost, destport);
  

}
