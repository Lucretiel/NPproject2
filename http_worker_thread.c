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
static __thread struct
{
	struct sockaddr_in client_addr;
	int client_fd;
	int server_fd;

	ConnState state;

	HTTP_Message request;
	HTTP_Message response;
} thread_data;

static inline void init_thread_data(void* ptr)
{
	HTTP_Data* data = ((HTTP_Data*)(ptr));
	thread_data.client_addr = data->connection_sockaddr;
	thread_data.client_fd = data->connection_fd;
	thread_data.server_fd = -1;
	thread_data.state = cs_unknown;
	thread_data.request = thread_data.response = empty_message;
	free(ptr);
}

static void cleanup_thread_data(void* td)
{
	if(thread_data.client_fd >= 0) close(thread_data.client_fd);
	if(thread_data.server_fd >= 0) close(thread_data.server_fd);

	clear_request(&thread_data.request);
	clear_response(&thread_data.response);
}

static inline String get_log_string()
{
	//Get the Client IP
	char ip_text[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &thread_data.client_addr.sin_addr, ip_text, INET_ADDRSTRLEN);

	//Get the method
	StringRef method_text = method_name(thread_data.request.request.method);

	//Attach the domain, method, and destination to the log
	return es_printf("%.*s: %.*s http://%.*s/%.*s",
		INET_ADDRSTRLEN, ip_text,
		ES_STRREFPRINT(&method_text),
		ES_STRINGPRINT(&thread_data.request.request.domain),
		ES_STRINGPRINT(&thread_data.request.request.domain));
}

static inline void error(const char* error) __attribute__ ((noreturn));
static inline void error(const char* error)
{
	stat_add_error();
	submit_debug(es_copy(es_temp(error)));
	pthread_exit(0);
}

static inline void respond_error(int code, const char* error) __attribute__ ((noreturn));
static inline void respond_error(int code, const char* error)
{
	stat_add_error();
	submit_debug(es_copy(es_temp(error)));
	handle_error(thread_data.client_fd, code, es_temp(error));
	pthread_exit(0);
}

static inline void success()
{
	stat_add_success();
	submit_print(get_log_string());
}

static inline void filter() __attribute__ ((noreturn));
static inline void filter()
{
	stat_add_filtered();
	String log_string = get_log_string();
	es_append(&log_string, es_temp(" [FILTERED]"));
	submit_print(log_string);
	handle_error(thread_data.client_fd, 403, es_temp("Blocked by Proxy Filter"));
	pthread_exit(0);
}

static inline void debug_msg(const char* msg)
{
	submit_debug(es_copy(es_temp(msg)));
}


void* http_thread(void* ptr)
{
	init_thread_data(ptr);

	pthread_cleanup_push(&cleanup_thread_data, 0);

	while(thread_data.state != cs_close)
	{
		debug_msg("Reading request");

		///////////////////////////////////////////////////////////////////////
		// READ REQUEST
		///////////////////////////////////////////////////////////////////////
		debug_msg("Reading request line");

		switch(read_request_line(&thread_data.request, thread_data.client_fd))
		{
		case connection_error: error("Error: Connection Error");
		case too_long: respond_error(414, "Error: Request line too long");
		case malformed_line: respond_error(400, "Error: Malformed request line");
		case bad_method: respond_error(405, "Error: bad method");
		case bad_version: respond_error(505, "Error: bad HTTP version");
		}

		debug_msg("Reading headers");

		switch(read_headers(&thread_data.request, thread_data.client_fd))
		{
		case connection_error: error("Error: Connection Error");
		case too_long: respond_error(413, "Error: Too much header data sent");
		case malformed_line: respond_error(400, "Error: Malformed headers");
		case too_many_headers: respond_error(413, "Error: Too many headers sent");
		}

		debug_msg("Reading body");

		switch(read_body(&thread_data.request, thread_data.client_fd))
		{
		case connection_error: error("Error: Connection Error");
		case bad_content_length: respond_error(400, "Error: Content-Length malformed");
		//TODO: separate too_long errors for chunk header, chunk, body, etc
		case too_long: respond_error(413, "Error: Body too long");
		case malformed_line: respond_error(400, "Error: Chunk size line malformed");
		}

		///////////////////////////////////////////////////////////////////////
		// VALDIDATE REQUEST
		///////////////////////////////////////////////////////////////////////

		debug_msg("Checking HTTP");

		//Force shutdown of persistent connections
		//Add connection:close header for http/1.1
		//TODO: check for HTTP/1.1 Connection: keep-alive
		if(thread_data.request.request.http_version == '1')
			if(!find_header(&thread_data.request, es_temp("Connection")))
				add_header(&thread_data.request, es_temp("Connection"), es_temp("close"));
		thread_data.state = cs_close;

		//Check filters
		if(filter_match_any(es_ref(&thread_data.request.request.domain)))
			filter();

		//Check host
		//Only need to check host in HTTP/1.1
		if(thread_data.request.request.http_version == '1')
		{
			//Just check for the precence of host and assume correctness
			if(!find_header(&thread_data.request, es_temp("Host")))
				respond_error(400, "Error: missing Host: header");
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

		debug_msg("Forwarding request");

		//This if is here for hypothetical persistant connections
		if(thread_data.server_fd < 0)
		{
			debug_msg("Opening initial connection to server");

			thread_data.server_fd = socket(PF_INET, SOCK_STREAM, 0);
			if(thread_data.server_fd < 0)
				respond_error(500, "Error: Unable to open socket");

			debug_msg("Looking up host");

			struct addrinfo* host_info;
			if(getaddrinfo(es_cstrc(&thread_data.request.request.domain),
					"http", 0, &host_info))
				respond_error(500, "Error: error looking up host");

			debug_msg("Connecting to host");

			if(connect(thread_data.server_fd, host_info->ai_addr, sizeof(*(host_info->ai_addr))) < 0)
			{
				freeaddrinfo(host_info);
				respond_error(500, "Error: unable to connect to host");
			}

			freeaddrinfo(host_info);
		}

		debug_msg("Writing request");

		if(write_request(&thread_data.request, thread_data.server_fd))
			respond_error(502, "Error: error writing request to server");

		///////////////////////////////////////////////////////////////////////
		// GET RESPONSE
		///////////////////////////////////////////////////////////////////////
		debug_msg("Reading response");

		/*
		 * TODO: better error messages back to the client. This isn't really
		 * a prioirty because these errors are all for various forms of
		 * invalid HTTP response, not valid HTTP responses that are just errors.
		 */
		if(
				read_request_line(&thread_data.response, thread_data.server_fd) ||
				read_headers(&thread_data.response, thread_data.server_fd) ||
				read_body(&thread_data.response, thread_data.server_fd))
			respond_error(502, "Error: Connection error reading response");

		///////////////////////////////////////////////////////////////////////
		// SEND RESPONSE
		///////////////////////////////////////////////////////////////////////
		debug_msg("Writing response");
		if(write_response(&thread_data.response, thread_data.client_fd))
			error("Error writing response");

		//NO ERRORS! WE SURVIVED!
		success();

		clear_request(&thread_data.request);
		clear_response(&thread_data.response);

		//This will loop is the connection is persistent
	}

	pthread_cleanup_pop(1);

	return 0;
}
