/*
 * autobuf.h
 *
 *  Created on: Feb 14, 2014
 *      Author: nathan
 *
 *  Autobuf contains facilities for
 */

#pragma once

//Read into an allocated, automatically sized buffer.

typedef struct
{
	char* storage_begin;
	char* storage_end;
	char* read_point;
} AutoBuffer;

/*
 * Not much interface needed. Initialize to init_autobuf when creating fresh,
 * then pass into readline. Free storage_begin when done.
 */
static const AutoBuffer init_autobuf;

enum
{
	autobuf_eof = 1,
	autobuf_error,
	autobuf_too_long
};
/*
 * Read a line up to LF, reallocating as necessary.
 */
int autobuf_read_line(AutoBuffer* buffer, FILE* connection, const size_t max);
