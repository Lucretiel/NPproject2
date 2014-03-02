/*
 * http_write.c
 *
 *  Created on: Feb 18, 2014
 *      Author: nathan
 */

#include <stdlib.h>
#include "http.h"

#include "config.h"

//TODO: io error checking

#define WRITE(...) dprintf(connection, __VA_ARGS__)

static inline int write_request_line(HTTP_ReqLine* line, int connection)
{
	switch(line->method)
	{
	case head:
		WRITE("HEAD ");
		break;
	case get:
		WRITE("GET ");
		break;
	case post:
		WRITE("POST ");
		break;
	}

	if(line->domain.size)
		WRITE("http://%.*s", (int)ES_SIZESTRING(&line->domain));

	WRITE("/%.*s HTTP/1.%c\r\n",
		(int)ES_SIZESTRING(&line->path),
		line->http_version);
	return 0;
}

//Write the response line
static inline int write_response_line(HTTP_RespLine* line, int connection)
{
	WRITE("HTTP/1.%c %d %.*s\r\n",
		line->http_version,
		line->status,
		(int)ES_SIZESTRING(&line->phrase));
	return 0;
}

//Write a header
static inline int write_headers(HTTP_Header* header, int connection)
{
	while(header)
	{
		WRITE("%.*s: %.*s\r\n",
			(int)ES_SIZESTRING(&header->name),
			(int)ES_SIZESTRING(&header->value));
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
	WRITE("%.*s", (int)ES_SIZESTRCNST(&message->body));

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
