/*
 * filter.c
 *
 *  Created on: Feb 18, 2014
 *      Author: nathan
 */

#include <string.h>
#include "filter.h"

typedef struct
{
	size_t size;
	const char* str;
} String;

//The maaaagic of function inlining
static inline bool prefix_match(const String filter, const String domain)
{
	return (strncasecmp(filter.str, domain.str, filter.size) == 0 &&
			(filter.size == domain.size || domain.str[filter.size] == '.'));
}

static inline bool postfix_match(const String filter, const String domain)
{
	const char* domain_offset = domain.str + (domain.size - filter.size);
	return (strncasecmp(filter.str, domain_offset, filter.size) == 0 &&
			(filter.size == domain.size || domain_offset[-1] == '.'));
}

bool filter_matches(const char* filter, const char* domain)
{
	String domain_str = { strlen(domain), domain };
	String filter_str = { strlen(filter), filter };

	return postfix_match(filter_str, domain_str) ||
			prefix_match(filter_str, domain_str);
}
