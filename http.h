/*
 * http_common.h
 *
 *  Created on: Feb 11, 2014
 *      Author: nathan
 *
 *  Utilities for manipulating HTTP data. Use fdopen to get a FILE* from an fd.
 */

#pragma once

#include <string.h>
#include "EasyString/easy_string.h"

/*
 * HTTP_ReqLine contains the data for a single request line. Members:
 *   method: an enum set to either head, get, or post
 *   domain: a null-terminated string with the domain (www.reddit.com)
 *   path: a null-terminated string with the path (r/python)
 *     No leading '/' character
 *   http_version: the character '0' or '1', depending on the http version
 */
typedef struct
{
	String domain;
	String path;
	enum { head, get, post } method;
	char http_version; //0->1.0, 1->1.1
} HTTP_ReqLine;

/*
 * HTTP_RespLine contains the data for a single Response line. Members:
 *   status: the status code as an int (404)
 *   phrase: the status phrase ("Not Found")
 *   http_version: as in HTTP_ReqLine, this is a '1' or '0' for the version.
 */
typedef struct
{
	String phrase;
	int status;
	char http_version; //0->1.0, 1->1.1
} HTTP_RespLine;

/*
 * HTTP_Header contains the data for a single header. It has 2 null-terminated
 * strings for each of the name and value.
 */
typedef struct _http_header
{
	String name;
	String value;
	struct _http_header* next;
} HTTP_Header;

/*
 * HTTP Message contains a complete HTTP message, either a request or a
 * response. The request line and response line are stored in a union, as a
 * message can only be a request or a response; it is the client's
 * responsibility to keep track of what a given message is. Members:
 *   request: an HTTP_ReqLine structure
 *   response: an HTTP_RespLine structure
 *   num_headers: the number of headers
 *   headers: an array of HTTP_Header data structures. Contains num_headers
 *     headers.
 *   body_length: the size, in bytes, of the body
 *   body: the binary body content.
 *
 * The read_* functions in this header dynamically allocate all the arrays and
 * strings. However, the write functions do not assume anything about the
 * pointers, so the client is free to set any pointers he wants, as long as they
 * free them up later.
 */
typedef struct
{
	union
	{
		HTTP_ReqLine request;
		HTTP_RespLine response;
	};

	HTTP_Header* headers;

	String body;
} HTTP_Message;

static const HTTP_Message empty_message;

/*
 * Must be called in order. Request/response -> headers -> body -> clear.
 * Clear can be called any time after the initial call to read_*_line.
 */

enum
{
	connection_error = 1, //EOF or other connection issue

	malformed_line, //regex didn't match

	bad_method, //method isn't GET, HEAD, or POST
	bad_version, //HTTP version isn't 1.0 or 1.1

	bad_content_length, //content length header is invalid

	too_long, //Something was too long
	too_many_headers //There are too many headers
};

//// READS
//Read the first HTTP Line
int read_request_line(HTTP_Message* message, int fd);
int read_response_line(HTTP_Message* message, int fd);

//Read all headers
int read_headers(HTTP_Message* message, int fd);

//Read the body
int read_body(HTTP_Message* message, int fd);

//// WRITES
int write_request(HTTP_Message* message, int fd);
int write_response(HTTP_Message* message, int fd);

//// CLEARS
void clear_request(HTTP_Message* message);
void clear_response(HTTP_Message* message);

////MANIPULATORS
//Find a header.
const HTTP_Header* find_header(const HTTP_Message* message, StringRef header);

//Add a header
void add_header(HTTP_Message* message, StringRef name, StringRef value);

//Set the response code, and an appropriate phrase
void set_response(HTTP_Message* message, int code);

//Get an appropriate phrase for a given response code
StringRef response_phrase(int code);

//Set the body and content-length header
void set_body(HTTP_Message* message, String body);

//Call this ONCE PER PROGRAM. Before threads.
void init_http();

/*
 * Call this at the end. Optional, because the memory used by init_http is
 * fixed size per run, so it isn't strictly a leak if it isn't cleared.
 */
void deinit_http();
