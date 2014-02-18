/*
 * http_common.c
 *
 *  Created on: Feb 11, 2014
 *      Author: nathan
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>

#include "http.h"
#include "ReadableRegex/readable_regex.h"
#include "autobuf.h"

///////////////////////////////////////////////////////////////////////////////
// UTILITY
///////////////////////////////////////////////////////////////////////////////

/*
 * This function resets pointers without freeing etc. It is here for the
 * initial call to read_*_line to make the message valid (ie, ready to be
 * written right away), it is assumed that you previously called clear before
 * doing a new message.
 */
static inline void clean_message(HTTP_Message* message)
{
	message->num_headers = 0;
	message->headers = 0;
	message->body_length = 0;
	message->body = 0;
}

//Find a header
HTTP_Header* find_header(HTTP_Message* message, const char* name)
{
	HTTP_Header* const back = message->headers + message->num_headers;
	for(HTTP_Header* header = message->headers; header < back; ++header)
		if(strcasecmp(name, header->name) == 0)
			return header;
	return 0;
}

///////////////////////////////////////////////////////////////////////////////
// INIT
///////////////////////////////////////////////////////////////////////////////

//// REGEXES

static regex_t request_regex;
static regex_t response_regex;
static regex_t header_regex;

enum
{
	request_match_all=0,
	request_match_method=1,
	request_match_domain=2,
	request_match_path=5,
	request_match_version=8
};

enum
{
	response_match_all=0,
	response_match_version=1,
	response_match_status=2,
	response_match_phrase=3
};

enum
{
	header_match_all=0,
	header_match_name=2,
	header_match_value=3
};

//Note that regexes are compiled to be case-insensitive

#define LWS "[ \t]"
#define CR_LF "\r?\n"

/*
 * Note to the reader: POSIX ERE are... not fun. They don't support non-
 * capturing groups (?:...), lazy captures (*?, +?), or character class set
 * operations (subtraction: [a-x-[aeiou]]. The lack of non-capturing groups is
 * especially problematic, as it means that we have to keep track of group
 * numbers; this is done in the comments, as "//+number" under each one.
 */
#define HTTP_VERSION \
	"HTTP/" SUBMATCH( \
		"[0-9]+" \
		"\\." \
		"[0-9]+")
//+1

#define URI_PATH_CHARACTER \
	SUBMATCH( EITHER( \
		CLASS("]a-z0-9._~:/?#[@!$&'()*+,;=-"), /* NORMAL CHARACTER */ \
		SUBMATCH("%[0-9a-f]{2}"))) /* PERCENT ENCODED CHARACTER */
//+2

//As above, without slash
#define URI_DOMAIN_CHARACTER \
	SUBMATCH( EITHER( \
		CLASS("]a-z0-9._~:?#[@!$&'()*+,;=-"), \
		SUBMATCH("%[0-9a-f]{2}")))
//+2

//Letters, numbers, and punctuation, except colon
#define HEADER_NAME_CHARACTER \
	CLASS("]a-z0-9!\"#$%&'()*+,\\./;<=>?@\[^_`{|}~-")

/*
 * Regex compiler wrapper. This could be a function, but I'd like to be able to
 * use the FILLED_REGEX macro, which requires a string literal
 */

#define REGEX_COMPILE(COMPONENT, CONTENT) \
	regcomp((COMPONENT), FULL_ANCHOR(CONTENT), REG_ICASE | REG_EXTENDED)

//Compile regular expressions
void init_http()
{
	//REQUEST LINE REGEX
	REGEX_COMPILE(&request_regex,
		SUBMATCH(AT_LEAST_ONE("[A-Z]")) //METHOD: index 1
		AT_LEAST_ONE(LWS)
		OPTIONAL(SUBMATCH("http://" MANY(URI_DOMAIN_CHARACTER))) //DOMAIN: index 2
		"/" SUBMATCH(MANY(URI_PATH_CHARACTER)) //PATH: index 5
		AT_LEAST_ONE(LWS)
		HTTP_VERSION //HTTP VERSION: index 8
		CR_LF);

	//RESPONSE LINE REGEX
	REGEX_COMPILE(&response_regex,
		HTTP_VERSION //HTTP VERSION: index 1
		AT_LEAST_ONE(LWS)
		SUBMATCH("[1-5][0-9][0-9]") //RESPONSE CODE: index 2
		AT_LEAST_ONE(LWS)
		SUBMATCH(MANY("[[:print:]]")) //REASON PHRASE: index 3
		CR_LF);

	//TODO: support header folding
	//HEADER REGEX
	REGEX_COMPILE(&header_regex,
		OPTIONAL( SUBMATCH( //The whole header is optional.
			SUBMATCH(AT_LEAST_ONE(HEADER_NAME_CHARACTER)) //NAME: index 2
			":" MANY(LWS)
			SUBMATCH(MANY("[[:print:]\t]")))) //VALUE: index 3
		CR_LF);
}

void deinit_http()
{
	regfree(&request_regex);
	regfree(&response_regex);
	regfree(&header_regex);
}

//Struct linking group matches to a char*, to make finding submatches easier
const static int max_groups = 16;
typedef struct
{
	const char* string;
	regmatch_t matches[max_groups];
} RegexMatches;

//Perform a match
inline static int regex_match(const regex_t* regex, RegexMatches* matches, const char* str)
{
	matches->string = str;
	return regexec(regex, str, max_groups, matches->matches, 0);
}

//Get a pointer to the beginning of the submatch
inline static const char* match_begin(const RegexMatches* matches, int which)
{
	const regmatch_t* const match = matches->matches + which;
	return match->rm_so != -1 ? matches->string + match->rm_so : 0;
}

//Get the size of a submatch
inline static size_t match_length(const RegexMatches* matches, int which)
{
	const regmatch_t* const match = matches->matches + which;
	return match->rm_eo - match->rm_so;
}

//Returns true if a submatch exists and is nonempty
inline static bool is_match_nonempty(const RegexMatches* matches, int which)
{
	return match_begin(matches, which) && match_length(matches, which);
}

//Allocate a new \0-terminated string and copy a submatch into it
inline static char* copy_regex_part(const RegexMatches* matches, int which)
{
	if(is_match_nonempty(matches, which))
		//I wonder what happens if you memcpy(0, ...)
		//Oh well.
		return memcpy(
			calloc(match_length(matches, which) + 1, sizeof(char)),
			match_begin(matches, which),
			match_length(matches, which));
	else
		return 0;
}

//Perform a string comparison to a match part
inline static bool compare_regex_part(const char* value,
		const RegexMatches* matches, int which)
{
	return strncmp(value,
		match_begin(matches, which),
		match_length(matches, which)) == 0;
}

//Perform a case-insensitive comparison to a match part
inline static bool case_compare_regex_part(const char* value,
		const RegexMatches* matches, int which)
{
	return strncasecmp(value,
		match_begin(matches, which),
		match_length(matches, which));
}

///////////////////////////////////////////////////////////////////////////////
// WRITES
///////////////////////////////////////////////////////////////////////////////

//TODO: io error checking

//Write the request line
static inline int write_request_line(HTTP_ReqLine* line, FILE* connection)
{
	switch(line->method)
	{
	case head:
		fputs("HEAD", connection);
		break;
	case get:
		fputs("GET", connection);
		break;
	case post:
		fputs("POST", connection);
		break;
	}

	fprintf(connection, " %s/%s HTTP/1.%c\r\n",
		line->domain ? line->domain : "", //optional domain
		line->path ? line->path : "", //optional path
		line->http_version);
	return 0;
}

//Write the response line
static inline int write_response_line(HTTP_RespLine* line, FILE* connection)
{
	fprintf(connection, "HTTP/1.%c %d %s\r\n",
		line->http_version,
		line->status,
		line->phrase);
	return 0;
}

//Write a header
static inline int write_header(HTTP_Header* header, FILE* connection)
{
	fprintf(connection, "%s: %s\r\n",
		header->name,
		header->value ? header->value : "");
	return 0;
}

//Write all headers, empty line, and body
static inline int write_common(HTTP_Message* message, FILE* connection)
{
	//Write headers until the first null header.
	for(int i = 0; i < message->num_headers; ++i)
		write_header(message->headers + i, connection);

	//Write blank line
	fputs("\r\n", connection);

	//Write body
	if(message->body)
		fwrite(message->body, sizeof(char), message->body_length, connection);

	//flush
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

///////////////////////////////////////////////////////////////////////////////
// READS
///////////////////////////////////////////////////////////////////////////////

const int connection_error = 1;

const int malformed_line = 2;
const int malformed_header = 3;

const int bad_method = 4;
const int bad_version = 5;

const int no_content_length = 6;
const int bad_content_length = 7;

//True if the method in the request matches the method given
static inline bool is_method(const char* method, const RegexMatches* matches)
{
	return case_compare_regex_part(method, matches, request_match_method);
}

//TODO: find a way to share AutoBuffers between read calls, to reduce allocations
//TODO: Holy shit error checking
int read_request_line(HTTP_Message* message, FILE* connection)
{
	//Wipe the message. The client must clear it before calling read.
	clean_message(message);

	AutoBuffer buffer = init_autobuf;
	#define RETURN(CODE) { free(buffer.storage_begin); return (CODE); }

	//Read up to a CR LF, autoallocating as nessesary
	if(autobuf_read_line(&buffer, connection))
		RETURN(connection_error);

	//TODO: check for regex out-of-memory error
	//Match the regex
	RegexMatches matches;
	if(regex_match(&request_regex, &matches, buffer.storage_begin) == REG_NOMATCH)
		RETURN(malformed_line);

	//Verify and get the method
	if(is_method("HEAD", &matches))
		message->request.method = head;
	else if(is_method("GET", &matches))
		message->request.method = get;
	else if(is_method("POST", &matches))
		message->request.method = post;
	else
		RETURN(bad_method);

	//Verify and get the HTTP version
	//Must be 1.0 or 1.1
	if(!(compare_regex_part("1.0", &matches, request_match_version) ||
			compare_regex_part("1.1", &matches, request_match_version)))
	{
		RETURN(bad_version);
	}
	else
	{
		const char* http_version = match_begin(&matches, request_match_version);
		message->request.http_version = http_version[2];
	}

	//Get the domain
	message->request.domain = copy_regex_part(&matches, request_match_domain);

	//Get the path
	message->request.path = copy_regex_part(&matches, request_match_path);

	RETURN(0);
	#undef RETURN
}

int read_response_line(HTTP_Message* message, FILE* connection)
{
	clean_message(message);

	AutoBuffer buffer = init_autobuf;
	#define RETURN(CODE) { free(buffer.storage_begin); return (CODE); }

	//Read up to a CR_LF, autoallocating as nessesary
	if(autobuf_read_line(&buffer, connection))
		RETURN(connection_error);

	//Match the response regex
	RegexMatches matches;
	if(regex_match(&response_regex, &matches, buffer.storage_begin) == REG_NOMATCH)
		RETURN(malformed_line);

	const char* match_ptr;

	//TODO: reduce code repitition between here and request
	//Verify and get the HTTP version
	//Must be 1.0 or 1.1
	if(!(compare_regex_part("1.0", &matches, response_match_version) ||
			compare_regex_part("1.1", &matches, response_match_version)))
	{
		RETURN(bad_version);
	}
	else
	{
		const char* http_version = match_begin(&matches, response_match_version);
		message->response.http_version = http_version[2];
	}

	/*
	 * Get the status code. We know from the regex that it's a valid 3 digit
	 * number, and also that it ends in a space. strtol is safe to use.
	 */
	match_ptr = match_begin(&matches, response_match_status);
	message->response.status = strtol(match_ptr, 0, 10);

	//Get the status phrase
	//TODO: Pick from a table instead?
	message->response.phrase = copy_regex_part(&matches, response_match_phrase);

	RETURN(0);
	#undef RETURN
}

/*
 * Recursive function to get and parse all headers, dynamically allocate an
 * array, and store all the headers. Recursion is used to store all the local
 * header pointers until the total number of headers is known. If there is
 * an error, the error code is returned and no memory is allocated
 */
int get_headers(int* num_headers, HTTP_Header** headers,
		AutoBuffer* buffer, FILE* connection, int depth)
{
	//TODO: size/recursion limit
	//Read next line, return if error
	if(autobuf_read_line(buffer, connection))
		return connection_error;

	//Parse line, return if error
	RegexMatches matches;
	if(regex_match(&header_regex, &matches, buffer->storage_begin) == REG_NOMATCH)
		return malformed_header;

	//If this is not the empty end-of-headers line
	if(strcmp("\r\n", match_begin(&matches, header_match_all)))
	{
		//Copy the header name and value.
		HTTP_Header local_header;
		local_header.name = copy_regex_part(&matches, header_match_name);
		local_header.value = copy_regex_part(&matches, header_match_value);

		//Recurse. Parse all remaining headers, then create the array.
		int error = get_headers(num_headers, headers, buffer, connection, depth+1);

		//If there was an error, free the local storage and pass it up
		//cough cough it sure would be nice if we had exceptions cough
		if(error)
		{
			free(local_header.name);
			free(local_header.value);
			return error;
		}
		//If there was no error, store the name and value in the array
		else
		{
			(*headers)[depth] = local_header;
			return 0;
		}
	}
	//If this is the empty end-of-headers line
	else
	{
		//Allocate the array, set the num_headers value
		*headers = depth ? malloc(sizeof(HTTP_Header) * depth) : 0;
		*num_headers = depth;
		return 0;
	}
}

int read_headers(HTTP_Message* message, FILE* connection)
{
	//Reuse the allocated buffer for each header
	AutoBuffer buffer = init_autobuf;

	message->headers = 0;
	message->num_headers = 0;

	/*
	 * Initiate the recursive call. This function call will fill
	 * message->headers and message->num_headers with a dynamic array and
	 * the count. If there is an error, nothing is allocated and these values
	 *
	 */
	int error = get_headers(&message->num_headers, &message->headers, &buffer,
			connection, 0);
	free(buffer.storage_begin);
	return error;

	#undef RETURN
}

int read_body(HTTP_Message* message, FILE* connection)
{
	//Lookup Content-Length
	const HTTP_Header* content_length_header = find_header(message, "Content-Length");

	//If there is a content-length header
	if(content_length_header)
	{
		//Convert string to int
		char* endptr;
		unsigned long content_length = strtoul(content_length_header->value, &endptr, 10);
		//TODO: overflow check

		//If conversion failed, bad_content_length
		if(endptr == content_length_header->value)
			return bad_content_length;

		//If content length is not 0
		if(content_length)
		{
			//Allocate and read in the body
			char* body = calloc(sizeof(char), content_length);
			size_t amount_read = fread(body, sizeof(char), content_length,
					connection);

			//If the wrong number of bytes were read, assume a connection error
			if(amount_read != content_length)
			{
				free(body);
				return connection_error;
			}

			//Attach body to message and return normally
			message->body_length = content_length;
			message->body = body;
		}
	}
	//If there is no content length header, no_content_length
	else
		return no_content_length;

	return 0;
}

///////////////////////////////////////////////////////////////////////////////
// CLEARS
///////////////////////////////////////////////////////////////////////////////

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
