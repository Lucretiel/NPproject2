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

static inline int write_request_line(HTTP_ReqLine* line, FILE* connection)
{
	switch(line->method)
	{
	case head:
		fputs("HEAD ", connection);
		break;
	case get:
		fputs("GET ", connection);
		break;
	case post:
		fputs("POST ", connection);
		break;
	}

	if(line->domain.size)
		fprintf(connection, "http://%.*s", (int)ES_SIZESTRING(&line->domain));

	fprintf(connection, "/%.*s HTTP/1.%c\r\n",
		(int)ES_SIZESTRING(&line->path),
		line->http_version);
	return 0;
}

//Write the response line
static inline int write_response_line(HTTP_RespLine* line, FILE* connection)
{
	fprintf(connection, "HTTP/1.%c %d %.*s\r\n",
		line->http_version,
		line->status,
		(int)ES_SIZESTRING(&line->phrase));
	return 0;
}

//Write a header
static inline int write_header(HTTP_Header* header, FILE* connection)
{
	fprintf(connection, "%.*s: %.*s\r\n",
		(int)ES_SIZESTRING(&header->name),
		(int)ES_SIZESTRING(&header->value));
	return 0;
}

//Write all headers, empty line, and body
//TODO: support for live forwarding of chunked encoding.
static inline int write_common(HTTP_Message* message, FILE* connection)
{
	//Write headers
	for(int i = 0; i < message->num_headers; ++i)
		write_header(message->headers + i, connection);

	//Write blank line
	fputs("\r\n", connection);

	//Write body
	fwrite(ES_STRINGSIZE(&message->body), 1, connection);

	//flush
	if(FLUSH_HTTP_MSGS)
		fflush(connection);

	return 0;
}

//Write a whole request
int write_request(HTTP_Message* request, FILE* connection)
{
	if(write_request_line(&request->request, connection))
		return -1;
	if(write_common(request, connection) == -1)
		return -1;
	return 0;
}

int write_response(HTTP_Message* response, FILE* connection)
{
	if(write_response_line(&response->response, connection))
		return -1;
	if(write_common(response, connection))
		return -1;
	return 0;
}
