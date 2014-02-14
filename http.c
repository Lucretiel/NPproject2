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
#include "readable_regex.h"
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

///////////////////////////////////////////////////////////////////////////////
// INIT
///////////////////////////////////////////////////////////////////////////////

//// REGEXES
/*
 * creates COMPONENT_regex, a global static compiled regex, and
 * num_COMPONENT_matches, a global static int for creating submatch arrays.
 *
 * Note that every regex you create here should be initialized in init_http,
 * and the size be the number of subcomponents in the initialized regex.
 */

typedef struct
{
	regex_t regex;
	int num_groups;
} Regex;

static Regex request_regex;
static Regex response_regex;
static Regex header_regex;

enum
{
	request_match_all,
	request_match_method,
	request_match_domain,
	request_match_path,
	request_match_version,
	num_request_matches
};

enum
{
	response_match_all,
	response_match_version,
	response_match_status,
	response_match_phrase,
	num_response_matches
};

enum
{
	header_match_all,
	header_match_name,
	header_match_value,
	num_header_matches
};

//Note that regexes are compiled to be case-insensitive

#define SPACE " "
#define CR_LF "\r\n"

#define HTTP_VERSION \
	"HTTP/" SUBMATCH( \
		AT_LEAST_ONE("[0-9]") \
		"\\." \
		AT_LEAST_ONE("[0-9]"))

#define URI_CHARACTER \
	GROUP( EITHER( \
		CLASS( \
			"a-z0-9"               /* ALPHANUMERICS */ \
			"._~:/?#[@!$&'()*+,;=" /* PUNCTUATION */ \
			"\\]\\-"               /* ESCAPED PUNCTUATION */ \
		), \
		"%" EXACTLY(2, "[0-9a-f]"))) /* PERCENT ENCODED CHARACTER */
/*
 * Regex compiler wrapper. This could be a function, but I'd like to be able to
 * use the FILLED_REGEX macro, which requires a string literal
 */
#define REGEX_COMPILE(COMPONENT, NUM_GROUPS, CONTENT) \
	COMPONENT.num_groups = NUM_GROUPS; \
	regcomp(&COMPONENT.regex, FULL_ANCHOR(CONTENT), REG_ICASE | REG_EXTENDED)

//Compile regular expressions
void init_http()
{
	REGEX_COMPILE(request_regex, num_request_matches,
		SUBMATCH(AT_LEAST_ONE("[A-Z]")) //METHOD
		SPACE
		OPTIONAL(SUBMATCH("http://" MINIMAL(MANY(URI_CHARACTER)))) //DOMAIN
		"/" SUBMATCH(MANY(URI_CHARACTER)) //PATH
		SPACE
		HTTP_VERSION //HTTP VERSION
		CR_LF);

	//TODO: Replace [:print:] with something better
	REGEX_COMPILE(response_regex, num_response_matches,
		HTTP_VERSION //HTTP VERSION
		SPACE
		SUBMATCH("[1-5][0-9][0-9]") //RESPONSE CODE
		SPACE
		SUBMATCH(MANY("[:print:]")) //REASON PHRASE
		CR_LF);

	//TODO: unicode?
	REGEX_COMPILE(header_regex, num_header_matches,
		SUBMATCH(MINIMAL(MANY("[:print:]")))
		": "
		SUBMATCH(MINIMAL(MANY("[:print:]")))
		CR_LF);
}

typedef struct
{
	const char* string;
	regmatch_t matches[10];
} RegexMatches;

inline static int regex_match(const Regex* regex, RegexMatches* matches, const char* str)
{
	return regexec(
			&regex->regex,
			matches->string = str,
			regex->num_groups,
			matches->matches, 0);
}

inline static const char* match_begin(const RegexMatches* matches, int which)
{
	const regmatch_t* const match = matches->matches + which;
	return match->rm_so != -1 ? matches->string + match->rm_so : 0;
}

inline static size_t match_length(const RegexMatches* matches, int which)
{
	const regmatch_t* const match = matches->matches + which;
	return match->rm_eo - match->rm_so;
}

inline static bool is_match_nonempty(const RegexMatches* matches, int which)
{
	return match_begin(matches, which) && match_length(matches, which);
}

inline static char* copy_regex_part(const RegexMatches* matches, int which)
{
	if(is_match_nonempty(matches, which))
		return memcpy(
			calloc(
				match_length(matches, which) + 1, sizeof(char)),
			match_begin(matches, which),
			match_length(matches, which));
	else
		return 0;
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
			line->reason);
	return 0;
}

//Write a header
static inline int write_header(HTTP_Header* header, FILE* connection)
{
	fprintf(connection, "%s: %s\r\n", header->name, header->value);
	return 0;
}

//Write all headers, empty line, and body
static inline int write_common(HTTP_Message* message, FILE* connection)
{
	for(int i = 0;
			i < message->num_headers &&
			message->headers[i].name &&
			message->headers[i].value;
			++i)
		write_header(message->headers + i, connection);

	fputs("\r\n", connection);

	if(message->body)
		fwrite(message->body, sizeof(char), message->body_length, connection);

	return 0;
}

//Write a whole request
int write_request(HTTP_Message* request, FILE* connection)
{
	if(write_request_line(&request->request, connection) == -1)
		return -1;
	if(write_common(request, connection) == -1)
		return -1;
	return 0;
}

int write_response(HTTP_Message* response, FILE* connection)
{
	if(write_response_line(&response->response, connection) == -1)
		return -1;
	if(write_common(response, connection) == -1)
		return -1;
	return 0;
}

///////////////////////////////////////////////////////////////////////////////
// READS
///////////////////////////////////////////////////////////////////////////////

const int read_success = 0;
const int connection_error = 1;

const int malformed_line = 2;
const int malformed_header = 3;

const int bad_method = 4;
const int bad_version = 5;
const int bad_status = 6;

static inline bool is_method(const char* method, const RegexMatches* matches)
{
	return strncasecmp(method,
			match_begin(matches, request_match_method),
			match_length(matches, request_match_method)) == 0;
}

#define READ_RETURN(CODE) { free(buffer.storage_begin); return (CODE); }
//TODO: Holy shit error checking
int read_request_line(HTTP_Message* message, FILE* connection)
{
	clean_message(message);

	AutoBuffer buffer = init_autobuf;

	//Magic line number 1: Read up to a CR LF, autoallocating as nessesary
	if(read_line(&buffer, connection))
		READ_RETURN(connection_error);

	//Magic line number 2: Match the regex
	RegexMatches matches;
	if(regex_match(&request_regex, &matches, buffer.storage_begin) == REG_NOMATCH)
		READ_RETURN(malformed_line);

	//Verify and get the method
	if(is_method("HEAD", &matches))
		message->request.method = head;
	else if(is_method("GET", &matches))
		message->request.method = get;
	else if(is_method("POST", &matches))
		message->request.method = post;
	else
		READ_RETURN(bad_method);

	//Verify and get the HTTP version
	const char* http_version = match_begin(&matches, request_match_version);
	if(match_length(&matches, request_match_version) != 3 ||
			http_version[0] != '1' || http_version[2] != '0' ||
			http_version[2] != '1')
		READ_RETURN(bad_version);
	message->request.http_version = http_version[2];

	//Get the domain
	message->request.domain = copy_regex_part(&matches, request_match_domain);

	//Get the path
	message->request.path = copy_regex_part(&matches, request_match_path);

	READ_RETURN(read_success);
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
	CLEAR(message->response.reason);
	clear_common(message);
}
