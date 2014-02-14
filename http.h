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
	char* reason;
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

	int num_headers;
	HTTP_Header* headers;

	int body_length;
	char* body;
} HTTP_Message;


/*
 * Must be called in order. Request/response -> headers -> body -> clear.
 * Clear can be called any time after the initial call to read_*_line.
 * You must have a fully populated data structure to use the writes.
 */

extern const int read_success;
extern const int connection_error;

extern const int malformed_line;
extern const int malformed_header;

extern const int bad_method;
extern const int bad_version;
extern const int bad_status;

int read_request_line(HTTP_Message*, FILE*);
int read_response_line(HTTP_Message*, FILE*);
int read_headers(HTTP_Message*, FILE*);
int read_body(HTTP_Message*, FILE*);

int write_request(HTTP_Message*, FILE*);
int write_response(HTTP_Message*, FILE*);

void clear_request(HTTP_Message*);
void clear_response(HTTP_Message*);

//Call this ONCE PER PROGRAM. Before threads.
void init_http();


#endif /* HTTP_COMMON_H_ */
