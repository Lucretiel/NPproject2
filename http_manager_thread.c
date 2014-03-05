/*
 * http_manager_thread.c
 *
 *  Created on: Mar 3, 2014
 *      Author: nathan
 */

#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdio.h>

#include "http_manager_thread.h"
#include "http_worker_thread.h"
#include "config.h"

/*
 * If this looks like it's copy-pasted from the print_thread.c global queue
 * implementation, that's because it is. My kingdom for some fucking
 * <templates>.
 */

typedef struct thread_list_node
{
	struct thread_list_node* next;
	pthread_t thread;
} ThreadListNode;

//Push to end, pop off begin
static struct
{
	ThreadListNode* begin;
	ThreadListNode* end;

	pthread_mutex_t lock;
	pthread_cond_t signal;

	pthread_t manager;
	bool shutdown;
} manager_data;

static inline void manager_lock()
{ pthread_mutex_lock(&manager_data.lock); }

static inline void manager_unlock()
{ pthread_mutex_unlock(&manager_data.lock); }

static inline void manager_wait()
{ pthread_cond_wait(&manager_data.signal, &manager_data.lock); }

static inline void manager_signal()
{ pthread_cond_signal(&manager_data.signal); }

//Waits for a thread to join the queue
static inline bool wait_for_thread(pthread_t* thread)
{
	//Set to true if a message is retrieved. False means shutdown.
	bool message_flag = false;

	manager_lock();

	//while there are no new messages and messages are incoming
	while(!manager_data.begin && !manager_data.shutdown)
		manager_wait();

	//Either there is a message or we were shutdown (or both)

	//If message
	if(manager_data.begin)
	{
		//Get the message
		ThreadListNode* node = manager_data.begin;

		//Update the begin ptr
		manager_data.begin = node->next;

		//If nessesary, clear the front ptr
		if(manager_data.end == node)
			manager_data.end = 0;

		//Get the message text
		*thread = node->thread;

		//Free message
		free(node);

		//Set the message flag
		message_flag = 1;
	}

	manager_unlock();

	return message_flag;
}

static inline void stop_manager()
{
	manager_lock();
	manager_data.shutdown = true;
	manager_signal();
	manager_unlock();
}

static inline void add_thread(pthread_t* thread)
{
	manager_lock();

	//If the manager isn't shutdown
	if(!manager_data.shutdown)
	{
		//Create a new node
		ThreadListNode* new_node = malloc(sizeof(ThreadListNode));
		new_node->thread = *thread;
		new_node->next = 0;

		//add the message to the end of the queue
		if(manager_data.end)
			manager_data.end->next = new_node;

		//set the message to the beginning of the queue, if necessary
		if(!manager_data.begin)
			manager_data.begin = new_node;

		//set the message to the front of the queue
		manager_data.end = new_node;

		//Signal the manager
		manager_signal();

		//Unlock;
		manager_unlock();
	}

	manager_unlock();
}


static void* http_manager(void* arg)
{
	pthread_t thread;
	while(wait_for_thread(&thread))
		pthread_join(thread, 0);
	return 0;
}

static int _manager_status = -1;

__attribute__((constructor))
void begin_http_manager()
{
	if(DEBUG_PRINT) puts("Launching HTTP thread manager");
	manager_data.begin = manager_data.end = 0;
	manager_data.shutdown = false;

	pthread_mutex_init(&manager_data.lock, 0);
	pthread_cond_init(&manager_data.signal, 0);

	_manager_status = pthread_create(&manager_data.manager, 0, http_manager, 0);
}

__attribute__((destructor))
void end_http_manager()
{
	if(DEBUG_PRINT) puts("Stopping HTTP thread manager");
	stop_manager();

	if(_manager_status) pthread_join(manager_data.manager, 0);

	pthread_mutex_destroy(&manager_data.lock);
	pthread_cond_destroy(&manager_data.signal);
}

int manager_status()
{
	return _manager_status;
}

int handle_connection(int fd, struct sockaddr_in* addr)
{
	HTTP_Data* data = malloc(sizeof(HTTP_Data));
	data->connection_fd = fd;
	data->connection_sockaddr = *addr;

	pthread_t thread;

	int result = pthread_create(&thread, 0, http_worker_thread, data);
	if(result)
	{
		free(data);
	}
	else
	{
		add_thread(&thread);
	}

	return result;
}
