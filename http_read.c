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
	HTTP_Header* const back = message->headers + message->num_headers;
	for(HTTP_Header* header = message->headers; header < back; ++header)
		if(es_compare(header_name, es_ref(&header->name)) == 0)
			return header;
	return 0;
}

///////////////////////////////////////////////////////////////////////////////
// GENERIC REGEX LIBRARY
///////////////////////////////////////////////////////////////////////////////

//TODO: consider moving the generic stuff to a separate header and source file

/*
 * Struct linking group matches to a char*, to make finding submatches easier,
 * since the regmatch_t string only stores offsets
 */
const static int max_groups = 16;
typedef struct
{
	StringRef matches[max_groups];
} RegexMatches;

//Perform a match
inline static int regex_match(const regex_t* regex, RegexMatches* matches,
	StringRef str, int num_matches)
{
	regmatch_t local_matches[max_groups];
	int return_code = regexec(regex, str.begin, num_matches, local_matches, 0);
	if(return_code == 0)
	{
		for(int i = 0; i < num_matches; ++i)
			if(local_matches[i].rm_so == -1)
				matches->matches[i] = es_null_ref;
			else
				matches->matches[i] = es_slice(
					str, local_matches[i].rm_so, local_matches[i].rm_eo);
	}
	return return_code;
}

///////////////////////////////////////////////////////////////////////////////
// HTTP REGEX LIBRARY
///////////////////////////////////////////////////////////////////////////////

//HTTP specific regex stuff

//Note that regexes are compiled to be case-insensitive
/*
 * Note to the reader: POSIX ERE are... not fun. They don't support non-
 * capturing groups (?:...), lazy captures (*?, +?), or character class set
 * operations (subtraction: [a-z-[aeiou]]. The lack of non-capturing groups is
 * especially problematic, as it means that we have to keep track of group
 * numbers.
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
		SUBMATCH("%[0-9a-f]{2}"))) /* PERCENT ENCODED CHARACTER */

//As above, without slash
#define URI_DOMAIN_CHARACTER \
	SUBMATCH( EITHER( \
		CLASS("]a-z0-9._~:?#[@!$&'()*+,;=-"), \
		SUBMATCH("%[0-9a-f]{2}")))

//All characters except colon and whitespace
#define HEADER_NAME_CHARACTER "[^:[:space:]]"

//TODO: update this to support \r inline
#define HEADER_VALUE_CHARACTER "[^\r\n]"
#define HEADER_VALUE AT_LEAST_ONE(HEADER_VALUE_CHARACTER)

//Full request regex string
#define REQUEST_REGEX_STR \
	FULL_ANCHOR( \
		SUBMATCH(AT_LEAST_ONE("[A-Z]")) /* METHOD: index 1 */ \
		AT_LEAST_ONE(LWS) \
		OPTIONAL(SUBMATCH("http://" \
			SUBMATCH(MANY(URI_DOMAIN_CHARACTER)))) /* DOMAIN: index 3 */ \
		"/" SUBMATCH(MANY(URI_PATH_CHARACTER)) /* PATH: index 6 */ \
		AT_LEAST_ONE(LWS) \
		HTTP_VERSION /* HTTP VERSION: index 9 */ \
		CR_LF)

//Full response regex string
#define RESPONSE_REGEX_STR \
	FULL_ANCHOR( HTTP_VERSION /* HTTP VERSION: index 1*/ \
	AT_LEAST_ONE(LWS) \
	SUBMATCH("[1-5][0-9][0-9]") /* RESPONSE CODE: index 2*/ \
	AT_LEAST_ONE(LWS) \
	SUBMATCH(MANY("[[:print:]]")) /* REASON PHRASE: index 3 */ \
	CR_LF )

//Full header regex string
#define HEADER_REGEX_STR \
	SUBMATCH(MANY(HEADER_NAME_CHARACTER)) /* HEADER NAME: 1 */ \
	":" MANY(LWS) \
	SUBMATCH( /* HEADER VALUE: 2 */ \
		HEADER_VALUE \
		MANY(SUBMATCH(CR_LF AT_LEAST_ONE(LWS) HEADER_VALUE))) \
		CR_LF

//Indexes of the relevant subgroups
enum
{
	request_match_all=0,
	request_match_method=1,
	request_match_domain=3,
	request_match_path=6,
	request_match_version=9,
	request_num_matches
} request_match_which;

enum
{
	response_match_all=0,
	response_match_version=1,
	response_match_status=2,
	response_match_phrase=3,
	response_num_matches
} response_match_which;

enum
{
	header_match_all=0,
	header_match_content=1,
	header_match_value = 2,
	header_num_matches
} header_match_which;

//Globals to store the compiled regexes
static regex_t request_regex; //matches the request line
static regex_t response_regex; //matches the response line
static regex_t header_regex; //matches a single header line

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
}

//Uncompile regular expressions
void deinit_http()
{
	regfree(&request_regex);
	regfree(&response_regex);
	regfree(&header_regex);
}


///////////////////////////////////////////////////////////////////////////////
// READS
///////////////////////////////////////////////////////////////////////////////

static inline StringRef get_regex_part(const RegexMatches* matches, int part)
{
	return matches->matches[part];
}

//True if the method in the request matches the method given. Pass in lowercase
static inline int is_method(const char* method, const RegexMatches* matches)
{
	String str = es_tolower(get_regex_part(matches, request_match_method));
	int result = es_compare(es_ref(&str), es_temp(method));
	es_free(&str);
	return result == 0;
}

//TODO: find a way to share AutoBuffers between read calls, to reduce allocations
int read_request_line(HTTP_Message* message, FILE* connection)
{
	//read a line
	String line = es_readanyline(connection, '\n');
	#define RETURN(CODE) { es_free(&line); return (CODE); }

	if(ferror(connection) || feof(connection)) RETURN(connection_error)

	//TODO: check for regex out-of-memory error
	//Match the regex
	RegexMatches matches;
	if(regex_match(&request_regex, &matches, es_ref(&line),
			request_num_matches) == REG_NOMATCH)
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
	StringRef version = get_regex_part(&matches, request_match_version);
	if(es_compare(es_temp("1.1"), version) && es_compare(es_temp("1.0"), version))
		RETURN(bad_version)
	else
		message->request.http_version = version.begin[2];

	//Get the domain
	message->request.domain = es_copy(get_regex_part(&matches, request_match_domain));

	//Get the path
	message->request.path = es_copy(get_regex_part(&matches, request_match_path));

	RETURN(0);
	#undef RETURN
}

int read_response_line(HTTP_Message* message, FILE* connection)
{
	clean_message(message);

	String line = string_read_line(connection, '\n');
	#define RETURN(CODE) { string_free(&line); return (CODE); }

	//Read up to a CR_LF, autoallocating as nessesary
	if(ferror(connection) || feof(connection)) RETURN(connection_error)

	//Match the response regex
	RegexMatches matches = {0};
	if(regex_match(&request_regex, &matches, string_temp(&line),
			response_num_matches) == REG_NOMATCH)
		RETURN(malformed_line)

	//TODO: reduce code repitition between here and request
	//Verify and get the HTTP version
	//Must be 1.0 or 1.1
	if(string_compare_cstr(
			"1.0", matches->matches[request_match_version]) == 0
		|| string_compare_cstr(
			"1.1", matches->matches[request_match_version]) == 0)
	{
		message->request.http_version =
			string_cstr(&matches->matches[request_match_version])[2];
	}
	else
		RETURN(bad_version)

	//Get status code
	message->response.status = strtol(
			match_begin(&matches, response_match_status), 0, 10);

	//Get the status phrase
	//TODO: Pick from a table instead?
	message->response.phrase = string_copy(
		&matches->matches[response_match_phrase]);

	RETURN(0);
	#undef RETURN
}

int read_body(HTTP_Message* message, FILE* connection)
{
	//TODO: chunked encoding
	//TODO: keepalive etc
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




