/*
 * http_read.c
 *
 *  Created on: Feb 11, 2014
 *      Author: nathan
 *
 *  Read and parse HTTP messages
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <regex.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "http.h"
#include "ReadableRegex/readable_regex.h"
#include "config.h"

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
				matches[i] = es_temp(0);
			else
				matches[i] = es_slice(str, local[i].rm_so,
					local[i].rm_eo - local[i].rm_so);
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
#define HEADER_NAME_CHARACTER CLASS("]a-z0-9[!\"#$%&'()*+,./;<=>?@[\\^_`{|}~-")

//TODO: update this to support \r inline
//#define HEADER_VALUE_CHARACTER CLASS("\t[:print:]")
#define HEADER_VALUE_CHARACTER CLASS("[:print:]")

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

#define HEADER_REGEX_STR \
	FRONT_ANCHOR( \
		SUBMATCH( AT_LEAST_ONE(HEADER_NAME_CHARACTER)) \
		":" MANY(LWS) \
		SUBMATCH(  /* HEADER VALUE: 2 */ \
			AT_LEAST_ONE(HEADER_VALUE_CHARACTER) \
			MANY(SUBMATCH( \
				CR_LF \
				AT_LEAST_ONE(LWS) \
				AT_LEAST_ONE(HEADER_VALUE_CHARACTER)))) \
			CR_LF)

#define CHUNK_REGEX_STR \
	FULL_ANCHOR( \
		SUBMATCH(AT_LEAST_ONE(CLASS("[:xdigit:]"))) /* SIZE FIELD: 1 */ \
		OPTIONAL(SUBMATCH(";" MANY(CLASS("^\n")))) \
		CR_LF )

//Indexes of the relevant subgroups
enum
{
	request_match_all,
	request_match_method=1,
	request_match_domain=3,
	request_match_path=5,
	request_match_version=7,
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
__attribute__((constructor (MODULE_HTTP_REGEX_PRI)))
void init_http_regex()
{
	if(DEBUG_PRINT) puts("Initializing HTTP regex");
	REGEX_COMPILE(&request_regex, REQUEST_REGEX_STR);
	REGEX_COMPILE(&response_regex, RESPONSE_REGEX_STR);
	REGEX_COMPILE(&header_regex, HEADER_REGEX_STR);
	REGEX_COMPILE(&chunk_regex, CHUNK_REGEX_STR);
}

//Uncompile regular expressions
__attribute__((destructor (MODULE_HTTP_REGEX_PRI)))
void deinit_http_regex()
{
	if(DEBUG_PRINT) puts("Clearing HTTP regex");
	regfree(&request_regex);
	regfree(&response_regex);
	regfree(&header_regex);
	regfree(&chunk_regex);
}


///////////////////////////////////////////////////////////////////////////////
// READS
///////////////////////////////////////////////////////////////////////////////

//GENERIC

//Perform a fixed-length TCP read
static inline int tcp_read_fixed(int fd, char* buffer, size_t size)
{
	return recv(fd, buffer, size, MSG_WAITALL) == size ? 0 : connection_error;
}

/*
 * Read up to delimiting character.
 *
 * Here's where we see the real advantage of a string library. Normally this
 * sort of code would involve an unpleasant amount of pointer manipulation,
 * but by using es_append we can use a static, stack-allocated local buffer
 * and let the library take care of automatic allocation. NO EXPLICT POINTERS.
 * Plus, es_append smartly doubles the allocation size every time, so the only
 * library overhead (besides increased memory use on the stack) is perhaps a
 * few too many memcpys
 */
const static size_t tcp_read_buffer_size = 256;
String tcp_read_line(int fd, char delim, size_t max)
{
	String result = es_empty_string;
	char c;
	int read_error = 0;

	do
	{
		char buffer[tcp_read_buffer_size];
		size_t amount_read = 0;

		for(size_t i = 0; i < tcp_read_buffer_size; ++i)
		{
			if((read_error = tcp_read_fixed(fd, &c, 1)))
				break;

			buffer[i] = c;
			++amount_read;
			--max;

			if(c == delim)
				break;
		}

		es_append(&result, es_tempn(buffer, amount_read));

	} while(c != delim && read_error == 0 && max);

	return result;
}

//FIXME: SO MUCH CODE REPITITION
//Especially the tcp_read_line error checking, http version, etc
int read_request_line(HTTP_Message* message, int connection)
{
	//read a line
	String line = tcp_read_line(connection, '\n', MAX_MSG_LINE_SIZE);
	#define RETURN(CODE) { es_free(&line); return (CODE); }

	//If the last character isn't a newline
	if(es_cstrc(&line)[line.size-1] != '\n')
	{
		//If we hit the max
		if(line.size >= MAX_MSG_LINE_SIZE)
			RETURN(too_long)
		//Otherwise, assume a connection error
		else
			RETURN(connection_error)
	}

	//Match the regex
	StringRef matches[request_num_matches];
	if(regex_match(&request_regex, matches, es_ref(&line),
			request_num_matches))
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
		if(es_compare(es_temp("1.1"), version) &&
				es_compare(es_temp("1.0"), version))
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

int read_response_line(HTTP_Message* message, int connection)
{
	//Read a line
	String line = tcp_read_line(connection, '\n', MAX_MSG_LINE_SIZE);
	#define RETURN(CODE) { es_free(&line); return (CODE); }

	//If the last character isn't a newline
	if(es_cstrc(&line)[line.size-1] != '\n')
	{
		//If we hit the max
		if(line.size >= MAX_MSG_LINE_SIZE) RETURN(too_long)

		//Otherwise, assume a connection error
		else RETURN(connection_error)
	}

	//Match the response regex
	StringRef matches[response_num_matches];
	if(regex_match(&response_regex, matches, es_ref(&line), response_num_matches))
		RETURN(malformed_line)

	//TODO: reduce code repitition between here and request
	//Get the HTTP Version
	{
		StringRef version = REGEX_PART(response_match_version);
		if(es_compare(es_temp("1.1"), version) && es_compare(es_temp("1.0"), version))
			RETURN(bad_version)
		else
			message->response.http_version = version.begin[2];
	}

	//Get status code
	StringRef status = REGEX_PART(response_match_status);
	message->response.status = strtol(status.begin, 0, 10);

	//Get the status phrase
	message->response.phrase = es_copy(REGEX_PART(response_match_phrase));

	RETURN(0);
	#undef RETURN
}

//True if a line is only '\r\n' or '\n'
static inline int empty_line(StringRef line)
{
	return es_compare(es_temp("\r\n"), line) == 0 ||
		es_compare(es_temp("\n"), line) == 0;
}

static inline int parse_headers(HTTP_Message* message, StringRef header_text)
{
	size_t header_i;
	for(header_i = 0; header_i < MAX_NUM_HEADERS; ++header_i)
	{
		//Done on empty line
		if(empty_line(header_text))
			break;

		//Match the next header
		StringRef matches[header_num_matches];
		if(regex_match(&header_regex, matches, header_text, header_num_matches))
			return malformed_line;

		//Add the header
		add_header(message, REGEX_PART(header_match_name),
			REGEX_PART(header_match_value));

		//Remove this header from the text
		header_text = es_slice(header_text, REGEX_PART(header_match_all).size,
			header_text.size);
	}

	//If there are too many headers, discard and return
	if(header_i >= MAX_NUM_HEADERS)
		return too_many_headers;

	return 0;
	#undef CLEAR_RETURN
}

int read_headers(HTTP_Message* message, int connection)
{
	String headers = es_empty_string;
	#define RETURN(CODE) { es_free(&headers); return (CODE); }

	//Read all the headers, up to a blank line or MAX_HEADER_SIZE
	int done = 0;
	do
	{
		//Read a line
		String line = tcp_read_line(connection, '\n', MAX_HEADER_LINE_SIZE);

		//If the last character isn't a newline, something went wrong
		if(es_cstrc(&line)[line.size-1] != '\n')
		{
			es_free(&line);

			//If we hit the max
			if(line.size >= MAX_HEADER_LINE_SIZE) RETURN(too_long)

			//Otherwise, assume a connection error
			else RETURN(connection_error)
		}

		//Append this line
		es_append(&headers, es_ref(&line));

		//If we found the empty line, we're done
		if(empty_line(es_ref(&line))) done = 1;

		//Free the line
		es_free(&line);

		//Continue while we haven't found the empty line, or gone over MAX
	} while(!done && headers.size <= MAX_HEADER_SIZE);

	//If all headers are too long, error
	if(headers.size > MAX_HEADER_SIZE) RETURN(too_long);

	//Parse headers
	int error = parse_headers(message, es_ref(&headers));

	RETURN(error)

	#undef RETURN
}

static inline int read_fixed_body(HTTP_Message* message, int connection, size_t size)
{
	//Real simple fixed size read
	if(size > MAX_BODY_SIZE)
		return too_long;

	else if(size)
	{
		char* buffer = malloc(size);
		if(tcp_read_fixed(connection, buffer, size))
		{
			free(buffer);
			return connection_error;
		}
		message->body = es_move_cstrn(buffer, size);
	}
	return 0;
}

static inline int read_chunked_body(HTTP_Message* message, int connection)
{
	//length of the next chunk
	size_t chunk_length = 0;

	//String to read into
	String body = es_empty_string;

	//String to read the chunk size lines into
	String chunk_head = es_empty_string;

	//buffer to read each chunk into. Reused, but reallocated if necessary
	char* read_buffer = 0;
	size_t buffer_size = 0;

	#define RETURN(CODE) { es_free(&body); es_free(&chunk_head);\
		free(read_buffer); return (CODE); }

	//Read each chunk till a length 0 chunk
	do
	{
		//Read the chunk length line
		chunk_head = tcp_read_line(connection, '\n', MAX_CHUNK_HEADER_SIZE);

		//If the last character isn't a newline
		if(es_cstrc(&chunk_head)[chunk_head.size-1] != '\n')
		{
			//If we hit the max
			if(chunk_head.size >= MAX_CHUNK_HEADER_SIZE)
				RETURN(too_long)
			//Otherwise, assume a connection error
			else
				RETURN(connection_error)
		}

		//Match the chunk length line
		StringRef matches[chunk_num_matches];
		if(regex_match(&chunk_regex, matches, es_ref(&chunk_head),
				chunk_num_matches))
			RETURN(malformed_line)

		//Get the chunk length. Remember- it's hex, not decimal
		chunk_length = strtoul(REGEX_PART(chunk_match_size).begin, 0, 16);

		//self explanatory
		if(chunk_length > MAX_CHUNK_SIZE) RETURN(too_long)

		//For the trailing \r\n
		size_t full_chunk_length = chunk_length + 2;

		//Reallocate buffer if needed
		if(full_chunk_length > buffer_size)
		{
			free(read_buffer);
			read_buffer = malloc(full_chunk_length);
			buffer_size = full_chunk_length;
		}

		//Read the chunk
		if(tcp_read_fixed(connection, read_buffer, full_chunk_length))
			RETURN(connection_error)

		//TODO: check that
		//Append the chunk
		es_append(&body, es_tempn(read_buffer, chunk_length));

		//Clear the chunk line
		es_clear(&chunk_head);

	//Repeat until MAX_BODY_SIZE or a length 0 chunk
	} while(chunk_length != 0 && body.size <= MAX_BODY_SIZE);

	//Error
	if(body.size > MAX_BODY_SIZE) RETURN(too_long)

	int header_error = read_headers(message, connection);
	if(header_error) RETURN(header_error)

	message->body = es_move(&body);
	RETURN(0)
	#undef RETURN
}

int read_body(HTTP_Message* message, int connection)
{
	const HTTP_Header* header;

	//Try chunked first. Ignore Content-Length
	//https://stackoverflow.com/questions/3304126/chunked-encoding-and-content-length-header
	header = find_header(message, es_temp("Transfer-Encoding"));
	if(header && es_compare(es_ref(&header->value), es_temp("chunked")) == 0)
		return read_chunked_body(message, connection);

	//Try content-length
	header = find_header(message, es_temp("Content-Length"));
	if(header)
	{
		//Extract content length
		unsigned long content_length = 0;
		if(es_toul(&content_length, es_ref(&header->value)))
			return bad_content_length;

		return read_fixed_body(message, connection, content_length);
	}

	//Nothing found. No body
	return 0;
}




