/*
 * http_write.c
 *
 *  Created on: Feb 18, 2014
 *      Author: nathan
 */

#include <sys/socket.h>
#include <stdbool.h>
#include "http.h"

#include "config.h"


static inline int write_ref(StringRef ref, int connection)
{
	return send(connection, ref.begin, ref.size, 0) < 0;
}

static inline int write_str(String str, int connection)
{
	int result = write_ref(es_ref(&str), connection);
	es_free(&str);
	return result;
}

static inline int write_request_line(HTTP_ReqLine* line, int connection)
{
	if(write_ref(method_name(line->method), connection)) return 1;
	if(write_ref(es_temp(" "), connection)) return 1;

	if(line->domain.size)
	{
		if(write_ref(es_temp("http://"), connection)) return 1;
		if(write_ref(es_ref(&line->domain), connection)) return 1;
	}

	if(write_str(es_printf("/%.*s HTTP/1.%c\r\n", ES_STRINGPRINT(&line->path),
			line->http_version), connection))
		return 1;
	return 0;
}

//Write the response line
static inline int write_response_line(HTTP_RespLine* line, int connection)
{
	if(write_str(es_printf("HTTP/1.%c %d %.*s\r\n",
			line->http_version,
			line->status,
			ES_STRINGPRINT(&line->phrase)), connection))
		return 1;
	return 0;
}

//Write a header
static inline int write_headers(HTTP_Header* header, int connection)
{
	while(header)
	{
		if(write_str(es_printf("%.*s: %.*s\r\n",
				ES_STRINGPRINT(&header->name),
				ES_STRINGPRINT(&header->value)), connection))
			return 1;
		header = header->next;
	}
	return 0;
}

//Write all headers, empty line, and body
//TODO: support for live forwarding of chunked encoding.
static inline int write_common(HTTP_Message* message, int connection)
{
	//Write headers
	if(write_headers(message->headers, connection)) return 1;

	//Write blank line
	if(write_ref(es_temp("\r\n"), connection)) return 1;

	//Write body
	if(write_ref(es_ref(&message->body), connection)) return 1;

	return 0;
}

//Write a whole request
int write_request(HTTP_Message* request, int connection)
{
	if(write_request_line(&request->request, connection))
		return -1;
	if(write_common(request, connection) == -1)
		return -1;
	return 0;
}

int write_response(HTTP_Message* response, int connection)
{
	if(write_response_line(&response->response, connection))
		return -1;
	if(write_common(response, connection))
		return -1;
	return 0;
}
