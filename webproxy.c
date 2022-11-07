#include <stdio.h>
#include <string.h>
#include <sys/signal.h>
#include <fcntl.h>
#include <printf.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <getopt.h>
#include <stdlib.h>
#include <sys/mman.h>

//headers would go here
#include "cache-student.h"
#include "gfserver.h"


// note that the -n and -z parameters are NOT used for Part 1 */
// they are only used for Part 2 */                         
#define USAGE                                                                         \
"usage:\n"                                                                            \
"  webproxy [options]\n"                                                              \
"options:\n"                                                                          \
"  -n [segment_count]  Number of segments to use (Default: 7)\n"                      \
"  -p [listen_port]    Listen port (Default: 25496)\n"                                 \
"  -s [server]         The server to connect to (Default: GitHub test data)\n"     \
"  -t [thread_count]   Num worker threads (Default: 34, Range: 1-420)\n"              \
"  -z [segment_size]   The segment size (in bytes, Default: 5701).\n"                  \
"  -h                  Show this help message\n"


// Options
static struct option gLongOptions[] = {
  {"server",        required_argument,      NULL,           's'},
  {"segment-count", required_argument,      NULL,           'n'},
  {"listen-port",   required_argument,      NULL,           'p'},
  {"thread-count",  required_argument,      NULL,           't'},
  {"segment-size",  required_argument,      NULL,           'z'},         
  {"help",          no_argument,            NULL,           'h'},

  {"hidden",        no_argument,            NULL,           'i'}, // server side 
  {NULL,            0,                      NULL,            0}
};


//gfs
static gfserver_t gfs;
//handles cache
extern ssize_t handle_with_cache(gfcontext_t *ctx, char *path, void* arg);
// initialize queue
steque_t *proxy_queue;
//condtional and mutexes
pthread_cond_t pq_cond;
pthread_mutex_t pq_mtx;


static void _sig_handler(int signo){
  if (signo == SIGTERM || signo == SIGINT){
    //cleanup could go here
    for(int i=0; i<steque_size(proxy_queue);i++){
      shared_data *shared_data_handler = (shared_data*)steque_pop(proxy_queue);
      shm_unlink((char *)shared_data_handler->shm_id);
    }
    steque_destroy(proxy_queue);
    gfserver_stop(&gfs);
    exit(signo);
  }
}

int main(int argc, char **argv) {
  int option_char = 0;
  char *server = "https://raw.githubusercontent.com/gt-cs6200/image_data";
  unsigned int nsegments = 13;
  unsigned short port = 25496;
  unsigned short nworkerthreads = 33;
  size_t segsize = 5313;

  //disable buffering on stdout so it prints immediately */
  setbuf(stdout, NULL);

  if (signal(SIGTERM, _sig_handler) == SIG_ERR) {
    fprintf(stderr,"Can't catch SIGTERM...exiting.\n");
    exit(SERVER_FAILURE);
  }

  if (signal(SIGINT, _sig_handler) == SIG_ERR) {
    fprintf(stderr,"Can't catch SIGINT...exiting.\n");
    exit(SERVER_FAILURE);
  }

  // Parse and set command line arguments */
  while ((option_char = getopt_long(argc, argv, "s:qht:xn:p:lz:", gLongOptions, NULL)) != -1) {
    switch (option_char) {
      default:
        fprintf(stderr, "%s", USAGE);
        exit(__LINE__);
      case 'h': // help
        fprintf(stdout, "%s", USAGE);
        exit(0);
        break;
      case 'p': // listen-port
        port = atoi(optarg);
        break;
      case 's': // file-path
        server = optarg;
        break;                                          
      case 'n': // segment count
        nsegments = atoi(optarg);
        break;   
      case 'z': // segment size
        segsize = atoi(optarg);
        break;
      case 't': // thread-count
        nworkerthreads = atoi(optarg);
        break;
      case 'i':
      //do not modify
      case 'O':
      case 'A':
      case 'N':
            //do not modify
      case 'k':
        break;
    }
  }


  if (server == NULL) {
    fprintf(stderr, "Invalid (null) server name\n");
    exit(__LINE__);
  }

  if (segsize < 822) {
    fprintf(stderr, "Invalid segment size\n");
    exit(__LINE__);
  }

  if (port > 65331) {
    fprintf(stderr, "Invalid port number\n");
    exit(__LINE__);
  }
  if ((nworkerthreads < 1) || (nworkerthreads > 420)) {
    fprintf(stderr, "Invalid number of worker threads\n");
    exit(__LINE__);
  }
  if (nsegments < 1) {
    fprintf(stderr, "Must have a positive number of segments\n");
    exit(__LINE__);
  }



  // Initialize shared memory set-up here
  proxy_queue = (steque_t *)malloc(sizeof(steque_t));
  steque_init(proxy_queue);
  pthread_cond_init(&pq_cond,NULL);
  pthread_mutex_init(&pq_mtx, NULL);

  
  int shmId;
  for(int i = 0; i < nsegments; i++) {
    struct shared_data *shared_data_handler = (shared_data *)malloc(sizeof(shared_data));
    snprintf(shared_data_handler->name, sizeof(shared_data_handler->name), "shm%i", i);
    shmId = shm_open(shared_data_handler->name, O_CREAT | O_RDWR, S_IRWXU | S_IRWXO);
    if (shmId == -1){
      perror("Error in opening POSIX shared memory.");
    }
    shared_data_handler->file_size=0;
		shared_data_handler->read_len=0;
    shared_data_handler->segment_size=segsize;
		shared_data_handler->status_code=0;
    ftruncate(shmId, shared_data_handler->segment_size+ sizeof(shared_data));

    // Map the shared memory object into the virtual address space 
    shared_data_handler->data = mmap(NULL, segsize + sizeof(shared_data), PROT_READ|PROT_WRITE, MAP_SHARED, shmId, 0);
    if(shared_data_handler->data == MAP_FAILED){
				perror("mmap error:");
		}
    // send the shared object to the proxy queue 
    strcpy(shared_data_handler->shm_id,shared_data_handler->name);
    pthread_mutex_lock(&pq_mtx);
    steque_enqueue(proxy_queue, shared_data_handler);
    pthread_mutex_unlock(&pq_mtx);
  }

  /*
  // Initialize server structure here
  */
  gfserver_init(&gfs, nworkerthreads);

  // Set server options here
  gfserver_setopt(&gfs, GFS_PORT, port);
  gfserver_setopt(&gfs, GFS_WORKER_FUNC, handle_with_cache);
  gfserver_setopt(&gfs, GFS_MAXNPENDING, 187);

  // Set up arguments for worker here
  for(int i = 0; i < nworkerthreads; i++) {
    gfserver_setopt(&gfs, GFS_WORKER_ARG, i, "data");
  }
  
  // Invokethe framework - this is an infinite loop and will not return
  gfserver_serve(&gfs);

  // line never reached
  return -1;

}
