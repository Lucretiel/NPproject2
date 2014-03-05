/*
 * http_manip.c
 *
 *  Created on: Mar 3, 2014
 *      Author: nathan
 */

#include <stdlib.h>

#include "http.h"

#define CASE(CODE, STR) case CODE: return es_temp(STR);
#define DEFAULT(STR) default: return es_temp(STR);

StringRef method_name(MethodType method)
{
	switch(method)
	{
	CASE(get, "GET")
	CASE(head, "HEAD")
	CASE(post, "POST")
	}
}

//Find a header
const HTTP_Header* find_header(const HTTP_Message* message, StringRef header_name)
{
	String name_lower = es_tolower(header_name);
	StringRef name_lower_ref = es_ref(&name_lower);

	#define RETURN(HEADER) { es_free(&name_lower); return (HEADER); }

	for(const HTTP_Header* header = message->headers; header;
			header = header->next)
	{
		String lower = es_tolower(es_ref(&header->name));
		int cmp = es_compare(name_lower_ref, es_ref(&lower));
		es_free(&lower);
		if(cmp == 0) RETURN(header);
	}

	RETURN(0)

	#undef RETURN
}

//Add a header
void add_header(HTTP_Message* message, StringRef name, StringRef value)
{
	HTTP_Header* header = malloc(sizeof(HTTP_Header));

	header->name = es_copy(name);
	header->value = es_copy(value);
	header->next = message->headers;
	message->headers = header->next;
}

StringRef response_phrase(int code)
{
	switch(code)
	{
		CASE(100, "Continue")
		CASE(101, "Switching Protocols")
		CASE(200, "OK")
		CASE(201, "Created")
		CASE(202, "Accepted")
		CASE(203, "Non-Authoritative Information")
		CASE(204, "No Content")
		CASE(205, "Reset Content")
		CASE(206, "Partial Content")
		CASE(300, "Multiple Choices")
		CASE(301, "Moved Permanently")
		CASE(302, "Found")
		CASE(303, "See Other")
		CASE(304, "Not Modified")
		CASE(305, "Use Proxy")
		CASE(307, "Temporary Redirect")
		CASE(400, "Bad Request")
		CASE(401, "Unauthorized")
		CASE(402, "Payment Required")
		CASE(403, "Forbidden")
		CASE(404, "Not Found")
		CASE(405, "Method Not Allowed")
		CASE(406, "Not Acceptable")
		CASE(407, "Proxy Authentication Required")
		CASE(408, "Request Timeout")
		CASE(409, "Conflict")
		CASE(410, "Gone")
		CASE(411, "Length Required")
		CASE(412, "Precondition Required")
		CASE(413, "Request Entity Too Large")
		CASE(414, "Request URI Too Long")
		CASE(415, "Unsupported Media Type")
		CASE(416, "Requested Range Not Satisfiable")
		CASE(417, "Expectation Failed")
		CASE(500, "Internal Server Error")
		CASE(501, "Not Implemented")
		CASE(502, "Bad Gateway")
		CASE(503, "Service Unavailable")
		CASE(504, "Gateway Timeout")
		CASE(505, "HTTP Version Not Supported")
		DEFAULT(0)
	}
}

void set_response(HTTP_Message* message, int code)
{
	message->response.status = code;
	message->response.phrase = es_copy(response_phrase(code));
}

void set_body(HTTP_Message* message, String body)
{
	char length_str[80];
	int size = snprintf(length_str, 80, "%zu", body.size);

	add_header(message, es_temp("Content-Length"), es_tempn(length_str, size));
	message->body = body;
}
