#include <mqueue.h>
#include "gfserver.h"
#include "cache-student.h"

#define BUFSIZE (822)

/*
 * Replace with your implementation
*/
ssize_t handle_with_cache(gfcontext_t *ctx, const char *path, void* arg){
	int queue_result;
	size_t bytes_transferred =0;
	size_t bytes_sent=0;
	cache_req_data *proxy_queue_data;
	pthread_mutex_lock(&pq_mtx);
	while(steque_isempty(proxy_queue)){
		pthread_cond_wait(&pq_cond,&pq_mtx);
	}
	shared_data *shared_data_handler =(shared_data*)steque_pop(proxy_queue);
	pthread_mutex_unlock(&pq_mtx);
	pthread_cond_broadcast(&pq_cond);

	if (!shared_data_handler){
		perror(" couldnot pop shared data from proxy queue.");
	}

	sem_init(&shared_data_handler->sem_filelen,1,0);		
	sem_init(&shared_data_handler->sem_proxy,1,0);
	sem_init(&shared_data_handler->sem_cache,1,1);

	// message queue
	strcpy(proxy_queue_data->shm_id, shared_data_handler->shm_id);
	proxy_queue_data->seg_size = shared_data_handler->seg_size;
	mqd_t request_queue = mq_open(MESSAGE_QUEUE_NAME, O_RDWR);
	queue_result = mq_send(request_queue,(char*)&proxy_queue_data,sizeof(proxy_queue_data),0); 
	if (queue_result == -1){
		perror(" message result is not send ");
	}

	sem_wait(&shared_data_handler->sem_filelen);
	// free(proxy_queue_data);

	// get data from the queue 
	if(shared_data_handler->status_code == 404){
		bytes_transferred = gfs_sendheader(ctx,GF_FILE_NOT_FOUND,0);
	}

	else if (shared_data_handler->status_code == 200){
		gfs_sendheader(ctx,GF_OK,shared_data_handler->file_size);
		while(bytes_transferred < shared_data_handler->file_size){
			sem_wait(&shared_data_handler->sem_proxy);
			bytes_sent = gfs_send(ctx,shared_data_handler->data,shared_data_handler->read_len);
			
			if(bytes_sent <=0){
				perror("Error sending data to client\n");
				break;
			}
			bytes_transferred +=bytes_sent;
			sem_post(&shared_data_handler->sem_cache);
		}
	}

	shared_data_handler->read_len=0;
	shared_data_handler->file_size=0;
	//return struct to the queue
	pthread_mutex_lock(&pq_mtx);
	steque_enqueue(proxy_queue, shared_data_handler);
	pthread_mutex_unlock(&pq_mtx);
	pthread_cond_broadcast(&pq_cond);
	return bytes_transferred;
}


