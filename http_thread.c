/*
 * http_thread.c
 *
 *  Created on: Mar 3, 2014
 *      Author: nathan
 */

#include <stdlib.h>
#include <unistd.h>

#include "print_thread.h"
#include "http.h"

typedef struct
{
	int connection_fd;
} HTTP_Data;

typedef enum { unknown, keep_alive, close_connection } ConnState;

/*
 * Some potential, unhandled bugs:
 * - If the server responds with 405 Method not allowed, and provides an Allow:
 *   header listing the allowed methods, those methods may not contain all of
 *   GET, HEAD, and POST. Currently, the proxy doesn't filter this.
 *
 * Response codes I don't want to deal with:
 * - HTTP 101: switch protocols
 * - Pretty much any redirection, though browsers should handle these anyway
 */
void handle_error(int code, const char* text)
{
	HTTP_Message message = empty_message;
	message.response.status = code
}
void* http_thread(void* ptr)
{
	HTTP_Data* data = ptr;
	int client_fd = ((HTTP_Data*)(ptr))->connection_fd;
	int server_fd = 0; //Assume that the actual fd won't be 0.
	free(ptr);

	ConnState connection_state = unknown;

	while(connection_state != close_connection)
	{
		#define RETURN() { clear_request(&request); clear_response(&response); \
			close(client_fd); if(server_fd) close(server_fd); return 0; }

		#define ERROR_CASE(ERROR_CODE, CODE, MSG) case ERROR_CODE: \
			handle_error(CODE, MSG); RETURN() break;

		HTTP_Message request = empty_message;
		HTTP_Message response = empty_message;

		submit_debug("Reading Request");

		switch(read_request_line(&request, client_fd))
		{
		ERROR_CASE(too_long, "Error: Request line too long")
		ERROR_CASE(connection_error, "Error: Connection error")
		ERROR_CASE(malformed_line, "Error: Request line malformed")
		ERROR_CASE(bad_method, )
		}


	}
}
