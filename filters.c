/*
 * filters.c
 *
 *  Created on: Feb 18, 2014
 *      Author: nathan
 */

#include <string.h>
#include <stdlib.h>

#include "filters.h"

/*
 * Tokenizer: return a dynamically allocated array of dynamic strings, where
 * each is a dynaically allocated string. The basic design is that it runs
 * strtok over ".". With each token, it recurses to find the next token; the
 * tokens are copied into local calloc'd space. Once no more tokens are found,
 * the recursions depth is used to figure out how big to dynamically allocate
 * the char**. The recursion then unwinds, with each call assigning the correct
 * slot in the array. The resultant char** is null-terminated.
 */
static inline char** tokenize_recurse(char* string, int depth, char** saveptr)
{
	//Get the next token.
	char* next = strtok_r(string, ".", saveptr);

	//If the next token exists:
	if(next)
	{
		//Copy the token into malloc'd storage
		char* token = calloc(strlen(next) + 1, sizeof(char));
		strcpy(token, next);

		//Recurse, and get the token array
		char** token_array = tokenize_recurse(0, depth+1, saveptr);

		//Assign the token to the array
		token_array[depth] = token;

		//Return the array to the caller
		return token_array;
	}
	//If no token is found, create an array of appropriate size and return it.
	else
		return depth == 0 ? 0 : calloc(depth+1, sizeof(char*));
}

static inline char** tokenize(const char* string)
{
	//Create a local, mutable copy of the string to tokenize
	char* local_string = alloca((strlen(string)+1) * sizeof(char));
	strcpy(local_string, string);

	//Create a saveptr for the thread-safe version of strtok
	char* saveptr;

	//Execute the tokenization
	return tokenize_recurse(local_string, 0, &saveptr);
}

static inline size_t array_size(char** array)
{
	size_t result = 0;
	if(array)
		while(array[result] != 0)
			++result;
	return result;
}

void make_filter(Filter* filter, const char* filter_string)
{

}

bool prefix_match(const Filter* filter, const char* domain);
bool postfix_match(const Filter* filter, const char* domain);

inline static void clear_numeric_filter(Filter* filter)
{
	free(filter->numeric_components);
	filter->num_components = 0;
}

inline static void clear_word_filter(Filter* filter)
{
	for(int i = 0; i < filter->num_components; ++i)
		free(filter->word_components[i]);
	free(filter->word_components);
	filter->word_components = 0;
}

void clear_filter(Filter* filter)
{
	switch(filter->type)
	{
	case word:
		clear_word_filter(filter);
		break;
	case numeric:
		clear_numeric_filter(filter);
		break;
	}
}
