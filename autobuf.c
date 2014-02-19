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

#include "config.h"

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

int autobuf_read_line(AutoBuffer* buffer, FILE* connection, const size_t max)
{
	reset_autobuf(buffer);

	if(buffer->storage_begin == 0 || buffer->storage_begin == buffer->storage_end)
		realloc_autobuf(buffer, autobuf_initial_size);

	do
	{
		//Check for EOF
		if(feof(connection))
			return autobuf_eof;

		//Check for other errors
		if(ferror(connection))
			return autobuf_error;

		//Check for too long
		if(autobuf_allocated(buffer) > max)
			return autobuf_too_long;
	
		//check for end of space.
		if(autobuf_available(buffer) <= 1)
			upsize_autobuf(buffer);
			
		//Read off the stream
		fgets(buffer->read_point, autobuf_available(buffer), connection);

		//Advance the read pointer
		buffer->read_point += strlen(buffer->read_point);
		
		//Continue looping if we don't have a newline.
	} while(buffer->read_point[-1] != '\n');

	return 0;
}
