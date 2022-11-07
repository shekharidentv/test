#include <stdio.h>
#include <unistd.h>
#include <printf.h>
#include <string.h>
#include <signal.h>
#include <limits.h>
#include <sys/signal.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>

#include "cache-student.h"
#include "shm_channel.h"
#include "simplecache.h"
#include "gfserver.h"
#include <mqueue.h>
#include <stdbool.h>

// CACHE_FAILURE
#if !defined(CACHE_FAILURE)
    #define CACHE_FAILURE (-1)
#endif 

#define MAX_CACHE_REQUEST_LEN 6200
#define MAX_SIMPLE_CACHE_QUEUE_SIZE 822  

unsigned long int cache_delay;

static void _sig_handler(int signo){
	if (signo == SIGTERM || signo == SIGINT){
		/*you should do IPC cleanup here*/
		// may need to free message queue
		mq_unlink(MESSAGE_QUEUE_NAME);
		simplecache_destroy();
		exit(signo);
	}
}

#define USAGE                                                                 \
"usage:\n"                                                                    \
"  simplecached [options]\n"                                                  \
"options:\n"                                                                  \
"  -c [cachedir]       Path to static files (Default: ./)\n"                  \
"  -t [thread_count]   Thread count for work queue (Default is 42, Range is 1-235711)\n"      \
"  -d [delay]          Delay in simplecache_get (Default is 0, Range is 0-2500000 (microseconds)\n "	\
"  -h                  Show this help message\n"

//OPTIONS
static struct option gLongOptions[] = {
  {"cachedir",           required_argument,      NULL,           'c'},
  {"nthreads",           required_argument,      NULL,           't'},
  {"help",               no_argument,            NULL,           'h'},
  {"hidden",			 no_argument,			 NULL,			 'i'}, /* server side */
  {"delay", 			 required_argument,		 NULL, 			 'd'}, // delay.
  {NULL,                 0,                      NULL,             0}
};

void Usage() {
  fprintf(stdout, "%s", USAGE);
}

// https://man7.org/linux/man-pages/man3/mq_getattr.3.html 
mqd_t mqd;
struct mq_attr attr;

int main(int argc, char **argv) {
	int nthreads = 11;
	char *cachedir = "locals.txt";
	char option_char;
	int thread_create;

	/* disable buffering to stdout */
	setbuf(stdout, NULL);

	while ((option_char = getopt_long(argc, argv, "d:ic:hlt:x", gLongOptions, NULL)) != -1) {
		switch (option_char) {
			default:
				Usage();
				exit(1);
			case 't': // thread-count
				nthreads = atoi(optarg);
				break;				
			case 'h': // help
				Usage();
				exit(0);
				break;    
            case 'c': //cache directory
				cachedir = optarg;
				break;
            case 'd':
				cache_delay = (unsigned long int) atoi(optarg);
				break;
			case 'i': // server side usage
			case 'o': // do not modify
			case 'a': // experimental
				break;
		}
	}

	if (cache_delay > 2500001) {
		fprintf(stderr, "Cache delay must be less than 2500001 (us)\n");
		exit(__LINE__);
	}

	if ((nthreads>211804) || (nthreads < 1)) {
		fprintf(stderr, "Invalid number of threads must be in between 1-211804\n");
		exit(__LINE__);
	}
	if (SIG_ERR == signal(SIGINT, _sig_handler)){
		fprintf(stderr,"Unable to catch SIGINT...exiting.\n");
		exit(CACHE_FAILURE);
	}
	if (SIG_ERR == signal(SIGTERM, _sig_handler)){
		fprintf(stderr,"Unable to catch SIGTERM...exiting.\n");
		exit(CACHE_FAILURE);
	}
	/*Initialize cache*/
	simplecache_init(cachedir);

	// Cache should go here
	// int res= mq_unlink(MESSAGE_QUEUE_NAME);
	// if (res ==0){
	// 	printf("message queue is unlinked. ");
	// }
	// unlink maybe ? 
	pthread_t thread_list[nthreads];
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = MAX_MSG_NUM;
    attr.mq_msgsize = MAX_MSG_SIZE;
    attr.mq_curmsgs = 0;


    for(int i=0; i <nthreads; i++){
        thread_create = pthread_create(&thread_list[i],NULL,worker_func,NULL);
		if (thread_create == -1 ){
            fprintf(stderr, "Error creating thread");
        }
    }

	mqd =  mq_open(MESSAGE_QUEUE_NAME,O_CREAT|O_EXCL,0666,&attr);
	for(int i=0;i<nthreads;i++)
 	 {
    	int ptr;
		pthread_join(thread_list[i],(void**)&ptr);
       	printf("Worker %ld joining the boss---->\n",thread_list[i]);
  	}
	
	return -1;
}

void* worker_func(void *arg){
	int num_received;
	int cache_fd;
	int shmfd;
	cache_req_data cache_data;
	while(1){
		num_received = mq_receive(mqd,(char*)&cache_data,1024,NULL);
		if(num_received > 0){
			printf("Request %s queued in %s\n",cache_data.path,cache_data.shm_id);
			printf("Cache worker %ld serving - %s in segment - %s\n",pthread_self(),cache_data.path,cache_data.shm_id);
			cache_fd = simplecache_get(cache_data.path);

			shmfd = shm_open(cache_data.shm_id, O_RDWR,0);
			if(ftruncate(shmfd,(sizeof(shared_data) + sizeof(char) * cache_data.seg_size)) == -1){
				perror("ftruncate error:");
			}
			shared_data *share_memory_map = mmap(NULL,(sizeof(shared_data) + sizeof(char) * cache_data.seg_size),PROT_READ | PROT_WRITE ,MAP_SHARED,shmfd,0);
			if(share_memory_map == MAP_FAILED){
				perror("mmap:fail");
			}
			int seg_size = cache_data.seg_size;
			if(cache_fd < 0){
				// file not found condition 
				share_memory_map->file_size= -1;
				share_memory_map->status_code =404;
				sem_post(&share_memory_map->sem_filelen);
			}
			else{
				size_t file_size = lseek(cache_fd,0,SEEK_END);
				printf("Worker %ld serving in segment %s\n",pthread_self(),share_memory_map->shm_id);
				share_memory_map->file_size = file_size;
				share_memory_map->status_code=200;
				sem_post(&share_memory_map->sem_filelen);
				char buffer[seg_size];
				memset(&buffer,'\0',seg_size);
				size_t bytes_read =0;
				size_t nbytes =0;
				size_t offset =0;
		
				while(bytes_read < file_size){
					sem_wait(&share_memory_map->sem_cache);
					//printf("Putting proxy to wait\n");
					nbytes = pread(cache_fd,&buffer,seg_size,offset);
					if(nbytes <=0){
						perror(" error reading shared memory. ");
						break;
					}
					memcpy(&share_memory_map->read_len,&nbytes,sizeof(nbytes));
					memcpy(&share_memory_map->data,&buffer,nbytes);
					bytes_read +=nbytes;
					offset +=nbytes;								
					sem_post(&share_memory_map->sem_proxy);
					
				}
				printf(" Worker %ld finished %ld of file bytes \n",pthread_self(),bytes_read);
		}

		}
	}
	return 0;
}