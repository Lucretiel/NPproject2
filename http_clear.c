/*
 * http_clear.c
 *
 *  Created on: Feb 18, 2014
 *      Author: nathan
 */

#include <stdlib.h>
#include "http.h"

#define CLEAR(PTR) free(PTR); PTR = 0

static inline void clear_header(HTTP_Header* header)
{
	CLEAR(header->name);
	CLEAR(header->value);
}

static inline void clear_common(HTTP_Message* message)
{
	CLEAR(message->body);
	for(int i = 0; i < message->num_headers; ++i)
		clear_header(message->headers + i);
	CLEAR(message->headers);
	message->num_headers = 0;
	message->body_length = 0;
}

void clear_request(HTTP_Message* message)
{
	CLEAR(message->request.domain);
	CLEAR(message->request.path);
	clear_common(message);
}

void clear_response(HTTP_Message* message)
{
	CLEAR(message->response.phrase);
	clear_common(message);
}
