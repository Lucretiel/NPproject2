/*
 * http_thread.c
 *
 *  Created on: Mar 3, 2014
 *      Author: nathan
 */

#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>

#include "print_thread.h"
#include "http.h"

typedef struct
{
	int connection_fd;
} HTTP_Data;

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
 *   server_fd) but
 *   I just close the connection right away anyway
 */

//TODO: static global HTTP_Message objects for fixed responses

const static char* error_template =
	"<html>"
	"<head><title>%d %.*s</title></head>"
	"<body><h1>%d %.*s</h1>%.*s</body>"
	"</html>";

void handle_error(int fd, int code, StringRef text)
{
	submit_debug(es_copy(text));

	HTTP_Message message = empty_message;

	//Set the request line
	message.response.http_version = '1';
	set_response(&message, code);

	//Set some headers
	add_header(&message, es_temp("Connection"), es_temp("close"));
	add_header(&message, es_temp("Content-Type"), es_temp("text/html"));
	add_header(&message, es_temp("Allow"), es_temp("GET, HEAD, POST"));

	//Get the phrase
	StringRef phrase = response_phrase(code);

	//Build the body
	char* body = malloc(512);
	int body_size = sprintf(body, error_template,
		code, (int)phrase.size, phrase.begin,
		code, (int)phrase.size, phrase.begin,
		(int)ES_SIZESTRREF(&text));

	set_body(&message, es_move_cstrn(body, body_size));

	//Send the response
	if(write_response(&message, fd))
		submit_debug(es_copy(es_temp("Error writing error to client")));

	//Clear the response
	clear_response(&message);
}

static inline void debug_msg(const char* msg)
{ submit_debug(es_copy(es_temp(msg))); }

void* http_thread(void* ptr)
{
	int client_fd = ((HTTP_Data*)(ptr))->connection_fd;
	//Assuming the actual fd won't be 0. Using 0 here to conditionally close()
	int server_fd = 0;
	free(ptr);

	ConnState state = cs_unknown;

	HTTP_Message request = empty_message;
	HTTP_Message response = empty_message;

	#define RETURN() { clear_request(&request); clear_response(&response); \
		close(client_fd); if(server_fd > 0) close(server_fd); return 0; }

	#define ERROR(MSG) { debug_msg(MSG); RETURN() }

	#define RESPOND_ERROR(CODE, MSG) { debug_msg(MSG); \
		handle_error(client_fd, CODE, es_temp(MSG)); RETURN() }

	while(state != cs_close)
	{
		//TODO: polling etc
		debug_msg("Reading request");

		debug_msg("Reading request line");

		switch(read_request_line(&request, client_fd))
		{
		case connection_error: ERROR("Error: Connection Error")
		case too_long: RESPOND_ERROR(414, "Error: Request line too long")
		case malformed_line: RESPOND_ERROR(400, "Error: Malformed request line")
		case bad_method: RESPOND_ERROR(405, "Error: bad method")
		case bad_version: RESPOND_ERROR(505, "Error: bad HTTP version")
		}

		debug_msg("Reading headers");

		switch(read_headers(&request, client_fd))
		{
		case connection_error: ERROR("Error: Connection Error")
		case too_long: RESPOND_ERROR(413, "Error: Too much header data sent")
		case malformed_line: RESPOND_ERROR(400, "Error: Malformed headers")
		case too_many_headers: RESPOND_ERROR(413, "Error: Too many headers sent")
		}

		debug_msg("Reading body");

		switch(read_body(&request, client_fd))
		{
		case connection_error: ERROR("Error: Connection Error")
		case bad_content_length: RESPOND_ERROR(400, "Error: Content-Length malformed")
		//TODO: separate too_long errors for chunk header, chunk, body, etc
		case too_long: RESPOND_ERROR(413, "Error: Body too long")
		case malformed_line: RESPOND_ERROR(400, "Error: Chunk size line malformed")
		}

		debug_msg("Checking HTTP");

		//Check filters
		//TODO: check filters

		//Check host
		if(request.request.http_version == '1')
		{
			const HTTP_Header* host = find_header(&request, es_temp("Host"));
			if(!host)
				RESPOND_ERROR(400, "Error: missing Host: header");
		}

		//Check for content-length in POST
		/*
		 * NOTE: I'M NOT ACTUALLY DOING THIS, AND HERE'S WHY:
		 * - the POST may be chunked.
		 * - More importantly, it isn't actually a requirement that POST requests
		 *   have a body, strange as it may sound. If you try to send a body without
		 *   a content length, it'll manifest as a parse error on the next
		 *   request proxy, or pass silently if the connection is closed.
		 */

		//TODO: persistent connections

		//Add connection:close header for http/1.1
		if(request.request.http_version == '1')
			add_header(&request, es_temp("Connection"), es_temp("close"));
		state = cs_close;

		debug_msg("Forwarding request");

		if(!server_fd)
		{
			debug_msg("Opening initial connection to server");

			server_fd = socket(PF_INET, SOCK_STREAM, 0);
			if(server_fd < 0)
				RESPOND_ERROR(500, "Error: Unable to open socket")

			debug_msg("Looking up host");

			struct addrinfo* host_info;
			if(getaddrinfo(es_cstrc(request.request.domain), "http", 0, &host_info))
				RESPOND_ERROR(500, "Error: error looking up host")

			debug_msg("Connecting to host");

			if(connect(server_fd, host_info->ai_addr, sizeof(*(host_info->ai_addr))) < 0)
			{
				freeaddrinfo(&host_info);
				RESPOND_ERROR(500, "Error: unable to connect to host")
			}

			freeaddrinfo(&host_info);
		}

		debug_msg("Writing request");

		if(write_request(&request, server_fd))
			RESPOND_ERROR(502, "Error: error writing request to server")

		debug_msg("Reading response");

		/*
		 * TODO: better error messages back to the client. This isn't really
		 * a prioirty because these errors are all for various forms of
		 * invalid HTTP response, not valid HTTP responses that are just errors.
		 */
		if(
				read_request_line(&response, server_fd) ||
				read_headers(&response, server_fd) ||
				read_body(&response, server_fd))
			RESPOND_ERROR(502, "Error: Connection error reading response")

		debug_msg("Writing response");
		if(write_response(&response, client_fd))
			ERROR("Error writing response");

		clear_request(&request);
		clear_response(&response);
		//TODO: non-debug output
	}

	debug_msg("Closing client and server connections");
	close(client_fd);
	close(server_fd);
}
