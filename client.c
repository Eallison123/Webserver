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
#define HOST "localhost"
volatile int cfd;
char* host;
char *path;
char *schedalg;
char *port;
pthread_cond_t cond;
pthread_mutex_t mutex;
pthread_barrier_t barrier;
void print(int x){
  printf("%d", x);
  fflush(stdout);
}
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
  char req[1000] = {0};
  sprintf(req, "GET %s HTTP/1.0\r\n\r\n", path);
  send(clientfd, req, strlen(req), 0);
}

void tpool_init(size_t num_threads, void *(*start_routine) (void *), int clientfd, char* file)
{
    pthread_t  thread;
    size_t     i;
    cfd = clientfd;
    path = file;
    pthread_cond_init(&cond, NULL);
    pthread_mutex_init(&mutex, NULL);
    pthread_mutex_lock(&mutex);
    for (i=0; i<num_threads; i++) {
        pthread_create(&thread, NULL, *start_routine, (void *) (i + 1));
        if(!strncmp(schedalg, "FIFO", 4)){
          pthread_cond_wait(&cond, &mutex);
        }
        pthread_detach(thread); // make non-joinable
        pthread_cond_signal(&cond);
    }
}
static void *worker(void* arg){
  char buf[BUF_SIZE];
  /* 
  *when it's a FIFO schedule, it will wake up a thread, send a GET
  *request, then signal to another sleeping thread, and then wait 
  *to be signalled again as it gets put on wait
  */
  if (!strncmp(schedalg, "FIFO", 4)){
    while(1){
      int clientfd = establishConnection(getHostInfo(host, port));
      if (clientfd == -1) {
        fprintf(stderr, "[main:73] Failed to connect to: %s:%s \n", host, port); //removed it from printing argv[3]
        return NULL;
      }
      GET(clientfd, path);
      while (recv(clientfd, buf, BUF_SIZE, 0) > 0) {
        fputs(buf, stdout);
        memset(buf, 0, BUF_SIZE);
      }
      close(clientfd);
      pthread_cond_signal(&cond);
      pthread_cond_wait(&cond, &mutex);
    }
  }  
  /*
  *else is just concurent, in this case each thread will 
  *wait by the barrier after establishing a connection to send 
  *simultanious get requests to the web server in an inf loop
  */
  else
    while(1){
      int clientfd = establishConnection(getHostInfo(host, port));
      if (clientfd == -1) {
        fprintf(stderr, "[main:73] Failed to connect to: %s:%s \n", host, port);
        return NULL;
      }
      pthread_barrier_wait(&barrier);
      GET(clientfd, path);
      while (recv(clientfd, buf, BUF_SIZE, 0) > 0) {
        fputs(buf, stdout);
        memset(buf, 0, BUF_SIZE);
      }
      close(clientfd);
      // pthread_barrier_wait(&barrier);
    }
  return NULL;
}

//MAIN
int main(int argc, char **argv) {
  int clientfd;
  // char buf[BUF_SIZE];

  if (argc != 7 && argc != 6) {
    fprintf(stderr, "USAGE: %s <hostname> <port> <threads> <schedalg> <filename1> <*opt*filename2> \n", argv[0]);
    return 1;
  }

  // Establish connection with <hostname>:<port>
  print(1);
  clientfd = establishConnection(getHostInfo(argv[1], argv[2]));
  if (clientfd == -1) {
    fprintf(stderr,
            "[main:73] Failed to connect to: %s:%s%s \n",
            argv[1], argv[2], argv[3]);
    return 3;
  }
  print(2);
  // Send GET request > stdout
  port = argv[2];
  host = argv[1];
  int numThreads = atoi(argv[3]);
  char *file = argv[5];
  schedalg = argv[4];
  //also add for possible second file 
  pthread_barrier_init(&barrier, NULL, numThreads);
  tpool_init(numThreads, worker, clientfd, file);
  // GET(clientfd, file);
  // while (recv(clientfd, buf, BUF_SIZE, 0) > 0) {
  //   fputs(buf, stdout);
  //   memset(buf, 0, BUF_SIZE);
  // }
  // close(clientfd);
  while(1);
  return 0;
}
