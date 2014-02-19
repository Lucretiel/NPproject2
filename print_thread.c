/*
 * print_thread.c
 *
 *  Created on: Feb 18, 2014
 *      Author: nathan
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "print_thread.h"

typedef struct message
{
	struct message* next;
	char* text;
} Message;

//Push to the front, pop off the back
static struct {
	Message* front;
	Message* back;

	pthread_mutex_t mutex;
	pthread_cond_t print_signal; //When the printer is waiting for a message

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
char* get_next()
{
	char* result = 0;
	lock_queue();
	//while there are no new messages and messages are incoming
	while((!queue.back->next) && (queue.front))
		printer_wait();

	//Either there is a message or we were shutdown (or both)

	//If message
	if(queue.back->next)
	{
		Message* next_msg = queue.back->next;
		free(queue.back);
		queue.back = next_msg;
		result = next_msg->text;
		next_msg->text = 0;
		unlock_queue();
	}

	unlock_queue();
	return result;
}

//Add a message to the queue
static inline void submit_message(const char* message)
{
	Message* new_message = calloc(1, sizeof(Message));
	size_t message_length = strlen(message);
	new_message->text = calloc(message_length + 1, sizeof(char));
	memcpy(new_message->text, message, message_length);

	lock_queue();

	if(queue.front)
	{
		queue.front->next = new_message;
		queue.front = new_message;
		signal_printer();
		unlock_queue();
	}
	else
	{
		unlock_queue();
		free(new_message->text);
		free(new_message);
	}
}

static inline void shutdown_queue()
{
	lock_queue();
	queue.front = 0;
	signal_printer();
	unlock_queue();
}

void* print_thread(void* arg)
{
	for(char* message = get_next(); message; message = get_next())
	{
		fputs(message, stdout);
		free(message);
	}
	return 0;
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC INTERFACE
///////////////////////////////////////////////////////////////////////////////

int begin_print_thread()
{
	//Initialize message queues
	queue.front = queue.back = calloc(1, sizeof(Message));

	//Initialize sync primitives
	pthread_mutex_init(&queue.mutex, 0);
	pthread_cond_init(&queue.print_signal, 0);

	//Launch thread
	return pthread_create(&queue.printer, 0, &print_thread, 0);
}

void end_print_thread()
{
	/*
	 * Shutdown the queue. No more messages can be submitted. Remaining
	 * messages will be printed.
	 */
	shutdown_queue();

	//Wait for the thread
	pthread_join(queue.printer, 0);

	//Wipe remaining messages
	Message* message = queue.front;
	while(message)
	{
		Message* to_clear = message;
		message = message->next;
		free(to_clear->text);
		free(to_clear);
	}
	//Wipe queue
	queue.front = 0;
	queue.back = 0;

	//Clear sync primitives
	pthread_mutex_destroy(&queue.mutex);
	pthread_cond_destroy(&queue.print_signal);
}

void submit_print(const char* message)
{
	submit_message(message);
}
