/*
 *  To be used by students
 */
 #ifndef __CACHE_STUDENT_H__822
 #define __CACHE_STUDENT_H__822

 #include "steque.h"
 #include <semaphore.h>
 #include "gfserver.h"
 #include <stdbool.h>

#define MESSAGE_QUEUE_NAME "/message_queue"
#define MAX_SHMNAME_LEN 32
#define MAX_PATH_LEN 256
#define MAX_MSG_NUM 10
#define MAX_MSG_SIZE 1024

extern pthread_mutex_t pq_mtx;
extern pthread_cond_t pq_cond;
extern steque_t *proxy_queue;

typedef struct shared_data
{
    char name[10];
    int segment_size;
    void *data;
    char shm_id[10];
    int seg_size;
    sem_t sem_proxy;
    sem_t sem_cache;
    sem_t sem_filelen;
    int file_size;
    int read_len;
    int status_code;

}shared_data;


typedef struct cache_req_data
{
    char path[MAX_REQUEST_LEN];
    char shm_id[10];
    size_t seg_size;
} cache_req_data;


typedef struct  {
  pthread_t pthread;
  bool is_live;
}thread_data;


typedef struct {
    char filePath[MAX_PATH_LEN];
    char shmName[MAX_SHMNAME_LEN];
    size_t nSegments;
    size_t segmentSize;
}MSQRequest_t;

void * worker_func(void *arg);
#endif // __CACHE_STUDENT_H__822