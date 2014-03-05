/*
 * print_thread.c
 *
 *  Created on: Feb 18, 2014
 *      Author: nathan
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/types.h>

#include "config.h"
#include "print_thread.h"

typedef struct message_node
{
	struct message_node* next;
	String message;
} MessageNode;

//ALL HAIL THE GLOBAL MESSAGE QUEUE
//Push to the front, pop off the back
static struct {
	MessageNode* front;
	MessageNode* back;
	bool shutdown;

	pthread_mutex_t mutex;
	pthread_cond_t print_signal; //When the printer is waiting for a message

	//ALL HAIL THE GLOBAL PRINT THREAD
	pthread_t printer; //The actual printer thread
} queue;

static inline void lock_queue()
{
	pthread_mutex_lock(&queue.mutex);
}

static inline void unlock_queue()
{
	pthread_mutex_unlock(&queue.mutex);
}

static inline void printer_wait()
{
	pthread_cond_wait(&queue.print_signal, &queue.mutex);
}

static inline void signal_printer()
{
	pthread_cond_signal(&queue.print_signal);
}

//Pop the next message off the queue.
static inline bool get_next(String* message)
{
	//Set to true if a message is retrieved. False means shutdown.
	bool message_flag = false;

	lock_queue();

	//while there are no new messages and messages are incoming
	while(!queue.back && !queue.shutdown)
		printer_wait();

	//Either there is a message or we were shutdown (or both)

	//If message
	if(queue.back)
	{
		//Get the message
		MessageNode* node = queue.back;

		//Update the back ptr
		queue.back = node->next;

		//If nessesary, clear the front ptr
		if(queue.front == node)
			queue.front = 0;

		//Get the message text
		*message = node->message;

		//Free message
		free(node);

		//Set the message flag
		message_flag = 1;
	}

	unlock_queue();

	return message_flag;
}

//Add a message to the queue
static inline void submit_message(String message)
{
	lock_queue();

	//If the queue isn't shutdown
	if(!queue.shutdown)
	{
		if(PRINT_TID)
		{
			//ASSUMES pthread_t IS AN UNSIGNED LONG
			pthread_t thread_id = pthread_self();
			String updated_message = es_printf("[thread: %u] %.*s",
					thread_id,
					ES_STRINGPRINT(&message));
			es_free(&message);
			message = updated_message;
		}
		//Create a new node
		MessageNode* new_node = malloc(sizeof(MessageNode));
		new_node->message = message;
		new_node->next = 0;

		//add the message to the front of the queue
		if(queue.front)
			queue.front->next = new_node;

		//set the message to the back of the queue, if necessary
		if(!queue.back)
			queue.back = new_node;

		//set the message to the front of the queue
		queue.front = new_node;

		//Signal the printer
		signal_printer();

		//Unlock;
		unlock_queue();
	}
	else
	{
		//No reason to hold lock while freeing
		unlock_queue();
		es_free(&message);
	}

}

static inline void shutdown_queue()
{
	lock_queue();
	queue.shutdown = true;
	signal_printer();
	unlock_queue();
}

void* print_thread(void* arg)
{
	String message;
	while(get_next(&message))
	{
		printf("%.*s\n", (int)ES_SIZESTRCNST(&message));
		es_free(&message);
	}
	return 0;
}

int _print_thread_status = -1;

__attribute__((constructor))
void begin_print_thread()
{
	if(DEBUG_PRINT) puts("Launching print thread");
	//This is implicit for static variables, but better to be explicit
	queue.front = queue.back = 0;
	queue.shutdown = 0;

	//Initialize sync primitives
	pthread_mutex_init(&queue.mutex, 0);
	pthread_cond_init(&queue.print_signal, 0);

	//Launch thread
	_print_thread_status = pthread_create(&queue.printer, 0, &print_thread, 0);
}

__attribute__((destructor))
void end_print_thread()
{
	if(DEBUG_PRINT) puts("Stopping print thread");
	/*
	 * Shutdown the queue. No more messages can be submitted. Remaining
	 * messages will be printed.
	 */
	shutdown_queue();

	//Wait for the thread
	if(_print_thread_status == 0) pthread_join(queue.printer, 0);

	//Clear sync primitives
	pthread_mutex_destroy(&queue.mutex);
	pthread_cond_destroy(&queue.print_signal);
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC INTERFACE
///////////////////////////////////////////////////////////////////////////////

void submit_print(String message)
{
	submit_message(message);
}

void submit_debug(String message)
{
	if(DEBUG_PRINT)
		submit_message(message);
	else
		es_free(&message);
}

int print_thread_status()
{
	return _print_thread_status;
}
