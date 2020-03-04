/* Generic */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Network */
#include <netdb.h>
#include <sys/socket.h>

#define BUF_SIZE 250
typedef struct {
    int job_id;
	int job_fd; // the socket file descriptor
    // what other stuff needs to be here eventually?
} job_t;

typedef struct {
    job_t * jobs; // array of server Jobs on heap
    size_t buf_capacity;
    size_t head; // position of writer
    size_t tail; // position of reader
	  size_t capacity;
    pthread_mutex_t work_mutex;
    pthread_cond_t c_cond; // P/C condition variables
    pthread_cond_t p_cond;
} tpool_t;
static tpool_t pool;
static tpool_t *the_pool = &pool;

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
  //make t pool, add
  // Send GET request > stdout
  GET(clientfd, argv[3]);
  while (recv(clientfd, buf, BUF_SIZE, 0) > 0) {
    fputs(buf, stdout);
    memset(buf, 0, BUF_SIZE);
  }

  close(clientfd);
  return 0;
}
