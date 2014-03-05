/*
 * http_thread.c
 *
 *  Created on: Mar 3, 2014
 *      Author: nathan
 */

#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <netdb.h>

#include "http_worker_thread.h"
#include "print_thread.h"
#include "stat_tracking.h"
#include "filters.h"
#include "http.h"

typedef enum { cs_unknown, cs_persist, cs_close } ConnState;

/*
 * Some potential, unhandled bugs:
 * - If the server responds with 405 Method not allowed, and provides an Allow:
 *   header listing the allowed methods, those methods may not contain all of
 *   GET, HEAD, and POST. Currently, the proxy doesn't filter this.
 * - Same with the reverse. The proxy currently adds Allow: GET, HEAD, POST to
 *   proxy-generated error responses
 * - We don't currently acknowledge that the client recieved the error before
 *   closing the socket, in the event of an error; this "may erase the client's
 *   unacknowledged input buffers before they can be read and interpreted by
 *   the HTTP application."
 *   http://www.w3.org/Protocols/rfc2616/rfc2616-sec10.html
 *
 * Response codes and other things I don't want to deal with:
 * - HTTP 101: switch protocols
 * - HTTP 100 and Expect: 100- continue
 * - Pretty much any redirection, though browsers should handle these anyway
 * - It would make much more sense to spawn a second thread after the first
 *   request domain is parsed, which would be resposible for forwarding
 *   response(s) back to the client. This would allow cases where multiple
 *   responses are sent (206 Partial Content) or in-transmission responses
 *   (100 Continue) to be handled seamlessly. Don't want to bother.
 * - Any case where interim responses might be sense. We're doing 1 request ->
 *   1 response and we're proud of it.
 * - For now, Persistent connections. Maybe this'll change, we'll see. There is
 *   some code in there that supports it (while loop, ConnState, polling,
 *   server_fd) but I just close the connection right away anyway
 */

//TODO: static global HTTP_Message objects for fixed responses

const static char* error_template =
	"<html>"
	"<head><title>%d %.*s</title></head>" // code phrase
	"<body><h1>%d %.*s</h1>%.*s</body>" // code phrase message
	"</html>";

//Send a formatted HTTP error to the client
static inline void handle_error(int fd, int code, StringRef text)
{
	HTTP_Message message = empty_message;

	//Set the request line
	message.response.http_version = '1';
	set_response(&message, code);

	//Set some headers
	add_header(&message, es_temp("Connection"), es_temp("close"));
	add_header(&message, es_temp("Content-Type"), es_temp("text/html"));

	if(code == 405)
		add_header(&message, es_temp("Allow"), es_temp("GET, HEAD, POST"));

	//Get the phrase
	StringRef phrase = response_phrase(code);

	//Build the body
	set_body(&message,
		es_printf(error_template,
			code, ES_STRREFPRINT(&phrase),
			code, ES_STRREFPRINT(&phrase),
			ES_STRREFPRINT(&text)));

	//Send the response
	if(write_response(&message, fd))
		submit_debug(es_copy(es_temp("Error writing error to client")));

	//Clear the response
	clear_response(&message);
}

//Global, thread-local thread data
typedef struct
{
	struct sockaddr_in client_addr;
	int client_fd;
	int server_fd;

	ConnState state;

	HTTP_Message request;
	HTTP_Message response;
} ThreadData;

static inline void init_thread_data(ThreadData* thread_data, void* ptr)
{
	HTTP_Data* data = ((HTTP_Data*)(ptr));
	thread_data->client_addr = data->connection_sockaddr;
	thread_data->client_fd = data->connection_fd;
	thread_data->server_fd = -1;
	thread_data->state = cs_unknown;
	thread_data->request = thread_data->response = empty_message;
	free(ptr);
}

static void cleanup_thread_data(void* td)
{
	ThreadData* thread_data = td;
	if(thread_data->client_fd >= 0) close(thread_data->client_fd);
	if(thread_data->server_fd >= 0) close(thread_data->server_fd);

	clear_request(&thread_data->request);
	clear_response(&thread_data->response);
}

static inline String get_log_string(ThreadData* thread_data)
{
	//Get the Client IP
	char ip_text[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &thread_data->client_addr.sin_addr, ip_text, INET_ADDRSTRLEN);

	//Get the method
	StringRef method_text = method_name(thread_data->request.request.method);

	//Attach the domain, method, and destination to the log
	return es_printf("%.*s: %.*s http://%.*s/%.*s",
		INET_ADDRSTRLEN, ip_text,
		ES_STRREFPRINT(&method_text),
		ES_STRINGPRINT(&thread_data->request.request.domain),
		ES_STRINGPRINT(&thread_data->request.request.path));
}

static inline String get_just_client(ThreadData* thread_data)
{
	char ip_text[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &thread_data->client_addr.sin_addr, ip_text, INET_ADDRSTRLEN);

	return es_copy(es_tempn(ip_text, INET_ADDRSTRLEN));
}

//Pass a 0 code to prevent response
static inline void error(ThreadData* thread_data, int code, const char* msg)
{
	stat_add_error();
	String log_string_base = get_just_client(thread_data);
	String log_string = es_printf("%.*s [ERROR] %s",
		ES_STRINGPRINT(&log_string_base), msg);
	es_free(&log_string_base);
	submit_print(log_string);
	if(code > 0) handle_error(thread_data->client_fd, code, es_temp(msg));
	pthread_exit(0);
}

#define ERROR(MSG) error(&thread_data, 0, MSG)
#define RESPOND_ERROR(CODE, MSG) error(&thread_data, CODE, MSG)

static inline void success(ThreadData* thread_data)
{
	stat_add_success();
	submit_print(get_log_string(thread_data));
}

static inline void filter(ThreadData* thread_data)
{
	stat_add_filtered();
	String log_string = get_log_string(thread_data);
	es_append(&log_string, es_temp(" [FILTERED]"));
	submit_print(log_string);
	handle_error(thread_data->client_fd, 403, es_temp("Blocked by Proxy Filter"));
	pthread_exit(0);
}

void* http_worker_thread(void* ptr)
{
	ThreadData thread_data;
	init_thread_data(&thread_data, ptr);

	pthread_cleanup_push(&cleanup_thread_data, &thread_data);

	while(thread_data.state != cs_close)
	{
		submit_debug_c("Reading request");

		///////////////////////////////////////////////////////////////////////
		// READ REQUEST
		///////////////////////////////////////////////////////////////////////
		submit_debug_c("Reading request line");

		switch(read_request_line(&thread_data.request, thread_data.client_fd))
		{
		case connection_error:
			ERROR("Error: Connection Error");
			break;
		case too_long:
			RESPOND_ERROR(414, "Error: Request line too long");
			break;
		case malformed_line:
			RESPOND_ERROR(400, "Error: Malformed request line");
			break;
		case bad_method:
			RESPOND_ERROR(405, "Error: bad method");
			break;
		case bad_version:
			RESPOND_ERROR(505, "Error: bad HTTP version");
			break;
		}

		submit_debug_c("Reading headers");

		switch(read_headers(&thread_data.request, thread_data.client_fd))
		{
		case connection_error:
			ERROR("Error: Connection Error");
			break;
		case too_long:
			RESPOND_ERROR(413, "Error: Too much header data sent");
			break;
		case malformed_line:
			RESPOND_ERROR(400, "Error: Malformed headers");
			break;
		case too_many_headers:
			RESPOND_ERROR(413, "Error: Too many headers sent");
			break;
		}

		submit_debug_c("Reading body");

		switch(read_body(&thread_data.request, thread_data.client_fd))
		{
		case connection_error:
			ERROR("Error: Connection Error");
			break;
		case bad_content_length:
			RESPOND_ERROR(400, "Error: Content-Length malformed");
			break;
		//TODO: separate too_long errors for chunk header, chunk, body, etc
		case too_long:
			RESPOND_ERROR(413, "Error: Body too long");
			break;
		case malformed_line:
			RESPOND_ERROR(400, "Error: Chunk size line malformed");
			break;
		}

		///////////////////////////////////////////////////////////////////////
		// VALDIDATE REQUEST
		///////////////////////////////////////////////////////////////////////

		submit_debug_c("Checking HTTP");

		//Force shutdown of persistent connections
		thread_data.state = cs_close;

		//Check filters
		if(filter_match_any(es_ref(&thread_data.request.request.domain)))
			filter(&thread_data);

		//Check host
		//Only need to check host in HTTP/1.1
		if(thread_data.request.request.http_version == '1')
		{
			//Just check for the precence of host and assume correctness
			if(!find_header(&thread_data.request, es_temp("Host")))
				RESPOND_ERROR(400, "Error: missing Host: header");
		}

		//Check for content-length in POST
		/*
		 * NOTE: I'M NOT ACTUALLY DOING THIS, AND HERE'S WHY:
		 * - the POST may be chunked.
		 * - More importantly, it isn't actually a requirement that POST
		 *   requests have a body, strange as it may sound. If you try to send
		 *   a body without a content length, it'll manifest as a parse error
		 *   on the next request proxy, or pass silently if the connection is
		 *   closed. However, in any case, there's no way to distinguish
		 *   between an accidental and deliberate missing content-length.
		 */

		///////////////////////////////////////////////////////////////////////
		// SEND REQUEST
		///////////////////////////////////////////////////////////////////////

		submit_debug_c("Forwarding request");

		//This if is here for hypothetical persistant connections
		if(thread_data.server_fd < 0)
		{
			submit_debug_c("Opening initial connection to server");

			thread_data.server_fd = socket(PF_INET, SOCK_STREAM, 0);
			if(thread_data.server_fd < 0)
				RESPOND_ERROR(500, "Error: Unable to open socket");

			submit_debug_c("Looking up host");

			struct addrinfo* host_info;
			if(getaddrinfo(es_cstrc(&thread_data.request.request.domain),
					"http", 0, &host_info))
				RESPOND_ERROR(500, "Error: error looking up host");

			submit_debug_c("Connecting to host");

			if(connect(thread_data.server_fd, host_info->ai_addr, sizeof(*(host_info->ai_addr))) < 0)
			{
				freeaddrinfo(host_info);
				RESPOND_ERROR(500, "Error: unable to connect to host");
			}

			freeaddrinfo(host_info);
		}

		submit_debug_c("Writing request");

		if(write_request(&thread_data.request, thread_data.server_fd))
			RESPOND_ERROR(502, "Error: error writing request to server");

		///////////////////////////////////////////////////////////////////////
		// GET RESPONSE
		///////////////////////////////////////////////////////////////////////
		submit_debug_c("Reading response");

		/*
		 * TODO: better error messages back to the client. This isn't really
		 * a prioirty because these errors are all for various forms of
		 * invalid HTTP response, not valid HTTP responses that are just errors.
		 */
		if(read_response_line(&thread_data.response, thread_data.server_fd))
			RESPOND_ERROR(502, "Error reading response line");
		if(read_headers(&thread_data.response, thread_data.server_fd))
			RESPOND_ERROR(502, "Error reading response headers");
		if(read_body(&thread_data.response, thread_data.server_fd))
			RESPOND_ERROR(502, "Error reading response body");

		///////////////////////////////////////////////////////////////////////
		// SEND RESPONSE
		///////////////////////////////////////////////////////////////////////
		submit_debug_c("Writing response");
		if(write_response(&thread_data.response, thread_data.client_fd))
			ERROR("Error writing response");

		//NO ERRORS! WE SURVIVED!
		success(&thread_data);

		clear_request(&thread_data.request);
		clear_response(&thread_data.response);

		//This will loop is the connection is persistent
	}

	pthread_cleanup_pop(1);

	return 0;
}
