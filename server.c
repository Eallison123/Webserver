#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <pthread.h>
#define VERSION 25
#define BUFSIZE 8096
#define ERROR      42
#define LOG        44
#define FORBIDDEN 403
#define NOTFOUND  404


/* what a worker thread needs to start a job */
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
static tpool_t *the_pool = &pool; // one pool to rule them all 



struct {
	char *ext;
	char *filetype;
} extensions [] = {
	{"gif", "image/gif" },  
	{"jpg", "image/jpg" }, 
	{"jpeg","image/jpeg"},
	{"png", "image/png" },  
	{"ico", "image/ico" },  
	{"zip", "image/zip" },  
	{"gz",  "image/gz"  },  
	{"tar", "image/tar" },  
	{"htm", "text/html" },  
	{"html","text/html" },  
	{0,0} };

static const char * HDRS_FORBIDDEN = "HTTP/1.1 403 Forbidden\nContent-Length: 185\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>403 Forbidden</title>\n</head><body>\n<h1>Forbidden</h1>\nThe requested URL, file type or operation is not allowed on this simple static file webserver.\n</body></html>\n";
static const char * HDRS_NOTFOUND = "HTTP/1.1 404 Not Found\nContent-Length: 136\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>404 Not Found</title>\n</head><body>\n<h1>Not Found</h1>\nThe requested URL was not found on this server.\n</body></html>\n";
static const char * HDRS_OK = "HTTP/1.1 200 OK\nServer: nweb/%d.0\nContent-Length: %ld\nConnection: close\nContent-Type: %s\n\n";
static int dummy; //keep compiler happy

void logger(int type, char *s1, char *s2, int socket_fd)
{
	int fd ;
	char logbuffer[BUFSIZE*2];

	switch (type) {
	case ERROR: (void)sprintf(logbuffer,"ERROR: %s:%s Errno=%d exiting pid=%d",s1, s2, errno,getpid()); 
		break;
	case FORBIDDEN: 
		dummy = write(socket_fd, HDRS_FORBIDDEN,271);
		(void)sprintf(logbuffer,"FORBIDDEN: %s:%s",s1, s2); 
		break;
	case NOTFOUND: 
		dummy = write(socket_fd, HDRS_NOTFOUND,224);
		(void)sprintf(logbuffer,"NOT FOUND: %s:%s",s1, s2); 
		break;
	case LOG: (void)sprintf(logbuffer," INFO: %s:%s:%d",s1, s2,socket_fd); break;
	}	
	/* No checks here, nothing can be done with a failure anyway */
	if((fd = open("nweb.log", O_CREAT| O_WRONLY | O_APPEND,0644)) >= 0) {
		dummy = write(fd,logbuffer,strlen(logbuffer)); 
		dummy = write(fd,"\n",1);      
		(void)close(fd);
	}
}

/* this is a child web server process, so we can exit on errors */
void web(int fd, int hit)
{
    int j, file_fd, buflen;
    long i, ret, len;
    char * fstr;
    static char buffer[BUFSIZE+1]; /* static so zero filled */

    ret =read(fd,buffer,BUFSIZE);   /* read Web request in one go */
    if(ret == 0 || ret == -1) { /* read failure stop now */
        logger(FORBIDDEN,"failed to read browser request","",fd);
        goto endRequest;
    }
    if(ret > 0 && ret < BUFSIZE) {  /* return code is valid chars */
        buffer[ret]=0;      /* terminate the buffer */
    }
    else {
        buffer[0]=0; 
    }
    for(i=0;i<ret;i++) {    /* remove CF and LF characters */
        if(buffer[i] == '\r' || buffer[i] == '\n') {
            buffer[i]='*';
        }
    }
    logger(LOG,"request",buffer,hit);
    if( strncmp(buffer,"GET ",4) && strncmp(buffer,"get ",4) ) {
        logger(FORBIDDEN,"Only simple GET operation supported",buffer,fd);
        goto endRequest;
    }
    for(i=4;i<BUFSIZE;i++) { /* null terminate after the second space to ignore extra stuff */
        if(buffer[i] == ' ') { /* string is "GET URL " +lots of other stuff */
            buffer[i] = 0;
            break;
        }
    }
    for(j=0;j<i-1;j++) {    /* check for illegal parent directory use .. */
        if(buffer[j] == '.' && buffer[j+1] == '.') {
            logger(FORBIDDEN,"Parent directory (..) path names not supported",buffer,fd);
            goto endRequest;
        }
    }
    if( !strncmp(&buffer[0],"GET /\0",6) || !strncmp(&buffer[0],"get /\0",6) ) { /* convert no filename to index file */
        (void)strcpy(buffer,"GET /index.html");
    }

    /* work out the file type and check we support it */
    buflen=strlen(buffer);
    fstr = (char *)0;
    for(i=0;extensions[i].ext != 0;i++) {
        len = strlen(extensions[i].ext);
        if( !strncmp(&buffer[buflen-len], extensions[i].ext, len)) {
            fstr =extensions[i].filetype;
            break;
        }
    }
    if(fstr == 0){
        logger(FORBIDDEN,"file extension type not supported",buffer,fd);
    }
    if(( file_fd = open(&buffer[5],O_RDONLY)) == -1) {  /* open the file for reading */
        logger(NOTFOUND, "failed to open file",&buffer[5],fd);
        goto endRequest;
    }
    logger(LOG,"SEND",&buffer[5],hit);
    len = (long)lseek(file_fd, (off_t)0, SEEK_END); /* lseek to the file end to find the length */
          (void)lseek(file_fd, (off_t)0, SEEK_SET); /* lseek back to the file start ready for reading */
          /* print out the response line, stock headers, and a blank line at the end. */
          (void)sprintf(buffer, HDRS_OK, VERSION, len, fstr); 
    logger(LOG,"Header",buffer,hit);
    dummy = write(fd,buffer,strlen(buffer));
    
  
    /* send file in 8KB block - last block may be smaller */
    while ( (ret = read(file_fd, buffer, BUFSIZE)) > 0 ) {
        dummy = write(fd,buffer,ret);
    }
    endRequest:
    sleep(1);   /* allow socket to drain before signalling the socket is closed */
    close(fd);
}

void tpool_init(tpool_t *tm, size_t num_threads, size_t buf_size, void *(*start_routine) (void *))
{
    pthread_t  thread;
    size_t     i;
    pthread_mutex_init(&tm->work_mutex, NULL);
    pthread_cond_init(&(tm->p_cond), NULL);
    pthread_cond_init(&(tm->c_cond), NULL);
    // initialize buffer to empty condition  
	tm->head = tm->tail = tm->capacity = 0;
	tm->buf_capacity = buf_size;
	tm->jobs = calloc (buf_size * sizeof(job_t) , 1); //this is dumb but it makes the compiler happy
    for (i=0; i<num_threads; i++) {
        pthread_create(&thread, NULL, *start_routine, (void *) (i + 1)); //maybe problem, (void * ) i + 1 is bad pointer arithmetic
        pthread_detach(thread); // make non-joinable
    }
}
job_t dequeue(tpool_t *pool){
	job_t job = pool->jobs[pool->tail];
	pool->tail = (pool->tail + 1) % pool->buf_capacity;
	pool->capacity--;
	return job;
}
int enqueue(tpool_t *pool, job_t *job){//returns int in case i want to add error return values
	pool->jobs[pool->head++] = *job;
	pool->head = (pool->head) % pool->buf_capacity;
	pool->capacity++;
	return 0;
}

static void *tpool_worker(void* arg)
{
    tpool_t *tm = the_pool; //removed a & from &the_pool for warning message
	// int my_id = (int) arg;

    while (1) {
       job_t job;
       pthread_mutex_lock(&(tm->work_mutex));
        while (the_pool->capacity == 0){ //if there are no jobs to do
            pthread_cond_wait(&(tm->c_cond), &(tm->work_mutex));
		}
		int queueWasFull = tm->capacity == tm->buf_capacity;
        job = dequeue(the_pool);
        pthread_mutex_unlock(&(tm->work_mutex));
		web(job.job_fd , 0);
		pthread_mutex_lock(&(tm->work_mutex));
        if (queueWasFull && (tm->capacity < tm->buf_capacity))//if we should be waking up the producer
            pthread_cond_signal(&(tm->p_cond));
        pthread_mutex_unlock(&(tm->work_mutex));
    }
    return NULL;
}
bool tpool_add_work(tpool_t *tm, job_t job)
{
    pthread_mutex_lock(&(tm->work_mutex));
	while (tm->capacity == tm->buf_capacity) //while full, wait    
		pthread_cond_wait(&(tm->p_cond), &(tm->work_mutex));
	job_t *job_p = &job;
    enqueue(tm, job_p);
    // Wake the Keystone Cops!! (improve this eventually)
    pthread_cond_broadcast(&(tm->c_cond));
    pthread_mutex_unlock(&(tm->work_mutex));
    return true;
}

int main(int argc, char **argv)
{
	int i, port, listenfd, socketfd, hit;
	socklen_t length;
	static struct sockaddr_in cli_addr; /* static = initialised to zeros */
	static struct sockaddr_in serv_addr; /* static = initialised to zeros */
	if( argc < 3 || !strcmp(argv[1], "-?") ) {
		(void)printf("USAGE: %s <port-number> <top-directory>\t\tversion %d\n\n"
	"\tnweb is a small and very safe mini web server\n"
	"\tnweb only servers out file/web pages with extensions named below\n"
	"\t and only from the named directory or its sub-directories.\n"
	"\tThere is no fancy features = safe and secure.\n\n"
	"\tExample: nweb 8181 /home/nwebdir &\n\n"
	"\tOnly Supports:", argv[0], VERSION);
		for(i=0;extensions[i].ext != 0;i++)
			(void)printf(" %s",extensions[i].ext);

		(void)printf("\n\tNot Supported: URLs including \"..\", Java, Javascript, CGI\n"
	"\tNot Supported: directories / /etc /bin /lib /tmp /usr /dev /sbin \n"
	"\tNo warranty given or implied\n\tNigel Griffiths nag@uk.ibm.com\n"  );
		exit(0);
	}

	if( !strncmp(argv[2],"/"   ,2 ) || !strncmp(argv[2],"/etc", 5 ) ||
	    !strncmp(argv[2],"/bin",5 ) || !strncmp(argv[2],"/lib", 5 ) ||
	    !strncmp(argv[2],"/tmp",5 ) || !strncmp(argv[2],"/usr", 5 ) ||
	    !strncmp(argv[2],"/dev",5 ) || !strncmp(argv[2],"/sbin",6) ){
		(void)printf("ERROR: Bad top directory %s, see nweb -?\n",argv[2]);
		exit(3);
	}
	if(chdir(argv[2]) == -1){ 
		(void)printf("ERROR: Can't Change to directory %s\n",argv[2]);
		exit(4);
	}
	
	logger(LOG,"nweb starting",argv[1],getpid());
	/* setup the network socket */
	if((listenfd = socket(AF_INET, SOCK_STREAM,0)) <0){
		logger(ERROR, "system call","socket",0);
	}
	port = atoi(argv[1]);
	if(port < 1025 || port >65000) {
		logger(ERROR,"Invalid port number (try 1025->65000)",argv[1],0);
	}
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(port);
	if(bind(listenfd, (struct sockaddr *)&serv_addr,sizeof(serv_addr)) <0){
		logger(ERROR,"system call","bind",0);
	}
	if( listen(listenfd,64) <0) {
		logger(ERROR,"system call","listen",0);
	}
	//3 == num threads
	//4 == size buffer
	int numThreads = atoi(argv[3]);
	int bufSize = atoi(argv[4]);
	//start with a pool of 1 thread, use the consumer code to test that even that works
	// tpool_t *tm, size_t num_threads, size_t buf_size, worker_fn *worker
	tpool_init(the_pool, numThreads, bufSize, tpool_worker);
	for(hit=1; ;hit++) {
		length = sizeof(cli_addr);
		if((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0) {
			logger(ERROR,"system call","accept",0);
		}
		job_t j;
		job_t *job = &j;
		job->job_fd = socketfd;
		job->job_id = hit;

		tpool_add_work(the_pool, *job);
	}
}
