/*
 * autobuf.c
 *
 *  Created on: Feb 14, 2014
 *      Author: nathan
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "autobuf.h"

//Total allocated size
static inline size_t autobuf_allocated(const AutoBuffer* buffer)
{
	return buffer->storage_end - buffer->storage_begin;
}

//Offset of the read ptr
static inline size_t autobuf_offset(const AutoBuffer* buffer)
{
	return buffer->read_point - buffer->storage_begin;
}

//Available size to read
static inline size_t autobuf_available(const AutoBuffer* buffer)
{
	return buffer->storage_end - buffer->read_point;
}

//Reallocate the buffer; update pointers
static inline void realloc_autobuf(AutoBuffer* buffer, size_t resize)
{
	const size_t offset = autobuf_offset(buffer);
	buffer->storage_begin = realloc(buffer->storage_begin, resize);
	buffer->storage_end = buffer->storage_begin + resize;
	buffer->read_point = buffer->storage_begin + offset;
}

//Reset the read ptr to the beginning
static inline void reset_autobuf(AutoBuffer* buffer)
{
	buffer->read_point = buffer->storage_begin;
}

//Double allocated size of autobuf
static inline void upsize_autobuf(AutoBuffer* buffer)
{
	realloc_autobuf(buffer, autobuf_allocated(buffer) * 2);
}

const int read_line_eof = 1;
const int read_line_error = 2;

const static size_t initial_size = 256;

int autobuf_read_line(AutoBuffer* buffer, FILE* connection)
{
	reset_autobuf(buffer);

	if(buffer->storage_begin == 0 || buffer->storage_begin == buffer->storage_end)
		realloc_autobuf(buffer, initial_size);

	do
	{
		/*
		 * TODO: potentially add a predicate, to ensure incoming lines are
		 * valid. For instance, for the HTTP request, fail immediatly if an
		 * invalid character comes in.
		 */
		//Check for EOF
		if(feof(connection))
			return read_line_eof;

		//Check for other errors
		if(ferror(connection))
			return read_line_error;
	
		//check for end of space.
		if(autobuf_available(buffer) <= 1)
			upsize_autobuf(buffer);
			
		//Read off the stream
		fgets(buffer->read_point, autobuf_available(buffer), connection);

		//Advance the read pointer
		buffer->read_point += strlen(buffer->read_point);
		
		//Continue looping if we don't have a CR-LF
	} while(strcmp(buffer->read_point - 2, "\r\n"));

	return 0;
}
