/*
 * http_clear.c
 *
 *  Created on: Feb 18, 2014
 *      Author: nathan
 */

#include <stdlib.h>
#include "http.h"

static inline void clear_headers(HTTP_Header* header)
{
	if(header)
	{
		es_free(&header->name);
		es_free(&header->value);
		clear_headers(header->next);
		free(header);
	}
}

static inline void clear_common(HTTP_Message* message)
{
	clear_headers(message->headers);
	message->headers = 0;
	es_clear(&message->body);
}

static inline void clear_request_line(HTTP_ReqLine* line)
{
	es_clear(&line->domain);
	es_clear(&line->path);
}

static inline void clear_response_line(HTTP_RespLine* line)
{
	es_clear(&line->phrase);
}

void clear_request(HTTP_Message* message)
{
	clear_request_line(&message->request);
	clear_common(message);
}

void clear_response(HTTP_Message* message)
{
	clear_response_line(&message->response);
	clear_common(message);
}
