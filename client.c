/* Generic */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Network */
#include <netdb.h>
#include <sys/socket.h>
#include <pthread.h>
#define BUF_SIZE 250
volatile int cfd;
char *path;
char *schedalg;
pthread_barrier_t barrier;

// Get host information (used to establishConnection)
struct addrinfo *getHostInfo(char* host, char* port) {
  int r;
  struct addrinfo hints, *getaddrinfo_res;
  // Setup hints
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if ((r = getaddrinfo(host, port, &hints, &getaddrinfo_res))) {
    fprintf(stderr, "[getHostInfo:21:getaddrinfo] %s\n", gai_strerror(r));
    return NULL;
  }

  return getaddrinfo_res;
}

// Establish connection with host
int establishConnection(struct addrinfo *info) {
  if (info == NULL) return -1;

  int clientfd;
  for (;info != NULL; info = info->ai_next) {
    if ((clientfd = socket(info->ai_family,
                           info->ai_socktype,
                           info->ai_protocol)) < 0) {
      perror("[establishConnection:35:socket]");
      continue;
    }

    if (connect(clientfd, info->ai_addr, info->ai_addrlen) < 0) {
      close(clientfd);
      perror("[establishConnection:42:connect]");
      continue;
    }

    freeaddrinfo(info);
    return clientfd;
  }

  freeaddrinfo(info);
  return -1;
}

// Send GET request
void GET(int clientfd, char *path) { 
  // pthread_barrier_wait(&barrier);
  char req[1000] = {0};
  sprintf(req, "GET %s HTTP/1.0\r\n\r\n", path);
  send(cfd, req, strlen(req), 0);
  
}

void tpool_init(size_t num_threads, void *(*start_routine) (void *), int clientfd, char* file)
{
    pthread_t  thread;
    size_t     i;
    cfd = clientfd;
    path = file;
    for (i=0; i<num_threads; i++) {
        pthread_create(&thread, NULL, *start_routine, (void *) (i + 1));
        pthread_detach(thread); // make non-joinable
    }
}
static void *worker(void* arg){
  if (!strncmp(schedalg, "FIFO", 4)){
    return NULL;
  } 
  else
    while(1){
      GET(cfd, path);
    }
  return NULL;
}

//MAIN
int main(int argc, char **argv) {
  int clientfd;
  char buf[BUF_SIZE];

  if (argc != 7 && argc != 6) {
    fprintf(stderr, "USAGE: %s <hostname> <port> <threads> <schedalg> <filename1> <*opt*filename2> \n", argv[0]);
    return 1;
  }

  // Establish connection with <hostname>:<port>
  clientfd = establishConnection(getHostInfo(argv[1], argv[2]));
  if (clientfd == -1) {
    fprintf(stderr,
            "[main:73] Failed to connect to: %s:%s%s \n",
            argv[1], argv[2], argv[3]);
    return 3;
  }
  // Send GET request > stdout
  
  int numThreads = atoi(argv[3]);
  char *file = argv[5];
  schedalg = argv[4];
  //also add for possible second file 
  pthread_barrier_init(&barrier, NULL, numThreads);
  tpool_init(numThreads, worker, clientfd, file);
  while (recv(clientfd, buf, BUF_SIZE, 0) > 0) {
    fputs(buf, stdout);
    memset(buf, 0, BUF_SIZE);
  }

  close(clientfd);
  return 0;
}
