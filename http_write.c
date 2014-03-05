/*
 * http_write.c
 *
 *  Created on: Feb 18, 2014
 *      Author: nathan
 */

#include <stdio.h>
#include "http.h"

#include "config.h"

//TODO: io error checking

#define WRITE(...) { if(dprintf(connection, __VA_ARGS__) < 0) return 1; }

static inline int write_request_line(HTTP_ReqLine* line, int connection)
{
	StringRef method = method_name(line->method);
	WRITE("%.*s ", ES_STRREFPRINT(&method))

	if(line->domain.size)
		WRITE("http://%.*s", ES_STRINGPRINT(&line->domain))

	WRITE("/%.*s HTTP/1.%c\r\n",
		ES_STRINGPRINT(&line->path),
		line->http_version)
	return 0;
}

//Write the response line
static inline int write_response_line(HTTP_RespLine* line, int connection)
{
	WRITE("HTTP/1.%c %d %.*s\r\n",
		line->http_version,
		line->status,
		ES_STRINGPRINT(&line->phrase));
	return 0;
}

//Write a header
static inline int write_headers(HTTP_Header* header, int connection)
{
	while(header)
	{
		WRITE("%.*s: %.*s\r\n",
			ES_STRINGPRINT(&header->name),
			ES_STRINGPRINT(&header->value));
		header = header->next;
	}
	return 0;
}

//Write all headers, empty line, and body
//TODO: support for live forwarding of chunked encoding.
static inline int write_common(HTTP_Message* message, int connection)
{
	//Write headers
	write_headers(message->headers, connection);

	//Write blank line
	WRITE("\r\n");

	//Write body
	WRITE("%.*s", ES_STRINGPRINT(&message->body));

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
