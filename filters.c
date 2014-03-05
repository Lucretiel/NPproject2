/*
 * filters.c
 *
 *  Created on: Mar 3, 2014
 *      Author: nathan
 */

#include <stdlib.h>

#include "filters.h"

typedef struct filter_node
{
	struct filter_node* next;
	String filter;
} FilterNode;

static FilterNode* filter_front;

void init_filters()
{
	filter_front = 0;
}

void filter_add(String filter)
{
	FilterNode* node = malloc(sizeof(FilterNode));
	node->next = filter_front;
	node->filter = es_tolower(es_ref(&filter));
	es_free(&filter);
	filter_front = node;
}

static inline bool filter_match(StringRef filter, StringRef domain)
{
	//If the filter is too long, it can't match
	if(filter.size > domain.size)
		return false;

	//If the filter is the same size, compare the whole thing
	else if(filter.size == domain.size)
		return es_compare(filter, domain) == 0;

	//Otherwise, do a prefix and postfix match
	else
	{
		//Compare the front
		if(es_compare(filter, es_slice(domain, 0, filter.size)) == 0
				&& domain.begin[filter.size] == '.')
			return true;

		//Compare the back
		size_t offset = domain.size - filter.size;
		if(es_compare(filter, es_slice(domain, offset, filter.size)) == 0
				&& domain.begin[offset - 1] == '.')
			return true;
	}
	return false;
}

bool filter_match_any(StringRef domain)
{
	String domain_lower = es_tolower(domain);
	StringRef domain_lower_ref = es_ref(&domain_lower);

	for(FilterNode* node = filter_front; node; node = node->next)
	{
		if(filter_match(es_ref(&node->filter), domain_lower_ref))
		{
			es_free(&domain_lower);
			return true;
		}
	}
	es_free(&domain_lower);
	return false;
}

void deinit_filters()
{
	while(filter_front)
	{
		FilterNode* node = filter_front;
		filter_front = filter_front->next;
		es_free(&node->filter);
		free(node);
	}
}

