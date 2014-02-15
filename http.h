/*
 * http_common.h
 *
 *  Created on: Feb 11, 2014
 *      Author: nathan
 *
 *  Utilities for manipulating HTTP data. Use fdopen to get a FILE* from an fd.
 */

#ifndef HTTP_COMMON_H_
#define HTTP_COMMON_H_

#include <stdio.h>

typedef struct
{
	//TODO: support other methods?
	enum { head, get, post } method;
	char* domain;
	char http_version; //0->1.0, 1->1.1
	char* path;
} HTTP_ReqLine;

typedef struct
{
	int status;
	char* phrase;
	char http_version; //0->1.0, 1->1.1
} HTTP_RespLine;

typedef struct
{
	char* name;
	char* value;
} HTTP_Header;

typedef struct
{
	union
	{
		HTTP_ReqLine request;
		HTTP_RespLine response;
	};

	//dynamic array of HTTP_Header
	HTTP_Header* headers;
	int num_headers;

	size_t body_length;
	char* body;
} HTTP_Message;


/*
 * Must be called in order. Request/response -> headers -> body -> clear.
 * Clear can be called any time after the initial call to read_*_line.
 */

extern const int connection_error; //EOF or other connection issue

extern const int malformed_line; //regex didn't match request/response line
extern const int malformed_header; //regex didn't match header line

extern const int bad_method; //method isn't GET, HEAD, or POST
extern const int bad_version; //HTTP version isn't 1.0 or 1.1

extern const int no_content_length; //content length header isn't present
extern const int bad_content_length; //content length header is invalid

int read_request_line(HTTP_Message*, FILE*);
int read_response_line(HTTP_Message*, FILE*);
int read_headers(HTTP_Message*, FILE*);
int read_body(HTTP_Message*, FILE*);

int write_request(HTTP_Message*, FILE*);
int write_response(HTTP_Message*, FILE*);

void clear_request(HTTP_Message*);
void clear_response(HTTP_Message*);

HTTP_Header* find_header(HTTP_Message*, const char*);

//Call this ONCE PER PROGRAM. Before threads.
void init_http();


#endif /* HTTP_COMMON_H_ */
