/*
 * http_common.c
 *
 *  Created on: Feb 11, 2014
 *      Author: nathan
 */

#include <stdlib.h>
#include <string.h>
#include <regex.h>

#include "http.h"
#include "ReadableRegex/readable_regex.h"
#include "config.h"

///////////////////////////////////////////////////////////////////////////////
// UTILITY
///////////////////////////////////////////////////////////////////////////////

//Find a header
HTTP_Header* find_header(HTTP_Message* message, StringRef header_name)
{
	String name_lower = es_tolower(header_name);
	StringRef name_lower_ref = es_ref(&name_lower);

	HTTP_Header* const back = message->headers + message->num_headers;
	for(HTTP_Header* header = message->headers; header < back; ++header)
	{
		String lower = es_tolower(es_ref(&header->name));
		int cmp = es_compare(name_lower_ref, es_ref(&lower));
		es_free(&lower);
		if(cmp == 0)
		{
			es_free(&name_lower);
			return header;
		}
	}
	es_free(&name_lower);
	return 0;
}

///////////////////////////////////////////////////////////////////////////////
// GENERIC REGEX LIBRARY
///////////////////////////////////////////////////////////////////////////////

//TODO: consider moving the generic stuff to a separate header and source file

//The matches are stored in an array of string ref
typedef StringRef* RegexMatches;

//Perform a match
inline static int regex_match(const regex_t* regex, RegexMatches matches,
	StringRef str, int num_matches)
{
	regmatch_t* local = alloca(sizeof(regmatch_t) * num_matches);
	int return_code = regexec(regex, str.begin, num_matches, local, 0);
	if(return_code == 0)
	{
		for(int i = 0; i < num_matches; ++i)
			if(local[i].rm_so == -1)
				matches[i] = es_null_ref;
			else
				matches[i] = es_slice(str, local[i].rm_so, local[i].rm_eo);
	}
	return return_code;
}

//Use a local variable called matches
#define REGEX_PART(PART) (matches[(PART)])

///////////////////////////////////////////////////////////////////////////////
// HTTP REGEX LIBRARY
///////////////////////////////////////////////////////////////////////////////

//HTTP specific regex stuff

//Note that regexes are compiled to be case-insensitive
/*
 * Note to the reader: POSIX ERE are... not fun. They don't support non-
 * capturing groups (?:...), or lazy captures (*?, +?). The lack of
 * non-capturing groups is especially problematic, as it means that we have to
 * keep track of group numbers.
 */

#define LWS "[ \t]"
#define CR_LF "\r?\n"

#define HTTP_VERSION \
	"HTTP/" SUBMATCH( \
		"[1-9][0-9]*" \
		"\\." \
		"[0-9]+")

#define URI_PATH_CHARACTER \
	SUBMATCH( EITHER( \
		CLASS("]a-z0-9._~:/?#[@!$&'()*+,;=-"), /* NORMAL CHARACTER */ \
		"%[0-9a-f]{2}")) /* PERCENT ENCODED CHARACTER */

//As above, without slash
#define URI_DOMAIN_CHARACTER \
	SUBMATCH( EITHER( \
		CLASS("]a-z0-9._~:?#[@!$&'()*+,;=-"), \
		"%[0-9a-f]{2}"))

//Full request regex string
#define REQUEST_REGEX_STR \
	FULL_ANCHOR( \
		SUBMATCH(AT_LEAST_ONE("[A-Z]")) /* METHOD: index 1 */ \
		AT_LEAST_ONE(LWS) \
		OPTIONAL(SUBMATCH("http://" \
			SUBMATCH(MANY(URI_DOMAIN_CHARACTER)))) /* DOMAIN: index 3 */ \
		"/" SUBMATCH(MANY(URI_PATH_CHARACTER)) /* PATH: index 5 */ \
		AT_LEAST_ONE(LWS) \
		HTTP_VERSION /* HTTP VERSION: index 7 */ \
		CR_LF)

//Full response regex string
#define RESPONSE_REGEX_STR \
	FULL_ANCHOR( HTTP_VERSION /* HTTP VERSION: index 1*/ \
	AT_LEAST_ONE(LWS) \
	SUBMATCH("[1-5][0-9][0-9]") /* RESPONSE CODE: index 2*/ \
	AT_LEAST_ONE(LWS) \
	SUBMATCH(MANY("[[:print:]]")) /* REASON PHRASE: index 3 */ \
	CR_LF )


//All printed characters except colon
#define HEADER_NAME_CHARACTER CLASS("]a-z0-9[!\"#$%&'()*+,./:;<=>?@[\\^_`{|}~]-")

//TODO: update this to support \r inline
#define HEADER_VALUE_CHARACTER CLASS("\t[:print:]")

/*
 * Full header regex. It matches strings of the form:
 *
 * header-name: header body
 *   more header body
 *   event more header body
 *
 * Header name must have no indentation or whitespace, more header body must
 * have some
 */
#define HEADER_REGEX_STR FRONT_ANCHOR( \
	SUBMATCH( /* HEADER NAME: 1 */ \
		AT_LEAST_ONE(HEADER_NAME_CHARACTER)) \
	":" MANY(LWS) \
	SUBMATCH(  /* HEADER VALUE: 2 */ \
		AT_LEAST_ONE(HEADER_VALUE_CHARACTER) \
		MANY(SUBMATCH( \
			CR_LF \
			AT_LEAST_ONE(LWS) \
			AT_LEAST_ONE(HEADER_VALUE_CHARACTER))))) \
		CR_LF

#define CHUNK_REGEX_STR \
	FULL_ANCHOR( \
		SUBMATCH(WITHIN(1, 7, CLASS("[:xdigit:]"))) /* SIZE FIELD: 1 */ \
		OPTIONAL(SUBMATCH(";" MANY(CLASS("^\n")))) \
		CR_LF )

//Indexes of the relevant subgroups
enum
{
	request_match_all,
	request_match_method=1,
	request_match_domain=3,
	request_match_path=6,
	request_match_version=9,
	request_num_matches
} request_match_which;

enum
{
	response_match_all,
	response_match_version=1,
	response_match_status=2,
	response_match_phrase=3,
	response_num_matches
} response_match_which;

enum
{
	header_match_all,
	header_match_name=1,
	header_match_value=2,
	header_num_matches
} header_match_which;

enum
{
	chunk_match_all,
	chunk_match_size=1,
	chunk_num_matches
};

//Globals to store the compiled regexes
static regex_t request_regex; //matches the request line
static regex_t response_regex; //matches the response line
static regex_t header_regex; //matches a single header
static regex_t chunk_regex; //matches the chunk header line


///////////////////////////////////////////////////////////////////////////////
// INIT
///////////////////////////////////////////////////////////////////////////////

#define REGEX_COMPILE(COMPONENT, CONTENT) \
	regcomp((COMPONENT), CONTENT, REG_ICASE | REG_EXTENDED)

//Compile regular expressions
void init_http()
{
	REGEX_COMPILE(&request_regex, REQUEST_REGEX_STR);
	REGEX_COMPILE(&response_regex, RESPONSE_REGEX_STR);
	REGEX_COMPILE(&header_regex, HEADER_REGEX_STR);
	REGEX_COMPILE(&chunk_regex, CHUNK_REGEX_STR);
}

//Uncompile regular expressions
void deinit_http()
{
	regfree(&request_regex);
	regfree(&response_regex);
	regfree(&header_regex);
	regfree(&chunk_regex);
}


///////////////////////////////////////////////////////////////////////////////
// READS
///////////////////////////////////////////////////////////////////////////////

//TODO: max line lengths
int read_request_line(HTTP_Message* message, FILE* connection)
{
	//read a line
	String line = es_readline(connection, '\n', MAX_MSG_LINE_SIZE);
	#define RETURN(CODE) { es_free(&line); return (CODE); }

	if(ferror(connection) || feof(connection)) RETURN(connection_error);

	//TODO: check for regex out-of-memory error
	//Match the regex
	StringRef matches[request_num_matches];
	if(regex_match(&request_regex, matches, es_ref(&line),
			request_num_matches) == REG_NOMATCH)
		RETURN(malformed_line);

	//Get the method
	{
		String method = es_tolower(REGEX_PART(request_match_method));
		StringRef methodr = es_ref(&method);
		if(es_compare(es_temp("head"), methodr) == 0)
			message->request.method = head;
		else if(es_compare(es_temp("get"), methodr) == 0)
			message->request.method = get;
		else if(es_compare(es_temp("post"), methodr) == 0)
			message->request.method = post;
		else
		{
			es_free(&method);
			RETURN(bad_method);
		}
		es_free(&method);
	}

	//Get the HTTP Version
	{
		StringRef version = REGEX_PART(request_match_version);
		if(es_compare(es_temp("1.1"), version) && es_compare(es_temp("1.0"), version))
			RETURN(bad_version)
		else
			message->request.http_version = version.begin[2];
	}

	//Get the domain
	message->request.domain = es_copy(REGEX_PART(request_match_domain));

	//Get the path
	message->request.path = es_copy(REGEX_PART(request_match_path));

	RETURN(0);
	#undef RETURN
}

int read_response_line(HTTP_Message* message, FILE* connection)
{
	//Read a line
	String line = es_readline(connection, '\n', MAX_MSG_LINE_SIZE);
	#define RETURN(CODE) { es_free(&line); return (CODE); }

	//Check for read errors
	if(ferror(connection) || feof(connection)) RETURN(connection_error)

	//Match the response regex
	StringRef matches[response_num_matches];
	if(regex_match(&response_regex, matches, es_ref(&line),
			response_num_matches) == REG_NOMATCH)
		RETURN(malformed_line)

	//TODO: reduce code repitition between here and request
	//Get the HTTP Version
	{
		StringRef version = REGEX_PART(response_match_version);
		if(es_compare(es_temp("1.1"), version) && es_compare(es_temp("1.0"), version))
			RETURN(bad_version)
		else
			message->request.http_version = version.begin[2];
	}

	//Get status code
	StringRef status = REGEX_PART(response_match_status);
	message->response.status = strtol(status.begin, 0, 10);

	//Get the status phrase
	message->response.phrase = es_copy(REGEX_PART(response_match_phrase));

	RETURN(0);
	#undef RETURN
}

static inline int empty_line(StringRef line)
{
	return es_compare(es_temp("\r\n"), line) == 0 ||
		es_compare(es_temp("\n"), line) == 0;
}

static inline int get_headers_recursive(unsigned depth, HTTP_Header** headers,
	unsigned* num_headers, StringRef header_text)
{
	if(depth > MAX_NUM_HEADERS)
	{
		return too_many_headers;
	}
	else if(empty_line(header_text))
	{

		if(depth != *num_headers)
		{
			*num_headers = depth;
			*headers = realloc(*headers, depth * sizeof(HTTP_Header));
		}
		return 0;
	}
	else
	{
		StringRef matches[header_num_matches];
		if(regex_match(&response_regex, matches, header_text,
				response_num_matches) == REG_NOMATCH)
			return malformed_line;

		int error = get_headers_recursive(depth + 1, headers, num_headers,
			es_slice(header_text, REGEX_PART(header_match_all).size,
				header_text.size));

		if(error) return error;

		(*headers)[depth].name = es_copy(REGEX_PART(header_match_name));
		(*headers)[depth].value = es_copy(REGEX_PART(header_match_value));
		return 0;
	}
}

int read_headers(HTTP_Message* message, FILE* connection)
{
	//Read up to the empty line.
	String headers = es_empty_string;
	#define RETURN(CODE) { es_free(&headers); return (CODE); }

	//Read all the headers, up to a blank line or MAX_HEADER_SIZE
	{
		String line = es_empty_string;
		do
		{
			es_free(&line);
			line = es_readline(connection, '\n', MAX_HEADER_SIZE);
			es_append(&headers, es_ref(&line));
		} while(!empty_line(es_ref(&line)) && headers.size <= MAX_HEADER_SIZE);

		es_free(&line);
	}

	//If all headers are too long, error
	if(headers.size > MAX_HEADER_SIZE) RETURN(too_long);

	//Parse headers
	int error = get_headers_recursive(message->num_headers, &message->headers,
		&message->num_headers, es_ref(&headers));
	if(error) RETURN(error)

	RETURN(0)
	#undef RETURN
}

static inline int read_fixed_body(HTTP_Message* message, FILE* connection,
	size_t size)
{
	if(size)
	{
		char* data = malloc(size);
		size_t amount_read = fread(data, 1, size, connection);
		if(amount_read != size)
		{
			free(data);
			return connection_error;
		}
		message->body = es_move_cstrn(data, size);
	}
	return 0;
}

static inline int read_chunked_body(HTTP_Message* message, FILE* connection)
{
	unsigned chunk_length = 0;
	String body = es_empty_string;
	char* read_buffer = 0;
	unsigned buffer_size = 0;
	#define RETURN(CODE) { es_free(&body); free(read_buffer); return (CODE); }

	do
	{
		String chunk_head = es_readline(connection, '\n', MAX_CHUNK_HEADER_SIZE);
		#define RETURN1(CODE) { es_free(&chunk_head); RETURN(CODE); }

		StringRef matches[chunk_num_matches];
		if(regex_match(&request_regex, matches, es_ref(&chunk_head),
				request_num_matches) == REG_NOMATCH)
			RETURN1(malformed_line)

		chunk_length = strtoul(es_ref(&chunk_head).begin, 0, 16);

		if(chunk_length > MAX_CHUNK_SIZE)
			RETURN1(too_long)

		if(chunk_length < buffer_size)
		{
			free(read_buffer);
			read_buffer = malloc(chunk_length);
			buffer_size = chunk_length;
		}

		unsigned amount_read = fread(read_buffer, 1, chunk_length, connection);
		if(amount_read != chunk_length)
			RETURN1(connection_error)

		es_append(&body, es_tempn(read_buffer, chunk_length));
		#undef RETURN1
		es_free(&chunk_head);
	} while(chunk_length != 0 && body.size <= MAX_BODY_SIZE);

	if(body.size > MAX_BODY_SIZE) RETURN(too_long)

	int header_error = read_headers(message, connection);
	if(header_error) RETURN(header_error)

	message->body = es_move(&body);
	RETURN(0)
	#undef RETURN
}


int read_body(HTTP_Message* message, FILE* connection)
{
	HTTP_Header* header;

	//Try chunked first. Ignore Content-Length
	//https://stackoverflow.com/questions/3304126/chunked-encoding-and-content-length-header
	header = find_header(message, es_temp("Transfer-Encoding"));
	if(header && es_compare(es_ref(&header->value), es_temp("chunked")))
		return read_chunked_body(message, connection);

	//Try content-length
	header = find_header(message, es_temp("Content-Length"));
	if(header)
	{
		//Extract content length
		unsigned long content_length = 0;
		if(es_toul(&content_length, es_ref(&header->value)))
			return bad_content_length;

		if(content_length > MAX_BODY_SIZE)
			return too_long;

		return read_fixed_body(message, connection, content_length);
	}

	//Nothing found. No body
	return 0;

}




