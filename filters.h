/*
 * filters.h
 *
 *  Created on: Feb 15, 2014
 *      Author: nathan
 */

#pragma once

#include <stdbool.h>

typedef enum { word, numeric } FilterType;

typedef char* WordComponent;
typedef int NumericComponent;

typedef struct
{
	FilterType type;
	int num_components;
	union
	{
		WordComponent* word_components;
		NumericComponent* numeric_components;
	};
} Filter;

/*
 * Dear client: these functions all expect SAFE "." separated strings. It's
 * your responsibility to supply these.
 */
void make_filter(Filter* filter, const char* filter_string);

bool prefix_match(const Filter* filter, const char* domain);
bool postfix_match(const Filter* filter, const char* domain);

static inline bool filter_match(const Filter* filter, const char* domain)
{ return prefix_match(filter, domain) || postfix_match(filter, domain); }

void clear_filter(Filter* filter);
