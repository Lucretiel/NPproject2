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

static inline StringRef get_regex_part(const RegexMatches* matches, int part)
{
	return matches->matches[part];
}

#define REGEX_PART(PART) get_regex_part(&matches, (PART))

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


//All printed characters except colon
#define HEADER_NAME_CHARACTER CLASS("]a-z0-9[!\"#$%&'()*+,./:;<=>?@[\\\\^_`{|}~]\-")

//TODO: update this to support \r inline
#define HEADER_VALUE_CHARACTER CLASS("\t[:print:]")

//Full header regex string
#define HEADER_REGEX_STR \
	SUBMATCH( /* HEADER NAME: 1 */ \
		AT_LEAST_ONE(HEADER_NAME_CHARACTER)) \
	":" MANY(LWS) \
	SUBMATCH(  /* HEADER VALUE: 2 */ \
		AT_LEAST_ONE(HEADER_VALUE_CHARACTER) \
		MANY(SUBMATCH( \
			CR_LF \
			AT_LEAST_ONE(LWS) \
			AT_LEAST_ONE(HEADER_VALUE_CHARACTER)))) \
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

//TODO: find a way to share AutoBuffers between read calls, to reduce allocations
int read_request_line(HTTP_Message* message, FILE* connection)
{
	//read a line
	String line = es_readline(connection, '\n', MAX_MSG_LINE_SIZE);
	#define RETURN(CODE) { es_free(&line); return (CODE); }

	if(ferror(connection) || feof(connection)) RETURN(connection_error);
	if(es_cstr(&line)[line.size - 1] != '\n') RETURN(too_long);

	//TODO: check for regex out-of-memory error
	//Match the regex
	RegexMatches matches;
	if(regex_match(&request_regex, &matches, es_ref(&line),
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
	if(es_cstr(&line)[line.size - 1] != '\n') RETURN(too_long);

	//Match the response regex
	RegexMatches matches;
	if(regex_match(&request_regex, &matches, es_ref(&line),
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

int read_headers(HTTP_Message* message, FILE* connection)
{
	String headers = es_
}

int read_body(HTTP_Message* message, FILE* connection)
{
}




